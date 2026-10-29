// SPDX-License-Identifier: GPL-3.0-or-later
#include "apps/mplayerc/bridge/DualSubtitleModel.h"
#include "apps/mplayerc/bridge/SubtitleCatalog.h"
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
    assert(SubtitleFileFormat(L"C:/movies/subtitles.EN.ASS")==SubtitleTextFormat::Ass);
    assert(SubtitleFileFormat(L"字幕.ssa")==SubtitleTextFormat::Ssa);
    assert(SubtitleFileFormat(L"captions.vtt")==SubtitleTextFormat::Vtt);
    assert(SubtitleFileFormat(L"captions.srt")==SubtitleTextFormat::Srt);
    assert(SubtitleFileFormat(L"captions.srt.exe")==SubtitleTextFormat::Unsupported);
    assert(SubtitleFileFormat(L"C:/folder.ass/no-extension")==SubtitleTextFormat::Unsupported);
    assert(SubtitleFileFormat(L"")==SubtitleTextFormat::Unsupported);
    assert(SubtitleReadingText(L"{\\pos(300,50)\\c&HFF&}Hello\\N世界")==L"Hello\n世界");
    assert(SubtitleReadingText(L"{\\p1}m 0 0 l 80 80{\\p0}Dialogue")==L"Dialogue");
    assert(SubtitleReadingText(L"Text{\\p2}m 0 0 l 80 80")==L"Text");
    assert(SubtitleReadingText(L"{\\p2}m 0 0{\\rDefault}Reset")==L"Reset");
    assert(SubtitleReadingText(L"{\\p2}m 0 0{\\p-1}Reset")==L"Reset");
    assert(SubtitleReadingText(L"{\\pbo10\\pos(1,2)}Text")==L"Text");
    assert(SubtitleReadingText(L"{\\t(0,100,\\p1)}Text")==L"Text");
    assert(SubtitleReadingText(L"{\\p0\\t(0,100,\\p1)}Text")==L"Text");
    assert(SubtitleReadingText(L"{\\p1}m 0 0{\\p0000}Text")==L"Text");
    assert(SubtitleReadingText(L"{\\p100000000000000000000000}m 0 0").empty());
    assert(SubtitleReadingText(L"{\\i1}Hi{\\i0}\\hthere\\nagain")==L"Hi there\nagain");
    assert(SubtitleReadingText(L"<i>Hi</i>")==L"<i>Hi</i>");
    assert(SubtitleReadingText(L"broken {tag")==L"broken {tag");
    assert(SubtitleReadingText(L"{comment}Text")==L"Text");
    assert(SubtitleReadingText(L"Set {a,b}",false)==L"Set {a,b}");
    assert(SubtitleReadingText(L"{\\pos(10,20)}Text",false)==L"Text");
    assert(SubtitleReadingText(L"").empty());
    auto bitmap = FitSubtitleBitmap(400, 100, 640, 54, 100);
    assert(bitmap.width == 216 && bitmap.height == 54);
    bitmap = FitSubtitleBitmap(400, 100, 640, 54, 50);
    assert(bitmap.width == 200 && bitmap.height == 50);
    bitmap = FitSubtitleBitmap(4000, 1000, 640, 54, 200);
    assert(bitmap.width <= 640 && bitmap.height == 54);
    assert(FitSubtitleBitmap(0, 100, 640, 54, 100).width == 0);
    assert(FitSubtitleBitmap(100, 100, 640, 0, 100).height == 0);
    state.slots[0].fontSize = 20; state.slots[0].bitmapScale = 24;
    assert(!ValidSubtitleSlot(state.slots[0]));
    state.slots[0].bitmapScale = 200; assert(ValidSubtitleSlot(state.slots[0]));
    std::cout<<"PASS: subtitle offsets, overflow, empty intervals, exchange, validation and disjoint layouts\n";
}
