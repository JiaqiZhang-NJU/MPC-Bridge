// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "DualSubtitleModel.h"
#include "Subtitles/RTS.h"
#include "Subtitles/RenderedHdmvSubtitle.h"
#include <atomic>

// Text-only combinations use RTS cue boundaries. Live PGS combinations use the
// queue's one-frame contract. Both lanes share a scratch surface and draw into
// disjoint destination bands.
class CDualSubtitleProvider final : public CRenderedTextSubtitle {
    CComPtr<CRenderedTextSubtitle> lanes[2];
    CComPtr<CRenderedHdmvSubtitle> pictures[2];
    Bridge::DualSubtitleState state;
    STSStyle styles[2];
    std::vector<uint32_t> surface;
    std::vector<uint32_t> cropped, scaled;
    CResampleRGB32 bitmapResampler;
    SIZE canvas = {};
    std::mutex renderMutex;
    double appliedSize[2] = {};
    bool spaceWarningSent = false; // protected by renderMutex, once per settings snapshot
    std::vector<REFERENCE_TIME> boundaries;
public:
    bool RenderOnQueueMiss() const override { return true; }
    std::atomic<bool> overflow{false};
    std::atomic<bool> hidden{false};
    CDualSubtitleProvider(CCritSec* lock, const Bridge::DualSubtitleState& settings,
        const std::shared_ptr<CSimpleTextSubtitle> (&sources)[2], const CComPtr<ISubStream> (&streams)[2], const STSStyle& defaults,
        bool completePictures = false);
    STDMETHODIMP_(POSITION) GetStartPosition(REFERENCE_TIME rt, double fps, bool clean = false) override;
    STDMETHODIMP_(POSITION) GetNext(POSITION pos) override;
    STDMETHODIMP_(REFERENCE_TIME) GetStart(POSITION pos, double fps) override;
    STDMETHODIMP Render(SubPicDesc& spd, REFERENCE_TIME rt, double fps, RECT& bbox) override;
    STDMETHODIMP GetTextureSize(POSITION pos, SIZE& maximum, SIZE& virtualSize, POINT& origin) override;
    STDMETHODIMP_(REFERENCE_TIME) GetStop(POSITION pos, double fps) override {
        // PGS buffers are live across decoder callbacks. Even a previously
        // known gap may acquire an unknown-end cue during a seek, before the
        // UI publishes a replacement. Never sample these sources in the future.
        if (!boundaries.empty()) return boundaries[size_t(pos)];
        return pictures[0] || pictures[1] ? ISubPicProvider::UNKNOWN_TIME : CRenderedTextSubtitle::GetStop(pos, fps);
    }
    STDMETHODIMP_(bool) IsAnimated(POSITION) override { return false; }
};
