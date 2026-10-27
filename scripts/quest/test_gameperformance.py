"""Verify which Wine graphics thread is registered with the XR runtime."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[2]
out=ROOT/'build/tests/quest-gameperformance';out.mkdir(parents=True,exist_ok=True)
compiler=next((ROOT/'dependencies/build').glob('llvm-mingw*/bin/i686-w64-mingw32-clang++.exe'))
target=out/'renderthread.exe'
subprocess.run([str(compiler),'-O2','-std=c++17','-static',str(ROOT/'source/native/tests/renderthread_fixture.cpp'),'-o',str(target)],check=True)
r=subprocess.run([str(target)],capture_output=True,text=True,timeout=10)
(out/'result.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='')
if r.returncode:raise RuntimeError('Game performance thread selection regression failed')
