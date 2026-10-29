// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <atomic>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Bridge {
enum class SubtitleReadStatus { Ready, Cancelled, Unsupported, InvalidFile, ReadError, TooLarge, SourceChanged };
struct MatroskaSubtitlePacket {
    int64_t start = 0; // media time in 100 ns, matching the native splitter
    int64_t stop = -1; // -1 means the container did not specify a duration
    std::vector<uint8_t> bytes;
};
struct MatroskaSubtitleTrack {
    uint64_t number = 0, uid = 0;
    std::wstring name, language;
    std::string codec;
    bool isDefault = false, forced = false, supported = false;
    std::vector<uint8_t> codecPrivate;
    std::vector<MatroskaSubtitlePacket> packets;
};
struct MatroskaSubtitleReadResult {
    SubtitleReadStatus status = SubtitleReadStatus::InvalidFile;
    std::vector<MatroskaSubtitleTrack> tracks;
    uint64_t payloadBytes = 0, visitedBlocks = 0;
    std::array<uint64_t,4> sourceVersion{}; // volume, file ID, size, last write
};
// Worker-thread API. Opens a separate read-only local-file handle; never
// connects to or changes a playback graph. Empty selection reads metadata only.
// UIDs are scoped to this file, not interchangeable with track indexes/numbers.
// Failed/cancelled extraction discards ALL packet data; metadata may remain.
MatroskaSubtitleReadResult ReadMatroskaSubtitles(const std::wstring& path,
    const std::vector<uint64_t>& selectedUids, const std::atomic<bool>& cancelled,
    uint64_t payloadBudget = 128ull * 1024 * 1024, std::atomic<int64_t>* scannedTime = nullptr);
}
