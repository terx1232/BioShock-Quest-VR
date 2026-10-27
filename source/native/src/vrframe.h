// Снимок шлема на каждый кадр: все запросы камеры одного кадра (игра спрашивает вид ~30 раз:
// рендер, звук, отсечение...) видят одну позу и один глаз.
#pragma once
#include "vrmath.h"
#include "xrapi.h"

namespace vrframe {

struct Frame {
    bool active;       // данные со шлема приходят
    bool inGame;       // идёт игра (камера игрока считается), а не меню/загрузка/ролик
    int eye;           // 0 левый, 1 правый (поочерёдный рендер глаз)
    int sync;          // номер позы для пикселя синхронизации
    unsigned long long frameId; // request that produced this frame's camera
    vr::Mat3 head;     // ориентация головы в осях Unreal, относительно комнаты
    vr::Vec3 headPos;  // смещение головы от точки центровки, единицы и оси Unreal
    float ipdUnits;    // расстояние между глазами в единицах Unreal
    float fov;         // поле зрения по горизонтали, градусы
    float fovV;        // по вертикали (из соотношения сторон кадра)
};

// Раз на показанный кадр: меняет глаз, берёт свежую позу, поворачивает тело стиком.
void Advance();
// Copies protected against concurrent publication by the Present thread.
Frame Current();
// Состояние шлема, по которому строится этот кадр (та же поза, что в пикселе синхронизации).
xrapi::State State();
struct Snapshot { Frame frame; xrapi::State state; };
Snapshot Capture();
// Сколько раз центровали положение (сброс накопленной ходьбы по комнате).
unsigned RecenterCount();
void SetInGame(bool inGame);
// Соотношение сторон кадра игры (ширина / высота).
void SetAspect(float aspect);
float VerticalFov(float horizontalDeg);
// Следующий кадр отсчитывает позицию головы отсюда.
void Recenter();

// Поворот тела (радианы): правый стик, плюс развороты, которые делает сама игра.
bool HasBody();
float BodyYaw();
void SetBodyYaw(float yaw);
void AddBodyYaw(float delta);

}  // namespace vrframe
