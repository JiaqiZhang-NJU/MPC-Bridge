// SPDX-License-Identifier: GPL-3.0-or-later
#define NOMINMAX
#include <windows.h>
#include "apps/mplayerc/bridge/PlaybackMemory.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace Bridge;
namespace fs = std::filesystem;
int checks = 0;
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
    ++checks;
}
PlaybackMemoryRecord Record(const std::string& key, int64_t position = 123456) {
    PlaybackMemoryRecord r;
    r.key = key; r.source = key; r.session = MemoryTimestamp(); r.updated = r.session;
    r.positionMs = position; r.durationMs = 600000; r.rate = 1.5; r.hasSettings = true;
    return r;
}
int main(int argc, char** argv) {
    try {
        if (argc == 4) {
            PlaybackMemoryStore store;
            store.Save(MemoryWide(argv[1]), Record(argv[2], std::stoll(argv[3])), 200, true, true);
            return store.Flush() && !store.TakeError() ? 0 : 2;
        }
        Check(OnlineMemoryKey("https://www.youtube.com/watch?v=aBc_123&t=2") == "youtube:aBc_123", "YouTube ID case");
        Check(OnlineMemoryKey("https://youtu.be/aBc_123?si=test") == OnlineMemoryKey("https://m.youtube.com/shorts/aBc_123"), "YouTube aliases");
        Check(OnlineMemoryKey("https://youtu.be/ABC") != OnlineMemoryKey("https://youtu.be/abc"), "distinct ID case");
        Check(OnlineMemoryKey("https://www.bilibili.com/video/BVAbC/?p=01&share=foo") == "bilibili:BVAbC:p1", "Bilibili page normalization");
        Check(OnlineMemoryKey("https://bilibili.com/video/BVAbC?p=1", "123_p1") != OnlineMemoryKey("https://bilibili.com/video/BVAbC?p=2", "123_p1"), "Bilibili parts isolated");
        Check(OnlineMemoryKey("https://bilibili.com/bangumi/play/ss1").empty(), "season without resolved episode is unsafe");
        Check(MemorySource(L"https://www.bilibili.com/bangumi/play/ss123/?share=test") == MemorySource(L"https://m.bilibili.com/bangumi/play/ss123"), "season history source supports aliases without conflating episode identity");
        Check(OnlineMemoryKey("https://bilibili.com/bangumi/play/ep1") != OnlineMemoryKey("https://bilibili.com/bangumi/play/ep2"), "episodes isolated");
        Check(OnlineMemoryKey("https://youtube.com.evil/watch?v=abc").empty(), "exact host");
        Check(OnlineStartMs("https://youtu.be/abc?t=1h2m3s") == 3723000, "compound explicit time");
        Check(OnlineStartMs("https://youtu.be/abc?t=0") == 0, "explicit zero");
        Check(!OnlineStartMs("https://youtu.be/abc?t=-1"), "negative time rejected");
        Check(!OnlineStartMs("https://youtu.be/abc?t=99999999999999999"), "time overflow rejected");
        Check(MemoryWide(MemoryUtf8(L"\x539f\x6587 a.srt")) == L"\x539f\x6587 a.srt", "Unicode round trip");
        wchar_t temp[MAX_PATH]; GetTempPathW(MAX_PATH, temp);
        auto folder = fs::path(temp) / (L"mpc-memory-test-" + std::to_wstring(MemoryTimestamp()));
        fs::create_directories(folder);
        const auto dir = folder.wstring() + L"\\";
        PlaybackMemoryStore store;
        Check(store.Read(dir, "A").status == MemoryReadStatus::Missing, "new store");
        auto a = Record("A"); a.dual.enabled = true; a.dual.layout = 1;
        a.dual.slots[0].path = L"C:\\subtitle\\\x539f\x6587 a.srt";
        a.dual.slots[0].delayMs = 500; a.dual.slots[1].embeddedTrack = 2;
        a.dual.slots[1].embeddedKey = L"SRT|English"; a.dual.slots[1].visible = false;
        a.dual.slots[1].bitmapScale = 125;
        store.Save(dir, a, 200, true, true); Check(store.Flush(), "save flushed");
        auto read = store.Read(dir, "A");
        Check(read.status == MemoryReadStatus::Found && read.record.positionMs == 123456, "position persisted with millisecond precision");
        Check(read.record.rate == 1.5 && read.record.dual.slots[0].path == a.dual.slots[0].path
            && read.record.dual.slots[1].embeddedKey == a.dual.slots[1].embeddedKey && !read.record.dual.slots[1].visible
            && read.record.dual.slots[1].bitmapScale == 125, "subtitle and speed round trip");
        auto older = a; older.positionMs = 5; older.session--; older.updated = MemoryTimestamp();
        store.Save(dir, older, 200, true, true); store.Flush();
        Check(store.Read(dir, "A").record.positionMs == 123456, "older session cannot overwrite newer session");
        a.positionMs = 2500; a.updated = MemoryTimestamp(); store.Save(dir, a, 200, true, false); store.Flush();
        Check(store.Read(dir, "A").record.positionMs == 2500 && store.Read(dir, "A").record.rate == 1.5, "position only preserves settings and supports rewind");
        a.rate = 2; a.positionMs = 9999; a.updated = MemoryTimestamp(); store.Save(dir, a, 200, false, true); store.Flush();
        Check(store.Read(dir, "A").record.positionMs == 2500 && store.Read(dir, "A").record.rate == 2, "settings only preserves position");
        store.Erase(dir, "A"); store.Flush(); store.Save(dir, a, 200, true, true); store.Flush();
        Check(store.Read(dir, "A").status == MemoryReadStatus::Deleted, "deletion rejects stale writes");
        { std::ofstream file(folder / "playback-memory.json", std::ios::binary); file << "broken after deletion"; }
        Check(store.Read(dir, "A").status == MemoryReadStatus::Deleted, "backup cannot resurrect a deleted record");
        auto reopened = Record("A", 7000); store.Save(dir, reopened, 200, true, true); store.Flush();
        Check(store.Read(dir, "A").record.positionMs == 7000, "new session can record after deletion");
        store.Erase(dir); store.Flush(); store.Save(dir, reopened, 200, true, true); store.Flush();
        Check(store.Read(dir, "A").status == MemoryReadStatus::Deleted, "clear all rejects stale writes and migration");
        for (int i = 0; i < 12; ++i) store.Save(dir, Record(std::to_string(i), i * 1000), 10, true, true);
        store.Flush();
        Check(store.Read(dir, "0").status != MemoryReadStatus::Found && store.Read(dir, "11").status == MemoryReadStatus::Found, "capacity evicts oldest");
        // A corrupted primary must recover the last complete atomic backup.
        { std::ofstream file(folder / "playback-memory.json", std::ios::binary); file << "{truncated"; }
        Check(store.Read(dir, "10").status == MemoryReadStatus::Found, "backup recovery");
        store.Save(dir, Record("backup-recovered"), 200, true, true); store.Flush();
        Check(store.Read(dir, "backup-recovered").status == MemoryReadStatus::Found, "save after backup recovery");
        { std::ofstream file(folder / "playback-memory.json", std::ios::binary); file << "{\"schemaVersion\":99,\"records\":[]}"; }
        store.Save(dir, Record("future"), 200, true, true); store.Flush();
        Check(store.Read(dir, "future").status == MemoryReadStatus::Unavailable, "future schema is not overwritten");
        Check(store.TakeError(), "write failure surfaced");
        fs::remove(folder / "playback-memory.json"); fs::remove(folder / "playback-memory.json.bak");
        // Separate processes share one directory, each preserving the other's entries.
        wchar_t executable[32768]; GetModuleFileNameW(nullptr, executable, 32768);
        PROCESS_INFORMATION children[2]{};
        for (int i = 0; i < 2; ++i) {
            std::wstring command = L"\"" + std::wstring(executable) + L"\" \"" + folder.wstring() + L"\\\\\" child" + std::to_wstring(i) + L" 4567";
            STARTUPINFOW startup{}; startup.cb = sizeof(startup);
            Check(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &children[i]) != FALSE, "child process launched");
        }
        for (auto& child : children) {
            Check(WaitForSingleObject(child.hProcess, 10000) == WAIT_OBJECT_0, "child process finished");
            DWORD code = 1; GetExitCodeProcess(child.hProcess, &code); Check(code == 0, "child save succeeded");
            CloseHandle(child.hProcess); CloseHandle(child.hThread);
        }
        Check(store.Read(dir, "child0").status == MemoryReadStatus::Found && store.Read(dir, "child1").status == MemoryReadStatus::Found, "cross-process merge");
        auto local = folder / L"\x89c6\x9891 file.mp4";
        { std::ofstream file(local); file << "content"; }
        const auto original = LocalMemoryKey(local.wstring());
        Check(!original.empty() && original == LocalMemoryKey((folder / L"." / local.filename()).wstring()), "normalized local path");
        { std::ofstream file(local); file << "replacement content"; }
        Check(original != LocalMemoryKey(local.wstring()), "same path replacement invalidates identity");
        auto destination = folder / "moved"; fs::create_directory(destination);
        const auto moved = destination.wstring() + L"\\";
        Check(store.Relocate(dir, moved), "settings-directory migration");
        Check(store.Read(moved, "child0").status == MemoryReadStatus::Found && !fs::exists(folder / "playback-memory.json"), "migration preserves records and retires old file");
        auto lock = CreateFileW((moved + L"playback-memory.lock").c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        Check(lock != INVALID_HANDLE_VALUE, "exclusive lock fixture");
        store.Save(moved, Record("locked"), 200, true, true);
        Check(store.Flush() && store.TakeError(), "bounded lock failure surfaces without hanging");
        CloseHandle(lock);
        Check(store.Read(moved, "locked").status == MemoryReadStatus::Missing, "lock failure does not create partial record");
        Check(fs::canonical(folder).parent_path() == fs::canonical(fs::path(temp))
            && folder.filename().wstring().find(L"mpc-memory-test-") == 0, "cleanup stays in the owned temporary directory");
        fs::remove_all(folder);
        std::cout << checks << " playback memory checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
