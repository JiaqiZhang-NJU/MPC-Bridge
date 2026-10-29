// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "MatroskaSubtitleSource.h"
#include <array>
#include <thread>

namespace Bridge {
struct SubtitleFileRequest { std::wstring key, nativeName; std::string codec; };
struct PreparedFileSubtitle {
    uint64_t uid = 0;
    std::shared_ptr<CSimpleTextSubtitle> text;
    CComPtr<ISubStream> picture;
};
class SubtitleFilePreparation final {
    std::wstring path;
    std::array<SubtitleFileRequest,2> requests;
    std::atomic<bool> cancelled{false}, done{false};
    std::thread worker;
    void Run(CCritSec* lock) noexcept;
public:
    std::atomic<int64_t> scannedTime{0};
    SubtitleReadStatus status = SubtitleReadStatus::Unsupported;
    std::array<PreparedFileSubtitle,2> result;
    SubtitleFilePreparation(std::wstring path, const std::array<SubtitleFileRequest,2>& requests, CCritSec* lock);
    ~SubtitleFilePreparation();
    bool Matches(const std::wstring& file, const std::array<SubtitleFileRequest,2>& selected) const;
    bool Ready() const { return done.load(std::memory_order_acquire); }
};
}
