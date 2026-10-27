#pragma once

struct IDirect3DDevice9;

namespace devharness {
void Init(const char* gameDir);
// Раз в кадр перед Present: читает команды, сохраняет снимок кадра.
void BeforePresent(IDirect3DDevice9* device);
// Подмешивает имитацию кнопок и стиков в состояние контроллеров.
void Apply(bool buttons[], float& lx, float& ly, float& rx, float& ry);
// Имитация наведения луча в кинотеатре.
bool Point(float& u, float& v);
// Explicit scripted button, independent of the user's real controller.
bool Button(int index);
}
