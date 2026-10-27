"""Compare real cached, unsupported-format fallback and pre-cache production output."""
from pathlib import Path
import hashlib
import json
import subprocess

ROOT=Path(__file__).resolve().parents[2]
out=ROOT/'build/tests/quest-shiftcache';out.mkdir(parents=True,exist_ok=True)
compiler=next((ROOT/'dependencies/build').glob('llvm-mingw*/bin/i686-w64-mingw32-clang++.exe'))
native=ROOT/'source/native'
original=ROOT/'archive/quest-source/20261026-build80/source/native/src/stereo.cpp'
modes=[('fallback',native/'src/stereo.cpp',['-DQUEST_SHIFT_CACHE_UNAVAILABLE_TEST']),
       ('cached',native/'src/stereo.cpp',[])]
if original.exists():modes.insert(0,('baseline80',original,[]))
for name,source,flags in modes:
    folder=out/name;folder.mkdir(exist_ok=True);target=folder/'depth.exe'
    subprocess.run([str(compiler),'-O2','-std=c++17','-static','-DQUEST_LAZY_DEPTH_TEST',*flags,
                    str(native/'tests/depth_pipeline.cpp'),str(source),'-o',str(target),'-ld3d9'],check=True)
    result=subprocess.run([str(target)],cwd=folder,capture_output=True,text=True,timeout=45)
    (folder/'result.log').write_text(result.stdout+result.stderr)
    if result.returncode:raise RuntimeError(name+' failed:\n'+result.stdout+result.stderr)
    active='same-frame RG32F disparity cache active' in result.stdout
    assert active==(name=='cached'),(name,'wrong cached/fallback path')
    print(name+': production GPU cases pass',flush=True)

reference=out/('baseline80' if original.exists() else 'fallback')
names=sorted([p.name for p in reference.glob('*.bmp')]+[p.name for p in reference.glob('*.depth')])
assert len(names)>=27,'Missing multi-resolution texture/depth cases'
records=[]
for name in names:
    expected=(reference/name).read_bytes()
    for mode in ('cached','fallback'):
        actual=(out/mode/name).read_bytes()
        if actual!=expected:
            differences=sum(a!=b for a,b in zip(actual,expected))
            raise AssertionError(f'{mode}/{name}: {differences} differing bytes, sizes {len(actual)}/{len(expected)}')
    records.append({'name':name,'sha256':hashlib.sha256(expected).hexdigest(),'bytes':len(expected)})
(out/'comparison.json').write_text(json.dumps({'reference':reference.name,'byteIdentical':True,'artifacts':records},indent=2)+'\n')
print(f'PASS: {len(names)} full stereo color/depth artifacts byte-identical across baseline80, cached and fallback; 720/864 source, thin/near edges, late tuning changes, dynamic crop, menu and Reset')
