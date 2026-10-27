"""Replay native camera recoil through the actual VR hook; no game input."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[2]
out=ROOT/'build/tests/quest-camera';out.mkdir(parents=True,exist_ok=True)
compiler=next((ROOT/'dependencies/build').glob('llvm-mingw*/bin/i686-w64-mingw32-clang++.exe'))
minhook=next((ROOT/'dependencies/build').glob('*minhook*/include/MinHook.h')).parent
fixture=ROOT/'source/native/tests/camera_recoil.cpp'
baseline=ROOT/'archive/quest-source/20261027-build85/source/native/src/camera.cpp'
for mode in ('current','release','baseline85'):
    if mode=='baseline85' and not baseline.exists():continue
    target=out/(mode+'.exe')
    args=[str(compiler),'-std=c++17','-O2','-static','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I',str(minhook)]
    if mode=='release':args+=['-DBIOSHOCK_RELEASE']
    if mode=='baseline85':args+=['-DCAMERA_SOURCE="'+baseline.as_posix()+'"']
    subprocess.run([*args,str(fixture),'-o',str(target)],check=True)
    r=subprocess.run([str(target)],capture_output=True,text=True,timeout=15)
    (out/(mode+'.log')).write_text(r.stdout+r.stderr)
    if mode in ('current','release'):
        print(r.stdout+r.stderr,end='')
        if r.returncode:raise RuntimeError('Production VR camera regression')
    else:
        assert r.returncode==1 and 'shotgun camera-only recoil cannot accumulate' in r.stderr,r.stdout+r.stderr
        print('PASS: unchanged build85 reproduces the cosmetic-yaw body-turn failure in the same production-hook fixture')
