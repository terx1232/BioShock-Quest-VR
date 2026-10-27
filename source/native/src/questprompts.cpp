// Replace action tags before BioShock creates keyboard/mouse button images.
// The original formatter still owns both FString arguments and the result.
#include "questprompts.h"
#include "config.h"
#include "log.h"
#include "MinHook.h"
#include <windows.h>
#include <string>
#include <cwchar>

namespace questprompts {
namespace {
struct GameString {wchar_t* data;int count,capacity;};
static_assert(sizeof(GameString)==12,"original 32-bit FString ABI");
using FormatFn=GameString*(__fastcall*)(void*,void*,GameString*,GameString,GameString);
using AssignFn=GameString*(__fastcall*)(GameString*,void*,const GameString*);
constexpr uintptr_t kFormat=0x109cf490,kAssign=0x1090aff0;
const unsigned char formatBytes[]={0x6a,0xff,0x68,0x94,0x12,0xf5,0x10,0x64,0xa1,0,0,0,0};
const unsigned char assignBytes[]={0x55,0x8b,0xec,0x56,0x8b,0xf1,0x8b,0x4d,0x08};
FormatFn original=nullptr;
AssignFn assign=reinterpret_cast<AssignFn>(kAssign);

const wchar_t* Label(const std::wstring& action){
    struct Binding {const wchar_t *action,*label;};
    static const Binding bindings[]={
        {L"Use",L"[A]"},{L"UseStopped",L"[A]"},{L"CollectAllContainerItems",L"[A]"},
        {L"Hack",L"[L Grip]"},{L"ReRollContainer",L"[R Trigger]"},
        {L"Reload",L"[B]"},{L"HandleReloadAndBioAmmo",L"[B]"},
        {L"MedHypo",L"[X]"},{L"UseHypoOfType MedHypo",L"[X]"},
        {L"InjectBioAmmo",L"[Y]"},{L"BioAmmoHypo",L"[Y]"},
        {L"Fire",L"[R Trigger]"},{L"SwitchAndFireWeapon",L"[R Trigger]"},
        {L"AltFire",L"[L Trigger]"},{L"SwitchAndFireAbility",L"[L Trigger]"},
        {L"SwitchWeaponsOrPlasmids",L"[Menu + B]"},
        {L"LaunchPCWeaponSelection",L"[Menu + B]"},
        {L"OpenWeaponMenu",L"[Menu + B]"},{L"OpenAbilityMenu",L"[Menu + B]"},
        {L"AmmoSelectionUp",L"[R Grip]"},{L"AmmoSelectionDown",L"[Menu + R Stick Down]"},
        {L"ZoomCycle",L"[Menu + R Grip]"},
        {L"Duck",L"[L Stick Click]"},{L"Jump",L"[R Stick Click]"},
        {L"MoveForward",L"[L Stick Up]"},{L"MoveBackward",L"[L Stick Down]"},
        {L"StrafeLeft",L"[L Stick Left]"},{L"StrafeRight",L"[L Stick Right]"},
        {L"Walking",L"[L Stick]"},{L"KeyTurnLeft",L"[R Stick Left]"},
        {L"KeyTurnRight",L"[R Stick Right]"},
        {L"NextWeaponOrPlasmid",L"[R Stick Down]"},{L"PreviousWeaponOrPlasmid",L"[R Stick Up]"},
        {L"PauseGame",L"[Menu]"},{L"PauseGameAlias",L"[Menu]"},
        {L"SecondaryAliasPauseGame",L"[Menu]"},
        {L"ShowContextHelp",L"[Menu + L Grip]"},{L"HintButtonAlias",L"[Menu + L Stick Click]"},
        {L"LaunchMapScreen",L"[Hold Menu]"},{L"CloseMaps",L"[B]"},
        {L"LaunchQuestsScreen",L"[Menu + X]"},{L"PlayOldestUnreadLog",L"[Menu + Y]"},
        {L"CloseContainer",L"[B]"},{L"CloseInventory",L"[B]"},{L"CloseHacking",L"[B]"},
        {L"CloseLogs",L"[B]"},{L"CloseVendingMachine",L"[B]"},
        {L"QuickSave",L"[Pause: Save]"},{L"QuickLoad",L"[Pause: Load]"}
    };
    for(const auto& b:bindings)if(!_wcsicmp(action.c_str(),b.action))return b.label;
    if((action.size()==12&&!_wcsnicmp(action.c_str(),L"EquipWeapon",11)&&action[11]>=L'1'&&action[11]<=L'8')||
       (action.size()==13&&!_wcsnicmp(action.c_str(),L"EquipAbility",12)&&action[12]>=L'1'&&action[12]<=L'6'))return L"[Menu + B]";
    return nullptr;
}

bool Replace(const GameString& source,std::wstring& text){
    if(!source.data||source.count<2||source.count>32768||source.capacity<source.count||
       IsBadReadPtr(source.data,source.count*sizeof(wchar_t))||source.data[source.count-1])return false;
    text.assign(source.data,source.count-1);bool changed=false;
    for(size_t at=0;at<text.size();){
        at=text.find(L'<',at);if(at==std::wstring::npos)break;
        if(text.size()-at<10||_wcsnicmp(text.c_str()+at,L"<Mapping=",9)){at++;continue;}
        const auto end=text.find(L'>',at+9);if(end==std::wstring::npos||end-at>128){at++;continue;}
        std::wstring action=text.substr(at+9,end-at-9);
        while(!action.empty()&&(action.back()==L' '||action.back()==L'\t'))action.pop_back();
        size_t begin=action.find_first_not_of(L" \t");if(begin!=std::wstring::npos)action.erase(0,begin);
        const wchar_t* label=Label(action);
        if(!label){static unsigned unknown=0;if(unknown++<12)Log("Quest prompt: unmapped action %ls; native formatter retained",action.c_str());at=end+1;continue;}
        const size_t length=wcslen(label);text.replace(at,end-at+1,label);at+=length;changed=true;
    }
    return changed;
}

GameString* __fastcall Format(void* self,void* edx,GameString* result,GameString description,GameString topic){
    std::wstring text;
    if(g_config.enabled&&g_config.questControllerBindings&&Replace(description,text)){
        // Native assignment copies into the game's allocator. The original
        // formatter destroys this by-value argument exactly once on return.
        const GameString borrowed{const_cast<wchar_t*>(text.c_str()),int(text.size()+1),int(text.size()+1)};
        assign(&description,nullptr,&borrowed);
        static unsigned count=0;if(count++<8)Log("Quest prompt: controller action tags substituted before native button images");
    }
    return original(self,edx,result,description,topic);
}
}
bool Install(){
    if(!g_config.questControllerBindings)return true;
    if(IsBadReadPtr((void*)kFormat,sizeof(formatBytes))||memcmp((void*)kFormat,formatBytes,sizeof(formatBytes))||
       IsBadReadPtr((void*)kAssign,sizeof(assignBytes))||memcmp((void*)kAssign,assignBytes,sizeof(assignBytes))){
        Log("Quest prompts: native formatter/string ABI unavailable; original labels retained");return false;
    }
    if(MH_CreateHook((void*)kFormat,(void*)Format,(void**)&original)!=MH_OK||MH_EnableHook((void*)kFormat)!=MH_OK)return false;
    Log("Quest prompts: native action-tag formatter hooked, controller labels replace keyboard/mouse icons");return true;
}
}
