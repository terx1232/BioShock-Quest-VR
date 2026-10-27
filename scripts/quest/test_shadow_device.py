"""Check the production shadow shader on Quest's offscreen GPU, without game input."""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/tests/quest-shadows';OUT.mkdir(parents=True,exist_ok=True)
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--serial',required=True)
args=parser.parse_args()
spec=importlib.util.spec_from_file_location('builder',ROOT/'scripts/quest/builder.py')
b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)
sdk=b.find_sdk();sdk_root=sdk['build_tools'].parents[1]
compiler=sorted(sdk_root.glob('ndk/*/toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe'))[-1]
binary=OUT/'quest-shadowcheck'
subprocess.run([str(compiler),'--target=aarch64-linux-android26','-std=c++17','-O2','-static-libstdc++',
    str(ROOT/'source/native/tests/shadow_quality_gpu.cpp'),'-o',str(binary),'-lEGL','-lGLESv3'],check=True)
adb=[str(sdk['adb']),'-s',args.serial]
remote='/data/local/tmp/bioshock-shadow-quality-test'
subprocess.run([*adb,'push',str(binary),remote],check=True,capture_output=True)
try:
    subprocess.run([*adb,'shell','chmod','700',remote],check=True,capture_output=True)
    r=subprocess.run([*adb,'shell',remote],capture_output=True,text=True,timeout=45)
    (OUT/'device-last.log').write_text(r.stdout+r.stderr)
    (OUT/'device-last.json').write_text(json.dumps({'serial':args.serial,'exitCode':r.returncode,'offscreen':True,'gameInput':False},indent=2)+'\n')
    print(r.stdout+r.stderr,end='')
    if r.returncode or 'PASS: shadow tonal precision' not in r.stdout:raise RuntimeError('Quest shadow GPU check failed; see device-last.log')
finally:
    subprocess.run([*adb,'shell','rm','-f',remote],check=False,capture_output=True)
