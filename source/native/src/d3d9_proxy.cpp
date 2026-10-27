// d3d9.dll in the game folder: loads the real d3d9 (DXVK in WinlatorXR) and wraps the device.
//
// Every Present the frame gets the sync pixel WinlatorXR reads back from the window: red is
// the index of the head pose the frame was rendered with, blue the eye (alternate eye
// rendering), alpha 255. Then the next frame switches eye and takes a new pose.
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdlib.h>
#include <psapi.h>
#include <math.h>

#include "MinHook.h"
#include "camera.h"
#include "modegate.h"
#include "menupresentation.h"
#include "renderpolicy.h"
#include "crash.h"
#include "devharness.h"
#include "hud.h"
#include "gameplayhud.h"
#include "hudbridge.h"
#include "input.h"
#include "mouse.h"
#include "stereo.h"
#include "theater.h"
#include "uistate.h"
#include "weapon.h"
#include "plasmid.h"
#include "aimreticle.h"
#include "weaponlayer.h"
#include "tutorial.h"
#include "healing.h"
#include "questprompts.h"
#include "navigation.h"
#include "melee.h"
#include "config.h"
#include "log.h"
#include "stallsampler.h"
#include "vrframe.h"
#include "xrapi.h"
#include "pcvr.h"
#include "engineviewport.h"
#include "worldresolution.h"
#include "dynamicresolution.h"

namespace {

HMODULE g_real = nullptr;
typedef IDirect3D9*(WINAPI* Direct3DCreate9Fn)(UINT);
typedef HRESULT(WINAPI* Direct3DCreate9ExFn)(UINT, IDirect3D9Ex**);
Direct3DCreate9Fn g_create9 = nullptr;
Direct3DCreate9ExFn g_create9ex = nullptr;

typedef HRESULT(STDMETHODCALLTYPE* CreateDeviceFn)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD,
                                                    D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
typedef HRESULT(STDMETHODCALLTYPE* PresentFn)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
typedef HRESULT(STDMETHODCALLTYPE* ResetFn)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
CreateDeviceFn g_createDevice = nullptr;
typedef HRESULT(STDMETHODCALLTYPE* ClearFn)(IDirect3DDevice9*, DWORD, const D3DRECT*, DWORD, D3DCOLOR, float, DWORD);
ClearFn g_clear = nullptr;
typedef HRESULT(STDMETHODCALLTYPE* SetDSFn)(IDirect3DDevice9*, IDirect3DSurface9*);
SetDSFn g_setDS = nullptr;
typedef HRESULT(STDMETHODCALLTYPE* CreateTexFn)(IDirect3DDevice9*, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9**, HANDLE*);
CreateTexFn g_createTex = nullptr;
HRESULT STDMETHODCALLTYPE HookedCreateTex(IDirect3DDevice9* d, UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT f, D3DPOOL pool,
                                          IDirect3DTexture9** out, HANDLE* sh) {
    HRESULT hr = g_createTex(d, w, h, levels, usage, f, pool, out, sh);
    if (usage & (D3DUSAGE_DEPTHSTENCIL | D3DUSAGE_RENDERTARGET)) {
        static int n = 0;
        if (n++ < 40) {
            char fcc[5] = {(char)(f & 0xff), (char)((f >> 8) & 0xff), (char)((f >> 16) & 0xff), (char)((f >> 24) & 0xff), 0};
            Log("Device: CreateTexture %ux%u usage=%s format=%d '%s' -> %p (caller %p)", w, h,
                usage & D3DUSAGE_DEPTHSTENCIL ? "DEPTH" : "RT", f, f > 0xffff ? fcc : "", out ? *out : nullptr,
                __builtin_return_address(0));
        }
    }
    return hr;
}
HRESULT STDMETHODCALLTYPE HookedSetDS(IDirect3DDevice9* d, IDirect3DSurface9* surf) {
    static IDirect3DSurface9* seen[16];
    static int nseen = 0;
    bool known = false;
    for (int i = 0; i < nseen; i++) known |= seen[i] == surf;
    if (!known && nseen < 16) {
        seen[nseen++] = surf;
        D3DSURFACE_DESC desc = {};
        if (surf) surf->GetDesc(&desc);
        Log("Device: SetDepthStencilSurface %p %ux%u format=%d (caller %p)", surf, desc.Width, desc.Height, desc.Format,
            __builtin_return_address(0));
    }
    return g_setDS(d, stereo::MapDepth(surf));
}
bool g_inGameNow = false;
bool g_worldVisible = false;
bool g_worldContent = false;
bool g_frameNativeHud=false;
bool g_menuContent = false;
HRESULT STDMETHODCALLTYPE HookedClear(IDirect3DDevice9* d, DWORD count, const D3DRECT* rects, DWORD flags, D3DCOLOR color,
                                      float z, DWORD stencil) {
    stereo::OnClear(d, flags, g_inGameNow);
    return g_clear(d, count, rects, flags, color, z, stencil);
}
typedef HRESULT(STDMETHODCALLTYPE* CreateDSFn)(IDirect3DDevice9*, UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, DWORD, BOOL,
                                               IDirect3DSurface9**, HANDLE*);
CreateDSFn g_createDS = nullptr;
HRESULT STDMETHODCALLTYPE HookedCreateDS(IDirect3DDevice9* d, UINT w, UINT h, D3DFORMAT f, D3DMULTISAMPLE_TYPE ms,
                                         DWORD q, BOOL discard, IDirect3DSurface9** out, HANDLE* sh) {
    HRESULT hr = g_createDS(d, w, h, f, ms, q, discard, out, sh);
    static int n = 0;
    if (n++ < 20) Log("Device: CreateDepthStencilSurface %ux%u format=%d -> %p (caller %p)", w, h, f, out ? *out : nullptr,
                      __builtin_return_address(0));
    return hr;
}
UINT g_backW = 0, g_backH = 0;
PresentFn g_present = nullptr;
ResetFn g_reset = nullptr;

bool g_started = false;
unsigned g_frames = 0;

void AdjustParams(D3DPRESENT_PARAMETERS* pp) {
    if (!pp || !g_config.enabled) return;
    // The sync pixel needs an alpha channel that survives to the window
    if (pp->BackBufferFormat == D3DFMT_X8R8G8B8 || pp->BackBufferFormat == D3DFMT_UNKNOWN)
        pp->BackBufferFormat = D3DFMT_A8R8G8B8;
    // The headset paces frames; waiting for the X server's vsync only adds latency
    pp->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    g_backW = pp->BackBufferWidth;
    g_backH = pp->BackBufferHeight;
    stereo::AdjustParams(pp);
    // соотношение сторон картинки движка (при стерео по глубине - половина кадра)
    if (pp->BackBufferHeight)
        vrframe::SetAspect(g_config.gameWidth && g_config.gameHeight &&
                                   (g_config.gameWidth < pp->BackBufferWidth || g_config.gameHeight < pp->BackBufferHeight)
                               ? (float)g_config.gameWidth / g_config.gameHeight
                               : stereo::Enabled() ? (float)pp->BackBufferWidth / 2 / pp->BackBufferHeight
                                                   : (float)pp->BackBufferWidth / pp->BackBufferHeight);
    Log("Device: %ux%u format=%d windowed=%d autodepth=%d depthformat=%d", pp->BackBufferWidth, pp->BackBufferHeight,
        pp->BackBufferFormat, pp->Windowed, pp->EnableAutoDepthStencil, pp->AutoDepthStencilFormat);
}

void WriteSyncPixel(IDirect3DDevice9* device) {
    const vrframe::Frame& f = vrframe::Current();
    IDirect3DSurface9* back = nullptr;
    if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)) || !back) return;
    RECT pixel = {0, 0, 1, 1};
    // В игре - номер позы, с которой камера посчитала этот кадр, а не самой свежей: иначе шлем
    // доворачивает картинку на чужую позу и при поворотах головы она дёргается
    // Before tracking arrives the corner is explicit metadata, rather than a
    // menu pixel that could poison WinlatorXR's persistent colour mapping.
    int sync = f.active ? f.sync : 0;
    int cam = camera::CameraSync();
    static unsigned frames = 0, mismatch = 0, lastTicks = 0;
    if (g_inGameNow && cam >= 0) {
        frames++;
        if (cam != f.sync) mismatch++;
        if (g_config.syncFromCamera) sync = cam;
    }
    static DWORD lastLog = 0;
    if (g_inGameNow && GetTickCount() - lastLog > 5000) {
        lastLog = GetTickCount();
        unsigned ticks = camera::CameraTicks();
        Log("Sync: camera thread %lu, present thread %lu, camera pose != newest in %u of %u frames, %u camera ticks",
            camera::CameraThread(), GetCurrentThreadId(), mismatch, frames, ticks - lastTicks);
        lastTicks = ticks;
        frames = mismatch = 0;
    }
    // альфа 0 - WinlatorXR пропускает пиксель (отладка: сравнить картинку без синхронизации)
    // Kind belongs to this image: 0=completed world, 1=full-size raw menu,
    // 2=transition. The compositor must never display a transition as a window.
    int kind=g_config.nativeCinema?(g_worldContent?0:g_menuContent?1:2):0;
    static unsigned worldSerial=0;
    unsigned blue=f.eye?255:0;
    if(kind==0&&g_worldContent&&g_config.depthStereo&&xrapi::WorldPosePinReady()){
        unsigned tag=(++worldSerial)&65535;
        if(xrapi::ExactPoseReady()&&camera::CameraFrameId()&&xrapi::SendFramePose(tag,camera::CameraFrameId(),g_frameNativeHud)){
            kind=4;sync=(tag>>8)&255;blue=tag&255;
        }else{kind=3;blue=tag&255;}
    }
    device->ColorFill(back, &pixel, D3DCOLOR_ARGB(g_config.syncPixel && f.active ? 255 : 0,
                                               sync & 0xFF, kind, blue));
    back->Release();
}

// Поле зрения кинотеатра: 0 в настройках = полное поле шлема (края кадра совпадают с краем линз
// и не видны); больше 0 - уже, экран меню чётче, но руки и луч обрезаются рамкой
float TheaterFov() {
    if (g_config.theaterFov > 1) return theater::Fov(g_config.theaterFov);
    float fov = vrframe::Current().fov;
    return fov > 1 ? fov : g_config.headsetFov;
}

// Динамическое разрешение: сторона квадратной картинки движка в игре. Видеочип загружен до
// предела - шлем не успевает поправлять кадр под поворот головы и картинка дрожит, поэтому
// держим загрузку видеочипа ниже ~88% и частоту кадров у частоты шлема.
unsigned g_dynSide = 0;
DynamicResolution g_dynamic;
DWORD g_dynLast = 0;
unsigned g_dynFrames = 0;

int GpuBusyPercent() {
    // Z: is imagefs, not Android /. The installer provides this read-only
    // symlink to the app-readable real sysfs counter.
    FILE* f = fopen("Z:\\usr\\tmp\\bioshock-gpu-busy", "rb");
    if (!f) return -1;
    int v = -1;
    if (fscanf(f, "%d", &v) != 1) v = -1;
    fclose(f);
    return v>=0&&v<=100?v:-1;
}

void UpdateDynamicRes(bool inGame) {
    unsigned maxSide = stereo::GameWidth() < stereo::GameHeight() ? stereo::GameWidth() : stereo::GameHeight();
    if (!maxSide) return;
    unsigned previousSide=g_dynSide;
    g_dynSide=g_dynamic.Configure(maxSide,g_config.dynamicRes);
    if(previousSide&&previousSide!=g_dynSide)Log("Dynamic source: %u -> %u, capacity %u",previousSide,g_dynSide,maxSide);
    g_dynFrames++;
    DWORD now = GetTickCount();
    if (!g_dynLast) g_dynLast = now;
    if (now - g_dynLast < 500 || !g_config.dynamicRes) return;
    float fps = g_dynFrames * 1000.0f / (now - g_dynLast);
    g_dynLast = now;
    g_dynFrames = 0;
    if (!inGame) return;
    int busy = GpuBusyPercent();
    static int loadAvailable=-1;
    if(loadAvailable!=(busy>=0)){
        loadAvailable=busy>=0;
        Log("GPU load reader: %s (sample %d%%)",loadAvailable?"Android counter available":"unavailable, quality retained",busy);
    }
    unsigned minSide = g_config.dynamicMin < maxSide ? g_config.dynamicMin : maxSide;
    unsigned old = g_dynSide;
    // Follow the configured game cap when adapting resolution. The GPU is
    // shared with the headset compositor, so also react to GPU saturation.
    float target = g_config.gameFpsCap >= 10 ? g_config.gameFpsCap : 72.0f;
    // A slow x86/game thread does not become faster by blurring the image.
    // Reduce only with measured GPU saturation; unknown load keeps quality.
    g_dynSide=g_dynamic.Adjust(fps,busy,minSide,target);
    if (g_dynSide != old) {
        static DWORD lastLog = 0;
        if (now - lastLog > 3000) {
            lastLog = now;
            Log("Dynamic resolution: %u -> %u (fps %.1f, gpu %d%%)", old, g_dynSide, fps, busy);
        }
    }
}

bool g_inGame = false;
bool g_levelLoading = false;

// Игра или меню. Меню - когда камера игрока не считается (главное меню, загрузка, ролик) или
// когда интерфейс открыл свой контекст (пауза, инвентарь, взлом...). В меню мод рисует
// "кинотеатр": экран с кадром игры в пустоте, руки и курсор; в игре - VR-вид от головы.
// WinlatorXR всё время в VR-режиме, плоский экран больше не используется.
void UpdateMode(IDirect3DDevice9* device, const vrframe::Frame& f) {
    int calls = camera::TakeCallCount();
    // в игру - только когда камера игрока считается несколько кадров подряд: во время загрузок
    // она мелькает, и режим (а с ним размер картинки) дёргался бы туда-сюда
    static ModeGate gate;
    bool inGame=gate.Update(calls>0&&camera::ViewportReady(),uistate::MenuOpen(),GetTickCount());
    bool changed = inGame != g_inGame;
    if (changed) {
        Log("Mode: %s", inGame ? "game (VR view)" : "menu (theater)");
        if (inGame) theater::Leave();
        else {weapon::ReleaseRigid();g_worldVisible=false;}  // menu/loading starts a new handoff
    }
    g_inGame = inGame;
    if(inGame||uistate::MenuOpen())g_levelLoading=false;
    weapon::SetCinematic(uistate::Cinematic());
    if(uistate::Cinematic())weapon::ReleaseRigid();
    worldresolution::Update(inGame,GetTickCount());
    g_inGameNow = inGame && f.active;
    // Disabling the cinema is a state change, not a per-frame network job.
    if(inGame && g_worldVisible && (changed || (g_frames % 30) == 0)) xrapi::SendCinema(0);
    stereo::SetActive(device, inGame);
    // Стерео по глубине: кадр двойной ширины, движок рисует в левую половину, мод строит оба
    // глаза. Иначе в меню (по настройке) - широкий экран движка, в игре - весь кадр.
    UpdateDynamicRes(inGame && f.active);
    const auto render=renderpolicy::Resolve(inGame,f.active,stereo::Ready()||stereo::SplitActive(),g_dynSide,g_backW,g_backH,g_config.menuAspect);
    camera::SetEngineSize(render.width,render.height,g_backW,g_backH);
    vrframe::SetInGame(inGame);
    input::SetActive(inGame && f.active && !uistate::Cinematic());
    hud::SetActive(((inGame && f.active) || g_config.hudInMenu) && g_config.wristHud);
    input::Update(vrframe::State());
    // Повторяем время от времени: WinlatorXR забывает режим при перезапуске своей сессии
    // Режим шлема отправляем всегда: в плоском режиме (меню) WinlatorXR не присылает позу головы,
    // и если ждать "активности", команда вернуться в VR не ушла бы никогда
    static bool lastVR = true;
    static int stableFrames = 0;
    bool rawVR = inGame || !g_config.flatMenus;
    // меняем режим шлема, только если новое состояние держится полсекунды (во время загрузок
    // камера игрока то появляется, то пропадает - иначе картинка мигала бы VR/плоский экран)
    stableFrames = rawVR != lastVR ? stableFrames + 1 : 0;
    bool wantVR = stableFrames > 36 ? rawVR : lastVR;
    if (wantVR != lastVR) {
        Log("Mode: headset %s", wantVR ? "VR" : "flat screen (menu)");
        lastVR = wantVR;
        changed = true;
    }
    float haptic=melee::HapticRight();static float previousHaptic=0;
    bool hapticChanged=haptic!=previousHaptic;previousHaptic=haptic;
    if(g_config.nativeCinema&&stereo::Enabled()&&!pcvr::Enabled()){
        // Resource Reset is not a change in the transport format. The native
        // quad does not need a different stock projection/FOV for menus.
        if(changed||hapticChanged||haptic>0||(g_frames%30)==0)
            xrapi::SendMode(true,1,f.fov>1?f.fov:g_config.headsetFov,
                            f.fovV>1?f.fovV:g_config.headsetFov,0,haptic);
    } else if (changed||hapticChanged||haptic>0||(g_frames % 30) == 0)
        // меню в плоском режиме WinlatorXR: окно игры как экран шлема на его полном разрешении
        // Flat menus are a full-width mono frame, even while the VR/flat mode
        // transition is debounced. Enabling depth stereo must not split them.
        xrapi::SendMode(wantVR, !wantVR ? 0 : ((stereo::Ready() && (inGame || !g_config.flatMenus)) || (!inGame && g_config.menuStereo)) ? 1 : (inGame && g_config.stereo ? 2 : 0),
                        inGame ? f.fov : TheaterFov(),
                        inGame ? f.fovV : vrframe::VerticalFov(TheaterFov()), 0,haptic);
}

// Время видеочипа: движок (от прошлого Present до нашего) и проходы мода (до настоящего Present).
// Запросы читаются через несколько кадров, без ожидания.
namespace gputime {
#ifdef BIOSHOCK_RELEASE
void Mark(IDirect3DDevice9*,int){}void Release(){}void Report(const char*){}
#else
struct Set { IDirect3DQuery9* disjoint = nullptr; IDirect3DQuery9* freq = nullptr; IDirect3DQuery9* t[6] = {}; bool issued = false; unsigned seen=0; };
Set g_sets[4];
int g_cur = 0;
bool g_ok = true;
double g_engine = 0, g_mod = 0;
unsigned g_samples = 0;
double g_stereo=0,g_weapon=0,g_hud=0,g_tail=0;
unsigned g_stageSamples=0;

bool Make(IDirect3DDevice9* d, Set& s) {
    if (s.disjoint) return true;
    if (FAILED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT, &s.disjoint)) ||
        FAILED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ, &s.freq))) return false;
    for (int i = 0; i < 6; i++)
        if (FAILED(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP, &s.t[i]))) return false;
    return true;
}

void Release() {
    for (Set& s : g_sets) {
        if (s.disjoint) s.disjoint->Release();
        if (s.freq) s.freq->Release();
        for (auto& q : s.t) if (q) q->Release();
        s = Set();
    }
}

// mark 0: кадр начался (после Present), 1: движок закончил (начало нашего Present), 2: перед Present
void Mark(IDirect3DDevice9* d, int mark) {
    if (!g_ok || !g_config.gpuTiming) return;
    Set& s = g_sets[g_cur];
    if (mark == 0) {
        // прочитать самый старый набор
        Set& old = g_sets[(g_cur + 1) % 4];
        if (old.issued) {
            BOOL dis = TRUE;
            UINT64 f = 0, t[6] = {};
            if (old.disjoint->GetData(&dis, sizeof(dis), 0) == S_OK && old.freq->GetData(&f, sizeof(f), 0) == S_OK &&
                old.t[0]->GetData(&t[0], 8, 0) == S_OK && old.t[1]->GetData(&t[1], 8, 0) == S_OK &&
                old.t[2]->GetData(&t[2], 8, 0) == S_OK && !dis && f) {
                g_engine += (double)(t[1] - t[0]) * 1000.0 / f;
                g_mod += (double)(t[2] - t[1]) * 1000.0 / f;
                g_samples++;
                if((old.seen&0x38)==0x38&&old.t[3]->GetData(&t[3],8,0)==S_OK&&
                   old.t[4]->GetData(&t[4],8,0)==S_OK&&old.t[5]->GetData(&t[5],8,0)==S_OK&&
                   t[1]<=t[3]&&t[3]<=t[4]&&t[4]<=t[5]&&t[5]<=t[2]){
                    double ms=1000.0/f;g_stereo+=(t[3]-t[1])*ms;g_weapon+=(t[4]-t[3])*ms;
                    g_hud+=(t[5]-t[4])*ms;g_tail+=(t[2]-t[5])*ms;g_stageSamples++;
                }
            }
            old.issued = false;
        }
        g_cur = (g_cur + 1) % 4;
        Set& n = g_sets[g_cur];
        if (!Make(d, n)) { g_ok = false; Log("GPU timing: queries not supported"); return; }
        n.disjoint->Issue(D3DISSUE_BEGIN);
        n.seen=0;
        n.t[0]->Issue(D3DISSUE_END);
        n.issued = true;
        return;
    }
    if (!s.issued) return;
    s.seen|=1u<<mark;
    s.t[mark]->Issue(D3DISSUE_END);
    if (mark == 2) {
        s.disjoint->Issue(D3DISSUE_END);
        s.freq->Issue(D3DISSUE_END);
    }
}

void Report(const char* mode) {
    if (!g_samples) return;
    Log("GPU time (%s): engine %.2f ms, mod passes %.2f ms per frame (%u samples)", mode, g_engine / g_samples,
        g_mod / g_samples, g_samples);
    if(g_stageSamples)Log("GPU stages (%s): stereo %.2f ms, weapon overlay %.2f ms, HUD/props %.2f ms, tail %.2f ms (%u samples)",
        mode,g_stereo/g_stageSamples,g_weapon/g_stageSamples,g_hud/g_stageSamples,g_tail/g_stageSamples,g_stageSamples);
    g_stereo=g_weapon=g_hud=g_tail=0;g_stageSamples=0;
    g_engine = g_mod = 0;
    g_samples = 0;
}
#endif
}  // namespace gputime

double g_presentMs = 0;  // процессор мода в Present (стерео, HUD, руки), для лога
double g_engineGapMs = 0, g_driverMs = 0, g_limitMs = 0, g_inputMs = 0;
LARGE_INTEGER g_lastHookEnd = {};
unsigned g_trackedFrames = 0, g_cinemaFrames = 0;

// Предел кадров игры в VR. Видеочип общий с WinlatorXR: когда игра рисует кадры так быстро, как
// может, она занимает его целиком, WinlatorXR отдаёт шлему 20-25 кадров вместо 72 и при повороте
// головы картинка дёргается. Ждём после Present - поза для следующего кадра берётся после ожидания.
void LimitFps() {
    static LARGE_INTEGER next = {};
    float cap = g_config.gameFpsCap;
    // WinlatorXR interprets its framerate=0 as the headset refresh rate and
    // exports that as DXVK_FRAME_RATE. A second clock here can wait again
    // after DXVK has already paced the frame. Keep the configured upper limit
    // with one clock; retain our limiter for uncapped/non-DXVK hosts.
    static bool pacingChecked = false;
    static float driverCap = 0;
    if (!pacingChecked) {
        char value[64] = {};
        DWORD length = GetEnvironmentVariableA("DXVK_FRAME_RATE", value, sizeof(value));
        char* end = nullptr;
        float parsed = length && length < sizeof(value) ? strtof(value, &end) : 0;
        if (end && !*end && isfinite(parsed) && parsed > 0) driverCap = parsed;
        pacingChecked = true;
        Log("Frame pacing: game cap %.1f, DXVK cap %.1f, clock %s", cap, driverCap,
            cap >= 10 && driverCap >= 10 && driverCap <= cap ? "DXVK" : "mod");
    }
    if (cap >= 10 && driverCap >= 10 && driverCap <= cap) {
        next.QuadPart = 0;
        return;
    }
    if (cap < 10) {
        next.QuadPart = 0;
        return;
    }
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    LONGLONG period = (LONGLONG)(freq.QuadPart / cap);
    if (!next.QuadPart || now.QuadPart - next.QuadPart > period) {
        next.QuadPart = now.QuadPart;  // отстали больше чем на кадр - не догоняем
    } else {
        while (now.QuadPart < next.QuadPart) {
            LONGLONG left = (next.QuadPart - now.QuadPart) * 1000 / freq.QuadPart;
            Sleep(left >= 2 ? (DWORD)(left - 1) : 0);
            QueryPerformanceCounter(&now);
        }
    }
    next.QuadPart += period;
}

HRESULT STDMETHODCALLTYPE HookedPresent(IDirect3DDevice9* device, const RECT* src, const RECT* dst,
                                        HWND window, const RGNDATA* dirty) {
    LARGE_INTEGER presentStart, qpcFreq;
    QueryPerformanceCounter(&presentStart);
    QueryPerformanceFrequency(&qpcFreq);
    if (g_config.cpuTiming && g_lastHookEnd.QuadPart)
        g_engineGapMs += (presentStart.QuadPart-g_lastHookEnd.QuadPart)*1000.0/qpcFreq.QuadPart;
    bool tracked = vrframe::Current().active;
    static unsigned previousCameraTicks=0;
    unsigned cameraTicks=camera::CameraTicks();
    bool freshCamera=cameraTicks!=previousCameraTicks;
    previousCameraTicks=cameraTicks;
    bool cinema = false;
    g_worldContent=false;
    g_frameNativeHud=false;
    g_menuContent=false;
    gputime::Mark(device, 1);
    hud::BeginPresent();
    if (g_config.enabled && vrframe::Current().active) {
        if (!g_inGame && !pcvr::Enabled()) {
            // A pause first appears in the old square/world viewport. Do not
            // publish that image/layout as a 1280x720 native menu.
            const auto route=menupresentation::Choose(worldresolution::MenuReady(),uistate::MenuOpen(),
                camera::HasPendingView(),camera::ViewportReady(),camera::EngineSizeMatches(g_backW,g_backH),g_levelLoading||!engineviewport::CanResize());
            if(!g_config.nativeCinema||route!=menupresentation::Route::Mask){
                bool loading=route==menupresentation::Route::Loading;
                bool firstLoading=loading&&!g_levelLoading;
                g_levelLoading=loading;
                cinema=theater::Render(device,TheaterFov(),g_config.flatMenus,loading,worldresolution::MenuWidth(),worldresolution::MenuHeight());
                g_menuContent=cinema&&g_config.nativeCinema;
                if(firstLoading&&cinema)Log("Presentation: level travel loading quad and live ray, no unsafe D3D Reset; source %ux%u -> panel %ux%u",g_backW,g_backH,worldresolution::MenuWidth(),worldresolution::MenuHeight());
            }
        }
        else if (!g_inGame) { /* Menu image is submitted as the host's flat quad. */ }
        else if(!camera::HasPendingView()||uistate::MenuOpen()||!freshCamera||
                (!pcvr::Enabled()&&g_config.nativeCinema&&stereo::Enabled()&&!worldresolution::WorldReady())||
                ((stereo::Ready()||stereo::SplitActive())&&g_dynSide&&
                 !camera::EngineSizeMatches(g_dynSide,g_dynSide))){
            // The engine may already be drawing a loading UI into the small
            // gameplay viewport. Never reconstruct it using stale world depth.
            device->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_XRGB(0,0,0),1,0);
        } else {
            bool weaponDrawn = weapon::TakeDrawn();
            const vrframe::Frame& cf = vrframe::Current();
            if (stereo::Ready()) {
                g_worldContent=stereo::Compose(device, cf.fov, cf.ipdUnits, g_dynSide, g_dynSide);
                gputime::Mark(device,3);
                if(g_worldContent&&!uistate::Cinematic())weaponlayer::Composite(device,stereo::WorldDepth());
                gputime::Mark(device,4);
                // HUD - как предмет в 2 м перед глазами (сдвиг в долях ширины картинки)
                float shift = (cf.ipdUnits * 0.5f) / (2.0f * tanf(cf.fov * 3.14159265f / 360.0f)) /
                              (2.0f * g_config.unitsPerMetre) * g_config.depthStrength;
                if(!g_config.gameplayHud)hud::CompositeFront(device, 2, shift);
            } else if (stereo::SplitActive()) {
                stereo::Upscale(device);
                g_worldContent=!stereo::Enabled();
                if(!g_config.gameplayHud)hud::CompositeFront(device, 1, 0, true);
            } else {
                g_worldContent=!stereo::Enabled();
                if(!g_config.gameplayHud)hud::CompositeFront(device);
            }
            if(g_worldContent&&g_config.aimReticles&&!uistate::Cinematic()&&!uistate::ContainerOpen()){
                const auto snapshot=vrframe::Capture();
                const auto right=aimreticle::Project(snapshot,snapshot.state.right);
                const auto left=aimreticle::Project(snapshot,snapshot.state.left);
                const float r[2]={right.u,right.v},l[2]={left.u,left.v};
                stereo::DrawAimReticles(device,right.visible&&weapon::RangedEquipped()?r:nullptr,left.visible&&plasmid::Available()?l:nullptr);
            }
            // контроллеры (по настройке) и панель HUD на запястье
            if(g_config.gameplayHud&&g_worldContent&&hud::Ready()){
                // Custom wrist layout/GPS are produced together in D3D9.
                // The older Android transport has a fixed right-ammo layout.
                bool sent=!g_config.ammoOnLeft&&!g_config.hudDashboard&&!g_config.hudVrText&&!g_config.hudMessagesOnLeft&&xrapi::NativeHudSupported()&&hudbridge::Present(device,hud::Texture(),hud::Source());
                g_frameNativeHud=sent&&xrapi::NativeHudReady();
                static int lastHud=-1;
                if(lastHud!=int(g_frameNativeHud)){lastHud=g_frameNativeHud;Log("Native HUD: %s; gameplay window unchanged",g_frameNativeHud?"compositor active, legacy overlay suppressed":"legacy overlay retained until GPU acknowledgement");}
                if(!g_frameNativeHud){
                    gameplayhud::Options options{g_config.ammoOnLeft,uistate::ContainerOpen()};
                    gameplayhud::Render(device,hud::Texture(),hud::Source(),hud::Layout(),vrframe::State(),cf.fov,stereo::Ready()?2:1,g_config.hudScale,options);
                }
            }
            if (g_config.gameHands || (!g_config.gameplayHud&&hud::Ready()) || g_config.plasmidRay)
                theater::GameHands(device, vrframe::Current().fov, weaponDrawn);
            if (g_config.vignette) theater::Vignette(device);
            gputime::Mark(device,5);
        }
    }
    if(g_worldContent&&!g_worldVisible){
        g_worldVisible=true;
        Log("Presentation: stereo world ready, frame %ux%u source %ux%u; leaving loading panel",g_backW,g_backH,g_dynSide,g_dynSide);
        xrapi::SendCinema(0);
    }
    if (g_config.enabled && !pcvr::Enabled()) WriteSyncPixel(device);
    devharness::BeforePresent(device);
    hud::FrameEnd();
    stereo::EndFrame();
    weaponlayer::EndFrame();
    pcvr::Submit(device, g_inGameNow && stereo::Ready(), g_inGameNow ? camera::CameraFrameId() : vrframe::Current().frameId);
    gputime::Mark(device, 2);
    {
        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);
        g_presentMs += (t.QuadPart - presentStart.QuadPart) * 1000.0 / qpcFreq.QuadPart;
    }
    LARGE_INTEGER driverStart, driverEnd, limitEnd;
    QueryPerformanceCounter(&driverStart);
    HRESULT hr = g_present(device, src, dst, window, dirty);
    QueryPerformanceCounter(&driverEnd);
    if (SUCCEEDED(hr)) xrapi::GraphicsReady();
    if (g_config.enabled) LimitFps();
    QueryPerformanceCounter(&limitEnd);
    if (g_config.cpuTiming) {
        g_driverMs += (driverEnd.QuadPart-driverStart.QuadPart)*1000.0/qpcFreq.QuadPart;
        g_limitMs += (limitEnd.QuadPart-driverEnd.QuadPart)*1000.0/qpcFreq.QuadPart;
    }
    gputime::Mark(device, 0);
    if (g_config.enabled) {
        vrframe::Advance();
        const vrframe::Frame& f = vrframe::Current();
        UpdateMode(device, f);
#ifndef BIOSHOCK_RELEASE
        LARGE_INTEGER inputEnd; QueryPerformanceCounter(&inputEnd);
        if (g_config.cpuTiming) g_inputMs += (inputEnd.QuadPart-limitEnd.QuadPart)*1000.0/qpcFreq.QuadPart;
        static DWORD fpsStart = GetTickCount();
        static unsigned fpsFrames = 0;
        fpsFrames++;
        g_trackedFrames += tracked;
        g_cinemaFrames += cinema;
        // подвисания: самый долгий кадр и сколько кадров дольше 30 мс
        static LARGE_INTEGER prevQpc = {};
        static double worstMs = 0;
        static unsigned slow = 0;
        {
            LARGE_INTEGER q, fq;
            QueryPerformanceCounter(&q);
            QueryPerformanceFrequency(&fq);
            if (prevQpc.QuadPart) {
                double ms = (q.QuadPart - prevQpc.QuadPart) * 1000.0 / fq.QuadPart;
                if (ms > worstMs) worstMs = ms;
                if (ms > 30.0) slow++;
            }
            prevQpc = q;
        }
        DWORD now = GetTickCount();
        if (now - fpsStart >= 5000) {
            double frames = fpsFrames ? fpsFrames : 1;
            Log("FPS: %.1f (%s), worst frame %.0f ms, %u frames over 30 ms, mod camera %.2f ms/frame, mod present %.2f ms/frame, tracking %u/%u, cinema %u/%u",
                fpsFrames * 1000.0f / (now - fpsStart), g_inGame ? "game" : "menu", worstMs, slow,
                  camera::TakeModMs() / frames, g_presentMs / frames, g_trackedFrames, fpsFrames, g_cinemaFrames, fpsFrames);
            if (g_config.cpuTiming)
                Log("CPU frame: engine interval %.2f ms, driver Present %.2f ms, limiter %.2f ms, mode/input %.2f ms",
                    g_engineGapMs/frames, g_driverMs/frames, g_limitMs/frames, g_inputMs/frames);
            if (g_config.cpuTiming) {
                MEMORYSTATUSEX memory{};memory.dwLength=sizeof(memory);
                PROCESS_MEMORY_COUNTERS_EX process{};process.cb=sizeof(process);
                if (GlobalMemoryStatusEx(&memory) && GetProcessMemoryInfo(GetCurrentProcess(),
                        reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&process),sizeof(process)))
                    Log("Memory: virtual free %llu / %llu MiB, private %llu MiB, working set %llu MiB",
                        memory.ullAvailVirtual>>20,memory.ullTotalVirtual>>20,
                        (unsigned long long)process.PrivateUsage>>20,(unsigned long long)process.WorkingSetSize>>20);
            }
            g_engineGapMs = g_driverMs = g_limitMs = g_inputMs = 0;
            g_trackedFrames = g_cinemaFrames = 0;
            g_presentMs = 0;
            worstMs = 0;
            slow = 0;
            gputime::Report(g_inGame ? "game" : "menu");
            const auto replay=weaponlayer::TakeReplayWork();
            if(replay.calls)Log("Weapon postprocess work: %u texture reads / %u legacy, %u candidate passes, %u per-eye draws",
                replay.textureReads,replay.calls*16,replay.calls,replay.replayedDraws);
            fpsStart = now;
            fpsFrames = 0;
        }
        if (g_frames == 0 || (g_frames % 900) == 0)
            Log("Frame %u: headset %s, %s", g_frames, f.active ? "tracking" : "no data yet",
                g_inGame ? "game" : "menu");
#endif
        g_frames++;
    }
    QueryPerformanceCounter(&g_lastHookEnd);
    return hr;
}

HRESULT STDMETHODCALLTYPE HookedReset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* pp) {
    g_worldContent=g_worldVisible=false;
    pcvr::Release();
    theater::Release();  // ресурсы D3DPOOL_DEFAULT должны уйти до Reset
    gputime::Release();
    hud::Release();
    gameplayhud::Release();
    hudbridge::Release();
    weaponlayer::Release();
    stereo::Release();
    AdjustParams(pp);
    HRESULT hr = g_reset(device, pp);
    if (SUCCEEDED(hr)) {
        camera::GraphicsReady();stereo::AfterCreate(device,pp);pcvr::DeviceCreated(device);
        worldresolution::DeviceReady(pp->BackBufferWidth,pp->BackBufferHeight);
    }
    Log("Resolution: D3D Reset %ux%u result 0x%08lx",pp->BackBufferWidth,pp->BackBufferHeight,static_cast<unsigned long>(hr));
    return hr;
}

void HookVtable(void* object, int index, void* hook, void** original) {
    void** vtable = *(void***)object;
    if (vtable[index] == hook) return;
    if (MH_CreateHook(vtable[index], hook, original) == MH_OK) MH_EnableHook(vtable[index]);
}

HRESULT STDMETHODCALLTYPE HookedCreateDevice(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type, HWND window,
                                             DWORD flags, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out) {
    AdjustParams(pp);
    HRESULT hr = g_createDevice(d3d, adapter, type, window, flags, pp, out);
    if (SUCCEEDED(hr)) {
        HWND w = pp && pp->hDeviceWindow ? pp->hDeviceWindow : window;
        theater::SetWindow(w);
        mouse::SetWindow(w);
        input::SetWindow(w);
        if(!pcvr::Enabled()&&g_config.worldBackWidth&&g_config.worldBackHeight){
            worldresolution::Attach(w,pp->BackBufferWidth,pp->BackBufferHeight,
                g_config.worldBackWidth,g_config.worldBackHeight,engineviewport::Resize,g_config.worldWindowed,engineviewport::CanResize);
            worldresolution::DeviceReady(pp->BackBufferWidth,pp->BackBufferHeight);
        }
    }
    if (SUCCEEDED(hr) && out && *out && !g_present) {
        camera::GraphicsReady();
        HookVtable(*out, 16, (void*)HookedReset, (void**)&g_reset);
        HookVtable(*out, 17, (void*)HookedPresent, (void**)&g_present);
        hud::SetDevice(*out);
        stereo::AfterCreate(*out, pp);
        pcvr::DeviceCreated(*out);
        HookVtable(*out, 29, (void*)HookedCreateDS, (void**)&g_createDS);
        HookVtable(*out, 43, (void*)HookedClear, (void**)&g_clear);
        HookVtable(*out, 39, (void*)HookedSetDS, (void**)&g_setDS);
        HookVtable(*out, 23, (void*)HookedCreateTex, (void**)&g_createTex);
        Log("Device created, Present/Reset hooked");
        stallsampler::Stop();
    }
    return hr;
}

void StartOnce() {
    if (g_started) return;
    g_started = true;
    if (!g_config.enabled) {
        Log("Disabled in BioshockVR.ini");
        return;
    }
    camera::Install();
    input::Install();
    uistate::Install();
    weapon::Install();
    healing::Install();
    questprompts::Install();
    tutorial::Install();
    if (g_config.wristHud) hud::Install();
    xrapi::Start("Z:\\tmp\\xr", g_config.stereo ? 2 : g_config.menuStereo ? 1 : 0);
}

bool LoadReal() {
    if (g_real) return true;
    char path[MAX_PATH];
    GetSystemDirectoryA(path, MAX_PATH);
    strcat_s(path, "\\d3d9.dll");
    g_real = LoadLibraryA(path);
    if (!g_real) {
        Log("Cannot load %s", path);
        return false;
    }
    g_create9 = (Direct3DCreate9Fn)GetProcAddress(g_real, "Direct3DCreate9");
    g_create9ex = (Direct3DCreate9ExFn)GetProcAddress(g_real, "Direct3DCreate9Ex");
    Log("Loaded %s", path);
    return true;
}

}  // namespace

extern "C" IDirect3D9* WINAPI Direct3DCreate9(UINT sdk) {
    if (!LoadReal() || !g_create9) return nullptr;
    StartOnce();
    IDirect3D9* d3d = g_create9(sdk);
    if (d3d && g_config.enabled && !g_createDevice) {
        if(!pcvr::Enabled()&&g_config.worldBackWidth){
            D3DDISPLAYMODE desktop{};d3d->GetAdapterDisplayMode(0,&desktop);
            bool supported=false,standard=false;
            unsigned count=d3d->GetAdapterModeCount(0,D3DFMT_X8R8G8B8);
            for(unsigned i=0;i<count&&i<512;++i){
                D3DDISPLAYMODE mode{};
                if(SUCCEEDED(d3d->EnumAdapterModes(0,D3DFMT_X8R8G8B8,i,&mode))){
                    supported|=mode.Width==g_config.worldBackWidth&&mode.Height==g_config.worldBackHeight;
                    standard|=mode.Width==1600&&mode.Height==900;
                }
            }
            Log("Resolution modes: world %ux%u supported=%d, standard 1600x900=%d, desktop %ux%u",g_config.worldBackWidth,g_config.worldBackHeight,supported,standard,desktop.Width,desktop.Height);
        }
        HookVtable(d3d, 16, (void*)HookedCreateDevice, (void**)&g_createDevice);
        Log("Direct3DCreate9: CreateDevice hooked");
    }
    return d3d;
}

extern "C" HRESULT WINAPI Direct3DCreate9Ex(UINT sdk, IDirect3D9Ex** out) {
    if (!LoadReal() || !g_create9ex) return E_NOTIMPL;
    StartOnce();
    return g_create9ex(sdk, out);
}

// Debug markers d3dx and some tools look for; DXVK implements them as no-ops
extern "C" int WINAPI D3DPERF_BeginEvent(DWORD, LPCWSTR) { return 0; }
extern "C" int WINAPI D3DPERF_EndEvent() { return 0; }
extern "C" DWORD WINAPI D3DPERF_GetStatus() { return 0; }
extern "C" BOOL WINAPI D3DPERF_QueryRepeatFrame() { return FALSE; }
extern "C" void WINAPI D3DPERF_SetMarker(DWORD, LPCWSTR) {}
extern "C" void WINAPI D3DPERF_SetOptions(DWORD) {}
extern "C" void WINAPI D3DPERF_SetRegion(DWORD, LPCWSTR) {}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        char dir[MAX_PATH];
        GetModuleFileNameA(instance, dir, MAX_PATH);
        char* slash = strrchr(dir, '\\');
        if (slash) *slash = 0;
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%s\\BioshockVR.log", dir);
        LogInit(path);
        devharness::Init(dir);
        Log(pcvr::Enabled() ? "BioShock 2007 PCVR (adapted F.E.A.R. VR host)" : "BioShock VR mod build 86: native actor yaw isolates VR view from shotgun camera recoil (WinlatorXR)");
        snprintf(path, sizeof(path), "%s\\BioshockVR.ini", dir);
        LoadConfig(path);
        // DllMain runs on the main thread (d3d9 is a static import of the exe)
        // Sampling suspends the game thread and can interfere with MinHook
        // while Wine creates input devices. Keep it out of normal builds.
#ifdef BIOSHOCK_STARTUP_DIAGNOSTICS
        stallsampler::Start();
#endif
        crash::Install();
        if (g_config.enabled) {
            // До того, как игра создаст мышь DirectInput
            MH_Initialize();
            mouse::Install();
            mouse::SetScale(g_config.menuMouseScale);
        }
    }
    return TRUE;
}
