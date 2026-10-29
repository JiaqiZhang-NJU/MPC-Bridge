// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "PlaybackMemory.h"
#include "ExtLib/rapidjson/include/rapidjson/document.h"
#include "ExtLib/rapidjson/include/rapidjson/writer.h"
#include "ExtLib/rapidjson/include/rapidjson/stringbuffer.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>

namespace Bridge {
namespace {
constexpr DWORD FileLimit = 8 * 1024 * 1024;
constexpr int64_t TimeLimit = 365ll * 24 * 3600 * 1000;
std::string LowerAscii(std::string value) {
    for (auto& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return value;
}
std::string Query(const std::string& url, const std::string& name) {
    const auto start = url.find('?');
    if (start == std::string::npos) return {};
    for (size_t pos = start + 1; pos < url.size();) {
        auto end = url.find_first_of("&#", pos);
        const auto part = url.substr(pos, end == std::string::npos ? end : end - pos);
        if (part.compare(0, name.size() + 1, name + "=") == 0) return part.substr(name.size() + 1);
        if (end == std::string::npos || url[end] == '#') break;
        pos = end + 1;
    }
    return {};
}
bool Identifier(const std::string& value) {
    return !value.empty() && value.size() <= 128 && std::all_of(value.begin(), value.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}
struct Database {
    uint64_t cleared = 0;
    std::map<std::string, uint64_t> deleted;
    std::map<std::string, PlaybackMemoryRecord> records;
};
using Value = rapidjson::Value;
std::string Str(const Value& v, const char* key) {
    auto i = v.FindMember(key);
    if (i == v.MemberEnd() || !i->value.IsString() || i->value.GetStringLength() > 8192) return {};
    const std::string value(i->value.GetString(), i->value.GetStringLength());
    return value.find('\0') == std::string::npos ? value : std::string();
}
int64_t Num(const Value& v, const char* key, int64_t fallback = 0) {
    auto i = v.FindMember(key); return i != v.MemberEnd() && i->value.IsInt64() ? i->value.GetInt64() : fallback;
}
bool Bool(const Value& v, const char* key, bool fallback = false) {
    auto i = v.FindMember(key); return i != v.MemberEnd() && i->value.IsBool() ? i->value.GetBool() : fallback;
}
std::string EncodeRecord(const PlaybackMemoryRecord& r) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
    auto str = [&w](const char* k, const std::string& s) { w.Key(k); w.String(s.data(), static_cast<rapidjson::SizeType>(s.size())); };
    auto num = [&w](const char* k, int64_t n) { w.Key(k); w.Int64(n); };
    auto flag = [&w](const char* k, bool b) { w.Key(k); w.Bool(b); };
    w.StartObject();
    str("key", r.key); str("source", r.source); num("session", r.session); num("updated", r.updated);
    num("positionMs", r.positionMs); num("durationMs", r.durationMs); flag("completed", r.completed);
    flag("hasPosition", r.hasPosition); flag("hasSettings", r.hasSettings);
    w.Key("rate"); w.Double(r.rate);
    flag("qualityAutomatic", r.qualityAutomatic); num("qualityHeight", r.qualityHeight);
    num("qualityCodec", r.qualityCodec); num("qualityFps", r.qualityFps); str("qualityFormat", r.qualityFormat);
    str("audioKey", r.audioKey); str("subtitleKey", r.subtitleKey); str("subtitlePath", r.subtitlePath);
    flag("subtitlesVisible", r.subtitlesVisible); num("subtitleDelayMs", r.subtitleDelayMs);
    flag("dualEnabled", r.dual.enabled); num("layout", r.dual.layout);
    w.Key("slots"); w.StartArray();
    for (const auto& slot : r.dual.slots) {
        w.StartObject(); str("path", MemoryUtf8(slot.path)); str("embeddedKey", MemoryUtf8(slot.embeddedKey));
        num("embeddedTrack", slot.embeddedTrack); flag("visible", slot.visible); num("delayMs", slot.delayMs);
        num("fontSize", slot.fontSize); num("bitmapScale", slot.bitmapScale); num("color", slot.color); w.EndObject();
    }
    w.EndArray(); w.EndObject();
    return {buffer.GetString(), buffer.GetSize()};
}
bool DecodeRecord(const Value& v, PlaybackMemoryRecord& r) {
    if (!v.IsObject()) return false;
    if (Num(v, "session") <= 0 || Num(v, "updated") <= 0) return false;
    for (const auto key : {"qualityHeight", "qualityCodec", "qualityFps", "subtitleDelayMs", "layout"}) {
        const auto n = Num(v, key); if (n < INT32_MIN || n > INT32_MAX) return false;
    }
    r.key = Str(v, "key"); r.source = Str(v, "source");
    r.session = Num(v, "session"); r.updated = Num(v, "updated");
    r.positionMs = Num(v, "positionMs"); r.durationMs = Num(v, "durationMs");
    r.completed = Bool(v, "completed"); r.hasPosition = Bool(v, "hasPosition"); r.hasSettings = Bool(v, "hasSettings");
    auto rate = v.FindMember("rate");
    if (rate != v.MemberEnd() && rate->value.IsNumber()) r.rate = rate->value.GetDouble();
    r.qualityAutomatic = Bool(v, "qualityAutomatic", true);
    r.qualityHeight = static_cast<int>(Num(v, "qualityHeight")); r.qualityCodec = static_cast<int>(Num(v, "qualityCodec", -1));
    r.qualityFps = static_cast<int>(Num(v, "qualityFps")); r.qualityFormat = Str(v, "qualityFormat");
    r.audioKey = Str(v, "audioKey"); r.subtitleKey = Str(v, "subtitleKey"); r.subtitlePath = Str(v, "subtitlePath");
    r.subtitlesVisible = Bool(v, "subtitlesVisible", true); r.subtitleDelayMs = static_cast<int>(Num(v, "subtitleDelayMs"));
    r.dual.enabled = Bool(v, "dualEnabled"); r.dual.layout = static_cast<int>(Num(v, "layout"));
    auto slots = v.FindMember("slots");
    if (slots != v.MemberEnd() && slots->value.IsArray() && slots->value.Size() == 2) {
        for (unsigned i = 0; i < 2; ++i) {
            const auto& s = slots->value[i]; if (!s.IsObject()) return false;
            for (const auto key : {"embeddedTrack", "delayMs", "fontSize", "bitmapScale", "color"}) {
                const auto n = Num(s, key); if (n < INT32_MIN || n > INT32_MAX) return false;
            }
            auto& slot = r.dual.slots[i];
            slot.path = MemoryWide(Str(s, "path")); slot.embeddedKey = MemoryWide(Str(s, "embeddedKey"));
            slot.embeddedTrack = static_cast<int>(Num(s, "embeddedTrack", -1)); slot.visible = Bool(s, "visible", true);
            slot.delayMs = static_cast<int>(Num(s, "delayMs")); slot.fontSize = static_cast<int>(Num(s, "fontSize", 20));
            slot.bitmapScale = static_cast<int>(Num(s, "bitmapScale", 100));
            slot.color = static_cast<uint32_t>(Num(s, "color", 0xffffff));
            if (!ValidSubtitleSlot(slot)) return false;
        }
    } else if (r.dual.enabled) return false;
    return !r.key.empty() && !r.source.empty() && r.session > 0 && r.updated > 0
        && r.positionMs >= 0 && r.positionMs <= TimeLimit && r.durationMs >= 0 && r.durationMs <= TimeLimit
        && std::isfinite(r.rate) && r.rate >= .125 && r.rate <= 16
        && r.qualityHeight >= 0 && r.qualityHeight <= 16384 && r.dual.layout >= 0 && r.dual.layout <= 1
        && std::abs(int64_t(r.subtitleDelayMs)) <= SubtitleDelayLimit;
}
// 0 missing, 1 valid, -1 corrupt/IO failure, -2 future version (never overwrite).
int ReadDatabase(const std::wstring& file, Database& db) {
    HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : -1;
    LARGE_INTEGER size{}; DWORD read = 0;
    const bool sized = GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart <= FileLimit;
    std::string bytes(sized ? static_cast<size_t>(size.QuadPart) : 0, '\0');
    const bool ok = sized && ReadFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size();
    CloseHandle(h);
    if (!ok) return -1;
    rapidjson::Document doc; doc.Parse<rapidjson::kParseValidateEncodingFlag>(bytes.data(), bytes.size());
    if (doc.HasParseError() || !doc.IsObject()) return -1;
    const auto version = Num(doc, "schemaVersion"); if (version > 1) return -2; if (version != 1) return -1;
    auto entries = doc.FindMember("records");
    if (entries == doc.MemberEnd() || !entries->value.IsArray() || entries->value.Size() > 900) return -1;
    db = {}; db.cleared = Num(doc, "cleared");
    auto deleted = doc.FindMember("deleted");
    if (deleted != doc.MemberEnd() && deleted->value.IsObject()) {
        for (auto i = deleted->value.MemberBegin(); i != deleted->value.MemberEnd(); ++i) {
            if (!i->value.IsUint64() || i->name.GetStringLength() > 8192 || db.deleted.size() >= 4096) return -1;
            db.deleted[std::string(i->name.GetString(), i->name.GetStringLength())] = i->value.GetUint64();
        }
    }
    for (const auto& v : entries->value.GetArray()) {
        PlaybackMemoryRecord r;
        if (DecodeRecord(v, r) && EncodeRecord(r).size() <= 32768) db.records[r.key] = std::move(r);
    }
    return 1;
}
struct DirectoryLock {
    HANDLE handle = INVALID_HANDLE_VALUE;
    explicit DirectoryLock(const std::wstring& dir) {
        const auto end = GetTickCount64() + 300;
        do {
            handle = CreateFileW((dir + L"playback-memory.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr);
            if (handle != INVALID_HANDLE_VALUE || GetLastError() != ERROR_SHARING_VIOLATION) break;
            Sleep(10);
        } while (GetTickCount64() < end);
    }
    ~DirectoryLock() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
};
bool Load(const std::wstring& dir, Database& db, bool& future, bool& primaryValid) {
    const int status = ReadDatabase(dir + L"playback-memory.json", db);
    primaryValid = status == 1; future = status == -2;
    if (status == 1) return true;
    if (future) return false;
    const int backup = ReadDatabase(dir + L"playback-memory.json.bak", db);
    future = backup == -2;
    return backup == 1 || (status == 0 && backup == 0);
}
bool WriteDatabase(const std::wstring& dir, const Database& db, bool primaryValid) {
    rapidjson::StringBuffer buffer; rapidjson::Writer<rapidjson::StringBuffer> w(buffer);
    w.StartObject(); w.Key("schemaVersion"); w.Int(1); w.Key("cleared"); w.Uint64(db.cleared);
    w.Key("deleted"); w.StartObject();
    for (const auto& d : db.deleted) { w.Key(d.first.data(), static_cast<rapidjson::SizeType>(d.first.size())); w.Uint64(d.second); }
    w.EndObject(); w.Key("records"); w.StartArray();
    for (const auto& item : db.records) { const auto json = EncodeRecord(item.second); w.RawValue(json.data(), json.size(), rapidjson::kObjectType); }
    w.EndArray(); w.EndObject();
    if (buffer.GetSize() > FileLimit) return false;
    const auto file = dir + L"playback-memory.json", temp = file + L".tmp", backup = file + L".bak";
    HANDLE h = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(h, buffer.GetString(), static_cast<DWORD>(buffer.GetSize()), &written, nullptr)
        && written == buffer.GetSize() && FlushFileBuffers(h);
    CloseHandle(h);
    if (ok && primaryValid) ok = CopyFileW(file.c_str(), backup.c_str(), FALSE) != FALSE;
    if (ok) ok = MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!ok) DeleteFileW(temp.c_str());
    return ok;
}
}

std::string MemoryUtf8(const std::wstring& value) {
    const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(count, '\0');
    if (count) WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
    return result;
}
std::wstring MemoryWide(const std::string& value) {
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(count, L'\0');
    if (count) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count);
    return result;
}
uint64_t MemoryTimestamp() {
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    const uint64_t now = (uint64_t(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    static std::atomic<uint64_t> previous{0};
    auto old = previous.load();
    while (!previous.compare_exchange_weak(old, std::max(now, old + 1))) {}
    return std::max(now, old + 1);
}
std::string OnlineMemoryKey(const std::string& url, const std::string& resolvedId) {
    const auto scheme = url.find("://"); if (scheme == std::string::npos) return {};
    const auto protocol = LowerAscii(url.substr(0, scheme)); if (protocol != "http" && protocol != "https") return {};
    const auto first = scheme + 3, slash = url.find('/', first);
    if (slash == std::string::npos) return {};
    auto host = LowerAscii(url.substr(first, slash - first));
    if (host.compare(0, 4, "www.") == 0) host.erase(0, 4);
    if (host.compare(0, 2, "m.") == 0) host.erase(0, 2);
    const auto path = url.substr(slash, url.find_first_of("?#", slash) - slash);
    std::string id;
    if (host == "youtu.be") id = path.substr(1);
    else if (host == "youtube.com") {
        if (path == "/watch") id = Query(url, "v");
        else if (path.compare(0, 8, "/shorts/") == 0) id = path.substr(8);
    }
    if ((host == "youtube.com" || host == "youtu.be") && Identifier(id)) return "youtube:" + id;
    if (host != "bilibili.com") return {};
    if (path.compare(0, 7, "/video/") == 0) {
        id = path.substr(7); if (!id.empty() && id.back() == '/') id.pop_back();
        if (!Identifier(id)) return {};
        auto part = Query(url, "p"); if (part.empty()) part = "1";
        if (part.size() > 6 || !std::all_of(part.begin(), part.end(), [](char c) { return c >= '0' && c <= '9'; })) return {};
        unsigned p = 0; for (char c : part) p = p * 10 + c - '0'; if (!p) return {};
        // The resolved ID is an extra discriminator. Keep the requested P even
        // when an extractor version reports only the parent video ID.
        return "bilibili:" + (Identifier(resolvedId) ? resolvedId : id) + ":p" + std::to_string(p);
    }
    if (path.compare(0, 14, "/bangumi/play/") == 0) {
        id = path.substr(14); if (!id.empty() && id.back() == '/') id.pop_back();
        if (Identifier(resolvedId)) return "bilibili:episode:" + resolvedId;
        if (id.compare(0, 2, "ep") == 0 && Identifier(id)) return "bilibili:episode:" + id;
    }
    return {};
}
std::optional<int64_t> OnlineStartMs(const std::string& url) {
    auto text = Query(url, "t"); if (text.empty()) text = Query(url, "start");
    if (text.empty()) { const auto hash = url.find("#t="); if (hash != std::string::npos) text = url.substr(hash + 3); }
    if (text.empty() || text.size() > 20) return {};
    int64_t total = 0, number = 0; bool digits = false;
    for (char c : text) {
        if (c >= '0' && c <= '9') { number = number * 10 + c - '0'; digits = true; if (number > TimeLimit / 1000) return {}; }
        else {
            const int scale = c == 'h' ? 3600 : c == 'm' ? 60 : c == 's' ? 1 : 0;
            if (!scale || !digits) return {};
            total += number * scale; number = 0; digits = false;
        }
        if (total > TimeLimit / 1000) return {};
    }
    total += number; if (total > TimeLimit / 1000) return {};
    return total * 1000;
}
std::string LocalMemoryKey(const std::wstring& path) {
    std::wstring full(32768, L'\0');
    const DWORD n = GetFullPathNameW(path.c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr);
    if (!n || n >= full.size()) return {};
    full.resize(n); WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExW(full.c_str(), GetFileExInfoStandard, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return {};
    CharLowerBuffW(full.data(), static_cast<DWORD>(full.size()));
    const uint64_t size = (uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    const uint64_t modified = (uint64_t(info.ftLastWriteTime.dwHighDateTime) << 32) | info.ftLastWriteTime.dwLowDateTime;
    return "file:" + MemoryUtf8(full) + ":" + std::to_string(size) + ":" + std::to_string(modified);
}
std::string MemorySource(const std::wstring& path) {
    const auto url = MemoryUtf8(path);
    const auto scheme = url.find("://");
    if (scheme != std::string::npos) {
        const auto key = OnlineMemoryKey(url);
        if (!key.empty()) return key;
        const auto slash = url.find('/', scheme + 3);
        if (slash == std::string::npos) return {};
        auto host = LowerAscii(url.substr(scheme + 3, slash - scheme - 3));
        if (host.compare(0, 4, "www.") == 0) host.erase(0, 4);
        if (host.compare(0, 2, "m.") == 0) host.erase(0, 2);
        auto resource = url.substr(slash, url.find_first_of("?#", slash) - slash);
        if (!resource.empty() && resource.back() == '/') resource.pop_back();
        // Season pages are not a media key. Their source groups deletion only;
        // the resolved episode remains the identity of every saved record.
        if (host == "bilibili.com" && resource.compare(0, 14, "/bangumi/play/") == 0)
            return "bilibili:source:" + resource.substr(14);
        return {};
    }
    std::wstring full(32768, L'\0');
    const DWORD n = GetFullPathNameW(path.c_str(), static_cast<DWORD>(full.size()), full.data(), nullptr);
    if (!n || n >= full.size()) return {};
    full.resize(n); CharLowerBuffW(full.data(), n);
    return "file:" + MemoryUtf8(full);
}

PlaybackMemoryStore::PlaybackMemoryStore() : worker([this] { Run(); }) {}
PlaybackMemoryStore::~PlaybackMemoryStore() {
    { std::lock_guard<std::mutex> lock(mutex); stopping = true; }
    wake.notify_one(); worker.join();
}
PlaybackMemoryStore& MemoryStore() { static PlaybackMemoryStore store; return store; }
bool PlaybackMemoryStore::Flush(unsigned milliseconds) {
    std::unique_lock<std::mutex> lock(mutex);
    return drained.wait_for(lock, std::chrono::milliseconds(milliseconds), [this] { return jobs.empty() && !busy; });
}
PlaybackMemoryLookup PlaybackMemoryStore::Read(const std::wstring& dir, const std::string& key, const std::string& source) {
    PlaybackMemoryLookup result;
    if (!Flush()) { result.status = MemoryReadStatus::Unavailable; return result; }
    DirectoryLock lock(dir); Database db; bool future = false, valid = false;
    if (lock.handle == INVALID_HANDLE_VALUE || !Load(dir, db, future, valid)) {
        error = true; result.status = MemoryReadStatus::Unavailable; return result;
    }
    const auto it = db.records.find(key);
    if (it != db.records.end()) { result.record = it->second; result.status = MemoryReadStatus::Found; }
    else if (db.cleared || db.deleted.count(key) || db.deleted.count(source)) result.status = MemoryReadStatus::Deleted;
    return result;
}
void PlaybackMemoryStore::Enqueue(Job job) {
    { std::lock_guard<std::mutex> lock(mutex);
        if (!jobs.empty() && !job.erase && !jobs.back().erase && jobs.back().directory == job.directory && jobs.back().record.key == job.record.key) jobs.back() = std::move(job);
        else if (jobs.size() < 64) jobs.push_back(std::move(job));
        else error = true;
    }
    wake.notify_one();
}
void PlaybackMemoryStore::Save(const std::wstring& directory, PlaybackMemoryRecord record, unsigned capacity, bool position, bool settings) {
    if (record.key.empty() || (!position && !settings)) return;
    Job job; job.directory = directory; job.record = std::move(record); job.capacity = std::clamp(capacity, 10u, 900u);
    job.position = position; job.settings = settings; Enqueue(std::move(job));
}
void PlaybackMemoryStore::Erase(const std::wstring& directory, const std::string& source) {
    Job job; job.directory = directory; job.record.source = source; job.erase = true; job.stamp = MemoryTimestamp(); Enqueue(std::move(job));
}
void PlaybackMemoryStore::Trim(const std::wstring& directory, unsigned capacity) {
    Job job; job.directory = directory; job.capacity = std::clamp(capacity, 10u, 900u); Enqueue(std::move(job));
}
bool PlaybackMemoryStore::Relocate(const std::wstring& from, const std::wstring& to) {
    if (from == to) return true;
    if (!Flush()) return false;
    DirectoryLock first(from), second(to); Database old, next; bool future = false, valid = false, newValid = false;
    if (first.handle == INVALID_HANDLE_VALUE || second.handle == INVALID_HANDLE_VALUE
        || !Load(from, old, future, valid) || !Load(to, next, future, newValid)) return false;
    next.cleared = std::max(next.cleared, old.cleared);
    for (const auto& item : old.deleted) next.deleted[item.first] = std::max(next.deleted[item.first], item.second);
    for (const auto& item : old.records) {
        auto i = next.records.find(item.first);
        if (i == next.records.end() || item.second.updated > i->second.updated) next.records[item.first] = item.second;
    }
    for (auto i = next.records.begin(); i != next.records.end();) {
        if (i->second.session <= std::max({next.cleared, next.deleted[i->first], next.deleted[i->second.source]})) i = next.records.erase(i);
        else ++i;
    }
    while (next.records.size() > 900) {
        auto i = std::min_element(next.records.begin(), next.records.end(), [](const auto& a, const auto& b) { return a.second.updated < b.second.updated; });
        next.records.erase(i);
    }
    if (!WriteDatabase(to, next, newValid)) return false;
    DeleteFileW((from + L"playback-memory.json").c_str()); DeleteFileW((from + L"playback-memory.json.bak").c_str());
    return true;
}
void PlaybackMemoryStore::Run() {
    for (;;) {
        Job job;
        { std::unique_lock<std::mutex> lock(mutex); wake.wait(lock, [this] { return stopping || !jobs.empty(); });
            if (jobs.empty() && stopping) break;
            job = std::move(jobs.front()); jobs.pop_front(); busy = true;
        }
        bool success = false;
        try {
            DirectoryLock lock(job.directory); Database db; bool future = false, primaryValid = false;
            const bool loaded = lock.handle != INVALID_HANDLE_VALUE && Load(job.directory, db, future, primaryValid);
            if (loaded || (lock.handle != INVALID_HANDLE_VALUE && job.erase && job.record.source.empty() && !future)) {
                const auto initialCount = db.records.size();
                if (job.erase) {
                    if (job.record.source.empty()) { db.records.clear(); db.deleted.clear(); db.cleared = job.stamp; }
                    else {
                        db.deleted[job.record.source] = job.stamp;
                        for (auto it = db.records.begin(); it != db.records.end();) {
                            if (it->second.source == job.record.source) { db.deleted[it->first] = job.stamp; it = db.records.erase(it); }
                            else ++it;
                        }
                    }
                } else {
                    auto& r = job.record;
                    const auto old = db.records.find(r.key);
                    uint64_t deleted = db.cleared;
                    if (db.deleted.count(r.key)) deleted = std::max(deleted, db.deleted[r.key]);
                    if (db.deleted.count(r.source)) deleted = std::max(deleted, db.deleted[r.source]);
                    if (r.session > deleted && (old == db.records.end() || r.session >= old->second.session)) {
                        auto next = job.settings ? r : old != db.records.end() ? old->second : PlaybackMemoryRecord{};
                        next.key = r.key; next.source = r.source; next.session = r.session; next.updated = r.updated;
                        next.hasSettings = job.settings || (old != db.records.end() && old->second.hasSettings);
                        if (job.position) { next.positionMs = r.positionMs; next.durationMs = r.durationMs; next.completed = r.completed; next.hasPosition = true; }
                        else if (old != db.records.end()) { next.positionMs = old->second.positionMs; next.durationMs = old->second.durationMs; next.completed = old->second.completed; next.hasPosition = old->second.hasPosition; }
                        if (EncodeRecord(next).size() <= 32768) db.records[r.key] = std::move(next);
                        else throw 0;
                    }
                    while (db.records.size() > job.capacity) {
                        auto oldest = std::min_element(db.records.begin(), db.records.end(), [](const auto& a, const auto& b) { return a.second.updated < b.second.updated; });
                        db.deleted[oldest->first] = MemoryTimestamp();
                        db.records.erase(oldest);
                    }
                }
                if (db.deleted.size() > 4096) {
                    // A compact global barrier prevents evicted tombstones from resurrecting.
                    for (const auto& item : db.deleted) db.cleared = std::max(db.cleared, item.second);
                    db.deleted.clear();
                }
                success = !job.erase && job.record.key.empty() && db.records.size() == initialCount
                    ? true : WriteDatabase(job.directory, db, primaryValid);
                if (success && job.erase) {
                    // A pre-deletion backup must not resurrect forgotten history.
                    const auto file = job.directory + L"playback-memory.json", backup = file + L".bak";
                    if (!DeleteFileW(backup.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) success = false;
                    if (success) success = CopyFileW(file.c_str(), backup.c_str(), FALSE) != FALSE;
                }
            }
        } catch (...) { success = false; }
        if (!success) error = true;
        { std::lock_guard<std::mutex> lock(mutex); busy = false; if (jobs.empty()) drained.notify_all(); }
    }
}
}
