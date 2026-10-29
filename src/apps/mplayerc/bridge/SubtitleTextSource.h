// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "SubtitleCatalog.h"
#include "Subtitles/STS.h"
#include <memory>

namespace Bridge {
inline bool SupportedSubtitleTextType(Subtitle::SubType type) {
    return type == Subtitle::SRT || type == Subtitle::ASS || type == Subtitle::SSA || type == Subtitle::VTT;
}
inline const wchar_t* SubtitleTextFormatName(Subtitle::SubType type) {
    if (type == Subtitle::SRT) return L"SRT";
    if (type == Subtitle::ASS) return L"ASS";
    if (type == Subtitle::SSA) return L"SSA";
    if (type == Subtitle::VTT) return L"VTT";
    return L"Text";
}
inline std::shared_ptr<CSimpleTextSubtitle> MakeSubtitleReadingCopy(CSimpleTextSubtitle& stream) {
    auto copy = std::make_shared<CSimpleTextSubtitle>();
    copy->m_subtitleType = Subtitle::SRT;
    copy->m_dstScreenSize = DEFSCREENSIZE;
    copy->CreateDefaultStyle(DEFAULT_CHARSET);
    for (size_t i = 0; i < stream.GetCount(); ++i) {
        const auto& cue = stream.GetAt(i);
        const auto raw = stream.GetStrW(static_cast<int>(i), true);
        const bool ass = stream.m_subtitleType == Subtitle::ASS || stream.m_subtitleType == Subtitle::SSA;
        const auto text = SubtitleReadingText(std::wstring_view(raw.GetString(), raw.GetLength()), ass);
        copy->Add(text.c_str(), cue.start, cue.end, L"Default", cue.actor, L"", CRect(0,0,0,0), cue.layer, cue.readorder);
    }
    return copy;
}
}
