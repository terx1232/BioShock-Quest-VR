"""Host-only test of the game/menu resize dispatcher; no headset input."""
import subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/tests/quest-resolution';OUT.mkdir(parents=True,exist_ok=True)
compiler=next((ROOT/'dependencies/build').glob('llvm-mingw*/bin/i686-w64-mingw32-clang++.exe'))
subprocess.run([str(compiler),'-O2','-std=c++17','-static',
               str(ROOT/'source/native/src/worldresolution.cpp'),
               str(ROOT/'source/native/tests/worldresolution_fixture.cpp'),
               '-o',str(OUT/'resolution.exe')],check=True)
result=subprocess.run([str(OUT/'resolution.exe')],capture_output=True,text=True,timeout=15)
(OUT/'result.log').write_text(result.stdout+result.stderr)
print(result.stdout+result.stderr,end='')
if result.returncode:raise RuntimeError('Resolution dispatcher failed')
