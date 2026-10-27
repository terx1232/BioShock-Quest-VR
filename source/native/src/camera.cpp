// Камера игры следует за шлемом.
//
// APlayerController::eventPlayerCalcView (Bioshock.exe 1.1, Steam, 0x10932bd0) запускает
// скриптовое событие PlayerCalcView, через которое проходит каждый вид игры: рендер, звук,
// видимость. После него камера получает ориентацию головы поверх поворота тела (тело
// поворачивает мод правым стиком), позицию головы (6DoF) и половину межзрачкового расстояния
// для глаза этого кадра. Итоговый поворот пишется в контроллер игрока: прицел и ходьба идут
// туда, куда смотрит голова.
#include "camera.h"
#include "engineviewport.h"

#include <windows.h>
#include <math.h>
#include <string.h>

#include "MinHook.h"
#include "config.h"
#include "crash.h"
#include "input.h"
#include "hud.h"
#include "uistate.h"
#include "log.h"
#include "uobject.h"
#include "vrframe.h"
#include "weapon.h"
#include "plasmid.h"
#include "navigation.h"
#include "mouse.h"
#include "xrapi.h"

namespace camera {
namespace {

const uintptr_t kCalcView = 0x10932bd0;
// sub esp,1Ch / push ebx / push ebp / mov ebp,[esp+28h] / mov eax,[ebp+0]
const unsigned char kCalcViewBytes[] = {0x83, 0xEC, 0x1C, 0x53, 0x55, 0x8B, 0x6C, 0x24, 0x28, 0x8B, 0x45, 0x00};
const int kActorRotation = 0x1cc;  // AActor::Rotation (Pitch, Yaw, Roll)
const int kActorLocation = 0x1c0;  // AActor::Location

// __thiscall через __fastcall: ecx = this, edx не используется, 3 аргумента снимает вызываемая
typedef void(__fastcall* CalcView)(void* self, void* edx, void** viewActor, float* location, int* rotation);
CalcView g_original = nullptr;
bool g_fovChecked = false;
bool g_fovUsable = false;

volatile LONG g_calls = 0;     // вызовы за кадр: 0 значит меню/загрузка/ролик
volatile LONG g_camSync = -1;  // номер позы, с которой камера посчитала последний вид
alignas(8) volatile LONG64 g_camFrameId = 0;
volatile LONG g_camTicks = 0;
DWORD g_camThread = 0;
int g_lastWrittenYaw = 0;
vr::Vec3 g_headWorld = {};
vr::Mat3 g_bodyWorld = {};
bool g_haveHead = false;
bool g_haveWritten = false;

// AActor::moveSmooth(FVector Delta) (0x10a9fdf0, thiscall, вектор по значению, ret 0xc):
// сдвиг актора со столкновениями, подъёмом на ступеньки и скольжением вдоль стен
typedef int(__fastcall* MoveSmoothFn)(void* actor, void* edx, float x, float y, float z);
MoveSmoothFn const kMoveSmooth = (MoveSmoothFn)0x10a9fdf0;
const unsigned char kMoveSmoothBytes[] = {0x81, 0xEC, 0x9C, 0x00, 0x00, 0x00, 0x53, 0x55};
int g_pawnOff = -1, g_collisionHeightOff = -1, g_eyeHeightOff = -1;
bool g_moveSmoothOk = false;
vr::Vec3 g_consumed = {};      // часть шагов по комнате, уже пройденная персонажем (мир игры)
unsigned g_recenterSeen = 0;
float g_prevBodyYaw = 0;
bool g_haveBodyYaw = false;

const DWORD kPlayerHandsVtable = 0x11059cd4;  // PlayerHands (руки от первого лица, к костям крепится оружие)
int g_handsOff = -1;
bool g_handsLogged = false;

// Руки от первого лица (и прикреплённое к ним оружие) - в позу правого контроллера в мире игры
void PlaceHands(void* pawn, void* held, vr::Vec3 head, const vr::Mat3& body, const xrapi::State& s) {
    if (g_handsOff < 0) g_handsOff = uobject::PropertyOffset(pawn, L"Hands");
    if (g_handsOff <= 0) return;
    void* hands = *(void**)((char*)pawn + g_handsOff);
    if (!hands || IsBadReadPtr(hands, 0x1e0) || *(DWORD*)hands != kPlayerHandsVtable) return;
    if (!s.valid) return;
    vr::Vec3 rel = vr::XrToUnreal(s.right.position - s.hmdPosition) * g_config.unitsPerMetre;
    vr::Vec3 hand = head + vr::Mul(body, rel);
    vr::Mat3 grip = vr::Mul(body, vr::XrToUnreal(vr::FromQuat(s.right.grip)));
    vr::Rotator adjust = {(int)(g_config.weaponPitch * 65536.0f / 360.0f), 0, 0};
    vr::Mat3 rot = vr::Mul(grip, vr::FromRotator(adjust));
    vr::Vec3 origin = hand - vr::Mul(rot, g_config.weaponOffset);
    float* loc = (float*)((char*)hands + kActorLocation);
    int* r = (int*)((char*)hands + kActorRotation);
    if (!g_handsLogged) {
        g_handsLogged = true;
        Log("Hands: %p game pose %.1f %.1f %.1f rot %d %d %d -> hand %.1f %.1f %.1f", hands, loc[0], loc[1], loc[2], r[0], r[1],
            r[2], origin.x, origin.y, origin.z);
        if (g_config.debugDump) {
            Log("Hands: natives GetBoneCoords %08lx GetBoneLocation %08lx GetBoneRotation %08lx", *(DWORD*)0x11130af0,
                *(DWORD*)0x11130b50, *(DWORD*)0x11130b44);
            uobject::DumpObjectRefs(hands, "hands");
            uobject::DumpBoneNames(*(void**)((char*)hands + 0x110));  // Mesh
            void* held = *(void**)((char*)pawn + uobject::PropertyOffset(pawn, L"ActiveHoldable"));
            if (held) {
                uobject::DumpObjectRefs(held, "holdable");
                uobject::DumpProperties(held, "holdable");
            }
            uobject::DumpProperties(hands, "hands");
        }
    }
    // руки Джека скрыты, видно только оружие (оно отдельный актор, прикреплённый к рукам)
    static int hidOff = 0;
    static unsigned hidMask = 0;
    static bool hidChecked = false;
    if (!hidChecked) {
        hidChecked = true;
        if (uobject::BoolProperty(hands, L"bHidden", hidOff, hidMask)) {
            if(g_config.hideMask)hidMask=g_config.hideMask;
            Log("Hands: bHidden at +0x%x mask %08x", hidOff, hidMask);
        } else Log("Hands: bHidden reflection unavailable; mesh filter retained");
    }
    // A hidden Hands actor can suppress its attached weapon in the game's
    // foreground pass. Prefer the existing mesh-only filter when configured.
    if (hidMask && !g_config.armsVertices[0]) {
        unsigned* w = (unsigned*)((char*)hands + hidOff);
        *w = g_config.hideArms ? (*w | hidMask) : (*w & ~hidMask);
    }
    vr::Rotator rr = vr::ToRotator(rot);
    // предмет в руке - жёстко в контроллере (поза покоя относительно рук, без их анимаций)
    // Models use their native +X forward/+Z up axes. The aim pose is also
    // what firing uses; no animated hand-bone rotation or -40deg hand offset.
    vr::Mat3 modelRotation=vr::Mul(body,vr::XrToUnreal(vr::FromQuat(s.right.orientation)));
    weapon::HoldRigid(held, hands, origin, vr::FromRotator(rr), hand,&modelRotation);
    bool ability=plasmid::Observe(hands);weapon::SetAbilityMode(ability);
    if(ability){
        vr::Vec3 left=head+vr::Mul(body,vr::XrToUnreal(s.left.position-s.hmdPosition)*g_config.unitsPerMetre);
        vr::Mat3 aim=vr::Mul(body,vr::XrToUnreal(vr::FromQuat(s.left.orientation)));
        if(plasmid::Anchor(hands,left,aim))return;
        // If bone reflection is unavailable, still put native emitters on the
        // left side. Full arms remain hidden; no guessed hand calibration.
        origin=left-vr::Mul(aim,g_config.weaponOffset);rr=vr::ToRotator(aim);
    }
    loc[0] = origin.x;
    loc[1] = origin.y;
    loc[2] = origin.z;
    r[0] = rr.pitch;
    r[1] = rr.yaw;
    r[2] = rr.roll;
}

int AngleDiff(int a, int b) {  // в единицах Unreal, -32768..32767
    return (short)(unsigned short)((a - b) & 0xFFFF);
}

// время процессора, которое мод тратит в камере (после игровой CalcView), для лога
volatile LONG64 g_modTicks = 0;
bool Readable(const void* p,size_t n);
struct ModTimer {
#ifndef BIOSHOCK_RELEASE
    LARGE_INTEGER start;
    ModTimer() { QueryPerformanceCounter(&start); }
    ~ModTimer() {
        LARGE_INTEGER end;
        QueryPerformanceCounter(&end);
        InterlockedExchangeAdd64(&g_modTicks, end.QuadPart - start.QuadPart);
    }
#endif
};

void __fastcall HookedCalcView(void* self, void* edx, void** viewActor, float* location, int* rotation) {
    g_original(self, edx, viewActor, location, rotation);
    ModTimer timer;
    uistate::UpdateCinematic(self);
    const bool cinematic=uistate::Cinematic();
    // Loading/menu cameras can call PlayerCalcView before a possessed pawn
    // exists. Count only a real player view, even before VR is enabled.
    if(g_pawnOff<0)g_pawnOff=uobject::PropertyOffset(self,L"Pawn");
    void* viewPawn=g_pawnOff>0?*(void**)((char*)self+g_pawnOff):nullptr;
    if(!viewPawn||!viewActor||(!cinematic&&*viewActor!=viewPawn)||!Readable(viewPawn,0x1d8))return;
    static void* pawnClass=nullptr;static int controllerOff=-1;
    void* cls=*(void**)((char*)viewPawn+0x2c);
    if(cls!=pawnClass){pawnClass=cls;controllerOff=uobject::PropertyOffset(viewPawn,L"Controller");}
    if(controllerOff>0&&(!Readable((char*)viewPawn+controllerOff,4)||*(void**)((char*)viewPawn+controllerOff)!=self))return;
    if(!location||!isfinite(location[0])||!isfinite(location[1])||!isfinite(location[2]))return;
    InterlockedIncrement(&g_calls);
    if (!g_config.enabled) return;
    const vrframe::Snapshot snapshot = vrframe::Capture();
    const vrframe::Frame& f = snapshot.frame;
    if (!f.active || !f.inGame) return;
    hud::UpdateReticle(self,viewPawn);
    // кадр должен уйти в шлем с номером именно этой позы (Present может идти на другом потоке)
    InterlockedExchange(&g_camSync, f.sync);
    InterlockedExchange64(&g_camFrameId, f.frameId);
    InterlockedIncrement(&g_camTicks);
    g_camThread = GetCurrentThreadId();

    // FOV лежит в PlayerController; один раз проверяем, что по смещению действительно угол
    float* fov = (float*)((char*)self + g_config.fovOffset);
    if (!g_fovChecked) {
        g_fovChecked = true;
        g_fovUsable = *fov > 30.0f && *fov < 170.0f;
        Log("Camera: FovAngle at +0x%x = %.2f (%s), next float = %.2f", g_config.fovOffset, *fov,
            g_fovUsable ? "used" : "ignored, not an angle", fov[1]);
    }
    if (g_fovUsable) {
        fov[0] = f.fov;
        // следующее поле - поле зрения оружия (60): руки от первого лица рисуются своим проходом с ним,
        // и в руке контроллера оказались бы в другом месте кадра; приравниваем к полю шлема
        if (fov[1] > 30.0f && fov[1] < 170.0f) fov[1] = f.fov;
    }

    if(cinematic){
        // Retain the original first-person script's animated eye anchor. Apply
        // head tracking without moving the pawn or replacing the script's actor.
        vr::Mat3 anchor=vr::FromRotator({rotation[0],rotation[1],rotation[2]});
        vrframe::SetBodyYaw(rotation[1]/vr::kUnrealPerRadian);
        vr::Mat3 view=vr::Mul(anchor,f.head);vr::Rotator r=vr::ToRotator(view);
        rotation[0]=r.pitch;rotation[1]=r.yaw;rotation[2]=r.roll;
        vr::Vec3 offset=vr::Mul(anchor,f.headPos);
        if(g_config.stereo)offset=offset+vr::Column(view,1)*((f.eye==0?-.5f:.5f)*f.ipdUnits);
        location[0]+=offset.x;location[1]+=offset.y;location[2]+=offset.z;
        g_haveWritten=false;g_haveHead=false;weapon::ReleaseRigid();weapon::SetAbilityMode(false);plasmid::Reset();
        return;
    }

    // CalcFirstPersonView adds lean and CameraAnimationOffsetRotation to
    // its output. A shotgun's cosmetic yaw must never become a body turn:
    // repeated camera queries would accumulate it into the entire VR world.
    // Actor.Rotation retains real spawn/script turns and our last hand aim.
    int* controller = (int*)((char*)self + kActorRotation);
    int gameYaw = controller[1];
    if(abs(AngleDiff(rotation[1],gameYaw))>1000){
        static unsigned logged=0;
        if(logged++<8)Log("VR camera: cosmetic view yaw %d excluded from body turn; native actor yaw %d retained",AngleDiff(rotation[1],gameYaw),gameYaw);
    }
    if (!vrframe::HasBody()) {
        vrframe::SetBodyYaw(gameYaw / vr::kUnrealPerRadian);
    } else if (g_haveWritten && abs(AngleDiff(gameYaw, g_lastWrittenYaw)) > 1000) {
        vrframe::AddBodyYaw(AngleDiff(gameYaw, g_lastWrittenYaw) / vr::kUnrealPerRadian);
    }

    vr::Mat3 body = vr::YawMatrix(vrframe::BodyYaw());
    vr::Mat3 view = vr::Mul(body, f.head);
    vr::Rotator r = vr::ToRotator(view);
    mouse::MeleePitch(static_cast<short>(r.pitch-rotation[0])*360.f/65536.f,
        g_config.meleePitchServo&&weapon::MeleeEquipped()&&!uistate::ContainerOpen());
    rotation[0] = r.pitch;
    rotation[1] = r.yaw;
    rotation[2] = r.roll;

    // Прицел и направление ходьбы - по взгляду (без крена)
    vr::Rotator aim = r;
    int hand = input::AimHand();
    if (hand >= 0&&!(hand==1&&weapon::MeleeEquipped())) {
        // курок зажат: игра стреляет туда, куда направлен контроллер (поза прицеливания)
        const xrapi::State& st = snapshot.state;
        const xrapi::Hand& h = hand == 0 ? st.left : st.right;
        aim = vr::ToRotator(vr::Mul(body, vr::XrToUnreal(vr::FromQuat(h.orientation))));
    }
    controller[0] = aim.pitch;
    controller[1] = aim.yaw;
    controller[2] = 0;
    g_lastWrittenYaw = aim.yaw;  // иначе прицел рукой приняли бы за поворот от игры
    g_haveWritten = true;

    vr::Vec3 offset = vr::Mul(body, f.headPos);
    void* pawn = nullptr;
    if (g_pawnOff < 0) g_pawnOff = uobject::PropertyOffset(self, L"Pawn");
    if (g_pawnOff > 0) pawn = *(void**)((char*)self + g_pawnOff);
    if (pawn && g_collisionHeightOff < 0) g_collisionHeightOff = uobject::PropertyOffset(pawn, L"CollisionHeight");

    // Ходьба по комнате: персонаж идёт туда, куда физически шагнула голова (moveSmooth -
    // со стенами); камера остаётся над головой, неотработанный остаток - наклон (до 25 см)
    if (vrframe::RecenterCount() != g_recenterSeen) {
        g_recenterSeen = vrframe::RecenterCount();
        g_consumed = {0, 0, 0};
    }
    if (g_haveBodyYaw && vrframe::BodyYaw() != g_prevBodyYaw) {
        // поворот тела: уже пройденное поворачивается вместе с ним, персонаж не сдвигается
        vr::Vec3 c = vr::Mul(vr::YawMatrix(vrframe::BodyYaw() - g_prevBodyYaw), g_consumed);
        g_consumed = c;
    }
    g_prevBodyYaw = vrframe::BodyYaw();
    g_haveBodyYaw = true;
    if (g_config.roomScale && g_moveSmoothOk && pawn) {
        vr::Vec3 want = {offset.x - g_consumed.x, offset.y - g_consumed.y, 0};
        if (want.x * want.x + want.y * want.y > 1.0f) {
            float* p = (float*)((char*)pawn + kActorLocation);
            float bx = p[0], by = p[1];
            *(unsigned*)((char*)pawn + 0x2ec) &= 0xffbfffffu;  // как в execMoveSmooth
            kMoveSmooth(pawn, nullptr, want.x, want.y, 0.0f);
            float mx = p[0] - bx, my = p[1] - by;
            g_consumed.x += mx;
            g_consumed.y += my;
            location[0] += mx;  // вид этого кадра уже посчитан от старого места
            location[1] += my;
        }
        float rx = offset.x - g_consumed.x, ry = offset.y - g_consumed.y;
        float len = sqrtf(rx * rx + ry * ry), lim = 0.25f * g_config.unitsPerMetre;
        if (len > lim) {
            rx *= lim / len;
            ry *= lim / len;
        }
        offset.x = rx;
        offset.y = ry;
    }

    if (g_config.debugDump && pawn) {
        static bool refs = false;
        if (!refs) {
            refs = true;
            uobject::DumpObjectRefs(self, "controller");
            uobject::DumpObjectRefs(pawn, "pawn");
        }
        static DWORD last = 0;
        if (GetTickCount() - last > 1500) {
            last = GetTickCount();
            float* p = (float*)((char*)pawn + kActorLocation);
            Log("Room: pawn %.1f %.1f %.1f, head offset %.1f %.1f, walked %.1f %.1f", p[0], p[1], p[2], vr::Mul(body, f.headPos).x,
                vr::Mul(body, f.headPos).y, g_consumed.x, g_consumed.y);
        }
    }
    // Без покачивания при ходьбе: игра добавляет к виду "head bob" и рывки высоты глаз на
    // ступеньках - в шлеме от этого качается весь мир. Камера - ровно над персонажем на высоте глаз.
    if (g_config.noBob && pawn && viewActor && *viewActor == pawn) {
        if (g_eyeHeightOff < 0) {
            g_eyeHeightOff = uobject::PropertyOffset(pawn, L"EyeHeight");
            Log("Camera: EyeHeight at +0x%x", g_eyeHeightOff);
        }
        float* p = (float*)((char*)pawn + kActorLocation);
        if (g_config.bobLog) {
            static DWORD last = 0;
            if (GetTickCount() - last > 300) {
                last = GetTickCount();
                Log("Bob: engine eye - pawn = %.2f %.2f %.2f, EyeHeight %.2f, BaseEyeHeight %.2f", location[0] - p[0],
                    location[1] - p[1], location[2] - p[2], *(float*)((char*)pawn + 0x51c),
                    g_eyeHeightOff > 0 ? *(float*)((char*)pawn + g_eyeHeightOff) : -1.0f);
            }
        }
        // только обычный вид от первого лица: камера игры рядом с персонажем (покачивание - единицы
        // сантиметров). Во время загрузки персонаж ещё не на месте - вид не трогаем
        float dx = location[0] - p[0], dy = location[1] - p[1], dz = location[2] - p[2];
        bool firstPerson = dx * dx + dy * dy < 30.0f * 30.0f && dz > -40.0f && dz < 150.0f;
        if (firstPerson) {
        location[0] = p[0];
        location[1] = p[1];
        }
        if (firstPerson && g_eyeHeightOff > 0) {
            // высота глаз персонажа (с приседанием), без раскачки ходьбы
            float eye = *(float*)((char*)pawn + g_eyeHeightOff);
            if (eye > 5.0f && eye < 200.0f) location[2] = p[2] + eye;
        }
    }
    // Высота: глаза на реальной высоте игрока над полом (по шлему), а не на высоте глаз Джека
    if (g_config.realHeight && pawn && g_collisionHeightOff > 0) {
        float alt = snapshot.state.altitude;
        bool altOk = alt > 0.5f && alt < 2.4f;
        float floorZ = ((float*)((char*)pawn + kActorLocation))[2] - *(float*)((char*)pawn + g_collisionHeightOff);
        if (altOk) {
            location[2] = floorZ + alt * g_config.unitsPerMetre;
            offset.z = 0;
        } else {
            location[2] = floorZ + (g_config.playerHeight - 0.11f) * g_config.unitsPerMetre;  // + наклоны головы (offset.z)
        }
    }
    g_headWorld = {location[0] + offset.x, location[1] + offset.y, location[2] + offset.z};
    g_bodyWorld = body;
    g_haveHead = true;
    if(g_config.navigationGeometry){auto forward=vr::Column(view,0);navigation::Update(viewPawn,r.yaw,&g_headWorld,&forward);}
    void* held = nullptr;
    if (pawn) {
        // смещение ищем заново для каждого класса персонажа: во время загрузки камера может считаться
        // для другого актора, и запомненное "свойства нет" оставило бы мод без предмета в руке
        static int heldOff = -1;
        static void* heldClass = nullptr;
        void* cls = *(void**)((char*)pawn + 0x2c);
        if (cls != heldClass) {
            heldClass = cls;
            heldOff = uobject::PropertyOffset(pawn, L"ActiveHoldable");
            Log("Camera: pawn %ls, ActiveHoldable at +0x%x", uobject::ClassName(pawn), heldOff);
        }
        held = heldOff > 0 ? *(void**)((char*)pawn + heldOff) : nullptr;
        weapon::NoteHoldable(held);
        static void* watched = nullptr;
        if (g_config.debugDump && held && held != watched) {
            watched = held;
            crash::WatchWrites((char*)held + kActorLocation, 12);  // кто ставит предмет в руку
        }
    }
    if (g_config.weaponInHand && pawn) PlaceHands(pawn, held, g_headWorld, body, snapshot.state);
    float eyeSide = f.eye == 0 ? -0.5f : 0.5f;
    if (g_config.stereo) offset = offset + vr::Column(view, 1) * (eyeSide * f.ipdUnits);
    location[0] += offset.x;
    location[1] += offset.y;
    location[2] += offset.z;
}

// HorizontalFOVLock: без него игра пересчитывает FOV под 4:3, и при квадратном или 16:9 кадре
// мир получается не того масштаба, что в шлеме. Расчёт проекции (0x10a557a0) читает флаг как
// Viewport->[+0x84]->[+0x90]; объект окна ищется по vtable UWindowsViewport.
bool Readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mbi;
    return VirtualQuery(p, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT &&
           (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_READONLY)) &&
           (BYTE*)p + n <= (BYTE*)mbi.BaseAddress + mbi.RegionSize;
}

}  // namespace

bool HeadWorld(vr::Vec3& head, vr::Mat3& body) {
    head = g_headWorld;
    body = g_bodyWorld;
    return g_haveHead;
}

bool SetEngineSize(unsigned w,unsigned h,unsigned backW,unsigned backH) {
    return engineviewport::SetSize(w,h,backW,backH);
}
void GraphicsReady(){engineviewport::DeviceCreated();}
bool ViewportReady(){return engineviewport::Ready();}
bool EngineSizeMatches(unsigned w,unsigned h){return engineviewport::Matches(w,h);}
unsigned ViewportHeight(){return engineviewport::Height();}


int TakeCallCount() { return (int)InterlockedExchange(&g_calls, 0); }
bool HasPendingView(){return InterlockedCompareExchange(&g_calls,0,0)>0;}
double TakeModMs() {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return InterlockedExchange64(&g_modTicks, 0) * 1000.0 / f.QuadPart;
}
int CameraSync() { return (int)g_camSync; }
unsigned long long CameraFrameId() { return InterlockedCompareExchange64(&g_camFrameId, 0, 0); }
unsigned CameraTicks() { return (unsigned)g_camTicks; }
unsigned long CameraThread() { return g_camThread; }

bool Install() {
    if (memcmp((void*)kCalcView, kCalcViewBytes, sizeof(kCalcViewBytes)) != 0) {
        const unsigned char* p = (const unsigned char*)kCalcView;
        Log("Camera: eventPlayerCalcView not found at 0x%08x (bytes %02x %02x %02x %02x) - other exe version?",
            (unsigned)kCalcView, p[0], p[1], p[2], p[3]);
        return false;
    }
    if (MH_CreateHook((void*)kCalcView, (void*)HookedCalcView, (void**)&g_original) != MH_OK ||
        MH_EnableHook((void*)kCalcView) != MH_OK) {
        Log("Camera: hook failed");
        return false;
    }
    g_moveSmoothOk = memcmp((void*)0x10a9fdf0, kMoveSmoothBytes, sizeof(kMoveSmoothBytes)) == 0;
    Log("Camera: eventPlayerCalcView hooked, moveSmooth %s", g_moveSmoothOk ? "found" : "NOT found (room-scale off)");
    return true;
}

}  // namespace camera
