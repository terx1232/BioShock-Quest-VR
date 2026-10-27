#pragma once
#include "vrmath.h"

namespace camera {
// Перехватывает расчёт вида игры; false, если Bioshock.exe не той версии.
bool Install();
// Сколько раз игра спрашивала камеру с прошлого вызова (0 = меню, загрузка, ролик).
int TakeCallCount();
// A validated possessed player view in the frame currently being presented.
bool HasPendingView();
// Миллисекунды процессора, потраченные модом в камере с прошлого вызова
double TakeModMs();
// Номер позы шлема, с которой камера посчитала последний вид (-1 - ещё не было)
int CameraSync();
unsigned long long CameraFrameId();
unsigned CameraTicks();
unsigned long CameraThread();
// Положение головы в мире игры (без сдвига глаза) и поворот тела из последнего расчёта вида.
bool HeadWorld(vr::Vec3& head, vr::Mat3& body);
// Размер экрана движка (UViewport SizeX/SizeY): движок рисует в левый верхний угол кадра
// такого размера. Вызывать каждый кадр.
bool SetEngineSize(unsigned w, unsigned h, unsigned backW, unsigned backH);
void GraphicsReady();
bool ViewportReady();
bool EngineSizeMatches(unsigned w,unsigned h);
// Текущая высота экрана движка (0 - окно ещё не найдено).
unsigned ViewportHeight();
}
