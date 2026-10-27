#include "flashpointer.h"
#include "MinHook.h"
#include "log.h"
#include <windows.h>
#include <algorithm>
#include <cstring>

namespace flashpointer {
namespace {
constexpr uintptr_t inputAddress=0x109bb6f0;
using Input=void(__thiscall*)(void*,const unsigned char*,const unsigned char*,float);
Input original=nullptr;
SRWLOCK lock=SRWLOCK_INIT;
struct TargetPoint {bool active=false;int x=0,y=0,w=0,h=0;} target;
// Verified against the retail EXE: input's X branch adds to +f0, Y branch
// subtracts from +f4, both clamp to ActualPosition.W/H (+68/+6c).
struct Movie {int x,y,w,h,cursorX,cursorY;};
bool Read(void* self,Movie& m){
    BYTE* p=static_cast<BYTE*>(self);
    SIZE_T got=0;int position[4],cursor[2];
    if(!p||!ReadProcessMemory(GetCurrentProcess(),p+0x60,position,sizeof(position),&got)||got!=sizeof(position)||
       !ReadProcessMemory(GetCurrentProcess(),p+0xf0,cursor,sizeof(cursor),&got)||got!=sizeof(cursor))return false;
    m={position[0],position[1],position[2],position[3],cursor[0],cursor[1]};
    return m.w>=320&&m.w<=8192&&m.h>=240&&m.h<=8192&&
        m.cursorX>=0&&m.cursorX<=m.w&&m.cursorY>=0&&m.cursorY<=m.h;
}
void __fastcall InputHook(void* self,void*,const unsigned char* key,const unsigned char* action,float delta){
    TargetPoint point;AcquireSRWLockShared(&lock);point=target;ReleaseSRWLockShared(&lock);
    Movie movie{};bool corrected=false;
    if(point.active&&key&&(*key==0xe4||*key==0xe5)&&Read(self,movie)&&
       movie.x>=0&&movie.y>=0&&movie.x+movie.w<=point.w&&movie.y+movie.h<=point.h){
        int x=std::clamp(point.x-movie.x,0,movie.w-1);
        int y=std::clamp(point.y-movie.y,0,movie.h-1);
        // Change the normal axis argument, then let the game update its own
        // cursor and dispatch hover/clicks. Integer pixel error is exact even
        // where the upstream sensitivity discarded fractional DI motion.
        delta=float(*key==0xe4?x-movie.cursorX:movie.cursorY-y);
        corrected=true;
    }
    original(self,key,action,delta);
    static DWORD last=0;static void* previous=nullptr;
    if(corrected&&*key==0xe5&&(previous!=self||GetTickCount()-last>=5000)){
        last=GetTickCount();previous=self;
        Movie after{};if(Read(self,after))
            Log("Mouse: Flash absolute movie=%p rect=%d,%d %dx%d cursor=%d,%d ray=%d,%d error=%d,%d",self,
                after.x,after.y,after.w,after.h,after.cursorX,after.cursorY,point.x,point.y,
                after.x+after.cursorX-point.x,after.y+after.cursorY-point.y);
    }
}
}
bool Install(){
    if(original)return true;
    // Reject another EXE before attaching to an internal function. Both the
    // entry and actual X/Y writes are checked, not an isolated vtable value.
    struct Signature {uintptr_t address;const char* bytes;unsigned size;};
    const Signature signatures[]={
        {inputAddress,"\x8b\x44\x24\x04\x0f\xb6\x00\x56\x8b\xf1",10},
        {0x109bb73d,"\x8b\x86\xf4\x00\x00\x00",6},
        {0x109bb768,"\x89\x86\xf4\x00\x00\x00",6},
        {0x109bb77f,"\x03\x8e\xf0\x00\x00\x00",6},
        {0x109bb7a8,"\x89\x86\xf0\x00\x00\x00",6}};
    for(auto s:signatures){char bytes[16];SIZE_T got=0;
        if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(s.address),bytes,s.size,&got)||
           got!=s.size||memcmp(bytes,s.bytes,s.size)){
            Log("Mouse: Flash input signature mismatch at %08x; retaining relative fallback",unsigned(s.address));return false;
        }
    }
    if(MH_CreateHook(reinterpret_cast<void*>(inputAddress),reinterpret_cast<void*>(InputHook),reinterpret_cast<void**>(&original))!=MH_OK)return false;
    if(MH_EnableHook(reinterpret_cast<void*>(inputAddress))!=MH_OK){MH_RemoveHook(reinterpret_cast<void*>(inputAddress));original=nullptr;return false;}
    Log("Mouse: original FlashMovie absolute ray input installed (post-sensitivity, pixel coordinates)");return true;
}
bool Ready(){return original!=nullptr;}
void Target(bool active,int x,int y,int width,int height){
    AcquireSRWLockExclusive(&lock);target={active&&width>0&&height>0,x,y,width,height};ReleaseSRWLockExclusive(&lock);
}
}
