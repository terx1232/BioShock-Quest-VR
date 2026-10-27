// Стерео по буферу глубины: оба глаза из одного кадра игры, каждый кадр.
//
// Окно игры W x H (2560x1440). В игровом режиме движок рисует картинку gameWidth x gameHeight
// (1280x1280) в левый верхний угол (camera::SetEngineSize); каждая половина кадра - глаз,
// WinlatorXR показывает её с квадратным полем зрения. Автоматический буфер глубины игры заменён текстурой INTZ,
// которую можно читать в шейдере. В Present картинка копируется в текстуру, и шейдер
// depthstereo.hlsl строит левый глаз в левую половину кадра и правый - в правую: каждая точка
// сдвигается по горизонтали на величину, обратную её расстоянию (как у глаз, разнесённых на
// межзрачковое расстояние). WinlatorXR показывает кадр как стереопару бок о бок (MODE_3D = 1).
// Оба глаза обновляются в каждом кадре игры, без поочерёдной отрисовки.
#include "stereo.h"

#include <windows.h>
#include <d3d9.h>

#include "d3dguard.h"
#include <math.h>

#include "config.h"
#include "log.h"
#include "theater.h"
#include "pcvr.h"
#include "vrframe.h"

namespace stereo {
namespace {

#include "stereoshaders.inc"

const uintptr_t kNearPlane = 0x11147ef0, kFarPlane = 0x11147ef4;  // плоскости отсечения движка (единицы Unreal)
const D3DFORMAT kINTZ = (D3DFORMAT)MAKEFOURCC('I', 'N', 'T', 'Z');

bool g_replaced = false;            // автоматический буфер глубины заменён нашим
bool g_active = false;
IDirect3DSurface9* g_originalDepth = nullptr;
bool g_pendingInit = false, g_initializing = false;
D3DPRESENT_PARAMETERS g_savedParams{};
D3DFORMAT g_autoFormat = D3DFMT_D24S8;
UINT g_w = 0, g_h = 0;     // кадр (окно)
UINT g_gw = 0, g_gh = 0;   // картинка движка в левом верхнем углу кадра
IDirect3DTexture9* g_depthTex = nullptr;
IDirect3DSurface9* g_depthSurf = nullptr;
IDirect3DTexture9* g_colorTex = nullptr;
IDirect3DSurface9* g_colorSurf = nullptr;
IDirect3DTexture9* g_outputTex = nullptr;
IDirect3DSurface9* g_outputSurf = nullptr;
IDirect3DTexture9* g_worldDepthTex=nullptr;
IDirect3DSurface9* g_worldDepthSurf=nullptr;
bool g_worldDepthValid=false;
IDirect3DVertexShader9* g_vs = nullptr;
IDirect3DPixelShader9* g_ps = nullptr;
IDirect3DPixelShader9* g_psDebug = nullptr;
IDirect3DPixelShader9* g_psCopy = nullptr;
IDirect3DPixelShader9* g_psCache=nullptr;
bool g_cacheSupported=false,g_cacheValid=false,g_cacheLogged=false;
float g_cachedTuning[4]={};
IDirect3DPixelShader9* g_psMerge = nullptr;
IDirect3DPixelShader9* g_psAim=nullptr;
bool g_aimUnavailable=false;
IDirect3DTexture9* g_aimDepth=nullptr; // borrowed, valid only until EndFrame
float g_aimShift[4]={},g_aimMap[4]={};
bool g_aimCached=false;
UINT g_aimOutputW=0,g_aimOutputH=0;
// BioShock очищает глубину перед руками от первого лица: глубину сцены копируем до этого
IDirect3DTexture9* g_depthCopyTex = nullptr;
IDirect3DSurface9* g_depthCopySurf = nullptr;
IDirect3DTexture9* g_depthMergedTex = nullptr;
IDirect3DSurface9* g_depthMergedSurf = nullptr;
int g_zClears = 0;          // очисток нашей глубины в этом кадре
bool g_copied = false;      // глубина сцены скопирована в этом кадре
bool g_clearsLogged = false;
IDirect3DVertexDeclaration9* g_decl = nullptr;
IDirect3DStateBlock9* g_state = nullptr;
bool g_ok = false;
bool g_logged = false;
bool g_split = false;               // моно: окно больше картинки движка, картинка растягивается на окно
IDirect3DTexture9* g_upTex = nullptr;
IDirect3DSurface9* g_upSurf = nullptr;

// Render targets are not captured by a D3D9 state block. In particular, do not
// leave the game's additional render targets bound to a smaller R32F pass.
struct PassGuard {
    IDirect3DDevice9* d;
    TargetsGuard targets;
    explicit PassGuard(IDirect3DDevice9* device) : d(device), targets(device) {
        g_state->Capture();
        for (unsigned i = 0; i < targets.count; i++) d->SetRenderTarget(i + 1, nullptr);
        d->SetDepthStencilSurface(nullptr);
    }
    ~PassGuard() {
        g_state->Apply();
    }
};

void SetupPass(IDirect3DDevice9* d) {
    d->SetVertexDeclaration(g_decl);
    d->SetVertexShader(g_vs);
    for (int s = 0; s < 3; s++) {
        d->SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        d->SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        d->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        d->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        d->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        d->SetSamplerState(s, D3DSAMP_SRGBTEXTURE, FALSE);
    }
    d->SetRenderState(D3DRS_ZENABLE, FALSE);
    d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    d->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    d->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    d->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    d->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    d->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
}

HRESULT DrawQuad(IDirect3DDevice9* d, UINT w, UINT h, float u = 1, float v = 1) {
    struct V { float x, y, z, w, u, v; };
    float hx = 1.0f / w, hy = 1.0f / h;
    V q[4] = {{-1 - hx, 1 + hy, 0, 1, 0, 0}, {1 - hx, 1 + hy, 0, 1, u, 0},
              {-1 - hx, -1 + hy, 0, 1, 0, v}, {1 - hx, -1 + hy, 0, 1, u, v}};
    return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(V));
}

void InitUpscale(IDirect3DDevice9* d, const D3DPRESENT_PARAMETERS* pp) {
    if (g_config.enabled && (g_gw < g_w || g_gh < g_h) &&
        SUCCEEDED(d->CreateTexture(g_gw, g_gh, 1, D3DUSAGE_RENDERTARGET, pp->BackBufferFormat, D3DPOOL_DEFAULT,
                                   &g_upTex, nullptr)) &&
        SUCCEEDED(g_upTex->GetSurfaceLevel(0, &g_upSurf))) {
        g_split = true;
        Log("Stereo: mono, window %ux%u, game picture %ux%u stretched over the window", g_w, g_h, g_gw, g_gh);
    }
}

float ReadFloat(uintptr_t a, float fallback) {
    float v = fallback;
    if (!IsBadReadPtr((void*)a, 4)) v = *(float*)a;
    return v > 0 && v < 1e9f ? v : fallback;
}
struct ShiftInfo {float nearPlane,farPlane,k,tuning[4];};
ShiftInfo Shift(float fov,float ipd){
    ShiftInfo p{};p.nearPlane=ReadFloat(kNearPlane,10.0f);p.farPlane=ReadFloat(kFarPlane,65536.0f);
    if(p.farPlane<=p.nearPlane)p.farPlane=p.nearPlane+65536.0f;
    p.k=(ipd*.5f)/(2.f*tanf(fov*3.14159265f/360.f));
    float a=1.f/p.nearPlane,b=(p.farPlane-p.nearPlane)/(p.nearPlane*p.farPlane);
    p.tuning[0]=p.k*a*g_config.depthStrength;p.tuning[1]=-p.k*b*g_config.depthStrength;
    p.tuning[2]=p.tuning[0]<g_config.depthMaxShift?p.tuning[0]:g_config.depthMaxShift;
    p.tuning[3]=g_config.depthNearSoftness;return p;
}
bool CreateDepthCopy(IDirect3DDevice9* d){
#ifndef QUEST_SHIFT_CACHE_UNAVAILABLE_TEST
    if(SUCCEEDED(d->CreateTexture(g_gw,g_gh,1,D3DUSAGE_RENDERTARGET,D3DFMT_G32R32F,D3DPOOL_DEFAULT,&g_depthCopyTex,nullptr))){
        g_cacheSupported=SUCCEEDED(d->CreatePixelShader(kDepthcachePS,&g_psCache));
    }
#endif
    if(!g_depthCopyTex&&FAILED(d->CreateTexture(g_gw,g_gh,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&g_depthCopyTex,nullptr)))return false;
    return SUCCEEDED(g_depthCopyTex->GetSurfaceLevel(0,&g_depthCopySurf));
}

}  // namespace

bool Enabled() { return g_config.enabled && g_config.depthStereo; }
bool LazyDepth() { return pcvr::Enabled() || g_config.lazyDepth; }

void AdjustParams(D3DPRESENT_PARAMETERS* pp) {
    if (!Enabled() || !pp || !pp->EnableAutoDepthStencil) return;
    g_autoFormat = pp->AutoDepthStencilFormat;
    if (!LazyDepth()) pp->EnableAutoDepthStencil = FALSE;
    g_replaced = true;
}

void AfterCreate(IDirect3DDevice9* d, const D3DPRESENT_PARAMETERS* pp) {
    if (!d || !pp) return;
    g_w = pp->BackBufferWidth;
    g_h = pp->BackBufferHeight;
    g_gw = g_config.gameWidth && g_config.gameWidth < g_w ? g_config.gameWidth : g_w;
    g_gh = g_config.gameHeight && g_config.gameHeight < g_h ? g_config.gameHeight : g_h;
    g_ok = false;
    g_split = false;
    if (LazyDepth() && !g_initializing) {
        d->GetDepthStencilSurface(&g_originalDepth);
        g_savedParams = *pp;
        g_pendingInit = Enabled();
        InitUpscale(d, pp);
        Log("Stereo: menu depth preserved; depth resources deferred until gameplay");
        return;
    }
    if (!g_replaced) {
        // Моно: меню рисуется во всё окно (2K), а в игре движок рисует картинку gameWidth x
        // gameHeight в угол кадра; она растягивается на окно, WinlatorXR показывает окно с
        // квадратным полем зрения - пропорции возвращаются
        InitUpscale(d, pp);
        return;
    }
    if (SUCCEEDED(d->CreateTexture(g_w, g_h, 1, D3DUSAGE_DEPTHSTENCIL, kINTZ, D3DPOOL_DEFAULT, &g_depthTex, nullptr)) &&
        SUCCEEDED(g_depthTex->GetSurfaceLevel(0, &g_depthSurf))) {
        g_ok = true;
    } else {
        // без INTZ стерео не построить: обычный буфер глубины, кадр как в моно
        Log("Stereo: INTZ depth texture not supported, falling back to a plain depth buffer");
        d->CreateDepthStencilSurface(g_w, g_h, g_autoFormat, D3DMULTISAMPLE_NONE, 0, TRUE, &g_depthSurf, nullptr);
    }
    if (g_depthSurf && !LazyDepth()) d->SetDepthStencilSurface(g_depthSurf);
    if (g_ok) {
        g_ok = SUCCEEDED(d->CreateTexture(g_gw, g_gh, 1, D3DUSAGE_RENDERTARGET, pp->BackBufferFormat, D3DPOOL_DEFAULT,
                                          &g_colorTex, nullptr)) &&
               SUCCEEDED(g_colorTex->GetSurfaceLevel(0, &g_colorSurf)) &&
               SUCCEEDED(d->CreateVertexShader((const DWORD*)kFullscreenVs, &g_vs)) &&
               SUCCEEDED(d->CreatePixelShader((const DWORD*)kDepthstereoPS, &g_ps)) &&
               SUCCEEDED(d->CreatePixelShader((const DWORD*)kDepthviewPS, &g_psDebug)) &&
               SUCCEEDED(d->CreatePixelShader((const DWORD*)kDepthcopyPS, &g_psCopy)) &&
               SUCCEEDED(d->CreatePixelShader((const DWORD*)kDepthmergePS, &g_psMerge)) &&
               CreateDepthCopy(d) &&
               SUCCEEDED(d->CreateTexture(g_gw, g_gh, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT,
                                          &g_depthMergedTex, nullptr)) &&
               SUCCEEDED(g_depthMergedTex->GetSurfaceLevel(0, &g_depthMergedSurf)) &&
               SUCCEEDED(d->CreateStateBlock(D3DSBT_ALL, &g_state));
        const D3DVERTEXELEMENT9 elems[] = {
            {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
            {0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
            D3DDECL_END()};
        if (g_ok) g_ok = SUCCEEDED(d->CreateVertexDeclaration(elems, &g_decl));
        // Keep costly depth reconstruction at gameplay resolution even when
        // the window is larger for a sharp VR cinema. Upscale the completed
        // pair with a cheap copy instead of ray marching every menu pixel.
        if (g_ok && g_gw * 2 <= g_w && g_gh <= g_h && (g_gw * 2 < g_w || g_gh < g_h)) {
            g_ok = SUCCEEDED(d->CreateTexture(g_gw * 2, g_gh, 1, D3DUSAGE_RENDERTARGET,
                pp->BackBufferFormat, D3DPOOL_DEFAULT, &g_outputTex, nullptr)) &&
                SUCCEEDED(g_outputTex->GetSurfaceLevel(0, &g_outputSurf));
        }
    }
    Log("Stereo: depth stereo %s (frame %ux%u, game picture %ux%u, eye %ux%u)", g_ok ? "ready" : "NOT available", g_w, g_h,
        g_gw, g_gh, g_w / 2, g_h);
    if(g_ok&&g_config.weaponGeometry&&!g_outputSurf){
        D3DCAPS9 caps{};d->GetDeviceCaps(&caps);
        if(caps.NumSimultaneousRTs>=2&&SUCCEEDED(d->CreateTexture(g_w,g_h,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&g_worldDepthTex,nullptr)))
            g_worldDepthTex->GetSurfaceLevel(0,&g_worldDepthSurf);
        Log("Stereo: per-eye world occlusion depth %s",g_worldDepthSurf?"ready":"unavailable");
    }
    if (!g_ok) InitUpscale(d, pp);
}

bool Ready() { return g_ok && (!LazyDepth() || g_active); }

IDirect3DSurface9* MapDepth(IDirect3DSurface9* surface) {
    if (!LazyDepth() || !g_active || !g_ok || !surface || theater::g_modDraw) return surface;
    if (surface == g_originalDepth) return g_depthSurf;
    D3DSURFACE_DESC desc{};
    if (SUCCEEDED(surface->GetDesc(&desc)) && desc.Width == g_w && desc.Height == g_h && desc.Format == g_autoFormat)
        return g_depthSurf;
    return surface;
}

void SetActive(IDirect3DDevice9* d, bool active) {
    if (!LazyDepth() || !d) return;
    if (active && g_pendingInit) {
        g_pendingInit = false;
        if (g_upSurf) g_upSurf->Release();
        if (g_upTex) g_upTex->Release();
        g_upSurf = nullptr; g_upTex = nullptr;
        g_initializing = true;
        AfterCreate(d, &g_savedParams);
        g_initializing = false;
    }
    active = active && g_ok;
    if (active == g_active) return;
    g_active = active;
    IDirect3DSurface9* current = nullptr;
    d->GetDepthStencilSurface(&current);
    if (active && current) d->SetDepthStencilSurface(MapDepth(current));
    else if (!active && current == g_depthSurf && g_originalDepth) d->SetDepthStencilSurface(g_originalDepth);
    if (current) current->Release();
    EndFrame();
    Log("Stereo: depth capture %s", active ? "enabled for gameplay" : "disabled for menu");
}

// Глубина сцены -> текстура R32F (полноэкранный проход, пока буфер глубины не очищен)
bool CopyDepth(IDirect3DDevice9* d) {
    PassGuard guard(d);
    if (FAILED(d->SetRenderTarget(0, g_depthCopySurf))) return false;
    UINT cw = guard.targets.vp.Width < g_gw ? guard.targets.vp.Width : g_gw;
    UINT ch = guard.targets.vp.Height < g_gh ? guard.targets.vp.Height : g_gh;
    if (!cw || !ch) return false;
    D3DVIEWPORT9 full = {0, 0, cw, ch, 0, 1};
    d->SetViewport(&full);
    SetupPass(d);
    d->SetPixelShader(g_cacheSupported?g_psCache:g_psCopy);
    d->SetTexture(1, g_depthTex);
    const float map[4] = {(float)cw / g_w, (float)ch / g_h,
                          (float)guard.targets.vp.X / g_w, (float)guard.targets.vp.Y / g_h};
    d->SetPixelShaderConstantF(0, map, 1);
    if(g_cacheSupported){
        const auto frame=vrframe::Current();const auto shift=Shift(frame.fov,frame.ipdUnits);
        memcpy(g_cachedTuning,shift.tuning,sizeof(g_cachedTuning));
        d->SetPixelShaderConstantF(1,g_cachedTuning,1);
    }
    // Clear may be called both inside and outside a game BeginScene/EndScene.
    bool opened = SUCCEEDED(d->BeginScene());
    HRESULT hr = DrawQuad(d, cw, ch);
    if (opened) d->EndScene();
    g_cacheValid=SUCCEEDED(hr)&&g_cacheSupported;
    return SUCCEEDED(hr);
}

// The owned depth is also what CopyDepth uses to locate the foreground
// boundary. A separate backbuffer-size heuristic can disagree with that
// boundary under Winlator's wrapped device. Use the same surface/counter.
bool FirstPersonPass() { return g_zClears >= 2; }

void OnClear(IDirect3DDevice9* d, DWORD flags, bool inGame) {
    if (!g_ok || !inGame || theater::g_modDraw || !(flags & D3DCLEAR_ZBUFFER)) return;
    IDirect3DSurface9* ds = nullptr;
    d->GetDepthStencilSurface(&ds);
    bool ours = ds && ds == g_depthSurf;
    if (g_config.depthDebug) {
        static int n = 0;
        if (n++ < 40) {
            D3DSURFACE_DESC desc = {};
            if (ds) ds->GetDesc(&desc);
            Log("Stereo: Clear Z (flags %lx) depth %p %ux%u fmt %d %s, clears this frame %d", flags, ds, desc.Width,
                desc.Height, desc.Format, ours ? "OURS" : "other", g_zClears);
        }
    }
    if (ds) ds->Release();
    if (!ours) return;
    // первая очистка кадра - перед сценой; следующая - перед руками (сцена уже нарисована)
    if (++g_zClears >= 2 && !g_copied) {
        g_copied = CopyDepth(d);
    }
}

void ProbeDepth(IDirect3DDevice9* d) {
    // глубина -> R32F -> системная память, статистика в лог
    // Do not overwrite the saved world with the cleared first-person buffer.
    if (!g_copied && !CopyDepth(d)) return;
    IDirect3DSurface9* sys = nullptr;
    D3DSURFACE_DESC description{};g_depthCopySurf->GetDesc(&description);
    const unsigned stride=description.Format==D3DFMT_G32R32F?2:1;
    if (FAILED(d->CreateOffscreenPlainSurface(g_gw, g_gh, description.Format, D3DPOOL_SYSTEMMEM, &sys, nullptr))) return;
    if (SUCCEEDED(d->GetRenderTargetData(g_depthCopySurf, sys))) {
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
            float mn = 1e9f, mx = -1e9f;
            int ones = 0, total = 0;
            for (UINT y = 0; y < g_gh; y += 8)
                for (UINT x = 0; x < g_gw; x += 8) {
                    float v = ((float*)((BYTE*)lr.pBits + y * lr.Pitch))[x*stride];
                    mn = v < mn ? v : mn;
                    mx = v > mx ? v : mx;
                    ones += v >= 0.99999f;
                    total++;
                }
            float c = ((float*)((BYTE*)lr.pBits + (g_gh / 2) * lr.Pitch))[(g_gw / 2)*stride];
            Log("Stereo: depth probe min %.6f max %.6f centre %.6f, %d of %d at 1.0", mn, mx, c, ones, total);
            sys->UnlockRect();
        }
    }
    sys->Release();
}

void EndFrame() {
    g_aimDepth=nullptr;
    if (g_ok && g_zClears && !g_clearsLogged) {
        g_clearsLogged = true;
        Log("Stereo: %d depth clears per frame, scene depth %s", g_zClears, g_copied ? "copied before the first-person pass" : "read at Present");
    }
    g_zClears = 0;
    g_copied = false;
    g_cacheValid=false;
    g_worldDepthValid=false;
}
IDirect3DTexture9* WorldDepth(){return g_worldDepthValid?g_worldDepthTex:nullptr;}
bool SplitActive() { return g_split; }

void Upscale(IDirect3DDevice9* d) {
    if (!g_split) return;
    IDirect3DSurface9* back = nullptr;
    if (FAILED(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)) || !back) return;
    RECT src = {0, 0, (LONG)g_gw, (LONG)g_gh};
    d->StretchRect(back, &src, g_upSurf, nullptr, D3DTEXF_POINT);
    d->StretchRect(g_upSurf, nullptr, back, nullptr, D3DTEXF_LINEAR);
    back->Release();
}
unsigned GameWidth() { return g_gw; }
unsigned GameHeight() { return g_gh; }

bool Compose(IDirect3DDevice9* d, float hfovDeg, float ipdUnits, unsigned cw, unsigned ch) {
    g_aimDepth=nullptr;
    g_worldDepthValid=false;
    if (!g_ok) return false;
    IDirect3DSurface9* back = nullptr;
    if (FAILED(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)) || !back) return false;
    // картинка движка сейчас cw x ch (динамическое разрешение) - в угол текстуры цвета
    if (!cw || cw > g_gw) cw = g_gw;
    if (!ch || ch > g_gh) ch = g_gh;
    // Dynamic resolution must reduce reconstruction work as well as the game.
    UINT outputW = g_outputSurf ? cw * 2 : g_w;
    UINT outputH = g_outputSurf ? ch : g_h;
    UINT ew = outputW / 2;
    RECT src = {0, 0, (LONG)cw, (LONG)ch};
    if (FAILED(d->StretchRect(back, &src, g_colorSurf, &src, D3DTEXF_POINT))) {
        back->Release();
        return false;
    }
    if (g_config.depthDebug) {
        static DWORD lastProbe = 0;
        if (GetTickCount() - lastProbe > 1000) {
            lastProbe = GetTickCount();
            ProbeDepth(d);
        }
    }

    // Сдвиг точки на расстоянии z (доли ширины глаза): K / z, K = (ipd/2) / (2 tan(fov/2))
    const auto shift=Shift(hfovDeg,ipdUnits);
    const float n=shift.nearPlane,f=shift.farPlane,K=shift.k;
    const float base=shift.tuning[0],slope=shift.tuning[1],maxShift=shift.tuning[2];

    PassGuard guard(d);
    SetupPass(d);
    if (FAILED(d->BeginScene())) { back->Release(); return false; }
    bool merged = false;
    // Quest's trace shows the second clear starts Flash. Its live depth
    // must not overwrite the world saved before this clear in geometry mode.
    if (g_copied && !g_config.weaponGeometry && SUCCEEDED(d->SetRenderTarget(0, g_depthMergedSurf))) {
        D3DVIEWPORT9 vp = {0, 0, cw, ch, 0, 1};
        d->SetViewport(&vp);
        d->SetPixelShader(g_psMerge);
        d->SetTexture(1, g_depthCopyTex);
        d->SetTexture(2, g_depthTex);
        const float map[4] = {(float)g_gw / g_w, (float)g_gh / g_h, 0, 0};
        d->SetPixelShaderConstantF(0, map, 1);
        merged = SUCCEEDED(DrawQuad(d, cw, ch, (float)cw / g_gw, (float)ch / g_gh));
        d->SetTexture(1, nullptr);
        d->SetTexture(2, nullptr);
    }
    bool drawn=SUCCEEDED(d->SetRenderTarget(0, g_outputSurf ? g_outputSurf : back));
    bool depthOutput=g_worldDepthSurf&&!g_config.depthDebug&&SUCCEEDED(d->SetRenderTarget(1,g_worldDepthSurf));
    if(depthOutput)d->SetRenderState(D3DRS_COLORWRITEENABLE1,0xf);
    d->SetDepthStencilSurface(nullptr);
    d->SetVertexDeclaration(g_decl);
    d->SetVertexShader(g_vs);
    d->SetPixelShader(g_config.depthDebug ? g_psDebug : g_ps);
    d->SetTexture(0, g_colorTex);
    d->SetTexture(1, merged ? g_depthMergedTex : g_copied ? g_depthCopyTex : g_depthTex);
    bool cached=g_copied&&!merged&&g_cacheValid&&!memcmp(g_cachedTuning,shift.tuning,sizeof(g_cachedTuning));
    const float cache[4]={cached?1.f:0.f,0,0,0};d->SetPixelShaderConstantF(4,cache,1);
    if(cached&&!g_cacheLogged){g_cacheLogged=true;Log("Stereo: same-frame RG32F disparity cache active; original depth/search/edge filtering retained");}
    for (int s = 0; s < 2; s++) {
        d->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        d->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        d->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        d->SetSamplerState(s, D3DSAMP_SRGBTEXTURE, FALSE);
    }
    d->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    d->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    d->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    d->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    d->SetRenderState(D3DRS_ZENABLE, FALSE);
    d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    d->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    d->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    d->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    d->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    d->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    // глубина картинки движка - в левом верхнем углу текстуры размера кадра
    const float depthMap[4] = {(float)cw / (g_copied ? g_gw : g_w),
                               (float)ch / (g_copied ? g_gh : g_h), 0.0f, 0.0f};
    d->SetPixelShaderConstantF(1, depthMap, 1);
    const float colorMap[4] = {(float)cw / g_gw, (float)ch / g_gh, 0.0f, 0.0f};
    d->SetPixelShaderConstantF(2, colorMap, 1);
    const float texelSize[4] = {1.0f / cw, 1.0f / ch, g_config.depthNearSoftness, 0};
    d->SetPixelShaderConstantF(3, texelSize, 1);
    struct V { float x, y, z, w, u, v; };
    // половина пикселя (правило D3D9): сдвигаем прямоугольник, чтобы пиксели совпали с текселями
    float hx = 1.0f / ew, hy = 1.0f / outputH;
    V q[4] = {{-1 - hx, 1 + hy, 0, 1, 0, 0}, {1 - hx, 1 + hy, 0, 1, 1, 0},
              {-1 - hx, -1 + hy, 0, 1, 0, 1}, {1 - hx, -1 + hy, 0, 1, 1, 1}};
    for (int eye = 0; eye < 2; eye++) {
        D3DVIEWPORT9 vp = {eye * ew, 0, ew, outputH, 0, 1};
        d->SetViewport(&vp);
        // левый глаз: точки сдвигаются вправо, правый - влево
        const float params[4] = {base, slope, eye == 0 ? 1.0f : -1.0f, maxShift};
        d->SetPixelShaderConstantF(0, params, 1);
        drawn &= SUCCEEDED(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(V)));
    }
    drawn &= SUCCEEDED(d->EndScene());
    if(depthOutput)d->SetRenderTarget(1,nullptr);
    g_worldDepthValid=drawn&&depthOutput;
    if (g_outputSurf) {
        d->SetTexture(0, nullptr);
        d->SetTexture(1, nullptr);
        d->SetRenderTarget(0, back);
        RECT output = {0, 0, (LONG)outputW, (LONG)outputH};
        drawn &= SUCCEEDED(d->StretchRect(g_outputSurf, &output, back, nullptr, D3DTEXF_LINEAR));
    }
    back->Release();
    if(drawn&&!g_config.depthDebug){
        g_aimDepth=merged?g_depthMergedTex:g_copied?g_depthCopyTex:g_depthTex;
        g_aimCached=cached;g_aimOutputW=outputW;g_aimOutputH=outputH;
        g_aimShift[0]=base;g_aimShift[1]=slope;g_aimShift[3]=maxShift;
        memcpy(g_aimMap,depthMap,sizeof(g_aimMap));
    }
    if (drawn && !g_logged) {
        g_logged = true;
        Log("Stereo: our depth surface %p", g_depthSurf);
        Log("Stereo: first stereo frame (near %.1f far %.0f, K %.3f, max shift %.3f)", n, f, K, maxShift);
    }
    return drawn;
}

unsigned DrawAimReticles(IDirect3DDevice9* d,const float* weaponUV,const float* plasmidUV){
    if(!g_config.aimReticles||!Ready()||!g_aimDepth||g_aimUnavailable||(!weaponUV&&!plasmidUV))return 0;
    if(!g_psAim&&FAILED(d->CreatePixelShader(kAimreticlePS,&g_psAim))){g_aimUnavailable=true;return 0;}
    struct ModDraw{bool previous;ModDraw():previous(theater::g_modDraw){theater::g_modDraw=true;}~ModDraw(){theater::g_modDraw=previous;}} mod;
    PassGuard guard(d);SetupPass(d);
    IDirect3DSurface9* back=nullptr;if(FAILED(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back))||!back)return 0;
    HRESULT bound=d->SetRenderTarget(0,back);back->Release();if(FAILED(bound)||FAILED(d->BeginScene()))return 0;
    d->SetPixelShader(g_psAim);d->SetTexture(0,g_aimDepth);d->SetTexture(1,WorldDepth());
    d->SetPixelShaderConstantF(1,g_aimMap,1);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);
    d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
    d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
    unsigned count=0;const UINT ew=g_w/2;
    float radius=fmaxf(3.f,5.5f*g_h/864.f),pad=ceilf(radius+3);
    const float* targets[]={weaponUV,plasmidUV};
    for(const float* target:targets){
        if(!target||!isfinite(target[0])||!isfinite(target[1])||target[0]<0||target[0]>1||target[1]<0||target[1]>1)continue;
        const bool left=target==plasmidUV;
        const float point[4]={target[0],target[1],radius,g_config.depthNearSoftness};
        const float size[4]={(float)ew,(float)g_h,g_aimCached?1.f:0.f,0};
        const float color[4]={left?.25f:1.f,left?.9f:1.f,1.f,1.f};
        d->SetPixelShaderConstantF(2,point,1);d->SetPixelShaderConstantF(3,size,1);d->SetPixelShaderConstantF(4,color,1);
        float x0=fmaxf(0,floorf((target[0]-g_aimShift[3])*ew-pad)),x1=fminf(ew,ceilf((target[0]+g_aimShift[3])*ew+pad));
        float y0=fmaxf(0,floorf(target[1]*g_h-pad)),y1=fminf(g_h,ceilf(target[1]*g_h+pad));
        struct V{float x,y,z,w,u,v;};
        auto vertex=[&](float x,float y){return V{2*x/ew-1-1.f/ew,1-2*y/g_h+1.f/g_h,0,1,x/ew,y/g_h};};
        V quad[4]={vertex(x0,y0),vertex(x1,y0),vertex(x0,y1),vertex(x1,y1)};
        bool ok=true;
        for(unsigned eye=0;eye<2;eye++){
            D3DVIEWPORT9 viewport={eye*ew,0,ew,g_h,0,1};d->SetViewport(&viewport);
            float shift[4]={g_aimShift[0],g_aimShift[1],eye==0?1.f:-1.f,g_aimShift[3]};
            const float map[4]={(float)g_aimOutputW*.5f/g_w,(float)g_aimOutputH/g_h,eye*(float)g_aimOutputW*.5f/g_w,g_worldDepthValid?1.f:0.f};
            d->SetPixelShaderConstantF(0,shift,1);d->SetPixelShaderConstantF(5,map,1);
            ok&=SUCCEEDED(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(V)));
        }
        if(ok)count++;
    }
    d->EndScene();return count;
}

void Release() {
    if(g_psAim)g_psAim->Release();g_psAim=nullptr;g_aimUnavailable=false;g_aimDepth=nullptr;
    if(g_psCache)g_psCache->Release();g_psCache=nullptr;
    g_cacheSupported=g_cacheValid=g_cacheLogged=false;memset(g_cachedTuning,0,sizeof(g_cachedTuning));
    if(g_worldDepthSurf)g_worldDepthSurf->Release();if(g_worldDepthTex)g_worldDepthTex->Release();
    g_worldDepthSurf=nullptr;g_worldDepthTex=nullptr;g_worldDepthValid=false;
    if (g_outputSurf) g_outputSurf->Release();
    if (g_outputTex) g_outputTex->Release();
    g_outputSurf = nullptr; g_outputTex = nullptr;
    if (g_originalDepth) g_originalDepth->Release();
    g_originalDepth = nullptr;
    g_active = false;
    g_pendingInit = g_initializing = false;
    if (g_upSurf) g_upSurf->Release();
    if (g_upTex) g_upTex->Release();
    g_upSurf = nullptr;
    g_upTex = nullptr;
    g_split = false;
    if (g_depthSurf) g_depthSurf->Release();
    if (g_depthTex) g_depthTex->Release();
    if (g_colorSurf) g_colorSurf->Release();
    if (g_colorTex) g_colorTex->Release();
    if (g_vs) g_vs->Release();
    if (g_ps) g_ps->Release();
    if (g_psCopy) g_psCopy->Release();
    if (g_psMerge) g_psMerge->Release();
    if (g_depthMergedSurf) g_depthMergedSurf->Release();
    if (g_depthMergedTex) g_depthMergedTex->Release();
    g_psMerge = nullptr;
    g_depthMergedSurf = nullptr;
    g_depthMergedTex = nullptr;
    if (g_depthCopySurf) g_depthCopySurf->Release();
    if (g_depthCopyTex) g_depthCopyTex->Release();
    g_psCopy = nullptr;
    g_depthCopySurf = nullptr;
    g_depthCopyTex = nullptr;
    if (g_psDebug) g_psDebug->Release();
    g_psDebug = nullptr;
    if (g_decl) g_decl->Release();
    if (g_state) g_state->Release();
    g_depthSurf = nullptr;
    g_depthTex = nullptr;
    g_colorSurf = nullptr;
    g_colorTex = nullptr;
    g_vs = nullptr;
    g_ps = nullptr;
    g_decl = nullptr;
    g_state = nullptr;
    g_ok = false;
    g_replaced = false;
    g_zClears = 0;
    g_copied = g_clearsLogged = g_logged = false;
}

}  // namespace stereo
