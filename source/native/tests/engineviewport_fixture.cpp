#include "../src/engineviewport.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>

void Log(const char* format,...){va_list args;va_start(args,format);vprintf(format,args);va_end(args);puts("");}
void check(bool value,const char* reason){if(!value){fprintf(stderr,"FAIL: %s\n",reason);exit(1);}}
BYTE* makeViewport(){
    BYTE* p=(BYTE*)VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    check(p!=nullptr,"allocate synthetic viewport");
    *(DWORD*)p=0x10fd71f4;*(DWORD*)(p+0x3c)=0x10fd71d4;
    *(unsigned*)(p+0xa4)=1280;*(unsigned*)(p+0xa8)=708;return p;
}
void waitReady(){for(int i=0;i<100&&!engineviewport::Ready();i++)Sleep(50);check(engineviewport::Ready(),"discover after graphics creation");}
int main(){
    BYTE* object=makeViewport();
    // The viewport can exist before graphics and the headset can stay asleep.
    // Discovery does not consume a timeout budget before DeviceCreated.
    check(!engineviewport::Ready(),"wait for real graphics initialization");
    check(!engineviewport::SetSize(720,720,1280,720),"reject resize before device");
    engineviewport::DeviceCreated();waitReady();
    *(void**)(object+0x44)=(void*)1; // the native crash path had Input but no Actor
    check(!engineviewport::CanResize(),"detached player blocks Exec even with a valid viewport/input");
    check(!engineviewport::Resize(1280,720,false),"TEMPSETRES never enters native Exec with a null Actor");
    BYTE* controller=(BYTE*)VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    *(void**)(object+0x40)=controller;
    check(engineviewport::CanResize(),"reattached controller permits resize even before Pawn possession");
    VirtualFree(controller,0,MEM_RELEASE);check(!engineviewport::CanResize(),"unreadable controller rejected");
    *(void**)(object+0x40)=nullptr;
    check(engineviewport::SetSize(720,720,1280,720),"select gameplay rectangle");
    check(engineviewport::Matches(720,720)&&*(unsigned*)(object+0xa4)==720,"actual engine resized");
    engineviewport::DeviceCreated();
    check(engineviewport::Ready()&&engineviewport::Matches(720,720),"native Reset retains validated viewport");
    check(engineviewport::SetSize(656,656,1280,720),"dynamic step");
    check(engineviewport::SetSize(720,720,1280,720),"dynamic step restored");
    check(engineviewport::SetSize(1280,720,1280,720),"full menu restored");
    *(unsigned*)(object+0xa4)=1920;
    check(!engineviewport::SetSize(720,720,1280,720),"reject unowned size");
    check(!engineviewport::Matches(720,720),"crop guard rejects wrong engine size");
    VirtualFree(object,0,MEM_RELEASE);
    check(!engineviewport::Ready(),"stale object rejected after reset");
    object=makeViewport();engineviewport::DeviceCreated();waitReady();
    check(engineviewport::SetSize(720,720,1280,720),"rediscovery after reset");
    check(engineviewport::Height()==720,"height matches confirmed engine rectangle");
    *(unsigned*)(object+0xa4)=1920;*(unsigned*)(object+0xa8)=1080;
    engineviewport::DeviceCreated();
    check(engineviewport::SetSize(864,864,1920,1080),"native larger framebuffer accepts game rectangle");
    check(engineviewport::SetSize(1280,720,1920,1080),"menu rectangle after larger game frame");
    puts("PASS: graphics-gated viewport discovery, actual size, dynamic/menu restoration, stale object guard");
    return 0;
}
