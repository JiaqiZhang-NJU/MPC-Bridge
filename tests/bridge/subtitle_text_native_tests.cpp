// SPDX-License-Identifier: GPL-3.0-or-later
// Headless tests against the player's actual STS parser. Optional arguments are
// read-only local subtitle paths; no movie/text content is written to logs.
#include "Subtitles/stdafx.h"
#include "apps/mplayerc/bridge/SubtitleTextSource.h"
#include "Subtitles/RenderedHdmvSubtitle.h"
#include "apps/mplayerc/bridge/DualSubtitleProvider.h"
#include "SubPic/MemSubPic.h"
#include <deque>
#include "SubPic/SubPicQueueImpl.h"
#include <iostream>

// Headless memory allocator with the same size negotiation as DX9/DX11.
class QualityAllocator : public CMemSubPicAllocator {
public:
    QualityAllocator() : CMemSubPicAllocator(CSize(1280,720)) {}
    STDMETHODIMP SetMaxTextureSize(SIZE size) override {
        m_maxsize=size; SetCurSize(size); SetCurVidRect(CRect(CPoint(0,0),size)); return S_OK;
    }
};

static bool PictureMultipleObjects() {
    for (const int scale : {1,2}) {
        std::vector<BYTE> packet;
        auto word = [](std::vector<BYTE>& data, int n) { data.push_back(BYTE(n>>8)); data.push_back(BYTE(n)); };
        auto segment = [&](BYTE type, const std::vector<BYTE>& data) {
            packet.push_back(type); word(packet, static_cast<int>(data.size())); packet.insert(packet.end(), data.begin(), data.end());
        };
        std::vector<BYTE> pcs; word(pcs,1920*scale); word(pcs,1080*scale);
        pcs.insert(pcs.end(),{0x10,0,1,0x80,0,0,2});
        for (int i=0;i<2;++i) {
            word(pcs,i); pcs.insert(pcs.end(),{0,0}); word(pcs,(600+i*100)*scale); word(pcs,(i ? 900 : 20)*scale);
        }
        segment(0x16,pcs); segment(0x14,{0,0,1,235,128,128,255});
        for (int i=0;i<2;++i) {
            std::vector<BYTE> rle;
            for(int y=0;y<20*scale;++y) { rle.insert(rle.end(),(i ? 120 : 240)*scale,1); rle.insert(rle.end(),{0,0}); }
            std::vector<BYTE> object; word(object,i); object.insert(object.end(),{0,0xc0});
            const auto length=rle.size()+4;
            object.push_back(BYTE(length>>16)); object.push_back(BYTE(length>>8)); object.push_back(BYTE(length));
            word(object,(i ? 120 : 240)*scale); word(object,20*scale);
            object.insert(object.end(),rle.begin(),rle.end()); segment(0x15,object);
        }
        segment(0x80,{});
        CCritSec lock;
        CComPtr<CRenderedHdmvSubtitle> source=new CRenderedHdmvSubtitle(&lock,ST_HDMV,L"English",0);
        source->ParseSample(packet.data(),static_cast<long>(packet.size()),0,50000000);
        std::vector<uint32_t> pixels(640*360,0xff000000);
        SubPicDesc surface{}; surface.w=640; surface.h=360; surface.bpp=32; surface.pitch=640*4;
        surface.bits=reinterpret_cast<BYTE*>(pixels.data()); surface.vidrect=CRect(0,0,640,360);
        RECT box{};
        if(source->Render(surface,10000000,25,box)!=S_OK || box.left!=200 || box.top!=7 || box.right!=280 || box.bottom!=307) { std::cerr << "Native picture check failed at line " << __LINE__ << std::endl; return false; }
        // Both distant objects must survive the union and downscale.
        if((pixels[10*640+210]>>24)==255 || (pixels[302*640+240]>>24)==255) { std::cerr << "Native picture check failed at line " << __LINE__ << std::endl; return false; }
        Bridge::DualSubtitleState settings;
        std::shared_ptr<CSimpleTextSubtitle> text[2];
        CComPtr<ISubStream> streams[2]; streams[0]=static_cast<ISubStream*>(source.p);
        CComPtr<CDualSubtitleProvider> combined=new CDualSubtitleProvider(&lock,settings,text,streams,STSStyle());
        SIZE maximum{}, virtualSize{}; POINT origin{};
        if (combined.p->GetTextureSize(nullptr, maximum, virtualSize, origin)!=S_OK
            || maximum.cx!=1920*scale || maximum.cy!=1080*scale
            || virtualSize.cx!=maximum.cx || virtualSize.cy!=maximum.cy || origin.x || origin.y) { std::cerr << "Native picture check failed at line " << __LINE__ << std::endl; return false; }
        // Exercise the actual queue, starting from the problematic 720p limit.
        CComPtr<ISubPicAllocator> allocator=new QualityAllocator;
        HRESULT queueResult=S_OK;
        CComPtr<ISubPicQueue> queue=new CSubPicQueueNoThread(false,allocator,&queueResult);
        queue->SetSubPicProvider(combined); queue->SetFPS(25); queue->SetTime(10000000);
        CComPtr<ISubPic> queued;
        if(!queue->LookupSubPic(10000000,queued) || !queued) return false;
        SubPicDesc actual{};
        if(FAILED(queued->GetDesc(actual)) || actual.w!=1920*scale || actual.h!=1080*scale) return false;
        if(combined.p->Render(surface,10000000,25,box)!=S_OK || !combined.p->overflow.exchange(false)) { std::cerr << "Native picture check failed at line " << __LINE__ << std::endl; return false; }
        if(combined.p->Render(surface,10000000,25,box)!=S_OK || combined.p->overflow.load()) { std::cerr << "Native picture check failed at line " << __LINE__ << std::endl; return false; }
    }
    return true;
}

static bool PictureDecodeAndSeek() {
    std::vector<BYTE> packet;
    auto segment = [&](BYTE type, const std::vector<BYTE>& body) {
        packet.push_back(type); packet.push_back(static_cast<BYTE>(body.size() >> 8));
        packet.push_back(static_cast<BYTE>(body.size())); packet.insert(packet.end(), body.begin(), body.end());
    };
    segment(0x16, {2,128,1,104,0x10,0,1,0x80,0,0,1,0,0,0,0,0,200,1,44});
    segment(0x14, {0,0,1,235,128,128,255,2,235,128,128,128});
    std::vector<BYTE> rle;
    for (int y=0;y<8;++y) { for(int x=0;x<60;++x) rle.push_back(x%2 ? 2 : 1); rle.push_back(0); rle.push_back(0); }
    const auto length=rle.size()+4;
    std::vector<BYTE> object={0,0,0,0xc0,0,static_cast<BYTE>(length>>8),static_cast<BYTE>(length),0,60,0,8};
    object.insert(object.end(),rle.begin(),rle.end()); segment(0x15,object); segment(0x80,{});
    std::vector<BYTE> clear={0x16,0,11,2,128,1,104,0x10,0,2,0x80,0,0,0,0x80,0,0};
    CCritSec lock;
    CComPtr<CRenderedHdmvSubtitle> source=new CRenderedHdmvSubtitle(&lock,ST_HDMV,L"English",0);
    std::vector<uint32_t> pixels(640*360,0xff000000);
    SubPicDesc surface{}; surface.w=640; surface.h=360; surface.bpp=32; surface.pitch=640*4;
    surface.bits=reinterpret_cast<BYTE*>(pixels.data()); surface.vidrect=CRect(0,0,640,360);
    for(int pass=0;pass<2;++pass) {
        source->NewSegment(0,240000000,1);
        source->ParseSample(packet.data(),static_cast<long>(packet.size()),20000000,40000000);
        source->ParseSample(packet.data(),static_cast<long>(packet.size()),40000000,90000000);
        source->ParseSample(clear.data(),static_cast<long>(clear.size()),90000000,90000001);
        const auto pos=source->GetStartPosition(30000000,25);
        if(!pos || source->GetStart(pos,25)!=20000000 || source->GetStop(pos,25)!=40000000) { std::cerr << "Native picture check failed at line " << __LINE__ << std::endl; return false; }
        RECT box{};
        if(source->Render(surface,30000000,25,box)!=S_OK || box.right-box.left!=60 || box.bottom-box.top!=8) { std::cerr << "Native picture check failed at line " << __LINE__ << std::endl; return false; }
        if(std::count_if(pixels.begin(),pixels.end(),[](uint32_t p){return (p>>24)<255;})!=480) { std::cerr << "Native picture check failed at line " << __LINE__ << std::endl; return false; }
        if(source->GetStartPosition(100000000,25)) { std::cerr << "Native picture check failed at line " << __LINE__ << std::endl; return false; }
    }
    Bridge::DualSubtitleState settings;
    std::shared_ptr<CSimpleTextSubtitle> text[2];
    CComPtr<ISubStream> streams[2]; streams[0]=static_cast<ISubStream*>(source.p);
    CComPtr<CDualSubtitleProvider> combined=new CDualSubtitleProvider(&lock,settings,text,streams,STSStyle());
    auto gap=combined.p->GetStartPosition(100000000,25);
    if(!gap || combined.p->GetStop(gap,25)!=ISubPicProvider::UNKNOWN_TIME) { std::cerr << "Native picture check failed at line " << __LINE__ << std::endl; return false; }
    // A new seek can make a previously empty interval live before the UI
    // rebuilds its provider. It must retain the one-frame queue contract.
    source->NewSegment(0,240000000,1);
    source->ParseSample(packet.data(),static_cast<long>(packet.size()),20000000,40000000);
    if(combined.p->GetStop(gap,25)!=ISubPicProvider::UNKNOWN_TIME) { std::cerr << "Native picture check failed at line " << __LINE__ << std::endl; return false; }
    SIZE maximum{}, virtualSize{}; POINT origin{};
    if (combined.p->GetTextureSize(gap,maximum,virtualSize,origin)!=S_OK || maximum.cx!=640 || maximum.cy!=360) { std::cerr << "Native picture check failed at line " << __LINE__ << std::endl; return false; }
    // A fitting 100% picture must be copied without changing even one pixel.
    RECT nativeBox{};
    std::fill(pixels.begin(),pixels.end(),0xff000000u);
    source->Render(surface,30000000,25,nativeBox);
    std::vector<uint32_t> reference;
    for(int y=nativeBox.top;y<nativeBox.bottom;++y)
        reference.insert(reference.end(),pixels.begin()+y*640+nativeBox.left,pixels.begin()+y*640+nativeBox.right);
    RECT dualBox{};
    if (combined.p->Render(surface,30000000,25,dualBox)!=S_OK || dualBox.right-dualBox.left!=60 || dualBox.bottom-dualBox.top!=8) { std::cerr << "Native picture check failed at line " << __LINE__ << std::endl; return false; }
    size_t at=0;
    for(int y=dualBox.top;y<dualBox.bottom;++y) for(int x=dualBox.left;x<dualBox.right;++x)
        if(pixels[y*640+x]!=reference[at++]) { std::cerr << "Native picture check failed at line " << __LINE__ << std::endl; return false; }
    for (int scale : {50,60,150,200}) {
        settings.slots[0].bitmapScale=scale;
        CComPtr<CDualSubtitleProvider> resized=new CDualSubtitleProvider(&lock,settings,text,streams,STSStyle());
        if(resized.p->Render(surface,30000000,25,dualBox)!=S_OK
            || dualBox.right-dualBox.left!=60*scale/100 || dualBox.bottom-dualBox.top!=8*scale/100) { std::cerr << "Native picture check failed at line " << __LINE__ << std::endl; return false; }
        if(scale==50) {
            // Alternating opaque/half-transparent pixels must average in all
            // four premultiplied/inverse-alpha channels, not select a neighbor.
            const auto actual=pixels[dualBox.top*640+dualBox.left];
            for(int channel=0;channel<4;++channel) {
                int expected=int(((reference[0]>>(8*channel))&255)+((reference[1]>>(8*channel))&255))/2;
                if(abs(int((actual>>(8*channel))&255)-expected)>2) { std::cerr << "Native picture check failed at line " << __LINE__ << std::endl; return false; }
            }
        }
    }
    return true;
}

int wmain(int argc, wchar_t** argv) {
    if (!AfxWinInit(GetModuleHandleW(nullptr), nullptr, GetCommandLineW(), 0)) return 2;
    if (!PictureDecodeAndSeek()) return 8;
    if (!PictureMultipleObjects()) return 9;
    std::cout << "PASS: actual subtitle queue replaces 1280x720 limit with HD/UHD canvas; 100% pixel identity and filtered inverse-alpha scaling\n";
    std::cout << "PASS: multi-object PGS bounds and downscale from HD/UHD canvases\n";
    std::cout << "PASS: native PGS decode, clear, reset/redecode and live-provider one-frame contract\n";
    CSimpleTextSubtitle original;
    original.m_subtitleType = Subtitle::ASS;
    original.CreateDefaultStyle(DEFAULT_CHARSET);
    original.Add(L"{\\pos(600,20)\\p1}m 0 0 l 50 50{\\p0}Hello\\N世界", 1000, 6000,
        L"Default", L"Speaker", L"Scroll up;0;100;10", CRect(1,2,3,4), 3, 12);
    original.Add(L"{\\p1}m 0 0 l 10 10", 1500, 2000);
    original.Add(L"Next", 4000, 8000);
    const auto raw = original.GetAt(0).str;
    auto reading = Bridge::MakeSubtitleReadingCopy(original);
    if (reading->GetCount() != 2 || reading->GetStrW(0, false) != L"Hello\n世界"
        || reading->GetAt(0).start != 1000 || reading->GetAt(0).end != 6000
        || reading->GetAt(0).layer != 3 || reading->GetAt(0).actor != L"Speaker"
        || reading->GetAt(0).readorder != 12 || !reading->GetAt(0).effect.IsEmpty()
        || original.GetAt(0).str != raw || original.m_subtitleType != Subtitle::ASS) return 3;
    CSimpleTextSubtitle literal;
    literal.m_subtitleType = Subtitle::SRT;
    literal.CreateDefaultStyle(DEFAULT_CHARSET);
    literal.Add(L"Set {a,b}", 0, 1000);
    if (Bridge::MakeSubtitleReadingCopy(literal)->GetAt(0).str != L"Set {a,b}") return 7;
    for (int i = 1; i < argc; ++i) {
        CSimpleTextSubtitle parsed;
        if (!parsed.Open(argv[i], CP_UTF8, true, L"", L"") || parsed.IsEmpty()
            || !Bridge::SupportedSubtitleTextType(parsed.m_subtitleType)) return 4;
        const auto count = parsed.GetCount();
        auto copy = Bridge::MakeSubtitleReadingCopy(parsed);
        if (copy->IsEmpty() || parsed.GetCount() != count) return 5;
        size_t newlines = 0;
        for (size_t n = 0; n < copy->GetCount(); ++n) {
            const auto& cue = copy->GetAt(n);
            if (cue.end < cue.start || cue.str.Find(L"{\\") >= 0) return 6;
            newlines += cue.str.Find(L"\\N") >= 0;
        }
        std::wcout << L"PASS file " << i << L": format=" << Bridge::SubtitleTextFormatName(parsed.m_subtitleType)
            << L", original=" << count << L", reading=" << copy->GetCount() << L", multiline=" << newlines << L"\n";
    }
    std::cout << "PASS: native STS reading copy, timings, actor/layer/order, drawing exclusion, original preservation\n";
    return 0;
}
