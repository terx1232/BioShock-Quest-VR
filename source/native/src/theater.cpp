// "Кинотеатр" для меню, паузы, загрузок и роликов, как в VR-портах вроде RE4 VR: вокруг пустота,
// перед игроком висит экран с кадром игры, голова свободно смотрит по сторонам, руки видны
// квадратиками, от руки идёт луч, а там, где он попадает в экран, - курсор (им же двигается мышь
// Windows для игры). Курок - клик, B - Esc.
//
// Всё рисуется здесь, в Present, фиксированным конвейером D3D9 поверх кадра игры: кадр меню
// копируется в текстуру, затем на его месте рисуется вид из головы. Состояние устройства игры
// сохраняется и возвращается блоком состояния.
#include "theater.h"

#include <windows.h>
#include <d3d9.h>

#include "d3dguard.h"
#include <math.h>

#include "camera.h"
#include "controllermodel.h"
#include "devharness.h"
#include "hud.h"
#include "stereo.h"
#include "config.h"
#include "log.h"
#include "mouse.h"
#include "vrmath.h"
#include "vrframe.h"
#include "xrapi.h"

namespace theater {

volatile bool g_modDraw = false;

RECT MenuBand(UINT w, UINT h) {
    // Меню - весь кадр: Flash-интерфейс игры раскладывается по всей ширине окна (в широком окне
    // стерео он растянут), поэтому экран показывает кадр целиком, сжатым до 16:9
    RECT r = {0, 0, (LONG)w, (LONG)h};
    if (stereo::Enabled()) return r;
    // меню движок рисует в верхнюю полосу кадра высотой "экрана движка"
    unsigned vh = camera::ViewportHeight();
    if (vh > 16 && vh < h) r.bottom = (LONG)vh;
    return r;
}

namespace {

using vr::Mat3;
using vr::Quat;
using vr::Vec3;

struct Vertex {
    float x, y, z;
    D3DCOLOR color;
    float u, v;
};
const DWORD kFvf = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1;

IDirect3DTexture9* g_screen = nullptr;
IDirect3DSurface9* g_screenSurface = nullptr;
bool g_screenMips = false;
IDirect3DStateBlock9* g_state = nullptr;
IDirect3DSurface9* g_depth = nullptr;
IDirect3DVertexBuffer9* g_handVB[4] = {};  // модели в видеопамяти: контроллеры (лев, прав), предметы (ключ, бур)
unsigned g_handTris[4] = {};
UINT g_width = 0, g_height = 0;
HWND g_window = nullptr;

bool g_active = false;
bool g_justPlaced = false;
bool g_placedOnce = false;
Vec3 g_anchor;      // центр экрана в комнате (метры, оси OpenXR)
Vec3 g_normal;      // смотрит на игрока
Vec3 g_right, g_up; // оси экрана
float g_halfW = 0.9f, g_halfH = 0.9f;
bool g_triggerWas = false, g_bWas = false, g_leftTriggerWas = false;
int g_hand = 1;     // какой рукой указываем (последняя, нажавшая курок)
int g_cursorX = 0, g_cursorY = 0;

Vec3 Cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Norm(Vec3 a) {
    float l = sqrtf(Dot(a, a));
    return l > 1e-6f ? a * (1.0f / l) : Vec3{0, 0, -1};
}

void ToD3D(const float m[16], D3DMATRIX& out) { memcpy(&out, m, sizeof(float) * 16); }

// Вид из головы: строки = оси камеры (D3D, вектор-строка), правая система OpenXR
void ViewMatrix(const Mat3& r, Vec3 p, D3DMATRIX& out) {
    Vec3 x = vr::Column(r, 0), y = vr::Column(r, 1), z = vr::Column(r, 2);
    float m[16] = {x.x, y.x, z.x, 0, x.y, y.y, z.y, 0, x.z, y.z, z.z, 0,
                   -Dot(x, p), -Dot(y, p), -Dot(z, p), 1};
    ToD3D(m, out);
}

float g_viewAspect = 0;  // соотношение сторон текущего вида (0 = весь кадр)

void ProjectionMatrix(float fovDeg, D3DMATRIX& out) {
    // fovDeg - по горизонтали; по вертикали - из соотношения сторон вида
    float aspect = g_viewAspect > 0 ? g_viewAspect : (g_height ? (float)g_width / g_height : 1.0f);
    float xs = 1.0f / tanf(fovDeg * vr::kPi / 360.0f), ys = xs * aspect;
    const float zn = 0.05f, zf = 100.0f;
    float m[16] = {xs, 0, 0, 0, 0, ys, 0, 0, 0, 0, zf / (zn - zf), -1, 0, 0, zn * zf / (zn - zf), 0};
    ToD3D(m, out);
}

// Виды для отрисовки: 1 (весь кадр) или 2 (стереопара бок о бок, у каждого глаза своя половина).
int Views() { return stereo::Ready() || (g_config.menuStereo && !vrframe::Current().inGame) ? 2 : 1; }

// Вид глаза: область вывода и положение глаза (вбок от головы на половину межзрачкового)
Vec3 SetView(IDirect3DDevice9* d, int view, int views, const Mat3& head, Vec3 headPos, float ipd, float fovDeg) {
    UINT w = g_width / views;
    D3DVIEWPORT9 vp = {view * w, 0, w, g_height, 0, 1};
    d->SetViewport(&vp);
    // стереопара: WinlatorXR показывает каждую половину кадра с квадратным полем зрения
    g_viewAspect = views == 2 || stereo::SplitActive() ? 1.0f : (g_height ? (float)w / g_height : 1.0f);
    Vec3 eye = headPos;
    if (views == 2) eye = eye + vr::Column(head, 0) * ((view == 0 ? -0.5f : 0.5f) * ipd);
    D3DMATRIX view_, proj;
    ViewMatrix(head, eye, view_);
    ProjectionMatrix(fovDeg, proj);
    d->SetTransform(D3DTS_VIEW, &view_);
    d->SetTransform(D3DTS_PROJECTION, &proj);
    return eye;
}

void Quad(IDirect3DDevice9* d, Vec3 c, Vec3 ax, Vec3 ay, D3DCOLOR col, bool textured) {
    Vertex v[4] = {
        {c.x - ax.x + ay.x, c.y - ax.y + ay.y, c.z - ax.z + ay.z, col, 0, 0},
        {c.x + ax.x + ay.x, c.y + ax.y + ay.y, c.z + ax.z + ay.z, col, 1, 0},
        {c.x - ax.x - ay.x, c.y - ax.y - ay.y, c.z - ax.z - ay.z, col, 0, 1},
        {c.x + ax.x - ay.x, c.y + ax.y - ay.y, c.z + ax.z - ay.z, col, 1, 1},
    };
    d->SetTexture(0, textured ? g_screen : nullptr);
    d->SetTextureStageState(0, D3DTSS_COLOROP, textured ? D3DTOP_SELECTARG1 : D3DTOP_SELECTARG2);
    d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(Vertex));
}


// Модель контроллера Quest 2 в позе рукоятки
void Hand(IDirect3DDevice9* d, const xrapi::Hand& h, bool left, bool active, bool prop = false) {
    Mat3 r = vr::FromQuat(h.grip);
    Vec3 x = vr::Column(r, 0), y = vr::Column(r, 1), z = vr::Column(r, 2);
    D3DMATRIX world = {{{x.x, x.y, x.z, 0, y.x, y.y, y.z, 0, z.x, z.y, z.z, 0,
                         h.position.x, h.position.y, h.position.z, 1}}};
    D3DMATRIX ident = {{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}}};
    int idx = (left ? 0 : 1) + (prop ? 2 : 0);
    if (!g_handVB[idx]) {
        unsigned count = 0;
        const controllermodel::Vertex* v = prop ? controllermodel::PropModel(left, count) : controllermodel::Get(left, count);
        UINT bytes = count * 3 * sizeof(controllermodel::Vertex);
        void* dst = nullptr;
        if (SUCCEEDED(d->CreateVertexBuffer(bytes, D3DUSAGE_WRITEONLY, D3DFVF_XYZ | D3DFVF_DIFFUSE, D3DPOOL_DEFAULT,
                                            &g_handVB[idx], nullptr)) &&
            SUCCEEDED(g_handVB[idx]->Lock(0, bytes, &dst, 0))) {
            memcpy(dst, v, bytes);
            g_handVB[idx]->Unlock();
            g_handTris[idx] = count;
        }
        if (!g_handTris[idx]) return;
    }
    d->SetTransform(D3DTS_WORLD, &world);
    d->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    d->SetTexture(0, nullptr);
    // Цвет строго из вершин: состояния игры (материал, туман, спекуляр) не должны его съедать
    d->SetRenderState(D3DRS_LIGHTING, FALSE);
    d->SetRenderState(D3DRS_COLORVERTEX, TRUE);
    d->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1);
    d->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    d->SetRenderState(D3DRS_FOGENABLE, FALSE);
    d->SetRenderState(D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    d->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    d->SetRenderState(D3DRS_COLORWRITEENABLE, 0x7);
    d->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    d->SetVertexShader(nullptr);
    d->SetPixelShader(nullptr);
    d->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    d->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
    d->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    d->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
    d->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    d->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    d->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);  // у игры может стоять обратная проверка глубины
    d->SetStreamSource(0, g_handVB[idx], 0, sizeof(controllermodel::Vertex));
    d->DrawPrimitive(D3DPT_TRIANGLELIST, 0, g_handTris[idx]);
    d->SetRenderState(D3DRS_ZENABLE, FALSE);
    d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    d->SetTransform(D3DTS_WORLD, &ident);
    d->SetFVF(kFvf);
    (void)active;
}

void Line(IDirect3DDevice9* d, Vec3 a, Vec3 b, D3DCOLOR col) {
    Vertex v[2] = {{a.x, a.y, a.z, col, 0, 0}, {b.x, b.y, b.z, col, 0, 0}};
    d->SetTexture(0, nullptr);
    d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
    d->DrawPrimitiveUP(D3DPT_LINELIST, 1, v, sizeof(Vertex));
}

// A triangle ribbon keeps the pointer visible when a one-pixel line falls
// between samples. Its width is at least three pixels in either eye.
void Beam(IDirect3DDevice9* d, Vec3 a, Vec3 b, Vec3 eye, Vec3 forward, float fovDeg, D3DCOLOR col) {
    Vec3 side = Cross(b-a,eye-(a+b)*.5f);
    if (Dot(side,side)<1e-10f) side = Cross(forward,Vec3{0,1,0});
    side=Norm(side);
    D3DVIEWPORT9 vp={}; d->GetViewport(&vp);
    float scale=3.0f*tanf(fovDeg*vr::kPi/360.0f)/(vp.Width?vp.Width:1536);
    float za=Dot(a-eye,forward), zb=Dot(b-eye,forward);
    Vec3 sa=side*fmaxf(.001f,za*scale), sb=side*fmaxf(.001f,zb*scale);
    Vec3 p[4]={a-sa,a+sa,b-sb,b+sb};
    Vertex v[4];
    for(int i=0;i<4;++i) v[i]={p[i].x,p[i].y,p[i].z,col,0,0};
    d->SetTexture(0,nullptr);
    d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG2);
    d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(Vertex));
}

// Луч из руки (вперёд = -Z контроллера) в плоскость экрана: true и координаты 0..1
bool Hit(const xrapi::Hand& h, float& u, float& v, Vec3& point) {
    Vec3 dir = Norm(vr::Mul(vr::FromQuat(h.orientation), Vec3{0, 0, -1}));
    float denom = Dot(dir, g_normal);
    if (denom > -1e-4f) return false;
    float t = Dot(g_anchor - h.position, g_normal) / denom;
    if (t < 0) return false;
    point = h.position + dir * t;
    Vec3 rel = point - g_anchor;
    float x = Dot(rel, g_right) / g_halfW, y = Dot(rel, g_up) / g_halfH;
    u = (x + 1) * 0.5f;
    v = (1 - y) * 0.5f;
    return u >= 0 && u <= 1 && v >= 0 && v <= 1;
}

void SendMouse(DWORD flags, int wheel = 0) {
    INPUT in = {};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = flags;
    in.mi.mouseData = wheel;
    SendInput(1, &in, sizeof(in));
}

void SendKey(WORD vk, bool down) {
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(in));
}

void PlaceScreen(const xrapi::State& s) {
    Mat3 head = vr::FromQuat(s.hmdOrientation);
    Vec3 fwd = vr::Column(head, 2) * -1.0f;
    fwd.y = 0;
    fwd = Norm(fwd);
    if (fwd.x == 0 && fwd.z == -1 && s.hmdOrientation.w == 0) fwd = {0, 0, -1};
    const float distance = g_config.screenDistance > 1.0f ? g_config.screenDistance : 2.2f;
    g_anchor = s.hmdPosition + fwd * distance;
    g_normal = fwd * -1.0f;
    g_up = {0, 1, 0};
    g_right = Norm(Cross(fwd, g_up));
    RECT band = MenuBand(g_width, g_height);
    float aspect = g_config.menuAspect > 0.2f ? g_config.menuAspect
                 : stereo::Enabled() || stereo::SplitActive() ? 16.0f / 9.0f  // меню в окне 16:9 - экран всегда 16:9
                 : (band.bottom > band.top ? (float)(band.right - band.left) / (band.bottom - band.top) : 1.0f);
    g_halfW = distance * tanf(g_config.screenWidthDeg * vr::kPi / 360.0f);
    g_halfH = g_halfW / aspect;
}

bool EnsureResources(IDirect3DDevice9* d, IDirect3DSurface9* back) {
    D3DSURFACE_DESC desc;
    back->GetDesc(&desc);
    if (g_screen && desc.Width == g_width && desc.Height == g_height) return true;
    Release();
    g_width = desc.Width;
    g_height = desc.Height;
    // Мип-уровни: экран меню на ~500 точках глаза показывает кадр в 2048 точек - без заранее
    // уменьшенных копий мелкие детали и текст рябят и мерцают при каждом движении головы
    g_screenMips = g_config.screenMipmaps && SUCCEEDED(d->CreateTexture(g_width, g_height, 0, D3DUSAGE_RENDERTARGET | D3DUSAGE_AUTOGENMIPMAP,
                                              desc.Format, D3DPOOL_DEFAULT, &g_screen, nullptr));
    if (!g_screenMips &&
        FAILED(d->CreateTexture(g_width, g_height, 1, D3DUSAGE_RENDERTARGET, desc.Format, D3DPOOL_DEFAULT,
                                &g_screen, nullptr))) {
        Log("Theater: cannot create %ux%u screen texture", g_width, g_height);
        return false;
    }
    g_screen->GetSurfaceLevel(0, &g_screenSurface);
    // The screen, ray and pointer use ZENABLE=FALSE. A full-resolution depth
    // attachment and its clear only consume Quest bandwidth here.
    d->CreateStateBlock(D3DSBT_ALL, &g_state);
    Log("Theater: screen texture %ux%u%s", g_width, g_height, g_screenMips ? " with mipmaps" : "");
    mouse::SetScreenSize(g_width, g_height);
    return true;
}

}  // namespace

void SetWindow(HWND window) { g_window = window; }

// Поле зрения кинотеатра по горизонтали: экран целиком в кадре с небольшим запасом
float Fov(float minimum) {
    // у стереопары каждый глаз квадратный
    float aspect = Views() == 2 || stereo::SplitActive() ? 1.0f : g_height ? (float)g_width / g_height : 1.0f;
    float halfW = g_halfW * 1.12f, halfH = g_halfH * 1.12f;
    const float distance = g_config.screenDistance > 1.0f ? g_config.screenDistance : 2.2f;
    float needH = 2 * atanf(halfW / distance) * 180 / vr::kPi;
    float needV = 2 * atanf(halfH / distance) * 180 / vr::kPi;
    float fromV = 2 * atanf(tanf(needV * vr::kPi / 360) * aspect) * 180 / vr::kPi;
    float f = needH > fromV ? needH : fromV;
    return f > minimum ? f : minimum;
}

// Панель на левом запястье: здоровье/EVE (левый верхний угол HUD) над патронами (правый нижний),
// всегда повёрнута к глазам. Вызывать с уже настроенными видом и проекцией.
void WristPanel(IDirect3DDevice9* d, const xrapi::Hand& h, Vec3 eye) {
    IDirect3DTexture9* tex = hud::Texture();
    if (!tex) return;
    Mat3 g = vr::FromQuat(h.grip);
    // над тыльной стороной запястья: чуть выше и ближе к локтю, чем рукоятка
    Vec3 base = h.position + vr::Column(g, 1) * 0.045f + vr::Column(g, 2) * 0.11f;
    Vec3 n = Norm(eye - base);
    Vec3 right = Norm(Cross(Vec3{0, 1, 0}, n));
    Vec3 up = Cross(n, right);
    const float* a = hud::LeftArea();
    const float* b = hud::RightArea();
    D3DSURFACE_DESC desc;
    tex->GetLevelDesc(0, &desc);
    float W = (float)desc.Width, H = (float)desc.Height;
    const float width = 0.15f * g_config.hudScale;
    float ha = width * ((a[3] - a[1]) * H) / ((a[2] - a[0]) * W);
    float hb = width * ((b[3] - b[1]) * H) / ((b[2] - b[0]) * W);
    struct TV { float x, y, z; D3DCOLOR c; float u, v; };
    auto quad = [&](Vec3 c, float hw, float hh, D3DCOLOR col, float u0, float v0, float u1, float v1) {
        Vec3 ax = right * hw, ay = up * hh;
        TV q[4] = {
            {c.x - ax.x + ay.x, c.y - ax.y + ay.y, c.z - ax.z + ay.z, col, u0, v0},
            {c.x + ax.x + ay.x, c.y + ax.y + ay.y, c.z + ax.z + ay.z, col, u1, v0},
            {c.x - ax.x - ay.x, c.y - ax.y - ay.y, c.z - ax.z - ay.z, col, u0, v1},
            {c.x + ax.x - ay.x, c.y + ax.y - ay.y, c.z + ax.z - ay.z, col, u1, v1},
        };
        d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(TV));
    };
    d->SetFVF(kFvf);
    d->SetRenderState(D3DRS_ZENABLE, FALSE);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
    d->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    // сам HUD: цвет в текстуре уже умножен на прозрачность
    d->SetTexture(0, tex);
    d->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    d->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    d->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    d->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    d->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    d->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    d->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_ONE);
    d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    quad(base + up * (hb + ha * 0.5f), width * 0.5f, ha * 0.5f, 0xFFFFFFFF, a[0], a[1], a[2], a[3]);
    quad(base + up * (hb * 0.5f), width * 0.5f, hb * 0.5f, 0xFFFFFFFF, b[0], b[1], b[2], b[3]);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    d->SetTexture(0, nullptr);
}

// Руки в игре: модели контроллеров Quest 2 в позах рук, вид из головы (как камера игры),
// поверх кадра игры.
void GameHands(IDirect3DDevice9* d, float fovDeg, bool rightHasWeapon) {
    TargetsGuard guard(d);  // вернуть игре её цель, буфер глубины и область вывода
    // Тот же снимок позы, что у камеры и пикселя синхронизации (иначе картинка двоится)
    xrapi::State s = vrframe::State();
    if (!s.valid) return;
    IDirect3DSurface9* back = nullptr;
    if (FAILED(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)) || !back) return;
    if (!EnsureResources(d, back)) {
        back->Release();
        return;
    }
    g_modDraw = true;
    g_state->Capture();
    d->SetRenderTarget(0, back);
    d->SetDepthStencilSurface(g_depth);
    D3DVIEWPORT9 vp = {0, 0, g_width, g_height, 0, 1};
    d->SetViewport(&vp);
    d->SetVertexShader(nullptr);
    d->SetPixelShader(nullptr);
    d->SetRenderState(D3DRS_LIGHTING, FALSE);
    d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    d->SetRenderState(D3DRS_FOGENABLE, FALSE);
    d->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    d->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    d->SetRenderState(D3DRS_COLORWRITEENABLE, 0x7);
    d->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    d->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    d->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
    d->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    d->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    Mat3 head = vr::FromQuat(s.hmdOrientation);
    float ipd = s.ipd > 0.04f && s.ipd < 0.09f ? s.ipd : 0.063f;
    int views = Views();
    d->BeginScene();
    for (int v = 0; v < views; v++) {
        Vec3 eye = SetView(d, v, views, head, s.hmdPosition, ipd, fovDeg);
        if (views == 1 && g_config.stereo) {
            // поочерёдные глаза (старый режим): глаз этого кадра, как у камеры игры
            eye = s.hmdPosition + vr::Column(head, 0) * ((vrframe::Current().eye == 0 ? -0.5f : 0.5f) * ipd);
            D3DMATRIX vm;
            ViewMatrix(head, eye, vm);
            d->SetTransform(D3DTS_VIEW, &vm);
        }
        if (g_depth) d->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
        if (g_config.gameHands) {
            Hand(d, s.left, true, false);
            if (!rightHasWeapon) Hand(d, s.right, false, false);
        }
        if (hud::Ready()&&!g_config.gameplayHud) {
            D3DMATRIX identity={{{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}}};d->SetTransform(D3DTS_WORLD,&identity);
            WristPanel(d, s.left, eye);
        }
        if (g_config.plasmidRay) {
            // прицел плазмида: тонкий луч из левой руки
            Vec3 dir = Norm(vr::Mul(vr::FromQuat(s.left.orientation), Vec3{0, 0, -1}));
            D3DMATRIX ident = {{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}}};
            d->SetTransform(D3DTS_WORLD, &ident);
            d->SetFVF(kFvf);
            d->SetRenderState(D3DRS_ZENABLE, FALSE);
            d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
            d->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
            Line(d, s.left.position + dir * 0.05f, s.left.position + dir * 4.0f, D3DCOLOR_XRGB(150, 210, 255));
        }
    }
    d->EndScene();
    g_viewAspect = 0;
    g_state->Apply();
    g_modDraw = false;
    back->Release();
}

// Мягкие края кадра игры: к краям картинка уходит в чёрное, а вокруг кадра в шлеме тоже чёрное,
// так что граница кадра не видна "квадратом".
void Vignette(IDirect3DDevice9* d) {
    TargetsGuard guard(d);  // вернуть игре её цель, буфер глубины и область вывода
    IDirect3DSurface9* back = nullptr;
    if (FAILED(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)) || !back) return;
    if (!EnsureResources(d, back)) {
        back->Release();
        return;
    }
    struct V { float x, y, z, rhw; D3DCOLOR c; };
    float w = (float)g_width, h = (float)g_height;
    float bw = w * 0.06f, bh = h * 0.08f;
    const D3DCOLOR edge = 0xFF000000, in = 0x00000000;
    g_modDraw = true;
    g_state->Capture();
    d->SetRenderTarget(0, back);
    d->SetDepthStencilSurface(nullptr);
    D3DVIEWPORT9 vp = {0, 0, g_width, g_height, 0, 1};
    d->SetViewport(&vp);
    d->SetVertexShader(nullptr);
    d->SetPixelShader(nullptr);
    d->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    d->SetTexture(0, nullptr);
    d->SetRenderState(D3DRS_ZENABLE, FALSE);
    d->SetRenderState(D3DRS_LIGHTING, FALSE);
    d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    d->SetRenderState(D3DRS_FOGENABLE, FALSE);
    d->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    d->SetRenderState(D3DRS_COLORWRITEENABLE, 0x7);
    d->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
    d->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    d->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
    d->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    d->BeginScene();
    V top[4] = {{0, 0, 0, 1, edge}, {w, 0, 0, 1, edge}, {0, bh, 0, 1, in}, {w, bh, 0, 1, in}};
    V bottom[4] = {{0, h - bh, 0, 1, in}, {w, h - bh, 0, 1, in}, {0, h, 0, 1, edge}, {w, h, 0, 1, edge}};
    V left[4] = {{0, 0, 0, 1, edge}, {bw, 0, 0, 1, in}, {0, h, 0, 1, edge}, {bw, h, 0, 1, in}};
    V right[4] = {{w - bw, 0, 0, 1, in}, {w, 0, 0, 1, edge}, {w - bw, h, 0, 1, in}, {w, h, 0, 1, edge}};
    d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, top, sizeof(V));
    d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, bottom, sizeof(V));
    d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, left, sizeof(V));
    d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, right, sizeof(V));
    d->EndScene();
    g_state->Apply();
    g_modDraw = false;
    back->Release();
}

void Release() {
    if (g_screenSurface) g_screenSurface->Release();
    if (g_screen) g_screen->Release();
    if (g_state) g_state->Release();
    if (g_depth) g_depth->Release();
    g_depth = nullptr;
    for (int i = 0; i < 4; i++) {
        if (g_handVB[i]) g_handVB[i]->Release();
        g_handVB[i] = nullptr;
        g_handTris[i] = 0;
    }
    g_screenSurface = nullptr;
    g_screen = nullptr;
    g_state = nullptr;
    g_width = g_height = 0;
}

void Leave() {
    g_active = false;
    mouse::Point(false, g_cursorX, g_cursorY, false);
}

// B или левое меню - Esc (назад / закрыть паузу); меню - только после отпускания,
// иначе нажатие, открывшее паузу, сразу её закроет
void MenuButtons(const xrapi::State& s, bool rightTrig, bool leftTrig) {
    static bool menuArmed = false;
    if (!s.buttons[xrapi::L_MENU]) menuArmed = true;
    if (g_justPlaced) menuArmed = !s.buttons[xrapi::L_MENU];
    g_justPlaced = false;
    bool b = s.buttons[xrapi::R_B] || (menuArmed && s.buttons[xrapi::L_MENU]);
    mouse::Escape(b);
    g_triggerWas = rightTrig;
    g_leftTriggerWas = leftTrig;
    g_bWas = b;
}

bool Render(IDirect3DDevice9* d, float fovDeg, bool flat,bool loading,unsigned panelWidth,unsigned panelHeight) {
    // Тот же снимок позы, что у камеры и пикселя синхронизации (иначе картинка двоится)
    xrapi::State s = vrframe::State();
    if (!s.valid) return false;
    if (flat) {
        // Меню показывает сам шлем плоской панелью, указатель - лазер WinlatorXR: кадр не копируем
        // и ничего не рисуем (каждое копирование 2048x1024 с мип-уровнями отнимало видеочип у меню)
        if (!g_active) {
            g_active = true;
            g_justPlaced = true;
        }
        mouse::Point(false, g_cursorX, g_cursorY, false);
        MenuButtons(s, s.buttons[xrapi::R_TRIGGER], s.buttons[xrapi::L_TRIGGER]);
        return true;
    }
    TargetsGuard guard(d);  // вернуть игре её цель, буфер глубины и область вывода
    IDirect3DSurface9* back = nullptr;
    if (FAILED(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back)) || !back) return false;
    if (!EnsureResources(d, back)) {
        back->Release();
        return false;
    }
    if (!g_active) {
        // экран остаётся на месте между входами в меню; переставляем, только если он сильно
        // в стороне от взгляда (или ещё не ставился)
        Vec3 fwd = vr::Column(vr::FromQuat(s.hmdOrientation), 2) * -1.0f;
        Vec3 toScreen = Norm(g_anchor - s.hmdPosition);
        if (!g_placedOnce || Dot(Norm(fwd), toScreen) < 0.5f) PlaceScreen(s);
        g_placedOnce = true;
        g_active = true;
        g_justPlaced = true;
        Log("Theater: screen placed");
    }
    // Правый стик вниз/вверх - ближе/дальше, нажатие правого стика - экран перед собой
    if (s.buttons[xrapi::R_THUMBSTICK_PRESS]) PlaceScreen(s);

    bool nativeCinema=g_config.nativeCinema;
    // The native route always receives a raw menu. During preparation Android
    // submits a transition mask; never bake a second cinema into this image.
    RECT band = loading&&panelWidth&&panelHeight?RECT{0,0,LONG(panelWidth),LONG(panelHeight)}:MenuBand(g_width, g_height);
    if (!nativeCinema && FAILED(d->StretchRect(back, &band, g_screenSurface, nullptr, D3DTEXF_POINT))) {
        back->Release();
        return false;
    }
    if (!nativeCinema && g_screenMips) g_screen->GenerateMipSubLevels();

    float u = 0, v = 0;
    Vec3 point;
    bool onScreen = false;
    const xrapi::Hand* hands[2] = {&s.left, &s.right};
    bool leftTrig = s.buttons[xrapi::L_TRIGGER], rightTrig = s.buttons[xrapi::R_TRIGGER];
    if (rightTrig && !g_triggerWas) g_hand = 1;
    if (leftTrig && !g_leftTriggerWas) g_hand = 0;
    const xrapi::Hand& pointer = *hands[g_hand];
    onScreen = Hit(pointer, u, v, point);
    {
        float du, dv;
        if (devharness::Point(du, dv)) {
            u = du;
            v = dv;
            onScreen = true;
            point = g_anchor + g_right * ((u * 2 - 1) * g_halfW) + g_up * ((1 - v * 2) * g_halfH);
        }
    }
    // Сглаживание луча (фильтр "1 евро"): на экране в 7 м дрожь руки в полградуса - это 20 точек
    // меню, курсор прыгал. Медленно - сильно сглаживаем, быстро - почти без задержки.
    {
        static float fu = -1, fv = -1, du = 0, dv = 0;
        static bool had = false;
        static LARGE_INTEGER lastPointerTick = {};
        LARGE_INTEGER tick, frequency;
        QueryPerformanceCounter(&tick); QueryPerformanceFrequency(&frequency);
        float dt = lastPointerTick.QuadPart ? (float)(tick.QuadPart-lastPointerTick.QuadPart)/frequency.QuadPart : 1.0f/72.0f;
        lastPointerTick = tick;
        if (dt > .25f) { had = false; dt = 1.0f/72.0f; }
        if (dt < .001f) dt = .001f;
        if (onScreen && g_config.debugClickU < 0) {
            auto alpha = [dt](float cutoff) {
                float tau = 1.0f / (2 * vr::kPi * cutoff);
                return dt / (dt + tau);
            };
            if (!had) {
                fu = u, fv = v, du = dv = 0;
            } else {
                float ad = alpha(1.0f);
                du += ((u - fu) / dt - du) * ad;
                dv += ((v - fv) / dt - dv) * ad;
                float speed = sqrtf(du * du + dv * dv);  // ширин экрана в секунду
                float a = alpha(g_config.pointerMinCutoff + g_config.pointerBeta * speed);
                fu += (u - fu) * a;
                fv += (v - fv) * a;
            }
            u = fu, v = fv;
            point = g_anchor + g_right * ((u * 2 - 1) * g_halfW) + g_up * ((1 - v * 2) * g_halfH);
        }
        had = onScreen;
    }
    // Луч ушёл с экрана - при возвращении курсор игры заново синхронизируется
    static bool wasOn = true;
    if (onScreen && !wasOn) mouse::Resync();
    wasOn = onScreen;
    // Отладка: щелчки в заданную точку
    if (g_config.debugClickU >= 0) {
        u = g_config.debugClickU;
        v = g_config.debugClickV;
        onScreen = true;
        float t = GetTickCount() / 1000.0f;
        rightTrig = fmodf(t, g_config.debugClickPeriod) < 0.15f;
        g_hand = 1;
    }
    // XR init can use a larger desktop than the actual 1280x720 menu window.
    // Explicit client dimensions keep the native panel and ray mapping exact.
    xrapi::SendCinema(nativeCinema?(loading?3:2):1,g_anchor,g_right,g_up,g_normal,g_halfW,g_halfH,g_hand,
        static_cast<unsigned>(band.right-band.left),static_cast<unsigned>(band.bottom-band.top));
    if (g_config.theaterDraw && !nativeCinema) {
    g_modDraw = true;
    g_state->Capture();
    d->SetRenderTarget(0, back);
    for (int i = 1; i < 4; i++) d->SetRenderTarget(i, nullptr);
    d->SetDepthStencilSurface(g_depth);
    D3DVIEWPORT9 vp = {0, 0, g_width, g_height, 0, 1};
    d->SetViewport(&vp);
    d->SetVertexShader(nullptr);
    d->SetPixelShader(nullptr);
    d->SetFVF(kFvf);
    d->SetRenderState(D3DRS_ZENABLE, FALSE);
    d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    d->SetRenderState(D3DRS_LIGHTING, FALSE);
    d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    d->SetRenderState(D3DRS_FOGENABLE, FALSE);
    d->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    d->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    d->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    d->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    d->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    d->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    d->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    d->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
    d->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    d->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    d->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    d->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    // трилинейная + анизотропная фильтрация экрана: гладкий текст без ряби
    d->SetSamplerState(0, D3DSAMP_MINFILTER, g_screenMips ? D3DTEXF_ANISOTROPIC : D3DTEXF_LINEAR);
    d->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    d->SetSamplerState(0, D3DSAMP_MIPFILTER, g_screenMips ? D3DTEXF_LINEAR : D3DTEXF_NONE);
    d->SetSamplerState(0, D3DSAMP_MAXANISOTROPY, 4);
    d->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    d->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    d->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);

    D3DMATRIX world = {{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}}};
    d->SetTransform(D3DTS_WORLD, &world);
    d->Clear(0, nullptr, D3DCLEAR_TARGET | (g_depth ? D3DCLEAR_ZBUFFER : 0), D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
    Mat3 headR = vr::FromQuat(s.hmdOrientation);
    float ipd = s.ipd > 0.04f && s.ipd < 0.09f ? s.ipd : 0.063f;
    int views = Views();
    d->BeginScene();
    for (int view = 0; view < views; view++) {
    Vec3 eye=SetView(d, view, views, headR, s.hmdPosition, ipd, fovDeg);
    Quad(d, g_anchor, g_right * g_halfW, g_up * g_halfH, D3DCOLOR_XRGB(255, 255, 255), true);

    // Руки и луч
    // в меню рук нет (только луч и точка); по настройке - контроллеры или предметы из игры
    if (g_config.menuHands)
        for (int i = 0; i < 2; i++) Hand(d, *hands[i], i == 0, i == g_hand, g_config.menuProps);
    {
    Vec3 dir = Norm(vr::Mul(vr::FromQuat(pointer.orientation), Vec3{0, 0, -1}));
    Beam(d, pointer.position, onScreen ? point : pointer.position + dir * 3.0f,
         eye,vr::Column(headR,2)*-1.0f,fovDeg,
         onScreen ? D3DCOLOR_XRGB(255, 255, 255) : D3DCOLOR_XRGB(170, 170, 170));
    if (onScreen) {
        Vec3 c = point + g_normal * 0.005f;
        Quad(d, c, g_right * 0.016f, g_up * 0.016f, D3DCOLOR_XRGB(255, 255, 255), false);
    }
    }
    }
    d->EndScene();
    g_viewAspect = 0;
    g_state->Apply();
    g_modDraw = false;
    }
    back->Release();

    // Мышь игры следует за курсором (через перехваты GetCursorPos и DirectInput), курок - кнопка
    bool trig = g_hand == 1 ? rightTrig : leftTrig;
    if (onScreen) {
        RECT band = MenuBand(g_width, g_height);
        g_cursorX = band.left + (int)(u * (band.right - band.left));
        g_cursorY = band.top + (int)(v * (band.bottom - band.top));
    }
    // Native ray coordinates are converted to the game's software cursor.
    // Physical Wine pointer movement must not be applied a second time.
    mouse::Point(!loading&&g_config.mouseInject && (!g_config.nativeCinema || xrapi::NativeCinemaReady()), g_cursorX, g_cursorY, trig);
    MenuButtons(s, rightTrig, leftTrig);
    return true;
}

}  // namespace theater
