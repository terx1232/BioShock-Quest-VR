#include "../src/mouse.cpp"
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
Config g_config;
namespace {int rayX=500,rayY=300;bool trigger=false,enabled=false;}
namespace xrapi {bool NativeCinemaReady(){return true;}bool NativePointer(int& x,int& y,bool& down){x=rayX;y=rayY;down=trigger;return true;}}
namespace devharness {bool Point(float&,float&){return false;}bool Button(int){return false;}}
namespace input {bool KeyHeld(int){return false;}}
namespace flashpointer {bool Ready(){return true;}bool Install(){return true;}void Target(bool a,int,int,int,int){enabled=a;}}
void Log(const char*,...){}
void Check(bool ok,const char* why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(1);}}
HRESULT STDMETHODCALLTYPE RealData(IDirectInputDevice8W*,DWORD,LPDIDEVICEOBJECTDATA,LPDWORD count,DWORD){*count=0;return DI_OK;}
HRESULT STDMETHODCALLTYPE RealState(IDirectInputDevice8W*,DWORD,LPVOID){return DI_OK;}
int main(){
    auto* device=reinterpret_cast<IDirectInputDevice8W*>(0x99);
    mouse::g_mice[0]=device;mouse::g_miceCount=1;mouse::g_getData=RealData;mouse::g_getState=RealState;
    mouse::SetScreenSize(1280,720);mouse::Point(true,1,1,false);
    Check(enabled&&mouse::g_x==500&&mouse::g_y==300,"fresh BSQA ray replaces old Point coordinates");
    auto take=[&](DWORD flags,DWORD expectedOfs,bool present=true){
        DIDEVICEOBJECTDATA event{};DWORD count=1;
        Check(mouse::HookedGetData(device,sizeof(event),&event,&count,flags)==DI_OK,"buffered mouse call");
        Check(count==(present?1u:0u),"bounded event count");
        if(present)Check(event.dwOfs==expectedOfs&&event.dwData==1,"one normal axis event, never a clamp/calibration move");
    };
    take(DIGDD_PEEK,DIMOFS_X);take(DIGDD_PEEK,DIMOFS_X);take(0,DIMOFS_X);
    take(DIGDD_PEEK,DIMOFS_Y);take(0,DIMOFS_Y);take(0,0,false);take(DIGDD_PEEK,0,false);
    mouse::Point(true,1,1,false);take(0,DIMOFS_X);take(0,DIMOFS_Y);take(0,0,false);
    mouse::Point(true,1,1,false);DIMOUSESTATE state{};
    mouse::HookedGetState(device,sizeof(state),&state);Check(state.lX==1&&state.lY==1,"state path consumes same queued axes");
    mouse::HookedGetState(device,sizeof(state),&state);Check(state.lX==0&&state.lY==0,"state polls do not regenerate movement");
    mouse::Point(false,0,0,false);Check(!enabled,"leaving menu disables absolute path");
    puts("PASS: production mouse BSQA target, stationary frame sync, one-event buffers, repeated PEEK/consume and state polling; no calibration moves");
}
