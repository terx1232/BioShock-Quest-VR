// Отладка без человека в шлеме: команды из файла vrcmd.txt рядом с игрой.
//
//   shot                 - сохранить кадр, уходящий в шлем, в vrshot.bmp
//   press <кнопка> <N>   - держать кнопку контроллера N кадров (R_TRIGGER, R_A, L_MENU, ...)
//   stick <L|R> x y <N>  - отклонить стик на N кадров
//   point u v            - в кинотеатре навести луч на точку экрана меню (0..1), "point off" - снять
//   wait <N>             - следующие команды через N кадров
//   fpscap <N>           - предел кадров игры в VR (0 - без предела), gputiming <0|1> - замер видеочипа
//   sync <0|1>           - пиксель синхронизации, fov <град> - поле зрения кадра по вертикали
//   rigid <0|1>          - предмет в руке жёстко в контроллере / по анимации рук, weaponpitch <град>
//
// Файл читается раз в 15 кадров и удаляется; результат пишется в BioshockVR.log.
#include "devharness.h"

#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#include <deque>
#include <string>

#include "config.h"
#include "hud.h"
#include "mouse.h"
#include "log.h"

namespace devharness {
namespace {

char g_dir[MAX_PATH];
unsigned g_frame = 0;
std::deque<std::string> g_queue;
unsigned g_waitUntil = 0;
bool g_shot = false;
bool g_hudShot = false;

struct Held {
    int button;
    unsigned until;
};
std::deque<Held> g_held;
float g_stick[2][2] = {};
unsigned g_stickUntil[2] = {};
bool g_point = false;
float g_pointU = 0, g_pointV = 0;

const char* kButtons[] = {"L_GRIP", "L_MENU", "L_THUMBSTICK_PRESS", "L_THUMBSTICK_LEFT", "L_THUMBSTICK_RIGHT",
                          "L_THUMBSTICK_UP", "L_THUMBSTICK_DOWN", "L_TRIGGER", "L_X", "L_Y",
                          "R_A", "R_B", "R_GRIP", "R_THUMBSTICK_PRESS", "R_THUMBSTICK_LEFT", "R_THUMBSTICK_RIGHT",
                          "R_THUMBSTICK_UP", "R_THUMBSTICK_DOWN", "R_TRIGGER"};

void ReadFile() {
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\vrcmd.txt", g_dir);
    FILE* f = fopen(path, "rb");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char* e = line + strlen(line);
        while (e > line && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ')) *--e = 0;
        if (*line) g_queue.push_back(line);
    }
    fclose(f);
    DeleteFileA(path);
}

// Поиск координат курсора меню в объекте окна игры (UWindowsViewport, vtable 0x10fd71f4):
// "vpscan x y" ищет в объектах окна пары float/int, близкие к (x, y), и пишет смещения.
void ViewportScan(float x, float y) {
    const DWORD kVtables[] = {0x10fd71f4, 0x10fd6e94};  // UWindowsViewport, UViewport
    MEMORY_BASIC_INFORMATION mbi;
    BYTE* addr = (BYTE*)0x10000;
    int objects = 0;
    while ((UINT_PTR)addr < 0x7FFF0000 && VirtualQuery(addr, &mbi, sizeof(mbi))) {
        bool rw = mbi.State == MEM_COMMIT && (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) &&
                  !(mbi.Protect & PAGE_GUARD);
        if (rw) {
            DWORD* p = (DWORD*)mbi.BaseAddress;
            size_t n = mbi.RegionSize / 4;
            for (size_t i = 0; i < n; i++) {
                if (p[i] != kVtables[0] && p[i] != kVtables[1]) continue;
                BYTE* obj = (BYTE*)(p + i);
                objects++;
                Log("Dev: viewport object at %p (vtable %08lx)", obj, p[i]);
                // поля объекта: ищем пары рядом с (x, y)
                size_t avail = (BYTE*)mbi.BaseAddress + mbi.RegionSize - obj;
                for (size_t off = 0; off + 8 <= 0x3000 && off + 8 <= avail; off += 4) {
                    float fx = *(float*)(obj + off), fy = *(float*)(obj + off + 4);
                    int ix = *(int*)(obj + off), iy = *(int*)(obj + off + 4);
                    if (fabsf(fx - x) < 8 && fabsf(fy - y) < 8)
                        Log("Dev:   float pair at +0x%zx = %.1f %.1f", off, fx, fy);
                    if (abs(ix - (int)x) < 8 && abs(iy - (int)y) < 8)
                        Log("Dev:   int pair at +0x%zx = %d %d", off, ix, iy);
                }
            }
        }
        addr = (BYTE*)mbi.BaseAddress + mbi.RegionSize;
    }
    Log("Dev: vpscan done, %d viewport objects", objects);
}

void Run(const std::string& cmd) {
    char name[64] = {};
    float a = 0, b = 0;
    int n = 0;
    unsigned address=0;
    if(sscanf(cmd.c_str(),"peek %x %d",&address,&n)==2){
        if(n<1||n>64){Log("Dev: peek count must be 1..64 words");return;}
        DWORD words[64]={};SIZE_T got=0;
        if(!ReadProcessMemory(GetCurrentProcess(),(void*)(uintptr_t)address,words,n*4,&got)){
            Log("Dev: peek %08x failed",address);return;
        }
        for(int i=0;i<(int)(got/4);++i){float value;memcpy(&value,&words[i],4);
            Log("Dev: %08x = %08lx int=%ld float=%.3f",address+i*4,words[i],(LONG)words[i],value);}
        return;
    }
    // key <vk> - нажать клавишу в окне игры (например 13 = Enter)
    if (cmd.compare(0, 4, "key ") == 0) {
        int vk = atoi(cmd.c_str() + 4);
        HWND w = mouse::GameWindow();
        UINT scan = MapVirtualKeyA(vk, 0);
        if (w) {
            PostMessageA(w, WM_KEYDOWN, vk, 1 | (scan << 16));
            PostMessageA(w, WM_KEYUP, vk, 1 | (scan << 16) | 0xC0000000);
        }
        Log("Dev: key %d -> window %p", vk, w);
        return;
    }
    if (cmd == "drawdump") {
        hud::DumpFrame();
        Log("Dev: %s", cmd.c_str());
        return;
    }
    if (cmd == "hudshot") {
        g_hudShot = true;
        Log("Dev: %s", cmd.c_str());
        return;
    }
    if (cmd == "shot") {
        g_shot = true;
    } else if (sscanf(cmd.c_str(), "press %63s %d", name, &n) == 2) {
        for (int i = 0; i < 19; i++)
            if (_stricmp(name, kButtons[i]) == 0) g_held.push_back({i, g_frame + n});
    } else if (sscanf(cmd.c_str(), "stick %63s %f %f %d", name, &a, &b, &n) == 4) {
        int s = (name[0] == 'L' || name[0] == 'l') ? 0 : 1;
        g_stick[s][0] = a;
        g_stick[s][1] = b;
        g_stickUntil[s] = g_frame + n;
    } else if (cmd == "point off") {
        g_point = false;
    } else if (sscanf(cmd.c_str(), "point %f %f", &a, &b) == 2) {
        g_point = true;
        g_pointU = a;
        g_pointV = b;
    } else if (sscanf(cmd.c_str(), "vpscan %f %f", &a, &b) == 2) {
        ViewportScan(a, b);
    } else if (sscanf(cmd.c_str(), "wait %d", &n) == 1) {
        g_waitUntil = g_frame + n;
    } else if (sscanf(cmd.c_str(), "fpscap %f", &a) == 1) {
        g_config.gameFpsCap = a;
    } else if (sscanf(cmd.c_str(), "gputiming %d", &n) == 1) {
        g_config.gpuTiming = n != 0;
    } else if (sscanf(cmd.c_str(), "cputiming %d", &n) == 1) {
        g_config.cpuTiming = n != 0;
    } else if (sscanf(cmd.c_str(), "sync %d", &n) == 1) {
        g_config.syncPixel = n != 0;
    } else if (sscanf(cmd.c_str(), "fov %f", &a) == 1) {
        if (a >= 60 && a <= 130) g_config.headsetFov = a;
    } else if (sscanf(cmd.c_str(), "rigid %d", &n) == 1) {
        g_config.rigidWeapon = n != 0;
    } else if (sscanf(cmd.c_str(), "weaponpitch %f", &a) == 1) {
        g_config.weaponPitch = a;
    } else {
        Log("Dev: unknown command '%s'", cmd.c_str());
        return;
    }
    Log("Dev: %s", cmd.c_str());
}

void SaveBmp(IDirect3DDevice9* d, const char* name = "vrshot.bmp", IDirect3DSurface9* source = nullptr) {
    IDirect3DSurface9* back = source;
    if (back) back->AddRef();
    else if (FAILED(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back))) return;
    D3DSURFACE_DESC desc;
    back->GetDesc(&desc);
    IDirect3DSurface9* sys = nullptr;
    if (SUCCEEDED(d->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &sys, nullptr)) &&
        SUCCEEDED(d->GetRenderTargetData(back, sys))) {
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "%s\\%s", g_dir, name);
            FILE* f = fopen(path, "wb");
            if (f) {
                BITMAPFILEHEADER fh = {};
                BITMAPINFOHEADER ih = {};
                ih.biSize = sizeof(ih);
                ih.biWidth = desc.Width;
                ih.biHeight = -(LONG)desc.Height;  // сверху вниз
                ih.biPlanes = 1;
                ih.biBitCount = 32;
                fh.bfType = 0x4D42;
                fh.bfOffBits = sizeof(fh) + sizeof(ih);
                fh.bfSize = fh.bfOffBits + desc.Width * desc.Height * 4;
                fwrite(&fh, sizeof(fh), 1, f);
                fwrite(&ih, sizeof(ih), 1, f);
                for (UINT y = 0; y < desc.Height; y++) fwrite((BYTE*)lr.pBits + y * lr.Pitch, 4, desc.Width, f);
                fclose(f);
                Log("Dev: frame saved to %s (%ux%u)", name, desc.Width, desc.Height);
            }
            sys->UnlockRect();
        }
    }
    if (sys) sys->Release();
    back->Release();
}

}  // namespace

void Init(const char* gameDir) {
    snprintf(g_dir,sizeof(g_dir),"%s",gameDir);
    if(!g_config.devHarness){
        char path[MAX_PATH];snprintf(path,sizeof(path),"%s\\vrcmd.txt",g_dir);DeleteFileA(path);
        Log("Dev: scripted input disabled");
    }
}

void BeforePresent(IDirect3DDevice9* device) {
    if(!g_config.devHarness)return;
    g_frame++;
    if ((g_frame % 15) == 0) ReadFile();
    while (!g_queue.empty() && g_frame >= g_waitUntil) {
        std::string c = g_queue.front();
        g_queue.pop_front();
        Run(c);
    }
    if (g_shot) {
        g_shot = false;
        SaveBmp(device);
    }
    if (g_hudShot && hud::Texture()) {
        g_hudShot = false;
        IDirect3DSurface9* s = nullptr;
        if (SUCCEEDED(hud::Texture()->GetSurfaceLevel(0, &s))) {
            SaveBmp(device, "hudshot.bmp", s);
            s->Release();
        }
    }
}

void Apply(bool buttons[], float& lx, float& ly, float& rx, float& ry) {
    if(!g_config.devHarness)return;
    while (!g_held.empty() && g_held.front().until < g_frame) g_held.pop_front();
    for (const Held& h : g_held) buttons[h.button] = true;
    if (g_frame < g_stickUntil[0]) { lx = g_stick[0][0]; ly = g_stick[0][1]; }
    if (g_frame < g_stickUntil[1]) { rx = g_stick[1][0]; ry = g_stick[1][1]; }
}

bool Point(float& u, float& v) {
    if (!g_config.devHarness || !g_point) return false;
    u = g_pointU;
    v = g_pointV;
    return true;
}

bool Button(int index){
    if(!g_config.devHarness)return false;
    for(const Held& held:g_held)if(held.button==index&&g_frame<=held.until)return true;
    return false;
}

}  // namespace devharness
