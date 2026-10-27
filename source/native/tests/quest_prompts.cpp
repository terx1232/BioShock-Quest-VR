// Exercise the production native handler with explicit FString ownership.
#include <cstdio>
#include <cstdlib>
#include <set>
#include "../src/questprompts.cpp"
Config g_config;
void Log(const char*,...){}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*){return MH_OK;}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID){return MH_OK;}
using S=questprompts::GameString;
std::set<void*> owned;
unsigned copies=0,formats=0;
void Check(bool ok,const char* why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(1);}}
void Destroy(S& s){if(s.data){Check(owned.erase(s.data)==1,"native string allocation freed exactly once");free(s.data);}s={};}
S Make(const wchar_t* text){S s{};s.count=s.capacity=int(wcslen(text)+1);s.data=(wchar_t*)malloc(s.count*2);Check(s.data!=nullptr,"fixture allocation");memcpy(s.data,text,s.count*2);owned.insert(s.data);return s;}
S* __fastcall Assign(S* target,void*,const S* source){
    auto copy=Make(source->data);Destroy(*target);*target=copy;copies++;return target;
}
std::wstring expected;
S* __fastcall Original(void* self,void* edx,S* result,S description,S topic){
    Check(self==(void*)0x1234&&edx==(void*)0x5678,"native this/edx preserved");
    Check(description.data&&std::wstring(description.data)==expected,"native formatter receives controller text and untouched unknown tags");
    Check(topic.data&&std::wstring(topic.data)==L"Search / hack","second owned FString and stack arguments preserved");
    *result=Make(description.data);Destroy(description);Destroy(topic);formats++;return result;
}
int main(){
    questprompts::assign=Assign;questprompts::original=Original;g_config.enabled=g_config.questControllerBindings=true;
    auto run=[&](const wchar_t* source,const wchar_t* wanted){
        auto description=Make(source),topic=Make(L"Search / hack");S result{};expected=wanted;
        volatile unsigned canary=0x1234abcd;
        Check(questprompts::Format((void*)0x1234,(void*)0x5678,&result,description,topic)==&result,"native result address returned");
        Check(canary==0x1234abcd&&std::wstring(result.data)==wanted,"fastcall stack/result ABI retained");
        Destroy(result);Check(owned.empty(),"no borrowed memory freed, no string leak or duplicate release");
    };
    run(L"Search (<Mapping=Use>) / hack (<Mapping=Hack>)",L"Search ([A]) / hack ([L Grip])");
    run(L"<b>Обыскать</b>: <mapping= use >; <Mapping=MedHypo>, <Mapping=InjectBioAmmo>",L"<b>Обыскать</b>: [A]; [X], [Y]");
    run(L"<Mapping=ReRollContainer> <Mapping=ShowContextHelp> <Mapping=LaunchMapScreen>",L"[R Trigger] [Menu + L Grip] [Hold Menu]");
    run(L"<Mapping=EquipWeapon8> <Mapping=EquipAbility6> <Mapping=ZoomCycle>",L"[Menu + B] [Menu + B] [Menu + R Grip]");
    run(L"<Mapping=FutureAction> / <Mapping=Use>",L"<Mapping=FutureAction> / [A]");
    const auto changed=copies;
    run(L"<Mapping=Unknown> <img src=QuestIcon> plain text",L"<Mapping=Unknown> <img src=QuestIcon> plain text");
    run(L"broken <Mapping=Use",L"broken <Mapping=Use");
    Check(copies==changed,"ordinary/unknown/malformed text performs no native reallocation");
    g_config.questControllerBindings=false;run(L"<Mapping=Use>",L"<Mapping=Use>");
    g_config.questControllerBindings=true;g_config.enabled=false;run(L"<Mapping=Hack>",L"<Mapping=Hack>");
    Check(formats==9,"every call reaches the native formatter once");
    std::wstring text;S invalid{(wchar_t*)1,100,100};Check(!questprompts::Replace(invalid,text),"invalid string safely falls back");
    Check(!questprompts::Label(L"EquipWeapon9")&&!questprompts::Label(L"EquipAbility7"),"unknown inventory slots do not claim a controller mapping");
    puts("PASS: production Quest action tags, localized/HTML text, all native argument bytes, native-owned strings freed once, unknown/malformed/disabled fallback");
}
