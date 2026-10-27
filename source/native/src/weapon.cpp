// Оружие и руки игрока следуют за правым контроллером.
//
// Руки от первого лица (AHands, к костям которых крепится оружие) и оружие (APlayerWeapon)
// рисуются через UCanvas::DrawActor (Bioshock.exe 1.1, 0x10a2bdc0). На время этой отрисовки
// мод ставит актору позу правого контроллера в мире игры и FOV сцены (иначе в VR оружие
// искажается), затем возвращает настоящие значения - логика игры их не видит.
#include "weapon.h"
#include "weaponpose.h"
#include "weaponlayer.h"
#include "weaponassets.h"
#include "melee.h"

#include <windows.h>
#include <math.h>
#include <string.h>

#include "MinHook.h"
#include "camera.h"
#include "config.h"
#include "hud.h"
#include "uistate.h"
#include "log.h"
#include "uobject.h"
#include "vrframe.h"
#include "xrapi.h"

namespace weapon {
namespace {

const uintptr_t kDrawActor = 0x10a2bdc0;
const DWORD kHandsVtable = 0x11059cd4;   // AHands
const DWORD kWeaponVtable = 0x1105a3b4;  // APlayerWeapon
const int kLocation = 0x1c0, kRotation = 0x1cc;

typedef void(__fastcall* DrawActorFn)(void* canvas, void* edx, void* actor, int wire, int clearZ, float fov);
DrawActorFn g_original = nullptr;
volatile LONG g_drawn = 0;
volatile LONG g_melee = 0;       // в руке оружие ближнего боя (гаечный ключ)
volatile LONG g_abilityMode=0;
DWORD g_meleeSeen = 0;           // когда его рисовали последний раз
void* g_lastWeapon = nullptr;
bool g_logged = false;

void __fastcall HookedDrawActor(void* canvas, void* edx, void* actor, int wire, int clearZ, float fov) {
    const vrframe::Frame& f = vrframe::Current();
    DWORD vt = actor ? *(DWORD*)actor : 0;
    if (vt == kWeaponVtable) {
        if (actor != g_lastWeapon) {
            g_lastWeapon = actor;
            const wchar_t* cls = uobject::ClassName(actor);
            bool melee = cls && (wcsstr(cls, L"Wrench") || wcsstr(cls, L"wrench") || wcsstr(cls, L"Melee"));
            InterlockedExchange(&g_melee, melee ? 1 : 0);
            Log("Weapon: in hand %ls%s", cls ? cls : L"?", melee ? " (melee: swing to hit)" : "");
        }
        g_meleeSeen = GetTickCount();
    }
    // Отладка: какие акторы рисуются поверх сцены (первые 12 разных)
    static DWORD seen[12];
    static int seenCount = 0;
    if (seenCount < 12) {
        bool known = false;
        for (int i = 0; i < seenCount; i++) known |= seen[i] == vt;
        if (!known) {
            seen[seenCount++] = vt;
            Log("Weapon: DrawActor actor vtable %08lx fov %.1f", vt, fov);
        }
    }
    bool hands = vt == kHandsVtable || vt == kWeaponVtable;
    if(g_config.enabled&&hands&&uistate::Cinematic())return;
    if (!g_config.enabled || !g_config.weaponInHand || !f.active || !f.inGame || !hands) {
        if (hands) hud::Suspend();
        g_original(canvas, edx, actor, wire, clearZ, fov);
        if (hands) hud::Resume();
        return;
    }
    vr::Vec3 head;
    vr::Mat3 body;
    if (!camera::HeadWorld(head, body)) {
        hud::Suspend();
        g_original(canvas, edx, actor, wire, clearZ, fov);
        hud::Resume();
        return;
    }
    if(vt==kHandsVtable&&g_abilityMode){
        hud::Suspend();g_original(canvas,edx,actor,wire,clearZ,f.fov);hud::Resume();
        InterlockedExchange(&g_drawn,1);return;
    }
    xrapi::State s = vrframe::State();
    // Рука относительно головы (оси OpenXR, метры) -> мир игры
    vr::Vec3 rel = vr::XrToUnreal(s.right.position - s.hmdPosition) * g_config.unitsPerMetre;
    vr::Vec3 hand = head + vr::Mul(body, rel);
    vr::Mat3 grip = vr::Mul(body, vr::XrToUnreal(vr::FromQuat(s.right.grip)));
    // Рукоятка контроллера наклонена; оружие держим по направлению рукоятки с поправкой
    vr::Rotator adjust = {(int)(g_config.weaponPitch * 65536.0f / 360.0f), 0, 0};
    vr::Mat3 rot = vr::Mul(grip, vr::FromRotator(adjust));
    // Начало координат модели рук - у глаза; правая кисть в ней примерно в WeaponOffset
    vr::Vec3 origin = hand - vr::Mul(rot, g_config.weaponOffset);

    float* loc = (float*)((char*)actor + kLocation);
    int* r = (int*)((char*)actor + kRotation);
    float savedLoc[3] = {loc[0], loc[1], loc[2]};
    int savedRot[3] = {r[0], r[1], r[2]};
    vr::Rotator rr = vr::ToRotator(rot);
    loc[0] = origin.x;
    loc[1] = origin.y;
    loc[2] = origin.z;
    r[0] = rr.pitch;
    r[1] = rr.yaw;
    r[2] = rr.roll;
    if (!g_logged) {
        g_logged = true;
        Log("Weapon: %s drawn in hand (game fov %.1f -> %.1f)", vt == kHandsVtable ? "AHands" : "APlayerWeapon", fov, f.fov);
    }
    hud::Suspend();  // оружие - в кадр, а не в текстуру HUD
    g_original(canvas, edx, actor, wire, clearZ, f.fov);
    hud::Resume();
    memcpy(loc, savedLoc, sizeof(savedLoc));
    memcpy(r, savedRot, sizeof(savedRot));
    InterlockedExchange(&g_drawn, 1);
}

// --- Предмет в руке жёстко в контроллере ------------------------------------------------------
// Оружие и гаечный ключ - отдельные акторы, прикреплённые к кости рук ("Wrench", "Pistol"...).
// Движок каждый кадр ставит их по анимированной кости (0x109a01b0, установка прикреплённого
// актора: thiscall, владелец, прикреплённый, два флага), поэтому предмет повторял все анимации
// рук: взмах ключа, покачивание при ходьбе, перезарядку. Мод запоминает, как предмет лежит
// относительно рук в покое, и после каждой установки движком ставит его сам - по контроллеру.
// Столкновения и удары движок по-прежнему считает по кости, мод меняет только то, что видно.
const uintptr_t kUpdateAttachment = 0x109a01b0;
// push ebp / mov ebp,esp / and esp,-16 / push -1 / push 10F4F40Bh
const unsigned char kUpdateAttachmentBytes[] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x6A, 0xFF, 0x68, 0x0B, 0xF4, 0xF4, 0x10};
typedef int(__fastcall* UpdateAttachmentFn)(void* inst, void* edx, void* owner, void* attached, int a, int b);
UpdateAttachmentFn g_updateAttachment = nullptr;

void* g_held = nullptr;          // предмет в руке
void* g_hands = nullptr;
void* g_rigid = nullptr;         // его ставит мод (поза покоя уже известна)
DWORD g_heldSince = 0;
vr::Vec3 g_relPos = {};          // поза покоя: предмет относительно актора рук
vr::Mat3 g_relRot = {};
vr::Vec3 g_lastRelPos = {};
vr::Mat3 g_lastRelRot = {};
int g_stable = 0;
bool g_haveRel = false;
vr::Vec3 g_wantLoc = {};         // куда ставить предмет в этом кадре (мир игры)
vr::Rotator g_wantRot = {};
bool g_holding = false;
bool g_wrenchGeometry = false;
bool g_pistolGeometry = false;
weaponassets::Model g_model=weaponassets::Model::None;
unsigned g_updates = 0;           // установок предмета движком с последней смены (для лога)        // игровой вид: предмет ставит мод (в меню и роликах - движок)
int g_scaleOffset=-1;float g_nativeScale=1;
void* g_scaleClass=nullptr;int g_scaleName=0;

vr::Mat3 ActorRot(void* a) { return vr::FromRotator(*(vr::Rotator*)((char*)a + kRotation)); }
vr::Vec3 ActorLoc(void* a) { return *(vr::Vec3*)((char*)a + kLocation); }

void Place(void* a) {
    *(vr::Vec3*)((char*)a+kLocation)=g_wantLoc;
    *(vr::Rotator*)((char*)a+kRotation)=g_wantRot;
    if(g_config.noWeaponAnimations&&g_scaleOffset>0&&!IsBadWritePtr((char*)a+g_scaleOffset,4))
        *(float*)((char*)a+g_scaleOffset)=g_nativeScale*(g_wrenchGeometry?g_config.wrenchScale:1);
}

// Leave animation time/notifies and hidden Hands logic running. Only the
// held model's pose blend is zeroed through the game's reflected API.
void NeutralModelAnimation(void* actor){
    if(!actor||!g_config.noWeaponAnimations)return;
    using Event=void(__fastcall*)(void*,void*,void*,void*,void*);
    static void* cls=nullptr;static void* weight=nullptr;static void* valid=nullptr;static int result=-1,weightResult=-1,offsets[4]={};static bool accepted=false;
    void* type=*(void**)((char*)actor+0x2c);
    if(type!=cls){
        cls=type;weight=uobject::FindField(actor,L"SetAnimationWeight");valid=uobject::FindField(actor,L"IsAnimationHandleValid");result=weightResult=-1;accepted=false;
        if(weight&&(!uobject::ClassName(weight)||wcscmp(uobject::ClassName(weight),L"Function")))weight=nullptr;
        if(valid&&(!uobject::ClassName(valid)||wcscmp(uobject::ClassName(valid),L"Function")))valid=nullptr;
        auto returnOffset=[](void* function){
            if(!function||IsBadReadPtr((char*)function+0x54,4))return -1;
            unsigned fields=0;
            for(void* p=*(void**)((char*)function+0x54);p&&fields++<4096&&!IsBadReadPtr(p,0x70);p=*(void**)((char*)p+0x3c)){
                const wchar_t* name=uobject::ObjectName(p);
                if(name&&!wcscmp(name,L"ReturnValue")){int off=*(int*)((char*)p+0x6c);return off>=4&&off<=28?off:-1;}
            }
            return -1;
        };
        result=returnOffset(valid);weightResult=returnOffset(weight);
        const wchar_t* names[]={L"AnimationHandle",L"EquippingAnimationHandle",L"UnEquippingAnimationHandle",L"IdlingAnimationHandle"};
        for(unsigned i=0;i<4;i++)offsets[i]=uobject::PropertyOffset(actor,names[i]);
        Log("Weapon pose: reflected visual blend API weight=%d valid=%d returns=+0x%x/+0x%x; gameplay animation clocks retained",weight!=nullptr,valid!=nullptr,result,weightResult);
    }
    if(!weight||!valid||result<0||IsBadReadPtr(*(void***)actor,16))return;
    Event event=(Event)(*(void***)actor)[3];
    for(int off:offsets){
        if(off<=0||off>0x10000||IsBadReadPtr((char*)actor+off,4))continue;
        int handle=*(int*)((char*)actor+off);if(handle<0)continue;
        alignas(4) unsigned params[8]={};params[0]=(unsigned)handle;
        event(actor,nullptr,valid,params,nullptr);
        if(!*(unsigned*)((char*)params+result))continue;
        memset(params,0,sizeof(params));params[0]=(unsigned)handle;
        event(actor,nullptr,weight,params,nullptr); // weight=0, playback rate/time unchanged
        if(!accepted&&weightResult>=8&&*(unsigned*)((char*)params+weightResult)){
            accepted=true;Log("Weapon pose: native SetAnimationWeight accepted zero visual blend on %ls; animation time/notifies retained",uobject::ClassName(actor));
        }
    }
}

// Пока поза покоя не известна: после установки движком смотрим, где предмет относительно рук.
// Поза покоя - когда после смены предмета прошло больше секунды (анимация доставания кончилась)
// и 10 кадров подряд предмет почти не двигается относительно рук.
void Observe(void* owner, void* attached) {
    vr::Mat3 hr = ActorRot(owner), ht = vr::Transpose(hr);
    vr::Vec3 rel = vr::Mul(ht, ActorLoc(attached) - ActorLoc(owner));
    vr::Mat3 rot = vr::Mul(ht, ActorRot(attached));
    DWORD age = GetTickCount() - g_heldSince;
    if (g_haveRel) {
        vr::Vec3 d = rel - g_lastRelPos;
        float dr = 0;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) dr = fmaxf(dr, fabsf(rot.m[i][j] - g_lastRelRot.m[i][j]));
        g_stable = (d.x * d.x + d.y * d.y + d.z * d.z < 0.25f && dr < 0.01f) ? g_stable + 1 : 0;
    }
    g_lastRelPos = rel;
    g_lastRelRot = rot;
    g_haveRel = true;
    if (age > 1000 && (g_stable >= 10 || age > 4000)) {
        g_relPos = rel;
        g_relRot = rot;
        g_rigid = attached;
        Log("Weapon: %ls held rigidly in the controller, rest pose %.1f %.1f %.1f (%s)", uobject::ClassName(attached),
            rel.x, rel.y, rel.z, g_stable >= 10 ? "settled" : "timeout");
    }
}

int __fastcall HookedUpdateAttachment(void* inst, void* edx, void* owner, void* attached, int a, int b) {
    int r = g_updateAttachment(inst, edx, owner, attached, a, b);
    if (attached && attached == g_held && g_config.rigidWeapon) {
        g_updates++;
        if (attached == g_rigid) {
            if(g_holding)Place(attached);
        }
        else if (owner && owner == g_hands) Observe(owner, attached);
    }
    return r;
}

}  // namespace

// During equipping, Pawn.ActiveHoldable can still be the old model or null.
// Hands.CurrentHoldable is the actor actually attached and being rendered.
// Use that actor for capture/anchoring without changing the game's selection.
void* VisualHoldable(void* active,void* hands){
    if(!g_config.weaponGeometry||!hands||IsBadReadPtr(hands,0x30))return active;
    static void* cls=nullptr;static int offset=-1;
    void* currentClass=*(void**)((char*)hands+0x2c);
    if(currentClass!=cls){cls=currentClass;offset=uobject::PropertyOffset(hands,L"CurrentHoldable");}
    if(offset<=0||offset>0x10000||IsBadReadPtr((char*)hands+offset,4))return active;
    void* current=*(void**)((char*)hands+offset);
    if(!current||IsBadReadPtr(current,0x30))return active;
    if(current==g_held)return current;
    const wchar_t* meshName=nullptr;
    const wchar_t* properties[]={L"StaticMesh",L"Mesh"};
    for(const wchar_t* key:properties){
        const int at=uobject::PropertyOffset(current,key);
        if(at<=0||at>0x10000||IsBadReadPtr((char*)current+at,4))continue;
        void* mesh=*(void**)((char*)current+at);
        if(mesh){meshName=uobject::ObjectName(mesh);if(weaponassets::Find(meshName)!=weaponassets::Model::None)break;}
    }
    if(weaponassets::Find(meshName)==weaponassets::Model::None)return active;
    if(current!=active)Log("Weapon handoff: capture rendered Hands.CurrentHoldable %ls while ActiveHoldable=%ls",meshName,active?uobject::ClassName(active):L"none");
    return current;
}

void HoldRigid(void* held, void* hands, const vr::Vec3& handsLoc, const vr::Mat3& handsRot, vr::Vec3 gripPosition,const vr::Mat3* modelRotation) {
    held=VisualHoldable(held,hands);
    g_hands = hands;
    if (held != g_held) {
        if(g_held&&g_scaleOffset>0&&!IsBadReadPtr(g_held,0x30)&&*(void**)((char*)g_held+0x2c)==g_scaleClass&&
           *(int*)((char*)g_held+0x24)==g_scaleName&&!IsBadWritePtr((char*)g_held+g_scaleOffset,4))
            *(float*)((char*)g_held+g_scaleOffset)=g_nativeScale;
        weaponlayer::InvalidateModel();
        g_held = held;
        g_rigid = nullptr;
        g_haveRel = false;
        g_stable = 0;
        g_heldSince = GetTickCount();
        g_updates = 0;
        g_wrenchGeometry = false;
        g_pistolGeometry = false;
        g_model=weaponassets::Model::None;
        g_scaleOffset=-1;g_nativeScale=1;
        if (held) {
            if(!g_config.noWeaponAnimations)Log("Weapon: learning how %ls lies in the hand (grip anchor %d)", uobject::ClassName(held),g_config.weaponGripAnchor);
            const int meshOff=uobject::PropertyOffset(held,L"StaticMesh");
            void* mesh=meshOff>0?*(void**)((char*)held+meshOff):nullptr;
            if(!mesh){const int skeletalOff=uobject::PropertyOffset(held,L"Mesh");if(skeletalOff>0)mesh=*(void**)((char*)held+skeletalOff);}
            const wchar_t* meshName=mesh?uobject::ObjectName(mesh):nullptr;
            g_wrenchGeometry=meshName&&!wcscmp(meshName,L"WP_WrenchMesh");
            g_pistolGeometry=meshName&&!wcscmp(meshName,L"WP_PistolMesh");
            g_model=weaponassets::Find(meshName);
            const int scaleOff=uobject::PropertyOffset(held,L"DrawScale");
            float scale=scaleOff>0?*(float*)((char*)held+scaleOff):1.0f;
            if(scaleOff>0&&isfinite(scale)&&scale>0){g_scaleOffset=scaleOff;g_nativeScale=scale;g_scaleClass=*(void**)((char*)held+0x2c);g_scaleName=*(int*)((char*)held+0x24);}
            Log("Weapon: model %ls, original DrawScale %.3f retained, verified geometry family %u",meshName?meshName:L"unknown",scale,unsigned(g_model));
            if(g_config.noWeaponAnimations){
                g_rigid=held;g_relPos={};g_relRot=vr::FromRotator({});
                Log("Weapon pose: immediate controller anchor, fixed mesh axes, no animation-derived calibration");
            }
        }
    }
    static DWORD lastLog = 0;
    if (held && held != g_rigid && GetTickCount() - lastLog > 5000) {
        lastLog = GetTickCount();
        Log("Weapon: %ls not locked yet: %u engine updates, %d stable frames", uobject::ClassName(held), g_updates, g_stable);
    }
    g_holding = held && held == g_rigid && g_config.rigidWeapon;
    if (!g_holding){melee::Reset();return;}
    const vr::Mat3 rotation=g_config.noWeaponAnimations?(modelRotation?*modelRotation:handsRot):vr::Mul(handsRot,g_relRot);
    const weaponpose::Pose pose=weaponpose::AtGrip(gripPosition,rotation,g_config.weaponGripOffset);
    g_wantLoc = g_config.weaponGripAnchor ? pose.position : handsLoc + vr::Mul(handsRot, g_relPos);
    if(g_wrenchGeometry)g_wantLoc=g_wantLoc+vr::Column(rotation,0)*(g_config.wrenchForward*g_config.unitsPerMetre);
    g_wantRot = vr::ToRotator(rotation);
    Place(held);
    NeutralModelAnimation(held);
    melee::Observe(g_wrenchGeometry?held:nullptr);
}

void ReleaseRigid() { g_holding = false;melee::Reset(); }
void SetCinematic(bool active){
    static void* hidden=nullptr;static void* cls=nullptr;static int name=0,offset=0;static unsigned mask=0;static bool wasHidden=false;
    if(hidden&&(!active||hidden!=g_held)){
        if(!IsBadReadPtr(hidden,0x30)&&*(void**)((char*)hidden+0x2c)==cls&&*(int*)((char*)hidden+0x24)==name&&!IsBadReadPtr((char*)hidden+offset,4)){
            unsigned* flags=(unsigned*)((char*)hidden+offset);
            if(!wasHidden)*flags&=~mask;
        }
        hidden=nullptr;
    }
    if(!active||!g_held||hidden||IsBadReadPtr(g_held,0x30))return;
    int off=0;unsigned bit=0;
    if(!uobject::BoolProperty(g_held,L"bHidden",off,bit)||off<=0||!bit||IsBadReadPtr((char*)g_held+off,4))return;
    hidden=g_held;cls=*(void**)((char*)hidden+0x2c);name=*(int*)((char*)hidden+0x24);offset=off;mask=bit;
    unsigned* flags=(unsigned*)((char*)hidden+offset);wasHidden=(*flags&mask)!=0;*flags|=mask;
    Log("Cinematic: held actor hidden, previous visibility retained");
}
bool WrenchGeometrySelected(){return g_held&&g_wrenchGeometry;}
bool PistolGeometrySelected(){return g_held&&g_pistolGeometry;}
bool GeometryMesh(unsigned vertices,unsigned primitives){return g_held&&weaponassets::Matches(g_model,vertices,primitives);}

bool Install() {
    if (memcmp((void*)kUpdateAttachment, kUpdateAttachmentBytes, sizeof(kUpdateAttachmentBytes)) == 0 &&
        MH_CreateHook((void*)kUpdateAttachment, (void*)HookedUpdateAttachment, (void**)&g_updateAttachment) == MH_OK &&
        MH_EnableHook((void*)kUpdateAttachment) == MH_OK)
        Log("Weapon: bone attachment update hooked (held item follows the controller)");
    else
        Log("Weapon: bone attachment update not found - held item follows the hands' animation");
    // sub esp,0x?? - проверяем, что функция на месте (первые байты конкретной сборки)
    const unsigned char* p = (const unsigned char*)kDrawActor;
    if (IsBadReadPtr(p, 4)) return false;
    if (MH_CreateHook((void*)kDrawActor, (void*)HookedDrawActor, (void**)&g_original) != MH_OK ||
        MH_EnableHook((void*)kDrawActor) != MH_OK) {
        Log("Weapon: DrawActor hook failed");
        return false;
    }
    Log("Weapon: UCanvas::DrawActor hooked (bytes %02x %02x %02x %02x)", p[0], p[1], p[2], p[3]);
    return true;
}

void SetAbilityMode(bool active){InterlockedExchange(&g_abilityMode,active?1:0);}
bool MeleeEquipped() { return !g_abilityMode&&g_melee && GetTickCount() - g_meleeSeen < 500; }
bool RangedEquipped(){return g_held&&GetTickCount()-g_meleeSeen<500&&g_model>=weaponassets::Model::Pistol&&g_model<=weaponassets::Model::GrenadeLauncher;}

// Оружие в руке по Pawn.ActiveHoldable: UCanvas::DrawActor в игре не вызывается, по нему ключ
// не распознавался и удар взмахом не срабатывал
void NoteHoldable(void* held) {
    static void* last = nullptr;
    if (held != last) {
        last = held;
        const wchar_t* cls = held ? uobject::ClassName(held) : nullptr;
        bool melee = cls && (wcsstr(cls, L"Wrench") || wcsstr(cls, L"wrench") || wcsstr(cls, L"Melee"));
        InterlockedExchange(&g_melee, melee ? 1 : 0);
        Log("Weapon: in hand %ls%s", cls ? cls : L"nothing", melee ? " (melee: swing to hit)" : "");
    }
    if (held) g_meleeSeen = GetTickCount();
}

bool TakeDrawn() { return InterlockedExchange(&g_drawn, 0) != 0; }

}  // namespace weapon
