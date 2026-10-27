// The live BioShock layout: UProperty.Offset +0x6c, field link +0x70,
// UBoolProperty.BitMask +0x94. Wrong masks can change unrelated actor flags.
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../src/uobject.h"
void Log(const char*,...){}
void check(bool ok,const char* why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(1);}}
void set(void* p,int off,unsigned value){*(unsigned*)((BYTE*)p+off)=value;}
int main(){
    BYTE* allocation=(BYTE*)VirtualAlloc((void*)0x112b0000,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    check(allocation==(BYTE*)0x112b0000,"map BioShock name table address");
    BYTE* globals=allocation+0xa000;
    BYTE* objects=(BYTE*)VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    check(objects!=nullptr,"allocate reflected objects");
    int* names[4];const wchar_t* text[]={L"None",L"BoolProperty",L"bReticleDisabled",L"ShockPlayer"};
    for(int i=0;i<4;i++){names[i]=(int*)(objects+i*128);wcscpy((wchar_t*)(names[i]+4),text[i]);}
    *(int***)(globals+0x280)=names;*(int*)(globals+0x284)=4;
    BYTE *boolClass=objects+512,*playerClass=objects+768,*field=objects+1024,*actor=objects+1536;
    set(boolClass,0x24,1);set(playerClass,0x24,3);set(playerClass,0x54,(unsigned)(uintptr_t)field);
    set(field,0x24,2);set(field,0x2c,(unsigned)(uintptr_t)boolClass);set(field,0x6c,0xaa4);
    set(field,0x70,0x03e091a0);set(field,0x94,4);set(actor,0x2c,(unsigned)(uintptr_t)playerClass);
    int offset=-1;unsigned mask=0;
    check(uobject::BoolProperty(actor,L"bReticleDisabled",offset,mask)&&offset==0xaa4&&mask==4,"read actual reticle bit, not UProperty link");
    unsigned flags=0x1008;flags|=mask;check(flags==0x100c,"disable reticle without changing sibling flags");
    set(field,0x94,0x40);
    check(uobject::BoolProperty(actor,L"bReticleDisabled",offset,mask)&&mask==0x40,"read native Actor.bHidden mask");
    set(field,0x94,0);set(field,0x70,2);
    check(!uobject::BoolProperty(actor,L"bReticleDisabled",offset,mask),"never use a plausible-looking field link as fallback mask");
    set(field,0x94,6);
    check(!uobject::BoolProperty(actor,L"bReticleDisabled",offset,mask),"reject multi-bit malformed flag");
    puts("PASS: live BoolProperty layout, reticle mask, bHidden mask, sibling flags, invalid metadata");
    VirtualFree(objects,0,MEM_RELEASE);VirtualFree(allocation,0,MEM_RELEASE);
}
