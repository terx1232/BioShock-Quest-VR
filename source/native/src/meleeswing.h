#pragma once
#include "vrmath.h"
#include <algorithm>
#include <cmath>
#include "remasteredswing.h"
namespace meleevr {
inline float Dot(vr::Vec3 a,vr::Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
using Swing=remasteredswing::Detector;
// HitActors is accepted only after the native Wrench creates a new collision
// phantom. A stale list from an earlier strike never generates impact feedback.
struct Impact {
    bool observed=false,armed=false,accepted=false,reported=false;float start=0,baseline=0;unsigned requested=0;
    void Request(unsigned now){armed=observed;accepted=reported=false;baseline=start;requested=now;}
    bool Observe(float nativeStart,unsigned hits,unsigned now){
        if(!std::isfinite(nativeStart))return false;
        if(!observed){observed=true;start=nativeStart;return false;}
        start=nativeStart;
        if(!armed||now-requested>1200)return false;
        if(start!=baseline)accepted=true;
        if(accepted&&hits&&!reported){reported=true;return true;}return false;
    }
};
}
