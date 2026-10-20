// SPDX-License-Identifier: GPL-3.0-or-later
#include "stdafx.h"
#include "DualSubtitleProvider.h"

CDualSubtitleProvider::CDualSubtitleProvider(CCritSec* lock, const Bridge::DualSubtitleState& settings,
    const std::shared_ptr<CSimpleTextSubtitle> (&sources)[2], const STSStyle& defaults)
    : CRenderedTextSubtitle(lock), state(settings) {
    // Keep an explicit transparent frame through gaps and global hiding.
    // Some external video renderers otherwise retain the last subtitle bitmap.
    Add(L"background", 0, INT32_MAX);
    for (int i = 0; i < 2; ++i) {
        if (!sources[i] || !state.slots[i].visible) continue;
        lanes[i] = new CRenderedTextSubtitle(lock);
        lanes[i]->Copy(*sources[i]);
        auto& style = styles[i]; style = defaults;
        style.fontSize = state.slots[i].fontSize; style.colors[0] = state.slots[i].color;
        style.scrAlignment = 2; style.relativeTo = STSStyle::WINDOW;
        style.marginRect = CRect(12, 8, 12, 8);
        style.fontAngleX = style.fontAngleY = style.fontAngleZ = 0;
        style.fontScaleX = style.fontScaleY = 100;
        style.outlineWidthX = style.outlineWidthY = 1;
        style.shadowDepthX = style.shadowDepthY = 1;
        lanes[i]->SetOverride(true, style); lanes[i]->SetAlignment(false, 0, 0);
        for (size_t n = 0; n < sources[i]->GetCount(); ++n) {
            const auto& entry = sources[i]->GetAt(n);
            int start, stop;
            if (Bridge::ShiftSubtitleInterval(entry.start, entry.end, state.slots[i].delayMs, start, stop)) {
                Add(L"cue", start, stop);
            }
        }
    }
    Sort();
}

STDMETHODIMP CDualSubtitleProvider::Render(SubPicDesc& spd, REFERENCE_TIME rt, double fps, RECT& bbox) {
    std::lock_guard<std::mutex> guard(renderMutex);
    bbox = CRect(0, 0, 0, 0); overflow = false;
    if (!spd.bits || spd.bpp != 32 || spd.w <= 0 || spd.h <= 0 || spd.pitch < spd.w * 4
        || uint64_t(spd.w) * spd.h > 3840ull * 2160) return E_INVALIDARG;
    // Allocator reset can discard dirty-rectangle tracking while reusing memory.
    // Own the full output frame, including transparent pixels, on every render.
    for (int y = 0; y < spd.h; ++y) {
        std::fill_n(reinterpret_cast<uint32_t*>(spd.bits + y * spd.pitch), spd.w, 0xff000000u);
    }
    if (hidden) { bbox = CRect(0, 0, spd.w, spd.h); return S_OK; }
    surface.resize(size_t(spd.w) * spd.h);
    CRect result;
    for (int i = 0; i < 2; ++i) {
        if (!lanes[i]) continue;
        const auto time = rt - int64_t(state.slots[i].delayMs) * 10000;
        // Negative time must not round toward zero and display a cue early.
        if (time < 0) continue;
        const auto band = Bridge::SubtitleLane(spd.h, state.layout, i);
        auto style = styles[i];
        bool fit = false;
        for (;;) {
            if (appliedSize[i] != style.fontSize) {
                lanes[i]->SetOverride(true, style); lanes[i]->Deinit(); appliedSize[i] = style.fontSize;
            }
            const auto needed = lanes[i]->MeasurePlainText(time, fps, CSize(spd.w, spd.h));
            if (needed.cy == 0) { fit = true; break; }
            if (needed.cy <= band.bottom - band.top && needed.cx <= spd.w - 8) { fit = true; break; }
            if (style.fontSize <= 8) break;
            style.fontSize = std::max(8.0, style.fontSize - 2);
        }
        if (!fit) { overflow = true; continue; }
        std::fill(surface.begin(), surface.end(), 0xff000000u);
        auto scratch = spd;
        scratch.pitch = spd.w * 4; scratch.bits = reinterpret_cast<BYTE*>(surface.data());
        scratch.vidrect = CRect(0, 0, spd.w, spd.h);
        CRect drawn;
        if (lanes[i]->Render(scratch, time, fps, drawn) != S_OK || drawn.IsRectEmpty()) continue;
        drawn &= CRect(0, 0, spd.w, spd.h);
        if (drawn.IsRectEmpty()) continue;
        if (drawn.Height() > band.bottom - band.top) { overflow = true; continue; }
        const int top = (state.layout == 1 && i == 1) ? band.top : band.bottom - drawn.Height();
        for (int row = 0; row < drawn.Height(); ++row) {
            memcpy(spd.bits + (top + row) * spd.pitch + drawn.left * 4,
                scratch.bits + (drawn.top + row) * scratch.pitch + drawn.left * 4, size_t(drawn.Width()) * 4);
        }
        result |= CRect(drawn.left, top, drawn.right, top + drawn.Height());
    }
    bbox = result;
    if (result.IsRectEmpty()) bbox = CRect(0, 0, spd.w, spd.h);
    return S_OK;
}
