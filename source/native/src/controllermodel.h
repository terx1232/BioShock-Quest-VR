// Модели контроллеров Quest 2 (WebXR Input Profiles, oculus-touch-v3, MIT): треугольники с
// цветами из текстуры и заранее посчитанным освещением. Координаты - метры в пространстве
// позы рукоятки (grip) контроллера OpenXR.
#pragma once

namespace controllermodel {

struct Vertex {
    float x, y, z;
    unsigned long color;  // D3DCOLOR
};

// Готовый список треугольников (по 3 вершины), count - число треугольников.
const Vertex* Get(bool left, unsigned& count);
// Предметы из BioShock для меню: левая рука - гаечный ключ, правая - бур Большого Папочки.
const Vertex* PropModel(bool left, unsigned& count);

}  // namespace controllermodel
