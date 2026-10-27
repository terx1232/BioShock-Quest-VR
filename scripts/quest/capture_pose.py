"""Bounded pose/image correspondence trace; never sends gameplay input."""
import argparse
import csv
import io
import json
import subprocess
import time
from datetime import datetime
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
PACKAGE='com.winlator.cmod'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial',default=None)
    parser.add_argument('--seconds',type=int,default=120)
    parser.add_argument('--wait-for-level',type=int,default=90)
    args=parser.parse_args()
    adb=['D:/Android/Sdk/platform-tools/adb.exe','-s',args.serial]
    out=ROOT/'build/diagnostics'/datetime.now().strftime('%Y%m%d-%H%M%S-pose')
    out.mkdir(parents=True,exist_ok=True)
    def call(*parts):
        return subprocess.run([*adb,*parts],capture_output=True,timeout=15)
    def read(path):
        r=call('exec-out','run-as',PACKAGE,'cat',path)
        return r.stdout if r.returncode==0 else None
    r=call('shell','run-as',PACKAGE,'touch','files/world_pose_trace.once')
    if r.returncode:raise RuntimeError(r.stderr.decode(errors='replace'))
    stream=(out/'headset.log').open('wb')
    live=subprocess.Popen([*adb,'logcat','-T','1','-v','threadtime','-s',
        'VrApi:I','BioshockInit:I','BioshockCinema:I','BioshockWorldPose:I',
        'BioshockFrameSync:I','BioshockPerf:I','System.out:I','AndroidRuntime:E','libc:F','*:S'],
        stdout=stream,stderr=subprocess.DEVNULL)
    print('Read-only pose trace armed:',out,flush=True)
    started=None;deadline=time.monotonic()+max(1,min(300,args.wait_for_level))
    power=[];data=b'';ending='no-exact-game-frame'
    try:
        while time.monotonic()<deadline:
            marker=call('shell','run-as',PACKAGE,'test','-f','files/world_pose_trace.once')
            if marker.returncode==1:
                current=read('files/world-pose-trace.csv')
                if current:
                    data=current;(out/'world-pose-trace.csv').write_bytes(data)
                    if started is None:
                        started=time.monotonic();deadline=started+max(1,min(120,args.seconds))
                        print('Exact gameplay pose samples started; recording sleep/wake without input.',flush=True)
            game=read('files/game/Builds/Release/BioshockVR.log')
            if game:(out/'game.log').write_bytes(game)
            state=call('shell','dumpsys','power').stdout.decode(errors='replace')
            wake=next((line.strip().split('=',1)[1] for line in state.splitlines() if 'mWakefulness=' in line),'unknown')
            power.append({'time':datetime.now().isoformat(timespec='seconds'),'state':wake})
            time.sleep(min(5,max(0,deadline-time.monotonic())))
        ending='captured' if data else ending
    finally:
        live.terminate()
        try:live.wait(timeout=5)
        except subprocess.TimeoutExpired:live.kill();live.wait(timeout=5)
        stream.close()
        complete=data.decode(errors='replace').rsplit('\n',1)[0]+'\n' if data else ''
        rows=[row for row in csv.DictReader(io.StringIO(complete)) if all(row.get(key) is not None for key in ('status','epoch','source_id','age_ms'))] if complete else []
        valid=[row for row in rows if row.get('status') in ('1','2') and row.get('source_id')!='0']
        summary={'status':ending,'samples':len(rows),'validSamples':len(valid),
            'epochs':sorted({row['epoch'] for row in rows}),
            'rejectedOldSpace':sum(row.get('status')=='-2' for row in rows),
            'expired':sum(row.get('status')=='-3' for row in rows),
            'pendingPacket':sum(row.get('status')=='0' for row in rows),
            'maxImageAgeMs':max((float(row['age_ms']) for row in valid),default=None),
            'power':power,'directory':str(out)}
        (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
        print(json.dumps(summary,indent=2),flush=True)

if __name__=='__main__':main()
