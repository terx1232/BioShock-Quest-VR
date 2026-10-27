"""Read-only memory/process capture while the user loads a level on Quest."""
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
    parser.add_argument('--serial',default=None)
    parser.add_argument('--seconds',type=int,default=90)
    parser.add_argument('--wait-for-load',type=int,default=0,help='capture during loading, then start the bounded interval when gameplay is reached')
    args=parser.parse_args()
    adb=['D:/Android/Sdk/platform-tools/adb.exe','-s',args.serial]
    destination=ROOT/'build/diagnostics'/datetime.now().strftime('%Y%m%d-%H%M%S-memory')
    destination.mkdir(parents=True,exist_ok=True)
    def call(*parts):
        try:result=subprocess.run([*adb,*map(str,parts)],capture_output=True,timeout=10)
        except subprocess.TimeoutExpired:return -1,'ADB command timed out'
        return result.returncode,result.stdout.decode(errors='replace')
    def kb(text,key):
        match=re.search(r'^'+key+r':\s+(\d+) kB',text,re.M)
        return int(match.group(1)) if match else None
    duration=min(180,max(1,args.seconds))
    waiting=max(0,min(600,args.wait_for_load))
    until=time.monotonic()+(waiting if waiting else duration)
    print('Read-only memory capture:',destination,flush=True)
    if waiting:print('Waiting for gameplay; memory is captured during the wait, then the post-load timer starts.',flush=True)
    live_file=(destination/'headset.log').open('wb')
    live=subprocess.Popen([*adb,'logcat','-T','1','-v','threadtime','-b','main','-b','system','-b','events',
                           'VrApi:I','System.out:I','Process:I','BioshockPerf:I','ActivityManager:I',
                           'BioshockCinema:I','BioshockFrameSync:I','BioshockInit:I','BioshockWorldPose:I',
                           'libc:F','DEBUG:F','AndroidRuntime:E','lmkd:I','lowmemorykiller:I','*:S'],
                           stdout=live_file,stderr=subprocess.DEVNULL)
    game_log='files/game/Builds/Release/BioshockVR.log'
    previous_log='';generation=0;prior_pids=None;ending=None
    last_thermal=0;thermal=None;power='unknown';reason='completed'
    with (destination/'samples.jsonl').open('w') as target:
        while time.monotonic()<until:
            code,processes=call('shell','ps','-A','-o','PID,ARGS')
            if code:
                reason='adb-unavailable';print('ADB unavailable; collected samples retained.',flush=True);break
            matches=re.findall(r'^\s*(\d+)\s+Bioshock\.exe(?:\s|$)',processes,re.M|re.I)
            pids={int(value) for value in matches}
            if prior_pids and pids!=prior_pids and ending is None:
                reason='game-process-changed';ending=time.monotonic()+5
                until=min(until,ending)
                print('Game process disappeared/changed; preserving the final logs.',flush=True)
            if pids:prior_pids=pids
            log_code,data=call('exec-out','run-as','com.winlator.cmod','cat',game_log)
            if log_code==0:
                if previous_log and not data.startswith(previous_log):generation+=1
                if data!=previous_log:(destination/f'game-generation-{generation}.log').write_text(data)
                previous_log=data
                # A startup/pause UI can close before Continue. Keep capturing
                # memory throughout the wait and start the bounded post-load
                # interval only when the latest mode is actually gameplay.
                modes=re.findall(r'Mode: (game \(VR view\)|menu \(theater\))',data)
                if waiting and ending is None and modes and modes[-1]=='game (VR view)':
                    waiting=0;until=time.monotonic()+duration
                    print(f'Game view started; capturing the next {duration} seconds.',flush=True)
            if time.monotonic()-last_thermal>=10:
                _,temperatures=call('shell','dumpsys','thermalservice')
                (destination/f'thermal-{int(time.monotonic())}.log').write_text(temperatures)
                current=temperatures.split('Current temperatures from HAL:',1)[-1].split('Current cooling devices',1)[0]
                status=re.search(r'Thermal Status:\s*(\d+)',temperatures)
                thermal={'status':int(status[1]) if status else None,
                         'sensors':{name:{'celsius':float(value),'status':int(state)}
                                    for value,name,state in re.findall(r'Temperature\{mValue=([0-9.]+), mType=\d+, mName=([^,]+), mStatus=(\d+)',current)}}
                _,state=call('shell','dumpsys','power');match=re.search(r'mWakefulness=(\w+)',state)
                power=match[1] if match else 'unknown';last_thermal=time.monotonic()
            _,memory=call('exec-out','cat','/proc/meminfo')
            # Preserve the breakdown: Vulkan/driver allocations need not
            # appear in the game's VmRSS when system memory collapses.
            with (destination/'system-memory.jsonl').open('a') as system_memory:
                system_memory.write(json.dumps({
                    'time':datetime.now().isoformat(timespec='seconds'),
                    'kb':{key:int(value) for key,value in re.findall(r'^(\w+):\s+(\d+) kB',memory,re.M)}})+'\n')
            row={'time':datetime.now().isoformat(timespec='seconds'),
                 'availableKb':kb(memory,'MemAvailable'),'swapFreeKb':kb(memory,'SwapFree'),
                 'games':[],'power':power,'thermal':thermal,'waitingForLoad':bool(waiting),'logGeneration':generation}
            for value in matches:
                pid=int(value)
                code,status=call('exec-out','run-as','com.winlator.cmod','cat',f'/proc/{pid}/status')
                _,adj=call('exec-out','run-as','com.winlator.cmod','cat',f'/proc/{pid}/oom_score_adj')
                _,group=call('exec-out','run-as','com.winlator.cmod','cat',f'/proc/{pid}/cgroup')
                row['games'].append({'pid':pid,'readable':code==0,'rssKb':kb(status,'VmRSS'),
                                     'peakRssKb':kb(status,'VmHWM'),'swapKb':kb(status,'VmSwap'),
                                     'oomAdj':int(adj.strip()) if adj.strip().lstrip('-').isdigit() else None,
                                     'cgroup':group.strip()})
            target.write(json.dumps(row)+'\n');target.flush()
            time.sleep(min(2,max(0,until-time.monotonic())))
    live.terminate()
    try:live.wait(timeout=5)
    except subprocess.TimeoutExpired:live.kill();live.wait(timeout=5)
    live_file.close()
    for name,parts in [('game',['exec-out','run-as','com.winlator.cmod','cat','files/game/Builds/Release/BioshockVR.log']),
                       ('previous-game',['exec-out','run-as','com.winlator.cmod','cat','files/game/Builds/Release/BioshockVR.prev.log']),
                       ('exit-info',['shell','dumpsys','activity','exit-info','com.winlator.cmod']),
                       ('kernel',['logcat','-b','kernel','-d','-v','threadtime','-t','30000'])]:
        _,data=call(*parts);(destination/(name+'.log')).write_text(data)
    _,files=call('shell','ls','-t','/sdcard/Download/Winlator/logs')
    for index,name in enumerate(files.splitlines()[:3]):
        if not re.fullmatch(r'[A-Za-z0-9_.-]+\.txt',name):continue
        _,data=call('exec-out','cat','/sdcard/Download/Winlator/logs/'+name)
        (destination/f'wine-{index}-{name}').write_text(data)
    for name in ('Bioshock_d3d9.log','Bioshock.exe_d3d9.log'):
        code,data=call('exec-out','run-as','com.winlator.cmod','cat','files/game/Builds/Release/'+name)
        if not code:(destination/name).write_text(data)
    rows=[json.loads(line) for line in (destination/'samples.jsonl').read_text().splitlines()]
    games=[game for row in rows for game in row['games'] if game['rssKb'] is not None]
    summary={'directory':str(destination),'samples':len(rows),'reason':reason,
             'timedOutWaitingForLoad':bool(waiting),'logGenerations':generation+1,
             'maxThermalStatus':max((row['thermal']['status'] for row in rows if row.get('thermal') and row['thermal']['status'] is not None),default=None),
             'pids':sorted({game['pid'] for game in games}),
             'maxGameRssKb':max((game['rssKb'] for game in games),default=None),
             'minAvailableKb':min((row['availableKb'] for row in rows if row['availableKb'] is not None),default=None)}
    (destination/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps(summary,indent=2),flush=True)

if __name__=='__main__':main()
