"""Rehost the original 2007 Flash input machine code; no Quest/game input."""
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'research'))
from pe import data,va2off
OUT=ROOT/'build/tests/quest-flashpointer';OUT.mkdir(parents=True,exist_ok=True)
start,end=0x109bb6f0,0x109bb900
(OUT/'native.bin').write_bytes(data[va2off(start):va2off(end)])
compiler=next((ROOT/'dependencies/build').glob('llvm-mingw*/bin/i686-w64-mingw32-clang++.exe'))
mh=next((ROOT/'dependencies/build').glob('*minhook*/include/MinHook.h')).parent.parent
objects=[]
for name in ('buffer.c','hook.c','trampoline.c','hde/hde32.c'):
    obj=OUT/(Path(name).stem+'.o')
    subprocess.run([str(compiler.with_name('i686-w64-mingw32-clang.exe')),'-O2','-c',str(mh/'src'/name),'-I',str(mh/'include'),'-o',str(obj)],check=True)
    objects.append(str(obj))
subprocess.run([str(compiler),'-O2','-std=c++17','-static','-I',str(mh/'include'),
               str(ROOT/'source/native/src/flashpointer.cpp'),str(ROOT/'source/native/tests/flashpointer_fixture.cpp'),
               *objects,'-o',str(OUT/'flashpointer.exe')],check=True)
run=subprocess.run([str(OUT/'flashpointer.exe')],cwd=OUT,capture_output=True,text=True,timeout=20)
(OUT/'result.log').write_text(run.stdout+run.stderr)
print(run.stdout+run.stderr,end='')
if run.returncode:raise RuntimeError('Original Flash input alignment regression failed')
subprocess.run([str(compiler),'-O2','-std=c++17','-static','-ffunction-sections','-fdata-sections',
               '-Wl,--gc-sections','-I',str(mh/'include'),str(ROOT/'source/native/tests/mouse_queue_fixture.cpp'),
               *objects,'-o',str(OUT/'queue.exe'),'-ldxguid'],check=True)
run=subprocess.run([str(OUT/'queue.exe')],cwd=OUT,capture_output=True,text=True,timeout=10)
with (OUT/'result.log').open('a') as f:f.write(run.stdout+run.stderr)
print(run.stdout+run.stderr,end='')
if run.returncode:raise RuntimeError('Production mouse queue regression failed')
