// Math between the headset (OpenXR) and BioShock (Unreal Engine 2.5).
//
// OpenXR: right handed, metres, +X right, +Y up, -Z forward.
// Unreal: left handed, units, +X forward, +Y right, +Z up; rotations are integers where
// 65536 is a full turn, applied as Yaw (around Z), Pitch (around Y), Roll (around X).
#pragma once
#include <math.h>

namespace vr {

constexpr float kPi = 3.14159265358979f;
constexpr float kUnrealPerRadian = 65536.0f / (2.0f * kPi);

struct Vec3 { float x, y, z; };
struct Quat { float x, y, z, w; };
struct Mat3 { float m[3][3]; };  // m[row][col], columns are the rotated basis vectors

struct Rotator { int pitch, yaw, roll; };  // FRotator memory layout

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }

inline Mat3 Mul(const Mat3& a, const Mat3& b) {
    Mat3 r{};
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
    return r;
}

inline Vec3 Mul(const Mat3& a, Vec3 v) {
    return {a.m[0][0] * v.x + a.m[0][1] * v.y + a.m[0][2] * v.z,
            a.m[1][0] * v.x + a.m[1][1] * v.y + a.m[1][2] * v.z,
            a.m[2][0] * v.x + a.m[2][1] * v.y + a.m[2][2] * v.z};
}

inline Mat3 FromQuat(Quat q) {
    float n = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (n < 1e-6f) return {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    float x = q.x / n, y = q.y / n, z = q.z / n, w = q.w / n;
    return {{{1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)},
             {2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)},
             {2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)}}};
}

// Unreal axis from OpenXR axis: X = -z, Y = x, Z = y
inline Vec3 XrToUnreal(Vec3 v) { return {-v.z, v.x, v.y}; }

// The same rotation expressed in Unreal axes: C * R * C^-1
inline Mat3 XrToUnreal(const Mat3& r) {
    const Mat3 c = {{{0, 0, -1}, {1, 0, 0}, {0, 1, 0}}};
    const Mat3 ci = {{{0, 1, 0}, {0, 0, 1}, {-1, 0, 0}}};
    return Mul(Mul(c, r), ci);
}

inline Mat3 YawMatrix(float yawRadians) {
    float c = cosf(yawRadians), s = sinf(yawRadians);
    return {{{c, -s, 0}, {s, c, 0}, {0, 0, 1}}};
}

inline float YawOf(const Mat3& r) { return atan2f(r.m[1][0], r.m[0][0]); }

// FRotationMatrix inverse: forward = column 0, right = column 1, up = column 2
inline Rotator ToRotator(const Mat3& r) {
    Vec3 f = {r.m[0][0], r.m[1][0], r.m[2][0]};
    Vec3 right = {r.m[0][1], r.m[1][1], r.m[2][1]};
    Vec3 up = {r.m[0][2], r.m[1][2], r.m[2][2]};
    float yaw = atan2f(f.y, f.x);
    float pitch = atan2f(f.z, sqrtf(f.x * f.x + f.y * f.y));
    float roll = atan2f(-right.z, up.z);
    return {(int)lrintf(pitch * kUnrealPerRadian), (int)lrintf(yaw * kUnrealPerRadian),
            (int)lrintf(roll * kUnrealPerRadian)};
}

inline Mat3 FromRotator(Rotator rot) {
    float p = rot.pitch / kUnrealPerRadian, y = rot.yaw / kUnrealPerRadian, r = rot.roll / kUnrealPerRadian;
    float sp = sinf(p), cp = cosf(p), sy = sinf(y), cy = cosf(y), sr = sinf(r), cr = cosf(r);
    // Columns: forward, right, up (UE2 FRotationMatrix axes)
    return {{{cp * cy, sr * sp * cy - cr * sy, -(cr * sp * cy + sr * sy)},
             {cp * sy, sr * sp * sy + cr * cy, cy * sr - cr * sp * sy},
             {sp, -sr * cp, cr * cp}}};
}

inline Vec3 Column(const Mat3& r, int c) { return {r.m[0][c], r.m[1][c], r.m[2][c]}; }

// Обратная к повороту
inline Mat3 Transpose(const Mat3& a) {
    Mat3 t;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) t.m[i][j] = a.m[j][i];
    return t;
}

}  // namespace vr
