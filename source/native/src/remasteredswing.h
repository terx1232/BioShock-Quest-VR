// Adapted from VR-Stereo-Hub/bioshock-trilogy-vr, core/input/swing.cpp,
// commit 41f004bc6a1e4fa8be0d0e0749f9ac5a0453e75e.
// Copyright (c) 2026 bioshock-vr contributors. MIT License; see
// third_party/bioshock-vr/LICENSE. Quest adapter: no XR/test-input threads.
#pragma once
#include "vrmath.h"
#include <cmath>
namespace remasteredswing {
constexpr unsigned kPulseMs=120;
struct Detector {
    bool tracked=false,armed=true;unsigned tick=0,resumes=0,lastStrike=0;
    vr::Vec3 previousHand{},previousHead{};
    bool Update(vr::Vec3 hand,vr::Vec3 head,unsigned now,unsigned epoch,float threshold){
        unsigned elapsed=now-tick;
        if(!tracked||epoch!=resumes||elapsed<4||elapsed>100){
            tracked=true;tick=now;resumes=epoch;previousHand=hand;previousHead=head;return false;
        }
        vr::Vec3 motion=hand-previousHand,relative=motion-(head-previousHead);
        previousHand=hand;previousHead=head;tick=now;
        float distance=sqrtf(relative.x*relative.x+relative.y*relative.y+relative.z*relative.z);
        float worldMotion=sqrtf(motion.x*motion.x+motion.y*motion.y+motion.z*motion.z);
        if(!std::isfinite(distance)||distance>.65f){tracked=false;return false;}
        float speed=distance/(elapsed*.001f);
        // Same hysteresis and cooldown as the reference. The extra world-motion
        // guard prevents moving only the head from inventing a hand gesture.
        if(speed<threshold*.45f)armed=true;
        if(worldMotion<.001f||speed<threshold||!armed||(lastStrike&&now-lastStrike<300))return false;
        vr::Vec3 reach=hand-head;float reachLength=sqrtf(reach.x*reach.x+reach.y*reach.y+reach.z*reach.z);
        float outward=reachLength>.01f?(relative.x*reach.x+relative.y*reach.y+relative.z*reach.z)/(reachLength*elapsed*.001f):0;
        // Quest adaptation: the wind-up and a turn around the body are not
        // the strike. Wait for the outward/downward part of a real swing.
        if(outward<threshold*.15f)return false;
        armed=false;lastStrike=now;return true;
    }
};
}
