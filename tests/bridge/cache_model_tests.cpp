// SPDX-License-Identifier: GPL-3.0-or-later
#include "apps/mplayerc/bridge/MediaCacheModel.h"
#include "apps/mplayerc/bridge/QualityModel.h"
#include <cstdlib>
#include <iostream>
int checks = 0;
void Check(bool condition) { ++checks; if (!condition) { std::cerr << "FAIL " << checks << '\n'; std::exit(1); } }
int main() {
    using namespace Bridge;
    ByteRange range;
    Check(ParseRange("", 100, range) == RangeResult::Full && range.first == 0 && range.last == 99);
    Check(ParseRange("bytes=10-20", 100, range) == RangeResult::Partial && range.first == 10 && range.last == 20);
    Check(ParseRange("bytes=10-", 100, range) == RangeResult::Partial && range.last == 99);
    Check(ParseRange("bytes=-10", 100, range) == RangeResult::Partial && range.first == 90);
    Check(ParseRange("bytes=-1000", 100, range) == RangeResult::Partial && range.first == 0);
    Check(ParseRange("bytes=0-1000", 100, range) == RangeResult::Partial && range.last == 99);
    for (auto invalid : {"bytes=0-1,2-3", "bytes=x-2", "bytes=0-x", "bytes=--", "items=0-1", "bytes=18446744073709551616-"})
        Check(ParseRange(invalid, 100, range) == RangeResult::Invalid);
    for (auto impossible : {"bytes=100-", "bytes=-0", "bytes=10-2"}) Check(ParseRange(impossible, 100, range) == RangeResult::Unsatisfiable);
    Check(ParseRange("", 0, range) == RangeResult::Unsatisfiable);
    Check(ForwardTarget(1, 30) == 30 && ForwardTarget(3, 30) == 90 && ForwardTarget(4, 90) == 120);
    MediaTrackIndex track;
    track.samples = {{0, 1, {0, 9}, true}, {1, 2, {10, 19}, false}, {2, 3, {20, 29}, true}, {3, 4, {30, 39}, false}};
    Check(track.Forward(.5, [](ByteRange r) { return r.last < 20; }) == 1.5);
    Check(track.Forward(1.5, [](ByteRange r) { return r.first > 0; }) == 0); // missing dependency, despite later bytes
    Check(track.Forward(2.5, [](ByteRange r) { return r.first >= 20; }) == 1.5);
    Check(track.Window(1.5, 1.5).front().first == 0); // pre-roll includes earlier keyframe
    Check(track.Start(3) == 2);
    Check(MediaTrackIndex{}.Forward(0, [](ByteRange) { return true; }) == -1);
    MediaTrackIndex gap;
    gap.samples = {{0, 1, {0, 9}, true}, {2, 3, {10, 19}, true}};
    Check(gap.Forward(0, [](ByteRange) { return true; }) == 1); // a timestamp gap is not continuous coverage
    const std::vector<QualityFormat> formats = {{0, 720, 0, 30, false, 1000}, {1, 1080, 2, 30, false, 2000}, {2, 1080, 2, 60, true, 3000}};
    Check(SelectHeight(formats, 1080, 0, false, false, true) == 1); // preferred codec missing: stay at requested height
    Check(SelectHeight(formats, 1080, 2, true, true, true) == 2);
    Check(SelectHeight(formats, 480, 0, false, false, false) == UINT32_MAX);
    Check(SelectHeight({}, 1080, 0, false, false, false) == UINT32_MAX);
    std::cout << "PASS " << checks << " cache/quality model checks\n";
}
