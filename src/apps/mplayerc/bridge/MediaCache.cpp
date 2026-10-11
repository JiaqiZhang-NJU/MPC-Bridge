// SPDX-License-Identifier: GPL-3.0-or-later
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include "MediaCache.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cwctype>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "bcrypt.lib")

namespace Bridge {
namespace {
constexpr std::uint64_t Chunk = 1024 * 1024;
constexpr std::uint64_t GlobalQuota = 1024ull * 1024 * 1024;
using Clock = std::chrono::steady_clock;
struct RootReservation {
    HANDLE handle = nullptr; bool acquired = false;
    explicit RootReservation(const std::filesystem::path& root) {
        auto key = std::filesystem::absolute(root).lexically_normal().wstring();
        std::uint64_t hash = 14695981039346656037ull;
        for (auto c : key) { hash ^= std::towlower(c); hash *= 1099511628211ull; }
        const auto name = L"Local\\MPC-Bridge-Cache-" + std::to_wstring(hash);
        handle = CreateMutexW(nullptr, FALSE, name.c_str());
        if (handle) { const auto result = WaitForSingleObject(handle, 5000); acquired = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED; }
    }
    ~RootReservation() { if (acquired) ReleaseMutex(handle); if (handle) CloseHandle(handle); }
};
std::string Token() {
    unsigned char bytes[16];
    if (BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG)) return {};
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (auto c : bytes) { result += digits[c >> 4]; result += digits[c & 15]; }
    return result;
}
std::wstring Query(HINTERNET handle, DWORD field) {
    DWORD size = 0;
    WinHttpQueryHeaders(handle, field, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &size, WINHTTP_NO_HEADER_INDEX);
    if (!size || size > 32768) return {};
    std::wstring result(size / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(handle, field, WINHTTP_HEADER_NAME_BY_INDEX, result.data(), &size, WINHTTP_NO_HEADER_INDEX)) return {};
    result.resize(size / sizeof(wchar_t));
    while (!result.empty() && result.back() == L'\0') result.pop_back();
    return result;
}
bool ContentRange(const std::wstring& text, ByteRange expected, std::uint64_t& length) {
    unsigned long long first = 0, last = 0, total = 0; wchar_t tail = 0;
    if (swscanf_s(text.c_str(), L"bytes %llu-%llu/%llu%c", &first, &last, &total, &tail, 1u) != 3) return false;
    if (!total || first != expected.first || last != expected.last || last >= total) return false;
    length = total; return true;
}
struct Request {
    std::mutex mutex;
    HINTERNET handle = nullptr;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE closed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::atomic<DWORD> status{0}, read{0};
    ~Request() { Close(); WaitForSingleObject(closed, INFINITE); CloseHandle(event); CloseHandle(closed); }
    void Close() {
        std::lock_guard<std::mutex> lock(mutex);
        if (handle) { auto old = handle; handle = nullptr; WinHttpCloseHandle(old); }
        else if (!status.load()) SetEvent(closed);
        SetEvent(event);
    }
    static void CALLBACK Callback(HINTERNET, DWORD_PTR context, DWORD notice, void*, DWORD size) {
        auto self = reinterpret_cast<Request*>(context);
        if (!self) return;
        if (notice == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) { SetEvent(self->closed); return; }
        if (notice == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) self->read = size;
        if (notice == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE || notice == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE
            || notice == WINHTTP_CALLBACK_STATUS_READ_COMPLETE || notice == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
            self->status = notice; SetEvent(self->event);
        }
    }
    template<class F> bool Step(DWORD notice, Clock::time_point deadline, F function) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!handle) return false;
            status = 1; ResetEvent(event);
            if (!function(handle) && GetLastError() != ERROR_IO_PENDING) return false;
        }
        const auto remain = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (remain <= 0 || WaitForSingleObject(event, static_cast<DWORD>(remain)) != WAIT_OBJECT_0) return false;
        return status == notice;
    }
};
struct Response {
    std::uint64_t length = 0;
    std::wstring validator;
    std::vector<std::uint8_t> bytes;
};
bool SendAll(SOCKET socket, const void* data, std::size_t size) {
    auto p = static_cast<const char*>(data);
    while (size) {
        const auto sent = send(socket, p, static_cast<int>(std::min<std::size_t>(size, 65536)), 0);
        if (sent <= 0) return false;
        p += sent; size -= sent;
    }
    return true;
}
}

struct MediaCache::Impl {
    struct Resource {
        std::wstring source, agent, referer, route, validator;
        std::uint64_t id = 0, length = 0;
        std::atomic<std::uint64_t> cursor{0};
        std::timed_mutex mutex; // deduplicates fetches for a representation
        std::vector<MediaTrackIndex> index;
        std::atomic<bool> indexed{false};
        std::atomic<bool> indexing{false};
    };
    struct Block { std::filesystem::path path; std::uint64_t size, use; bool complete = true; std::uint64_t charge = 0; };
    CacheSettings settings;
    std::filesystem::path directory;
    HANDLE owner = INVALID_HANDLE_VALUE;
    bool storage = false, ownsDirectory = false, winsockStarted = false;
    std::string token;
    std::atomic<SOCKET> listener{INVALID_SOCKET};
    std::uint16_t port = 0;
    std::atomic<bool> stopping{false};
    std::atomic<bool> failed{false};
    std::atomic<unsigned> foreground{0};
    std::atomic<std::uint64_t> generation{0}, hits{0}, requests{0};
    std::atomic<std::uint64_t> readerEpoch{0};
    std::mutex mutex, storeMutex, networkMutex, socketMutex;
    std::condition_variable wake, clientWake, slots;
    std::map<std::wstring, std::shared_ptr<Resource>> resources;
    std::vector<std::shared_ptr<Resource>> selected;
    std::map<std::pair<std::uint64_t, std::uint64_t>, Block> blocks;
    std::uint64_t bytes = 0, clock = 0;
    std::uint64_t allocationUnit = 65536;
    std::map<Request*, std::pair<std::shared_ptr<Request>, bool>> active;
    unsigned connections = 0;
    std::atomic<std::size_t> indexBytes{0};
    std::deque<SOCKET> pending;
    std::set<SOCKET> clients;
    std::thread acceptThread, preloadThread;
    std::vector<std::thread> workers;
    double position = 0, rate = 1;
    bool paused = false, capped = false, quiescing = false;

    explicit Impl(CacheSettings config) : settings(std::move(config)) {
        settings.forwardSeconds = std::clamp(settings.forwardSeconds, 1, 120);
        settings.backSeconds = std::clamp(settings.backSeconds, 0, 120);
        settings.diskBytes = std::clamp<std::uint64_t>(settings.diskBytes, 16 * Chunk, GlobalQuota);
        if (!settings.enabled) return;
        token = Token(); if (token.empty()) return;
        std::filesystem::path root = settings.directory;
        if (root.empty()) {
            wchar_t local[32768];
            if (!GetEnvironmentVariableW(L"LOCALAPPDATA", local, _countof(local))) return;
            root = std::filesystem::path(local) / L"MPC-Bridge" / L"Cache";
        }
        std::error_code error;
        std::filesystem::create_directories(root, error); if (error) return;
        wchar_t volume[32768]; DWORD sectors, sectorBytes, freeClusters, clusters;
        if (GetVolumePathNameW(std::filesystem::absolute(root).c_str(), volume, _countof(volume))
            && GetDiskFreeSpaceW(volume, &sectors, &sectorBytes, &freeClusters, &clusters)) allocationUnit = std::uint64_t(sectors) * sectorBytes;
        if (!allocationUnit) allocationUnit = 65536;
        RootReservation reservation(root); if (!reservation.acquired) return;
        // Global quota uses a conservative per-process reservation. Sharing violations mean an active owner.
        std::uint64_t reserved = 0;
        for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
            if (error) break;
            const auto name = entry.path().filename().wstring();
            if (name.rfind(L"session-", 0) || name.size() != 40 || entry.is_symlink(error)
                || !std::all_of(name.begin() + 8, name.end(), [](wchar_t c) { return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'); })) continue;
            auto marker = entry.path() / L"owner.lock";
            auto file = CreateFileW(marker.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
            if (file == INVALID_HANDLE_VALUE) {
                if (GetLastError() == ERROR_SHARING_VIOLATION) reserved += GlobalQuota / 4;
                continue;
            }
            FILETIME created{}, accessed{}, modified{}; GetFileTime(file, &created, &accessed, &modified); CloseHandle(file);
            ULARGE_INTEGER time{}; time.LowPart = modified.dwLowDateTime; time.HighPart = modified.dwHighDateTime;
            FILETIME now{}; GetSystemTimeAsFileTime(&now);
            ULARGE_INTEGER current{}; current.LowPart = now.dwLowDateTime; current.HighPart = now.dwHighDateTime;
            if (current.QuadPart > time.QuadPart + 24ull * 3600 * 10000000) {
                // Only our token-shaped directory with the exact private marker is eligible.
                std::ifstream check(marker, std::ios::binary); std::string signature; std::getline(check, signature); check.close();
                if (signature == "MPC-Bridge cache v1") std::filesystem::remove_all(entry.path(), error);
            }
            if (std::filesystem::exists(entry.path(), error)) {
                for (const auto& block : std::filesystem::directory_iterator(entry.path(), error)) {
                    if (error) break;
                    if (block.is_regular_file(error)) { const auto size = block.file_size(error); if (!error) reserved += ((size + allocationUnit - 1) / allocationUnit) * allocationUnit; }
                }
            }
        }
        if (reserved >= GlobalQuota || GlobalQuota - reserved < 16 * Chunk) return;
        settings.diskBytes = std::min({settings.diskBytes, GlobalQuota / 4, GlobalQuota - reserved});
        directory = root / (L"session-" + std::wstring(token.begin(), token.end()));
        ownsDirectory = std::filesystem::create_directory(directory, error); if (error || !ownsDirectory) return;
        auto marker = directory / L"owner.lock";
        owner = CreateFileW(marker.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (owner == INVALID_HANDLE_VALUE) return;
        const char signature[] = "MPC-Bridge cache v1\n"; DWORD wrote = 0;
        storage = WriteFile(owner, signature, sizeof(signature) - 1, &wrote, nullptr) && wrote == sizeof(signature) - 1;
        if (!storage) return;
        bytes = allocationUnit;
        WSADATA data{}; if (WSAStartup(MAKEWORD(2, 2), &data)) return;
        winsockStarted = true;
        listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == INVALID_SOCKET) return;
        BOOL exclusive = TRUE; setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<char*>(&exclusive), sizeof(exclusive));
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(listener.load(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) || listen(listener, 8)) return;
        int size = sizeof(address); getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size); port = ntohs(address.sin_port);
        acceptThread = std::thread([this] { Accept(); });
        for (int i = 0; i < 4; ++i) workers.emplace_back([this] { Worker(); });
        preloadThread = std::thread([this] { Preload(); });
    }
    void Abort(bool all) {
        std::lock_guard<std::mutex> lock(networkMutex);
        for (auto& item : active) if (all || !item.second.second) item.second.first->Close();
        slots.notify_all(); wake.notify_all();
    }
    void Cancel() {
        if (stopping.exchange(true)) return;
        Abort(true);
        const auto server = listener.exchange(INVALID_SOCKET);
        if (server != INVALID_SOCKET) { shutdown(server, SD_BOTH); closesocket(server); }
        { std::lock_guard<std::mutex> lock(socketMutex); for (auto client : clients) shutdown(client, SD_BOTH); }
        wake.notify_all(); clientWake.notify_all(); slots.notify_all();
    }
    ~Impl() {
        Cancel();
        if (acceptThread.joinable()) acceptThread.join();
        for (auto& thread : workers) thread.join();
        if (preloadThread.joinable()) preloadThread.join();
        if (owner != INVALID_HANDLE_VALUE) CloseHandle(owner);
        if (ownsDirectory) { std::error_code error; std::filesystem::remove_all(directory, error); }
        if (winsockStarted) WSACleanup();
    }
    bool Fetch(const Resource& resource, ByteRange range, bool demand, Response& result,
        Clock::time_point deadline = Clock::now() + std::chrono::seconds(8)) {
        const auto requestGeneration = generation.load();
        {
            std::unique_lock<std::mutex> lock(networkMutex);
            if (!slots.wait_until(lock, deadline, [&] { return stopping || (!demand && requestGeneration != generation)
                || (connections < 2 && (demand || !foreground)); })) return false;
            if (stopping || (!demand && requestGeneration != generation)) return false;
            ++connections;
        }
        struct Slot { Impl* self; ~Slot() { std::lock_guard<std::mutex> lock(self->networkMutex); --self->connections; self->slots.notify_all(); } } slot{this};
        URL_COMPONENTS parts{sizeof(parts)}; parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = DWORD(-1);
        if (!WinHttpCrackUrl(resource.source.c_str(), 0, 0, &parts)
            || (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS)
            || parts.dwUserNameLength || parts.dwPasswordLength) return false;
        std::wstring host(parts.lpszHostName, parts.dwHostNameLength), path(parts.lpszUrlPath, parts.dwUrlPathLength);
        if (parts.dwExtraInfoLength) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        if (path.empty()) path = L"/";
        const bool local = host == L"127.0.0.1" || host == L"localhost" || host == L"::1";
        WINHTTP_CURRENT_USER_IE_PROXY_CONFIG proxy{};
        WinHttpGetIEProxyConfigForCurrentUser(&proxy);
        DWORD access = local ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY;
        if (!local && !proxy.fAutoDetect && !proxy.lpszAutoConfigUrl) access = proxy.lpszProxy ? WINHTTP_ACCESS_TYPE_NAMED_PROXY : WINHTTP_ACCESS_TYPE_NO_PROXY;
        auto session = WinHttpOpen(resource.agent.empty() ? L"MPC-Bridge/0.2" : resource.agent.c_str(), access,
            access == WINHTTP_ACCESS_TYPE_NAMED_PROXY ? proxy.lpszProxy : WINHTTP_NO_PROXY_NAME,
            access == WINHTTP_ACCESS_TYPE_NAMED_PROXY ? proxy.lpszProxyBypass : WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
        if (proxy.lpszProxy) GlobalFree(proxy.lpszProxy);
        if (proxy.lpszProxyBypass) GlobalFree(proxy.lpszProxyBypass);
        if (proxy.lpszAutoConfigUrl) GlobalFree(proxy.lpszAutoConfigUrl);
        if (!session) return false;
        WinHttpSetTimeouts(session, 5000, 5000, 5000, 5000);
        auto connection = WinHttpConnect(session, host.c_str(), parts.nPort, 0);
        auto request = std::make_shared<Request>();
        request->handle = connection ? WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
        bool ok = false;
        if (request->handle) {
            WinHttpSetStatusCallback(request->handle, Request::Callback, WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES, 0);
            DWORD_PTR context = reinterpret_cast<DWORD_PTR>(request.get());
            WinHttpSetOption(request->handle, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context));
            DWORD disable = WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
            WinHttpSetOption(request->handle, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable));
            { std::lock_guard<std::mutex> lock(networkMutex); active.emplace(request.get(), std::make_pair(request, demand)); }
            std::wstring headers = L"Accept-Encoding: identity\r\nRange: bytes=" + std::to_wstring(range.first) + L"-" + std::to_wstring(range.last) + L"\r\n";
            if (resource.referer.find_first_of(L"\r\n") == resource.referer.npos && !resource.referer.empty()) headers += L"Referer: " + resource.referer + L"\r\n";
            if (!resource.validator.empty()) headers += L"If-Range: " + resource.validator + L"\r\n";
            ++requests;
            ok = !stopping && (demand || requestGeneration == generation)
                && request->Step(WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, deadline, [&](HINTERNET h) {
                    return WinHttpSendRequest(h, headers.c_str(), static_cast<DWORD>(headers.size()), nullptr, 0, 0, context); })
                && request->Step(WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, deadline, [](HINTERNET h) { return WinHttpReceiveResponse(h, nullptr); });
            if (ok) {
                std::lock_guard<std::mutex> lock(request->mutex);
                if (!request->handle) ok = false;
                else {
                    DWORD status = 0, size = sizeof(status);
                    WinHttpQueryHeaders(request->handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
                    result.validator = Query(request->handle, WINHTTP_QUERY_ETAG);
                    if (result.validator.empty()) result.validator = Query(request->handle, WINHTTP_QUERY_LAST_MODIFIED);
                    const auto contentLength = Query(request->handle, WINHTTP_QUERY_CONTENT_LENGTH);
                    std::uint64_t declared = 0;
                    const auto encoding = Query(request->handle, WINHTTP_QUERY_CONTENT_ENCODING);
                    ok = status == 206 && Decimal(std::string(contentLength.begin(), contentLength.end()), declared)
                        && declared == range.last - range.first + 1 && (encoding.empty() || encoding == L"identity")
                        && ContentRange(Query(request->handle, WINHTTP_QUERY_CONTENT_RANGE), range, result.length)
                        && (resource.length == 0 || resource.length == result.length)
                        && (resource.validator.empty() || resource.validator == result.validator);
                }
            }
            if (ok) {
                result.bytes.resize(static_cast<std::size_t>(range.last - range.first + 1));
                std::size_t offset = 0;
                while (ok && offset < result.bytes.size()) {
                    const DWORD count = static_cast<DWORD>(std::min<std::size_t>(65536, result.bytes.size() - offset));
                    ok = request->Step(WINHTTP_CALLBACK_STATUS_READ_COMPLETE, deadline, [&](HINTERNET h) {
                        return WinHttpReadData(h, result.bytes.data() + offset, count, nullptr); });
                    if (!request->read) { ok = false; break; }
                    offset += request->read;
                }
            }
            request->Close();
            WaitForSingleObject(request->closed, INFINITE);
            { std::lock_guard<std::mutex> lock(networkMutex); active.erase(request.get()); }
        }
        request.reset();
        if (connection) WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        if (!ok) result.bytes.clear();
        return ok;
    }
    bool Covered(std::uint64_t id, ByteRange range) {
        std::lock_guard<std::mutex> lock(storeMutex);
        for (auto c = range.first / Chunk; c <= range.last / Chunk; ++c) {
            auto block = blocks.find({id, c}); if (block == blocks.end() || !block->second.complete) return false;
        }
        return true;
    }
    bool ReadBlock(const Resource& resource, std::uint64_t chunk, std::vector<std::uint8_t>& result) {
        std::lock_guard<std::mutex> lock(storeMutex);
        auto found = blocks.find({resource.id, chunk}); if (found == blocks.end()) return false;
        if (!found->second.complete) return false;
        std::ifstream file(found->second.path, std::ios::binary);
        result.resize(static_cast<std::size_t>(found->second.size));
        if (!file.read(reinterpret_cast<char*>(result.data()), result.size())) {
            result.clear(); std::error_code error;
            found->second.complete = false;
            if (std::filesystem::remove(found->second.path, error) && !error) { bytes -= found->second.charge; blocks.erase(found); }
            return false;
        }
        found->second.use = ++clock; ++hits; return true;
    }
    bool Put(const Resource& resource, std::uint64_t chunk, const std::vector<std::uint8_t>& data) {
        std::lock_guard<std::mutex> lock(storeMutex);
        const auto key = std::make_pair(resource.id, chunk);
        if (blocks.count(key)) return blocks.at(key).complete;
        const auto charge = ((data.size() + allocationUnit - 1) / allocationUnit) * allocationUnit;
        while (bytes + charge > settings.diskBytes && !blocks.empty()) {
            auto victim = std::min_element(blocks.begin(), blocks.end(), [](const auto& a, const auto& b) { return a.second.use < b.second.use; });
            std::error_code error; if (!std::filesystem::remove(victim->second.path, error) || error) return false;
            bytes -= victim->second.charge; blocks.erase(victim);
        }
        auto path = directory / (std::to_wstring(resource.id) + L"-" + std::to_wstring(chunk) + L".block");
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(data.data()), data.size()); file.close();
        if (!file) {
            std::error_code error;
            std::filesystem::remove(path, error);
            if (error) {
                const auto size = std::filesystem::file_size(path, error);
                if (!error) { const auto leftover = ((size + allocationUnit - 1) / allocationUnit) * allocationUnit;
                    blocks.emplace(key, Block{path, size, ++clock, false, leftover}); bytes += leftover; }
            }
            return false;
        }
        blocks.emplace(key, Block{path, data.size(), ++clock, true, charge}); bytes += charge; return true;
    }
    bool Read(Resource& resource, std::uint64_t offset, std::size_t count, bool demand, std::vector<std::uint8_t>& result,
        Clock::time_point deadline = Clock::now() + std::chrono::seconds(8)) {
        if (offset >= resource.length || count > resource.length - offset || count > 16 * Chunk || stopping || failed) return false;
        if (demand) resource.cursor = offset;
        const auto epoch = readerEpoch.load();
        // A completed cache hit needs neither a network slot nor cancellation of metadata work.
        if (count <= Chunk - offset % Chunk) {
            std::vector<std::uint8_t> block;
            if (ReadBlock(resource, offset / Chunk, block)) {
                const auto start = static_cast<std::size_t>(offset % Chunk);
                result.assign(block.begin() + start, block.begin() + start + count); return true;
            }
        }
        if (demand) { ++foreground; Abort(false); }
        struct Priority { Impl* self; bool demand; ~Priority() { if (demand) { --self->foreground; self->slots.notify_all(); } } } priority{this, demand};
        std::unique_lock<std::timed_mutex> lock(resource.mutex, std::defer_lock);
        if (!lock.try_lock_until(deadline)) return false;
        if (demand && epoch != readerEpoch) return false;
        if (!demand && foreground) return false;
        result.clear(); result.reserve(count);
        const auto end = offset + count;
        while (offset < end && !stopping && (!demand || epoch == readerEpoch)) {
            const auto chunk = offset / Chunk;
            std::vector<std::uint8_t> data;
            if (!ReadBlock(resource, chunk, data)) {
                Response response;
                if (!Fetch(resource, {chunk * Chunk, std::min(resource.length, (chunk + 1) * Chunk) - 1}, demand, response, deadline)) {
                    if (demand && !stopping && epoch == readerEpoch) failed = true;
                    return false;
                }
                data = std::move(response.bytes);
                if (!Put(resource, chunk, data)) { failed = true; return false; }
            }
            const auto start = static_cast<std::size_t>(offset % Chunk);
            const auto size = static_cast<std::size_t>(std::min<std::uint64_t>(end - offset, data.size() - start));
            result.insert(result.end(), data.begin() + start, data.begin() + start + size); offset += size;
        }
        return result.size() == count;
    }
    std::wstring Register(const std::wstring& source, const std::wstring& agent, const std::wstring& referer) {
        if (!port || stopping) return {};
        { std::lock_guard<std::mutex> lock(mutex); for (const auto& item : resources) if (item.second->source == source) return item.first; }
        auto resource = std::make_shared<Resource>(); resource->source = source; resource->agent = agent; resource->referer = referer;
        Response response;
        ++foreground; Abort(false);
        const auto ok = Fetch(*resource, {0, 0}, true, response); --foreground; slots.notify_all();
        if (!ok || !response.length) return {};
        resource->length = response.length; resource->validator = response.validator;
        const auto routeToken = Token(); if (routeToken.empty()) return {};
        std::lock_guard<std::mutex> lock(mutex);
        resource->id = resources.size() + 1;
        resource->route = L"http://127.0.0.1:" + std::to_wstring(port) + L"/" + std::wstring(token.begin(), token.end())
            + L"/" + std::wstring(routeToken.begin(), routeToken.end()) + L"/media.mp4";
        resources.emplace(resource->route, resource);
        return resource->route;
    }
    void Accept() {
        const auto server = listener.load();
        while (!stopping) {
            auto client = accept(server, nullptr, nullptr); if (client == INVALID_SOCKET) break;
            DWORD timeout = 8000; setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));
            setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));
            std::lock_guard<std::mutex> lock(socketMutex);
            if (stopping || clients.size() >= 16) closesocket(client);
            else { clients.insert(client); pending.push_back(client); clientWake.notify_all(); }
        }
    }
    void Worker() {
        for (;;) {
            SOCKET client;
            {
                std::unique_lock<std::mutex> lock(socketMutex);
                clientWake.wait(lock, [&] { return stopping || !pending.empty(); });
                if (pending.empty()) return;
                client = pending.front(); pending.pop_front();
            }
            if (!stopping) Serve(client);
            { std::lock_guard<std::mutex> lock(socketMutex); clients.erase(client); closesocket(client); }
        }
    }
    bool StreamAllowed(Resource& resource, std::uint64_t offset) {
        double now, speed;
        { std::lock_guard<std::mutex> lock(mutex); now = position; speed = rate; }
        std::unique_lock<std::timed_mutex> lock(resource.mutex, std::try_to_lock);
        if (!lock.owns_lock()) return false;
        if (resource.index.empty()) return true;
        const auto chunk = offset / Chunk;
        for (const auto& track : resource.index) {
            for (const auto& range : track.Window(std::max(0.0, now - settings.backSeconds), now + ForwardTarget(speed, settings.forwardSeconds)))
                if (chunk >= range.first / Chunk && chunk <= range.last / Chunk) return true;
        }
        return false;
    }
    bool Urgent(Resource& resource, std::uint64_t offset) {
        double now;
        { std::lock_guard<std::mutex> lock(mutex); now = position; }
        std::unique_lock<std::timed_mutex> lock(resource.mutex, std::try_to_lock);
        if (!lock.owns_lock()) return false;
        if (resource.index.empty()) return true;
        const auto chunk = offset / Chunk;
        for (const auto& track : resource.index)
            for (const auto& range : track.Window(now, now + 2))
                if (chunk >= range.first / Chunk && chunk <= range.last / Chunk) return true;
        return false;
    }
    void Serve(SOCKET client) {
        const auto epoch = readerEpoch.load();
        std::string header; char buffer[2048];
        while (header.find("\r\n\r\n") == header.npos && header.size() < 16384 && !stopping) {
            auto count = recv(client, buffer, sizeof(buffer), 0); if (count <= 0) return;
            header.append(buffer, count);
        }
        const auto end = header.find("\r\n\r\n"); if (end == header.npos) return;
        std::istringstream parser(header.substr(0, end));
        std::string method, target, version; parser >> method >> target >> version;
        std::string line, rangeHeader; bool rangeSeen = false;
        std::getline(parser, line);
        while (std::getline(parser, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            auto colon = line.find(':'); if (colon == line.npos) continue;
            auto name = line.substr(0, colon);
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (name == "range") {
                if (rangeSeen) { rangeHeader = "invalid"; break; }
                rangeSeen = true; rangeHeader = line.substr(colon + 1);
                while (!rangeHeader.empty() && rangeHeader.front() == ' ') rangeHeader.erase(0, 1);
            }
        }
        std::shared_ptr<Resource> resource;
        {
            std::lock_guard<std::mutex> lock(mutex);
            const auto url = L"http://127.0.0.1:" + std::to_wstring(port) + std::wstring(target.begin(), target.end());
            // Closing filters can reconnect once before their worker stops. Keep their registered
            // routes readable during a switch; only preloading is quiesced until Select resumes it.
            auto found = resources.find(url); if (!stopping && found != resources.end()) resource = found->second;
        }
        auto error = [&](int status, const std::string& extra = "") {
            const auto text = "HTTP/1.1 " + std::to_string(status) + " Error\r\nConnection: close\r\nContent-Length: 0\r\n" + extra + "\r\n";
            SendAll(client, text.data(), text.size());
        };
        if ((method != "GET" && method != "HEAD") || version != "HTTP/1.1") { error(405); return; }
        if (!resource) { error(404); return; }
        ByteRange range; const auto parsed = ParseRange(rangeHeader, resource->length, range);
        if (parsed == RangeResult::Invalid) { error(400); return; }
        if (parsed == RangeResult::Unsatisfiable) { error(416, "Content-Range: bytes */" + std::to_string(resource->length) + "\r\n"); return; }
        std::vector<std::uint8_t> data;
        // Do not send success headers until the initial range is available. Bound each origin request to 8s.
        auto offset = range.first;
        auto count = static_cast<std::size_t>(std::min<std::uint64_t>(Chunk - offset % Chunk, range.last - offset + 1));
        if (method == "GET" && !Read(*resource, offset, count, true, data)) { error(502); return; }
        std::string response = parsed == RangeResult::Partial ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
        response += "Connection: close\r\nAccept-Ranges: bytes\r\nContent-Type: video/mp4\r\nContent-Length: " + std::to_string(range.last - range.first + 1) + "\r\n";
        if (parsed == RangeResult::Partial) response += "Content-Range: bytes " + std::to_string(range.first) + "-" + std::to_string(range.last) + "/" + std::to_string(resource->length) + "\r\n";
        response += "\r\n";
        if (!SendAll(client, response.data(), response.size()) || method == "HEAD") return;
        do {
            if (!SendAll(client, data.data(), data.size())) return;
            offset += count;
            if (offset > range.last || stopping || epoch != readerEpoch) break;
            // An open-ended native GET is not permission to fetch the whole movie while paused.
            // Each new range's first block remains immediately readable (metadata/foreground seeks).
            while (!stopping && epoch == readerEpoch && range.last == resource->length - 1 && !StreamAllowed(*resource, offset)) {
                std::unique_lock<std::mutex> lock(mutex); wake.wait_for(lock, std::chrono::milliseconds(100));
            }
            if (stopping || epoch != readerEpoch) break;
            count = static_cast<std::size_t>(std::min<std::uint64_t>(Chunk - offset % Chunk, range.last - offset + 1));
            const auto deadline = Clock::now() + std::chrono::seconds(8);
            bool ready = false;
            while (!stopping && epoch == readerEpoch && Clock::now() < deadline) {
                const bool urgent = Urgent(*resource, offset);
                if (Read(*resource, offset, count, urgent, data, deadline)) { ready = true; break; }
                if (urgent) break;
                // Future data requested by an open-ended native reader is still preloading.
                // A short outage must not replace a playable graph merely because its tail failed.
                std::unique_lock<std::mutex> lock(mutex); wake.wait_for(lock, std::chrono::milliseconds(250));
            }
            if (!ready) break;
        } while (!stopping && epoch == readerEpoch);
    }
    void Preload() {
        while (!stopping) {
            std::vector<std::shared_ptr<Resource>> list; double now, speed;
            { std::unique_lock<std::mutex> lock(mutex);
                wake.wait_for(lock, std::chrono::milliseconds(250));
                if (!quiescing) list = selected;
                now = position; speed = rate;
            }
            if (stopping || foreground) continue;
            const auto version = generation.load();
            { std::lock_guard<std::mutex> lock(mutex); capped = false; }
            for (auto& resource : list) {
                if (!resource->indexed && !resource->indexing.exchange(true)) {
                    struct IndexClaim { Resource& resource; ~IndexClaim() { resource.indexing = false; } } claim{*resource};
                    std::set<std::uint64_t> metadataChunks;
                    auto index = BuildMediaTimeIndex(resource->length, [&](std::uint64_t start, std::size_t count, std::vector<std::uint8_t>& data) {
                        for (auto c = start / Chunk; c <= (start + count - 1) / Chunk; ++c) {
                            if (!Covered(resource->id, {c * Chunk, c * Chunk})) metadataChunks.insert(c);
                        }
                        if (metadataChunks.size() > 16) return false;
                        return version == generation && !foreground && Read(*resource, start, count, false, data); });
                    if (version != generation || foreground) break;
                    std::size_t size = 0; for (const auto& track : index) size += track.samples.capacity() * sizeof(TimedBytes);
                    if (indexBytes.fetch_add(size) + size > 24 * Chunk) { indexBytes -= size; index.clear(); }
                    { std::lock_guard<std::timed_mutex> lock(resource->mutex); resource->index = std::move(index); resource->indexed = true; }
                }
                std::unique_lock<std::timed_mutex> indexLock(resource->mutex);
                std::set<std::uint64_t> seen;
                std::vector<std::uint64_t> chunks;
                auto add = [&](std::uint64_t c) { if (seen.insert(c).second) chunks.push_back(c); };
                if (resource->index.empty()) {
                    // Unknown time coverage is explicitly shown as bytes. No guessed seconds.
                    const auto first = resource->cursor.load() / Chunk;
                    const auto limit = std::min<std::uint64_t>(first + 16 / std::max<std::size_t>(1, list.size()), (resource->length + Chunk - 1) / Chunk);
                    for (auto i = first; i < limit; ++i) add(i);
                } else {
                    for (const auto& track : resource->index) {
                        for (const auto& range : track.Window(now, now + ForwardTarget(speed, settings.forwardSeconds))) {
                            for (auto c = range.first / Chunk; c <= range.last / Chunk; ++c) add(c);
                        }
                        for (const auto& range : track.Window(std::max(0.0, now - settings.backSeconds), now)) {
                            for (auto c = range.first / Chunk; c <= range.last / Chunk; ++c) add(c);
                        }
                    }
                }
                indexLock.unlock();
                bool limited = chunks.size() * Chunk > settings.diskBytes / std::max<std::size_t>(1, list.size());
                { std::lock_guard<std::mutex> lock(mutex); capped = capped || limited; }
                std::size_t fetched = 0;
                for (auto chunk : chunks) {
                    if (stopping || version != generation || foreground) break;
                    // Avoid continuously downloading and evicting the same oversized window.
                    if (++fetched * Chunk > settings.diskBytes / std::max<std::size_t>(1, list.size())) break;
                    if (Covered(resource->id, {chunk * Chunk, chunk * Chunk})) continue;
                    std::vector<std::uint8_t> data;
                    if (!Read(*resource, chunk * Chunk, static_cast<std::size_t>(std::min(Chunk, resource->length - chunk * Chunk)), false, data)) break;
                }
            }
        }
    }
};
MediaCache::MediaCache(CacheSettings settings) : impl(std::make_unique<Impl>(std::move(settings))) {}
MediaCache::~MediaCache() = default;
std::wstring MediaCache::Register(const std::wstring& source, const std::wstring& userAgent, const std::wstring& referer) { return impl->Register(source, userAgent, referer); }
void MediaCache::Select(const std::vector<std::wstring>& routes) {
    ++impl->generation; impl->Abort(false);
    std::lock_guard<std::mutex> lock(impl->mutex); impl->selected.clear(); impl->capped = false; impl->quiescing = false;
    for (const auto& route : routes) { auto found = impl->resources.find(route); if (found != impl->resources.end()) impl->selected.push_back(found->second); }
    impl->wake.notify_all();
}
void MediaCache::Prepare(const std::vector<std::wstring>& routes) {
    const auto deadline = Clock::now() + std::chrono::seconds(2);
    const auto version = impl->generation.load();
    for (const auto& route : routes) {
        std::shared_ptr<Impl::Resource> resource;
        { std::lock_guard<std::mutex> lock(impl->mutex); auto found = impl->resources.find(route); if (found != impl->resources.end()) resource = found->second; }
        if (!resource || resource->indexed || impl->stopping || Clock::now() >= deadline || resource->indexing.exchange(true)) continue;
        struct IndexClaim { Impl::Resource& resource; ~IndexClaim() { resource.indexing = false; } } claim{*resource};
        std::set<std::uint64_t> metadataChunks;
        auto index = BuildMediaTimeIndex(resource->length, [&](std::uint64_t offset, std::size_t count, std::vector<std::uint8_t>& bytes) {
            for (auto c = offset / Chunk; c <= (offset + count - 1) / Chunk; ++c)
                if (!impl->Covered(resource->id, {c * Chunk, c * Chunk})) metadataChunks.insert(c);
            if (metadataChunks.size() > 16) return false;
            return impl->generation == version && Clock::now() < deadline && impl->Read(*resource, offset, count, false, bytes, deadline); });
        if (impl->generation != version || impl->stopping) break;
        std::size_t size = 0; for (const auto& track : index) size += track.samples.capacity() * sizeof(TimedBytes);
        if (impl->indexBytes.fetch_add(size) + size > 24 * Chunk) { impl->indexBytes -= size; index.clear(); }
        std::lock_guard<std::timed_mutex> lock(resource->mutex);
        resource->index = std::move(index); resource->indexed = !resource->index.empty() || Clock::now() < deadline;
    }
}
void MediaCache::Update(double position, double rate, bool paused) {
    bool changed;
    { std::lock_guard<std::mutex> lock(impl->mutex);
        changed = std::abs(position - impl->position) > 3 || std::abs(rate - impl->rate) > .01;
        impl->position = std::max(0.0, position); impl->rate = rate; impl->paused = paused;
    }
    if (changed) { ++impl->generation; impl->Abort(false); }
    impl->wake.notify_all();
}
CacheStatus MediaCache::Status() const {
    CacheStatus result;
    std::vector<std::shared_ptr<Impl::Resource>> selected; double position;
    { std::lock_guard<std::mutex> lock(impl->mutex); selected = impl->selected; position = impl->position; result.capped = impl->capped; }
    { std::lock_guard<std::mutex> store(impl->storeMutex); result.bytes = impl->bytes; }
    result.requests = impl->requests; result.hits = impl->hits;
    result.failed = impl->failed;
    result.active = !impl->stopping && !selected.empty(); result.forwardSeconds = result.active ? 1e9 : -1;
    for (const auto& resource : selected) {
        // Index publication is protected by the representation mutex.
        std::unique_lock<std::timed_mutex> resourceLock(resource->mutex, std::try_to_lock);
        if (!resourceLock.owns_lock()) { result.forwardSeconds = -1; break; }
        if (resource->index.empty()) { result.forwardSeconds = -1; break; }
        for (const auto& track : resource->index) result.forwardSeconds = std::min(result.forwardSeconds,
            track.Forward(position, [&](ByteRange range) { return impl->Covered(resource->id, range); }));
    }
    return result;
}
void MediaCache::Clear() {
    ++impl->generation; impl->Abort(false);
    std::lock_guard<std::mutex> lock(impl->storeMutex);
    for (auto it = impl->blocks.begin(); it != impl->blocks.end();) {
        std::error_code error;
        if (std::filesystem::remove(it->second.path, error) && !error) { impl->bytes -= it->second.charge; it = impl->blocks.erase(it); }
        else ++it;
    }
}
void MediaCache::Cancel() { impl->Cancel(); }
void MediaCache::CancelReaders() {
    ++impl->readerEpoch; ++impl->generation;
    { std::lock_guard<std::mutex> lock(impl->mutex); impl->quiescing = true; }
    impl->Abort(true);
    { std::lock_guard<std::mutex> lock(impl->socketMutex); for (auto client : impl->clients) shutdown(client, SD_BOTH); }
}
}
