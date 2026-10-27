#include "pcvr.h"
#include "log.h"
#include "config.h"
#include <windows.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#ifdef BIOSHOCK_PCVR
#include "protocol_utils.h"
#include "ipc_names.h"

namespace pcvr {
namespace {
FearVrSharedHeader* g_shared = nullptr;
HANDLE g_mapping = nullptr, g_ready = nullptr, g_consumed = nullptr, g_request = nullptr;
uint64_t g_session = 0, g_generation = 1, g_hapticId = 0;
unsigned g_w = 0, g_h = 0;
bool g_cpu = false, g_failed = false;
IDirect3D9Ex* g_auxD3D = nullptr;
IDirect3DDevice9Ex* g_aux = nullptr;
HWND g_auxWindow = nullptr;
IDirect3DSurface9* g_capture = nullptr;
IDirect3DSurface9* g_readback = nullptr;
IDirect3DSurface9* g_upload = nullptr;
struct Pair {
    IDirect3DTexture9* tex[2] = {};
    IDirect3DSurface9* surf[2] = {};
    IDirect3DQuery9* complete = nullptr;
    bool pending = false;
};
Pair g_pair[3];
volatile LONG* A32(uint32_t& value) { return reinterpret_cast<volatile LONG*>(&value); }
volatile LONG64* A64(uint64_t& value) { return reinterpret_cast<volatile LONG64*>(&value); }
uint64_t Read64(uint64_t& value) { return InterlockedCompareExchange64(A64(value), 0, 0); }
template<class T> bool Snapshot(uint64_t& sequence, const T& source, T& result) {
    for (int attempt = 0; attempt < 8; ++attempt) {
        uint64_t before = Read64(sequence);
        if (!before || (before & 1)) continue;
        MemoryBarrier();
        T copy = source;
        MemoryBarrier();
        if (before == Read64(sequence)) { result = copy; return true; }
    }
    return false;
}
void Flags(uint32_t set, uint32_t clear = 0) {
    if (!g_shared) return;
    InterlockedAnd(A32(g_shared->bridgeFlags), ~clear);
    InterlockedOr(A32(g_shared->bridgeFlags), set);
}
void DropTextures() {
    for (unsigned i = 0; i < 3; ++i) {
        Pair& pair = g_pair[i];
        for (unsigned eye = 0; eye < 2; ++eye) {
            if (pair.surf[eye]) pair.surf[eye]->Release();
            if (pair.tex[eye]) pair.tex[eye]->Release();
            if (g_shared) {
                g_shared->slot[eye][i].sharedHandle = 0;
                InterlockedExchange(A32(g_shared->slot[eye][i].state), FEARVR_SLOT_EMPTY);
            }
        }
        if (pair.complete) pair.complete->Release();
        pair = Pair{};
    }
    if (g_capture) g_capture->Release();
    if (g_readback) g_readback->Release();
    if (g_upload) g_upload->Release();
    g_capture = g_readback = g_upload = nullptr;
    g_w = g_h = 0;
    ++g_generation;
}
bool AnyConsuming() {
    if (!g_shared) return false;
    for (auto& eyes : g_shared->slot)
        for (auto& slot : eyes)
            if (InterlockedCompareExchange(A32(slot.state), 0, 0) == FEARVR_SLOT_CONSUMING) return true;
    return false;
}
void FinishCopies() {
    for (unsigned i = 0; i < 3; ++i) {
        Pair& pair = g_pair[i];
        if (!pair.pending) continue;
        HRESULT hr = pair.complete->GetData(nullptr, 0, D3DGETDATA_FLUSH);
        if (hr == S_FALSE) continue;
        pair.pending = false;
        MemoryBarrier();
        for (unsigned eye = 0; eye < 2; ++eye)
            InterlockedExchange(A32(g_shared->slot[eye][i].state), SUCCEEDED(hr) ? FEARVR_SLOT_READY : FEARVR_SLOT_EMPTY);
        if (SUCCEEDED(hr)) SetEvent(g_ready);
    }
}
bool AuxDevice(IDirect3DDevice9* device) {
    if (g_aux) return true;
    wchar_t path[MAX_PATH];
    GetSystemDirectoryW(path, MAX_PATH);
    wcscat_s(path, L"\\d3d9.dll");
    HMODULE module = LoadLibraryW(path);
    using Create = HRESULT(WINAPI*)(UINT, IDirect3D9Ex**);
    auto create = module ? reinterpret_cast<Create>(GetProcAddress(module, "Direct3DCreate9Ex")) : nullptr;
    if (!create || FAILED(create(D3D_SDK_VERSION, &g_auxD3D))) return false;
    D3DDEVICE_CREATION_PARAMETERS cp{};
    device->GetCreationParameters(&cp);
    UINT adapter = cp.AdapterOrdinal;
    if (g_shared->hostAdapterLuid) {
        for (UINT i = 0; i < g_auxD3D->GetAdapterCount(); ++i) {
            LUID luid{};
            if (SUCCEEDED(g_auxD3D->GetAdapterLUID(i, &luid)) &&
                fearvr::PackLuid(static_cast<uint32_t>(luid.HighPart), luid.LowPart) == Read64(g_shared->hostAdapterLuid)) { adapter = i; break; }
        }
    }
    LUID luid{};
    if (FAILED(g_auxD3D->GetAdapterLUID(adapter, &luid))) return false;
    InterlockedExchange64(A64(g_shared->gameAdapterLuid), fearvr::PackLuid(static_cast<uint32_t>(luid.HighPart), luid.LowPart));
    g_auxWindow = CreateWindowExW(0, L"STATIC", L"BioShock VR transfer", WS_OVERLAPPED, 0, 0, 32, 32, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = g_auxWindow;
    pp.BackBufferWidth = pp.BackBufferHeight = 32;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    HRESULT hr = g_auxD3D->CreateDeviceEx(adapter, D3DDEVTYPE_HAL, g_auxWindow,
        D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE | D3DCREATE_MULTITHREADED, &pp, nullptr, &g_aux);
    Log("PCVR: compatibility transfer device 0x%08lx", hr);
    return SUCCEEDED(hr);
}
bool Allocate(IDirect3DDevice9* game, unsigned w, unsigned h, bool cpu) {
    IDirect3DDevice9* transfer = cpu ? static_cast<IDirect3DDevice9*>(g_aux) : game;
    for (unsigned i = 0; i < 3; ++i) {
        for (unsigned eye = 0; eye < 2; ++eye) {
            HANDLE handle = nullptr;
            HRESULT hr = transfer->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_pair[i].tex[eye], &handle);
            if (FAILED(hr) || !handle || FAILED(g_pair[i].tex[eye]->GetSurfaceLevel(0, &g_pair[i].surf[eye]))) return false;
            FearVrSlot& slot = g_shared->slot[eye][i];
            slot.sharedHandle = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(handle));
            slot.width = w; slot.height = h; slot.format = FEARVR_FMT_B8G8R8A8; slot.generation = g_generation;
        }
        if (FAILED(transfer->CreateQuery(D3DQUERYTYPE_EVENT, &g_pair[i].complete))) return false;
    }
    if (cpu && (FAILED(game->CreateRenderTarget(w, h, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &g_capture, nullptr)) ||
        FAILED(game->CreateOffscreenPlainSurface(w, h, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &g_readback, nullptr)) ||
        FAILED(transfer->CreateOffscreenPlainSurface(w, h, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &g_upload, nullptr)))) return false;
    g_w = w; g_h = h; g_cpu = cpu;
    Flags(FEARVR_BF_SHARED_SUPPORTED | (cpu ? FEARVR_BF_CPU_FALLBACK : 0), FEARVR_BF_DEVICE_LOST | (cpu ? 0 : FEARVR_BF_CPU_FALLBACK));
    Log("PCVR: %ux%u per eye, %s transfer, triple buffer generation %llu", w, h, cpu ? "D3D9 compatibility" : "GPU shared", g_generation);
    return true;
}
}

bool Enabled() { return true; }
bool Start() {
    if (g_shared) return true;
    char id[32] = {};
    GetEnvironmentVariableA("BIOSHOCK_PCVR_SESSION", id, sizeof(id));
    g_session = _strtoui64(id, nullptr, 0);
    if (!g_session) { Log("PCVR: launch with RUN_BIOSHOCK_STEAMVR.bat (session ID is missing)"); return false; }
    auto name = fearvr::MakeIpcObjectName(g_session, L"Mapping");
    g_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(FearVrSharedHeader), name.c_str());
    if (!g_mapping || GetLastError() == ERROR_ALREADY_EXISTS) { Log("PCVR: IPC session collision or mapping creation failure"); return false; }
    g_shared = static_cast<FearVrSharedHeader*>(MapViewOfFile(g_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(FearVrSharedHeader)));
    if (!g_shared) return false;
    fearvr::InitializeProtocolHeader(*g_shared);
    g_ready = CreateEventW(nullptr, FALSE, FALSE, fearvr::MakeIpcObjectName(g_session, L"FrameReady").c_str());
    g_consumed = CreateEventW(nullptr, FALSE, FALSE, fearvr::MakeIpcObjectName(g_session, L"SlotConsumed").c_str());
    g_request = CreateEventW(nullptr, FALSE, FALSE, fearvr::MakeIpcObjectName(g_session, L"RenderRequest").c_str());
    g_shared->gameProcessId = GetCurrentProcessId();
    Flags(FEARVR_BF_GAME_READY);
    Log("PCVR: F.E.A.R. VR protocol %u, session 0x%016llX", FEARVR_PROTOCOL_VERSION, g_session);
    return g_ready && g_consumed && g_request;
}

xrapi::State Get() {
    xrapi::State s{};
    if (!g_shared) return s;
    static uint64_t lastFrame = 0;
    static DWORD tick = 0;
    static unsigned recenters = 0, generation = 0;
    FearVrRenderRequest r{};
    if (!Snapshot(g_shared->requestSequence, g_shared->request, r)) return s;
    if (lastFrame != r.frameId) { lastFrame = r.frameId; tick = GetTickCount(); }
    s.valid = (r.flags & FEARVR_RF_VALID) && GetTickCount() - tick < 300;
    if (!s.valid) return s;
    if (generation != r.recenterGeneration) { generation = r.recenterGeneration; ++recenters; }
    s.tick = tick; s.resumes = recenters; s.packets = static_cast<unsigned>(r.frameId);
    s.frameId = r.frameId; s.sync = r.frameId & 255;
    const auto& l = r.eye[0].pose; const auto& rr = r.eye[1].pose;
    s.hmdOrientation = {l.qx, l.qy, l.qz, l.qw};
    s.hmdPosition = {(l.px + rr.px)*.5f, (l.py + rr.py)*.5f, (l.pz + rr.pz)*.5f};
    float dx = l.px - rr.px, dy = l.py - rr.py, dz = l.pz - rr.pz;
    s.ipd = sqrtf(dx*dx + dy*dy + dz*dz);
    // The game uses a symmetric projection. Cover both runtime eye frusta;
    // the host crops it to each eye's asymmetric projection before submission.
    float tx = 0, ty = 0;
    for (const auto& eye : r.eye) {
        tx = fmaxf(tx, fmaxf(fabsf(tanf(eye.fov.angleLeft)), fabsf(tanf(eye.fov.angleRight))));
        ty = fmaxf(ty, fmaxf(fabsf(tanf(eye.fov.angleDown)), fabsf(tanf(eye.fov.angleUp))));
    }
    s.fovX = 2*atanf(tx)*180/vr::kPi; s.fovY = 2*atanf(fmaxf(tx,ty))*180/vr::kPi;
    if (r.flags & FEARVR_RF_FLOOR_SPACE) s.altitude = s.hmdPosition.y;
    s.left.orientation.w = s.left.grip.w = s.right.orientation.w = s.right.grip.w = 1;
    FearVrInputState in{};
    if (Snapshot(g_shared->inputSequence, g_shared->input, in) &&
        (in.flags & (FEARVR_IF_VALID | FEARVR_IF_FOCUSED)) == (FEARVR_IF_VALID | FEARVR_IF_FOCUSED)) {
        s.left.stickX = in.moveX; s.left.stickY = in.moveY;
        s.right.stickX = in.turnX; s.right.stickY = in.turnY;
        xrapi::Hand* hands[] = {&s.left, &s.right};
        for (unsigned i = 0; i < 2; ++i) {
            const auto& aim = in.handAimPose[i]; const auto& grip = in.handGripPose[i];
            if (in.aimPoseValidHands & (1u << i)) hands[i]->orientation = {aim.qx, aim.qy, aim.qz, aim.qw};
            if (in.gripPoseValidHands & (1u << i)) { hands[i]->grip = {grip.qx, grip.qy, grip.qz, grip.qw}; hands[i]->position = {grip.px, grip.py, grip.pz}; }
        }
        s.buttons[xrapi::L_GRIP] = in.squeeze[0] > .5f; s.buttons[xrapi::R_GRIP] = in.squeeze[1] > .5f;
        s.buttons[xrapi::L_TRIGGER] = in.trigger[0] > .5f; s.buttons[xrapi::R_TRIGGER] = in.trigger[1] > .5f;
        const unsigned mask[] = {FEARVR_IB_LEFT_PRIMARY, FEARVR_IB_LEFT_SECONDARY, FEARVR_IB_LEFT_MENU, FEARVR_IB_LEFT_STICK, FEARVR_IB_RIGHT_PRIMARY, FEARVR_IB_RIGHT_SECONDARY, FEARVR_IB_RIGHT_STICK};
        const int button[] = {xrapi::L_X, xrapi::L_Y, xrapi::L_MENU, xrapi::L_THUMBSTICK_PRESS, xrapi::R_A, xrapi::R_B, xrapi::R_THUMBSTICK_PRESS};
        for (unsigned i = 0; i < 7; ++i) s.buttons[button[i]] = (in.buttons & mask[i]) != 0;
        const int base[] = {xrapi::L_THUMBSTICK_LEFT, xrapi::R_THUMBSTICK_LEFT};
        for (unsigned i = 0; i < 2; ++i) {
            s.buttons[base[i]] = hands[i]->stickX < -.5f; s.buttons[base[i]+1] = hands[i]->stickX > .5f;
            s.buttons[base[i]+2] = hands[i]->stickY > .5f; s.buttons[base[i]+3] = hands[i]->stickY < -.5f;
        }
    }
    return s;
}

void Mode(bool stereo, float left, float right) {
    // Image mode is published with a completed image, not a future pose.
    (void)stereo;
    if (!g_shared || (left <= 0 && right <= 0)) return;
    FearVrHapticRequest h{};
    h.requestId = ++g_hapticId; h.durationNs = 30000000;
    h.amplitude = fminf(1, fmaxf(left, right)); h.flags = FEARVR_HF_VALID;
    h.handMask = (left > 0 ? FEARVR_HAND_MASK_LEFT : 0) | (right > 0 ? FEARVR_HAND_MASK_RIGHT : 0);
    InterlockedIncrement64(A64(g_shared->hapticSequence));
    g_shared->haptic = h; MemoryBarrier();
    InterlockedIncrement64(A64(g_shared->hapticSequence));
}

void DeviceCreated(IDirect3DDevice9* device) {
    if (!g_shared) return;
    // Obtain an adapter LUID without replacing the game's D3D9 device. Legacy
    // managed textures remain supported. The compatibility uploader is used
    // only when this device cannot create shareable textures.
    D3DDEVICE_CREATION_PARAMETERS cp{}; device->GetCreationParameters(&cp);
    wchar_t path[MAX_PATH]; GetSystemDirectoryW(path, MAX_PATH); wcscat_s(path, L"\\d3d9.dll");
    HMODULE dll = LoadLibraryW(path);
    using Create = HRESULT(WINAPI*)(UINT, IDirect3D9Ex**);
    auto create = dll ? reinterpret_cast<Create>(GetProcAddress(dll, "Direct3DCreate9Ex")) : nullptr;
    IDirect3D9Ex* d3d = nullptr;
    if (create && SUCCEEDED(create(D3D_SDK_VERSION, &d3d))) {
        LUID luid{};
        if (SUCCEEDED(d3d->GetAdapterLUID(cp.AdapterOrdinal, &luid)))
            InterlockedExchange64(A64(g_shared->gameAdapterLuid), fearvr::PackLuid(static_cast<uint32_t>(luid.HighPart), luid.LowPart));
        d3d->Release();
    }
    g_failed = false;
}

void Submit(IDirect3DDevice9* device, bool stereo, unsigned long long frameId) {
    if (!g_shared || g_failed) return;
    InterlockedIncrement64(A64(g_shared->gameHeartbeat));
    FinishCopies();
    if (!(g_shared->bridgeFlags & FEARVR_BF_HOST_READY)) return;
    if (stereo && !frameId) return;
    IDirect3DSurface9* back = nullptr;
    if (FAILED(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back))) return;
    D3DSURFACE_DESC desc{}; back->GetDesc(&desc);
    unsigned w = stereo ? desc.Width/2 : desc.Width, h = desc.Height;
    if (w > 1536) { h = h*1536/w; w = 1536; }
    if (g_w != w || g_h != h) {
        if (AnyConsuming()) { back->Release(); return; }
        // Do not release surfaces while their GPU copy is still pending.
        for (const auto& pair : g_pair) if (pair.pending) { back->Release(); return; }
        DropTextures();
        bool allocated = !g_aux && Allocate(device, w, h, false);
        if (!allocated) {
            DropTextures();
            allocated = AuxDevice(device) && Allocate(device,w,h,true);
        }
        if (!allocated) { DropTextures(); g_failed = true; Log("PCVR: shared frame transfer unavailable; desktop rendering continues"); back->Release(); return; }
    }
    unsigned index = 3;
    for (unsigned i = 0; i < 3; ++i) {
        if (InterlockedCompareExchange(A32(g_shared->slot[0][i].state),FEARVR_SLOT_WRITING,FEARVR_SLOT_EMPTY) != FEARVR_SLOT_EMPTY) continue;
        if (InterlockedCompareExchange(A32(g_shared->slot[1][i].state),FEARVR_SLOT_WRITING,FEARVR_SLOT_EMPTY) == FEARVR_SLOT_EMPTY) { index=i; break; }
        InterlockedExchange(A32(g_shared->slot[0][i].state),FEARVR_SLOT_EMPTY);
    }
    if (index == 3) { back->Release(); return; } // never overwrite a frame the host is reading
    Pair& pair = g_pair[index];
    ++g_generation;
    HRESULT hr = S_OK;
    for (unsigned eye = 0; eye < 2 && SUCCEEDED(hr); ++eye) {
        RECT source = {stereo ? static_cast<LONG>(eye*desc.Width/2) : 0,0,
            stereo ? static_cast<LONG>((eye+1)*desc.Width/2) : static_cast<LONG>(desc.Width),static_cast<LONG>(desc.Height)};
        if (!g_cpu) hr = device->StretchRect(back,&source,pair.surf[eye],nullptr,D3DTEXF_LINEAR);
        else {
            hr = device->StretchRect(back,&source,g_capture,nullptr,D3DTEXF_LINEAR);
            if (SUCCEEDED(hr)) hr = device->GetRenderTargetData(g_capture,g_readback);
            D3DLOCKED_RECT read{}, write{};
            if (SUCCEEDED(hr)) hr = g_readback->LockRect(&read,nullptr,D3DLOCK_READONLY);
            if (SUCCEEDED(hr)) {
                hr = g_upload->LockRect(&write,nullptr,0);
                if (SUCCEEDED(hr)) {
                    for (unsigned y=0;y<h;++y) memcpy(static_cast<char*>(write.pBits)+y*write.Pitch,static_cast<char*>(read.pBits)+y*read.Pitch,w*4);
                    g_upload->UnlockRect();
                }
                g_readback->UnlockRect();
            }
            if (SUCCEEDED(hr)) hr = g_aux->UpdateSurface(g_upload,nullptr,pair.surf[eye],nullptr);
        }
        FearVrSlot& slot = g_shared->slot[eye][index];
        slot.frameId = frameId ? frameId : Read64(g_shared->request.frameId);
        slot.generation = g_generation; slot.camera = {};
    }
    back->Release();
    if (SUCCEEDED(hr)) hr = pair.complete->Issue(D3DISSUE_END);
    if (SUCCEEDED(hr)) {
        pair.pending = true;
        Flags(stereo ? FEARVR_BF_STEREO_ACTIVE : 0, stereo ? 0 : FEARVR_BF_STEREO_ACTIVE);
        FinishCopies();
    } else {
        for (unsigned eye=0;eye<2;++eye) InterlockedExchange(A32(g_shared->slot[eye][index].state),FEARVR_SLOT_EMPTY);
        static unsigned errors=0;
        if (errors++ < 5) Log("PCVR: frame copy failed 0x%08lx",hr);
    }
}

void Release() {
    if (!g_shared) return;
    Flags(FEARVR_BF_DEVICE_LOST, FEARVR_BF_STEREO_ACTIVE);
    DWORD until = GetTickCount()+250;
    while (AnyConsuming() && static_cast<LONG>(until-GetTickCount())>0) Sleep(1);
    // The host holds its own COM references if a reset interrupts a copy.
    DropTextures();
    if (g_aux) g_aux->Release(); if (g_auxD3D) g_auxD3D->Release();
    g_aux = nullptr; g_auxD3D = nullptr;
    if (g_auxWindow) DestroyWindow(g_auxWindow); g_auxWindow=nullptr;
    g_failed=false;
}
}
#else
namespace pcvr {
bool Enabled() { return false; }
bool Start() { return false; }
xrapi::State Get() { return {}; }
void Mode(bool,float,float) {}
void DeviceCreated(IDirect3DDevice9*) {}
void Submit(IDirect3DDevice9*,bool,unsigned long long) {}
void Release() {}
}
#endif
