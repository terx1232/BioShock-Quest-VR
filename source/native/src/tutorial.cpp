#include "tutorial.h"
#include <windows.h>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>
#include "MinHook.h"
#include "config.h"
#include "log.h"
#include "uobject.h"
#include "input.h"

namespace tutorial {
namespace {
// BioShock 1.1: native TriggerTrainingMessage, called by its VM wrapper
// 0x10c8aa00 after decoding FName (two DWORDs) and TrainingMessageTrigger*.
// FireWeapon is the localized "swing your WRENCH" tutorial in ShockGame.U.
// Rejecting this queue entry prevents the whole Flash panel, including the
// substituted mouse-button image, without hiding other training or HUD.
constexpr uintptr_t kTrigger=0x10c8a5c0;
const unsigned char kBytes[]={0x6a,0xff,0x68,0x78,0xc4,0xf4,0x10,0x64,0xa1,0,0,0,0};
using TriggerFn=int(__fastcall*)(void*,void*,int,int,void*);
TriggerFn original=nullptr;
struct GameString {wchar_t* data;int count,capacity;};
// The native AddTextToTraining wrapper calls this borrowed-string event
// dispatcher (0x10c19949). Adaptive messages can reach it without calling
// TriggerTrainingMessage, so filter the final Training event as well.
constexpr uintptr_t kDispatch=0x10c18d00;
const unsigned char kDispatchBytes[]={0x6a,0xff,0x68,0x18,0x28,0xf5,0x10,0x64,0xa1,0,0,0,0};
using DispatchFn=void(__fastcall*)(void*,void*,const GameString*,int,int);
DispatchFn dispatchOriginal=nullptr;
// AActor::TriggerEffectEvent, vtable slot 0x288 used by the real
// ShockPlayer.TriggerDamageIndicatorsFromActor implementation. The native
// function consumes 48 stack bytes; forward the complete argument block.
constexpr uintptr_t kEffect=0x10942b50;
const unsigned char kEffectBytes[]={0x8b,0xc1,0x8b,0x88,0xe0,0,0,0,0xb2,1};
struct EffectArgs {unsigned words[12];};
static_assert(sizeof(EffectArgs)==48,"native effect event ABI");
using EffectFn=int(__fastcall*)(void*,void*,EffectArgs);
EffectFn effectOriginal=nullptr;
// execReactToDamage decodes its arguments, then calls Pawn.TriggerEffectEvent
// at 0x10c95911 (return 0x10c95913). Health/OnDamaged is handled before this
// cosmetic reaction. Match this caller, never every event with a "hit" name.
constexpr uintptr_t kReceivedDamageReturn=0x10c95913;
int EffectFrom(void* self,void* edx,EffectArgs args,uintptr_t caller){
    const wchar_t* name=uobject::NameByIndex(args.words[0]);
    if(g_config.enabled&&g_config.hideDamageOverlay&&caller==kReceivedDamageReturn){
        static unsigned logged=0;
        if(logged++<8)Log("Combat HUD: received-hit visual event %ls suppressed at ReactToDamage; native health/damage retained",name?name:L"?");
        return 0;
    }
    if(g_config.enabled&&g_config.hideAttackTutorial&&name&&!_wcsnicmp(name,L"DamageIndicator",15)){
        static bool logged=false;if(!logged){logged=true;Log("Combat HUD: native damage indicator overlays suppressed");}
        return 0;
    }
    return effectOriginal(self,edx,args);
}
int __fastcall Effect(void* self,void* edx,EffectArgs args){
    return EffectFrom(self,edx,args,(uintptr_t)__builtin_return_address(0));
}
bool AttackText(const GameString* value){
    if(!value||IsBadReadPtr(value,sizeof(*value))||!value->data||value->count<2||value->count>32768||
       value->capacity<value->count||IsBadReadPtr(value->data,value->count*sizeof(wchar_t)))return false;
    std::wstring text(value->data,value->count-1);
    for(auto& c:text){if((c>=L'A'&&c<=L'Z')||(c>=L'А'&&c<=L'Я'))c+=32;else if(c==L'Ё')c=L'ё';}
    auto has=[&](const wchar_t* word){return text.find(word)!=std::wstring::npos;};
    return has(L"mapping=fire")||has(L"switchandfireweapon")||has(L"mapping=switchweaponsorplasmids")||has(L"to swing")||
           has(L"to attack")||has(L"to equip weapon")||has(L"to equip a weapon")||
           (has(L"mouse")&&has(L"right"))||
           (has(L"наж")&&(has(L"атак")||has(L"удар")))||
           (has(L"мыш")&&has(L"прав"));
}
void __fastcall Dispatch(void* self,void* edx,const GameString* value,int name,int number){
    const wchar_t* method=uobject::NameByIndex(name);
    if(g_config.enabled&&g_config.hideAttackTutorial&&method&&!_wcsicmp(method,L"Training")&&(input::CombatActive()||AttackText(value))){
        Log("Training: final combat popup blocked together with text/button panel");
        return;
    }
    const bool focus=method&&(!_wcsicmp(method,L"UseFocus")||!_wcsicmp(method,L"Focus")||!_wcsicmp(method,L"FocusNoUse"));
    if(g_config.enabled&&g_config.hideAttackTutorial&&focus&&AttackText(value)){
        // UseFocus("") takes the movie's complete clear/fade path, removing
        // both its caption and button image instead of leaving a stale clip.
        wchar_t blank[]={0};GameString clear{blank,1,1};
        dispatchOriginal(self,edx,&clear,name,number);
        Log("Combat HUD: mouse attack focus cleared with its button image");return;
    }
    dispatchOriginal(self,edx,value,name,number);
}
int __fastcall Trigger(void* self,void* edx,int name,int number,void* source){
    const wchar_t* text=uobject::NameByIndex(name);
    if(g_config.enabled&&g_config.hideAttackTutorial&&text&&!_wcsicmp(text,L"FireWeapon")){
        static bool logged=false;
        if(!logged){logged=true;Log("Training: FireWeapon attack tutorial suppressed (text and button image)");}
        return 0;
    }
    return original(self,edx,name,number,source);
}
}
bool Install(){
    if(!g_config.hideAttackTutorial&&!g_config.hideDamageOverlay)return true;
    if(IsBadReadPtr((void*)kTrigger,sizeof(kBytes))||memcmp((void*)kTrigger,kBytes,sizeof(kBytes))){
        Log("Training: native trigger signature unavailable; original tutorials retained");return false;
    }
    if(MH_CreateHook((void*)kTrigger,(void*)Trigger,(void**)&original)!=MH_OK||MH_EnableHook((void*)kTrigger)!=MH_OK){
        Log("Training: native trigger hook failed");return false;
    }
    Log("Training: targeted FireWeapon tutorial filter installed");
    if(IsBadReadPtr((void*)kDispatch,sizeof(kDispatchBytes))||memcmp((void*)kDispatch,kDispatchBytes,sizeof(kDispatchBytes))||
       MH_CreateHook((void*)kDispatch,(void*)Dispatch,(void**)&dispatchOriginal)!=MH_OK||MH_EnableHook((void*)kDispatch)!=MH_OK){
        Log("Training: final popup dispatcher unavailable");return false;
    }
    Log("Training: final Training popup filter installed (borrowed string, complete panel)");
    if(IsBadReadPtr((void*)kEffect,sizeof(kEffectBytes))||memcmp((void*)kEffect,kEffectBytes,sizeof(kEffectBytes))||
       MH_CreateHook((void*)kEffect,(void*)Effect,(void**)&effectOriginal)!=MH_OK||MH_EnableHook((void*)kEffect)!=MH_OK){
        Log("Combat HUD: damage event filter unavailable");return false;
    }
    const unsigned char call[]={0xff,0xd0};
    if(g_config.hideDamageOverlay&&(IsBadReadPtr((void*)(kReceivedDamageReturn-2),2)||memcmp((void*)(kReceivedDamageReturn-2),call,2))){
        g_config.hideDamageOverlay=false;Log("Combat HUD: received-hit caller signature mismatch; targeted visual filter disabled");
    }
    Log("Combat HUD: damage event filter installed (48-byte native argument block); received-hit visual caller=%08x enabled=%d",unsigned(kReceivedDamageReturn),g_config.hideDamageOverlay);return true;
}
}
