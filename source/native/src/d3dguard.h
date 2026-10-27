// Блоки состояния D3D9 не сохраняют цель рисования и буфер глубины: всё, что мод рисует сам,
// обязано вернуть игре её цель, буфер глубины и область вывода, иначе игра продолжит рисовать
// сцену в чужой буфер глубины (или вовсе без него).
#pragma once
#include <d3d9.h>

struct TargetsGuard {
    IDirect3DDevice9* d;
    IDirect3DSurface9* rt = nullptr;
    IDirect3DSurface9* ds = nullptr;
    IDirect3DSurface9* extra[3] = {};
    unsigned count = 0;
    D3DVIEWPORT9 vp = {};
    explicit TargetsGuard(IDirect3DDevice9* device) : d(device) {
        d->GetRenderTarget(0, &rt);
        d->GetDepthStencilSurface(&ds);
        d->GetViewport(&vp);
        D3DCAPS9 caps = {};
        if (SUCCEEDED(d->GetDeviceCaps(&caps)))
            count = caps.NumSimultaneousRTs > 4 ? 3 : caps.NumSimultaneousRTs > 0 ? caps.NumSimultaneousRTs - 1 : 0;
        for (unsigned i = 0; i < count; ++i) d->GetRenderTarget(i + 1, &extra[i]);
    }
    ~TargetsGuard() {
        // Unbind targets of a different size before restoring the game's set.
        d->SetDepthStencilSurface(nullptr);
        for (unsigned i = 0; i < count; ++i) d->SetRenderTarget(i + 1, nullptr);
        if (rt) d->SetRenderTarget(0, rt);
        for (unsigned i = 0; i < count; ++i) {
            d->SetRenderTarget(i + 1, extra[i]);
            if (extra[i]) extra[i]->Release();
        }
        d->SetDepthStencilSurface(ds);
        d->SetViewport(&vp);
        if (rt) rt->Release();
        if (ds) ds->Release();
    }
};
