// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <string_view>

namespace Bridge {
enum class SubtitleTextFormat { Unsupported, Srt, Ass, Ssa, Vtt };
inline SubtitleTextFormat SubtitleFileFormat(std::wstring_view path) {
    const auto dot = path.find_last_of(L'.');
    if (dot == std::wstring_view::npos) return SubtitleTextFormat::Unsupported;
    std::wstring ext(path.substr(dot));
    for (auto& c : ext) if (c >= L'A' && c <= L'Z') c += L'a' - L'A';
    if (ext == L".srt") return SubtitleTextFormat::Srt;
    if (ext == L".ass") return SubtitleTextFormat::Ass;
    if (ext == L".ssa") return SubtitleTextFormat::Ssa;
    if (ext == L".vtt") return SubtitleTextFormat::Vtt;
    return SubtitleTextFormat::Unsupported;
}

// STS stores all parsed text formats as ASS-like strings. Reading mode removes
// positioning/effects without turning ASS drawing coordinates into dialogue.
// This never modifies the native subtitle used for ordinary playback/export.
inline std::wstring SubtitleReadingText(std::wstring_view input, bool assComments = true) {
    std::wstring out;
    bool drawing = false;
    for (size_t i = 0; i < input.size();) {
        if (input[i] == L'{') {
            const auto end = input.find(L'}', i + 1);
            if (end != std::wstring_view::npos) {
                if (!assComments && input.find(L'\\', i + 1) >= end) {
                    if (!drawing) out.append(input.substr(i, end - i + 1));
                    i = end + 1;
                    continue;
                }
                int depth = 0;
                for (size_t j = i + 1; j < end; ++j) {
                    if (input[j] == L'(') ++depth;
                    else if (input[j] == L')' && depth) --depth;
                    else if (!depth && input[j] == L'\\' && j + 1 < end) {
                        const auto command = input[++j];
                        if (command == L'r') drawing = false;
                        if (command == L'p') {
                            size_t n = j + 1;
                            while (n < end && input[n] == L' ') ++n;
                            bool negative = n < end && input[n] == L'-';
                            if (n < end && (input[n] == L'+' || input[n] == L'-')) ++n;
                            if (n < end && input[n] >= L'0' && input[n] <= L'9') {
                                drawing = false;
                                while (n < end && input[n] >= L'0' && input[n] <= L'9') {
                                    if (input[n] != L'0') drawing = true;
                                    ++n;
                                }
                                drawing = drawing && !negative;
                            }
                        }
                    }
                }
                i = end + 1;
                continue;
            }
        }
        const auto c = input[i++];
        if (drawing) continue;
        if (c == L'\\' && i < input.size()) {
            if (input[i] == L'N' || input[i] == L'n') { out += L'\n'; ++i; continue; }
            if (input[i] == L'h') { out += L' '; ++i; continue; }
        }
        out += c;
    }
    return out;
}
}
