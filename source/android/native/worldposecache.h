#pragma once
#include <cmath>
#include <cstring>
namespace worldpose {
// Stock writes 0,12,...252; slot 1 is never a producer slot.
constexpr int kSlot=1,kHistory=0x638,kRight=0x1c1c,kStride=28;
struct Pose {float qx,qy,qz,qw,x,y,z;};
struct Pair {Pose left,right;};
static_assert(sizeof(Pose)==kStride,"native XrPosef layout");
inline bool Valid(Pose p){
    const float values[]={p.qx,p.qy,p.qz,p.qw,p.x,p.y,p.z};
    for(float v:values)if(!std::isfinite(v))return false;
    float q=p.qx*p.qx+p.qy*p.qy+p.qz*p.qz+p.qw*p.qw;
    return q>.9f&&q<1.1f&&fabsf(p.x)<10000&&fabsf(p.y)<10000&&fabsf(p.z)<10000;
}
class Cache {
    Pair poses{};int token=-1,blocked=-1;bool valid=false,waiting=false;
public:
    void Resume(){blocked=token;valid=false;waiting=blocked>=0;}
    bool Update(int sync,int serial,const Pair& source){
        if(sync<0||sync>252||sync%12||serial<0||serial>255)return false;
        int next=(sync<<8)|serial;
        if(waiting&&next==blocked)return false;
        if(valid&&next==token)return true;
        if(!Valid(source.left)||!Valid(source.right))return false;
        poses=source;token=next;valid=true;waiting=false;return true;
    }
    const Pair& Get()const{return poses;}
    int Token()const{return token;}
    bool Blocked(int sync,int serial)const{return waiting&&blocked==((sync<<8)|serial);}
};
inline Pair Read(const void* renderer,int sync){
    Pair p;auto* bytes=static_cast<const unsigned char*>(renderer);
    memcpy(&p.left,bytes+kHistory+sync*kStride,kStride);
    memcpy(&p.right,bytes+kHistory+kRight+sync*kStride,kStride);return p;
}
inline void Write(void* renderer,const Pair& p){
    auto* bytes=static_cast<unsigned char*>(renderer);
    memcpy(bytes+kHistory+kSlot*kStride,&p.left,kStride);
    memcpy(bytes+kHistory+kRight+kSlot*kStride,&p.right,kStride);
}
}
