"""Exercise the production viewport scanner against a synthetic process object."""
import subprocess
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'research'))
from pe import data,va2off
p=va2off(0x10a2f05a)
assert data[p:p+9]==bytes.fromhex('8b 46 04 8b 80 14 04 00 00'), 'Native crash path/Actor offset changed'
OUT=ROOT/'build/tests/quest-viewport';OUT.mkdir(parents=True,exist_ok=True)
compiler=next((ROOT/'dependencies/build').glob('llvm-mingw*/bin/i686-w64-mingw32-clang++.exe'))
subprocess.run([str(compiler),'-O2','-std=c++17','-static',
                str(ROOT/'source/native/src/engineviewport.cpp'),
                str(ROOT/'source/native/tests/engineviewport_fixture.cpp'),
                '-o',str(OUT/'viewport.exe')],check=True)
result=subprocess.run([str(OUT/'viewport.exe')],capture_output=True,text=True,timeout=15)
(OUT/'result.log').write_text(result.stdout+result.stderr)
print(result.stdout+result.stderr,end='')
if result.returncode:raise RuntimeError('Viewport discovery regression failed')
