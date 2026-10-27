#include "../src/worldresolution.h"
#include "../src/dynamicresolution.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
static DWORD owner;static int calls;static bool fail=false;
static bool attached=true,detachInside=false;
bool safe(){return attached;}
static unsigned lastW,lastH;
void Log(const char* f,...){va_list a;va_start(a,f);vprintf(f,a);va_end(a);puts("");}
void check(bool b,const char* why){if(!b){fprintf(stderr,"FAIL: %s\n",why);exit(1);}}
bool resize(unsigned w,unsigned h,bool windowed){
    check(GetCurrentThreadId()==owner,"resize dispatched on window thread");
    check(windowed==(w==1920),"world windowed, menu fullscreen");
    ++calls;lastW=w;lastH=h;
    if(detachInside){attached=false;return false;}
    if(fail)return false;
    worldresolution::DeviceReady(w,h);return true;
}
void pump(){MSG m;while(PeekMessage(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessage(&m);}}
DWORD WINAPI request(void*){worldresolution::Update(true,1000);worldresolution::Update(true,3100);return 0;}
int main(){
    DynamicResolution quality;
    check(quality.Configure(720,true)==720,"initial menu capacity");
    check(quality.Configure(864,true)==864,"larger game buffer starts at new quality");
    check(quality.Adjust(40,-1,720,72)==864,"unknown load retains new quality");
    check(quality.Adjust(40,95,720,72)==800,"measured saturation may reduce quality");
    check(quality.Adjust(40,95,864,72)==864,"quality floor is honoured");
    check(quality.Configure(720,true)==720,"menu shrink clamps source");
    check(quality.Configure(864,false)==864,"fixed resolution uses current capacity");
    owner=GetCurrentThreadId();
    HWND window=CreateWindowA("STATIC","Resolution fixture",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandle(nullptr),nullptr);
    check(window!=nullptr,"create host-only hidden window");
    worldresolution::Attach(window,1280,720,1920,1080,resize,true,safe);
    worldresolution::DeviceReady(1280,720);
    check(!worldresolution::WorldReady(),"loading panel retained before real world device");
    check(worldresolution::MenuReady(),"actual startup menu may be published");
    worldresolution::Update(false,0);pump();check(calls==0,"startup menu untouched");
    HANDLE worker=CreateThread(nullptr,0,request,nullptr,0,nullptr);WaitForSingleObject(worker,1000);CloseHandle(worker);
    check(calls==0,"Present thread only posts");pump();
    check(calls==1&&lastW==1920&&lastH==1080,"world resize applied");
    check(worldresolution::WorldReady(),"stereo handoff may start after actual device resize");
    check(!worldresolution::MenuReady(),"world-sized pause frame must not be copied to native menu");
    worldresolution::Update(false,3200);pump();check(calls==1,"brief camera gap ignored");
    worldresolution::Update(true,3300);pump();check(calls==1,"world retained when brief gap ends");
    worldresolution::Update(false,3400);worldresolution::Update(false,3800);pump();check(calls==2&&lastW==1280&&lastH==720,"pause restores menu");
    check(!worldresolution::WorldReady(),"menu device cannot be published as upgraded world");
    check(worldresolution::MenuReady(),"pause may publish menu only after menu device reset");
    worldresolution::Update(true,4000);worldresolution::Update(true,6100);
    worldresolution::Update(false,6200);worldresolution::Update(false,6600);pump();
    check(calls==2,"stale queued world request cancelled by menu");
    worldresolution::Update(true,7000);worldresolution::Update(true,9100);attached=false;pump();
    check(calls==2&&worldresolution::MenuReady(),"player detached after queue: no native Exec or fallback");
    worldresolution::Update(true,9200);pump();check(calls==2,"detached loader does not queue unsafe resizes");
    attached=true;worldresolution::Update(true,9300);pump();check(calls==3&&worldresolution::WorldReady(),"deferred request resumes after player reattaches");
    attached=false;worldresolution::Update(false,9400);worldresolution::Update(false,9800);pump();
    check(calls==3&&worldresolution::WorldReady(),"level travel keeps the live device while Actor is missing");
    attached=true;worldresolution::Update(true,9900);pump();check(calls==3,"new level cancels stale loading-menu size without a needless Reset");
    worldresolution::Update(false,10000);worldresolution::Update(false,10400);pump();check(calls==4&&worldresolution::MenuReady(),"real pause still returns to menu size");
    detachInside=true;worldresolution::Update(true,11000);worldresolution::Update(true,13100);pump();
    check(calls==5&&!worldresolution::WorldReady(),"late detach inside callback does not disable world resolution or enter fallback");
    attached=true;detachInside=false;worldresolution::Update(true,13200);pump();check(calls==6&&worldresolution::WorldReady(),"late-detached request remains retryable");
    worldresolution::Update(false,13400);worldresolution::Update(false,13800);pump();check(calls==7,"menu restored before genuine failure fixture");
    fail=true;worldresolution::Update(true,14000);worldresolution::Update(true,16100);pump();
    check(calls==8,"failed world resize attempted once");
    check(worldresolution::WorldReady(),"bounded fallback can hand off at its actual lower capacity");
    for(int i=0;i<100;++i){worldresolution::Update(true,17000+i*20);pump();}
    check(calls==8,"failed world request not retried every frame");
    DestroyWindow(window);
    puts("PASS: window-thread dispatch, warmup, pause restore, stale request, bounded fallback");
}
