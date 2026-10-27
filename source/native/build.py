"""Builds the BioShock VR mod (d3d9.dll, 32-bit) with llvm-mingw.

Finds llvm-mingw in dependencies/build and compiles the mod with MinHook.
Output: build/native/quest/d3d9.dll and the canonical Quest profile.
"""
import glob
import os
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

MOD = Path(__file__).resolve().parent
ROOT = MOD.parents[1]
DEVTOOLS = ROOT / "dependencies" / "build"
PCVR = "--pcvr" in sys.argv
RELEASE = "--release" in sys.argv
OUT = ROOT / "build" / "native" / ("pcvr" if PCVR else "quest-release" if RELEASE else "quest")


def find_toolchain():
    for clang in DEVTOOLS.glob("llvm-mingw*/bin/i686-w64-mingw32-clang++.exe"):
        return clang
    zips = sorted(list((DEVTOOLS / "dl").glob("llvm-mingw*x86_64*.zip")) +
                  list((Path.home() / "Downloads").glob("llvm-mingw*x86_64*.zip")),
                  key=lambda p: p.stat().st_mtime, reverse=True)
    for z in zips:
        print(f"Unpacking {z.name} ...")
        with zipfile.ZipFile(z) as archive:
            archive.extractall(DEVTOOLS)
        for clang in DEVTOOLS.glob("llvm-mingw*/bin/i686-w64-mingw32-clang++.exe"):
            return clang
    sys.exit("llvm-mingw not found: put llvm-mingw-*-ucrt-x86_64.zip into Downloads or devtools\\dl")


def find_minhook():
    for header in DEVTOOLS.glob("*minhook*/include/MinHook.h"):
        return header.parent.parent
    sys.exit("MinHook sources not found in devtools")


def main():
    clang = find_toolchain()
    minhook = find_minhook()
    OUT.mkdir(parents=True, exist_ok=True)
    # HLSL is the source of truth; do not ship stale embedded shader bytecode.
    shaders = [MOD / "shaders" / name for name in
               ("fullscreen_vs.hlsl", "depthstereo.hlsl", "depthview.hlsl", "depthcopy.hlsl", "depthcache.hlsl", "depthmerge.hlsl", "weaponlayer.hlsl", "hudpack.hlsl", "aimreticle.hlsl")]
    subprocess.run([sys.executable, "-I", str(MOD / "tools" / "compile_shaders.py"),
                    *map(str, shaders), "-o", str(MOD / "src" / "stereoshaders.inc")], check=True)
    sources = [str(p) for p in (MOD / "src").glob("*.cpp")]
    if RELEASE:
        sources=[p for p in sources if Path(p).name not in ('devharness.cpp','stallsampler.cpp','crash.cpp')]
    mh_sources = [str(minhook / "src" / n) for n in ("buffer.c", "hook.c", "trampoline.c")]
    mh_sources.append(str(minhook / "src" / "hde" / "hde32.c"))
    cc = clang.with_name("i686-w64-mingw32-clang.exe")

    objects = []
    for src in mh_sources:
        obj = OUT / (Path(src).stem + ".o")
        subprocess.run([str(cc), "-O2", "-c", src, "-I", str(minhook / "include"), "-o", str(obj)], check=True)
        objects.append(str(obj))

    dll = OUT / "d3d9.dll"
    cmd = [str(clang), "-O2", "-std=c++17", "-shared", "-static", "-fno-exceptions",
           "-I", str(minhook / "include"), *sources, *objects,
           str(MOD / "d3d9.def"), "-o", str(dll), "-lws2_32", "-lpsapi", "-ldxguid", "-Wl,--kill-at", "-s"]
    if PCVR:
        cmd[1:1] = ["-DBIOSHOCK_PCVR", "-I", str(ROOT / "archive" / "pcvr" / "vendor" / "fear-vr" / "src" / "common")]
    if RELEASE:cmd.insert(1,"-DBIOSHOCK_RELEASE")
    print(" ".join(Path(c).name if os.sep in c else c for c in cmd))
    subprocess.run(cmd, check=True)
    shutil.copy2(ROOT / "archive" / "pcvr" / "BioshockVR.ini" if PCVR else ROOT / "config" / "quest" / "profiles" / ("release.ini" if RELEASE else "candidate.ini"), OUT / "BioshockVR.ini")
    print(f"OK: {dll} ({dll.stat().st_size // 1024} KB)")


if __name__ == "__main__":
    main()
