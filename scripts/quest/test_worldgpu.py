"""Run the production world snapshot/PBO pipeline on a hidden GLES3 context."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
NATIVE=ROOT/'source/android/native'
HUD='--hud' in sys.argv
SHADOWS='--shadows' in sys.argv
OUT=ROOT/('build/tests/quest-shadows' if SHADOWS else 'build/tests/quest-hudgpu' if HUD else 'build/tests/quest-worldgpu');OUT.mkdir(parents=True,exist_ok=True)
ANGLE=Path('C:/Program Files/Mozilla Firefox')
if not all((ANGLE/name).is_file() for name in ('libEGL.dll','libGLESv2.dll')):
    raise RuntimeError('This host GPU fixture needs the installed Firefox ANGLE GLES runtime')
toolchain=next((ROOT/'dependencies/build').glob('llvm-mingw*/bin/x86_64-w64-mingw32-clang++.exe')).parent
headers=Path('D:/Android/Sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/windows-x86_64/sysroot/usr/include')
for name in ('EGL','GLES3','KHR'):
    shutil.copytree(headers/name,OUT/'include'/name,dirs_exist_ok=True)
sources=[NATIVE/'worldpresenter.inc',NATIVE/'hudpresenter.inc',NATIVE/'cinemamath.inc',NATIVE/'cinemaglguard.inc',NATIVE/'cinemacopy.inc',ROOT/'source/native/tests'/('shadow_quality_gpu.cpp' if SHADOWS else 'hudpresenter_gpu.cpp' if HUD else 'worldpresenter_gpu.cpp')]
text='\n'.join(p.read_text() for p in sources)
for prefix,dll in [('gl','libGLESv2.dll'),('egl','libEGL.dll')]:
    functions=sorted(set(re.findall(r'\b('+prefix+r'[A-Z]\w*)\s*\(',text)))
    definition=OUT/(prefix+'.def');definition.write_text('LIBRARY '+dll+'\nEXPORTS\n'+'\n'.join(functions)+'\n')
    subprocess.run([str(toolchain/'x86_64-w64-mingw32-dlltool.exe'),'-d',str(definition),'-l',str(OUT/(prefix+'.a'))],check=True)
exe=OUT/'worldgpu.exe'
subprocess.run([str(toolchain/'x86_64-w64-mingw32-clang++.exe'),'-std=c++17','-O2','-static',
    '-I',str(OUT/'include'),'-I',str(ROOT/'dependencies/build/openxr/include'),str(sources[-1]),
    str(OUT/'gl.a'),str(OUT/'egl.a'),'-o',str(exe)],check=True)
result=subprocess.run([str(exe)],cwd=OUT,env={**os.environ,'PATH':str(ANGLE)+os.pathsep+os.environ.get('PATH','')},capture_output=True,text=True,timeout=45)
(OUT/'result.log').write_text(result.stdout+result.stderr)
print(result.stdout+result.stderr,end='')
if result.returncode:raise RuntimeError('Production GLES world presentation regression failed: '+str(result.returncode))
