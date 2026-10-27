"""Production diagnostics are compiled out, with accepted rendering retained."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[2];out=ROOT/'build/tests/quest-release';out.mkdir(parents=True,exist_ok=True)
compiler=next((ROOT/'dependencies/build').glob('llvm-mingw*/bin/i686-w64-mingw32-clang++.exe'))
target=out/'release.exe';native=ROOT/'source/native'
subprocess.run([str(compiler),'-DBIOSHOCK_RELEASE','-O2','-std=c++17','-static',str(native/'tests/release_config.cpp'),str(native/'src/config.cpp'),str(native/'src/log.cpp'),str(native/'src/release_stubs.cpp'),'-o',str(target)],check=True)
r=subprocess.run([str(target)],capture_output=True,text=True,timeout=15);(out/'result.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='')
if r.returncode:raise RuntimeError('Release diagnostics regression')
binary=(ROOT/'build/native/quest-release/d3d9.dll').read_bytes()
for needle in (b'vrcmd.txt',b'Stall %d: eip=',b'GPU stages (%s)',b'FPS: %.1f',b'cursor candidates after first step'):
    assert needle not in binary,'Release contains diagnostic worker: '+repr(needle)
accepted=ROOT/'archive/quest-source/20261027-build86/source/native'
for name in ('depthstereo.hlsl','depthcache.hlsl','weaponlayer.hlsl','hudpack.hlsl'):
    assert (native/'shaders'/name).read_bytes()==(accepted/'shaders'/name).read_bytes(),name
print('PASS: release DLL excludes command/profiling workers; accepted world/weapon/HUD shader sources remain byte-identical to86')
