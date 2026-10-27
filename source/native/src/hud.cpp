// Интерфейс игры в VR: показатели на левом запястье, подсказки перед глазами.
//
// Весь Flash-интерфейс BioShock (HUD, подсказки, меню) рисует рендерер Scaleform GFx
// (Bioshock.exe 1.1, функция 0x10b8a1f0-0x10b8a500). Игра копит команды и выполняет их в конце кадра,
// поэтому мод отличает вызовы отрисовки по адресу возврата: всё, что рисует GFx, в игровом
// режиме уходит в прозрачную текстуру того же размера, что кадр; 3D-сцена и оружие - в кадр.
// В Present середина текстуры (подсказки, прицел, субтитры) возвращается в кадр, а углы -
// здоровье/EVE (слева сверху) и патроны (справа снизу) - рисуются панелью на левом запястье
// (см. theater::GameHands).
#include "hud.h"

#include <windows.h>
#include <d3d9.h>

#include "d3dguard.h"
#include <stdio.h>
#include <string.h>

#include "MinHook.h"
#include "config.h"
#include "log.h"
#include "stereo.h"
#include "uobject.h"
#include "weaponlayer.h"
#include "worldresolution.h"
#include "foregroundroute.h"
#include "weapon.h"
#include "plasmid.h"
#include "navigation.h"
#include "engineviewport.h"
#include "hudcoverage.h"
#include "hudraster.h"

namespace hud {
namespace {

const uintptr_t kGfxBegin = 0x10b8a1f0, kGfxEnd = 0x10b8a500;  // отрисовка треугольников Flash (GFx), 0x10b8a1f0; соседний код рисует мир
const uintptr_t kTileBegin = 0x10b90a70, kTileEnd = 0x10b90f55;  // DrawTile холста движка: полоски здоровья, подсказки, прицел
const uintptr_t kRenderScene = 0x10aed400;    // сцена (thiscall, без аргументов)
const uintptr_t kRenderOverlay = 0x10aed970;  // сцена + HUD + Flash (thiscall, без аргументов)
const unsigned char kSceneBytes[] = {0x83, 0xEC, 0x24, 0x53, 0x55, 0x56, 0x8B, 0xF1};
const unsigned char kOverlayBytes[] = {0x6A, 0xFF, 0x68, 0xD6, 0xC0, 0xF5, 0x10};

typedef void(__fastcall* ThisFn)(void* self, void* edx);
ThisFn g_scene = nullptr, g_overlay = nullptr;
typedef HRESULT(STDMETHODCALLTYPE* SetRTFn)(IDirect3DDevice9*, DWORD, IDirect3DSurface9*);
SetRTFn g_setRT = nullptr;

IDirect3DDevice9* g_device = nullptr;
IDirect3DTexture9* g_tex = nullptr;
IDirect3DSurface9* g_surf = nullptr;
IDirect3DSurface9* g_back = nullptr;   // задний буфер на время фазы (со ссылкой)
IDirect3DSurface9* g_hudDepth=nullptr;
IDirect3DSurface9* g_savedDepth=nullptr;
float g_rasterScale=1;
IDirect3DStateBlock9* g_state = nullptr;
UINT g_w = 0, g_h = 0;
volatile LONG g_active = 0;
bool g_phase = false;       // игра сейчас рисует HUD (в нашу текстуру)
bool g_suspended = false;
bool g_haveFrame = false;   // в текстуре HUD этого кадра
bool g_logged = false;

// Отладка: вызовы отрисовки по фазам кадра (0 - до сцены/в сцене, 1 - фаза HUD, 2 - после)
typedef HRESULT(STDMETHODCALLTYPE* DrawIPFn)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
typedef HRESULT(STDMETHODCALLTYPE* DrawPUPFn)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, const void*, UINT);
typedef HRESULT(STDMETHODCALLTYPE* DrawIPUPFn)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT, UINT, const void*, D3DFORMAT, const void*, UINT);
DrawIPFn g_drawIP = nullptr;
weaponlayer::DrawPrimitiveFn g_drawPrimitive = nullptr;
DrawPUPFn g_drawPUP = nullptr;
DrawIPUPFn g_drawIPUP = nullptr;
int g_stage = 0;
unsigned g_counts[3] = {};
void* g_firstAfter = nullptr;
unsigned g_sceneCalls = 0, g_overlayCalls = 0, g_beginFail = 0;
// Отладка: один кадр по порядку - откуда вызвана отрисовка и включена ли глубина
volatile LONG g_dump = 0;
struct Rec { void* ret; DWORD z; bool resolved,foreground; char api; UINT nv,pc,rw,rh,dw,dh; bool shader; };
Rec g_recs[4096];
unsigned g_recCount = 0;
bool g_redirect = false;
bool g_tileIsHud = false;    // текущая отрисовка DrawTile - интерфейс (не перенос сцены на экран)      // сейчас рисует GFx - цель подменена на текстуру HUD
bool g_clearedThisFrame = false;
bool g_gfxThisFrame = false;
hudlayout::Source g_source{},g_pendingSource{};
unsigned g_captureDraws=0;
bool Ensure(IDirect3DDevice9* d);

void Redirect(bool gfx) {
    IDirect3DDevice9* d = g_device;
    if (!d || !g_setRT) return;
    if (gfx && !g_redirect && g_active) {
        IDirect3DSurface9* cur = nullptr;
        d->GetRenderTarget(0, &cur);
        IDirect3DSurface9* back = nullptr;
        d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back);
        bool onBack = cur && cur == back;
        if (cur) cur->Release();
        if (!onBack || !Ensure(d)) {
            if (back) back->Release();
            return;
        }
        if (g_back) g_back->Release();
        g_back = back;
        D3DVIEWPORT9 vp;
        d->GetViewport(&vp);
        RECT clip{};d->GetScissorRect(&clip);
        // Actual Quest capture: Flash lays out against this full viewport,
        // independently of the smaller square 3D engine canvas.
        auto scaled=[](unsigned v){return unsigned(std::lround(v*g_rasterScale));};
        const auto source=hudlayout::Observe(g_w,g_h,scaled(vp.X),scaled(vp.Y),scaled(vp.Width),scaled(vp.Height));
        if(!source.Valid()){back->Release();g_back=nullptr;return;}
        if(!g_pendingSource.Valid()||memcmp(&source,&g_pendingSource,sizeof(source))){
            Log("HUD: UI viewport %u,%u %ux%u, engine %ux%u, source %u,%u %ux%u in %ux%u",
                vp.X,vp.Y,vp.Width,vp.Height,engineviewport::Width(),engineviewport::Height(),
                source.x,source.y,source.width,source.height,g_w,g_h);
            g_pendingSource=source;
        }
        if(g_savedDepth)g_savedDepth->Release();g_savedDepth=nullptr;
        d->GetDepthStencilSurface(&g_savedDepth);d->SetDepthStencilSurface(nullptr);
        if(FAILED(g_setRT(d,0,g_surf))){d->SetDepthStencilSurface(g_savedDepth);if(g_savedDepth)g_savedDepth->Release();g_savedDepth=nullptr;d->SetViewport(&vp);d->SetScissorRect(&clip);return;}
        d->SetDepthStencilSurface(g_hudDepth);
        if (!g_clearedThisFrame) {
            D3DVIEWPORT9 full = {0, 0, g_w, g_h, 0, 1};
            d->SetViewport(&full);
            DWORD scissor=0;d->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissor);d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
            d->Clear(0, nullptr, D3DCLEAR_TARGET|(g_hudDepth?(D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL):0), D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);
            d->SetRenderState(D3DRS_SCISSORTESTENABLE,scissor);
            g_clearedThisFrame = true;
        }
        d->SetViewport(&vp);  // смена цели сбрасывает область вывода - возвращаем как было у игры
        d->SetScissorRect(&clip);
        g_redirect = true;
        g_gfxThisFrame = true;
    } else if (!gfx && g_redirect) {
        D3DVIEWPORT9 vp;
        d->GetViewport(&vp);
        RECT clip{};d->GetScissorRect(&clip);
        d->SetDepthStencilSurface(nullptr);g_setRT(d, 0, g_back);d->SetDepthStencilSurface(g_savedDepth);
        if(g_savedDepth)g_savedDepth->Release();g_savedDepth=nullptr;
        d->SetViewport(&vp);
        d->SetScissorRect(&clip);
        g_redirect = false;
    }
}

bool g_afterResolve=false; // completed canvas resolve, also followed by world translucency
void Count(void* ret,char api,UINT nv,UINT pc) {
    uintptr_t r = (uintptr_t)ret;
    // DrawTile холста (полоски здоровья) не перехватываем: им же игра собирает и саму сцену
    Redirect(r >= kGfxBegin && r < kGfxEnd);
    if(g_redirect)g_captureDraws++;
    static bool coverageLogged=false;
    if(g_redirect&&g_config.gameplayHud&&!coverageLogged){
        coverageLogged=true;DWORD mask=0,blend=0,src=0,dst=0;
        g_device->GetRenderState(D3DRS_COLORWRITEENABLE,&mask);g_device->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend);
        g_device->GetRenderState(D3DRS_SRCBLEND,&src);g_device->GetRenderState(D3DRS_DESTBLEND,&dst);
        Log("HUD coverage: native write mask %lu, RGB blend %lu %lu/%lu; independent alpha enabled",mask,blend,src,dst);
    }
    if (g_dump == 2 && g_recCount < 4096 && g_device) {
        DWORD z = 0;
        g_device->GetRenderState(D3DRS_ZENABLE, &z);
        IDirect3DSurface9 *rt=nullptr,*ds=nullptr;D3DSURFACE_DESC color{},depth{};
        g_device->GetRenderTarget(0,&rt);g_device->GetDepthStencilSurface(&ds);
        if(rt){rt->GetDesc(&color);rt->Release();}if(ds){ds->GetDesc(&depth);ds->Release();}
        IDirect3DVertexShader9* vs=nullptr;g_device->GetVertexShader(&vs);bool shader=vs!=nullptr;if(vs)vs->Release();
        g_recs[g_recCount++] = {ret,z,g_afterResolve,stereo::FirstPersonPass(),api,nv,pc,color.Width,color.Height,depth.Width,depth.Height,shader};
    }
    g_counts[g_stage]++;
    if (g_stage == 2 && !g_firstAfter) g_firstAfter = ret;
}
// Руки Джека (скелетная модель) в проходе от первого лица не рисуем - видно только оружие
bool SkipArms(void* ret, UINT nv = 0, UINT pc = 0) {
    if ((uintptr_t)ret == 0x10b91e43) g_afterResolve = true;
    if (!g_config.hideArms || !g_active) return false;
    uintptr_t r = (uintptr_t)ret;
    // руки - сетка с известным числом вершин (настройка ArmsVertices, через запятую)
    if (!foregroundroute::MeshSite(r)) return false;
    if(plasmid::IsHandsMesh(nv,pc))return true;
    for (int i = 0; i < 8 && g_config.armsVertices[i]; i++)
        if (nv == g_config.armsVertices[i]) return true;
    return false;
}

unsigned g_meshDraws=0,g_foregroundDraws=0,g_routedDraws=0,g_originalKept=0,g_beforeCanvas=0;
void Captured(UINT nv,UINT pc){
    g_routedDraws++;
}
foregroundroute::Action TryWeapon(uintptr_t site,UINT nv,UINT pc){
    if(!g_config.weaponGeometry||!g_active)return foregroundroute::Action::Keep;
    const bool matched=weapon::GeometryMesh(nv,pc)||
                       foregroundroute::NavigationMesh(g_config.navigationGeometry&&navigation::GeometrySelected(),nv,pc);
    if(foregroundroute::MeshSite(site)){g_meshDraws++;if(matched){g_foregroundDraws++;if(!g_afterResolve)g_beforeCanvas++;}}
    return foregroundroute::Decide(site,worldresolution::WorldReady(),matched);
}
HRESULT STDMETHODCALLTYPE HookedDrawPrimitive(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,UINT start,UINT count){
    uintptr_t site=(uintptr_t)__builtin_return_address(0);
    UINT vertices=t==D3DPT_TRIANGLELIST?count*3:(t==D3DPT_TRIANGLESTRIP||t==D3DPT_TRIANGLEFAN)?count+2:0;
    bool skip=SkipArms((void*)site,vertices,count);Count((void*)site,'P',vertices,count);
    HudCoverage coverage(d,g_redirect&&g_config.gameplayHud);
    HudRaster raster(d,g_redirect,g_rasterScale);
    if(skip)return D3D_OK;
    const auto route=TryWeapon(site,vertices,count);
    float relocation=foregroundroute::NavigationMesh(g_config.navigationGeometry&&navigation::GeometrySelected(),vertices,count)?navigation::Current().stereoScale:1;
    if(route==foregroundroute::Action::Capture&&weaponlayer::DrawPrimitive(d,t,start,count,g_drawPrimitive,relocation)){Captured(vertices,count);return D3D_OK;}
    if(route==foregroundroute::Action::Capture)g_originalKept++;
    return g_drawPrimitive(d,t,start,count);
}
HRESULT STDMETHODCALLTYPE HookedDrawIP(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, INT b, UINT mi, UINT nv, UINT si, UINT pc) {
    bool skip = SkipArms(__builtin_return_address(0), nv, pc);
    Count(__builtin_return_address(0),'I',nv,pc);
    HudCoverage coverage(d,g_redirect&&g_config.gameplayHud);
    HudRaster raster(d,g_redirect,g_rasterScale);
    const uintptr_t site=(uintptr_t)__builtin_return_address(0);
    if(g_active&&foregroundroute::MeshSite(site)&&worldresolution::WorldReady()&&plasmid::HandGeometry(nv,pc)){
        if(weaponlayer::DrawPlasmidHand(d,t,b,mi,nv,si,pc,g_drawIP))Captured(nv,pc);
        return D3D_OK; // the full original arm draw is never used as fallback
    }
    if (skip) return D3D_OK;
    const auto route=TryWeapon(site,nv,pc);
    float relocation=foregroundroute::NavigationMesh(g_config.navigationGeometry&&navigation::GeometrySelected(),nv,pc)?navigation::Current().stereoScale:1;
    if(route==foregroundroute::Action::Capture&&weaponlayer::DrawIndexed(d,t,b,mi,nv,si,pc,g_drawIP,relocation)){Captured(nv,pc);return D3D_OK;}
    if(route==foregroundroute::Action::Capture)g_originalKept++;
    return g_drawIP(d, t, b, mi, nv, si, pc);
}
// Прямоугольник DrawTile на весь экран - это перенос сцены (HDR -> экран), а не интерфейс
bool FullscreenQuad(const void* v, UINT stride, UINT verts) {
    if (!v || stride < 8 || verts < 3) return false;
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (UINT i = 0; i < verts && i < 6; i++) {
        const float* p = (const float*)((const BYTE*)v + i * stride);
        x0 = p[0] < x0 ? p[0] : x0;
        x1 = p[0] > x1 ? p[0] : x1;
        y0 = p[1] < y0 ? p[1] : y0;
        y1 = p[1] > y1 ? p[1] : y1;
    }
    float w = x1 - x0, h = y1 - y0;
    if (x1 <= 2.0f && y1 <= 2.0f && x0 >= -2.0f && y0 >= -2.0f) return w > 1.6f && h > 1.6f;  // координаты отсечения
    return g_w && w > g_w/g_rasterScale * 0.8f && h > g_h/g_rasterScale * 0.8f;                // native pixels
}

HRESULT STDMETHODCALLTYPE HookedDrawPUP(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, UINT pc, const void* v, UINT st) {
    uintptr_t r = (uintptr_t)__builtin_return_address(0);
    if (r == 0x10b91e43) g_afterResolve = true;
    if (r >= kTileBegin && r < kTileEnd) {
        UINT verts = t == D3DPT_TRIANGLESTRIP || t == D3DPT_TRIANGLEFAN ? pc + 2 : pc * 3;
        g_tileIsHud = !FullscreenQuad(v, st, verts);
    }
    Count((void*)r,'U',t==D3DPT_TRIANGLELIST?pc*3:pc+2,pc);
    HudCoverage coverage(d,g_redirect&&g_config.gameplayHud);
    HudRaster raster(d,g_redirect,g_rasterScale);
    HRESULT result=g_drawPUP(d,t,pc,v,st);
    if(SUCCEEDED(result)&&r>=kTileBegin&&r<kTileEnd)weaponlayer::ReplayPostProcess(d,t,pc,v,st,g_drawPUP);
    return result;
}
HRESULT STDMETHODCALLTYPE HookedDrawIPUP(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, UINT mi, UINT nv, UINT pc, const void* i, D3DFORMAT f, const void* v, UINT st) {
    Count(__builtin_return_address(0),'X',nv,pc);
    HudCoverage coverage(d,g_redirect&&g_config.gameplayHud);
    HudRaster raster(d,g_redirect,g_rasterScale);
    return g_drawIPUP(d, t, mi, nv, pc, i, f, v, st);
}

HRESULT STDMETHODCALLTYPE HookedSetRT(IDirect3DDevice9* d, DWORD index, IDirect3DSurface9* surf) {
    if ((g_phase || g_redirect) && !g_suspended && index == 0 && surf && surf == g_back) surf = g_surf;
    return g_setRT(d, index, surf);
}

bool Ensure(IDirect3DDevice9* d) {
    IDirect3DSurface9* back = nullptr;
    if (FAILED(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)) || !back) return false;
    D3DSURFACE_DESC desc;
    back->GetDesc(&desc);
    back->Release();
    const float requested=g_config.gameplayHud?g_config.hudSupersample:1;
    unsigned w=unsigned(std::lround(desc.Width*requested)),h=unsigned(std::lround(desc.Height*requested));
    if(w>4096||h>4096){w=desc.Width;h=desc.Height;}
    if (g_tex && w == g_w && h == g_h) return true;
    Release();
    if (FAILED(d->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                D3DPOOL_DEFAULT, &g_tex, nullptr))) {
        Log("HUD: cannot create %ux%u texture", w,h);
        return false;
    }
    g_tex->GetSurfaceLevel(0, &g_surf);
    if(FAILED(d->CreateDepthStencilSurface(w,h,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&g_hudDepth,nullptr))){
        Log("HUD: independent high-resolution stencil unavailable; retaining native draws");Release();return false;
    }
    d->CreateStateBlock(D3DSBT_ALL, &g_state);
    g_w=w;g_h=h;g_rasterScale=float(w)/desc.Width;
    Log("HUD: texture %ux%u, native canvas %ux%u, raster %.2fx; world resolution unchanged",g_w,g_h,desc.Width,desc.Height,g_rasterScale);
    return true;
}

void Begin() {
    g_haveFrame = false;
    IDirect3DDevice9* d = g_device;
    if (!d || !g_active || !g_setRT || !Ensure(d)) return;
    IDirect3DSurface9* cur = nullptr;
    d->GetRenderTarget(0, &cur);
    IDirect3DSurface9* back = nullptr;
    d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back);
    bool onBack = cur && cur == back;
    if (cur) cur->Release();
    if (!onBack) {
        if (back) back->Release();
        return;
    }
    g_back = back;  // ссылку держим до End
    g_setRT(d, 0, g_surf);
    d->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);
    g_phase = true;
    g_suspended = false;
}

void End() {
    if (!g_phase) return;
    g_phase = false;
    g_setRT(g_device, 0, g_back);
    g_back->Release();
    g_back = nullptr;
    g_haveFrame = true;
    if (!g_logged) {
        g_logged = true;
        Log("HUD: drawn into its own texture (wrist + front)");
    }
}

void __fastcall HookedScene(void* self, void* edx) {
    g_scene(self, edx);
    g_sceneCalls++;
    g_stage = 1;
}

void __fastcall HookedOverlay(void* self, void* edx) {
    g_overlay(self, edx);
    g_overlayCalls++;
    g_stage = 2;
}

void ParseRect(const char* key, float* r, const float* def) {
    char buf[96], defs[96];
    snprintf(defs, sizeof(defs), "%g %g %g %g", def[0], def[1], def[2], def[3]);
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) strcpy(slash + 1, "BioshockVR.ini");
    GetPrivateProfileStringA("VR", key, defs, buf, sizeof(buf), path);
    if (sscanf(buf, "%f %f %f %f", &r[0], &r[1], &r[2], &r[3]) != 4) memcpy(r, def, sizeof(float) * 4);
}

float g_left[4], g_right[4];  // области углов HUD (доли кадра: x0 y0 x1 y1)

}  // namespace

void UpdateReticle(void* controller,void* pawn) {
    if(!g_config.hideCrosshair)return;
    struct Field {void* cls=nullptr;int offset=-1;unsigned mask=0;};
    static Field fields[3];
    static void* controllerClass=nullptr;
    static int hudOff=-1;
    if(!controller||IsBadReadPtr(controller,0x30))return;
    void* cls=*(void**)((char*)controller+0x2c);
    if(cls!=controllerClass){
        controllerClass=cls;
        hudOff=uobject::PropertyOffset(controller,L"myHUD");
        if(hudOff<=0)hudOff=uobject::PropertyOffset(controller,L"HUD");
    }
    void* objects[3]={controller,pawn,hudOff>0&&!IsBadReadPtr((char*)controller+hudOff,4)?*(void**)((char*)controller+hudOff):nullptr};
    for(int i=0;i<3;i++){
        void* obj=objects[i];if(!obj||IsBadReadPtr(obj,0x30))continue;
        void* type=*(void**)((char*)obj+0x2c);Field& f=fields[i];
        if(f.cls!=type){
            f={type,-1,0};
            if(uobject::BoolProperty(obj,L"bReticleDisabled",f.offset,f.mask))
                Log("HUD: reticle disabled through %ls +0x%x mask %08x",uobject::ClassName(obj),f.offset,f.mask);
            else { f.offset=-1;Log("HUD: no writable bReticleDisabled on %ls",uobject::ClassName(obj)); }
        }
        if(f.offset>0&&f.offset<0x10000&&f.mask&&!IsBadWritePtr((char*)obj+f.offset,4))
            *(unsigned*)((char*)obj+f.offset)|=f.mask;
    }
}

bool Install() {
    if (memcmp((void*)kRenderScene, kSceneBytes, sizeof(kSceneBytes)) != 0 ||
        memcmp((void*)kRenderOverlay, kOverlayBytes, sizeof(kOverlayBytes)) != 0) {
        Log("HUD: render functions not found (other exe version?)");
        return false;
    }
    const float defLeft[4] = {0.0f, 0.0f, 0.36f, 0.26f}, defRight[4] = {0.70f, 0.76f, 1.0f, 1.0f};
    ParseRect("HudLeftArea", g_left, defLeft);
    ParseRect("HudRightArea", g_right, defRight);
    if (MH_CreateHook((void*)kRenderScene, (void*)HookedScene, (void**)&g_scene) != MH_OK ||
        MH_CreateHook((void*)kRenderOverlay, (void*)HookedOverlay, (void**)&g_overlay) != MH_OK ||
        MH_EnableHook((void*)kRenderScene) != MH_OK || MH_EnableHook((void*)kRenderOverlay) != MH_OK) {
        Log("HUD: hooks failed");
        return false;
    }
    Log("HUD: render hooks installed (left area %.2f %.2f %.2f %.2f, right area %.2f %.2f %.2f %.2f)",
        g_left[0], g_left[1], g_left[2], g_left[3], g_right[0], g_right[1], g_right[2], g_right[3]);
    return true;
}

void SetDevice(IDirect3DDevice9* device) {
    g_device = device;
    void** vt = *(void***)device;
    auto attach=[&](unsigned slot,void* hook,void** original){
        if(!*original&&MH_CreateHook(vt[slot],hook,original)!=MH_OK)return false;
        MH_STATUS result=MH_EnableHook(vt[slot]);
        return result==MH_OK||result==MH_ERROR_ENABLED;
    };
    bool rt=attach(37,(void*)HookedSetRT,(void**)&g_setRT);
    bool indexed=attach(82,(void*)HookedDrawIP,(void**)&g_drawIP);
    bool ordinary=attach(81,(void*)HookedDrawPrimitive,(void**)&g_drawPrimitive);
    bool canvas=attach(83,(void*)HookedDrawPUP,(void**)&g_drawPUP);
    bool indexedUP=attach(84,(void*)HookedDrawIPUP,(void**)&g_drawIPUP);
    Log("HUD: enabled hooks indexed=%d ordinary=%d canvas=%d indexedUP=%d target=%d",indexed,ordinary,canvas,indexedUP,rt);
}

void SetActive(bool active) {
    InterlockedExchange(&g_active, active ? 1 : 0);
    if(!active){Redirect(false);g_haveFrame=false;g_gfxThisFrame=false;g_clearedThisFrame=false;g_source={};g_pendingSource={};}
}

void Suspend() {
    if (!g_phase || g_suspended) return;
    g_suspended = true;
    g_setRT(g_device, 0, g_back);
}

void Resume() {
    if (!g_phase || !g_suspended) return;
    g_suspended = false;
    g_setRT(g_device, 0, g_surf);
}

// Explicit read-only, one-shot diagnostics. Independent from DevHarness/input.
// Never reads a command stream or injects a key/mouse/controller event.
void CaptureOnce(IDirect3DSurface9* surface,const char* name){
    if(!surface)return;D3DSURFACE_DESC desc{};surface->GetDesc(&desc);IDirect3DSurface9* sys=nullptr;
    if(FAILED(g_device->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&sys,nullptr)))return;
    if(SUCCEEDED(g_device->GetRenderTargetData(surface,sys))){
        D3DLOCKED_RECT lr{};if(SUCCEEDED(sys->LockRect(&lr,nullptr,D3DLOCK_READONLY))){
            char path[MAX_PATH];GetModuleFileNameA(nullptr,path,MAX_PATH);char* slash=strrchr(path,'\\');if(slash)strcpy(slash+1,name);
            FILE* f=fopen(path,"wb");
            if(f){BITMAPFILEHEADER fh{};BITMAPINFOHEADER ih{};ih.biSize=sizeof(ih);ih.biWidth=desc.Width;ih.biHeight=-(LONG)desc.Height;
                ih.biPlanes=1;ih.biBitCount=32;fh.bfType=0x4d42;fh.bfOffBits=sizeof(fh)+sizeof(ih);fh.bfSize=fh.bfOffBits+desc.Width*desc.Height*4;
                fwrite(&fh,sizeof(fh),1,f);fwrite(&ih,sizeof(ih),1,f);
                for(unsigned y=0;y<desc.Height;y++)fwrite((BYTE*)lr.pBits+y*lr.Pitch,4,desc.Width,f);fclose(f);}
            unsigned rgb=0,alpha=0,outside=0,minX=desc.Width,minY=desc.Height,maxX=0,maxY=0;
            for(unsigned y=0;y<desc.Height;y++)for(unsigned x=0;x<desc.Width;x++){
                DWORD c=((DWORD*)((BYTE*)lr.pBits+y*lr.Pitch))[x];if(c>>24)alpha++;
                if(c&0xffffff){rgb++;minX=std::min(minX,x);minY=std::min(minY,y);maxX=std::max(maxX,x);maxY=std::max(maxY,y);
                    if(x<g_source.x||y<g_source.y||x>=g_source.x+g_source.width||y>=g_source.y+g_source.height)outside++;}
            }
            Log("HUD capture: %s saved=%d RGB=%u alpha=%u bounds=%u,%u..%u,%u outside source=%u; GFx draws=%u",
                name,f!=nullptr,rgb,alpha,minX,minY,maxX,maxY,outside,g_captureDraws);sys->UnlockRect();
        }
    }else Log("HUD capture: readback failed for %s",name);
    sys->Release();
}
void BeginPresent() {
    Redirect(false);
    g_afterResolve = false;
    g_haveFrame = g_gfxThisFrame;
    g_source=g_haveFrame?g_pendingSource:hudlayout::Source{};
    static DWORD checked=0;
    if(g_active&&worldresolution::WorldReady()&&stereo::Ready()&&GetTickCount()-checked>=2000){
        checked=GetTickCount();char path[MAX_PATH];GetModuleFileNameA(nullptr,path,MAX_PATH);char* slash=strrchr(path,'\\');if(slash)strcpy(slash+1,"hud_capture.once");
        if(GetFileAttributesA(path)!=INVALID_FILE_ATTRIBUTES){
            DeleteFileA(path);CaptureOnce(g_surf,"hud_capture.bmp");
            IDirect3DSurface9* back=nullptr;g_device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back);CaptureOnce(back,"hud_scene.bmp");if(back)back->Release();
            g_dump=1;
        }
    }
    g_captureDraws=0;
    g_gfxThisFrame = false;
    g_clearedThisFrame = false;
}

void FrameEnd() {
    static DWORD routeLog=0;
    static bool missingRouteCaptured=false;
    if(g_config.weaponGeometry&&GetTickCount()-routeLog>=5000){
        if(g_active&&worldresolution::WorldReady()){
            Log("Weapon route: mesh %u, selected model %u (before canvas %u), geometry captured %u, original retained %u",g_meshDraws,g_foregroundDraws,g_beforeCanvas,g_routedDraws,g_originalKept);
            // Read-only capture of one real frame when the expected route is
            // absent. It never sends game/controller input or changes poses.
            if(g_meshDraws&&!g_routedDraws&&!missingRouteCaptured){missingRouteCaptured=true;g_dump=1;}
        }
        routeLog=GetTickCount();g_meshDraws=g_foregroundDraws=g_routedDraws=g_originalKept=g_beforeCanvas=0;
    }
    if (g_dump == 2) {
        // сводка: подряд идущие вызовы с одного адреса и с одной глубиной - одной строкой
        Log("HUD: frame dump, %u draws", g_recCount);
        unsigned i = 0;
        while (i < g_recCount) {
            unsigned j = i;
            const Rec& r=g_recs[i];
            while(j<g_recCount&&g_recs[j].ret==r.ret&&g_recs[j].z==r.z&&g_recs[j].resolved==r.resolved&&
                  g_recs[j].foreground==r.foreground&&g_recs[j].api==r.api&&g_recs[j].rw==r.rw&&g_recs[j].rh==r.rh&&
                  g_recs[j].dw==r.dw&&g_recs[j].dh==r.dh&&g_recs[j].shader==r.shader)j++;
            Log("HUD: %4u x%-4u %c from %p z=%lu resolved=%d foreground=%d RT=%ux%u DS=%ux%u VS=%d nv=%u pc=%u",
                i,j-i,r.api,r.ret,r.z,r.resolved,r.foreground,r.rw,r.rh,r.dw,r.dh,r.shader,r.nv,r.pc);
            if(r.resolved||r.foreground)
                for(unsigned k=i+1;k<j;k++)Log("HUD:          +%u nv %u pc %u",k-i,g_recs[k].nv,g_recs[k].pc);
            i = j;
        }
        g_dump = 0;
    }
    if (g_dump == 1) {
        g_dump = 2;
        g_recCount = 0;
    }
    g_stage = 0;
}

void DumpFrame() { InterlockedExchange(&g_dump, 1); }

bool Ready() { return g_haveFrame && g_tex && g_active; }
IDirect3DTexture9* Texture() { return g_tex; }
const float* LeftArea() { return g_left; }
const float* RightArea() { return g_right; }
hudlayout::Source Source(){return Ready()?g_source:hudlayout::Source{};}
hudlayout::Layout Layout(){return hudlayout::Partition({g_left[0],g_left[1],g_left[2],g_left[3]},
                                                      {g_right[0],g_right[1],g_right[2],g_right[3]},g_config.hudDashboard,g_config.hudVrText,g_config.hudMessagesOnLeft);}

// Середина HUD обратно в кадр (всё, кроме двух углов, которые ушли на запястье)
void CompositeFront(IDirect3DDevice9* d, int views, float shiftPx, bool squareSource) {
    TargetsGuard guard(d);  // вернуть игре её цель, буфер глубины и область вывода
    if (!Ready() || !g_state) return;
    IDirect3DSurface9* back = nullptr;
    if (FAILED(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)) || !back) return;
    struct V { float x, y, z, rhw, u, v; };
    float W = (float)g_w, H = (float)g_h;
    const float* L = g_left;
    const float* R = g_right;
    // дополнение двух углов: полоса справа от левого угла, средняя полоса, полоса слева от правого
    float rects[3][4] = {{L[2], 0, 1, L[3]}, {0, L[3], 1, R[1]}, {0, R[1], R[0], 1}};
    g_state->Capture();
    g_setRT(d, 0, back);
    d->SetDepthStencilSurface(nullptr);
    D3DVIEWPORT9 vp = {0, 0, g_w, g_h, 0, 1};
    d->SetViewport(&vp);
    d->SetVertexShader(nullptr);
    d->SetPixelShader(nullptr);
    d->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
    d->SetTexture(0, g_tex);
    d->SetRenderState(D3DRS_ZENABLE, FALSE);
    d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    d->SetRenderState(D3DRS_LIGHTING, FALSE);
    d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    d->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);           // цвет в текстуре уже умножен на прозрачность
    d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    d->SetRenderState(D3DRS_FOGENABLE, FALSE);
    d->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    d->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    d->SetRenderState(D3DRS_COLORWRITEENABLE, 0x7);
    d->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    d->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    d->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    d->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    d->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    d->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    d->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
    d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    d->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    d->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    d->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    d->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    d->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    d->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    d->BeginScene();
    if (views == 2 || squareSource) {
        // стереопара: каждому глазу - квадратная середина HUD (кадр широкий, а поле глаза
        // квадратное; углы ушли на запястье), со сдвигом, как у предмета в паре метров
        // HudScale > 1 - берём меньшую середину HUD: на экране она крупнее
        float front = 1.0f + (g_config.hudScale - 1.0f) * 0.4f;  // перед глазами мягче, чтобы субтитры не обрезались
        float ew = W / views, side = H / (front > 0.5f ? front : 1.0f);
        for (int eye = 0; eye < views; eye++) {
            float sx = (W - side) / 2 - (views == 1 ? 0.0f : eye == 0 ? shiftPx : -shiftPx) * side;
            float sy = (H - side) / 2;
            float x0 = eye * ew - 0.5f, x1 = x0 + ew, y0 = -0.5f, y1 = H - 0.5f;
            float u0 = sx / W, u1 = (sx + side) / W, v0 = sy / H, v1 = (sy + side) / H;
            V q[4] = {{x0, y0, 0, 1, u0, v0}, {x1, y0, 0, 1, u1, v0}, {x0, y1, 0, 1, u0, v1}, {x1, y1, 0, 1, u1, v1}};
            d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(V));
        }
    } else {
        for (auto& r : rects) {
            if (r[2] <= r[0] || r[3] <= r[1]) continue;
            float x0 = r[0] * W - 0.5f, y0 = r[1] * H - 0.5f, x1 = r[2] * W - 0.5f, y1 = r[3] * H - 0.5f;
            V q[4] = {{x0, y0, 0, 1, r[0], r[1]}, {x1, y0, 0, 1, r[2], r[1]},
                      {x0, y1, 0, 1, r[0], r[3]}, {x1, y1, 0, 1, r[2], r[3]}};
            d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(V));
        }
    }
    d->EndScene();
    g_state->Apply();
    back->Release();
}

void Release() {
    if (g_back) g_back->Release();
    if (g_surf) g_surf->Release();
    if (g_tex) g_tex->Release();
    if (g_state) g_state->Release();
    if(g_hudDepth)g_hudDepth->Release();if(g_savedDepth)g_savedDepth->Release();
    g_hudDepth=g_savedDepth=nullptr;g_rasterScale=1;
    g_back = nullptr;
    g_surf = nullptr;
    g_tex = nullptr;
    g_state = nullptr;
    g_phase = false;
    g_redirect = false;
    g_haveFrame = false;
    g_gfxThisFrame=false;g_clearedThisFrame=false;g_source={};g_pendingSource={};g_captureDraws=0;
    g_w = g_h = 0;
}

}  // namespace hud
