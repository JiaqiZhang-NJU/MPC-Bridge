// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MediaCacheModel.h"
#include <functional>
#include <memory>
#include <string>

namespace Bridge {
struct CacheSettings {
    bool enabled = true;
    int forwardSeconds = 30, backSeconds = 15;
    std::uint64_t diskBytes = 256ull * 1024 * 1024;
    std::wstring directory; // empty uses LOCALAPPDATA/MPC-Bridge/Cache
};
struct CacheStatus {
    std::uint64_t bytes = 0, hits = 0, requests = 0;
    double forwardSeconds = -1;
    bool active = false, capped = false, failed = false;
};
// Owned by an online session, independently of the DirectShow graph.
// Only registered HTTP resources are exposed through random loopback routes.
class MediaCache final {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    explicit MediaCache(CacheSettings settings);
    ~MediaCache();
    MediaCache(const MediaCache&) = delete;
    MediaCache& operator=(const MediaCache&) = delete;
    std::wstring Register(const std::wstring& source, const std::wstring& userAgent,
        const std::wstring& referer); // empty means unsupported/unavailable, use native source
    void Select(const std::vector<std::wstring>& routes);
    void Prepare(const std::vector<std::wstring>& routes); // at most 2s of extra metadata preparation
    void Update(double position, double rate, bool paused);
    CacheStatus Status() const;
    void Clear();
    void CancelReaders(); // close a graph while keeping its completed blocks for rollback
    void Cancel(); // wakes network and local readers before graph shutdown
};
using IndexRead = std::function<bool(std::uint64_t, std::size_t, std::vector<std::uint8_t>&)>;
std::vector<MediaTrackIndex> BuildMediaTimeIndex(std::uint64_t length, const IndexRead& read);
}
