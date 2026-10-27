"""Inspect/apply/restore affinity inside our debug app, without restarting the game."""
import argparse
import re
import subprocess
import time
from datetime import datetime
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command',choices=('inspect','apply','restore','top-app','background','top-threads','bg-threads','xr-threads','driver-xr','legacy-xr','refresh-xr','inspect-ui'),nargs='?',default='inspect')
    parser.add_argument('--serial',default=None)
    parser.add_argument('--pid',type=int)
    parser.add_argument('--mask',default='f0')
    args=parser.parse_args()
    if not re.fullmatch('[0-9a-fA-F]{1,2}',args.mask) or int(args.mask,16)==0:parser.error('mask must be 01..ff')
    adb=['D:/Android/Sdk/platform-tools/adb.exe','-s',args.serial]
    def call(*parts):return subprocess.run([*adb,*map(str,parts)],check=True,capture_output=True,timeout=30).stdout
    pid=args.pid
    if not pid:
        processes=call('shell','ps','-A','-o','PID,ARGS').decode()
        matches=re.findall(r'^\s*(\d+)\s+Bioshock\.exe(?:\s|$)',processes,re.M|re.I)
        if len(matches)!=1:raise RuntimeError('Expected one running BioShock process')
        pid=int(matches[0])
    vr=call('shell','pidof','com.winlator.cmod:vr_process').decode().split()
    if len(vr)!=1:raise RuntimeError('VR process not running')
    source=ROOT/'source/android/native/cpu_probe.cpp'
    out=ROOT/'build/tests/quest-cpu';out.mkdir(parents=True,exist_ok=True)
    library=out/'libbioshockcpuprobe.so'
    ndk=Path('D:/Android/Sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/windows-x86_64/bin')
    if not library.exists() or library.stat().st_mtime<source.stat().st_mtime:
        subprocess.run([str(ndk/'clang++.exe'),'--target=aarch64-linux-android28','-shared','-fPIC','-O2',
                        '-std=c++17','-static-libstdc++','-I',str(ROOT/'dependencies/build/openxr/include'),str(source),'-o',str(library),'-llog','-ldl'],check=True)
    nonce=str(time.time_ns())
    # Loaded agents can stay mapped. Never truncate an in-use shared object.
    target='/data/user/0/com.winlator.cmod/code_cache/bioshock-perf/libcpuprobe-'+nonce+'.so'
    call('push',library,'/data/local/tmp/bioshockcpuprobe.so')
    call('shell','chmod','644','/data/local/tmp/bioshockcpuprobe.so')
    call('shell','run-as','com.winlator.cmod','mkdir','-p','code_cache/bioshock-perf')
    call('shell','run-as','com.winlator.cmod','cp','/data/local/tmp/bioshockcpuprobe.so',target)
    call('shell','run-as','com.winlator.cmod','chmod','500',target)
    options=f'{args.command}:{pid}:{args.mask}:{nonce}'
    if args.command=='inspect-ui':
        game_log=call('exec-out','run-as','com.winlator.cmod','cat','files/game/Builds/Release/BioshockVR.log').decode(errors='replace')
        pointers=re.findall(r'Camera: validated viewport (?:0x)?([0-9a-fA-F]+)',game_log)
        if not pointers:raise RuntimeError('No validated viewport in this game launch')
        options=f'{args.command}:{pid}:{int(pointers[-1],16):x}:{nonce}'
    call('shell','am','attach-agent',vr[0],target+'='+options)
    report=''
    for _ in range(15):
        try:report=call('exec-out','run-as','com.winlator.cmod','cat','files/cpu-affinity/last.log').decode()
        except subprocess.CalledProcessError:pass
        if report.startswith('options='+options+'\n'):break
        time.sleep(.2)
    else:raise RuntimeError('CPU agent did not produce a fresh report')
    path=out/(datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+args.command+'.log')
    path.write_text(report)
    print(report,end='');print('Saved:',path)
    if 'ERROR' in report:raise RuntimeError('Probe reported an error; inspect report')
if __name__=='__main__':main()
