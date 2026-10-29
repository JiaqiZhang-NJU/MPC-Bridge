// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace Bridge {
constexpr int SubtitleDelayLimit = 600000;
struct SubtitleSlot {
    std::wstring path;
    int embeddedTrack = -1; // -1: external file; otherwise a track in this media
    std::wstring embeddedKey; // name/type identity, checked again after reopening
    bool visible = true;
    int delayMs = 0;
    int fontSize = 20;
    int bitmapScale = 100; // percent of the native picture in the display area
    uint32_t color = 0x00ffffff;
};
struct DualSubtitleState {
    SubtitleSlot slots[2];
    bool enabled = false;
    int layout = 0; // bottom pair, or main bottom / secondary top
    DualSubtitleState() { slots[1].color = 0x0080ffff; }
};
inline bool ValidSubtitleSlot(const SubtitleSlot& slot) {
    return slot.embeddedTrack >= -1 && slot.delayMs >= -SubtitleDelayLimit && slot.delayMs <= SubtitleDelayLimit
        && slot.fontSize >= 8 && slot.fontSize <= 48 && slot.bitmapScale >= 25 && slot.bitmapScale <= 200 && slot.color <= 0xffffff;
}
inline int ResolveEmbeddedTrack(const SubtitleSlot& slot, const std::vector<std::wstring>& keys) {
    if (slot.embeddedTrack < 0 || slot.embeddedKey.empty()) return -1;
    if (size_t(slot.embeddedTrack) < keys.size() && keys[slot.embeddedTrack] == slot.embeddedKey) return slot.embeddedTrack;
    int match = -1;
    for (size_t i = 0; i < keys.size(); ++i) if (keys[i] == slot.embeddedKey) {
        if (match >= 0) return -1; // ambiguous identity: never silently choose another language
        match = static_cast<int>(i);
    }
    return match;
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
struct SubtitleBitmapSize { int width = 0, height = 0; };
inline SubtitleBitmapSize FitSubtitleBitmap(int width, int height, int availableWidth, int availableHeight, int scale) {
    if (width <= 0 || height <= 0 || availableWidth <= 0 || availableHeight <= 0 || scale < 25 || scale > 200) return {};
    int64_t w = std::max<int64_t>(1, int64_t(width) * scale / 100);
    int64_t h = std::max<int64_t>(1, int64_t(height) * scale / 100);
    if (w > availableWidth) { h = std::max<int64_t>(1, h * availableWidth / w); w = availableWidth; }
    if (h > availableHeight) { w = std::max<int64_t>(1, w * availableHeight / h); h = availableHeight; }
    return {static_cast<int>(w), static_cast<int>(h)};
}
}
