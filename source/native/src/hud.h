#pragma once
#include "hudlayout.h"

struct IDirect3DDevice9;
struct IDirect3DTexture9;

namespace hud {
// Перехват отрисовки HUD игры; false - Bioshock.exe не той версии.
bool Install();
// Disable only the game's reticle, through its reflected BoolProperty.
void UpdateReticle(void* controller,void* pawn);
// Устройство игры (перехват SetRenderTarget).
void SetDevice(IDirect3DDevice9* device);
// true - игровой режим VR: HUD уходит в свою текстуру (запястье + перед глазами).
void SetActive(bool active);
// Оружие игры рисуется в той же фазе, но в сам кадр.
void Suspend();
void Resume();
// Есть HUD этого кадра.
bool Ready();
IDirect3DTexture9* Texture();
// Углы HUD для запястья (доли кадра x0 y0 x1 y1): здоровье/EVE и патроны.
const float* LeftArea();
const float* RightArea();
hudlayout::Source Source();
hudlayout::Layout Layout();
// Середина HUD (подсказки, прицел, субтитры) - обратно в кадр.
// views = 2 - стереопара бок о бок, shiftPx - сдвиг для глубины (доля ширины картинки).
// squareSource - окно шире картинки (моно 2K): берётся квадратная середина HUD.
void CompositeFront(IDirect3DDevice9* device, int views = 1, float shiftPx = 0, bool squareSource = false);
// Начало Present: интерфейс этого кадра готов в текстуре.
void BeginPresent();
// Конец кадра (отладочная статистика).
void FrameEnd();
// Отладка: записать в лог отрисовки следующего кадра.
void DumpFrame();
// Перед Reset устройства.
void Release();
}
