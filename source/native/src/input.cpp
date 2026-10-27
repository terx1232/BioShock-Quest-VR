// Управление в игре: контроллеры Quest 2 превращаются в клавиатуру и мышь ПК-версии BioShock.
//
// Клавиатуру игра читает из сообщений окна (WM_KEYDOWN/WM_KEYUP), мышь - через DirectInput.
// Мод шлёт окну клавиши, а кнопки и колесо мыши отдаёт через перехват DirectInput (mouse.cpp).
// Геймпада для игры нет: XInput всегда отвечает "не подключён".
//
//   левый стик        стрелки (ходьба)
//   правый стик       влево/вправо - поворот (мод), вверх/вниз - колесо мыши (смена оружия)
//   правый курок      J - оружие (переключиться на него и стрелять), прицел - правая рука
//   левый курок       G - плазмид (переключиться и применить), прицел - левая рука
//   A  использовать         B  перезарядка
//   X  аптечка              Y  EVE при отпускании, удержание Y - взлом (QuestHealingButtons)
//   левый грип  H (взлом, QuestHackGrip), иначе Z; правый грип Q (тип патронов)
//   нажатие левого стика  C (присесть)   нажатие правого стика  пробел (прыжок)
//   левое меню  Esc (пауза)
#include "input.h"

#include <windows.h>
#include <xinput.h>
#include <math.h>

#include "MinHook.h"
#include "config.h"
#include "log.h"
#include "mouse.h"
#include "weapon.h"
#include "xrapi.h"
#include "pcvr.h"
#include "uistate.h"
#include "taphold.h"
#include "meleeswing.h"
#include "melee.h"

namespace input {
namespace {

typedef DWORD(WINAPI* XInputGetStateFn)(DWORD, XINPUT_STATE*);
XInputGetStateFn g_original = nullptr;
HWND g_window = nullptr;
volatile LONG g_active = 0;
volatile LONG g_held[256] = {};    // клавиши, которые мод сейчас держит
bool g_armed[xrapi::BUTTON_COUNT];  // кнопка отпущена после входа в игру (иначе нажатие из меню протекает в игру)
bool g_moveDown[4] = {};            // W S A D
int g_wheelDir = 0;
// Удар взмахом: скорость правой руки (относительно головы, м/с) и удержание "огня" на пару кадров
meleevr::Swing g_swing;
DWORD g_swingUntil=0;
DWORD g_swingLast = 0;
volatile bool g_aimRight = false, g_aimLeft = false;  // прицел по руке (курок зажат / удар)
questinput::TapHold g_eveHack;
questinput::TapHold g_menuMap;
bool g_menuUsed=false;
bool g_mapSent=false;
bool g_menuInputReady=false;
DWORD g_combatLast=0;

DWORD WINAPI HookedGetState(DWORD, XINPUT_STATE*) { return ERROR_DEVICE_NOT_CONNECTED; }

void Key(BYTE vk, bool down) {
    if ((g_held[vk] != 0) == down) return;
    InterlockedExchange(&g_held[vk], down ? 1 : 0);
    if (!g_window) return;
    UINT scan = MapVirtualKeyA(vk, 0 /* MAPVK_VK_TO_VSC */);
    bool extended = vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT;
    LPARAM lp = 1 | (scan << 16) | (extended ? 1 << 24 : 0) | (down ? 0 : 0xC0000000);
    PostMessageA(g_window, down ? WM_KEYDOWN : WM_KEYUP, vk, lp);
}

void ReleaseAll() {
    g_eveHack={};
    g_menuMap={};g_menuUsed=g_mapSent=false;
    g_menuInputReady=false;
    g_combatLast=0;
    g_aimRight=g_aimLeft=false;
    g_swing={};g_swingUntil=g_swingLast=0;melee::Reset();
    for (int vk = 0; vk < 256; vk++)
        if (g_held[vk]) Key((BYTE)vk, false);
    for (bool& m : g_moveDown) m = false;
    g_wheelDir = 0;
    mouse::GameInput(false, false, false);
}

// Направление стика с гистерезисом: нажать на 0.5, отпустить ниже 0.35
bool Axis(bool& state, float v) {
    state = state ? v > 0.35f : v > 0.5f;
    return state;
}

}  // namespace

bool Install() {
    HMODULE xinput = GetModuleHandleA("xinput1_3.dll");
    if (!xinput) xinput = LoadLibraryA("xinput1_3.dll");
    void* getState = xinput ? (void*)GetProcAddress(xinput, (LPCSTR)2) : nullptr;  // XInputGetState
    if (getState && MH_CreateHook(getState, (void*)HookedGetState, (void**)&g_original) == MH_OK)
        MH_EnableHook(getState);
    Log("Input: keyboard/mouse mapping, gamepad hidden (%s)", getState ? "XInput hooked" : "no XInput");
    return true;
}

void SetWindow(HWND window) { g_window = window; }

void SetActive(bool active) {
    LONG was = InterlockedExchange(&g_active, active ? 1 : 0);
    if (was && !active) {
        ReleaseAll();
        g_aimRight = g_aimLeft = false;
    }
    if (!was && active) {
        // кнопки, зажатые в момент входа в игру, срабатывают только после отпускания
        xrapi::State s = xrapi::Get();
        for (int i = 0; i < xrapi::BUTTON_COUNT; i++) g_armed[i] = !(s.valid && s.buttons[i]);
    }
}

bool IsActive() { return g_active != 0; }

int AimHand() { return g_aimLeft ? 0 : g_aimRight ? 1 : -1; }

bool KeyHeld(int vk) { return vk >= 0 && vk < 256 && g_held[vk] != 0; }
bool CombatActive(){return g_active&&g_combatLast&&GetTickCount()-g_combatLast<3000;}

void Update(const xrapi::State& s) {
    if (!s.valid) { ReleaseAll(); return; }
    if (!g_active) {
        if(g_config.questControllerBindings&&uistate::MenuOpen()){
            if(!g_menuInputReady){
                for(int i=0;i<xrapi::BUTTON_COUNT;i++)g_armed[i]=!s.buttons[i];
                g_menuInputReady=true;
            }
            for(int i=0;i<xrapi::BUTTON_COUNT;i++)if(!s.buttons[i])g_armed[i]=true;
            auto pressed=[&](int i){return s.buttons[i]&&g_armed[i];};
            Key('A',pressed(xrapi::R_A));Key('X',pressed(xrapi::L_X));Key('Y',pressed(xrapi::L_Y));
            Key('H',pressed(xrapi::L_GRIP));Key('Q',pressed(xrapi::R_GRIP));
            Key(VK_UP,s.left.stickY>.6f);Key(VK_DOWN,s.left.stickY<-.6f);
            Key(VK_LEFT,s.left.stickX<-.6f);Key(VK_RIGHT,s.left.stickX>.6f);
        }else if(g_menuInputReady)ReleaseAll();
        if (pcvr::Enabled()) {
            Key(VK_UP, s.left.stickY > .6f); Key(VK_DOWN, s.left.stickY < -.6f);
            Key(VK_LEFT, s.left.stickX < -.6f); Key(VK_RIGHT, s.left.stickX > .6f);
            Key(VK_RETURN, s.buttons[xrapi::R_A]);
            Key(VK_ESCAPE, s.buttons[xrapi::R_B] || s.buttons[xrapi::L_MENU]);
        }
        return;
    }
    using namespace xrapi;
    bool b[BUTTON_COUNT];
    for (int i = 0; i < BUTTON_COUNT; i++) {
        if (!s.buttons[i]) g_armed[i] = true;
        b[i] = s.buttons[i] && g_armed[i];
    }
    bool pauseTap=false;
    if(g_config.questControllerBindings){
        const bool modified=b[L_MENU];
        if(modified&&!g_menuMap.down)g_menuUsed=g_mapSent=false;
        const bool combo=modified&&(b[R_A]||b[R_B]||b[L_X]||b[L_Y]||b[L_GRIP]||b[R_GRIP]||b[L_THUMBSTICK_PRESS]||fabsf(s.right.stickY)>.7f);
        if(combo)g_menuUsed=true;
        const DWORD menuNow=GetTickCount();
        const bool longRelease=!modified&&g_menuMap.down&&DWORD(menuNow-g_menuMap.since)>=450&&!g_menuUsed&&!g_mapSent;
        const auto menu=g_menuMap.Update(modified,menuNow);
        const bool mapHold=menu.hold&&!g_menuUsed;
        pauseTap=menu.tap&&!g_menuUsed;
        const bool map=(modified&&(b[R_A]||mapHold))||longRelease;
        Key('M',map);if(map)g_mapSent=true;
        Key(VK_SHIFT,modified&&b[R_B]);
        Key('I',modified&&b[L_X]);Key('T',modified&&b[L_Y]);
        Key(VK_OEM_3,modified&&b[L_GRIP]);Key('Z',modified&&b[R_GRIP]);
        Key('P',modified&&b[L_THUMBSTICK_PRESS]);
        Key(VK_PRIOR,modified&&s.right.stickY>.7f);Key(VK_NEXT,modified&&s.right.stickY<-.7f);
        if(modified){
            // A shortcut must never also search, heal, consume EVE, hack,
            // change ammunition or fire. Preserve physical release events.
            const int regularKeys[]={VK_UP,VK_DOWN,VK_LEFT,VK_RIGHT,'A','B','X','Y','H','Q','C',VK_SPACE,'J','G',VK_ESCAPE};
            for(int key:regularKeys)Key((BYTE)key,false);
            g_eveHack={};g_swing={};g_swingUntil=g_swingLast=0;melee::Reset();
            g_aimRight=g_aimLeft=false;g_wheelDir=0;
            for(bool& move:g_moveDown)move=false;
            // Require release of held chord buttons before normal gameplay.
            for(int i=0;i<BUTTON_COUNT;i++)if(i!=L_MENU&&b[i])g_armed[i]=false;
            mouse::GameInput(true,false,false);return;
        }
    }
    // Действия игры назначены на клавиши с буквами кнопок Quest (порт правит DefUser.ini),
    // поэтому подсказки игры показывают A, B, X, Y
    Key(VK_UP, Axis(g_moveDown[0], s.left.stickY));
    Key(VK_DOWN, Axis(g_moveDown[1], -s.left.stickY));
    Key(VK_LEFT, Axis(g_moveDown[2], -s.left.stickX));
    Key(VK_RIGHT, Axis(g_moveDown[3], s.left.stickX));
    Key('A', b[R_A]);
    Key('B', b[R_B]);
    Key('X', b[L_X]);
    const bool hackGrip=g_config.questHackGrip&&!uistate::ContainerOpen()&&b[L_GRIP];
    if(g_config.questHealingButtons&&!uistate::ContainerOpen()){
        const auto action=g_eveHack.Update(b[L_Y],GetTickCount());
        Key('Y',action.tap);Key('H',action.hold||hackGrip);
    }else{g_eveHack={};Key('Y',b[L_Y]);Key('H',hackGrip);}
    Key('Z', !g_config.questHackGrip&&b[L_GRIP]);
    Key('Q', b[R_GRIP]);
    Key('C', b[L_THUMBSTICK_PRESS]);
    Key(VK_SPACE, b[R_THUMBSTICK_PRESS]);
    Key(VK_ESCAPE, g_config.questControllerBindings?pauseTap:b[L_MENU]);

    // Правый стик вверх/вниз - один щелчок колеса за отклонение
    int dir = s.right.stickY > 0.7f ? 1 : s.right.stickY < -0.7f ? -1 : 0;
    if (s.right.stickY > -0.3f && s.right.stickY < 0.3f) g_wheelDir = 0;
    int wheel = 0;
    if (dir != 0 && g_wheelDir == 0) {
        wheel = dir;
        g_wheelDir = dir;
    }
    const DWORD now=GetTickCount();bool container=uistate::ContainerOpen(),wrench=weapon::MeleeEquipped();
    bool request=false;
    if(g_config.swingMelee&&wrench&&!container){
        request=g_swing.Update(s.right.position,s.hmdPosition,now,s.resumes,g_config.swingSpeed);
        if(request){g_swingLast=now;g_swingUntil=now+remasteredswing::kPulseMs;melee::Request();Log("VR melee: native attack, 120ms pulse; visible wrench fixed to controller, no animation");}
    }else{g_swing={};g_swingUntil=0;g_swingLast=0;}
    bool swing=g_swingUntil&&static_cast<LONG>(g_swingUntil-now)>0;
    // курки как на геймпаде: правый - оружие (J), левый - плазмид (G); мышь только для колеса.
    // Wrench attacks come only from a real hand stroke. Trigger still fires
    // other weapons and retains its separate native loot-menu action.
    bool fire = b[R_TRIGGER] && (!wrench||container);
    if(!container&&(fire||swing||b[L_TRIGGER]))g_combatLast=GetTickCount();
    Key('J', fire || swing);
    Key('G', b[L_TRIGGER]);
    g_aimRight = !container&&(fire || swing || (g_swingLast&&now-g_swingLast<500));
    g_aimLeft = !container&&b[L_TRIGGER];
    mouse::GameInput(true, false, false, wheel);
}

}  // namespace input
