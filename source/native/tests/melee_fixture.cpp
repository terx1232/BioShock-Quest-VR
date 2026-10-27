#include <cstdio>
#include <cstdlib>
#include "../src/melee.cpp"
void Log(const char*,...){}
namespace uobject {int PropertyOffset(void*,const wchar_t* name){if(!wcscmp(name,L"HitActors"))return 0x40;if(!wcscmp(name,L"CollisionPhantomStartTime"))return 0x50;return -1;}}
void Check(bool v,const char* s){if(!v){fprintf(stderr,"FAIL: %s\n",s);exit(1);}}
int main(){
    meleevr::Swing swing;vr::Vec3 head{0,1.7f,0},hand{0,1.4f,-.5f};unsigned t=100;
    for(int i=0;i<5;i++,t+=33)Check(!swing.Update(hand,head,t,0,1.2f),"rest cannot strike");
    hand.z+=.10f;Check(!swing.Update(hand,head,t,0,1.2f),"backward wind-up waits for the actual strike");t+=33;
    hand.z-=.11f;Check(swing.Update(hand,head,t,0,1.2f),"moderate outward hand stroke starts native attack");t+=33;
    for(int i=0;i<12;i++,t+=33){hand.z-=.07f;Check(!swing.Update(hand,head,t,0,1.2f),"continuous motion cannot auto-repeat after cooldown");}
    for(int i=0;i<5;i++,t+=33)Check(!swing.Update(hand,head,t,0,1.2f),"return to rest re-arms without hitting");
    hand.y-=.11f;Check(swing.Update(hand,head,t,0,1.2f),"a fresh downward strike can fire");t+=33;
    for(int i=0;i<6;i++,t+=33){head.x+=.10f;Check(!swing.Update(hand,head,t,0,1.2f),"head motion alone cannot trigger attack");}
    hand.x+=2;Check(!swing.Update(hand,head,t,1,1.2f),"tracking resume/teleport cannot strike");
    unsigned char actor[256]{};void* victim=(void*)1;*(void**)(actor+0x40)=&victim;*(int*)(actor+0x44)=*(int*)(actor+0x48)=1;*(float*)(actor+0x50)=5;
    melee::Observe(actor);melee::Request();melee::Observe(actor);Check(melee::HapticRight()==0,"stale native hit list gives no feedback");
    *(float*)(actor+0x50)=6;*(int*)(actor+0x44)=0;melee::Observe(actor);Check(melee::HapticRight()==0,"accepted native attack without contact gives no hit feedback");
    *(int*)(actor+0x44)=1;melee::Observe(actor);Check(melee::HapticRight()>.5f,"new native phantom with HitActors confirms contact");
    Sleep(90);melee::Observe(actor);Check(melee::HapticRight()==0,"same native hit cannot vibrate repeatedly");
    melee::Observe(nullptr);Check(melee::HapticRight()==0,"unequip clears pending impact");
    puts("PASS: Remastered head-relative gesture, wind-up/outward strike, one pulse per stroke, rest/recovery, head-only motion, tracking jump, stale collision rejection and bounded native impact feedback");
}
