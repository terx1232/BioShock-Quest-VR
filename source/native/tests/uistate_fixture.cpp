// Replay real context parser; never starts BioShock or sends input.
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include "../src/uistate.cpp"
void Log(const char*,...){}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*){return MH_OK;}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID){return MH_OK;}
namespace uobject {bool BoolProperty(void*,const wchar_t* name,int& offset,unsigned& mask){if(wcscmp(name,L"bCinematicMode"))return false;offset=0x40;mask=8;return true;}}
void Check(bool ok,const char* msg){if(!ok){fprintf(stderr,"FAIL: %s\n",msg);exit(1);}}
int main(){
    InitializeCriticalSection(&uistate::g_lock);
    uistate::Observe(L"SetInputContexts Default");Check(!uistate::MenuOpen(),"gameplay");
    uistate::Observe(L"PushInputContext ContainerUIActive");Check(uistate::ContainerOpen()&&!uistate::MenuOpen(),"looting preserves world camera and gameplay input");
    uistate::Observe(L"PushInputContext PauseUIActive");Check(uistate::MenuOpen(),"pause above loot remains cinema");
    uistate::Observe(L"PopInputContext PauseUIActive");Check(!uistate::MenuOpen()&&uistate::ContainerOpen(),"return to loot without new mode switch");
    uistate::Observe(L"PopInputContext ContainerUIActive");Check(!uistate::ContainerOpen(),"normal loot exit");
    uistate::Observe(L"PushInputContext NullInput");Check(uistate::Cinematic()&&!uistate::MenuOpen(),"scripted camera stays immersive");
    uistate::Observe(L"PushInputContext PauseUIActive");Check(!uistate::Cinematic()&&uistate::MenuOpen(),"pause above cinematic");
    uistate::Observe(L"setinputcontexts Default");Check(!uistate::MenuOpen()&&!uistate::Cinematic(),"case-insensitive exit");
    unsigned char controller[128]{};*reinterpret_cast<void**>(controller+0x2c)=reinterpret_cast<void*>(1);
    *reinterpret_cast<unsigned*>(controller+0x40)=8;uistate::UpdateCinematic(controller);Check(uistate::Cinematic(),"native cinematic flag");
    *reinterpret_cast<unsigned*>(controller+0x40)=0;uistate::UpdateCinematic(controller);Check(!uistate::Cinematic(),"native cinematic ends");
    puts("PASS: real UI parser, persistent world loot, nested pause, cinematic context/native flag, normal exit");
}
