#include "melee.h"
#include "meleeswing.h"
#include "uobject.h"
#include "log.h"
#include <windows.h>
namespace melee {
namespace {
void* selected=nullptr;int actors=-1,startTime=-1;meleevr::Impact impact;DWORD pulse=0,requested=0;
}
void Reset(){selected=nullptr;actors=startTime=-1;impact={};pulse=requested=0;}
void Observe(void* wrench){
    if(!wrench){Reset();return;}
    if(wrench!=selected){
        Reset();selected=wrench;actors=uobject::PropertyOffset(wrench,L"HitActors");startTime=uobject::PropertyOffset(wrench,L"CollisionPhantomStartTime");
        Log("VR melee: native Wrench hit list +0x%x, collision start +0x%x",actors,startTime);
    }
    if(actors<=0||startTime<=0||actors>0x10000||startTime>0x10000||IsBadReadPtr((char*)wrench+actors,12)||IsBadReadPtr((char*)wrench+startTime,4))return;
    int* a=(int*)((char*)wrench+actors);if(a[1]<0||a[1]>64||a[2]<a[1]||(a[1]&&IsBadReadPtr((void*)a[0],a[1]*4)))return;
    if(impact.Observe(*(float*)((char*)wrench+startTime),a[1],GetTickCount())){
        pulse=GetTickCount();Log("VR melee: native collision hit confirmed (%d actors); right-hand impact feedback",a[1]);
    }
}
void Request(){requested=GetTickCount();impact.Request(requested);}
bool Animating(){return requested&&GetTickCount()-requested<700;}
float HapticRight(){return pulse&&GetTickCount()-pulse<80?.7f:0;}
}
