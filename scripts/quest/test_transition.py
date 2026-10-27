"""Replay loading/pause timing against the production mode gate."""
import subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
target=ROOT/'build/tests/quest-transition'
target.mkdir(parents=True,exist_ok=True)
compiler=ROOT/'dependencies/build/llvm-mingw-20261006-ucrt-x86_64/bin/i686-w64-mingw32-clang++.exe'
exe=target/'modegate.exe'
subprocess.run([str(compiler),'-std=c++17','-O2','-static','-include','initializer_list',
                '-I',str(ROOT/'source/native/src'),str(ROOT/'source/native/tests/modegate_fixture.cpp'),'-o',str(exe)],check=True)
result=subprocess.run([str(exe)],check=True,capture_output=True,text=True)
(target/'result.log').write_text(result.stdout,encoding='utf-8')
print(result.stdout,end='')
