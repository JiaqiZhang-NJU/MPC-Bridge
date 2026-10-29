// SPDX-License-Identifier: GPL-3.0-or-later
#include "stdafx.h"
#include "SubtitleFilePreparation.h"
#include "SubtitleTextSource.h"

namespace Bridge {
SubtitleFilePreparation::SubtitleFilePreparation(std::wstring file, const std::array<SubtitleFileRequest,2>& selected, CCritSec* lock)
    : path(std::move(file)), requests(selected) { worker = std::thread([this,lock] { Run(lock); }); }
SubtitleFilePreparation::~SubtitleFilePreparation() {
    cancelled = true;
    if (worker.joinable()) worker.join();
}
bool SubtitleFilePreparation::Matches(const std::wstring& file, const std::array<SubtitleFileRequest,2>& selected) const {
    if (file != path) return false;
    for (int i=0;i<2;++i) if (requests[i].key != selected[i].key || requests[i].codec != selected[i].codec) return false;
    return true;
}
void SubtitleFilePreparation::Run(CCritSec* lock) noexcept {
    try {
        const auto catalog = ReadMatroskaSubtitles(path, {}, cancelled);
        status = catalog.status;
        if (status == SubtitleReadStatus::Ready) {
            std::vector<uint64_t> uids;
            for (int i=0;i<2;++i) {
                if (requests[i].key.empty()) continue;
                uint64_t match = 0;
                for (const auto& track : catalog.tracks) {
                    const bool ass = (track.codec == "S_TEXT/ASS" || track.codec == "S_TEXT/SSA")
                        && (requests[i].codec == "S_TEXT/ASS" || requests[i].codec == "S_TEXT/SSA");
                    if (!track.supported || track.name.empty() || (!ass && track.codec != requests[i].codec)) continue;
                    auto suffix = track.name;
                    if (track.isDefault && track.forced) suffix += L" [Default, Forced]";
                    else if (track.forced) suffix += L" [Forced]";
                    else if (track.isDefault) suffix += L" [Default]";
                    const auto& name = requests[i].nativeName;
                    // The native pin prepends a localized language. Match the
                    // complete track-name suffix, not a substring/ordinal.
                    if (name != suffix && (name.size() < suffix.size()+2
                        || name.compare(name.size()-suffix.size(),suffix.size(),suffix)
                        || name.compare(name.size()-suffix.size()-2,2,L", "))) continue;
                    if (match) { match = 0; break; } // ambiguous: retain native source
                    match = track.uid;
                }
                if (!match) { status = SubtitleReadStatus::Unsupported; break; }
                result[i].uid = match; uids.push_back(match);
            }
            if (status == SubtitleReadStatus::Ready) {
                auto extracted = ReadMatroskaSubtitles(path, uids, cancelled, 256ull*1024*1024, &scannedTime);
                status = extracted.status;
                if (status == SubtitleReadStatus::Ready && extracted.sourceVersion != catalog.sourceVersion) status = SubtitleReadStatus::SourceChanged;
                if (status == SubtitleReadStatus::Ready) for (int i=0;i<2;++i) {
                    if (!result[i].uid) continue;
                    const auto track = std::find_if(extracted.tracks.begin(),extracted.tracks.end(),[&](const auto& t) { return t.uid == result[i].uid; });
                    if (track == extracted.tracks.end() || cancelled) { status = SubtitleReadStatus::Cancelled; break; }
                    if (track->codec == "S_HDMV/PGS") result[i].picture = static_cast<ISubStream*>(MakeMatroskaPictureSource(*track,lock,cancelled).p);
                    else if (auto source = MakeMatroskaTextSource(*track,cancelled)) result[i].text = MakeSubtitleReadingCopy(*source);
                    if (!result[i].picture && !result[i].text) { status = SubtitleReadStatus::Unsupported; break; }
                }
            }
        }
    } catch (...) { status = SubtitleReadStatus::ReadError; }
    if (cancelled) status = SubtitleReadStatus::Cancelled;
    if (status != SubtitleReadStatus::Ready) result = {};
    done.store(true,std::memory_order_release);
}
}
