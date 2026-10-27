#include "engineviewport.h"
#include "log.h"
#include <windows.h>
#include <stdio.h>

namespace engineviewport {
namespace {
constexpr DWORD primary=0x10fd71f4,exec=0x10fd71d4;
constexpr int sizeX=0xa4,sizeY=0xa8;
BYTE* volatile viewport=nullptr;
volatile LONG deviceSeen=0,searching=0;
volatile LONG resizing=0;
BYTE* previous=nullptr;
unsigned previousW=0,previousH=0;
void* __fastcall discardDestroy(void* self,void*,unsigned){return self;}
void __fastcall discardSerialize(void*,void*,const wchar_t*,int){}
void __fastcall discardFlush(void*,void*){}
int __fastcall discardCategory(void*,void*,int){return 0;}

bool readable(const void* p,size_t size){
    MEMORY_BASIC_INFORMATION m{};
    return p&&VirtualQuery(p,&m,sizeof(m))&&m.State==MEM_COMMIT&&
        !(m.Protect&(PAGE_GUARD|PAGE_NOACCESS))&&
        (m.Protect&(PAGE_READONLY|PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE))&&
        (const BYTE*)p+size<=(const BYTE*)m.BaseAddress+m.RegionSize;
}
bool valid(BYTE* p){
    // Verify both MSVC subobjects, not an isolated vtable address left in a
    // scratch buffer or emulator code cache.
    if(!readable(p,0xac)||*(DWORD*)p!=primary||*(DWORD*)(p+0x3c)!=exec)return false;
    unsigned w=*(unsigned*)(p+sizeX),h=*(unsigned*)(p+sizeY);
    return w>=320&&w<=8192&&h>=240&&h<=8192;
}
BYTE* find(){
    MEMORY_BASIC_INFORMATION m{};
    static DWORD chunk[16384];
    for(BYTE* address=(BYTE*)0x10000;(UINT_PTR)address<0x7fff0000&&VirtualQuery(address,&m,sizeof(m));){
        BYTE* base=(BYTE*)m.BaseAddress;
        if(m.State==MEM_COMMIT&&m.Type==MEM_PRIVATE&&
           (m.Protect&(PAGE_READWRITE|PAGE_EXECUTE_READWRITE))&&!(m.Protect&PAGE_GUARD)){
            for(size_t pos=0;pos<m.RegionSize;pos+=sizeof(chunk)){
                size_t length=m.RegionSize-pos;if(length>sizeof(chunk))length=sizeof(chunk);
                SIZE_T got=0;if(!ReadProcessMemory(GetCurrentProcess(),base+pos,chunk,length,&got))continue;
                for(size_t i=0;i<got/4;i++)if(chunk[i]==primary&&valid(base+pos+i*4))return base+pos+i*4;
            }
        }
        BYTE* next=base+m.RegionSize;if(next<=address)break;address=next;
    }
    return nullptr;
}
DWORD WINAPI discover(void*){
    for(unsigned attempt=0;;attempt++){
        BYTE* found=find();
        if(found){
            BYTE* target=*(BYTE**)(found+0x84);
            if(readable(target,0x94)){
                DWORD* lock=(DWORD*)(target+0x90);
                if(*lock<=1){*lock=1;Log("Camera: HorizontalFOVLock set");}
            }
            // Wine can report a client rectangle such as 1280x708 before the
            // fullscreen 1280x720 backbuffer is applied. This verified initial
            // rectangle belongs to our viewport too; do not reject it merely
            // because window decorations made it smaller than the backbuffer.
            previous=found;previousW=*(unsigned*)(found+sizeX);previousH=*(unsigned*)(found+sizeY);
            InterlockedExchangePointer((PVOID volatile*)&viewport,found);
            Log("Camera: validated viewport %p, engine %ux%u (after graphics ready)",found,
                *(unsigned*)(found+sizeX),*(unsigned*)(found+sizeY));
            InterlockedExchange(&searching,0);return 0;
        }
        if(attempt==0||attempt==60)Log("Camera: waiting for viewport after graphics ready; depth crop withheld");
        Sleep(attempt<60?2000:10000);
    }
}
void ensure(){
    if(!deviceSeen||valid(viewport)||InterlockedCompareExchange(&searching,1,0))return;
    HANDLE thread=CreateThread(nullptr,0,discover,nullptr,0,nullptr);
    if(thread)CloseHandle(thread);else InterlockedExchange(&searching,0);
}
}
void DeviceCreated(){
    InterlockedExchange(&deviceSeen,1);
    // A native resolution Reset keeps the same viewport object. Retain a
    // validated object so the mode gate does not mistake resize for loading.
    if(valid(viewport))return;
    InterlockedExchangePointer((PVOID volatile*)&viewport,nullptr);
    ensure();
}
bool Ready(){
    BYTE* observed=viewport;if(valid(observed))return true;
    // Do not let an object allocated later at the same address reuse stale
    // ownership dimensions from the previous viewport.
    InterlockedCompareExchangePointer((PVOID volatile*)&viewport,nullptr,observed);
    ensure();return false;
}
bool Matches(unsigned w,unsigned h){return Ready()&&*(unsigned*)(viewport+sizeX)==w&&*(unsigned*)(viewport+sizeY)==h;}
unsigned Height(){return Ready()?*(unsigned*)(viewport+sizeY):0;}
unsigned Width(){return Ready()?*(unsigned*)(viewport+sizeX):0;}
bool CanResize(){
    if(!Ready())return false;
    BYTE* actor=*(BYTE**)(viewport+0x40); // exec subobject +4; verified at 10a2f05a
    return readable(actor,0x418);         // native Exec reads Actor->Pawn at +0x414
}
bool Resize(unsigned w,unsigned h,bool windowed){
    if(w<640||w>4096||h<480||h>2160||!CanResize())return false;
    BYTE* p=viewport;
    if(!readable((void*)exec,4)||*(DWORD*)exec!=0x109253e0)return false;
    // Verified in this EXE's FOutputDevice::Logf (10caeb40): filter +10,
    // Serialize +4, Flush +8. Use an explicit MSVC layout: MinGW C++ virtual
    // destructors have a different number of vtable entries.
    void* methods[]={reinterpret_cast<void*>(discardDestroy),reinterpret_cast<void*>(discardSerialize),
        reinterpret_cast<void*>(discardFlush),reinterpret_cast<void*>(discardFlush),reinterpret_cast<void*>(discardCategory)};
    struct Output {void** methods;DWORD flags[2];} output{methods,{0,0}};
    wchar_t command[64];swprintf(command,64,L"TEMPSETRES %ux%ux32%c",w,h,windowed?L'w':L'f');
    using Exec=int(__thiscall*)(void*,const wchar_t*,void*);
    if(InterlockedCompareExchange(&resizing,1,0))return false;
    Log("Resolution: native %ux%u %s on window thread %lu",w,h,windowed?"windowed":"fullscreen",GetCurrentThreadId());
    bool accepted=reinterpret_cast<Exec>(0x109253e0)(p+0x3c,command,&output)!=0;
    InterlockedExchange(&resizing,0);return accepted;
}
bool SetSize(unsigned w,unsigned h,unsigned backW,unsigned backH){
    if(InterlockedCompareExchange(&resizing,0,0))return false;
    BYTE* p=viewport;if(!Ready()||!w||!h)return false;p=viewport;
    unsigned* size=(unsigned*)(p+sizeX);
    bool ours=(size[0]==backW&&size[1]==backH)||(size[0]==w&&size[1]==h)||
              (p==previous&&size[0]==previousW&&size[1]==previousH);
    if(!ours)return false;
    if(size[0]!=w||size[1]!=h){
        Log("Camera: engine screen %ux%u -> %ux%u (frame %ux%u)",size[0],size[1],w,h,backW,backH);
        size[0]=w;size[1]=h;
    }
    previous=p;previousW=w;previousH=h;return Matches(w,h);
}
}
