#pragma once
#include "vrmath.h"
namespace navigation {
struct State {bool visible=false;float yaw=0,opacity=0,stereoScale=1;};
void Update(void* pawn,int headYaw,const vr::Vec3* head=nullptr,const vr::Vec3* forward=nullptr);
State Current();
bool GeometrySelected();
}
