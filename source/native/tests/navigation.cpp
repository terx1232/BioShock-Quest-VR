#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../src/navigation.cpp"
Config g_config;
unsigned char pawn[128]{},arrow[1024]{};const wchar_t* asset=L"GPS_arrow";
void Log(const char*,...){}
namespace uobject {
const wchar_t* ClassName(void* p){return p==arrow?L"GPSArrow":L"ShockPlayer";}
const wchar_t* ObjectName(void*){return asset;}
int PropertyOffset(void* p,const wchar_t* n){
 if(p==pawn&&!wcscmp(n,L"GPSArrow"))return 0x40;
 if(p==arrow&&!wcscmp(n,L"StaticMesh"))return 0x44;
 if(p==arrow&&!wcscmp(n,L"CurrentOpacity"))return 0x48;
 return -1;
}
bool BoolProperty(void*,const wchar_t* n,int& off,unsigned& mask){if(wcscmp(n,L"bHidden"))return false;off=0x40;mask=2;return true;}
}
void Check(bool v,const char* why){if(!v){fprintf(stderr,"FAIL: %s\n",why);exit(1);}}
int main(){
 *(void**)(pawn+0x2c)=(void*)1;*(void**)(arrow+0x2c)=(void*)2;*(void**)(pawn+0x40)=arrow;*(void**)(arrow+0x44)=(void*)3;
 *(float*)(arrow+0x48)=.7f;*(int*)(arrow+0x1d0)=16384;
 navigation::Update(pawn,0);auto a=navigation::Current();Check(a.visible&&fabsf(a.yaw-vr::kPi*.5f)<.001f&&a.opacity==.7f,"native waypoint rotation and animation opacity observed");
 Check(navigation::GeometrySelected(),"visible original GPS mesh selected for geometry stereo");
 vr::Vec3 head{},forward{1,0,0};*(vr::Vec3*)(arrow+0x1c0)={77,0,0};g_config.navigationDistance=2;
 navigation::Update(pawn,0,&head,&forward);Check(fabsf(navigation::Current().stereoScale-2)<.001f,"native apparent position/size retained while stereo depth is moved from 1m to 2m");
 Check(((vr::Vec3*)(arrow+0x1c0))->x==77,"native GPS actor pose is never rewritten");
 navigation::Update(pawn,16384);Check(fabsf(navigation::Current().yaw)<.001f,"waypoint responds to head yaw, not aim controller");
 *(unsigned*)(arrow+0x40)=2;navigation::Update(pawn,0);Check(!navigation::Current().visible&&!navigation::GeometrySelected(),"native hidden flag clears geometry selection");
 *(unsigned*)(arrow+0x40)=0;asset=L"OtherMesh";navigation::Update(pawn,0);Check(!navigation::Current().visible,"unverified mesh cannot suppress the world");
 asset=L"GPS_arrow";*(float*)(arrow+0x48)=NAN;navigation::Update(pawn,0);Check(!navigation::Current().visible,"invalid opacity rejected");
 navigation::Update(nullptr,0);Check(!navigation::Current().visible,"pawn/level loss clears stale waypoint");
 puts("PASS: native GPS actor/mesh validation, yaw/opacity, visible geometry selection, hidden/invalid state and level reset");
}
