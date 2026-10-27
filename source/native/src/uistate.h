#pragma once

namespace uistate {
// Перехватывает команды контекстов ввода игры.
bool Install();
// Открыто меню, которое надо показывать экраном (пауза, инвентарь, взлом, автоматы...).
bool MenuOpen();
// Loot remains in the world; its original input context still handles actions.
bool ContainerOpen();
bool Cinematic();
void UpdateCinematic(void* controller);
}
