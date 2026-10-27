// Какое меню открыто: интерфейс BioShock включает свои "контексты ввода" консольными командами
// PUSHINPUTCONTEXT / POPINPUTCONTEXT / SETINPUTCONTEXT(S) <имена>, их разбирает UInput::Exec
// (Bioshock.exe 1.1, 0x10a505d0). Перехват видит каждую команду и ведёт набор активных
// контекстов; пауза, инвентарь, автоматы, взлом и т.п. показываются "кинотеатром".
#include "uistate.h"

#include <windows.h>
#include <string.h>
#include <wchar.h>

#include <string>
#include <vector>

#include "MinHook.h"
#include "log.h"
#include "uobject.h"

namespace uistate {
namespace {

const uintptr_t kInputExec = 0x10a505d0;
// push -1 / mov eax,fs:[0] / push 10F5410Fh
const unsigned char kInputExecBytes[] = {0x6A, 0xFF, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x68, 0x0F, 0x41, 0xF5, 0x10};

// Контексты, в которых нужен экран с курсором, а не VR-вид
const wchar_t* kTheaterContexts[] = {
    L"GathererChoice", L"InventoryUIActive", L"PlasmidEquipUIActive",
    L"VendingMachineUIActive", L"CraftingUIActive", L"InGameManualUIActive", L"PauseUIActive",
    L"CalibrationUIActive", L"HackingUIActive", L"MapsUIActive", L"ComboLockUIActive",
    L"WeaponStationUIActive", L"WarningUIActive", L"BathysphereUIActive", L"PhotoGradingUIActive"};

typedef int(__fastcall* ExecFn)(void* self, void* edx, const wchar_t* cmd, void* ar);
ExecFn g_original = nullptr;

CRITICAL_SECTION g_lock;
std::vector<std::wstring> g_stack;
volatile LONG g_theater = 0;
volatile LONG g_container=0,g_scripted=0,g_cinematic=0;

bool IsTheater(const std::wstring& name) {
    for (const wchar_t* t : kTheaterContexts)
        if (_wcsicmp(name.c_str(), t) == 0) return true;
    return false;
}

void Recount() {
    bool theater = false,container=false,scripted=false;
    for (const std::wstring& n : g_stack){
        if (IsTheater(n)) theater = true;
        if(!_wcsicmp(n.c_str(),L"ContainerUIActive"))container=true;
        if(!_wcsicmp(n.c_str(),L"NullInput")||!_wcsicmp(n.c_str(),L"NullAllInputExceptHarvestRelease"))scripted=true;
    }
    if (theater != (g_theater != 0)||container!=(g_container!=0)||scripted!=(g_scripted!=0)) {
        std::string list;
        for (const std::wstring& n : g_stack) {
            char buf[128];
            WideCharToMultiByte(CP_UTF8, 0, n.c_str(), -1, buf, sizeof(buf), nullptr, nullptr);
            list += buf;
            list += ' ';
        }
        Log("UI: %s [%s] container=%d cinematic-context=%d", theater ? "menu open" : "menu closed", list.c_str(),container,scripted);
    }
    InterlockedExchange(&g_theater, theater ? 1 : 0);
    InterlockedExchange(&g_container,container?1:0);InterlockedExchange(&g_scripted,scripted?1:0);
}

// "CMD a b c" -> слово после пробелов
std::vector<std::wstring> Words(const wchar_t* s) {
    std::vector<std::wstring> out;
    while (*s) {
        while (*s == L' ' || *s == L'\t') s++;
        const wchar_t* start = s;
        while (*s && *s != L' ' && *s != L'\t') s++;
        if (s > start) out.emplace_back(start, s - start);
    }
    return out;
}

void Observe(const wchar_t* cmd) {
    if (!cmd || !wcsstr(cmd, L"INPUTCONTEXT")) {
        // команды могут прийти в любом регистре
        if (!cmd) return;
        std::wstring upper(cmd);
        for (wchar_t& c : upper) c = towupper(c);
        if (upper.find(L"INPUTCONTEXT") == std::wstring::npos) return;
    }
    std::vector<std::wstring> w = Words(cmd);
    if (w.empty()) return;
    std::wstring verb = w[0];
    for (wchar_t& c : verb) c = towupper(c);

    EnterCriticalSection(&g_lock);
    if (verb == L"PUSHINPUTCONTEXT") {
        for (size_t i = 1; i < w.size(); i++) g_stack.push_back(w[i]);
    } else if (verb == L"POPINPUTCONTEXT") {
        if (w.size() > 1) {
            for (size_t i = g_stack.size(); i-- > 0;)
                if (_wcsicmp(g_stack[i].c_str(), w[1].c_str()) == 0) { g_stack.erase(g_stack.begin() + i); break; }
        } else if (!g_stack.empty()) {
            g_stack.pop_back();
        }
    } else if (verb == L"SETINPUTCONTEXT" || verb == L"SETINPUTCONTEXTS") {
        g_stack.assign(w.begin() + 1, w.end());
    }
    Recount();
    LeaveCriticalSection(&g_lock);
}

int __fastcall HookedExec(void* self, void* edx, const wchar_t* cmd, void* ar) {
    Observe(cmd);
    return g_original(self, edx, cmd, ar);
}

}  // namespace

bool Install() {
    InitializeCriticalSection(&g_lock);
    if (memcmp((void*)kInputExec, kInputExecBytes, sizeof(kInputExecBytes)) != 0) {
        Log("UI: UInput::Exec not found - menus are recognised only without player camera");
        return false;
    }
    if (MH_CreateHook((void*)kInputExec, (void*)HookedExec, (void**)&g_original) != MH_OK ||
        MH_EnableHook((void*)kInputExec) != MH_OK) {
        Log("UI: hook failed");
        return false;
    }
    Log("UI: UInput::Exec hooked");
    return true;
}

bool MenuOpen() { return g_theater != 0; }
bool ContainerOpen(){return g_container!=0;}
bool Cinematic(){return !MenuOpen()&&(g_scripted||g_cinematic);}
void UpdateCinematic(void* controller){
    if(!controller||IsBadReadPtr(controller,0x30))return;
    static void* cls=nullptr;static int offset=-1;static unsigned mask=0;
    void* current=*(void**)((char*)controller+0x2c);
    if(current!=cls){cls=current;offset=-1;mask=0;
        if(uobject::BoolProperty(controller,L"bCinematicMode",offset,mask))
            Log("Cinematic: reflected bCinematicMode +0x%x mask %08x",offset,mask);
    }
    bool active=offset>0&&mask&&!IsBadReadPtr((char*)controller+offset,4)&&(*(unsigned*)((char*)controller+offset)&mask);
    if(active!=(g_cinematic!=0))Log("Cinematic: native camera %s",active?"entered":"left");
    InterlockedExchange(&g_cinematic,active?1:0);
}

}  // namespace uistate
