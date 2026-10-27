"""Модель контроллера (glTF .glb) -> простой меш для мода: треугольники с запечёнными цветами.

Цвет каждой вершины берётся из текстуры по её UV, так что моду не нужен декодер картинок.
Выход - C++-заголовок с массивом вершин (позиция, нормаль, цвет), встраивается в d3d9.dll.

    py -3 glb2mesh.py left.glb right.glb -o ../src/controllermesh.inc
"""
import argparse
import io
import json
import math
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "devtools" / "py"))
from PIL import Image, ImageFilter  # noqa: E402

COMP = {5120: ('b', 1), 5121: ('B', 1), 5122: ('h', 2), 5123: ('H', 2), 5125: ('I', 4), 5126: ('f', 4)}
NUM = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}


def load(path):
    d = Path(path).read_bytes()
    jlen = struct.unpack_from('<I', d, 12)[0]
    gltf = json.loads(d[20:20 + jlen])
    off = 20 + jlen
    blen = struct.unpack_from('<I', d, off)[0]
    return gltf, d[off + 8:off + 8 + blen]


def accessor(gltf, binc, idx):
    a = gltf['accessors'][idx]
    bv = gltf['bufferViews'][a['bufferView']]
    fmt, size = COMP[a['componentType']]
    n = NUM[a['type']]
    stride = bv.get('byteStride', size * n)
    base = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
    out = []
    for i in range(a['count']):
        vals = struct.unpack_from('<' + fmt * n, binc, base + i * stride)
        if a.get('normalized'):
            m = float((1 << (8 * size - (0 if fmt.isupper() else 1))) - 1)
            vals = tuple(v / m for v in vals)
        out.append(vals)
    return out


def mat_mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]


def node_matrix(n):
    if 'matrix' in n:
        m = n['matrix']
        return [[m[c * 4 + r] for c in range(4)] for r in range(4)]
    t = n.get('translation', [0, 0, 0])
    q = n.get('rotation', [0, 0, 0, 1])
    s = n.get('scale', [1, 1, 1])
    x, y, z, w = q
    r = [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
         [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
         [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]
    return [[r[0][0] * s[0], r[0][1] * s[1], r[0][2] * s[2], t[0]],
            [r[1][0] * s[0], r[1][1] * s[1], r[1][2] * s[2], t[1]],
            [r[2][0] * s[0], r[2][1] * s[1], r[2][2] * s[2], t[2]],
            [0, 0, 0, 1]]


def texture_for(gltf, binc, prim, cache):
    mat = gltf['materials'][prim['material']] if 'material' in prim else {}
    pbr = mat.get('pbrMetallicRoughness', {})
    factor = pbr.get('baseColorFactor', [1, 1, 1, 1])
    tex = pbr.get('baseColorTexture')
    if not tex:
        return None, factor
    src = gltf['textures'][tex['index']]['source']
    if src not in cache:
        img = gltf['images'][src]
        bv = gltf['bufferViews'][img['bufferView']]
        data = binc[bv.get('byteOffset', 0):bv.get('byteOffset', 0) + bv['byteLength']]
        # размытие: цвет вершины - среднее по окрестности, а не случайный тексель (без пятен)
        cache[src] = Image.open(io.BytesIO(data)).convert('RGB').filter(ImageFilter.GaussianBlur(6))
    return cache[src], factor


def convert(path):
    gltf, binc = load(path)
    tris = []
    cache = {}

    def walk(ni, parent):
        n = gltf['nodes'][ni]
        m = mat_mul(parent, node_matrix(n))
        if 'mesh' in n:
            for prim in gltf['meshes'][n['mesh']]['primitives']:
                pos = accessor(gltf, binc, prim['attributes']['POSITION'])
                nrm = accessor(gltf, binc, prim['attributes']['NORMAL']) if 'NORMAL' in prim['attributes'] else None
                uv = accessor(gltf, binc, prim['attributes']['TEXCOORD_0']) if 'TEXCOORD_0' in prim['attributes'] else None
                idx = [i[0] for i in accessor(gltf, binc, prim['indices'])] if 'indices' in prim else list(range(len(pos)))
                img, factor = texture_for(gltf, binc, prim, cache)
                verts = []
                for i, p in enumerate(pos):
                    wp = [m[r][0] * p[0] + m[r][1] * p[1] + m[r][2] * p[2] + m[r][3] for r in range(3)]
                    if nrm:
                        nn = nrm[i]
                        wn = [m[r][0] * nn[0] + m[r][1] * nn[1] + m[r][2] * nn[2] for r in range(3)]
                        l = math.sqrt(sum(c * c for c in wn)) or 1
                        wn = [c / l for c in wn]
                    else:
                        wn = [0, 1, 0]
                    if img and uv:
                        u, v = uv[i]
                        px = img.getpixel((min(img.width - 1, max(0, int((u % 1) * img.width))),
                                           min(img.height - 1, max(0, int((v % 1) * img.height)))))
                        col = [px[k] / 255 * factor[k] for k in range(3)]
                    else:
                        col = factor[:3]
                    verts.append((wp, wn, col))
                for t in range(0, len(idx) - 2, 3):
                    tris.append((verts[idx[t]], verts[idx[t + 1]], verts[idx[t + 2]]))
        for c in n.get('children', []):
            walk(c, m)

    ident = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
    scene = gltf['scenes'][gltf.get('scene', 0)]
    for ni in scene['nodes']:
        walk(ni, ident)
    return tris


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
    ap.add_argument("left")
    ap.add_argument("right")
    ap.add_argument("-o", "--out", required=True)
    a = ap.parse_args()
    left, right = convert(a.left), convert(a.right)
    out = ["// Сгенерировано mod/tools/glb2mesh.py из моделей WebXR Input Profiles (oculus-touch-v3, MIT)",
           emit("kControllerLeft", left), emit("kControllerRight", right)]
    Path(a.out).write_text("\n".join(out) + "\n", encoding="utf-8")
    print(f"left {len(left)} tris, right {len(right)} tris -> {a.out}")


if __name__ == "__main__":
    main()
