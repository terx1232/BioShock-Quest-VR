"""Original skin topology and production wrist anchor, without game input."""
from pathlib import Path
import importlib.util
import subprocess
import struct
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/tests/quest-plasmid';OUT.mkdir(parents=True,exist_ok=True)
spec=importlib.util.spec_from_file_location('quest_hand_asset',ROOT/'scripts/quest/handmask.py')
asset=importlib.util.module_from_spec(spec);spec.loader.exec_module(asset)
_,chunks=asset.read(asset.ensure_psk())
size,count,faces=chunks['FACE0000'];indices=[]
for i in range(count):indices.extend(struct.unpack_from('<HHH',faces,i*size))
path=OUT/'native-indices.bin';path.write_bytes(struct.pack('<%dH'%len(indices),*indices))
clang=next((ROOT/'dependencies/build').glob('llvm-mingw*/bin/i686-w64-mingw32-clang++.exe'))
logs=[]
for name,args in [('plasmid_pose',[]),('handmask_fixture',[str(path)])]:
 exe=OUT/(name+'.exe')
 subprocess.run([str(clang),'-O2','-std=c++17','-static',str(ROOT/'source/native/tests'/(name+'.cpp')),'-o',str(exe)],check=True)
 result=subprocess.run([str(exe),*args],capture_output=True,text=True)
 print(result.stdout+result.stderr,end='');logs.append(result.stdout+result.stderr)
 if result.returncode:raise RuntimeError(name+' failed')
(OUT/'result.log').write_text(''.join(logs))
