#include <cstdio>
#include <cstdlib>
#include "../src/weapon.cpp"
void Log(const char*,...){}
Config g_config;
namespace melee {void Observe(void*){}void Reset(){}bool Animating(){return false;}}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*){return MH_OK;}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID){return MH_OK;}
namespace uobject {
void* FindField(void*,const wchar_t*){return nullptr;}
bool BoolProperty(void*,const wchar_t* name,int& offset,unsigned& mask){if(wcscmp(name,L"bHidden"))return false;offset=0x40;mask=2;return true;}
const wchar_t* ClassName(void*){return L"Fixture";}
const wchar_t* ObjectName(void* object){return static_cast<const wchar_t*>(object);}
int PropertyOffset(void*,const wchar_t* name){
    if(!wcscmp(name,L"StaticMesh"))return 0x58;
    if(!wcscmp(name,L"Mesh"))return 0x50;
    if(!wcscmp(name,L"DrawScale"))return 0x54;
    if(!wcscmp(name,L"CurrentHoldable"))return 0x60;
    return -1;
}
}
namespace weaponlayer {unsigned modelChanges=0;void InvalidateModel(){modelChanges++;}}
namespace camera {bool HeadWorld(vr::Vec3&,vr::Mat3&){return false;}}
namespace uistate {bool Cinematic(){return true;}}
namespace hud {void Suspend(){}void Resume(){}}
namespace vrframe {Frame Current(){return {};}xrapi::State State(){return {};}}
void Check(bool ok,const char* s){if(!ok){fprintf(stderr,"FAIL: %s\n",s);exit(1);}}
int main(){
    unsigned char first[128]{},second[128]{};*reinterpret_cast<void**>(first+0x2c)=reinterpret_cast<void*>(1);*reinterpret_cast<void**>(second+0x2c)=reinterpret_cast<void*>(2);
    auto& a=*reinterpret_cast<unsigned*>(first+0x40);auto& b=*reinterpret_cast<unsigned*>(second+0x40);a=0x8000;b=0x4002;
    weapon::g_held=first;weapon::SetCinematic(true);Check(a==0x8002,"held actor hidden");
    a|=0x1000;weapon::SetCinematic(true);Check(a==0x9002,"repeated cinematic preserves game flags");
    weapon::g_held=second;weapon::SetCinematic(true);Check(a==0x9000&&b==0x4002,"actor swap restores only the owned visibility bit");
    weapon::SetCinematic(false);Check(b==0x4002,"already hidden weapon remains hidden");
    weapon::g_held=first;weapon::SetCinematic(true);weapon::SetCinematic(false);Check(a==0x9000,"ordinary weapon returns with unrelated flags intact");
    puts("PASS: actual cinematic weapon hiding/restoration, actor swap, pre-hidden state, unrelated flag writes");
    auto select=[&](unsigned char* actor,const wchar_t* mesh,bool skeletal){
        *reinterpret_cast<const wchar_t**>(actor+(skeletal?0x50:0x58))=mesh;
        *reinterpret_cast<float*>(actor+0x54)=1.25f;
        weapon::HoldRigid(actor,nullptr,{},vr::Mat3{},{});
        Check(*reinterpret_cast<float*>(actor+0x54)==1.25f,"native model scale retained");
    };
    weapon::g_held=nullptr;
    select(first,L"WP_WrenchMesh",false);
    Check(weapon::WrenchGeometrySelected()&&!weapon::PistolGeometrySelected(),"static wrench selected");
    select(second,L"WP_PistolMesh",true);
    Check(weapon::PistolGeometrySelected()&&!weapon::WrenchGeometrySelected(),"skeletal pistol selected through Mesh property");
    const auto changes=weaponlayer::modelChanges;
    weapon::HoldRigid(second,nullptr,{},vr::Mat3{},{});
    Check(weaponlayer::modelChanges==changes,"unchanged model keeps its completed frame");
    weapon::HoldRigid(nullptr,nullptr,{},vr::Mat3{},{});
    Check(!weapon::PistolGeometrySelected()&&!weapon::WrenchGeometrySelected()&&weaponlayer::modelChanges==changes+1,"unequip invalidates both geometry routes");
    *reinterpret_cast<const wchar_t**>(first+0x58)=nullptr;
    select(first,L"UnknownMesh",true);
    Check(!weapon::PistolGeometrySelected()&&!weapon::WrenchGeometrySelected(),"unknown model keeps the original renderer");
    unsigned toggle=1;
    for(auto name:{L"TommyGunMESH",L"WP_ShotgunMesh",L"WP_ChemicalThrowerMesh",L"WP_CrossbowMesh",L"WP_GrenadeLauncherMesh",L"PlasmidEquipMESH"}){
        select((toggle++%2)?second:first,name,true);
        auto model=weaponassets::Find(name);Check(model!=weaponassets::Model::None,"actual named weapon/tool family found");
        auto range=weaponassets::ranges[unsigned(model)];Check(range.count>=4,"all native LODs represented");
        for(unsigned i=range.first;i<range.first+range.count;i++){
            auto p=weaponassets::pairs[i];Check(weapon::GeometryMesh(p.vertices,p.primitives),"selected root and its native upgrade sections use geometry");
            Check(!weapon::GeometryMesh(p.vertices,p.primitives+1),"different primitive count never captures world geometry");
        }
        Check(!weapon::GeometryMesh(2180,3588)&&!weapon::GeometryMesh(67,78),"weapon catalog cannot capture arms or GPS");
    }
    Check(weaponassets::Matches(weaponassets::Model::Pistol,2361,2824)&&weaponassets::Matches(weaponassets::Model::Wrench,1953,1686),"accepted pistol and wrench signatures preserved");
    puts("PASS: actual static/skeletal weapon selection, model handoff invalidation, unknown model fallback, native scale");
    g_config.weaponGeometry=true;
    unsigned char hands[128]{};*reinterpret_cast<void**>(hands+0x2c)=reinterpret_cast<void*>(3);
    *reinterpret_cast<const wchar_t**>(first+0x58)=L"WP_WrenchMesh";
    *reinterpret_cast<const wchar_t**>(second+0x50)=L"WP_PistolMesh";
    *reinterpret_cast<const wchar_t**>(second+0x58)=nullptr;
    *reinterpret_cast<void**>(hands+0x60)=first;
    weapon::HoldRigid(first,hands,{},vr::Mat3{},{});
    *reinterpret_cast<void**>(hands+0x60)=second;
    weapon::HoldRigid(first,hands,{},vr::Mat3{},{});
    Check(weapon::PistolGeometrySelected()&&weapon::GeometryMesh(2361,2824)&&!weapon::GeometryMesh(1953,1686),"equipping pistol captured before ActiveHoldable catches up");
    const auto handoffChanges=weaponlayer::modelChanges;
    weapon::HoldRigid(nullptr,hands,{},vr::Mat3{},{});
    weapon::HoldRigid(second,hands,{},vr::Mat3{},{});
    Check(weaponlayer::modelChanges==handoffChanges&&weapon::PistolGeometrySelected(),"null active interval and equip completion keep the rendered model and its same stereo epoch");
    *reinterpret_cast<void**>(hands+0x60)=first;
    weapon::HoldRigid(second,hands,{},vr::Mat3{},{});
    Check(weapon::WrenchGeometrySelected()&&weapon::GeometryMesh(1953,1686),"reverse handoff captures wrench during equipping");
    *reinterpret_cast<void**>(hands+0x60)=reinterpret_cast<void*>(1);
    weapon::HoldRigid(second,hands,{},vr::Mat3{},{});
    Check(weapon::PistolGeometrySelected(),"invalid hands reference falls back to the active actor");
    *reinterpret_cast<const wchar_t**>(first+0x58)=L"UnknownMesh";
    *reinterpret_cast<const wchar_t**>(first+0x50)=nullptr;
    *reinterpret_cast<void**>(hands+0x60)=first;
    weapon::HoldRigid(second,hands,{},vr::Mat3{},{});
    Check(weapon::PistolGeometrySelected(),"scripted/unknown hands prop cannot take weapon capture ownership");
    puts("PASS: real visual-owner resolver, early pending model, null active interval, commit without invalidation, reverse switch, invalid/unknown fallback");
}
