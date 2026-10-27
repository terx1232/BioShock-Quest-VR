"""Шейдеры мода (HLSL) -> байткод D3D9 (ps_3_0, *_vs - vs_3_0) системным d3dcompiler_47.dll -> C-массив.

    py -3 compile_shaders.py ../shaders/fullscreen_vs.hlsl ../shaders/depthstereo.hlsl -o ../src/stereoshaders.inc
"""
import argparse
import ctypes
from ctypes import wintypes
from pathlib import Path


def compile_hlsl(path, entry="main", target=None):
    target = target or ("vs_3_0" if Path(path).stem.endswith("_vs") else "ps_3_0")
    src = Path(path).read_bytes()
    d3dc = ctypes.WinDLL("d3dcompiler_47.dll")
    code = ctypes.c_void_p()
    err = ctypes.c_void_p()
    hr = d3dc.D3DCompile(src, len(src), str(path).encode(), None, None, entry.encode(), target.encode(),
                         1 << 15, 0, ctypes.byref(code), ctypes.byref(err))  # D3DCOMPILE_OPTIMIZATION_LEVEL3
    def blob(p):
        vt = ctypes.cast(ctypes.cast(p, ctypes.POINTER(ctypes.c_void_p))[0], ctypes.POINTER(ctypes.c_void_p))
        ptr = ctypes.WINFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p)(vt[3])(p)
        size = ctypes.WINFUNCTYPE(ctypes.c_size_t, ctypes.c_void_p)(vt[4])(p)
        return ctypes.string_at(ptr, size)
    if hr != 0:
        raise SystemExit(f"{path}: D3DCompile failed 0x{hr & 0xffffffff:08x}\n" + (blob(err).decode(errors="replace") if err else ""))
    return blob(code)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("hlsl", nargs="+")
    ap.add_argument("-o", "--out", required=True)
    a = ap.parse_args()
    out = ["// Сгенерировано mod/tools/compile_shaders.py (d3dcompiler_47, ps_3_0)"]
    for h in a.hlsl:
        code = compile_hlsl(h)
        stem = Path(h).stem
        name = "k" + "".join(p.capitalize() for p in stem.split("_")) + ("" if stem.endswith("_vs") else "PS")
        words = [int.from_bytes(code[i:i + 4], "little") for i in range(0, len(code), 4)]
        out.append(f"static const unsigned long {name}[] = {{")
        for i in range(0, len(words), 8):
            out.append("    " + ", ".join(f"0x{w:08x}" for w in words[i:i + 8]) + ",")
        out.append("};")
        print(f"{h}: {len(code)} bytes -> {name}")
    Path(a.out).write_text("\n".join(out) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
