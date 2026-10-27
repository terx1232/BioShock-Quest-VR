"""Production gameplay HUD on a hidden host GPU window; no game input."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/tests/quest-hud';OUT.mkdir(parents=True,exist_ok=True)
compiler=next((ROOT/'dependencies/build').glob('llvm-mingw*/bin/i686-w64-mingw32-clang++.exe'))
target=OUT/'hud.exe'
subprocess.run([str(compiler),'-O2','-std=c++17','-static',str(ROOT/'source/native/tests/gameplay_hud.cpp'),
                str(ROOT/'source/native/src/gameplayhud.cpp'),'-o',str(target),'-ld3d9'],check=True)
result=subprocess.run([str(target)],cwd=OUT,capture_output=True,text=True,timeout=30)
(OUT/'result.log').write_text(result.stdout+result.stderr)
print(result.stdout+result.stderr,end='')
if result.returncode:raise RuntimeError('Gameplay HUD regression failed')
minhook=next((ROOT/'dependencies/build').glob('*minhook*/include/MinHook.h')).parent
target=OUT/'hudraster.exe'
subprocess.run([str(compiler),'-O2','-std=c++17','-static','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I',str(minhook),str(ROOT/'source/native/tests/hudraster_gpu.cpp'),'-o',str(target),'-ld3d9'],check=True)
result=subprocess.run([str(target)],cwd=OUT,capture_output=True,text=True,timeout=30)
with (OUT/'result.log').open('a') as f:f.write(result.stdout+result.stderr)
print(result.stdout+result.stderr,end='')
if result.returncode:raise RuntimeError('High-resolution HUD redirect regression failed')
