"""GPU tests of the Quest shader path and delayed depth initialization."""
import subprocess
import importlib.util
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
NATIVE=ROOT/'source'/'native'
OUT=ROOT/'build'/'tests'/'quest-depth'
OUT.mkdir(parents=True,exist_ok=True)
COMPILER=next((ROOT/'dependencies'/'build').glob('llvm-mingw*/bin/i686-w64-mingw32-clang++.exe'))
for mode in ('legacy','lazy'):
    command=[str(COMPILER),'-O2','-std=c++17','-static',str(NATIVE/'tests'/'depth_pipeline.cpp'),str(NATIVE/'src'/'stereo.cpp'),'-o',str(OUT/f'{mode}.exe'),'-ld3d9']
    if mode=='lazy': command.append('-DQUEST_LAZY_DEPTH_TEST')
    subprocess.run(command,check=True)
    result=subprocess.run([str(OUT/f'{mode}.exe')],cwd=OUT,capture_output=True,text=True,timeout=30)
    (OUT/f'{mode}.log').write_text(result.stdout+result.stderr)
    print(result.stdout+result.stderr)
    if result.returncode: raise RuntimeError(f'{mode} depth test failed')
cinema=OUT/'cinema.exe'
subprocess.run([str(COMPILER),'-O2','-std=c++17','-static',str(NATIVE/'tests'/'cinema_pipeline.cpp'),str(NATIVE/'src'/'theater.cpp'),'-o',str(cinema),'-ld3d9'],check=True)
result=subprocess.run([str(cinema)],cwd=OUT,capture_output=True,text=True,timeout=30)
(OUT/'cinema.log').write_text(result.stdout+result.stderr)
print(result.stdout+result.stderr)
if result.returncode: raise RuntimeError('Stereo cinema test failed')
spec=importlib.util.spec_from_file_location('quest_builder',ROOT/'scripts/quest/builder.py')
builder=importlib.util.module_from_spec(spec); spec.loader.exec_module(builder)
java=builder.find_java()
subprocess.run([str(java/'javac.exe'),'--release','11','-d',str(OUT),
                str(ROOT/'source/android/src/com/bioshockport/PoseWire.java'),
                str(NATIVE/'tests/PoseWireFixture.java')],check=True)
fixture=OUT/'pose-wire.bin'
subprocess.run([str(java/'java.exe'),'-cp',str(OUT),'PoseWireFixture',str(fixture)],check=True)
wire=OUT/'pose-wire.exe'
subprocess.run([str(COMPILER),'-O2','-std=c++17','-static',str(NATIVE/'tests/pose_wire.cpp'),'-o',str(wire)],check=True)
result=subprocess.run([str(wire),str(fixture)],capture_output=True,text=True,timeout=10)
(OUT/'pose-wire.log').write_text(result.stdout+result.stderr)
print(result.stdout+result.stderr)
if result.returncode: raise RuntimeError('Java-to-DLL pose bridge test failed')
