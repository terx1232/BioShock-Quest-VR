#include "controllermodel.h"

#include <math.h>

#include <vector>

namespace controllermodel {
namespace {

struct MeshVertex {
    float px, py, pz, nx, ny, nz;
    unsigned rgb;
};

#include "controllermesh.inc"
#include "propmesh.inc"

std::vector<Vertex> Light(const MeshVertex* src, unsigned n) {
    std::vector<Vertex> out(n);
    // Свет сверху-спереди и немного заполняющего снизу, чтобы чёрный пластик читался
    const float lx = 0.35f, ly = 0.8f, lz = 0.45f;
    for (unsigned i = 0; i < n; i++) {
        const MeshVertex& v = src[i];
        float d = v.nx * lx + v.ny * ly + v.nz * lz;
        float k = 0.45f + 0.75f * (d > 0 ? d : 0) + 0.15f * (d < 0 ? -d : 0);
        unsigned r = (v.rgb >> 16) & 0xFF, g = (v.rgb >> 8) & 0xFF, b = v.rgb & 0xFF;
        // Тёмные участки (корпус) чуть приподнимаем, иначе в чёрной пустоте его не видно
        const unsigned floor = 38;
        r = r < floor ? floor : r;
        g = g < floor ? floor : g;
        b = b < floor ? floor : b;
        auto c = [&](unsigned x) {
            float f = x * k;
            return (unsigned)(f > 255 ? 255 : f);
        };
        out[i] = {v.px, v.py, v.pz, 0xFF000000u | (c(r) << 16) | (c(g) << 8) | c(b)};
    }
    return out;
}

}  // namespace

const Vertex* Get(bool left, unsigned& count) {
    static std::vector<Vertex> l = Light(kControllerLeft, sizeof(kControllerLeft) / sizeof(kControllerLeft[0]));
    static std::vector<Vertex> r = Light(kControllerRight, sizeof(kControllerRight) / sizeof(kControllerRight[0]));
    const std::vector<Vertex>& v = left ? l : r;
    count = (unsigned)(v.size() / 3);
    return v.data();
}

const Vertex* PropModel(bool left, unsigned& count) {
    static std::vector<Vertex> wrench = Light(kPropWrench, sizeof(kPropWrench) / sizeof(kPropWrench[0]));
    static std::vector<Vertex> drill = Light(kPropDrill, sizeof(kPropDrill) / sizeof(kPropDrill[0]));
    const std::vector<Vertex>& v = left ? wrench : drill;
    count = (unsigned)(v.size() / 3);
    return v.data();
}

}  // namespace controllermodel
