"""Bounded GPU/CPU metadata observation in the debug app; does not alter input.

Readbacks briefly disturb frame timing, so do not use this interval as an FPS
measurement. The agent stops after eight samples/10 s, or one image/60 s.
"""
import argparse
import subprocess
import time
from datetime import datetime
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial',default=None)
    parser.add_argument('--build-only',action='store_true')
    parser.add_argument('--capture-menu',action='store_true',help='one read-only full menu image and panel layout')
    parser.add_argument('--capture-world',action='store_true',help='one read-only full stereo world image')
    parser.add_argument('--before-draw',action='store_true',help='observe CPU/GPU metadata immediately before the window draw; wait up to 60 s')
    args=parser.parse_args()
    adb=['D:/Android/Sdk/platform-tools/adb.exe','-s',args.serial]
    def call(*parts):
        return subprocess.run([*adb,*map(str,parts)],check=True,capture_output=True,timeout=30).stdout
    out=ROOT/'build/tests/quest-sync';out.mkdir(parents=True,exist_ok=True)
    include=out/'include';include.mkdir(exist_ok=True)
    # Resolve jni.h from the Android sysroot, not the Windows JDK next to this
    # platform-independent JVMTI declaration.
    jdk=Path('C:/Program Files/Eclipse Adoptium/jdk-21.0.9.10-hotspot')
    (include/'jvmti.h').write_bytes((jdk/'include/jvmti.h').read_bytes())
    compiler=Path('D:/Android/Sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe')
    library=out/'libsyncprobe.so'
    subprocess.run([str(compiler),'--target=aarch64-linux-android28','-shared','-fPIC','-O2',
                    '-std=c++17','-static-libstdc++','-I',str(include),str(ROOT/'source/android/native/sync_probe.cpp'),
                    '-o',str(library),'-lGLESv3','-llog','-ldl'],check=True)
    if args.build_only:
        print('Built:',library);return
    pids=call('shell','pidof','com.winlator.cmod:vr_process').decode().split()
    if len(pids)!=1:raise RuntimeError('Expected one VR process')
    capture=args.capture_menu or args.capture_world
    nonce=('early:' if args.before_draw else 'capture:world:' if args.capture_world else 'capture:' if args.capture_menu else '')+str(time.time_ns())
    target='/data/user/0/com.winlator.cmod/code_cache/bioshock-perf/libsyncprobe-'+nonce.replace(':','-')+'.so'
    call('push',library,'/data/local/tmp/bioshock-sync-probe.so')
    call('shell','chmod','644','/data/local/tmp/bioshock-sync-probe.so')
    call('shell','run-as','com.winlator.cmod','mkdir','-p','code_cache/bioshock-perf')
    call('shell','run-as','com.winlator.cmod','cp','/data/local/tmp/bioshock-sync-probe.so',target)
    call('shell','run-as','com.winlator.cmod','chmod','500',target)
    call('shell','am','attach-agent',pids[0],target+'='+nonce)
    report=''
    for _ in range(124 if capture or args.before_draw else 24):
        report=call('exec-out','run-as','com.winlator.cmod','cat','files/frame-sync-probe.log').decode(errors='replace')
        if report.startswith('options='+nonce+'\n') and ('stopped ' in report or 'ERROR' in report):break
        time.sleep(.5)
    destination=out/(datetime.now().strftime('%Y%m%d-%H%M%S')+'.log')
    destination.write_text(report)
    if capture and 'saved menu-source-probe.bmp' in report:
        data=call('exec-out','run-as','com.winlator.cmod','cat','files/menu-source-probe.bmp')
        destination.with_suffix('.bmp').write_bytes(data)
    print(report,end='');print('Saved:',destination)
    if 'ERROR' in report:raise RuntimeError('Probe failed; see saved report')

if __name__=='__main__':main()
