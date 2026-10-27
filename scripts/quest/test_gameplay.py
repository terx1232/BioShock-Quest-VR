"""Host-only weapon grip and reflected gameplay flags; no headset input."""
import subprocess
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/tests/quest-gameplay';OUT.mkdir(parents=True,exist_ok=True)
compiler=next((ROOT/'dependencies/build').glob('llvm-mingw*/bin/i686-w64-mingw32-clang++.exe'))
for name,files in [('weapon-grip',['tests/weapon_grip.cpp']),('bool-property',['tests/bool_property.cpp','src/uobject.cpp'])]:
    target=OUT/(name+'.exe')
    subprocess.run([str(compiler),'-O2','-std=c++17','-static',*[str(ROOT/'source/native'/f) for f in files],'-o',str(target)],check=True)
    result=subprocess.run([str(target)],capture_output=True,text=True,timeout=10)
    (OUT/(name+'.log')).write_text(result.stdout+result.stderr)
    print(result.stdout+result.stderr,end='')
    if result.returncode:raise RuntimeError(name+' test failed')
subprocess.run([sys.executable,'-I',str(ROOT/'scripts/quest/test_weapon_geometry.py')],check=True)
