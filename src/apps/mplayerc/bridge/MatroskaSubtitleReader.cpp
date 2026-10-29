// SPDX-License-Identifier: GPL-3.0-or-later
#include "stdafx.h"
#include "MatroskaSubtitleReader.h"
#include "DSUtil/text.h"
#include "filters/parser/BaseSplitter/AsyncReader.h"
#include "filters/parser/MatroskaSplitter/MatroskaFile.h"
#include "ExtLib/zlib/zlib.h"
#include <set>

namespace Bridge {
namespace {
using namespace MatroskaReader;
bool SameFile(const BY_HANDLE_FILE_INFORMATION& a, const BY_HANDLE_FILE_INFORMATION& b) {
    return a.dwVolumeSerialNumber == b.dwVolumeSerialNumber && a.nFileIndexHigh == b.nFileIndexHigh
        && a.nFileIndexLow == b.nFileIndexLow && a.nFileSizeHigh == b.nFileSizeHigh && a.nFileSizeLow == b.nFileSizeLow
        && a.ftLastWriteTime.dwHighDateTime == b.ftLastWriteTime.dwHighDateTime
        && a.ftLastWriteTime.dwLowDateTime == b.ftLastWriteTime.dwLowDateTime;
}
bool SupportedCodec(const CStringA& codec) {
    return codec == "S_TEXT/UTF8" || codec == "S_TEXT/ASS" || codec == "S_TEXT/SSA"
        || codec == "S_TEXT/WEBVTT" || codec == "S_HDMV/PGS";
}
SubtitleReadStatus ExpandPacket(CBinary& data, TrackEntry& entry, uint64_t scope, uint64_t limit,
    const std::atomic<bool>& cancelled) {
    std::vector<ContentEncoding*> encodings;
    for (const auto& encoding : entry.ces.ce) if (encoding->ContentEncodingScope & scope) encodings.push_back(encoding.get());
    // Encodings are decoded in reverse encoding order. Bound decompression
    // before allocation; the splitter's generic Expand has no output limit.
    std::sort(encodings.begin(), encodings.end(), [](const auto a, const auto b) { return a->ContentEncodingOrder > b->ContentEncodingOrder; });
    for (const auto encoding : encodings) {
        if (cancelled) return SubtitleReadStatus::Cancelled;
        if (encoding->ContentEncodingType != ContentEncoding::Compression) return SubtitleReadStatus::Unsupported;
        auto& compression = encoding->cc;
        if (compression.ContentCompAlgo == ContentCompression::HDRSTRIP) {
            if (compression.ContentCompSettings.size() > limit || data.size() > limit - compression.ContentCompSettings.size()) return SubtitleReadStatus::TooLarge;
            data.insert(data.begin(), compression.ContentCompSettings.begin(), compression.ContentCompSettings.end());
        } else if (compression.ContentCompAlgo == ContentCompression::ZLIB) {
            z_stream stream{};
            if (inflateInit(&stream) != Z_OK) return SubtitleReadStatus::ReadError;
            stream.next_in = data.data(); stream.avail_in = static_cast<uInt>(data.size());
            CBinary output;
            int code = Z_OK;
            while (code == Z_OK && !cancelled) {
                const auto used = stream.total_out;
                if (output.size() >= limit) break;
                output.resize(static_cast<size_t>(std::min<uint64_t>(limit, std::max<uint64_t>(4096, output.size() * 2))));
                stream.next_out = output.data() + used; stream.avail_out = static_cast<uInt>(output.size() - used);
                code = inflate(&stream, Z_NO_FLUSH);
            }
            const auto length = stream.total_out; const auto remaining = stream.avail_in;
            inflateEnd(&stream);
            if (cancelled) return SubtitleReadStatus::Cancelled;
            if (code == Z_OK && output.size() == limit) return SubtitleReadStatus::TooLarge;
            if (code != Z_STREAM_END || remaining) return SubtitleReadStatus::InvalidFile;
            output.resize(length); data.swap(output);
        } else return SubtitleReadStatus::Unsupported;
    }
    return data.size() <= limit ? SubtitleReadStatus::Ready : SubtitleReadStatus::TooLarge;
}
}

MatroskaSubtitleReadResult ReadMatroskaSubtitles(const std::wstring& path,
    const std::vector<uint64_t>& selectedUids, const std::atomic<bool>& cancelled, uint64_t payloadBudget, std::atomic<int64_t>* scannedTime) {
    MatroskaSubtitleReadResult result;
    auto fail = [&](SubtitleReadStatus status) {
        result.status = status; result.payloadBytes = 0;
        for (auto& track : result.tracks) { track.packets.clear(); track.codecPrivate.clear(); }
        return std::move(result);
    };
    if (cancelled) return fail(SubtitleReadStatus::Cancelled);
    if (path.empty() || PathIsURLW(path.c_str())) return fail(SubtitleReadStatus::Unsupported);
    HRESULT hr = S_OK;
    CComPtr<IAsyncReader> reader = new CAsyncFileReader(path.c_str(), hr, FALSE);
    if (FAILED(hr)) return fail(SubtitleReadStatus::ReadError);
    CComQIPtr<IFileHandle> handle = reader;
    BY_HANDLE_FILE_INFORMATION before{}, after{};
    if (!handle || !GetFileInformationByHandle(handle->GetFileHandle(), &before)) return fail(SubtitleReadStatus::ReadError);
    result.sourceVersion = {before.dwVolumeSerialNumber, (uint64_t(before.nFileIndexHigh)<<32)|before.nFileIndexLow,
        (uint64_t(before.nFileSizeHigh)<<32)|before.nFileSizeLow,
        (uint64_t(before.ftLastWriteTime.dwHighDateTime)<<32)|before.ftLastWriteTime.dwLowDateTime};
    CMatroskaFile file(reader, hr);
    if (FAILED(hr)) return fail(SubtitleReadStatus::InvalidFile);
    if (cancelled) return fail(SubtitleReadStatus::Cancelled);
    const auto& segment = file.m_segment;
    std::vector<TrackEntry*> entries;
    std::set<uint64_t> numbers, uids, selected(selectedUids.begin(), selectedUids.end());
    for (const auto& group : segment.Tracks) for (const auto& entry : group->TrackEntries) {
        if (entry->TrackType != TrackEntry::TypeSubtitle) continue;
        MatroskaSubtitleTrack track;
        track.number = entry->TrackNumber; track.uid = entry->TrackUID;
        track.name = entry->Name.GetString();
        const CStringA language = entry->LanguageBCP47.IsEmpty() ? entry->Language : entry->LanguageBCP47;
        track.language = UTF8ToWStr(language).GetString();
        const auto codec = entry->CodecID.ToString(); track.codec = codec.GetString();
        track.isDefault = entry->FlagDefault != 0; track.forced = entry->FlagForced != 0;
        track.supported = track.uid && SupportedCodec(codec) && entry->TrackTimecodeScale == 1.0;
        for (const auto& encoding : entry->ces.ce) {
            if (encoding->ContentEncodingType != ContentEncoding::Compression || (encoding->ContentEncodingScope & 4)
                || (encoding->cc.ContentCompAlgo != ContentCompression::ZLIB && encoding->cc.ContentCompAlgo != ContentCompression::HDRSTRIP)) track.supported = false;
        }
        if (!track.number || !numbers.insert(track.number).second || (track.uid && !uids.insert(track.uid).second)) {
            return fail(SubtitleReadStatus::InvalidFile);
        }
        if (selected.count(track.uid)) {
            if (!track.uid || !track.supported) return fail(SubtitleReadStatus::Unsupported);
            CBinary data = entry->CodecPrivate;
            const auto status = ExpandPacket(data, *entry, ContentEncoding::TracksPrivateData,
                std::min<uint64_t>(4ull * 1024 * 1024, payloadBudget - result.payloadBytes), cancelled);
            if (status != SubtitleReadStatus::Ready) return fail(status);
            if (data.size() > payloadBudget - result.payloadBytes) return fail(SubtitleReadStatus::TooLarge);
            track.codecPrivate.assign(data.begin(), data.end()); result.payloadBytes += data.size();
        }
        result.tracks.push_back(std::move(track)); entries.push_back(entry.get());
    }
    for (auto uid : selected) if (!uid || !uids.count(uid)) return fail(SubtitleReadStatus::Unsupported);
    if (!selected.empty()) {
        CMatroskaNode root(&file);
        auto node = root.Child(MATROSKA_ID_SEGMENT);
        auto cluster = node ? node->Child(MATROSKA_ID_CLUSTER) : nullptr;
        if (!cluster) return fail(SubtitleReadStatus::InvalidFile);
        do {
            if (cancelled) return fail(SubtitleReadStatus::Cancelled);
            Cluster time;
            if (FAILED(time.ParseTimeCode(cluster.get()))) return fail(SubtitleReadStatus::InvalidFile);
            const auto clusterTime = segment.GetRefTime(time.TimeCode);
            const auto offset = clusterTime >= file.m_rtOffset ? file.m_rtOffset : 0;
            if (scannedTime) *scannedTime = clusterTime - offset;
            // Read only block headers for unselected streams. In particular,
            // this never reads/copies a movie's compressed video/audio payload.
            auto block = cluster->Child();
            if (block) do {
                if (cancelled) return fail(SubtitleReadStatus::Cancelled);
                if (block->m_id != MATROSKA_ID_BLOCKGROUP && block->m_id != MATROSKA_ID_SIMPLEBLOCK) continue;
                ++result.visitedBlocks;
                auto simple = block->m_id == MATROSKA_ID_BLOCKGROUP ? block->Child(0xA1) : block->Copy();
                if (!simple) return fail(SubtitleReadStatus::InvalidFile);
                SimpleBlock header;
                if (FAILED(header.Parse(simple.get(), false))) return fail(SubtitleReadStatus::InvalidFile);
                size_t index = 0;
                for (; index < result.tracks.size(); ++index) if (result.tracks[index].number == header.TrackNumber) break;
                if (index == result.tracks.size() || !selected.count(result.tracks[index].uid)) continue;
                // Do not invent per-frame timing for a laced subtitle block.
                if (header.Lacing & 6) return fail(SubtitleReadStatus::Unsupported);
                constexpr uint64_t packetLimit = 4ull * 1024 * 1024;
                if (block->m_len > packetLimit || result.tracks[index].packets.size() >= 200000) return fail(SubtitleReadStatus::TooLarge);
                BlockGroup group;
                if (block->m_id == MATROSKA_ID_BLOCKGROUP) hr = group.Parse(block.get(), true);
                else hr = group.Block.Parse(block.get(), true);
                if (FAILED(hr) || group.Block.BlockData.size() != 1) return fail(SubtitleReadStatus::InvalidFile);
                auto& bytes = *group.Block.BlockData.front();
                const auto status = ExpandPacket(bytes, *entries[index], ContentEncoding::AllFrameContents,
                    std::min(packetLimit, payloadBudget - result.payloadBytes), cancelled);
                if (status != SubtitleReadStatus::Ready) return fail(status);
                if (bytes.size() > packetLimit || bytes.size() > payloadBudget - result.payloadBytes) return fail(SubtitleReadStatus::TooLarge);
                MatroskaSubtitlePacket packet;
                packet.start = clusterTime + segment.GetRefTime(group.Block.TimeCode) - offset;
                int64_t duration = group.BlockDuration.IsValid() ? segment.GetRefTime(group.BlockDuration)
                    : int64_t(entries[index]->DefaultDuration / 100);
                if (duration > 0) {
                    if (packet.start > INT64_MAX - duration) return fail(SubtitleReadStatus::InvalidFile);
                    packet.stop = packet.start + duration;
                }
                packet.bytes.assign(bytes.begin(), bytes.end()); result.payloadBytes += bytes.size();
                result.tracks[index].packets.push_back(std::move(packet));
            } while (block->Next());
            if (FAILED(file.GetLastReadError())) return fail(SubtitleReadStatus::ReadError);
        } while (cluster->Next(true));
    }
    if (cancelled) return fail(SubtitleReadStatus::Cancelled);
    if (!GetFileInformationByHandle(handle->GetFileHandle(), &after)) return fail(SubtitleReadStatus::ReadError);
    if (!SameFile(before, after)) return fail(SubtitleReadStatus::SourceChanged);
    result.status = SubtitleReadStatus::Ready;
    return result;
}
}
