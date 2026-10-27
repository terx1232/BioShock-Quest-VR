#pragma once
#include <windows.h>

#include "xrapi.h"

namespace input {
bool CombatActive(); // recent user weapon/plasmid action; never injects input
// Прячет геймпад от игры (XInput "не подключён"): игра работает с клавиатурой и мышью.
bool Install();
// Окно игры, которому идут клавиши.
void SetWindow(HWND window);
// true - игровой режим VR: контроллеры Quest нажимают клавиши и кнопки мыши.
void SetActive(bool active);
bool IsActive();
// Каждый кадр в игровом режиме: кнопки контроллеров -> клавиши и мышь.
void Update(const xrapi::State& state);
// Держит ли мод сейчас эту клавишу (для GetAsyncKeyState/GetKeyState).
bool KeyHeld(int vk);
// Рука, по которой сейчас целиться: 0 - левая (плазмид), 1 - правая (оружие), -1 - по взгляду.
int AimHand();
}
