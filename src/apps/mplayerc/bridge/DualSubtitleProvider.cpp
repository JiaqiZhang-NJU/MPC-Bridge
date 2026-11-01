// SPDX-License-Identifier: GPL-3.0-or-later
#include "stdafx.h"
#include "DualSubtitleProvider.h"

CDualSubtitleProvider::CDualSubtitleProvider(CCritSec* lock, const Bridge::DualSubtitleState& settings,
    const std::shared_ptr<CSimpleTextSubtitle> (&sources)[2], const CComPtr<ISubStream> (&streams)[2], const STSStyle& defaults,
    bool completePictures)
    : CRenderedTextSubtitle(lock), state(settings) {
    CAutoLock sourceLock(lock);
    // Keep an explicit transparent frame through gaps and global hiding.
    // Some external video renderers otherwise retain the last subtitle bitmap.
    Add(L"background", 0, INT32_MAX);
    for (int i = 0; i < 2; ++i) {
        if (!state.slots[i].visible) continue;
        auto picture = dynamic_cast<CRenderedHdmvSubtitle*>(streams[i].p);
        if (picture && picture->GetType() == ST_HDMV) {
            pictures[i] = picture;
            continue;
        }
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
    if (completePictures && (pictures[0] || pictures[1])) {
        // Immutable file sources retain every composition. Cache a static
        // image until its exact next cue boundary instead of invalidating it
        // on every video frame (especially across graph rate changes).
        boundaries = {0, REFERENCE_TIME(INT32_MAX)*10000};
        for (int i=0;i<2;++i) {
            const auto delay=REFERENCE_TIME(state.slots[i].delayMs)*10000;
            auto add=[&](REFERENCE_TIME t) {
                const REFERENCE_TIME limit=REFERENCE_TIME(INT32_MAX)*10000;
                if(t > -delay && t < limit-delay) boundaries.push_back(t+delay);
            };
            if (pictures[i]) {
                for(auto pos=pictures[i]->GetStartPosition(0,25);pos;pos=pictures[i]->GetNext(pos)) {
                    add(pictures[i]->GetStart(pos,25)); add(pictures[i]->GetStop(pos,25));
                }
            } else if (sources[i] && state.slots[i].visible) {
                for(size_t n=0;n<sources[i]->GetCount();++n) {
                    add(REFERENCE_TIME(sources[i]->GetAt(n).start)*10000);
                    add(REFERENCE_TIME(sources[i]->GetAt(n).end)*10000);
                }
            }
        }
        std::sort(boundaries.begin(),boundaries.end());
        boundaries.erase(std::unique(boundaries.begin(),boundaries.end()),boundaries.end());
    }
}

STDMETHODIMP_(POSITION) CDualSubtitleProvider::GetStartPosition(REFERENCE_TIME rt, double fps, bool clean) {
    if(boundaries.empty()) return CRenderedTextSubtitle::GetStartPosition(rt,fps,clean);
    auto it=std::upper_bound(boundaries.begin(),boundaries.end(),rt);
    return it==boundaries.end() ? nullptr : POSITION(it-boundaries.begin());
}
STDMETHODIMP_(POSITION) CDualSubtitleProvider::GetNext(POSITION pos) {
    if(boundaries.empty()) return CRenderedTextSubtitle::GetNext(pos);
    return size_t(pos)+1<boundaries.size() ? POSITION(size_t(pos)+1) : nullptr;
}
STDMETHODIMP_(REFERENCE_TIME) CDualSubtitleProvider::GetStart(POSITION pos, double fps) {
    return boundaries.empty() ? CRenderedTextSubtitle::GetStart(pos,fps) : boundaries[size_t(pos)-1];
}

STDMETHODIMP CDualSubtitleProvider::GetTextureSize(POSITION pos, SIZE& maximum, SIZE& virtualSize, POINT& origin) {
    if (!pictures[0] && !pictures[1]) return CRenderedTextSubtitle::GetTextureSize(pos, maximum, virtualSize, origin);
    std::lock_guard<std::mutex> guard(renderMutex);
    // Read metadata rather than forwarding our unrelated text POSITION. Keep
    // the largest native canvas through clear packets and source seeks.
    for (const auto& picture : pictures) if (picture) {
        const auto size = picture->GetCanvasSize();
        if (size.cx <= 0 || size.cy <= 0) continue;
        if (size.cx > 3840 || size.cy > 2160) return E_INVALIDARG;
        if (int64_t(size.cx)*size.cy > int64_t(canvas.cx)*canvas.cy) canvas = size;
    }
    if (!canvas.cx || !canvas.cy) return E_PENDING;
    maximum = virtualSize = canvas;
    origin = {0, 0};
    return S_OK;
}

STDMETHODIMP CDualSubtitleProvider::Render(SubPicDesc& spd, REFERENCE_TIME rt, double fps, RECT& bbox) {
    std::lock_guard<std::mutex> guard(renderMutex);
    bbox = CRect(0, 0, 0, 0);
    auto needsMoreSpace = [&] { if (!spaceWarningSent) { overflow = true; spaceWarningSent = true; } };
    if (!spd.bits || spd.bpp != 32 || spd.w <= 0 || spd.h <= 0 || spd.pitch < spd.w * 4
        || uint64_t(spd.w) * spd.h > 3840ull * 2160) return E_INVALIDARG;
    // Allocator reset can discard dirty-rectangle tracking while reusing memory.
    // Own the full output frame, including transparent pixels, on every render.
    for (int y = 0; y < spd.h; ++y) {
        std::fill_n(reinterpret_cast<uint32_t*>(spd.bits + y * spd.pitch), spd.w, 0xff000000u);
    }
    if (hidden) { bbox = CRect(0, 0, spd.w, spd.h); return S_OK; }
    CRect result;
    for (int i = 0; i < 2; ++i) {
        if (!lanes[i] && !pictures[i]) continue;
        const auto time = rt - int64_t(state.slots[i].delayMs) * 10000;
        // Negative time must not round toward zero and display a cue early.
        if (time < 0) continue;
        const auto band = Bridge::SubtitleLane(spd.h, state.layout, i);
        if (pictures[i]) {
            const auto nativeSize = pictures[i]->GetCanvasSize();
            if (nativeSize.cx <= 0 || nativeSize.cy <= 0) continue;
            if (nativeSize.cx > 3840 || nativeSize.cy > 2160) return E_INVALIDARG;
            const auto pos = pictures[i]->GetStartPosition(time, fps);
            if (!pos || pictures[i]->GetStart(pos, fps) > time || pictures[i]->GetStop(pos, fps) <= time) continue;
            surface.assign(size_t(nativeSize.cx) * nativeSize.cy, 0xff000000u);
            auto scratch = spd;
            scratch.w = nativeSize.cx; scratch.h = nativeSize.cy;
            scratch.pitch = scratch.w * 4; scratch.bits = reinterpret_cast<BYTE*>(surface.data());
            scratch.vidrect = CRect(0, 0, scratch.w, scratch.h);
            CRect drawn;
            if (pictures[i]->Render(scratch, time, fps, drawn) != S_OK || drawn.IsRectEmpty()) continue;
            drawn &= CRect(0, 0, scratch.w, scratch.h);
            if (drawn.IsRectEmpty()) continue;
            // Map source coordinates uniformly, then combine user scaling and
            // band fitting before the single resampling operation.
            const double mapping = std::min(double(spd.w)/scratch.w, double(spd.h)/scratch.h);
            const int mappedW = std::max(1, int(std::lround(drawn.Width()*mapping)));
            const int mappedH = std::max(1, int(std::lround(drawn.Height()*mapping)));
            const auto size = Bridge::FitSubtitleBitmap(mappedW, mappedH, spd.w - 8,
                band.bottom - band.top, state.slots[i].bitmapScale);
            if (!size.width || !size.height) continue;
            // Poster translations/annotations may span most of the original
            // frame. Keep every block, but warn when lane fitting makes the
            // requested picture less than half its intended display size.
            if (int64_t(size.height)*200 < int64_t(mappedH)*state.slots[i].bitmapScale) needsMoreSpace();
            const int left = (spd.w - size.width) / 2;
            const int top = (state.layout == 1 && i == 1) ? band.top : band.bottom - size.height;
            const uint32_t* pixels = surface.data() + size_t(drawn.top)*scratch.w + drawn.left;
            int stride = scratch.w;
            if (size.width != drawn.Width() || size.height != drawn.Height()) {
                cropped.resize(size_t(drawn.Width())*drawn.Height());
                for (int y = 0; y < drawn.Height(); ++y)
                    memcpy(cropped.data()+size_t(y)*drawn.Width(), pixels+size_t(y)*stride, size_t(drawn.Width())*4);
                scaled.resize(size_t(size.width)*size.height);
                const int filter = size.width < drawn.Width() || size.height < drawn.Height()
                    ? CResampleRGB32::FILTER_BOX : CResampleRGB32::FILTER_BILINEAR;
                HRESULT hr = bitmapResampler.SetParameters(size.width, size.height, drawn.Width(), drawn.Height(), filter, true);
                if (FAILED(hr)) return hr;
                hr = bitmapResampler.Process(reinterpret_cast<BYTE*>(scaled.data()), reinterpret_cast<const BYTE*>(cropped.data()));
                if (FAILED(hr)) return hr;
                pixels = scaled.data(); stride = size.width;
            }
            for (int y = 0; y < size.height; ++y) {
                auto dest = reinterpret_cast<uint32_t*>(spd.bits + (top + y) * spd.pitch) + left;
                memcpy(dest, pixels + size_t(y)*stride, size_t(size.width)*4);
            }
            result |= CRect(left, top, left + size.width, top + size.height);
            continue;
        }
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
        if (!fit) { needsMoreSpace(); continue; }
        surface.assign(size_t(spd.w)*spd.h, 0xff000000u);
        auto scratch = spd;
        scratch.pitch = spd.w * 4; scratch.bits = reinterpret_cast<BYTE*>(surface.data());
        scratch.vidrect = CRect(0, 0, spd.w, spd.h);
        CRect drawn;
        if (lanes[i]->Render(scratch, time, fps, drawn) != S_OK || drawn.IsRectEmpty()) continue;
        drawn &= CRect(0, 0, spd.w, spd.h);
        if (drawn.IsRectEmpty()) continue;
        if (drawn.Height() > band.bottom - band.top) { needsMoreSpace(); continue; }
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
