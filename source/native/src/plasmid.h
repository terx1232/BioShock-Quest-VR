#pragma once
#include "vrmath.h"
namespace plasmid {
bool Observe(void* hands);
bool Anchor(void* hands,vr::Vec3 wrist,const vr::Mat3& aim);
bool Active();
bool Available();
bool HandGeometry(unsigned vertices,unsigned primitives);
bool IsHandsMesh(unsigned vertices,unsigned primitives);
void Reset();
}
