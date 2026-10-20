// SPDX-License-Identifier: GPL-3.0-or-later
#include "apps/mplayerc/bridge/DualSubtitleModel.h"
#include <cassert>
#include <iostream>
#include <utility>
int main() {
    using namespace Bridge;
    int a, b;
    assert(ShiftSubtitleInterval(2000,3000,500,a,b) && a==2500 && b==3500);
    assert(ShiftSubtitleInterval(0,3000,-500,a,b) && a==0 && b==2500);
    assert(!ShiftSubtitleInterval(100,200,-500,a,b));
    assert(!ShiftSubtitleInterval(INT32_MAX-10,INT32_MAX,100,a,b));
    assert(!ShiftSubtitleInterval(100,100,0,a,b));
    assert(!ShiftSubtitleInterval(200,100,0,a,b));
    DualSubtitleState state; assert(!state.enabled && state.slots[0].visible && state.slots[1].visible);
    state.slots[0].path=L"english.srt"; state.slots[0].delayMs=500;
    state.slots[1].path=L"chinese.srt"; state.slots[1].delayMs=-300;
    std::swap(state.slots[0],state.slots[1]);
    assert(state.slots[0].path==L"chinese.srt" && state.slots[0].delayMs==-300);
    assert(state.slots[1].path==L"english.srt" && state.slots[1].delayMs==500);
    for(int h:{180,360,720,1080,2160}) for(int layout:{0,1}) {
        auto x=SubtitleLane(h,layout,0),y=SubtitleLane(h,layout,1);
        assert(x.top>=0 && x.bottom<=h && x.top<x.bottom);
        assert(y.top>=0 && y.bottom<=h && y.top<y.bottom);
        assert(x.bottom<=y.top || y.bottom<=x.top);
    }
    assert(ValidSubtitleSlot(state.slots[0]));
    state.slots[0].delayMs=600001;assert(!ValidSubtitleSlot(state.slots[0]));
    state.slots[0].delayMs=0;state.slots[0].fontSize=49;assert(!ValidSubtitleSlot(state.slots[0]));
    SubtitleSlot track; track.embeddedTrack=1; track.embeddedKey=L"srt|Chinese";
    assert(ResolveEmbeddedTrack(track,{L"srt|English",L"srt|Chinese"})==1);
    assert(ResolveEmbeddedTrack(track,{L"srt|Chinese",L"srt|English"})==0);
    assert(ResolveEmbeddedTrack(track,{L"srt|English"})==-1);
    assert(ResolveEmbeddedTrack(track,{L"srt|Chinese",L"srt|English",L"srt|Chinese"})==-1);
    state.slots[0]=track;state.slots[1]={};state.slots[1].path=L"translation.srt";
    std::swap(state.slots[0],state.slots[1]);
    assert(state.slots[1].embeddedTrack==1 && state.slots[1].embeddedKey==L"srt|Chinese");
    assert(state.slots[0].embeddedTrack==-1 && state.slots[0].path==L"translation.srt");
    std::cout<<"PASS: subtitle offsets, overflow, empty intervals, exchange, validation and disjoint layouts\n";
}
