// SPDX-License-Identifier: GPL-3.0-or-later
#include "stdafx.h"
#include "MatroskaSubtitleSource.h"
#include "DSUtil/text.h"

namespace Bridge {
std::shared_ptr<CSimpleTextSubtitle> MakeMatroskaTextSource(const MatroskaSubtitleTrack& track) {
    const std::atomic<bool> cancelled{false};
    return MakeMatroskaTextSource(track,cancelled);
}
std::shared_ptr<CSimpleTextSubtitle> MakeMatroskaTextSource(const MatroskaSubtitleTrack& track, const std::atomic<bool>& cancelled) {
    const bool ass = track.codec == "S_TEXT/ASS" || track.codec == "S_TEXT/SSA";
    if (!ass && track.codec != "S_TEXT/UTF8" && track.codec != "S_TEXT/WEBVTT") return nullptr;
    if (track.packets.empty()) return nullptr;
    auto source = std::make_shared<CSimpleTextSubtitle>();
    if (ass && !track.codecPrivate.empty()) {
        auto header = track.codecPrivate;
        // Reuse STS style/header parsing, exactly as native embedded ASS does.
        source->Open(header.data(), static_cast<int>(header.size()), CP_UTF8, track.name.c_str());
    }
    source->m_subtitleType = ass ? (track.codec == "S_TEXT/SSA" ? Subtitle::SSA : Subtitle::ASS)
        : (track.codec == "S_TEXT/WEBVTT" ? Subtitle::VTT : Subtitle::SRT);
    source->m_name = track.name.c_str(); source->CreateDefaultStyle(DEFAULT_CHARSET);
    for (const auto& packet : track.packets) {
        if (cancelled) return nullptr;
        // Unknown durations must not turn into fabricated millisecond cues.
        if (packet.stop <= packet.start || packet.stop / 10000 > INT32_MAX || packet.bytes.size() > INT32_MAX) return nullptr;
        if (packet.stop <= 0) continue;
        const auto start = static_cast<int>(std::max<int64_t>(0, packet.start / 10000));
        const auto stop = static_cast<int>(packet.stop / 10000);
        CStringW text = UTF8ToWStr(CStringA(reinterpret_cast<const char*>(packet.bytes.data()), static_cast<int>(packet.bytes.size()))).Trim();
        if (text.IsEmpty()) continue;
        if (!ass) source->Add(text, start, stop);
        else {
            std::list<CStringW> fields;
            Explode(text, fields, L',', 9);
            if (fields.size() != 9) return nullptr;
            auto field = fields.cbegin(); STSEntry cue;
            cue.readorder = wcstol(*field++, nullptr, 10); cue.layer = wcstol(*field++, nullptr, 10);
            cue.style = *field++; cue.actor = *field++;
            cue.marginRect.left = wcstol(*field++, nullptr, 10); cue.marginRect.right = wcstol(*field++, nullptr, 10);
            cue.marginRect.top = cue.marginRect.bottom = wcstol(*field++, nullptr, 10);
            cue.effect = *field++; cue.str = *field++;
            source->Add(cue.str, start, stop, cue.style, cue.actor, cue.effect, cue.marginRect, cue.layer, cue.readorder);
        }
    }
    source->Sort(); source->CreateSegments();
    return source->IsEmpty() ? nullptr : source;
}

CComPtr<CRenderedHdmvSubtitle> MakeMatroskaPictureSource(const MatroskaSubtitleTrack& track, CCritSec* lock) {
    const std::atomic<bool> cancelled{false};
    return MakeMatroskaPictureSource(track,lock,cancelled);
}
CComPtr<CRenderedHdmvSubtitle> MakeMatroskaPictureSource(const MatroskaSubtitleTrack& track, CCritSec* lock, const std::atomic<bool>& cancelled) {
    if (track.codec != "S_HDMV/PGS" || track.packets.empty() || !lock) return nullptr;
    // Unlike a live pin, this source is complete and cannot be refilled by a
    // playback seek. Keep earlier objects when rendering later movie times.
    auto name = track.name;
    if (track.forced) name += L" [Forced]";
    CComPtr<CRenderedHdmvSubtitle> source = new CRenderedHdmvSubtitle(lock, ST_HDMV, name.c_str(), 0, true);
    for (const auto& packet : track.packets) {
        if (cancelled) return nullptr;
        if (packet.bytes.empty() || packet.bytes.size() > LONG_MAX) return nullptr;
        auto bytes = packet.bytes;
        if (FAILED(source->ParseSample(bytes.data(), static_cast<long>(bytes.size()), packet.start, packet.stop))) return nullptr;
    }
    if (FAILED(source->EndOfStream())) return nullptr;
    return source;
}
}
