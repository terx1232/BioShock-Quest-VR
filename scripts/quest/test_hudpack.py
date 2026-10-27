"""Hidden D3D9 test of the production GPU-only HUD transport."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/tests/quest-hudpack';OUT.mkdir(parents=True,exist_ok=True)
compiler=next((ROOT/'dependencies/build').glob('llvm-mingw*/bin/i686-w64-mingw32-clang++.exe'))
exe=OUT/'hudpack.exe'
subprocess.run([str(compiler),'-std=c++17','-O2','-static',str(ROOT/'source/native/tests/hudpack_gpu.cpp'),'-o',str(exe),'-ld3d9'],check=True)
r=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);(OUT/'result.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='')
if r.returncode:raise RuntimeError('HUD transport GPU regression failed')
