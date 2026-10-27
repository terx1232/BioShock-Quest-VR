"""Retained image poses through ring reuse, independent of the game/headset."""
import importlib.util
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/tests/quest-worldpose';OUT.mkdir(parents=True,exist_ok=True)
compiler=next((ROOT/'dependencies/build').glob('llvm-mingw*/bin/i686-w64-mingw32-clang++.exe'))
exe=OUT/'worldpose.exe'
subprocess.run([str(compiler),'-std=c++17','-O2','-static',str(ROOT/'source/native/tests/worldpose_fixture.cpp'),'-o',str(exe)],check=True)
result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=15)
(OUT/'result.log').write_text(result.stdout+result.stderr)
print(result.stdout+result.stderr,end='')
if result.returncode:raise RuntimeError('Image pose retention regression failed')
spec=importlib.util.spec_from_file_location('quest_builder',ROOT/'scripts/quest/builder.py')
builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)
print('PASS: packaged renderer ABI:',builder.verify_worldpose_renderer())
