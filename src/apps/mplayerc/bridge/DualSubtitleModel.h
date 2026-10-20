// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>

namespace Bridge {
constexpr int SubtitleDelayLimit = 600000;
struct SubtitleSlot {
    std::wstring path;
    bool visible = true;
    int delayMs = 0;
    int fontSize = 20;
    uint32_t color = 0x00ffffff;
};
struct DualSubtitleState {
    SubtitleSlot slots[2];
    bool enabled = false;
    int layout = 0; // bottom pair, or main bottom / secondary top
    DualSubtitleState() { slots[1].color = 0x0080ffff; }
};
inline bool ValidSubtitleSlot(const SubtitleSlot& slot) {
    return slot.delayMs >= -SubtitleDelayLimit && slot.delayMs <= SubtitleDelayLimit
        && slot.fontSize >= 8 && slot.fontSize <= 48 && slot.color <= 0xffffff;
}
inline bool ShiftSubtitleInterval(int start, int end, int delay, int& first, int& last) {
    const int64_t a = int64_t(start) + delay, b = int64_t(end) + delay;
    if (b <= 0 || b <= a || a > INT32_MAX || b > INT32_MAX) return false;
    first = static_cast<int>(std::max<int64_t>(0, a)); last = static_cast<int>(b);
    return true;
}
struct SubtitleBand { int top, bottom; };
inline SubtitleBand SubtitleLane(int height, int layout, int slot) {
    if (layout == 1) return slot ? SubtitleBand{height * 3 / 100, height * 45 / 100}
        : SubtitleBand{height * 55 / 100, height * 97 / 100};
    return slot ? SubtitleBand{height * 82 / 100, height * 97 / 100}
        : SubtitleBand{height * 65 / 100, height * 80 / 100};
}
}
