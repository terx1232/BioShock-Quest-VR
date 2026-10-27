"""Capture a bounded headset measurement without restarting or changing settings."""
import argparse
import json
import re
import subprocess
import time
from datetime import datetime
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds',type=int,default=20)
    parser.add_argument('--tag',default='menu')
    parser.add_argument('--serial',default=None)
    parser.add_argument('--adb',default='D:/Android/Sdk/platform-tools/adb.exe')
    args=parser.parse_args()
    adb=[args.adb,'-s',args.serial]
    def call(*parts):
        return subprocess.run([*adb,*parts],check=True,capture_output=True,timeout=30).stdout
    destination=ROOT/'build'/'measurements'/datetime.now().strftime('%Y%m%d-%H%M%S')
    destination.mkdir(parents=True,exist_ok=True)
    try:
        first=call('exec-out','run-as','com.winlator.cmod','cat','files/game/Builds/Release/BioshockVR.log')
    except (subprocess.CalledProcessError,subprocess.TimeoutExpired) as error:
        result={'tag':args.tag,'status':'device-unavailable','samples':[],
                'directory':str(destination),'error':str(error)}
        (destination/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps(result,indent=2),flush=True)
        return
    (destination/'before.log').write_bytes(first)
    native_process=None
    native_file=None
    power=[]
    def record_power():
        try:
            state=call('shell','dumpsys','power').decode(errors='replace')
            match=re.search(r'mWakefulness=(\w+)',state)
            power.append({'time':datetime.now().isoformat(timespec='seconds'),
                          'wakefulness':match.group(1) if match else 'unknown'})
        except (subprocess.CalledProcessError,subprocess.TimeoutExpired):
            power.append({'time':datetime.now().isoformat(timespec='seconds'),'wakefulness':'unavailable'})
    record_power()
    try:
        pids=call('shell','pidof','com.winlator.cmod:vr_process').decode().split()
        if pids:
            # Live filtered capture survives the huge Wine/AVC log volume and
            # excludes a previous run's (or sleeping headset's) cached FPS.
            native_file=(destination/'headset.log').open('wb')
            native_process=subprocess.Popen([*adb,'logcat','-T','1','--pid',pids[0],'-s',
                'VrApi:I','BioshockInit:I','BioshockCinema:I','BioshockPerf:I','BioshockPointer:I','BioshockFrameSync:I',
                'BioshockTextureSync:I','BioshockPose:E','BioshockWorldPose:I','*:S'],stdout=native_file,stderr=subprocess.DEVNULL)
    except (subprocess.CalledProcessError,subprocess.TimeoutExpired):
        pass
    final=first
    interrupted=None
    duration=max(1,min(45,args.seconds))
    until=time.monotonic()+duration
    # Save intermediate reads too: unplugging USB must not discard the
    # measurements collected while the headset was worn.
    while time.monotonic()<until:
        time.sleep(min(5,until-time.monotonic()))
        try:
            final=call('exec-out','run-as','com.winlator.cmod','cat','files/game/Builds/Release/BioshockVR.log')
        except (subprocess.CalledProcessError,subprocess.TimeoutExpired) as error:
            interrupted=str(error)
            break
        (destination/'game.log').write_bytes(final)
        record_power()
    (destination/'game.log').write_bytes(final)
    fresh=final[len(first):] if final.startswith(first) else final
    (destination/'interval.log').write_bytes(fresh)
    if native_process:
        native_process.terminate()
        native_process.wait(timeout=10)
    if native_file: native_file.close()
    native_path=destination/'headset.log'
    native=native_path.read_text(errors='replace') if native_path.exists() else ''
    headset=[{'fps':int(fps),'refresh':int(refresh)} for fps,refresh in re.findall(r'VrApi\s*:\s*FPS=(\d+)/(\d+)',native)]
    samples=[{'fps':float(fps),'mode':mode,'worstMs':float(worst)}
             for fps,mode,worst in re.findall(rb'FPS: ([0-9.]+) \((menu|game)\), worst frame ([0-9.]+) ms',fresh)]
    for sample in samples: sample['mode']=sample['mode'].decode()
    timings=[dict(zip(('engineMs','driverPresentMs','limiterMs','inputMs'),map(float,values)))
             for values in re.findall(rb'CPU frame: engine interval ([0-9.]+) ms, driver Present ([0-9.]+) ms, limiter ([0-9.]+) ms, mode/input ([0-9.]+) ms',fresh)]
    gpu=[{'mode':mode.decode(),'engineMs':float(engine),'modMs':float(mod),'samples':int(count)}
         for mode,engine,mod,count in re.findall(rb'GPU time \((game|menu)\): engine ([0-9.]+) ms, mod passes ([0-9.]+) ms per frame \((\d+) samples\)',fresh)]
    result={'tag':args.tag,'seconds':duration,'samples':samples,'cpuTimings':timings,
            'gpuTimings':gpu,
            'headsetSamples':headset,'power':power,
            'status':'interrupted' if interrupted else 'captured',
            'directory':str(destination)}
    if interrupted: result['error']=interrupted
    (destination/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2),flush=True)

if __name__=='__main__':main()
