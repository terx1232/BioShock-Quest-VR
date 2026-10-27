// Runs the actual production CalcView hook against a deterministic native
// first-person view. No BioShock process, headset or input events are used.
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <vector>
#ifndef CAMERA_SOURCE
#define CAMERA_SOURCE "../src/camera.cpp"
#endif
#include CAMERA_SOURCE
Config g_config;
std::vector<unsigned char> player(0x800),pawn(0x800);
vrframe::Snapshot snapshot{};
bool haveBody=false,cinematic=false,otherView=false;
float bodyYaw=0;
int aimHand=1;
vr::Rotator cosmetic{};
unsigned nativeCalls=0,bodyTurns=0;
#ifndef BIOSHOCK_RELEASE
void Log(const char*,...){}
#endif
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*){return MH_OK;}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID){return MH_OK;}
namespace engineviewport {
bool SetSize(unsigned,unsigned,unsigned,unsigned){return true;}void DeviceCreated(){}
bool Ready(){return true;}bool Matches(unsigned,unsigned){return true;}unsigned Height(){return 864;}
}
namespace input {int AimHand(){return aimHand;}}
namespace hud {void UpdateReticle(void*,void*){}}
namespace mouse {void MeleePitch(float,bool){}}
namespace uistate {void UpdateCinematic(void*){}bool Cinematic(){return cinematic;}bool ContainerOpen(){return false;}}
namespace crash {void WatchWrites(void*,int){}}
namespace uobject {
int PropertyOffset(void* object,const wchar_t* name){
    if(object==player.data()&&!wcscmp(name,L"Pawn"))return 0x100;
    if(object==pawn.data()&&!wcscmp(name,L"Controller"))return 0x108;
    if(object==pawn.data()&&!wcscmp(name,L"ActiveHoldable"))return 0x10c;
    return -1;
}
bool BoolProperty(void*,const wchar_t*,int&,unsigned&){return false;}
const wchar_t* ClassName(void*){return L"CameraFixture";}
void DumpObjectRefs(void*,const char*){}void DumpProperties(void*,const char*,const wchar_t*){}void DumpBoneNames(void*){}
}
namespace vrframe {
Snapshot Capture(){return snapshot;}
bool HasBody(){return haveBody;}float BodyYaw(){return bodyYaw;}
void SetBodyYaw(float yaw){bodyYaw=yaw;haveBody=true;}void AddBodyYaw(float delta){bodyYaw+=delta;bodyTurns++;}
unsigned RecenterCount(){return 0;}
}
namespace weapon {
bool MeleeEquipped(){return false;}void ReleaseRigid(){}void SetAbilityMode(bool){}void NoteHoldable(void*){}
void HoldRigid(void*,void*,const vr::Vec3&,const vr::Mat3&,vr::Vec3,const vr::Mat3*){}
}
namespace plasmid {void Reset(){}bool Observe(void*){return false;}bool Anchor(void*,vr::Vec3,const vr::Mat3&){return false;}}
namespace navigation {void Update(void*,int,const vr::Vec3*,const vr::Vec3*){}}
void Check(bool ok,const char* why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(1);}}
vr::Rotator& ActorRotation(){return *reinterpret_cast<vr::Rotator*>(player.data()+0x1cc);}
void __fastcall NativeView(void* self,void*,void** actor,float* location,int* rotation){
    Check(self==player.data(),"native camera receives the original controller");nativeCalls++;
    *actor=otherView?player.data():pawn.data();
    location[0]=10;location[1]=20;location[2]=70;
    const auto r=ActorRotation();rotation[0]=r.pitch+cosmetic.pitch;
    rotation[1]=r.yaw+cosmetic.yaw;rotation[2]=r.roll+cosmetic.roll;
}
vr::Rotator View(){
    void* actor=nullptr;float location[3]={};vr::Rotator rotation{};
    camera::HookedCalcView(player.data(),nullptr,&actor,location,&rotation.pitch);
    Check(location[0]==10&&location[1]==20&&location[2]==70,"unmodified native eye position when positional options are off");
    return rotation;
}
void Same(vr::Rotator a,vr::Rotator b,const char* why){
    Check(abs(camera::AngleDiff(a.pitch,b.pitch))<=2&&abs(camera::AngleDiff(a.yaw,b.yaw))<=2&&abs(camera::AngleDiff(a.roll,b.roll))<=2,why);
}
int main(){
    *reinterpret_cast<void**>(player.data()+0x100)=pawn.data();
    *reinterpret_cast<void**>(pawn.data()+0x108)=player.data();
    *reinterpret_cast<void**>(pawn.data()+0x2c)=reinterpret_cast<void*>(1);
    g_config.enabled=true;g_config.weaponInHand=false;g_config.roomScale=false;
    g_config.noBob=false;g_config.realHeight=false;g_config.stereo=false;
    g_config.fovOffset=0x420;*reinterpret_cast<float*>(player.data()+0x420)=100;
    snapshot.frame.active=snapshot.frame.inGame=true;snapshot.frame.fov=100;
    snapshot.frame.head=vr::FromRotator({4500,7000,1200});snapshot.state.valid=true;
    snapshot.state.right.orientation={0,.2f,0,sqrtf(.96f)};
    camera::g_original=NativeView;ActorRotation()={0,1200,0};
    const float initial=1200/vr::kUnrealPerRadian;
    View();Check(fabsf(bodyYaw-initial)<.00001f,"spawn starts from native actor rotation");
    auto expectedView=[&](){return vr::ToRotator(vr::Mul(vr::YawMatrix(bodyYaw),snapshot.frame.head));};
    auto expectedAim=[&](){return vr::ToRotator(vr::Mul(vr::YawMatrix(bodyYaw),vr::XrToUnreal(vr::FromQuat(snapshot.state.right.orientation))));};
    for(int shot=0;shot<10;shot++)for(int yaw:{2400,3800,1800,-2800,-1600,0}){
        cosmetic={5000,yaw,3000};
        for(int query=0;query<30;query++){
            const auto view=View();Check(fabsf(bodyYaw-initial)<.00001f&&bodyTurns==0,"shotgun camera-only recoil cannot accumulate into VR body yaw");
            Same(view,expectedView(),"physical headset orientation stays unchanged through firing recoil");
            auto aim=expectedAim();aim.roll=0;Same(ActorRotation(),aim,"native weapon aim remains on the right controller");
        }
    }
    cosmetic={0,4200,0};aimHand=-1;snapshot.frame.head=vr::FromRotator({-3000,18000,-1500});
    for(int i=0;i<20;i++){Same(View(),expectedView(),"real head movement remains responsive during recoil");Check(fabsf(bodyYaw-initial)<.00001f,"releasing trigger cannot import hand/head difference into body yaw");}
    ActorRotation().yaw+=5000;const float turned=initial+5000/vr::kUnrealPerRadian;
    View();Check(fabsf(bodyYaw-turned)<.00001f&&bodyTurns==1,"real script/spawn actor turn is retained once, without cosmetic view offset");
    for(int i=0;i<30;i++)View();Check(fabsf(bodyYaw-turned)<.00001f&&bodyTurns==1,"repeated render/audio camera queries cannot repeat a real turn");
    camera::g_lastWrittenYaw=65000;ActorRotation().yaw=2500;View();
    Check(fabsf(bodyYaw-(turned+3036/vr::kUnrealPerRadian))<.00001f,"real actor turn across Unreal yaw wrap is retained");
    bodyYaw+=vr::kPi/4;const float stickYaw=bodyYaw;const auto turns=bodyTurns;
    for(int i=0;i<30;i++){Same(View(),expectedView(),"stick turn stays responsive during recoil");Check(fabsf(bodyYaw-stickYaw)<.00001f&&bodyTurns==turns,"camera does not double a VR stick turn");}
    haveBody=false;camera::g_haveWritten=false;ActorRotation()={0,6000,0};View();
    Check(fabsf(bodyYaw-6000/vr::kUnrealPerRadian)<.00001f,"new body/spawn does not import a currently animated camera offset");
    cinematic=true;ActorRotation()={1000,6000,200};cosmetic={2000,4000,500};
    auto anchor=vr::FromRotator({3000,10000,700});
    Same(View(),vr::ToRotator(vr::Mul(anchor,snapshot.frame.head)),"cinematic keeps its full scripted camera anchor");
    cinematic=false;snapshot.frame.active=false;const auto before=ActorRotation();
    Same(View(),{before.pitch+2000,before.yaw+4000,before.roll+500},"tracking loss retains original native output without stale VR writes");
    snapshot.frame.active=true;otherView=true;Same(View(),{before.pitch+2000,before.yaw+4000,before.roll+500},"non-player camera remains native");
    otherView=false;g_config.enabled=false;Same(View(),{before.pitch+2000,before.yaw+4000,before.roll+500},"disabled mod preserves the native view");
    Check(nativeCalls>1800,"all repeated queries still call original PlayerCalcView");
    puts("PASS: actual CalcView, 10 recoil bursts / 1800 queries, physical HMD motion, controller aim, trigger release, real/script turns, yaw wrap, cinematics, tracking loss and non-player fallback");
}
