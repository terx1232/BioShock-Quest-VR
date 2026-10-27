#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include "../src/weapon.cpp"
Config g_config;unsigned changes=0,weights=0,validations=0;
bool animating=false;
namespace melee {void Observe(void*){}void Reset(){}bool Animating(){return animating;}}
unsigned char weightFunction[256]{},validFunction[256]{},returnProperty[128]{},weightReturn[128]{};
void Log(const char*,...){}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*){return MH_OK;}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID){return MH_OK;}
namespace uobject {
void* FindField(void*,const wchar_t* name){if(!wcscmp(name,L"SetAnimationWeight"))return weightFunction;if(!wcscmp(name,L"IsAnimationHandleValid"))return validFunction;return nullptr;}
const wchar_t* ClassName(void* p){return p==weightFunction||p==validFunction?L"Function":L"FixtureWeapon";}
const wchar_t* ObjectName(void* p){return p==returnProperty||p==weightReturn?L"ReturnValue":static_cast<const wchar_t*>(p);}
int PropertyOffset(void*,const wchar_t* n){
 if(!wcscmp(n,L"StaticMesh"))return 0x58;if(!wcscmp(n,L"Mesh"))return 0x50;if(!wcscmp(n,L"DrawScale"))return 0x54;
 if(!wcscmp(n,L"AnimationHandle"))return 0x60;if(!wcscmp(n,L"EquippingAnimationHandle"))return 0x64;
 if(!wcscmp(n,L"UnEquippingAnimationHandle"))return 0x68;if(!wcscmp(n,L"IdlingAnimationHandle"))return 0x6c;return -1;
}
bool BoolProperty(void*,const wchar_t*,int&,unsigned&){return false;}
}
namespace weaponlayer {void InvalidateModel(){changes++;}}
namespace camera {bool HeadWorld(vr::Vec3&,vr::Mat3&){return false;}}
namespace uistate {bool Cinematic(){return false;}}
namespace hud {void Suspend(){}void Resume(){}}
namespace vrframe {Frame Current(){return {};}xrapi::State State(){return {};}}
void Check(bool ok,const char* why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(1);}}
void __fastcall Event(void* actor,void*,void* function,void* data,void*){
 unsigned* p=(unsigned*)data;
 Check(p[0]>=101&&p[0]<=104,"native animation handle retained");
 if(function==validFunction){p[1]=1;validations++;}
 else{Check(function==weightFunction&&p[1]==0,"only visual weight=0 requested");p[2]=1;weights++;}
 Check(*(float*)((char*)actor+0x80)==17.f,"animation time never modified");
}
int __fastcall Attachment(void*,void*,void*,void* actor,int,int){
 *(vr::Vec3*)((char*)actor+0x1c0)={900,800,700};*(vr::Rotator*)((char*)actor+0x1cc)={100,200,300};*(float*)((char*)actor+0x54)=.2f;return 42;
}
int main(){
 *(void**)(validFunction+0x54)=returnProperty;*(int*)(returnProperty+0x6c)=4;
 *(void**)(weightFunction+0x54)=weightReturn;*(int*)(weightReturn+0x6c)=8;
 void* table[4]={nullptr,nullptr,nullptr,(void*)Event};unsigned char pistol[1024]{},wrench[1024]{};
 for(auto a:{pistol,wrench}){
  *(void***)a=table;*(void**)(a+0x2c)=a;*(float*)(a+0x54)=1.f;*(float*)(a+0x80)=17.f;
  for(int i=0;i<4;i++)*(int*)(a+0x60+i*4)=101+i;
 }
 *(const wchar_t**)(pistol+0x50)=L"WP_PistolMesh";*(const wchar_t**)(wrench+0x58)=L"WP_WrenchMesh";
 g_config.noWeaponAnimations=g_config.rigidWeapon=g_config.weaponGripAnchor=true;
 vr::Vec3 grip{10,20,30};vr::Mat3 aim=vr::FromRotator({1200,9000,-400});vr::Rotator expected=vr::ToRotator(aim);
 for(auto a:{pistol,wrench,pistol}){
  weapon::HoldRigid(a,nullptr,{},vr::FromRotator({30000,-24000,19000}),grip,&aim);
  Check(weapon::g_rigid==a&&weapon::g_holding,"immediate rigid pose on first frame, no settle timeout");
  Check(!memcmp(a+0x1c0,&grip,sizeof(grip))&&!memcmp(a+0x1cc,&expected,sizeof(expected)),"orientation and grip independent of animated hand/equip transform");
  weapon::g_updateAttachment=Attachment;Check(weapon::HookedUpdateAttachment(nullptr,nullptr,nullptr,a,0,0)==42,"native attachment result retained");
  Check(!memcmp(a+0x1c0,&grip,sizeof(grip))&&!memcmp(a+0x1cc,&expected,sizeof(expected))&&*(float*)(a+0x54)==1.f,"attachment animation cannot change visible position, rotation or size");
 }
 Check(changes==3&&weights==12&&validations==12,"all visible model animation weights cleared; native animation clocks retained");
 g_config.wrenchScale=.9f;
 for(int i=0;i<4;i++){
  weapon::HoldRigid(wrench,nullptr,{},aim,grip,&aim);
  Check(fabsf(*(float*)(wrench+0x54)-.9f)<.0001f,"wrench reduced by ten percent without compounded scale");
  weapon::HoldRigid(pistol,nullptr,{},aim,grip,&aim);
  Check(*(float*)(wrench+0x54)==1.f&&*(float*)(pistol+0x54)==1.f,"unequip restores native scale; pistol unchanged");
 }
 unsigned char hands[1024]{};
 g_config.wrenchForward=.05f;
 weapon::HoldRigid(wrench,hands,{},aim,grip,&aim);
 const vr::Vec3 ahead=grip+vr::Column(aim,0)*(.05f*g_config.unitsPerMetre);
 Check(!memcmp(wrench+0x1c0,&ahead,sizeof(ahead)),"wrench shifted five centimetres forward in controller coordinates");
 animating=true;
 for(int i=0;i<8;i++){
  weapon::HookedUpdateAttachment(nullptr,nullptr,hands,wrench,0,0);
  Check(!memcmp(wrench+0x1c0,&ahead,sizeof(ahead))&&!memcmp(wrench+0x1cc,&expected,sizeof(expected)),"native strike attachment cannot change five-centimetre controller offset");
  Check(fabsf(*(float*)(wrench+0x54)-.9f)<.0001f,"strike attachment cannot change wrench size");
 }
 animating=false;
 weapon::HoldRigid(pistol,hands,{},aim,grip,&aim);
 Check(!memcmp(pistol+0x1c0,&grip,sizeof(grip))&&*(float*)(pistol+0x54)==1.f,"wrench forward offset and scale never affect pistol");
 puts("PASS: first-frame canonical aim/grip, fixed native scale, repeated pistol/wrench swaps, animated attachment rejection, native weight-only API, animation clocks/results retained");
}
