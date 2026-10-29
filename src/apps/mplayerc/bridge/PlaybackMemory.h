// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "DualSubtitleModel.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>

namespace Bridge {
struct PlaybackMemoryRecord {
    std::string key, source, audioKey, subtitleKey, subtitlePath;
    uint64_t session = 0, updated = 0;
    int64_t positionMs = 0, durationMs = 0;
    bool completed = false, hasPosition = false, hasSettings = false;
    double rate = 1;
    bool qualityAutomatic = true;
    int qualityHeight = 0, qualityCodec = -1, qualityFps = 0;
    std::string qualityFormat;
    bool subtitlesVisible = true;
    int subtitleDelayMs = 0;
    DualSubtitleState dual;
};

// Only host/scheme are case insensitive. Video identifiers are not lowercased.
std::string OnlineMemoryKey(const std::string& url, const std::string& resolvedId = {});
std::optional<int64_t> OnlineStartMs(const std::string& url);
std::string LocalMemoryKey(const std::wstring& path);
std::string MemorySource(const std::wstring& path);
std::string MemoryUtf8(const std::wstring& value);
std::wstring MemoryWide(const std::string& value);
uint64_t MemoryTimestamp();

enum class MemoryReadStatus { Missing, Found, Deleted, Unavailable };
struct PlaybackMemoryLookup {
    MemoryReadStatus status = MemoryReadStatus::Missing;
    PlaybackMemoryRecord record;
};

// No COM objects or UI callbacks cross this boundary. Writes run on one worker;
// the directory lock also serializes read/merge/replace across /new processes.
class PlaybackMemoryStore {
public:
    PlaybackMemoryStore();
    ~PlaybackMemoryStore();
    PlaybackMemoryLookup Read(const std::wstring& directory, const std::string& key, const std::string& source = {});
    void Save(const std::wstring& directory, PlaybackMemoryRecord record, unsigned capacity,
        bool position, bool settings);
    void Erase(const std::wstring& directory, const std::string& source = {});
    void Trim(const std::wstring& directory, unsigned capacity);
    bool Relocate(const std::wstring& from, const std::wstring& to);
    bool Flush(unsigned milliseconds = 2000);
    bool TakeError() { return error.exchange(false); }
private:
    struct Job {
        std::wstring directory;
        PlaybackMemoryRecord record;
        unsigned capacity = 200;
        bool erase = false, position = true, settings = true;
        uint64_t stamp = 0;
    };
    void Run();
    void Enqueue(Job job);
    std::mutex mutex;
    std::condition_variable wake, drained;
    std::deque<Job> jobs;
    bool stopping = false, busy = false;
    std::atomic<bool> error{false};
    std::thread worker;
};
PlaybackMemoryStore& MemoryStore();
}
