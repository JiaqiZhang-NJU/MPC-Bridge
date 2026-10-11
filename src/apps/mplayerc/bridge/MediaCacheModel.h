// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace Bridge {
struct ByteRange {
    std::uint64_t first = 0, last = 0; // inclusive, never a sparse-file hole
};
enum class RangeResult { Full, Partial, Invalid, Unsatisfiable };
inline bool Decimal(std::string_view text, std::uint64_t& value)
{
    if (text.empty()) return false;
    value = 0;
    for (char c : text) {
        if (c < '0' || c > '9' || value > (UINT64_MAX - (c - '0')) / 10) return false;
        value = value * 10 + (c - '0');
    }
    return true;
}
inline RangeResult ParseRange(std::string_view header, std::uint64_t length, ByteRange& range)
{
    if (!length) return RangeResult::Unsatisfiable;
    range = {0, length - 1};
    if (header.empty()) return RangeResult::Full;
    if (header.substr(0, 6) != "bytes=" || header.find(',') != header.npos) return RangeResult::Invalid;
    const auto spec = header.substr(6);
    const auto dash = spec.find('-');
    if (dash == spec.npos) return RangeResult::Invalid;
    std::uint64_t first = 0, last = 0;
    if (!dash) {
        if (!Decimal(spec.substr(1), last)) return RangeResult::Invalid;
        if (!last) return RangeResult::Unsatisfiable;
        range.first = length - std::min(last, length);
    } else {
        if (!Decimal(spec.substr(0, dash), first)) return RangeResult::Invalid;
        if (first >= length) return RangeResult::Unsatisfiable;
        range.first = first;
        if (dash + 1 < spec.size()) {
            if (!Decimal(spec.substr(dash + 1), last)) return RangeResult::Invalid;
            if (last < first) return RangeResult::Unsatisfiable;
            range.last = std::min(last, length - 1);
        }
    }
    return RangeResult::Partial;
}
struct TimedBytes {
    double start = 0, end = 0;
    ByteRange bytes;
    bool sync = false;
};
struct MediaTrackIndex {
    std::vector<TimedBytes> samples;
    std::size_t Start(double time) const {
        if (samples.empty()) return 0;
        // Decode order is retained: B-frame presentation times need not be sorted.
        std::size_t result = 0;
        for (std::size_t i = 0; i < samples.size(); ++i) {
            if (samples[i].sync && samples[i].start <= time) result = i;
            if (samples[i].start > time + 5) break;
        }
        return result;
    }
    template<class Covered> double Forward(double time, Covered covered) const {
        if (samples.empty()) return -1;
        double end = time;
        for (auto i = Start(time); i < samples.size(); ++i) {
            const auto& sample = samples[i];
            if (sample.start > end + .25) break; // never bridge a timestamp gap with cached bytes
            if (!covered(sample.bytes)) break;
            end = std::max(end, sample.end);
        }
        return std::max(0.0, end - time);
    }
    std::vector<ByteRange> Window(double first, double last) const {
        std::vector<ByteRange> result;
        for (auto i = Start(first); i < samples.size(); ++i) {
            if (samples[i].start > last + 1) break;
            result.push_back(samples[i].bytes);
        }
        return result;
    }
};
inline double ForwardTarget(double rate, int seconds) {
    return std::clamp(seconds * std::clamp(rate, 1.0, 4.0), 1.0, 120.0);
}
}
