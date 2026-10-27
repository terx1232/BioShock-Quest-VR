#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include "../src/tutorial.cpp"
Config g_config;unsigned calls=0;void* expected=nullptr;
bool combat=false;unsigned dispatched=0;bool clearedFocus=false;
unsigned effects=0;tutorial::EffectArgs expectedEffect{};
namespace input {bool CombatActive(){return combat;}}
void Log(const char*,...){}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*){return MH_OK;}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID){return MH_OK;}
namespace uobject {const wchar_t* NameByIndex(int i){switch(i){case 1:return L"FireWeapon";case 2:return L"Reload";case 3:return L"WeaponMode";case 4:return L"fireweapon";case 5:return L"Training";case 6:return L"Subtitle";case 7:return L"DamageIndicatorUp";case 8:return L"MuzzleFlash";case 9:return L"DamageIndicatorLeft";case 10:return L"UseFocus";case 11:return L"Focus";case 12:return L"FocusNoUse";default:return nullptr;}}}
int __fastcall Original(void* self,void*,int name,int number,void* source){
    if(self!=expected||source!=expected||number!=7){puts("FAIL: native parameters changed");exit(1);}
    calls++;return name+100;
}
void Check(bool ok,const char* why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(1);}}
void __fastcall OriginalDispatch(void* self,void*,const tutorial::GameString* value,int method,int number){Check(self==expected&&number==7,"native dispatcher parameters retained");if(method>=10&&method<=12)clearedFocus=value->count==1&&!value->data[0];dispatched++;}
int __fastcall OriginalEffect(void* self,void*,tutorial::EffectArgs args){Check(self==expected&&!memcmp(&args,&expectedEffect,sizeof(args)),"all 48 native event argument bytes retained");effects++;return 123;}
int main(){
    expected=reinterpret_cast<void*>(0x1234);tutorial::original=Original;
    g_config.enabled=g_config.hideAttackTutorial=true;
    Check(tutorial::Trigger(expected,nullptr,1,7,expected)==0&&calls==0,"whole attack tutorial rejected before Flash queue");
    Check(tutorial::Trigger(expected,nullptr,4,7,expected)==0&&calls==0,"case-insensitive FName");
    for(int name : {0,2,3})Check(tutorial::Trigger(expected,nullptr,name,7,expected)==name+100,"other tutorials and unknown names retain native result");
    g_config.hideAttackTutorial=false;Check(tutorial::Trigger(expected,nullptr,1,7,expected)==101,"setting restores native tutorial");
    g_config.hideAttackTutorial=true;g_config.enabled=false;Check(tutorial::Trigger(expected,nullptr,1,7,expected)==101,"disabled mod preserves game");
    Check(calls==5,"only exact attack tutorial suppressed");
    tutorial::dispatchOriginal=OriginalDispatch;g_config.enabled=true;
    auto dispatch=[&](const wchar_t* text,int method=5){
        tutorial::GameString value{const_cast<wchar_t*>(text),int(wcslen(text))+1,int(wcslen(text))+1};
        tutorial::Dispatch(expected,nullptr,&value,method,7);
        Check(value.data==text&&value.count==int(wcslen(text))+1,"borrowed string ownership and contents retained");
    };
    dispatch(L"Press <img src=Mouse_Right> to swing your WRENCH.");
    dispatch(L"НАЖМИТЕ ПРАВУЮ КНОПКУ МЫШИ ДЛЯ АТАКИ.");
    dispatch(L"Press <Mapping=Fire> to kill the STUNNED enemy.");
    Check(dispatched==0,"PC and localized attack panels cannot reach Flash");
    dispatch(L"Press A to search");Check(dispatched==1,"ordinary noncombat interaction retained");
    combat=true;dispatch(L"An unrelated adaptive tutorial");Check(dispatched==1,"no new training panel during combat");
    dispatch(L"Press right mouse to attack",6);Check(dispatched==2,"subtitles stay visible even during combat");
    g_config.hideAttackTutorial=false;dispatch(L"Press right mouse to attack");Check(dispatched==3,"disabled filter preserves final native event");
    g_config.hideAttackTutorial=true;dispatch(L"НАЖМИТЕ ПРАВУЮ КНОПКУ МЫШИ ДЛЯ УДАРА",10);Check(clearedFocus,"UseFocus attack instruction uses native empty-panel path");
    clearedFocus=false;dispatch(L"Обыскать контейнер",10);Check(!clearedFocus,"ordinary loot focus is untouched");
    for(int method:{10,11,12}){
        clearedFocus=false;dispatch(L"Press <Mapping=SwitchAndFireWeapon> to swing",method);Check(clearedFocus,"attack caption cleared through every native focus channel");
        clearedFocus=false;dispatch(L"Обыскать контейнер",method);Check(!clearedFocus,"normal focus/loot label retained in each channel");
    }
    tutorial::effectOriginal=OriginalEffect;g_config.hideAttackTutorial=true;
    for(unsigned i=0;i<12;i++)expectedEffect.words[i]=0x12340000+i;
    expectedEffect.words[0]=7;Check(tutorial::Effect(expected,nullptr,expectedEffect)==0&&effects==0,"whole damage indicator effect blocked");
    expectedEffect.words[0]=9;Check(tutorial::Effect(expected,nullptr,expectedEffect)==0&&effects==0,"all native indicator directions blocked");
    expectedEffect.words[0]=8;Check(tutorial::Effect(expected,nullptr,expectedEffect)==123&&effects==1,"world muzzle effect and full native result retained");
    g_config.hideAttackTutorial=false;expectedEffect.words[0]=7;Check(tutorial::Effect(expected,nullptr,expectedEffect)==123&&effects==2,"disabled filter retains all effects");
    g_config.hideDamageOverlay=true;expectedEffect.words[0]=8;
    Check(tutorial::EffectFrom(expected,nullptr,expectedEffect,0x10c95913)==0&&effects==2,"received-hit visual is blocked even when its event is not named DamageIndicator");
    Check(tutorial::EffectFrom(expected,nullptr,expectedEffect,0x10c95914)==123&&effects==3,"same named world event from a different caller is untouched");
    g_config.hideDamageOverlay=false;
    Check(tutorial::EffectFrom(expected,nullptr,expectedEffect,0x10c95913)==123&&effects==4,"disabled received-hit visual filter forwards complete original arguments");
    g_config.hideDamageOverlay=true;g_config.enabled=false;
    Check(tutorial::EffectFrom(expected,nullptr,expectedEffect,0x10c95913)==123&&effects==5,"disabled mod preserves received-hit effects");
    puts("PASS: actual native attack tutorial filter, unknown names, other tutorials, full native parameters/results, disabled setting/mod");
    puts("PASS: final borrowed-string HUD event, localized mouse icon/text removal, combat training suppression, subtitles retained");
    puts("PASS: damage indicator visuals blocked; world effects, complete 48-byte native argument ABI and results retained");
}
