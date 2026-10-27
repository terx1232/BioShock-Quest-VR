"""Модели предметов BioShock (выгружены UE Viewer из пакетов игры в glTF) -> меш для мода.

Бур Большого Папочки (StaticMesh ConeDrill, текстура BOUNCER_WEAPONS_DIFF) и гаечный ключ
(StaticMesh WP_AI_Wrench; его текстура в пакетах без имени, поэтому цвет - сталь) переводятся в
пространство позы рукоятки контроллера OpenXR (метры, -Z вперёд, +Y вверх) и пишутся C++-массивом
с цветами вершин, как модели контроллеров (controllermesh.inc).

    umodel -game=bio -path=<Bioshock\\Content> -export -gltf -out=work\\props Maps/1-Welcome.bsm ConeDrill StaticMesh
    umodel -game=bio -path=<Bioshock\\Content> -export -gltf -out=work\\props Maps/1-Welcome.bsm WP_AI_Wrench StaticMesh
    umodel -game=bio -path=<Bioshock\\Content> -export -png -out=work\\props Maps/1-Welcome.bsm BOUNCER_WEAPONS_DIFF Texture
    py -3 gltf2prop.py <work\\props\\1-Welcome> -o ../src/propmesh.inc
"""
import argparse
import json
import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "devtools" / "py"))
from PIL import Image, ImageFilter  # noqa: E402

COMP = {5121: ('B', 1), 5123: ('H', 2), 5125: ('I', 4), 5126: ('f', 4)}
NUM = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}


def accessor(gltf, binc, idx):
    a = gltf['accessors'][idx]
    bv = gltf['bufferViews'][a['bufferView']]
    fmt, size = COMP[a['componentType']]
    n = NUM[a['type']]
    stride = bv.get('byteStride', size * n)
    base = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
    return [struct.unpack_from('<' + fmt * n, binc, base + i * stride) for i in range(a['count'])]


def load(path):
    gltf = json.loads(Path(path).read_text())
    binc = (Path(path).parent / gltf['buffers'][0]['uri']).read_bytes()
    tris = []
    for mesh in gltf['meshes']:
        for prim in mesh['primitives']:
            pos = accessor(gltf, binc, prim['attributes']['POSITION'])
            nrm = accessor(gltf, binc, prim['attributes']['NORMAL'])
            uv = accessor(gltf, binc, prim['attributes']['TEXCOORD_0']) if 'TEXCOORD_0' in prim['attributes'] else None
            idx = [i[0] for i in accessor(gltf, binc, prim['indices'])]
            for t in range(0, len(idx) - 2, 3):
                tris.append([(pos[k], nrm[k], uv[k] if uv else (0, 0)) for k in idx[t:t + 3]])
    return tris


def to_grip(p, pivot, scale, back):
    # модель: +X - к острию/голове, +Y - вверх; рукоятка: -Z вперёд, +Y вверх, +X вправо
    x, y, z = p[0] - pivot[0], p[1] - pivot[1], p[2] - pivot[2]
    return (z * scale, y * scale, -x * scale + back)


def convert(tris, pivot, scale, back, color_fn):
    out = []
    for tri in tris:
        verts = []
        for p, n, uv in tri:
            gp = to_grip(p, pivot, scale, back)
            gn = (n[2], n[1], -n[0])
            l = math.sqrt(sum(c * c for c in gn)) or 1
            gn = tuple(c / l for c in gn)
            verts.append((gp, gn, color_fn(p, uv)))
        out.append(verts)
    return out


def emit(name, tris):
    lines = [f"// {len(tris)} triangles", f"static const MeshVertex {name}[] = {{"]
    for tri in tris:
        for p, n, c in tri:
            rgb = tuple(max(0, min(255, int(x * 255))) for x in c)
            lines.append("    {%.5ff,%.5ff,%.5ff, %.3ff,%.3ff,%.3ff, 0x%02x%02x%02x}," % (*p, *n, *rgb))
    lines.append("};")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("props", help="папка выгрузки UE Viewer (с StaticMesh/ и Texture/)")
    ap.add_argument("-o", "--out", required=True)
    ap.add_argument("--drill-length", type=float, default=0.36, help="длина бура в метрах")
    ap.add_argument("--wrench-length", type=float, default=0.42, help="длина ключа в метрах")
    a = ap.parse_args()
    root = Path(a.props)

    drill = load(root / "StaticMesh" / "ConeDrill.gltf")
    tex = Image.open(root / "Texture" / "BOUNCER_WEAPONS_DIFF.png").convert("RGB").filter(ImageFilter.GaussianBlur(2))

    def drill_color(p, uv):
        u, v = uv
        px = tex.getpixel((min(tex.width - 1, int((u % 1) * tex.width)), min(tex.height - 1, int((v % 1) * tex.height))))
        return [c / 255 for c in px]

    xs = [p[0] for t in drill for p, _, _ in t]
    lo, hi = min(xs), max(xs)
    # основание бура - в кулаке, чуть позади рукоятки
    drill_mesh = convert(drill, (lo, 0, 0), a.drill_length / (hi - lo), 0.02, drill_color)

    wrench = load(root / "StaticMesh" / "WP_AI_Wrench.gltf")
    xs = [p[0] for t in wrench for p, _, _ in t]
    lo, hi = min(xs), max(xs)
    ys = [p[1] for t in wrench for p, _, _ in t if p[0] < lo + (hi - lo) * 0.2]
    yc = (min(ys) + max(ys)) / 2

    def wrench_color(p, uv):
        # сталь; голова (губки и гайка) темнее и с ржавчиной, рукоять светлее
        t = (p[0] - lo) / (hi - lo)
        if t > 0.72:
            return (0.42, 0.36, 0.30)
        return (0.55, 0.55, 0.57)

    # держим за рукоять у конца (12% длины), голова вперёд
    pivot = (lo + (hi - lo) * 0.12, yc, 0)
    wrench_mesh = convert(wrench, pivot, a.wrench_length / (hi - lo), 0.0, wrench_color)

    out = ["// Сгенерировано mod/tools/gltf2prop.py из моделей BioShock (UE Viewer): ConeDrill, WP_AI_Wrench",
           emit("kPropDrill", drill_mesh), emit("kPropWrench", wrench_mesh)]
    Path(a.out).write_text("\n".join(out) + "\n", encoding="utf-8")
    print(f"drill {len(drill_mesh)} tris, wrench {len(wrench_mesh)} tris -> {a.out}")


if __name__ == "__main__":
    main()
