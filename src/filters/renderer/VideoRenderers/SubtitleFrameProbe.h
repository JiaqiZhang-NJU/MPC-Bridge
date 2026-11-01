// SPDX-License-Identifier: GPL-3.0-or-later
// Opt-in test instrumentation. Never included in ordinary player builds.
#pragma once
#include <d3d11.h>
#include <d3d9.h>
#include "SubPic/DX11SubPic.h"
#include "SubPic/DX9SubPic.h"
#include "SubPic/SubPicProviderImpl.h"

inline void ProbeSubtitleFrame(ISubPicQueue* queue, ISubPic* picture, REFERENCE_TIME time, HRESULT blend, const CRect& frameRect) {
    char path[MAX_PATH];
    if (!GetEnvironmentVariableA("MPC_SUBTITLE_FRAME_PROBE", path, MAX_PATH)) return;
    CComPtr<ISubPicProvider> source;
    queue->GetSubPicProvider(&source);
    const auto native = dynamic_cast<CSubPicProviderImpl*>(source.p);
    int counts[2] = {-1,-1};
    SubPicDesc desc{};
    if (picture) picture->GetDesc(desc);
    auto count = [&](const BYTE* data, int pitch, int width, int height, uint32_t mask) {
        const CRect frame=frameRect & CRect(0,0,width,height);
        for(int lane=0;lane<2;++lane) {
            counts[lane]=0;
            const int top=frame.top+frame.Height()*(lane ? 81 : 64)/100;
            const int bottom=frame.top+frame.Height()*(lane ? 100 : 81)/100;
            for(int y=top;y<bottom;y+=2) {
                const auto row=reinterpret_cast<const uint32_t*>(data+size_t(y)*pitch);
                for(int x=frame.left;x<frame.right;x+=2) counts[lane]+=(row[x]&mask)!=0;
            }
        }
    };
    if (picture && dynamic_cast<CDX11SubPic*>(picture)) {
        const auto dx11=dynamic_cast<CDX11SubPic*>(picture);
        CComPtr<ID3D11Device> device=dx11->m_pAllocator->GetDevice();
        CComPtr<ID3D11DeviceContext> context;device->GetImmediateContext(&context);
        CComPtr<ID3D11RenderTargetView> target;context->OMGetRenderTargets(1,&target,nullptr);
        CComPtr<ID3D11Resource> resource;if(target)target->GetResource(&resource);
        CComQIPtr<ID3D11Texture2D> texture=resource;
        if(texture) {
            D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);
            const auto format=d.Format;
            if(d.SampleDesc.Count==1 && (format==DXGI_FORMAT_B8G8R8A8_UNORM || format==DXGI_FORMAT_R8G8B8A8_UNORM || format==DXGI_FORMAT_R10G10B10A2_UNORM)) {
                d.BindFlags=0;d.MiscFlags=0;d.Usage=D3D11_USAGE_STAGING;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
                CComPtr<ID3D11Texture2D> readable;
                if(SUCCEEDED(device->CreateTexture2D(&d,nullptr,&readable))) {
                    context->CopyResource(readable,texture);D3D11_MAPPED_SUBRESOURCE mapped{};
                    if(SUCCEEDED(context->Map(readable,0,D3D11_MAP_READ,0,&mapped))) {
                        count(static_cast<const BYTE*>(mapped.pData),mapped.RowPitch,d.Width,d.Height,format==DXGI_FORMAT_R10G10B10A2_UNORM ? 0x3fffffff : 0xffffff);
                        context->Unmap(readable,0);
                    }
                }
            }
        }
    } else if (picture && dynamic_cast<CDX9SubPic*>(picture)) {
        const auto texture=static_cast<IDirect3DTexture9*>(picture->GetObject());
        CComPtr<IDirect3DDevice9> device;texture->GetDevice(&device);
        CComPtr<IDirect3DSurface9> target;device->GetRenderTarget(0,&target);
        D3DSURFACE_DESC d{};if(target)target->GetDesc(&d);
        CComPtr<IDirect3DSurface9> readable;
        if(target && SUCCEEDED(device->CreateOffscreenPlainSurface(d.Width,d.Height,d.Format,D3DPOOL_SYSTEMMEM,&readable,nullptr))
            && SUCCEEDED(device->GetRenderTargetData(target,readable))) {
            D3DLOCKED_RECT locked{};
            if(SUCCEEDED(readable->LockRect(&locked,nullptr,D3DLOCK_READONLY))) {
                count(static_cast<const BYTE*>(locked.pBits),locked.Pitch,d.Width,d.Height,0xffffff);
                readable->UnlockRect();
            }
        }
    }
    FILE* log=nullptr;
    if(!fopen_s(&log,path,"a") && log) {
        fprintf(log,"FRAME %lu %lld %lld %lld %d %d %lx %d\n",GetCurrentProcessId(),time,
            picture ? picture->GetStart() : -1,picture ? picture->GetStop() : -1,
            counts[0],counts[1],blend,native && native->RenderOnQueueMiss());
        fclose(log);
    }
}
