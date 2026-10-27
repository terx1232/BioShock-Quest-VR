// Resize through the game's own Exec/Reset path, on its window thread.
// Present only posts a request; it never flushes or resets its own render queue.
#include "worldresolution.h"
#include "log.h"

namespace worldresolution {
namespace {
HWND window=nullptr;
WNDPROC previous=nullptr;
UINT message=0;
Resize resize=nullptr;
Safety safety=nullptr;
volatile LONG deferred=0;
LONG menu=0;
volatile LONG world=0;
volatile LONG actual=0,desired=0,queued=0,attempted=0;
bool lastGame=false;
bool worldWindowed=false;
DWORD gameSince=0;
LONG pack(unsigned w,unsigned h){return static_cast<LONG>((w<<16)|h);}
unsigned width(LONG value){return (static_cast<unsigned>(value)>>16)&0xffff;}
unsigned height(LONG value){return static_cast<unsigned>(value)&0xffff;}
bool Safe(){
    if(safety&&!safety()){
        if(!InterlockedExchange(&deferred,1))Log("Resolution: deferred during level travel; viewport player detached");
        return false;
    }
    if(InterlockedExchange(&deferred,0))Log("Resolution: viewport player reattached; pending size may resume");
    return true;
}
LRESULT CALLBACK procedure(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp){
    if(msg==message&&wp==0x42535652){
        InterlockedExchange(&queued,0);
        LONG target=InterlockedCompareExchange(&desired,0,0);
        // Read the current request here: a queued world resize must not win
        // over a pause/menu that was requested while the message was pending.
        if(target&&target!=actual&&target!=attempted&&resize){
            // Re-check on dispatch: a request can have been queued before the
            // old level detached its controller. Never enter UPlayer::Exec then.
            if(!Safe())return 0;
            InterlockedExchange(&attempted,target);
            bool accepted=resize(width(target),height(target),worldWindowed&&target==world&&target!=menu);
            if(!accepted&&!Safe()){InterlockedExchange(&attempted,0);return 0;}
            Log("Resolution: request %ux%u accepted=%d, device %ux%u",width(target),height(target),accepted,width(actual),height(actual));
            if(!accepted||actual!=target){
                // One fallback; do not retry a failed resize every Present.
                InterlockedExchange(&world,menu);InterlockedExchange(&desired,menu);
                if(actual!=menu)resize(width(menu),height(menu),false);
                Log("Resolution: world upgrade disabled after unsuccessful Reset");
            }
        }
        return 0;
    }
    return CallWindowProc(previous,hwnd,msg,wp,lp);
}
}
void Attach(HWND hwnd,unsigned mw,unsigned mh,unsigned ww,unsigned wh,Resize callback,bool windowed,Safety guard){
    if(!hwnd||!callback||ww<640||wh<480||ww>4096||wh>2160)return;
    if(window==hwnd)return;
    if(window&&previous&&IsWindow(window))SetWindowLongPtr(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(previous));
    message=RegisterWindowMessageA("BioShockVR.WorldResolution.v1");if(!message)return;
    window=hwnd;menu=pack(mw,mh);world=pack(ww,wh);resize=callback;worldWindowed=windowed;safety=guard;
    SetLastError(0);
    previous=reinterpret_cast<WNDPROC>(SetWindowLongPtr(hwnd,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(procedure)));
    if(!previous){window=nullptr;return;}
    Log("Resolution: menu %ux%u, world frame %ux%u; window thread %lu",mw,mh,ww,wh,GetWindowThreadProcessId(hwnd,nullptr));
}
void DeviceReady(unsigned w,unsigned h){InterlockedExchange(&actual,pack(w,h));}
bool WorldReady(){return !window||InterlockedCompareExchange(&actual,0,0)==InterlockedCompareExchange(&world,0,0);}
bool MenuReady(){return !window||InterlockedCompareExchange(&actual,0,0)==menu;}
unsigned MenuWidth(){return width(menu);}
unsigned MenuHeight(){return height(menu);}
void Update(bool inGame,DWORD now){
    if(!window)return;
    if(inGame!=lastGame){lastGame=inGame;gameSince=now;}
    // Brief camera gaps while the engine flushes Reset are not a real menu.
    LONG upgraded=InterlockedCompareExchange(&world,0,0);
    LONG target=inGame?(actual==upgraded||now-gameSince>=2000?upgraded:menu)
                        :(now-gameSince<300?InterlockedCompareExchange(&desired,0,0):menu);
    if(!target)target=menu;
    LONG old=InterlockedExchange(&desired,target);
    if(old!=target)InterlockedExchange(&attempted,0);
    if(target!=actual&&!Safe())return;
    if(target!=actual&&target!=attempted&&!InterlockedCompareExchange(&queued,1,0)){
        if(!PostMessage(window,message,0x42535652,0))InterlockedExchange(&queued,0);
    }
}
}
