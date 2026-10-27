#pragma once
#include <windows.h>

struct IDirect3DDevice9;

namespace theater {
// Окно игры, в координатах которого двигается мышь.
void SetWindow(HWND window);
// Поле зрения кинотеатра по горизонтали (не меньше minimum).
float Fov(float minimum);
// Рисует кинотеатр поверх кадра меню (вызывать перед Present). false - нет данных шлема.
// flat - меню показывает сам шлем плоским экраном: не рисуем и мышь не ведём, только кнопки.
bool Render(IDirect3DDevice9* device, float fovDeg, bool flat = false,bool loading=false,unsigned panelWidth=0,unsigned panelHeight=0);
// Вышли из меню: в следующий раз экран снова встанет перед игроком.
void Leave();
// Мягкие края кадра игры (в игровом режиме).
void Vignette(IDirect3DDevice9* device);
// Модели контроллеров Quest 2 в руках поверх кадра игры.
void GameHands(IDirect3DDevice9* device, float fovDeg, bool rightHasWeapon);
// Часть кадра, в которую движок рисует меню (верхняя полоса 16:9).
RECT MenuBand(UINT width, UINT height);
// true, пока мод сам рисует (его SetViewport не трогаем).
extern volatile bool g_modDraw;
// Перед Reset устройства.
void Release();
}
