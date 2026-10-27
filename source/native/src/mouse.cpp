// Мышь игры в "кинотеатре": курсор там, куда указывает луч руки, курок - левая кнопка.
//
// BioShock читает мышь двумя путями: Windows-курсор (GetCursorPos) и DirectInput8 (смещения и
// кнопки мыши). Оба перехватываются: GetCursorPos возвращает точку луча, а мышь DirectInput
// получает смещение к этой точке и состояние курка. Вне "кинотеатра" всё идёт как обычно.
#include "mouse.h"

#include <windows.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#include "MinHook.h"
#include "input.h"
#include "devharness.h"
#include "xrapi.h"
#include "log.h"
#include "config.h"
#include "flashpointer.h"

#include <math.h>
#include <algorithm>
#include <stdlib.h>
#include <vector>

namespace mouse {
namespace {

volatile LONG g_active = 0;
volatile LONG g_x = 0, g_y = 0;        // целевая точка, координаты окна (клиентские)
volatile LONG g_button = 0;
HWND g_window = nullptr;
LONG g_sentX = 0, g_sentY = 0;          // куда по нашему счёту уже довели мышь DirectInput
bool g_buttonSent = false;
volatile LONG g_needClamp = 0;   // упереть курсор игры в угол, чтобы знать, где он
double g_pendX = 0, g_pendY = 0;  // смещение, которое ещё надо отдать игре
float g_scale = 1.47f;            // чувствительность мыши игры
void Advance();
bool ButtonDown();
unsigned g_stateCalls = 0, g_dataCalls = 0, g_cursorCalls = 0;
DWORD g_rayClampUntil=0;

typedef BOOL(WINAPI* GetCursorPosFn)(LPPOINT);
GetCursorPosFn g_getCursorPos = nullptr;
typedef SHORT(WINAPI* GetKeyFn)(int);
GetKeyFn g_getAsyncKeyState = nullptr, g_getKeyState = nullptr;
volatile LONG g_escape = 0;
// Игровой режим: кнопки и колесо от контроллеров
volatile LONG g_game = 0, g_gameLeft = 0, g_gameRight = 0, g_gameWheel = 0;
volatile LONG g_gamePitch=0,g_gamePitchStamp=0;
LONG TakeGamePitch(){LONG value=InterlockedExchange(&g_gamePitch,0);return GetTickCount()-DWORD(g_gamePitchStamp)<150?value:0;}
bool g_gameLeftSent = false, g_gameRightSent = false;
bool g_buttonPosted = false, g_escapePosted = false;
LONG g_postedX = -1, g_postedY = -1;

// Кнопки, которые мод "нажимает" в меню: левая кнопка мыши (курок) и Esc (B)
bool Forced(int vk) {
    if (!g_active) return false;
    if (vk == VK_LBUTTON) return ButtonDown();
    if (vk == VK_ESCAPE) return g_escape != 0;
    return false;
}

SHORT GameKey(int vk) {
    if (vk == VK_LBUTTON) return g_gameLeft ? (SHORT)0x8000 : 0;
    if (vk == VK_RBUTTON) return g_gameRight ? (SHORT)0x8000 : 0;
    return input::KeyHeld(vk) ? (SHORT)0x8000 : 0;
}

SHORT WINAPI HookedGetAsyncKeyState(int vk) {
    if (g_game && (vk == VK_LBUTTON || vk == VK_RBUTTON || input::KeyHeld(vk))) return GameKey(vk);
    static bool logged = false;
    if (g_active && vk == VK_LBUTTON && !logged) { logged = true; Log("Mouse: game polls GetAsyncKeyState(LBUTTON)"); }
    if (g_active && (vk == VK_LBUTTON || vk == VK_ESCAPE)) return Forced(vk) ? (SHORT)0x8001 : 0;
    return g_getAsyncKeyState(vk);
}

SHORT WINAPI HookedGetKeyState(int vk) {
    if (g_game && (vk == VK_LBUTTON || vk == VK_RBUTTON || input::KeyHeld(vk))) return GameKey(vk);
    static bool logged = false;
    if (g_active && vk == VK_LBUTTON && !logged) { logged = true; Log("Mouse: game polls GetKeyState(LBUTTON)"); }
    if (g_active && (vk == VK_LBUTTON || vk == VK_ESCAPE)) return Forced(vk) ? (SHORT)0x8000 : 0;
    return g_getKeyState(vk);
}
typedef HRESULT(WINAPI* DI8CreateFn)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
DI8CreateFn g_di8Create = nullptr;
typedef HRESULT(STDMETHODCALLTYPE* CreateDeviceFn)(IDirectInput8W*, REFGUID, LPDIRECTINPUTDEVICE8W*, LPUNKNOWN);
CreateDeviceFn g_createDevice = nullptr;
typedef HRESULT(STDMETHODCALLTYPE* GetStateFn)(IDirectInputDevice8W*, DWORD, LPVOID);
GetStateFn g_getState = nullptr;
typedef HRESULT(STDMETHODCALLTYPE* GetDataFn)(IDirectInputDevice8W*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
GetDataFn g_getData = nullptr;
IDirectInputDevice8W* g_mice[8] = {};
int g_miceCount = 0;
bool IsMouse(IDirectInputDevice8W* dev) {
    for (int i = 0; i < g_miceCount; i++)
        if (g_mice[i] == dev) return true;
    return false;
}

BOOL WINAPI HookedGetCursorPos(LPPOINT p) {
    if (!g_active || !p) return g_getCursorPos(p);
    if(xrapi::NativeCinemaReady())Advance();
    if ((g_cursorCalls++ % 600) == 0) Log("Mouse: game reads GetCursorPos (%u)", g_cursorCalls);
    POINT pt = {g_x, g_y};
    if (g_window) ClientToScreen(g_window, &pt);
    *p = pt;
    return TRUE;
}

HRESULT STDMETHODCALLTYPE HookedGetState(IDirectInputDevice8W* dev, DWORD size, LPVOID data) {
    HRESULT hr = g_getState(dev, size, data);
    if (IsMouse(dev) && !g_active && g_game && data && size >= sizeof(DIMOUSESTATE)) {
        // В игре мышь - только кнопки контроллеров: смотрит голова, указатель WinlatorXR не крутит камеру
        DIMOUSESTATE* m = (DIMOUSESTATE*)data;
        m->lX=0;m->lY=TakeGamePitch();
        m->lZ = InterlockedExchange(&g_gameWheel, 0) * WHEEL_DELTA;
        ZeroMemory(m->rgbButtons, sizeof(m->rgbButtons));
        m->rgbButtons[0] = g_gameLeft ? 0x80 : 0;
        m->rgbButtons[1] = g_gameRight ? 0x80 : 0;
        return DI_OK;
    }
    if (!IsMouse(dev) || !g_active || !data || size < sizeof(DIMOUSESTATE)) return hr;
    if ((g_stateCalls++ % 600) == 0) Log("Mouse: game reads DirectInput GetDeviceState (%u)", g_stateCalls);
    DIMOUSESTATE* m = (DIMOUSESTATE*)data;
    Advance();
    LONG ix = (LONG)g_pendX, iy = (LONG)g_pendY;
    m->lX = ix;
    m->lY = iy;
    m->lZ = 0;
    g_pendX -= ix;
    g_pendY -= iy;
    m->rgbButtons[0] = ButtonDown() ? 0x80 : 0;
    return DI_OK;
}

// Смещение, которое ещё надо отдать игре по каждой оси (игра умножает смещения мыши на свою
// чувствительность, поэтому делим на g_scale), и сколько уже отдано
// --- Поиск поля курсора меню в объекте окна игры ------------------------------------------
// Курсор меню игра ведёт сама по смещениям мыши (со своей чувствительностью и сглаживанием),
// поэтому точно поставить его смещениями нельзя. Мод находит поле, где игра хранит курсор:
// упирает его в угол, сдвигает на известные (40, 30) и ищет в объекте окна (UWindowsViewport)
// пару чисел, изменившуюся в этой пропорции. Дальше пишет туда координаты луча напрямую.
enum CalState { CAL_IDLE, CAL_CLAMP, CAL_PROBE1, CAL_SCAN, CAL_PROBE2, CAL_FILTER, CAL_MAXX, CAL_MAX, CAL_DONE, CAL_FAILED };
volatile LONG g_scanDone = 0;
float g_maxX = 0, g_maxY = 0;   // предел поля курсора (правый нижний угол меню)
LONG g_screenW = 0, g_screenH = 0;
CalState g_cal = CAL_IDLE;
bool ButtonDown(){return g_button&&(!xrapi::NativeCinemaReady()||(g_rayClampUntil&&(LONG)(GetTickCount()-g_rayClampUntil)>=0));}
DWORD g_calTime = 0;
struct Cand { BYTE* addr; bool isFloat; float v0, v1; };
std::vector<Cand> g_cands;
std::vector<Cand> g_fields;  // найденные поля курсора (игра может держать копии)

float Read(const Cand& c, int i) {
    DWORD v = 0;
    ReadProcessMemory(GetCurrentProcess(), c.addr + i * 4, &v, 4, nullptr);
    return c.isFloat ? *(float*)&v : (float)(int)v;
}

// Вся доступная на запись память игры: пары чисел в пропорции сдвига (4:3) и в пределах экрана
void ScanAll() {
    g_cands.clear();
    static BYTE chunk[65536 + 8];
    MEMORY_BASIC_INFORMATION mbi;
    BYTE* addr = (BYTE*)0x10000;
    while ((UINT_PTR)addr < 0x7FFF0000 && VirtualQuery(addr, &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE &&
            (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) &&
            !(mbi.Protect & PAGE_GUARD) && g_cands.size() < 3000000) {
            BYTE* base = (BYTE*)mbi.BaseAddress;
            for (size_t pos = 0; pos < mbi.RegionSize; pos += 65536) {
                size_t len = mbi.RegionSize - pos < 65536 ? mbi.RegionSize - pos : 65536;
                SIZE_T got = 0;
                // ReadProcessMemory вместо прямого чтения: память может освободиться прямо сейчас
                if (!ReadProcessMemory(GetCurrentProcess(), base + pos, chunk, len, &got) || got < 8) continue;
                for (size_t off = 0; off + 8 <= got; off += 4) {
                    float f0 = *(float*)(chunk + off), f1 = *(float*)(chunk + off + 4);
                    if (f0 >= 30 && f0 <= 125 && f1 >= 22 && f1 <= 95 && f0 / f1 > 0.6f && f0 / f1 < 3.0f &&
                        f0 == (float)(int)f0 && f1 == (float)(int)f1)
                        g_cands.push_back({base + pos + off, true, f0, f1});
                    int i0 = *(int*)(chunk + off), i1 = *(int*)(chunk + off + 4);
                    if (i0 >= 30 && i0 <= 125 && i1 >= 22 && i1 <= 95 && i0 * 10 > i1 * 6 && i0 < i1 * 3)
                        g_cands.push_back({base + pos + off, false, (float)i0, (float)i1});
                }
            }
        }
        addr = (BYTE*)mbi.BaseAddress + mbi.RegionSize;
    }
}

bool SafeRead(BYTE* a, void* out, size_t n) {
    SIZE_T got = 0;
    return ReadProcessMemory(GetCurrentProcess(), a, out, n, &got) && got == n;
}

bool Readable(BYTE* a) {
    MEMORY_BASIC_INFORMATION mbi;
    return VirtualQuery(a, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT &&
           (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE));
}

// Второй шаг: какие кандидаты ровно удвоились. Читаем память блоками по порядку адресов
DWORD WINAPI FilterThread(LPVOID) {
    std::vector<Cand> found;
    static BYTE chunk[65536 + 8];
    size_t i = 0;
    while (i < g_cands.size() && found.size() < 8) {
        BYTE* start = g_cands[i].addr;
        BYTE* page = (BYTE*)((UINT_PTR)start & ~(UINT_PTR)0xFFFF);
        SIZE_T got = 0;
        bool ok = ReadProcessMemory(GetCurrentProcess(), page, chunk, 65536 + 8, &got) ||
                  ReadProcessMemory(GetCurrentProcess(), page, chunk, 65536, &got);
        for (; i < g_cands.size() && g_cands[i].addr < page + 65536; i++) {
            const Cand& c = g_cands[i];
            size_t off = c.addr - page;
            if (!ok || off + 8 > got) continue;
            float a, b;
            if (c.isFloat) { a = *(float*)(chunk + off); b = *(float*)(chunk + off + 4); }
            else { a = (float)*(int*)(chunk + off); b = (float)*(int*)(chunk + off + 4); }
            if (fabsf(a - 2 * c.v0) < 0.12f * c.v0 + 1 && fabsf(b - 2 * c.v1) < 0.12f * c.v1 + 1)
                found.push_back({c.addr, c.isFloat, a, b});
        }
    }
    g_fields = found;
    InterlockedExchange(&g_scanDone, 1);
    return 0;
}

DWORD WINAPI ScanThread(LPVOID) {
    ScanAll();
    InterlockedExchange(&g_scanDone, 1);
    return 0;
}

void Calibrate() {
    DWORD now = GetTickCount();
    if (g_cal == CAL_CLAMP && now - g_calTime > 300) {
        g_pendX += 40;
        g_pendY += 30;
        g_cal = CAL_PROBE1;
        g_calTime = now;
    } else if (g_cal == CAL_PROBE1 && now - g_calTime > 300) {
        // Поиск по памяти - в фоне: под эмуляцией он долгий, игра не должна замирать
        g_scanDone = 0;
        g_cal = CAL_SCAN;
        CreateThread(nullptr, 0, ScanThread, nullptr, 0, nullptr);
    } else if (g_cal == CAL_SCAN && g_scanDone) {
        Log("Mouse: %zu cursor candidates after first step", g_cands.size());
        g_pendX += 40;
        g_pendY += 30;
        g_cal = CAL_PROBE2;
        g_calTime = GetTickCount();
    } else if (g_cal == CAL_PROBE2 && now - g_calTime > 300) {
        g_scanDone = 0;
        g_cal = CAL_FILTER;
        CreateThread(nullptr, 0, FilterThread, nullptr, 0, nullptr);
    } else if (g_cal == CAL_FILTER && g_scanDone) {
        for (const Cand& f : g_fields)
            Log("Mouse: cursor field %s at %p -> %.1f %.1f", f.isFloat ? "float" : "int", f.addr, f.v0, f.v1);
        if (!g_fields.empty()) g_scale = g_fields[0].v0 / 80.0f;
        g_cands.clear();
        g_cands.shrink_to_fit();
        g_calTime = now;
        if (g_fields.empty()) {
            g_cal = CAL_FAILED;
            Log("Mouse: cursor field not found, steering by mouse motion");
        } else {
            g_pendX += 30000;   // упор вправо, затем вниз: узнать предел координат меню
            g_cal = CAL_MAXX;
        }
        g_sentX = g_x;
        g_sentY = g_y;
    } else if (g_cal == CAL_MAXX && now - g_calTime > 500) {
        g_pendY += 30000;
        g_cal = CAL_MAX;
        g_calTime = now;
    } else if (g_cal == CAL_MAX && now - g_calTime > 500) {
        // Настоящий курсор упирается в край экрана: его предел соизмерим с экраном
        int best = -1;
        float bestX = 0;
        std::vector<Cand> cursors;
        for (size_t i = 0; i < g_fields.size(); i++) {
            if (!Readable(g_fields[i].addr)) continue;
            float mx = Read(g_fields[i], 0), my = Read(g_fields[i], 1);
            Log("Mouse: field %p range %.0f x %.0f", g_fields[i].addr, mx, my);
            if(mx>=200&&my>=150&&mx<=g_screenW*2&&my<=g_screenH*2){
                cursors.push_back({g_fields[i].addr,g_fields[i].isFloat,mx,my});
                if(mx>bestX){best=(int)cursors.size()-1;bestX=mx;}
            }
        }
        if (best < 0) {
            Log("Mouse: no field reaches the screen edge, steering by mouse motion");
            g_fields.clear();
            g_cal = CAL_FAILED;
            g_calTime = now;
            return;
        }
        Cand chosen = cursors[best];
        cursors[best]=cursors[0];cursors[0]=chosen;
        g_fields=cursors;
        g_maxX = Read(chosen, 0);
        g_maxY = Read(chosen, 1);
        Log("Mouse: menu cursor range %.0f x %.0f (screen %ld x %ld)", g_maxX, g_maxY, g_screenW, g_screenH);
        g_cal = CAL_DONE;
    }
}

void Advance() {
    bool native=xrapi::NativeCinemaReady();
    if(native){
        int x,y;bool down;
        if(xrapi::NativePointer(x,y,down)){g_x=x;g_y=y;g_button=down?1:0;}
        else g_button=0;
        float u,v;
        if(devharness::Point(u,v)){
            g_x=(LONG)(u*g_screenW);g_y=(LONG)(v*g_screenH);
            g_button=(devharness::Button(xrapi::R_TRIGGER)||devharness::Button(xrapi::L_TRIGGER))?1:0;
        }
        if(flashpointer::Ready()){
            flashpointer::Target(true,g_x,g_y,g_screenW,g_screenH);
            InterlockedExchange(&g_needClamp,0);
            g_rayClampUntil=GetTickCount();
            return;
        }
        if(InterlockedExchange(&g_needClamp,0)){
            // Wine recentres its physical pointer. Keep one software cursor,
            // synchronised once at the edge, then feed measured UI deltas.
            g_pendX=g_pendY=-30000;g_sentX=g_sentY=0;
            g_rayClampUntil=GetTickCount()+150;
            Log("Mouse: ray cursor synchronising, gain %.3f %.3f",g_config.menuMouseScaleX,g_config.menuMouseScaleY);
            return;
        }
        if((LONG)(GetTickCount()-g_rayClampUntil)<0)return;
        LONG tx=g_x,ty=g_y;
        if(tx<0)tx=0;if(ty<0)ty=0;
        if(tx>=g_screenW)tx=g_screenW-1;if(ty>=g_screenH)ty=g_screenH-1;
        g_pendX+=(tx-g_sentX)/g_config.menuMouseScaleX;
        g_pendY+=(ty-g_sentY)/g_config.menuMouseScaleY;
        g_sentX=tx;g_sentY=ty;
        return;
    }
    LONG tx = g_x, ty = g_y;
    static int attempts = 0;
    if (g_cal == CAL_FAILED && attempts < 6 && GetTickCount() - g_calTime > 4000) {
        g_cal = CAL_IDLE;               // меню могло ещё не реагировать (заставка) - пробуем снова
        g_needClamp = 1;
    }
    if (InterlockedExchange(&g_needClamp, 0) && g_cal != CAL_DONE) {
        if (g_cal == CAL_IDLE) attempts++;
        g_pendX = g_pendY = -30000;   // упираем курсор игры в левый верхний угол
        g_sentX = g_sentY = 0;
        if (g_cal == CAL_IDLE) {
            g_cal = CAL_CLAMP;
            g_calTime = GetTickCount();
        }
        return;
    }
    if (g_cal == CAL_CLAMP || g_cal == CAL_PROBE1 || g_cal == CAL_SCAN || g_cal == CAL_PROBE2 || g_cal == CAL_FILTER ||
        g_cal == CAL_MAXX || g_cal == CAL_MAX) {
        Calibrate();
        return;
    }
    if (g_cal == CAL_DONE) {
        // Обратная связь: поле показывает, где курсор игры сейчас; досылаем смещение до луча
        const Cand& f = g_fields[0];
        if (!Readable(f.addr)) {
            g_cal = CAL_IDLE;
            g_fields.clear();
            return;
        }
        float cx = Read(f, 0), cy = Read(f, 1);
        // После загрузки уровня память поля может уйти под другое - тогда ищем курсор заново
        float limX = g_maxX > 1 ? g_maxX : (float)g_screenW, limY = g_maxY > 1 ? g_maxY : (float)g_screenH;
        if (!(cx > -64 && cy > -64 && cx < limX + 64 && cy < limY + 64)) {
            Log("Mouse: cursor field lost (%.0f,%.0f), searching again", cx, cy);
            g_cal = CAL_IDLE;
            g_fields.clear();
            InterlockedExchange(&g_needClamp, 1);
            return;
        }
        static DWORD lastLog = 0;
        if (GetTickCount() - lastLog > 2000) {
            lastLog = GetTickCount();
            Log("Mouse: cursor %.0f,%.0f -> target %ld,%ld native=%d fields=%zu", cx, cy, tx, ty,native?1:0,g_fields.size());
        }
        // точка луча в координатах меню (у меню свой масштаб по осям)
        float fx = g_screenW > 0 && g_maxX > 1 ? tx * g_maxX / g_screenW : (float)tx;
        float fy = g_screenH > 0 && g_maxY > 1 ? ty * g_maxY / g_screenH : (float)ty;
        if(native){
            // Calibration identifies the software cursor actually moved by DI.
            // Write that field directly instead of steering it with relative deltas.
            for(const Cand& cursor:g_fields){
                float px=g_screenW>0?tx*cursor.v0/g_screenW:fx;
                float py=g_screenH>0?ty*cursor.v1/g_screenH:fy;
                DWORD values[2];
                if(cursor.isFloat){memcpy(&values[0],&px,4);memcpy(&values[1],&py,4);}
                else {values[0]=(DWORD)(int)px;values[1]=(DWORD)(int)py;}
                SIZE_T written=0;
                if(!WriteProcessMemory(GetCurrentProcess(),cursor.addr,values,sizeof(values),&written)||written!=sizeof(values)){
                    g_cal=CAL_IDLE;g_fields.clear();InterlockedExchange(&g_needClamp,1);return;
                }
            }
            g_pendX=g_pendY=0;g_sentX=tx;g_sentY=ty;
            return;
        }
        float ex = fx - cx, ey = fy - cy;
        // часть ошибки за раз: игра сглаживает, иначе курсор будет раскачиваться
        static DWORD lastCorrection = 0;
        if (fabsf(g_pendX) < 1 && fabsf(g_pendY) < 1 && GetTickCount() - lastCorrection > 33) {
            lastCorrection = GetTickCount();
            g_pendX = ex / g_scale * 0.6f;
            g_pendY = ey / g_scale * 0.6f;
        }
        g_sentX = tx;
        g_sentY = ty;
        return;
    }
    g_pendX += (tx - g_sentX) / g_scale;
    g_pendY += (ty - g_sentY) / g_scale;
    g_sentX = tx;
    g_sentY = ty;
}

HRESULT STDMETHODCALLTYPE HookedGetData(IDirectInputDevice8W* dev, DWORD size, LPDIDEVICEOBJECTDATA data,
                                        LPDWORD count, DWORD flags) {
    if (IsMouse(dev) && !g_active && g_game && count) {
        DWORD real = INFINITE;
        g_getData(dev, size, nullptr, &real, 0);  // настоящие события (указатель WinlatorXR) выбрасываем
        if (!data || *count == 0 || (flags & DIGDD_PEEK)) {
            *count = 0;
            return DI_OK;
        }
        DWORD n = 0, max = *count;
        auto push = [&](DWORD ofs, DWORD value) {
            DIDEVICEOBJECTDATA* e = (DIDEVICEOBJECTDATA*)((BYTE*)data + n * size);
            ZeroMemory(e, size);
            e->dwOfs = ofs;
            e->dwData = value;
            e->dwTimeStamp = GetTickCount();
            n++;
        };
        bool left = g_gameLeft != 0, right = g_gameRight != 0;
        if(n<max){LONG pitch=TakeGamePitch();if(pitch)push(DIMOFS_Y,DWORD(pitch));}
        if (left != g_gameLeftSent && n < max) { push(DIMOFS_BUTTON0, left ? 0x80 : 0); g_gameLeftSent = left; }
        if (right != g_gameRightSent && n < max) { push(DIMOFS_BUTTON1, right ? 0x80 : 0); g_gameRightSent = right; }
        LONG wheel = g_gameWheel;
        if (wheel != 0 && n < max) {
            push(DIMOFS_Z, (DWORD)(wheel * WHEEL_DELTA));
            InterlockedExchangeAdd(&g_gameWheel, -wheel);
        }
        *count = n;
        return DI_OK;
    }
    if (!IsMouse(dev) || !g_active || !count) {
        HRESULT hr=g_getData(dev,size,data,count,flags);
        static unsigned reported=0;
        if(IsMouse(dev)&&!g_game&&xrapi::NativeCinemaReady()&&count&&data&&*count&&reported<12){
            Log("Mouse: native DI caller=%p flags=%lu size=%lu count=%lu first=%lu,%ld",__builtin_return_address(0),flags,size,*count,data->dwOfs,(LONG)data->dwData);
            ++reported;
        }
        return hr;
    }
    if ((g_dataCalls++ % 20000) == 0) Log("Mouse: game reads DirectInput GetDeviceData (%u)", g_dataCalls);
    // Настоящие события мыши выбрасываем, вместо них - движение к лучу и курок
    DWORD real = INFINITE;
    g_getData(dev, size, nullptr, &real, 0);
    if (!data || *count == 0) {
        *count = 0;
        return DI_OK;
    }
    Advance();
    DWORD n = 0, max = *count;
    auto push = [&](DWORD ofs, DWORD value) {
        DIDEVICEOBJECTDATA* e = (DIDEVICEOBJECTDATA*)((BYTE*)data + n * size);
        ZeroMemory(e, size);
        e->dwOfs = ofs;
        e->dwData = value;
        e->dwTimeStamp = GetTickCount();
        n++;
    };
    // Игра забирает по одному событию за вызов: отдаём то, что влезает, остальное - в следующий раз
    LONG ix = (LONG)g_pendX, iy = (LONG)g_pendY;
    bool consume=(flags&DIGDD_PEEK)==0;
    if (ix != 0 && n < max) { push(DIMOFS_X, (DWORD)ix); if(consume)g_pendX -= ix; }
    if (iy != 0 && n < max) { push(DIMOFS_Y, (DWORD)iy); if(consume)g_pendY -= iy; }
    bool button = ButtonDown();
    if (button != g_buttonSent && n < max) {
        push(DIMOFS_BUTTON0, button ? 0x80 : 0);
        if(consume)g_buttonSent = button;
    }
    *count = n;
    return DI_OK;
}

HRESULT STDMETHODCALLTYPE HookedCreateDevice(IDirectInput8W* di, REFGUID guid, LPDIRECTINPUTDEVICE8W* out,
                                             LPUNKNOWN outer) {
    HRESULT hr = g_createDevice(di, guid, out, outer);
    if (SUCCEEDED(hr) && out && *out && IsEqualGUID(guid, GUID_SysMouse)) {
        if (g_miceCount < 8) g_mice[g_miceCount++] = *out;
        void** vt = *(void***)*out;
        if (!g_getState && MH_CreateHook(vt[9], (void*)HookedGetState, (void**)&g_getState) == MH_OK)
            MH_EnableHook(vt[9]);
        if (!g_getData && MH_CreateHook(vt[10], (void*)HookedGetData, (void**)&g_getData) == MH_OK)
            MH_EnableHook(vt[10]);
        Log("Mouse: DirectInput mouse created, hooked");
    }
    return hr;
}

HRESULT WINAPI HookedDI8Create(HINSTANCE inst, DWORD version, REFIID iid, LPVOID* out, LPUNKNOWN outer) {
    HRESULT hr = g_di8Create(inst, version, iid, out, outer);
    if (SUCCEEDED(hr) && out && *out && !g_createDevice) {
        void** vt = *(void***)*out;
        if (MH_CreateHook(vt[3], (void*)HookedCreateDevice, (void**)&g_createDevice) == MH_OK) MH_EnableHook(vt[3]);
        Log("Mouse: DirectInput8Create hooked");
    }
    return hr;
}

}  // namespace

bool Install() {
    MH_CreateHookApi(L"user32", "GetCursorPos", (void*)HookedGetCursorPos, (void**)&g_getCursorPos);
    MH_CreateHookApi(L"user32", "GetAsyncKeyState", (void*)HookedGetAsyncKeyState, (void**)&g_getAsyncKeyState);
    MH_CreateHookApi(L"user32", "GetKeyState", (void*)HookedGetKeyState, (void**)&g_getKeyState);
    MH_EnableHook(MH_ALL_HOOKS);
    HMODULE di = GetModuleHandleA("dinput8.dll");
    if (!di) di = LoadLibraryA("dinput8.dll");
    void* create = di ? (void*)GetProcAddress(di, "DirectInput8Create") : nullptr;
    if (create && MH_CreateHook(create, (void*)HookedDI8Create, (void**)&g_di8Create) == MH_OK) MH_EnableHook(create);
    Log("Mouse: hooks %s", create ? "installed" : "partial (no dinput8)");
    flashpointer::Install();
    return true;
}

void SetWindow(HWND window) { g_window = window; }
HWND GameWindow() { return g_window; }
void SetScale(float scale) { if (scale > 0.05f) g_scale = scale; }
void SetScreenSize(int w, int h) { g_screenW = w; g_screenH = h; }

void Point(bool active, int x, int y, bool button) {
    bool native=xrapi::NativeCinemaReady();
    if (active && !g_active) InterlockedExchange(&g_needClamp, 1);
    InterlockedExchange(&g_x, x);
    InterlockedExchange(&g_y, y);
    InterlockedExchange(&g_button, button ? 1 : 0);
    InterlockedExchange(&g_active, active ? 1 : 0);
    flashpointer::Target(native&&active,x,y,g_screenW,g_screenH);
    if(native && active){
        Advance();x=g_x;y=g_y;button=ButtonDown();
        // One ordinary event per axis per rendered menu frame. Flash's input
        // hook resolves it to the absolute ray point; repeated DI polls/PEEK
        // do not create more events. New movies synchronise even at rest.
        if(flashpointer::Ready())g_pendX=g_pendY=1;
    }
    if (!active || !g_window) {
        g_buttonPosted = false;
        return;
    }
    // Те же события окну: движение (для подсветки пунктов) и нажатие/отпускание
    LPARAM pos = MAKELPARAM(x, y);
    if (x != g_postedX || y != g_postedY) {
        PostMessageA(g_window, WM_MOUSEMOVE, button ? MK_LBUTTON : 0, pos);
        g_postedX = x;
        g_postedY = y;
    }
    if (button != g_buttonPosted) {
        Log("Mouse: click %s at %d,%d", button ? "down" : "up", x, y);
        PostMessageA(g_window, button ? WM_LBUTTONDOWN : WM_LBUTTONUP, button ? MK_LBUTTON : 0, pos);
        g_buttonPosted = button;
    }
}

void GameInput(bool active, bool left, bool right, int wheel) {
    if (left != (g_gameLeft != 0) || right != (g_gameRight != 0))
        Log("Mouse: game buttons left=%d right=%d", left, right);
    InterlockedExchange(&g_gameLeft, active && left ? 1 : 0);
    InterlockedExchange(&g_gameRight, active && right ? 1 : 0);
    if (active && wheel) InterlockedExchangeAdd(&g_gameWheel, wheel);
    if (!active){InterlockedExchange(&g_gameWheel,0);InterlockedExchange(&g_gamePitch,0);}
    InterlockedExchange(&g_game, active ? 1 : 0);
}

void MeleePitch(float error,bool valid){
    LONG step=valid&&isfinite(error)&&fabsf(error)>1.5f?LONG(lrintf(-std::clamp(error*.5f,-8.f,8.f))):0;
    InterlockedExchange(&g_gamePitch,step);InterlockedExchange(&g_gamePitchStamp,LONG(GetTickCount()));
    static DWORD logged=0;
    if(valid&&GetTickCount()-logged>5000){logged=GetTickCount();Log("VR melee pitch: head-engine error %.2f deg, native mouse step %ld",error,step);}
}

void Resync() { InterlockedExchange(&g_needClamp, 1); }

void Escape(bool down) {
    InterlockedExchange(&g_escape, down ? 1 : 0);
    if (down != g_escapePosted && g_window) {
        PostMessageA(g_window, down ? WM_KEYDOWN : WM_KEYUP, VK_ESCAPE, down ? 0x00010001 : 0xC0010001);
        g_escapePosted = down;
    }
}

}  // namespace mouse
