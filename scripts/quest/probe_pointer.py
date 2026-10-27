"""Save two hover-only game frames and always release the debug pointer."""
import argparse
import subprocess
import time
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
GAME='files/game/Builds/Release'
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--serial',default=None)
parser.add_argument('--point',action='append',help='hover point: label,u,v; repeat for multiple points')
parser.add_argument('--output',type=Path)
args=parser.parse_args()
adb=['D:/Android/Sdk/platform-tools/adb.exe','-s',args.serial]
out=args.output or ROOT/'build/tests/quest-pointer/device'
out.mkdir(parents=True,exist_ok=True)
points=[('upper-left',.3,.4),('lower-right',.7,.6)]
if args.point:
    points=[]
    for value in args.point:
        label,u,v=value.split(',');u=float(u);v=float(v)
        if not label.replace('-','').replace('_','').isalnum() or not(0<=u<=1 and 0<=v<=1):
            parser.error('point must be a simple label and coordinates between 0 and 1')
        points.append((label,u,v))
def call(*parts,check=True):
    return subprocess.run([*adb,*map(str,parts)],check=check,capture_output=True,timeout=30)
def command(text):
    source=out/'vrcmd.txt';source.write_text(text,encoding='ascii')
    remote='/data/local/tmp/bioshock-port-update/vrcmd.txt'
    call('push',source,remote)
    call('shell','chmod','644',remote)
    call('shell','run-as','com.winlator.cmod','cp',remote,GAME+'/vrcmd.txt')
settings=call('exec-out','run-as','com.winlator.cmod','cat',GAME+'/BioshockVR.ini').stdout.decode(errors='replace')
import re
if not re.search(r'(?mi)^DevHarness\s*=\s*1\s*$',settings):
    raise SystemExit('Scripted input is disabled in this production profile. No pointer commands sent.')
try:
    for label,u,v in points:
        call('shell','input','keyevent','224')
        call('shell','run-as','com.winlator.cmod','rm','-f',GAME+'/vrshot.bmp')
        command(f'point {u} {v}\nwait 90\nshot\n')
        until=time.monotonic()+18
        wake_at=time.monotonic()+8
        while time.monotonic()<until:
            time.sleep(.5)
            if time.monotonic()>=wake_at:
                call('shell','input','keyevent','224');wake_at=time.monotonic()+8
            result=call('exec-out','run-as','com.winlator.cmod','cat',GAME+'/vrshot.bmp',check=False)
            if result.returncode==0 and result.stdout.startswith(b'BM'):
                path=out/(label+'.bmp');path.write_bytes(result.stdout)
                print(f'{label}: {path} ({len(result.stdout)} bytes)',flush=True)
                break
        else: raise RuntimeError('No fresh game frame; wake Quest and keep the menu open')
finally:
    command('point off\n')
