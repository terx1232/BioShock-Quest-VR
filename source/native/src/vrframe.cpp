#include "vrframe.h"

#include <windows.h>
#include <math.h>

#include "config.h"
#include "log.h"
#include "xrapi.h"
#include "pcvr.h"
#include "uistate.h"

namespace vrframe {
namespace {

Frame g_frame = {};
xrapi::State g_state = {};
SRWLOCK g_frameLock = SRWLOCK_INIT;
bool g_haveOrigin = false;
bool g_recenter = false;
vr::Vec3 g_origin = {};
unsigned g_count = 0;
bool g_menuWasDown = false;
unsigned g_recenters = 0;

bool g_haveBody = false;
float g_bodyYaw = 0;
LARGE_INTEGER g_lastTime = {};
bool g_snapArmed = true;
float g_aspect = 1.0f;

float Seconds() {
    LARGE_INTEGER now, freq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&freq);
    float dt = g_lastTime.QuadPart ? (float)(now.QuadPart - g_lastTime.QuadPart) / freq.QuadPart : 0;
    g_lastTime = now;
    return dt > 0.25f ? 0.25f : dt;  // после паузы не прыгаем
}

void Turn(float stickX, float dt) {
    const float rad = vr::kPi / 180.0f;
    if (g_config.snapTurn) {
        if (stickX > 0.7f && g_snapArmed) { g_bodyYaw += g_config.snapAngle * rad; g_snapArmed = false; }
        if (stickX < -0.7f && g_snapArmed) { g_bodyYaw -= g_config.snapAngle * rad; g_snapArmed = false; }
        if (stickX > -0.3f && stickX < 0.3f) g_snapArmed = true;
    } else if (stickX > 0.15f || stickX < -0.15f) {
        // Стик вправо - поворот вправо (в Unreal это рост Yaw)
        g_bodyYaw += stickX * g_config.turnSpeed * rad * dt;
    }
}

}  // namespace

void Recenter() { g_recenter = true; }
void SetInGame(bool inGame) {
    AcquireSRWLockExclusive(&g_frameLock);
    g_frame.inGame = inGame;
    ReleaseSRWLockExclusive(&g_frameLock);
}
void SetAspect(float aspect) { if (aspect > 0.2f) g_aspect = aspect; }
float VerticalFov(float h) {
    float t = tanf(h * vr::kPi / 360.0f) / g_aspect;
    return atanf(t) * 360.0f / vr::kPi;
}
bool HasBody() { return g_haveBody; }
float BodyYaw() { return g_bodyYaw; }
void SetBodyYaw(float yaw) { g_bodyYaw = yaw; g_haveBody = true; }
void AddBodyYaw(float delta) { g_bodyYaw += delta; }

void Advance() {
    float dt = Seconds();
    xrapi::State s = xrapi::Get();
    Frame f = Current();
    f.active = s.valid;
    if (!s.valid) {
        AcquireSRWLockExclusive(&g_frameLock);
        g_state = s;
        g_frame = f;
        ReleaseSRWLockExclusive(&g_frameLock);
        return;
    }

    f.eye = g_config.stereo ? (f.eye ^ 1) : 0;
    f.sync = s.sync;
    f.frameId = s.frameId;
    f.head = vr::XrToUnreal(vr::FromQuat(s.hmdOrientation));

    // Поворот тела правым стиком - только в игре
    if (f.inGame&&!uistate::Cinematic()) Turn(s.right.stickX, dt);

    // Левое меню держать 1 с - центровка позиции головы (короткое нажатие = пауза в игре)
    static float menuHeld = 0;
    menuHeld = s.buttons[xrapi::L_MENU] ? menuHeld + dt : 0;
    if (menuHeld > 1.0f && !g_menuWasDown) { g_recenter = true; g_menuWasDown = true; }
    if (!s.buttons[xrapi::L_MENU]) g_menuWasDown = false;

    vr::Vec3 pos = vr::XrToUnreal(s.hmdPosition);
    // После сна шлема или его перепривязки к комнате начало координат шлема сдвигается: центруем
    // заново (паузы считает поток приёма по самим пакетам - подвисание игры сюда не попадает)
    {
        static unsigned seenResumes = 0;
        if (s.resumes != seenResumes) {
            if (g_haveOrigin) {
                g_recenter = true;
                Log("Headset data resumed or tracking jumped - recentring");
            }
            seenResumes = s.resumes;
        }
    }
    if (!g_haveOrigin || g_recenter) {
        g_origin = pos;
        g_haveOrigin = true;
        g_recenter = false;
        g_recenters++;
        Log("Recentred at %.3f %.3f %.3f m", s.hmdPosition.x, s.hmdPosition.y, s.hmdPosition.z);
    }
    f.headPos = g_config.positional ? (pos - g_origin) * g_config.unitsPerMetre : vr::Vec3{0, 0, 0};

    float ipd = s.ipd > 0.04f && s.ipd < 0.09f ? s.ipd : 0.063f;
    f.ipdUnits = ipd * g_config.ipdScale * g_config.unitsPerMetre;

    // Кадр закрывает поле шлема по вертикали; по горизонтали - сколько даёт форма кадра
    // (16:9 шире линзы, лишнее за краем). Так нет чёрных полос и масштаб мира настоящий.
    // Поле шлема - постоянное: WinlatorXR присылает обратно то поле, что запросил мод, и если
    // брать его отсюда, оно с каждым кадром растёт (16:9 шире по горизонтали) до 180 градусов
    float fov = pcvr::Enabled() && s.fovY > 1 ? s.fovY : g_config.headsetFov;
    if (g_config.fovOverride > 1) {
        f.fov = g_config.fovOverride;
        f.fovV = VerticalFov(f.fov);
    } else {
        f.fovV = fov;
        f.fov = atanf(tanf(fov * vr::kPi / 360.0f) * g_aspect) * 360.0f / vr::kPi;
        if (f.fov < fov) {  // кадр уже квадрата - держим поле по горизонтали
            f.fov = fov;
            f.fovV = VerticalFov(fov);
        }
    }

    if (g_config.logPoses && (g_count % 90) == 0) {
        Log("Pose #%u sync=%d eye=%d game=%d q=(%.3f %.3f %.3f %.3f) p=(%.3f %.3f %.3f) sticks=(%.2f %.2f | %.2f %.2f) fov=%.1fx%.1f alt=%.3f",
            s.packets, s.sync, f.eye, f.inGame, s.hmdOrientation.x, s.hmdOrientation.y, s.hmdOrientation.z,
            s.hmdOrientation.w, s.hmdPosition.x, s.hmdPosition.y, s.hmdPosition.z,
            s.left.stickX, s.left.stickY, s.right.stickX, s.right.stickY, s.fovX, s.fovY, s.altitude);
    }
    g_count++;
    AcquireSRWLockExclusive(&g_frameLock);
    g_state = s;
    g_frame = f;
    ReleaseSRWLockExclusive(&g_frameLock);
}

Snapshot Capture() {
    AcquireSRWLockShared(&g_frameLock);
    Snapshot snapshot = {g_frame, g_state};
    ReleaseSRWLockShared(&g_frameLock);
    return snapshot;
}
Frame Current() { return Capture().frame; }
xrapi::State State() { return Capture().state; }
unsigned RecenterCount() { return g_recenters; }

}  // namespace vrframe
