// SPDX-License-Identifier: GPL-3.0-or-later
#include "Subtitles/stdafx.h"
#include "apps/mplayerc/bridge/MatroskaSubtitleReader.h"
#include "apps/mplayerc/bridge/MatroskaSubtitleSource.h"
#include "apps/mplayerc/bridge/SubtitleTextSource.h"
#include "apps/mplayerc/bridge/SubtitleFilePreparation.h"
#include <iostream>

int wmain(int argc, wchar_t** argv) {
    if (!AfxWinInit(GetModuleHandleW(nullptr), nullptr, GetCommandLineW(), 0) || argc < 2) return 2;
    std::atomic<bool> cancelled{false};
    const std::wstring folder = argv[1];
    {
        CCritSec lock;
        std::array<Bridge::SubtitleFileRequest,2> requests;
        requests[0] = {L"primary",L"English, Source 2 [Default]","S_TEXT/UTF8"};
        requests[1] = {L"secondary",L"Chinese, Source 3 [Default]","S_TEXT/UTF8"};
        Bridge::SubtitleFilePreparation job(folder+L"/plain.mkv",requests,&lock);
        const auto deadline = GetTickCount64()+5000;
        while (!job.Ready() && GetTickCount64()<deadline) Sleep(5);
        if (!job.Ready() || job.status!=Bridge::SubtitleReadStatus::Ready || !job.result[0].text
            || job.result[0].uid!=102 || job.result[1].uid!=103 || job.result[1].text->GetCount()!=2) return 25;
        if (!job.Matches(folder+L"/plain.mkv",requests) || job.Matches(folder+L"/other.mkv",requests)) return 26;
        requests[0].key = L"different source";
        if (job.Matches(folder+L"/plain.mkv",requests)) return 27;
        requests[0].nativeName = L"Unknown";
        Bridge::SubtitleFilePreparation bad(folder+L"/plain.mkv",requests,&lock);
        const auto end = GetTickCount64()+5000;
        while (!bad.Ready() && GetTickCount64()<end) Sleep(5);
        if (!bad.Ready() || bad.status!=Bridge::SubtitleReadStatus::Unsupported || bad.result[0].text || bad.result[1].text) return 28;
        { Bridge::SubtitleFilePreparation cancel(folder+L"/plain.mkv",requests,&lock); }
        std::cout << "PASS: background source preparation, media/source identity, complete publication and failure fallback\n";
    }
    for (const auto name : {L"plain.mkv", L"zlib.mkv", L"header.mkv", L"pgs.mkv", L"ass.mkv"}) {
        std::wcout << L"Checking " << name << std::endl;
        const auto path = folder + L"/" + name;
        auto catalog = Bridge::ReadMatroskaSubtitles(path, {}, cancelled);
        if (catalog.status != Bridge::SubtitleReadStatus::Ready || catalog.tracks.size() != 2
            || catalog.visitedBlocks || catalog.payloadBytes || catalog.tracks[0].uid != 102
            || catalog.tracks[1].uid != 103 || catalog.tracks[0].number != 2
            || catalog.tracks[0].language != L"eng" || !catalog.tracks[0].supported) return 3;
        auto extracted = Bridge::ReadMatroskaSubtitles(path, {102, 103}, cancelled);
        if (extracted.status != Bridge::SubtitleReadStatus::Ready || extracted.tracks.size() != 2) return 4;
        const bool pgs = extracted.tracks[0].codec == "S_HDMV/PGS";
        const bool ass = extracted.tracks[0].codec == "S_TEXT/ASS";
        if (extracted.status != Bridge::SubtitleReadStatus::Ready || extracted.visitedBlocks < 6
            || extracted.tracks[0].packets.size() != (pgs ? 4 : 2)
            || extracted.tracks[1].packets.size() != (pgs ? 4 : 2)) return 4;
        const auto& first = extracted.tracks[0].packets[0];
        const auto& last = extracted.tracks[0].packets[pgs ? 2 : 1];
        if (first.start != 20000000 || first.stop != 50000000 || last.start != 900000000) return 5;
        if (pgs) {
            if (first.bytes.empty() || first.bytes.front() != 0x16 || extracted.payloadBytes < 10000) return 6;
            if (extracted.tracks[0].packets[1].stop != -1) return 7; // unknown duration stays unknown
            CCritSec lock;
            auto source = Bridge::MakeMatroskaPictureSource(extracted.tracks[0], &lock);
            if (!source) return 17;
            std::vector<uint32_t> pixels(640*360);
            SubPicDesc surface{}; surface.w=640; surface.h=360; surface.bpp=32; surface.pitch=640*4;
            surface.bits=reinterpret_cast<BYTE*>(pixels.data()); surface.vidrect=CRect(0,0,640,360);
            for (const auto now : {30000000ll,910000000ll,30000000ll}) {
                std::fill(pixels.begin(),pixels.end(),0xff000000); RECT box{};
                if (source->Render(surface,now,25,box)!=S_OK || IsRectEmpty(&box)) return 18;
            }
            auto forced = extracted.tracks[0]; forced.forced = true;
            auto forcedSource = Bridge::MakeMatroskaPictureSource(forced,&lock);
            g_bForcedSubtitle = true;
            const auto forcedPosition = forcedSource ? forcedSource->GetStartPosition(30000000,25) : nullptr;
            g_bForcedSubtitle = false;
            if (!forcedPosition) return 29;
        } else {
            if (!ass && (std::string(first.bytes.begin(), first.bytes.end()) != "line 2" || extracted.payloadBytes != 24)) return 8;
            auto source = Bridge::MakeMatroskaTextSource(extracted.tracks[0]);
            if (!source || source->GetCount()!=2 || source->GetAt(1).start!=90000) return 19;
            auto reading = Bridge::MakeSubtitleReadingCopy(*source);
            if (reading->GetStrW(0,false)!=(ass ? L"line 2\nsecond, clause" : L"line 2")) return 20;
            if (ass && (source->GetAt(0).actor!=L"Speaker" || source->GetAt(0).layer!=3
                || source->GetAt(0).readorder!=12 || source->GetAt(0).effect.IsEmpty())) return 21;
            auto unknown = extracted.tracks[0]; unknown.packets[0].stop=-1;
            if (Bridge::MakeMatroskaTextSource(unknown)) return 22;
        }
        auto single = Bridge::ReadMatroskaSubtitles(path, {103}, cancelled);
        if (single.status != Bridge::SubtitleReadStatus::Ready || !single.tracks[0].packets.empty()
            || single.tracks[1].packets.empty()) return 9;
        auto limited = Bridge::ReadMatroskaSubtitles(path, {102, 103}, cancelled, 8);
        if (limited.status != Bridge::SubtitleReadStatus::TooLarge || limited.payloadBytes
            || std::any_of(limited.tracks.begin(), limited.tracks.end(), [](const auto& t) { return !t.packets.empty(); })) return 10;
        auto missing = Bridge::ReadMatroskaSubtitles(path, {2}, cancelled); // number is NOT a UID
        if (missing.status != Bridge::SubtitleReadStatus::Unsupported) return 11;
        cancelled = true;
        if (Bridge::ReadMatroskaSubtitles(path, {102}, cancelled).status != Bridge::SubtitleReadStatus::Cancelled) return 12;
        cancelled = false;
        std::wcout << L"PASS: independent " << name << L" catalog, UID selection, full timeline, payload budget and cancellation\n";
    }
    // Opt-in private movies: log only counts/timing, never names or subtitle
    // contents. Read the first supported UID independently of playback.
    for (int arg = 2; arg < argc; ++arg) {
        auto catalog = Bridge::ReadMatroskaSubtitles(argv[arg], {}, cancelled);
        if (catalog.status != Bridge::SubtitleReadStatus::Ready) return 13;
        auto track = std::find_if(catalog.tracks.begin(), catalog.tracks.end(), [](const auto& t) { return t.supported && t.uid; });
        if (track == catalog.tracks.end()) return 14;
        auto extracted = Bridge::ReadMatroskaSubtitles(argv[arg], {track->uid}, cancelled);
        std::wcout << L"Private media " << arg - 1 << L": status=" << int(extracted.status) << L", tracks=" << catalog.tracks.size()
            << L", payload=" << extracted.payloadBytes << L", visited=" << extracted.visitedBlocks << L"\n";
        if (extracted.status != Bridge::SubtitleReadStatus::Ready) return 15;
        const auto selected = std::find_if(extracted.tracks.begin(), extracted.tracks.end(), [](const auto& t) { return !t.packets.empty(); });
        if (selected == extracted.tracks.end()) return 16;
        CCritSec lock;
        if (selected->codec=="S_HDMV/PGS") {
            if (!Bridge::MakeMatroskaPictureSource(*selected,&lock)) return 23;
        } else if (!Bridge::MakeMatroskaTextSource(*selected)) return 24;
        std::wcout << L"PASS: independent private source " << arg - 1 << L", packets=" << selected->packets.size()
            << L", first_ms=" << selected->packets.front().start / 10000 << L", last_ms=" << selected->packets.back().start / 10000 << L"\n";
    }
    return 0;
}
