"""Controller projection and actual depth-aware marker shader; no game input."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[2]
out=ROOT/'build/tests/quest-reticle';out.mkdir(parents=True,exist_ok=True)
compiler=next((ROOT/'dependencies/build').glob('llvm-mingw*/bin/i686-w64-mingw32-clang++.exe'))
for name in ('aim_projection','aim_pipeline'):
    sources=[ROOT/'source/native/tests'/(name+'.cpp')]
    if name=='aim_pipeline':sources.append(ROOT/'source/native/src/stereo.cpp')
    exe=out/(name+'.exe')
    subprocess.run([str(compiler),'-O2','-std=c++17','-static',*map(str,sources),'-o',str(exe),'-ld3d9'],check=True)
    result=subprocess.run([str(exe)],cwd=out,capture_output=True,text=True,timeout=30)
    (out/(name+'.log')).write_text(result.stdout+result.stderr)
    print(result.stdout+result.stderr,end='')
    if result.returncode:raise RuntimeError(name+' failed')
