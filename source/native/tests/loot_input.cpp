// Input goes only to this fixture's hidden window, never BioShock.
#include <cstdio>
#include <cstdlib>
#include "../src/input.cpp"
Config g_config;bool loot=false;
namespace melee {void Request(){}void Reset(){}}
void Log(const char*,...){}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*){return MH_OK;}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID){return MH_OK;}
namespace pcvr {bool Enabled(){return false;}}
bool menu=false;
namespace uistate {bool ContainerOpen(){return loot;}bool MenuOpen(){return menu;}}
namespace weapon {bool MeleeEquipped(){return true;}}
namespace xrapi {State Get(){State s{};s.valid=true;return s;}}
namespace mouse {void GameInput(bool,bool,bool,int){}}
void Check(bool ok,const char* s){if(!ok){fprintf(stderr,"FAIL: %s\n",s);exit(1);}}
int main(){
    HWND window=CreateWindowExW(0,L"STATIC",L"hidden input fixture",WS_OVERLAPPED,0,0,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    input::SetWindow(window);input::SetActive(true);xrapi::State s=xrapi::Get();s.buttons[xrapi::R_A]=true;
    input::Update(s);Check(input::KeyHeld('A'),"A opens loot");
    MSG m{};while(PeekMessageW(&m,window,0,0,PM_REMOVE)){}
    loot=true;input::SetActive(true);input::Update(s);
    Check(input::KeyHeld('A'),"opening loot keeps game input active");
    Check(!PeekMessageW(&m,window,WM_KEYDOWN,WM_KEYDOWN,PM_REMOVE),"held opening button does not press again in the loot context");
    s.buttons[xrapi::R_A]=false;input::Update(s);Check(!input::KeyHeld('A'),"native onRelease follows physical release");
    s.buttons[xrapi::R_TRIGGER]=true;input::Update(s);Check(input::KeyHeld('J')&&input::AimHand()==-1,"loot reroll works even with wrench and cannot aim camera");
    loot=false;input::Update(s);Check(!input::KeyHeld('J'),"wrench trigger returns to normal melee behavior");
    s.buttons[xrapi::R_TRIGGER]=false;g_config.questHealingButtons=true;
    input::Update(s);g_config.swingMelee=true;
    s.buttons[xrapi::R_TRIGGER]=true;input::Update(s);Check(!input::KeyHeld('J'),"wrench trigger cannot request a strike");
    Sleep(145);input::Update(s);Check(!input::KeyHeld('J'),"holding wrench trigger cannot attack");
    s.buttons[xrapi::R_TRIGGER]=false;input::Update(s);
    input::g_swing={};input::g_swingUntil=input::g_swingLast=0;
    s.hmdPosition={0,1.7f,0};s.right.position={0,1.4f,-.5f};
    for(int i=0;i<5;i++){Sleep(33);input::Update(s);Check(!input::KeyHeld('J'),"stationary wrench cannot strike");}
    s.right.position.z+=.10f;Sleep(33);input::Update(s);Check(!input::KeyHeld('J'),"wind-up does not strike");
    s.right.position.z-=.11f;Sleep(33);input::Update(s);Check(input::KeyHeld('J'),"actual input still strikes by controller swing with trigger released");
    Sleep(145);input::Update(s);Check(!input::KeyHeld('J'),"gesture pulse ends normally");
    s.buttons[xrapi::L_X]=true;input::Update(s);Check(input::KeyHeld('X'),"native health button X forwarded");s.buttons[xrapi::L_X]=false;
    s.buttons[xrapi::L_Y]=true;input::Update(s);Check(!input::KeyHeld('Y')&&!input::KeyHeld('H'),"EVE waits for short release");
    s.buttons[xrapi::L_Y]=false;input::Update(s);Check(input::KeyHeld('Y')&&!input::KeyHeld('H'),"EVE tap forwarded to native Y command");
    input::Update(s);Check(!input::KeyHeld('Y'),"one EVE keydown per tap");
    s.buttons[xrapi::L_Y]=true;input::Update(s);Sleep(460);input::Update(s);
    Check(input::KeyHeld('H')&&!input::KeyHeld('Y'),"held Y forwards hack without consuming EVE");
    s.buttons[xrapi::L_Y]=false;input::Update(s);Check(!input::KeyHeld('H')&&!input::KeyHeld('Y'),"hack release cannot inject EVE");
    g_config.questHackGrip=true;s.buttons[xrapi::L_GRIP]=true;input::Update(s);
    Check(input::KeyHeld('H')&&!input::KeyHeld('Z')&&!input::KeyHeld('Y'),"dedicated left grip hacks without EVE or zoom");
    s.buttons[xrapi::L_GRIP]=false;input::Update(s);Check(!input::KeyHeld('H'),"hack grip release forwarded");
    s.buttons[xrapi::L_TRIGGER]=true;input::Update(s);Check(input::CombatActive(),"real player fire marks combat for popup suppression");
    input::SetActive(false);Check(!input::CombatActive()&&!input::KeyHeld('G'),"menu/sleep releases keys and combat state");
    s={};s.valid=true;g_config.questControllerBindings=true;input::SetActive(true);input::Update(s);
    s.buttons[xrapi::L_MENU]=true;input::Update(s);Check(!input::KeyHeld(VK_ESCAPE)&&!input::KeyHeld('M'),"menu waits for pause tap or map hold");
    s.buttons[xrapi::L_MENU]=false;input::Update(s);Check(input::KeyHeld(VK_ESCAPE)&&!input::KeyHeld('M'),"short menu release pauses once");
    input::Update(s);Check(!input::KeyHeld(VK_ESCAPE),"pause pulse releases");
    s.buttons[xrapi::L_MENU]=true;input::Update(s);input::g_menuMap.since=GetTickCount()-500;input::Update(s);
    Check(input::KeyHeld('M')&&!input::KeyHeld(VK_ESCAPE),"held menu opens direct map without pause");
    s.buttons[xrapi::L_MENU]=false;input::Update(s);Check(!input::KeyHeld('M')&&!input::KeyHeld(VK_ESCAPE),"map hold release cannot pause");
    s.buttons[xrapi::L_MENU]=true;input::Update(s);input::g_menuMap.since=GetTickCount()-500;
    s.buttons[xrapi::L_MENU]=false;input::Update(s);Check(input::KeyHeld('M')&&!input::KeyHeld(VK_ESCAPE),"slow-frame long release still opens the map");input::Update(s);
    const int buttons[]={xrapi::R_A,xrapi::R_B,xrapi::L_X,xrapi::L_Y,xrapi::L_GRIP,xrapi::R_GRIP,xrapi::L_THUMBSTICK_PRESS};
    const int keys[]={'M',VK_SHIFT,'I','T',VK_OEM_3,'Z','P'};
    for(unsigned i=0;i<sizeof(buttons)/sizeof(buttons[0]);i++){
        s.buttons[xrapi::L_MENU]=true;input::Update(s);s.buttons[buttons[i]]=true;input::Update(s);
        Check(input::KeyHeld(keys[i]),"controller shortcut reaches its own native key");
        Check(!input::KeyHeld('A')&&!input::KeyHeld('B')&&!input::KeyHeld('X')&&!input::KeyHeld('Y')&&!input::KeyHeld('H')&&!input::KeyHeld('Q')&&!input::KeyHeld('J')&&!input::KeyHeld('G'),"shortcut cannot leak use/heal/EVE/hack/ammo/fire");
        s.buttons[xrapi::L_MENU]=false;input::Update(s);Check(!input::KeyHeld(VK_ESCAPE)&&!input::KeyHeld('X')&&!input::KeyHeld('Y'),"chord release cannot pause, heal or consume EVE");
        s.buttons[buttons[i]]=false;input::Update(s);
    }
    s.buttons[xrapi::L_MENU]=true;input::Update(s);s.right.stickY=-1;input::Update(s);
    Check(input::KeyHeld(VK_NEXT),"menu plus right stick down selects previous ammo");
    s={};s.valid=true;input::Update(s);input::SetActive(false);menu=true;
    s.buttons[xrapi::R_A]=true;input::Update(s);Check(!input::KeyHeld('A'),"button opening a menu cannot accept again before release");
    s.buttons[xrapi::R_A]=false;input::Update(s);s.buttons[xrapi::R_A]=true;input::Update(s);Check(input::KeyHeld('A'),"A reaches native contextual UI after release");
    s.buttons[xrapi::L_X]=true;s.buttons[xrapi::L_Y]=true;s.buttons[xrapi::L_GRIP]=true;s.buttons[xrapi::R_GRIP]=true;input::Update(s);
    Check(input::KeyHeld('X')&&input::KeyHeld('Y')&&input::KeyHeld('H')&&input::KeyHeld('Q'),"native UI special actions remain accessible from face buttons and grips");
    menu=false;input::Update(s);Check(!input::KeyHeld('A')&&!input::KeyHeld('X')&&!input::KeyHeld('H'),"UI exit releases all contextual keys");
    puts("PASS: controller map/pause tap-hold, slow release, all shortcuts without gameplay leakage, menu re-arm and contextual buttons");
    DestroyWindow(window);puts("PASS: actual input, loot release, X healing, Y EVE tap/hold hack, one-shot consumption, combat/menu reset");
}
