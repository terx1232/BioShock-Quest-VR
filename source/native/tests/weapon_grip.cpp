#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include "../src/weaponpose.h"
void check(bool ok,const char* message){if(!ok){fprintf(stderr,"FAIL: %s\n",message);exit(1);}}
float length(vr::Vec3 p){return sqrtf(p.x*p.x+p.y*p.y+p.z*p.z);}
int main(){
    // Production transform must put the model grip at the tracked hand, under
    // body yaw, controller pitch/roll and a non-zero model pivot. The animation
    // attachment observed on Quest (71,25,-23.7) must not translate that grip.
    const vr::Vec3 hand={123,-47,99},pivot={7,2,-3};
    for(int yaw : {0,8192,16384,30000})for(int pitch : {-8000,0,11000}){
        const vr::Mat3 rotation=vr::FromRotator({pitch,yaw,6200});
        const auto pose=weaponpose::AtGrip(hand,rotation,pivot);
        check(length(pose.position+vr::Mul(pose.rotation,pivot)-hand)<.0001f,"model grip follows hand at every orientation");
        const auto shifted=weaponpose::AtGrip(hand+vr::Vec3{50,0,-12},rotation,pivot);
        check(length(shifted.position-pose.position-vr::Vec3{50,0,-12})<.0001f,"room movement moves weapon without scaling");
        check(fabsf(length(vr::Mul(pose.rotation,vr::Vec3{48,0,0}))-48)<.0001f,"original weapon dimensions retained");
    }
    const auto identity=weaponpose::AtGrip(hand,vr::YawMatrix(0),{});
    check(length(identity.position-hand)<.0001f,"root grip has no flat animation offset");
    puts("PASS: tracked grip, rotated model pivot, room movement, original model dimensions");
}
