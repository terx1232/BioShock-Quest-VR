"""One read-only GPU snapshot of the real HUD; never sends gameplay input."""
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import time

ROOT=Path(__file__).resolve().parents[2]
GAME='files/game/Builds/Release/'
PACKAGE='com.winlator.cmod'

def inspect(data):
    if data[:2]!=b'BM':raise ValueError('GPU capture is not a BMP')
    offset=struct.unpack_from('<I',data,10)[0]
    width,height=struct.unpack_from('<ii',data,18)
    if width<1 or height>=0 or struct.unpack_from('<H',data,28)[0]!=32:
        raise ValueError('Expected top-down 32-bit GPU capture')
    height=-height
    if len(data)!=offset+width*height*4:raise ValueError('Incomplete GPU capture')
    rgb=alpha=0;left=width;top=height;right=bottom=-1
    for y in range(height):
        for x in range(width):
            b,g,r,a=data[offset+4*(y*width+x):offset+4*(y*width+x+1)]
            if a:alpha+=1
            if r or g or b:
                rgb+=1;left=min(left,x);top=min(top,y);right=max(right,x);bottom=max(bottom,y)
    return dict(width=width,height=height,rgbPixels=rgb,alphaPixels=alpha,
                rgbBounds=[left,top,right,bottom] if rgb else None)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial',default=None)
    parser.add_argument('--wait-seconds',type=int,default=180)
    args=parser.parse_args()
    adb=['D:/Android/Sdk/platform-tools/adb.exe','-s',args.serial]
    def run(*parts):
        return subprocess.run([*adb,*parts],capture_output=True,timeout=15)
    def read(name):
        result=run('exec-out','run-as',PACKAGE,'cat',GAME+name)
        if result.returncode:raise RuntimeError(result.stderr.decode(errors='replace'))
        return result.stdout
    destination=ROOT/'build/diagnostics'/datetime.now().strftime('%Y%m%d-%H%M%S-hud')
    destination.mkdir(parents=True,exist_ok=True)
    baseline=read('BioshockVR.log').decode(errors='replace')
    baseline_count=baseline.count('HUD capture: hud_scene.bmp')
    result=run('shell','run-as',PACKAGE,'touch',GAME+'hud_capture.once')
    if result.returncode:raise RuntimeError(result.stderr.decode(errors='replace'))
    print(f'One-shot HUD capture armed: {destination}. Waiting for the user to open a level.',flush=True)
    deadline=time.monotonic()+min(600,max(1,args.wait_seconds))
    while time.monotonic()<deadline:
        current=read('BioshockVR.log').decode(errors='replace')
        if not current.startswith(baseline):baseline_count=0
        if current.count('HUD capture: hud_scene.bmp')>baseline_count:
            (destination/'game.log').write_text(current)
            summary={}
            for name in ('hud_capture.bmp','hud_scene.bmp'):
                data=read(name);(destination/name).write_bytes(data)
                summary[name]=inspect(data)
            for name in ('d3d9.dll','BioshockVR.ini'):
                data=read(name);summary[name]={'sha256':hashlib.sha256(data).hexdigest()}
                if name.endswith('.ini'):(destination/name).write_bytes(data)
            summary['activation']=[line for line in current.splitlines() if re.search(r'Gameplay HUD:|HUD: UI viewport|HUD capture:|Launch:',line)]
            (destination/'report.json').write_text(json.dumps(summary,indent=2)+'\n')
            print(json.dumps(summary,indent=2),flush=True);return
        time.sleep(2)
    print('No active gameplay capture yet. Marker remains armed; no input was sent.',flush=True)
    (destination/'status.json').write_text(json.dumps({'status':'waiting-for-gameplay','markerArmed':True})+'\n')

if __name__=='__main__':main()
