#pragma once
#include <d3d9.h>

namespace stereo {
// Стерео по буферу глубины включено в настройках (DepthStereo=1).
bool Enabled();
// Параметры устройства: автоматический буфер глубины заменяется читаемым (INTZ).
void AdjustParams(D3DPRESENT_PARAMETERS* pp);
// После CreateDevice/Reset: свой буфер глубины, текстуры и шейдеры.
void AfterCreate(IDirect3DDevice9* device, const D3DPRESENT_PARAMETERS* pp);
// Всё готово (INTZ поддерживается, шейдеры созданы).
bool Ready();
// PC adapter preserves the engine's ordinary depth surface in menus.
void SetActive(IDirect3DDevice9* device, bool active);
IDirect3DSurface9* MapDepth(IDirect3DSurface9* surface);
// Перед Clear устройства: глубина сцены копируется до очистки перед руками.
void OnClear(IDirect3DDevice9* device, DWORD flags, bool inGame);
// Конец кадра (после Present).
void EndFrame();
// Legacy depth-clear marker. On Quest this starts HUD, not weapon drawing.
bool FirstPersonPass();
// Моно с окном больше картинки движка (меню в 2K, игра растягивается на окно).
bool SplitActive();
// Игровой режим, моно: картинка движка растягивается на всё окно.
void Upscale(IDirect3DDevice9* device);
// Размер картинки движка в игровом режиме.
unsigned GameWidth();
unsigned GameHeight();
// Игровой режим: из картинки движка (левая половина) строит левый и правый глаз.
// cw x ch - текущий размер картинки движка (динамическое разрешение).
bool Compose(IDirect3DDevice9* device, float hfovDeg, float ipdUnits, unsigned cw, unsigned ch);
// Borrowed current-frame depth in the same reconstructed per-eye pixels.
IDirect3DTexture9* WorldDepth();
// Optional centre-eye UVs. Uses only this frame's world depth; no CPU readback.
unsigned DrawAimReticles(IDirect3DDevice9*,const float* weaponUV,const float* plasmidUV);
// Перед Reset устройства.
void Release();
}
