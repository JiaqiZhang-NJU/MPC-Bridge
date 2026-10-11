// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <cmath>
#include "MediaCache.h"
#include "ExtLib/Bento4/Core/Ap4File.h"
#include "ExtLib/Bento4/Core/Ap4SidxAtom.h"
#include <chrono>

namespace Bridge {
namespace {
std::uint32_t U32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8 | p[3];
}
std::uint64_t U64(const std::uint8_t* p) { return std::uint64_t(U32(p)) << 32 | U32(p + 4); }
bool SafeMetadata(const std::uint8_t* data, std::size_t length, int depth = 0) {
    if (depth > 12) return false;
    std::size_t offset = 0;
    while (offset < length) {
        if (length - offset < 8) return false;
        const auto p = data + offset;
        std::uint64_t size = U32(p); const auto type = U32(p + 4);
        std::size_t header = 8;
        if (size == 1) { if (length - offset < 16) return false; size = U64(p + 8); header = 16; }
        if (size < header || size > length - offset) return false;
        if (type == 0x636d6f76) return false; // compressed metadata has an independent expansion budget
        switch (type) {
        case 0x6d6f6f76: case 0x7472616b: case 0x6d646961: case 0x6d696e66: case 0x7374626c:
        case 0x65647473: case 0x6d766578: case 0x64696e66:
            if (!SafeMetadata(p + header, static_cast<std::size_t>(size - header), depth + 1)) return false;
            break;
        case 0x7374737a: // stsz can declare a huge implicit sample count in a small box
            if (size < header + 12 || U32(p + header + 8) > 250000) return false;
            if (!U32(p + header + 4) && U32(p + header + 8) * 4ull > size - header - 12) return false;
            break;
        case 0x73747364:
            if (size < header + 8 || U32(p + header + 4) > 32) return false;
            break;
        default: {
            unsigned stride = 0;
            switch (type) {
            case 0x73747473: case 0x63747473: case 0x636f3634: stride = 8; break;
            case 0x73747363: stride = 12; break;
            case 0x7374636f: case 0x73747373: stride = 4; break;
            }
            if (stride && (size < header + 8 || U32(p + header + 4) > 250000
                || U32(p + header + 4) * static_cast<std::uint64_t>(stride) > size - header - 8)) return false;
        }
        }
        offset += static_cast<std::size_t>(size);
    }
    return true;
}
}
std::vector<MediaTrackIndex> BuildMediaTimeIndex(std::uint64_t length, const IndexRead& read)
{
    std::vector<std::uint8_t> moov;
    std::vector<MediaTrackIndex> fragments;
    std::uint64_t position = 0, metadataBytes = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    // Scan top-level headers, skipping media bytes. No mutable splitter objects cross threads.
    for (int atoms = 0; atoms < 512 && position + 8 <= length; ++atoms) {
        if (std::chrono::steady_clock::now() > deadline) return {};
        std::vector<std::uint8_t> header;
        if (!read(position, static_cast<std::size_t>(std::min<std::uint64_t>(16, length - position)), header) || header.size() < 8) return {};
        std::uint64_t size = U32(header.data());
        const auto type = U32(header.data() + 4);
        std::size_t headerSize = 8;
        if (size == 1) {
            if (header.size() < 16) return {};
            size = U64(header.data() + 8); headerSize = 16;
        } else if (!size) size = length - position;
        if (size < headerSize || size > length - position) return {};
        if (type == 0x6d6f6f76 || type == 0x73696478) { // moov / sidx
            if (size > 8 * 1024 * 1024 || (metadataBytes += size) > 8 * 1024 * 1024) return {};
            std::vector<std::uint8_t> atom;
            if (!read(position, static_cast<std::size_t>(size), atom) || atom.size() != size) return {};
            if (type == 0x6d6f6f76) moov = std::move(atom);
            else {
                // Bento4's sidx reader masks reference_type and omits SAP flags. Validate them first.
                if (atom.size() <= headerSize) return {};
                const auto version = atom[headerSize];
                const std::size_t countOffset = headerSize + (version == 0 ? 22 : 30);
                if (version > 1 || atom.size() < countOffset + 2) return {};
                const unsigned count = (atom[countOffset] << 8) | atom[countOffset + 1];
                if (!count || atom.size() < countOffset + 2 + count * 12ull) return {};
                for (unsigned i = 0; i < count; ++i) {
                    const auto p = atom.data() + countOffset + 2 + i * 12;
                    const auto sap = U32(p + 8);
                    const auto sapType = (sap >> 28) & 7;
                    if ((U32(p) & 0x80000000) || !(sap & 0x80000000) || sapType < 1 || sapType > 2
                        || (sap & 0x0fffffff)) return {};
                }
                auto stream = new AP4_MemoryByteStream(atom.data(), atom.size());
                AP4_Atom* parsed = nullptr;
                const auto result = AP4_AtomFactory::DefaultFactory.CreateAtomFromStream(*stream, parsed);
                if (AP4_FAILED(result) || !parsed) { stream->Release(); return {}; }
                auto sidx = AP4_DYNAMIC_CAST(AP4_SidxAtom, parsed);
                MediaTrackIndex track;
                if (sidx && sidx->GetTimeScale()) {
                    for (unsigned i = 0; i < sidx->GetSampleTable().ItemCount(); ++i) {
                        const auto& s = sidx->GetSampleTable()[i];
                        if (!s.m_Size || s.m_Offset > length - position || s.m_Size > length - position - s.m_Offset) {
                            track.samples.clear(); break;
                        }
                        const double scale = sidx->GetTimeScale();
                        track.samples.push_back({s.m_StartTime / scale, (s.m_StartTime + s.m_Duration) / scale,
                            {position + s.m_Offset, position + s.m_Offset + s.m_Size - 1}, true});
                    }
                }
                delete parsed; stream->Release();
                if (track.samples.empty()) return {};
                fragments.push_back(std::move(track));
            }
        }
        position += size;
        // A single validated sidx covering the complete resource already describes all fragments.
        // Do not fetch each later moof just to rediscover that index (which could download the video).
        if (!moov.empty() && fragments.size() == 1 && fragments.front().samples.back().bytes.last == length - 1) break;
    }
    if (moov.empty() || !SafeMetadata(moov.data(), moov.size())) return {};
    auto stream = new AP4_MemoryByteStream(moov.data(), moov.size());
    std::vector<MediaTrackIndex> tracks;
    {
        AP4_File file(*stream, false);
        auto movie = file.GetMovie();
        if (movie && movie->HasFragments()) {
            // Only a complete sidx snapshot is supported. Never claim seconds from partial moofs.
            if (fragments.size() == 1) tracks = std::move(fragments);
        } else if (movie) {
            std::size_t totalSamples = 0;
            for (auto item = movie->GetTracks().FirstItem(); item; item = item->GetNext()) {
                auto track = item->GetData();
                if (track->GetType() != AP4_Track::TYPE_VIDEO && track->GetType() != AP4_Track::TYPE_AUDIO) continue;
                const auto count = track->GetSampleCount();
                const double scale = track->GetMediaTimeScale();
                if (!count || !scale || (totalSamples += count) > 250000) { tracks.clear(); break; }
                MediaTrackIndex snapshot;
                snapshot.samples.reserve(count);
                for (unsigned i = 0; i < count; ++i) {
                    AP4_Sample s;
                    if (AP4_FAILED(track->GetSample(i, s)) || !s.GetSize() || s.GetOffset() >= length
                        || s.GetSize() > length - s.GetOffset()) { snapshot.samples.clear(); break; }
                    snapshot.samples.push_back({std::max(0.0, s.GetCts() / scale),
                        std::max(0.0, (s.GetCts() + static_cast<double>(s.GetDuration())) / scale),
                        {s.GetOffset(), s.GetOffset() + s.GetSize() - 1}, s.IsSync()});
                }
                if (snapshot.samples.empty()) { tracks.clear(); break; }
                tracks.push_back(std::move(snapshot));
            }
        }
    }
    stream->Release();
    return tracks;
}
}
