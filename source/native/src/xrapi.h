// Client side of WinlatorXR's XrAPI 0.5: the headset state arrives as text over UDP, the game
// answers with the mode it renders in (VR, alternate eye) and the field of view it used.
#pragma once
#include "vrmath.h"

namespace xrapi {

// Order of the buttons in the packet (XrInterface.ControllerButton on the WinlatorXR side)
enum Button {
    L_GRIP, L_MENU, L_THUMBSTICK_PRESS, L_THUMBSTICK_LEFT, L_THUMBSTICK_RIGHT, L_THUMBSTICK_UP,
    L_THUMBSTICK_DOWN, L_TRIGGER, L_X, L_Y,
    R_A, R_B, R_GRIP, R_THUMBSTICK_PRESS, R_THUMBSTICK_LEFT, R_THUMBSTICK_RIGHT, R_THUMBSTICK_UP,
    R_THUMBSTICK_DOWN, R_TRIGGER,
    BUTTON_COUNT
};

struct Hand {
    vr::Quat orientation;  // поза "прицела" (луч)
    vr::Quat grip;         // поза рукоятки (модель контроллера)
    vr::Vec3 position;     // metres, stage space (OpenXR axes)
    float stickX, stickY;
};

struct State {
    bool valid;
    unsigned long tick;    // GetTickCount() последнего пакета
    unsigned packets;
    unsigned resumes;      // сколько раз данные шлема возобновлялись после паузы > 0.7 с или скачка позиции
    Hand left, right;
    vr::Quat hmdOrientation;
    vr::Vec3 hmdPosition;  // metres, stage space (OpenXR axes)
    float ipd;             // metres
    float fovX, fovY;      // degrees, per eye, as WinlatorXR shows the frame
    int sync;              // pose index the frame has to carry back in its sync pixel
    bool buttons[BUTTON_COUNT];
    float altitude;        // высота шлема над полом, метры (0 - нет данных)
    unsigned long long frameId; // full OpenXR request ID on PC, zero on Winlator
};

// Starts the UDP listener and tells WinlatorXR which XrAPI version to speak.
// mode3d: 0 flat, 1 side-by-side, 2 alternate eye (asked for until the headset answers)
bool Start(const char* apiDir, int mode3d);
// Enable immersive rendering only after the first game Present succeeds.
void GraphicsReady();
// Latest headset state (copy).
State Get();
// Mode: vr = render per eye with the headset pose, mode3d: 0 flat, 1 side-by-side, 2 alternate eye.
void SendMode(bool vr, int mode3d, float fovX, float fovY, float hapticLeft, float hapticRight);
bool NativeCinemaReady();
bool WorldPosePinReady();
bool ExactPoseReady();
bool NativeHudSupported();
bool NativeHudReady();
bool SendFramePose(unsigned tag,unsigned long long frameId,bool nativeHud=false);
bool NativePointer(int& x,int& y,bool& down);
void SendCinema(int mode,vr::Vec3 anchor={},vr::Vec3 right={},vr::Vec3 up={},vr::Vec3 normal={},float halfW=0,float halfH=0,int hand=1,unsigned width=0,unsigned height=0);

}  // namespace xrapi
