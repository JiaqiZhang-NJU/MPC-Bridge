// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <tuple>
#include <vector>
namespace Bridge {
struct QualityFormat { std::uint32_t index; int height, codec, fps; bool hdr; float bitrate; };
inline std::uint32_t SelectHeight(const std::vector<QualityFormat>& formats, int height, int codec,
    bool highFps, bool hdr, bool highBitrate) {
    std::uint32_t best = UINT32_MAX;
    std::tuple<bool, bool, bool, float> rank{};
    for (const auto& format : formats) {
        if (format.height != height) continue;
        const auto candidate = std::make_tuple(format.codec == codec, format.hdr == hdr,
            (format.fps > 30) == highFps, highBitrate ? format.bitrate : -format.bitrate);
        if (best == UINT32_MAX || candidate > rank) { best = format.index; rank = candidate; }
    }
    return best;
}
}
