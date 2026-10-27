#include "../src/flashpointer.h"
#include "MinHook.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cmath>
#include <algorithm>
#include <cstring>
void Log(const char* format,...){va_list a;va_start(a,format);vprintf(format,a);va_end(a);puts("");}
void Check(bool ok,const char* why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(1);}}
float __cdecl Clamp(float v,float lo,float hi){return std::clamp(v,lo,hi);}
__attribute__((naked)) void Round(){__asm__("subl $8, %esp\n fistpl (%esp)\n movl (%esp), %eax\n addl $8, %esp\n ret");}
void PatchCall(uintptr_t at,void* fn){*(int*)(at+1)=int(uintptr_t(fn)-at-5);}
using Input=void(__thiscall*)(void*,const unsigned char*,const unsigned char*,float);
struct Movie {
    BYTE bytes[0x110]{};
    int& X(){return *(int*)(bytes+0xf0);}int& Y(){return *(int*)(bytes+0xf4);}
    void Rect(int x,int y,int w,int h){int p[]={x,y,w,h};memcpy(bytes+0x60,p,sizeof(p));}
};
int main(){
    void* page=VirtualAlloc((void*)0x109b0000,65536,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE);
    Check(page==(void*)0x109b0000,"private host page for original native handler");
    FILE* file=fopen("native.bin","rb");Check(file!=nullptr,"original EXE bytes");
    Check(fread((void*)0x109bb6f0,1,0x210,file)==0x210,"complete native code and jump table");fclose(file);
    for(auto at:{0x109bb75bu,0x109bb79bu})PatchCall(at,(void*)Clamp);
    for(auto at:{0x109bb763u,0x109bb7a3u})PatchCall(at,(void*)Round);
    Input input=(Input)0x109bb6f0;
    unsigned char x=0xe4,y=0xe5,left=1,press=1,release=3,axis=4;
    Movie main;main.Rect(0,0,1280,720);main.X()=240;main.Y()=688;
    input(&main,&x,&axis,1.92f);
    Check(main.X()==241,"original axis really truncates fractional sensitivity");
    Check(MH_Initialize()==MH_OK,"MinHook init");Check(flashpointer::Install(),"hook exact native handler/signature");
    flashpointer::Target(true,641,359,1280,720);
    for(unsigned i=0;i<100;i++){
        input(&main,&x,&axis,(i&1)?-30000.f:1.92f);input(&main,&y,&axis,1.44f);
        Check(main.X()==641&&main.Y()==359,"fixed ray stays pixel-exact regardless of DI gain/clamp/repeated polls");
    }
    Movie pause;pause.Rect(0,0,1280,720);pause.X()=1199;pause.Y()=1;
    input(&pause,&x,&axis,1);input(&pause,&y,&axis,1);
    Check(pause.X()==641&&pause.Y()==359,"new pause movie aligns at rest, no old menu address");
    for(int yy:{0,1,45,360,718,719})for(int xx:{0,1,62,640,1278,1279}){
        flashpointer::Target(true,xx,yy,1280,720);input(&pause,&x,&axis,1);input(&pause,&y,&axis,1);
        Check(pause.X()==xx&&pause.Y()==yy,"corners/centre exact at 1280x720");
    }
    Movie inset;inset.Rect(100,80,800,600);flashpointer::Target(true,450,360,1280,720);
    input(&inset,&x,&axis,1);input(&inset,&y,&axis,1);
    Check(inset.X()+100==450&&inset.Y()+80==360,"movie origin included in screen mapping");
    input(&pause,&left,&press,0);Check((*(unsigned*)(pause.bytes+0x100)&1)!=0,"native click down preserved");
    input(&pause,&left,&release,0);Check((*(unsigned*)(pause.bytes+0x100)&1)==0,"native click release preserved");
    pause.X()=400;
    flashpointer::Target(false,500,500,1280,720);int before=pause.X();input(&pause,&x,&axis,7);
    Check(pause.X()==before+7,"gameplay/inactive path unchanged");
    flashpointer::Target(true,500,500,1280,720);Movie resized;resized.Rect(0,0,1728,864);
    input(&resized,&x,&axis,7);Check(resized.X()==7,"unmatched render rectangle keeps safe native fallback");
    flashpointer::Target(true,-20,900,1280,720);input(&pause,&x,&axis,1);input(&pause,&y,&axis,1);
    Check(pause.X()==0&&pause.Y()==719,"outside ray clamps to menu image edges");
    MH_Uninitialize();VirtualFree(page,0,MEM_RELEASE);
    puts("PASS: original retail x86 handler with real hook: static/moving ray, main/pause/new movie, 36 edge points, origin, clicks and fallback");
}
