// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "DualSubtitleModel.h"
#include "Subtitles/RTS.h"
#include <atomic>

// RTS supplies the union of all cue boundaries. Each lane renders independently
// into one reused surface; disjoint destination bands need no alpha conversion.
class CDualSubtitleProvider final : public CRenderedTextSubtitle {
    CComPtr<CRenderedTextSubtitle> lanes[2];
    Bridge::DualSubtitleState state;
    STSStyle styles[2];
    std::vector<uint32_t> surface;
    std::mutex renderMutex;
    double appliedSize[2] = {};
public:
    std::atomic<bool> overflow{false};
    std::atomic<bool> hidden{false};
    CDualSubtitleProvider(CCritSec* lock, const Bridge::DualSubtitleState& settings,
        const std::shared_ptr<CSimpleTextSubtitle> (&sources)[2], const STSStyle& defaults);
    STDMETHODIMP Render(SubPicDesc& spd, REFERENCE_TIME rt, double fps, RECT& bbox) override;
    STDMETHODIMP_(bool) IsAnimated(POSITION) override { return false; }
};
