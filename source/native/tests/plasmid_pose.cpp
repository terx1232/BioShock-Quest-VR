#include <cstdio>
#include <cstdlib>
#include "../src/plasmid.cpp"
Config g_config;
unsigned char hands[1024]{},function[128]{},boneField[128]{},resultField[128]{},mesh[128]{};
vr::Vec3 local{20,-15,4};vr::Mat3 localRotation=vr::FromRotator({900,1800,-2500});bool bad=false;unsigned calls=0;
void Log(const char*,...){}
void Check(bool v,const char* message){if(!v){fprintf(stderr,"FAIL: %s\n",message);exit(1);}}
namespace uobject {
const wchar_t* ClassName(void* p){return p==function?L"Function":L"Hands";}
const wchar_t* ObjectName(void* p){return p==boneField?L"BoneName":p==resultField?L"ReturnValue":p==mesh?L"NEWPlayerHands":L"?";}
int FindName(const wchar_t*){return 101;}
void* FindField(void*,const wchar_t* n){return !wcscmp(n,L"GetBoneCoords")?function:nullptr;}
int PropertyOffset(void*,const wchar_t* n){return !wcscmp(n,L"CurrentAbility")?0x44:!wcscmp(n,L"Mesh")?0x48:-1;}
bool BoolProperty(void*,const wchar_t*,int& off,unsigned& mask){off=0x40;mask=4;return true;}
}
void __fastcall Event(void* actor,void*,void* fn,void* params,void*){
 Check(actor==hands&&fn==function,"only native GetBoneCoords called on Hands");
 Check(*(int*)params==101&&*(int*)((char*)params+4)==0&&*(int*)((char*)params+8)==0,"original FName/optional parameter layout");
 auto rotation=vr::FromRotator(*(vr::Rotator*)(hands+0x1cc));auto world=vr::Mul(rotation,localRotation);
 plasmid::Coords c{*(vr::Vec3*)(hands+0x1c0)+vr::Mul(rotation,local),vr::Column(world,0),vr::Column(world,1),vr::Column(world,2)};
 if(bad)c.x.x=NAN;memcpy((char*)params+12,&c,sizeof(c));calls++;
}
int main(){
 void* table[4]={nullptr,nullptr,nullptr,(void*)Event};*(void***)hands=table;*(void**)(hands+0x2c)=(void*)1;
 *(void**)(hands+0x44)=(void*)2;*(void**)(hands+0x48)=mesh;*(void**)(function+0x54)=boneField;
 *(void**)(boneField+0x3c)=resultField;*(int*)(boneField+0x6c)=0;*(int*)(resultField+0x6c)=12;
 g_config.plasmidInHand=true;*(unsigned*)(hands+0x40)=4;
 Check(!plasmid::Observe(hands)&&plasmid::IsHandsMesh(1659,2500),"weapon mode has no plasmid hand, known arm LOD still hidden");
 Check(plasmid::Available(),"selected plasmid remains aimable before switching from weapon mode");
 *(unsigned*)(hands+0x40)=0;
 for(int i=0;i<5;i++){
  *(vr::Vec3*)(hands+0x1c0)={100,200,300};*(vr::Rotator*)(hands+0x1cc)={1200,6000,-1100};
  local.x+=3;localRotation=vr::FromRotator({i*400,i*700,1500});
  auto aim=vr::FromRotator({3000,-9000+i*1100,600});vr::Vec3 target{40.f+i,80,150};
  Check(plasmid::Observe(hands)&&plasmid::Anchor(hands,target,aim),"active ability anchors actual left wrist");
  auto owner=vr::FromRotator(*(vr::Rotator*)(hands+0x1cc));auto actual=*(vr::Vec3*)(hands+0x1c0)+vr::Mul(owner,local);
  auto delta=actual-target;Check(sqrtf(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z)<.01f,"native equip/fire wrist motion cannot displace controller wrist");
  auto facing=vr::Mul(owner,localRotation);
  for(int r=0;r<3;r++)for(int c=0;c<3;c++)Check(fabsf(facing.m[r][c]-aim.m[r][c])<.001f,"palm faces controller under changing native animation");
  Check(plasmid::HandGeometry(2180,3588)&&!plasmid::HandGeometry(1659,2500),"only verified close hand topology enabled");
 }
 bad=true;plasmid::Observe(hands);auto saved=*(vr::Vec3*)(hands+0x1c0);
 Check(!plasmid::Anchor(hands,{},vr::YawMatrix(0))&&!memcmp(hands+0x1c0,&saved,sizeof(saved)),"invalid bone data cannot move actor");
 *(unsigned*)(hands+0x40)=4;Check(!plasmid::Observe(hands)&&!plasmid::HandGeometry(2180,3588),"weapon switch clears palm immediately");
 g_config.plasmidInHand=false;Check(!plasmid::Observe(hands),"disabled setting preserves native rendering");
 Check(!plasmid::Available(),"disabled/unavailable plasmid has no aim marker");
 puts("PASS: reflected native left wrist, position/orientation compensation, no animated drift, all arm LOD recognition, weapon switch and invalid-data fallback");
}
