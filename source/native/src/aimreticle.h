#pragma once
#include "vrframe.h"

// The camera applies this same aim quaternion to native firing. Project its
// direction from the centre eye; using the grip or the hand's translated
// position would add parallax that the native firing direction does not have.
namespace aimreticle {
struct Target { float u=0,v=0;bool visible=false; };
inline Target Project(const vrframe::Snapshot& snapshot,const xrapi::Hand& hand){
    const auto& frame=snapshot.frame;
    if(!snapshot.state.valid||!frame.active||!frame.inGame)return {};
    const auto q=hand.orientation;
    float norm=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    if(!isfinite(norm)||norm<.25f||norm>4)return {};
    vr::Vec3 direction=vr::Column(vr::XrToUnreal(vr::FromQuat(q)),0);
    vr::Vec3 local=vr::Mul(vr::Transpose(frame.head),direction);
    if(!isfinite(local.x)||!isfinite(local.y)||!isfinite(local.z)||local.x<.05f)return {};
    if(!isfinite(frame.fov)||!isfinite(frame.fovV)||frame.fov<30||frame.fov>170||frame.fovV<30||frame.fovV>170)return {};
    float u=.5f+local.y/(2*local.x*tanf(frame.fov*vr::kPi/360));
    float v=.5f-local.z/(2*local.x*tanf(frame.fovV*vr::kPi/360));
    return {u,v,isfinite(u)&&isfinite(v)&&u>=0&&u<=1&&v>=0&&v<=1};
}
}
