#pragma once
#include "vrmath.h"

// A first-person animation places its attachment relative to the camera, not
// to the controller. Retain its model rotation, but anchor the model's grip
// point directly to the tracked hand. Do not scale or rotate the saved offset
// about the camera: that makes the weapon orbit far ahead of the hand.
namespace weaponpose {
struct Pose { vr::Vec3 position; vr::Mat3 rotation; };
inline Pose AtGrip(vr::Vec3 hand, const vr::Mat3& rotation, vr::Vec3 modelGrip) {
    return {hand - vr::Mul(rotation, modelGrip), rotation};
}
}
