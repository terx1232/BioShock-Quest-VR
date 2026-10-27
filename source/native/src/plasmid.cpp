#include "plasmid.h"
#include "config.h"
#include "uobject.h"
#include "log.h"
#include <windows.h>
#include <cstring>
#include <cwchar>
#include <cmath>
namespace plasmid {
namespace {
void* type=nullptr;void* coords=nullptr;int modeOffset=-1,abilityOffset=-1,meshOffset=-1;
unsigned modeMask=0;int nameOffset=-1,resultOffset=-1,boneName=-1;
bool active=false,available=false,anchored=false,knownHands=false;DWORD seen=0;
struct Coords {vr::Vec3 origin,x,y,z;};
bool Read(void* p,int off,unsigned n){return p&&off>0&&off<0x10000&&!IsBadReadPtr((char*)p+off,n);}
int FieldOffset(void* function,const wchar_t* key){
    if(!Read(function,0x54,4))return -1;
    unsigned count=0;
    for(void* p=*(void**)((char*)function+0x54);p&&count++<256&&!IsBadReadPtr(p,0x70);p=*(void**)((char*)p+0x3c)){
        const wchar_t* name=uobject::ObjectName(p);
        if(name&&!_wcsicmp(name,key)){int off=*(int*)((char*)p+0x6c);return off>=0&&off<=64?off:-1;}
    }
    return -1;
}
}
bool Observe(void* hands){
    active=available=anchored=false;knownHands=false;seen=GetTickCount();
    if(!g_config.plasmidInHand||!Read(hands,0x2c,4))return false;
    void* cls=*(void**)((char*)hands+0x2c);
    if(cls!=type){
        type=cls;modeOffset=abilityOffset=meshOffset=-1;modeMask=0;
        uobject::BoolProperty(hands,L"bIsInWeaponMode",modeOffset,modeMask);
        abilityOffset=uobject::PropertyOffset(hands,L"CurrentAbility");meshOffset=uobject::PropertyOffset(hands,L"Mesh");
        coords=uobject::FindField(hands,L"GetBoneCoords");
        const wchar_t* c=coords?uobject::ClassName(coords):nullptr;if(!c||wcscmp(c,L"Function"))coords=nullptr;
        nameOffset=FieldOffset(coords,L"BoneName");resultOffset=FieldOffset(coords,L"ReturnValue");
        boneName=uobject::FindName(L"Bip01 L Hand");if(boneName<0)boneName=uobject::FindName(L"Bip01_L_Hand");
        Log("Plasmid: native Hands mode +0x%x mask %x ability +0x%x, wrist FName=%d, coords offsets %d/%d",modeOffset,modeMask,abilityOffset,boneName,nameOffset,resultOffset);
    }
    if(Read(hands,meshOffset,4)){
        void* mesh=*(void**)((char*)hands+meshOffset);const wchar_t* name=mesh?uobject::ObjectName(mesh):nullptr;
        knownHands=name&&!_wcsicmp(name,L"NEWPlayerHands");
    }
    if(!Read(hands,modeOffset,4)||!modeMask||!Read(hands,abilityOffset,4))return false;
    available=*(void**)((char*)hands+abilityOffset)!=nullptr;
    active=(*(unsigned*)((char*)hands+modeOffset)&modeMask)==0&&available;
    return active;
}
bool Anchor(void* hands,vr::Vec3 wrist,const vr::Mat3& aim){
    if(!active||!knownHands||!coords||boneName<0||nameOffset<0||nameOffset>80||resultOffset<8||resultOffset+int(sizeof(Coords))>128||!Read(hands,0x1c0,24))return false;
    alignas(4) unsigned char params[128]{};*(int*)(params+nameOffset)=boneName;
    using Event=void(__fastcall*)(void*,void*,void*,void*,void*);
    if(IsBadReadPtr(*(void***)hands,16))return false;
    Event event=(Event)(*(void***)hands)[3];event(hands,nullptr,coords,params,nullptr);
    Coords bone{};memcpy(&bone,params+resultOffset,sizeof(bone));
    vr::Mat3 owner=vr::FromRotator(*(vr::Rotator*)((char*)hands+0x1cc));vr::Mat3 inverse=vr::Transpose(owner);
    vr::Vec3 local=vr::Mul(inverse,bone.origin-*(vr::Vec3*)((char*)hands+0x1c0));
    float length=sqrtf(local.x*local.x+local.y*local.y+local.z*local.z);
    vr::Mat3 world={{{bone.x.x,bone.y.x,bone.z.x},{bone.x.y,bone.y.y,bone.z.y},{bone.x.z,bone.y.z,bone.z.z}}};
    for(unsigned c=0;c<3;c++){
        auto v=vr::Column(world,c);float n=v.x*v.x+v.y*v.y+v.z*v.z;
        if(!std::isfinite(n)||n<.01f||n>64)return false;
        float scale=1/sqrtf(n);for(unsigned row=0;row<3;row++)world.m[row][c]*=scale;
    }
    float determinant=world.m[0][0]*(world.m[1][1]*world.m[2][2]-world.m[1][2]*world.m[2][1])-world.m[0][1]*(world.m[1][0]*world.m[2][2]-world.m[1][2]*world.m[2][0])+world.m[0][2]*(world.m[1][0]*world.m[2][1]-world.m[1][1]*world.m[2][0]);
    if(!std::isfinite(determinant)||determinant<.95f)return false;
    if(!std::isfinite(length)||length<1||length>g_config.unitsPerMetre*4)return false;
    vr::Mat3 localRotation=vr::Mul(inverse,world);vr::Mat3 wanted=vr::Mul(aim,vr::Transpose(localRotation));
    *(vr::Vec3*)((char*)hands+0x1c0)=wrist-vr::Mul(wanted,local);
    *(vr::Rotator*)((char*)hands+0x1cc)=vr::ToRotator(wanted);anchored=true;
    static bool logged=false;if(!logged){logged=true;Log("Plasmid: native left wrist anchored to the left controller; original ability skins/notifies/effects retained");}
    return true;
}
bool Active(){return active&&GetTickCount()-seen<500;}
bool Available(){return available&&GetTickCount()-seen<500;}
bool IsHandsMesh(unsigned v,unsigned p){return knownHands&&GetTickCount()-seen<500&&((v==2180&&p==3588)||(v==1659&&p==2500)||(v==891&&p==1230)||(v==270&&p==341));}
bool HandGeometry(unsigned v,unsigned p){return Active()&&anchored&&v==2180&&p==3588;}
void Reset(){active=available=anchored=knownHands=false;}
}
