#pragma once
#include <d3d9.h>
#include <cmath>
// GFx's actual Quest draws use clip-space vertex shaders. Scale only their
// raster viewport/scissor, then restore the engine's cached coordinates.
struct HudRaster {
    IDirect3DDevice9* d;D3DVIEWPORT9 vp{};RECT clip{};bool changed=false;
    HudRaster(IDirect3DDevice9* device,bool capture,float scale):d(device){
        if(!capture||scale<=1)return;
        if(FAILED(d->GetViewport(&vp))||FAILED(d->GetScissorRect(&clip)))return;
        auto s=[&](unsigned x){return unsigned(std::lround(x*scale));};
        D3DVIEWPORT9 large{s(vp.X),s(vp.Y),s(vp.Width),s(vp.Height),vp.MinZ,vp.MaxZ};
        RECT sc{LONG(std::lround(clip.left*scale)),LONG(std::lround(clip.top*scale)),LONG(std::lround(clip.right*scale)),LONG(std::lround(clip.bottom*scale))};
        if(SUCCEEDED(d->SetViewport(&large))){d->SetScissorRect(&sc);changed=true;}
    }
    ~HudRaster(){if(changed){d->SetViewport(&vp);d->SetScissorRect(&clip);}}
};
