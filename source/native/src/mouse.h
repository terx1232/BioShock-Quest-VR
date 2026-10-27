#pragma once
#include <windows.h>

namespace mouse {
// Перехватывает GetCursorPos и мышь DirectInput8. Вызывать до того, как игра создаст мышь.
bool Install();
void SetWindow(HWND window);
// Окно игры.
HWND GameWindow();
// Во сколько раз игра увеличивает смещения мыши (её чувствительность).
void SetScale(float scale);
// Размер кадра игры (координаты луча).
void SetScreenSize(int w, int h);
// active: меню - игра видит курсор в (x, y) окна и кнопку button; иначе обычная мышь.
void Point(bool active, int x, int y, bool button);
// Игровой режим: кнопки мыши (левая - огонь, правая - смена) и щелчки колеса (+1 вверх, -1 вниз)
// от контроллеров; движение мыши игре не отдаётся (смотрит голова). active=false - отпустить.
void GameInput(bool active, bool left, bool right, int wheel = 0);
// Remastered melee pitch servo: feed the engine's own input pitch while the
// visible VR camera continues to use the exact tracked head pose.
void MeleePitch(float headMinusEngineDegrees,bool valid);
// Esc (кнопка B в меню).
void Escape(bool down);
// Заново упереть курсор игры в угол (если разошёлся с лучом).
void Resync();
}
