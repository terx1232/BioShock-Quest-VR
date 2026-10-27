#include "navigation.h"
#include "uobject.h"
#include "vrmath.h"
#include "log.h"
#include "config.h"
#include <windows.h>
#include <cmath>
#include <cwchar>
#include <algorithm>
namespace navigation {
namespace {
State current{};DWORD observed=0;
bool readable(void* p,int off,unsigned n){return p&&off>0&&off<0x10000&&!IsBadReadPtr((char*)p+off,n);}
}
void Update(void* pawn,int headYaw,const vr::Vec3* head,const vr::Vec3* forward){
    current={};observed=GetTickCount();
    if(!pawn||IsBadReadPtr(pawn,0x30))return;
    static void* pawnClass=nullptr;static int gps=-1;
    void* cls=*(void**)((char*)pawn+0x2c);
    if(cls!=pawnClass){pawnClass=cls;gps=uobject::PropertyOffset(pawn,L"GPSArrow");}
    if(!readable(pawn,gps,4))return;
    void* arrow=*(void**)((char*)pawn+gps);
    if(!arrow||IsBadReadPtr(arrow,0x1d8))return;
    const wchar_t* type=uobject::ClassName(arrow);
    if(!type||wcscmp(type,L"GPSArrow"))return;
    static void* arrowClass=nullptr;static int opacity=-1,mesh=-1,hidden=-1;static unsigned mask=0;
    cls=*(void**)((char*)arrow+0x2c);
    if(cls!=arrowClass){
        arrowClass=cls;opacity=uobject::PropertyOffset(arrow,L"CurrentOpacity");mesh=uobject::PropertyOffset(arrow,L"StaticMesh");
        hidden=-1;mask=0;uobject::BoolProperty(arrow,L"bHidden",hidden,mask);
        Log("Navigation geometry: native GPSArrow fields opacity +0x%x mesh +0x%x hidden +0x%x",opacity,mesh,hidden);
    }
    if(!readable(arrow,opacity,4)||!readable(arrow,mesh,4)||!readable(arrow,hidden,4)||!mask)return;
    void* asset=*(void**)((char*)arrow+mesh);const wchar_t* name=asset?uobject::ObjectName(asset):nullptr;
    if(!name||_wcsicmp(name,L"GPS_arrow"))return;
    float alpha=*(float*)((char*)arrow+opacity);
    if(!std::isfinite(alpha)||alpha<=.01f||(*(unsigned*)((char*)arrow+hidden)&mask))return;
    const int yaw=*(int*)((char*)arrow+0x1d0);
    current={true,static_cast<short>(yaw-headYaw)/vr::kUnrealPerRadian,std::clamp(alpha,0.f,1.f)};
    if(head&&forward){
        vr::Vec3 delta=*(vr::Vec3*)((char*)arrow+0x1c0)-*head;
        float depth=delta.x*forward->x+delta.y*forward->y+delta.z*forward->z;
        if(std::isfinite(depth)&&depth>1&&depth<g_config.unitsPerMetre*20){
            current.stereoScale=std::clamp(g_config.navigationDistance*g_config.unitsPerMetre/depth,.25f,128.f);
            static bool logged=false;if(!logged){logged=true;Log("Navigation: native GPS pose/size/material retained, source depth %.3fm, stereo depth %.2fm, homogeneous factor %.3f",depth/g_config.unitsPerMetre,g_config.navigationDistance,current.stereoScale);}
        }
    }
}
State Current(){return GetTickCount()-observed<=500?current:State{};}
bool GeometrySelected(){return Current().visible;}
}
