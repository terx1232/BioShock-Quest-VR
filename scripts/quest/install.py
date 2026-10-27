"""Install/update Quest, back up exact files, verify bytes, and watch startup."""
import argparse
import hashlib
import json
import subprocess
import sys
import time
import uuid
import configparser
import re
from datetime import datetime
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
PACKAGE='com.winlator.cmod'
INTERNAL='files/game/Builds/Release'
SHARED='/sdcard/BioshockMQ2/Builds/Release'

def require_quest2(call):
    model=call('shell','getprop','ro.product.model').stdout.decode().strip()
    device=call('shell','getprop','ro.product.device').stdout.decode().strip()
    if model.replace('_',' ').casefold()!='quest 2' or device!='hollywood':
        raise RuntimeError(f'This package targets Meta Quest 2; selected device is {model!r} ({device!r}). No files changed.')
    return {'model':model,'device':device}

def snapshot_file(read,manifest,backup,label,directory,name,private):
    result=read(directory,name,private,check=False)
    record={'directory':directory,'name':name,'private':private}
    if result.returncode:
        # A fresh Winlator installation has no game cache yet. Missing files
        # are created by this update; an unreadable existing file is not safe
        # to overwrite without a backup.
        if b'No such file or directory' not in result.stderr:
            raise RuntimeError(f'Cannot back up {directory}/{name}: '+result.stderr.decode(errors='replace'))
        manifest['created'].append(record)
        return
    relative=f'{label}/{name}'; path=backup/relative
    path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(result.stdout)
    manifest['files'].append({**record,'backup':relative,'sha256':hashlib.sha256(result.stdout).hexdigest()})

def verify_runtime_files(game,read):
    verified=[]
    for private,release,flash in [(True,INTERNAL,'files/game/Content/FlashMovies'),
                                  (False,SHARED,'/sdcard/BioshockMQ2/Content/FlashMovies')]:
        for name in ('d3d9.dll','BioshockVR.ini','HUDPC.swf','HUDRadial.swf'):
            movie=name.endswith('.swf');directory=flash if movie else release
            local=game.parents[1]/'Content/FlashMovies'/name if movie else game/name
            expected=local.read_bytes();actual=read(directory,name,private).stdout
            if actual!=expected:
                raise RuntimeError(f'Post-launch byte verification failed: {directory}/{name}')
            verified.append({'directory':directory,'name':name,'private':private,
                             'sha256':hashlib.sha256(actual).hexdigest()})
    return verified

def active_menu_samples(text):
    samples=re.findall(r'FPS: ([0-9.]+) \(menu\), worst frame ([0-9.]+) ms[^\r\n]*tracking (\d+)/(\d+), cinema (\d+)/(\d+)',text)
    return [float(value) if float(worst)<1000 and int(total)>0 and int(count)>0
            and int(tracked)>=int(total)*.95 and int(drawn)>=int(count)*.95 else None
            for value,worst,tracked,total,drawn,count in samples]

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--adb',default='D:/Android/Sdk/platform-tools/adb.exe')
    ap.add_argument('--serial')
    ap.add_argument('--rollback',type=Path)
    ap.add_argument('--files-only',action='store_true',help='update game files without reinstalling the APK')
    ap.add_argument('--wait-seconds',type=int,default=120)
    ap.add_argument('--settle-seconds',type=int,default=20,help='wait after cinema becomes active before accepting FPS')
    ap.add_argument('--min-menu-fps',type=float,default=60)
    fallback_options=ap.add_mutually_exclusive_group()
    fallback_options.add_argument('--fallback',type=Path,help='known working backup to restore if startup fails')
    fallback_options.add_argument('--fallback-current',action='store_true',help='on startup failure restore the installation captured before this update')
    args=ap.parse_args()
    adb=[args.adb]
    def call(*parts,check=True,timeout=300):
        r=subprocess.run([*adb,*map(str,parts)],capture_output=True,timeout=timeout)
        if check and r.returncode: raise RuntimeError(r.stderr.decode(errors='replace')+'\n'+r.stdout.decode(errors='replace'))
        return r
    devices=[line.split()[0] for line in call('devices').stdout.decode().splitlines()[1:] if '\tdevice' in line]
    serial=args.serial or (devices[0] if len(devices)==1 else None)
    if not serial or serial not in devices: raise RuntimeError('Connect one authorized Quest via USB (adb devices must show device).')
    adb += ['-s',serial]
    identity=require_quest2(call)
    installed=call('shell','pm','path',PACKAGE,check=False).stdout.decode().strip()
    if args.files_only and not installed.startswith('package:'):
        raise RuntimeError('Files-only update requires the Quest application to be installed first.')
    backup=args.rollback.resolve() if args.rollback else ROOT/'archive'/'quest-installs'/datetime.now().strftime('%Y%m%d-%H%M%S')
    if args.rollback:
        if not (backup/'manifest.json').is_file():raise RuntimeError('Rollback manifest does not exist: '+str(backup))
    else:
        backup.mkdir(parents=True,exist_ok=False)
    manifest={'serial':serial,'device':identity,'package':PACKAGE,'files':[],'created':[],'apk':False,'status':'preparing'}
    game=ROOT/'dist'/'quest'/'BioshockMQ2'/'Builds'/'Release'
    apk=ROOT/'dist'/'quest'/'BioShock_VR.apk'
    profile=configparser.ConfigParser()
    profile.read(game/'BioshockVR.ini',encoding='utf-8')
    require_precision=profile.getboolean('VR','RawPose',fallback=True)

    def read(directory,name,private,check=True):
        prefix=['exec-out','run-as',PACKAGE] if private else ['exec-out']
        return call(*prefix,'cat',f'{directory}/{name}',check=check)
    def transfer(directory,name,private,path):
        temporary='/data/local/tmp/bioshock-port-update/'+name
        call('shell','mkdir','-p','/data/local/tmp/bioshock-port-update')
        call('push',path,temporary)
        call('shell','chmod','644',temporary)
        prefix=['shell','run-as',PACKAGE] if private else ['shell']
        # App/shared data only, keep the original files in the local backup.
        call(*prefix,'mkdir','-p',directory)
        call(*prefix,'cp',temporary,f'{directory}/{name}')
        actual=read(directory,name,private).stdout
        if actual!=path.read_bytes(): raise RuntimeError(f'Byte verification failed: {directory}/{name}')

    def restore(saved):
        print('Restoring the saved Quest installation...',flush=True)
        call('shell','am','force-stop',PACKAGE,check=False)
        if saved.get('apk') and (backup/'previous.apk').exists(): call('install','-r',backup/'previous.apk',timeout=600)
        for record in saved['files']:
            transfer(record['directory'],record['name'],record['private'],backup/record['backup'])
        for record in saved.get('created',[]):
            prefix=['shell','run-as',PACKAGE] if record['private'] else ['shell']
            call(*prefix,'rm','-f',record['directory']+'/'+record['name'],check=False)
        call('shell','am','start','-n',PACKAGE+'/com.bioshockport.Launcher')

    gpu_bridge_ready=False
    def ensure_gpu_bridge():
        # Wine's Z: is imagefs. Link only the app-readable GPU counter, not
        # all of Android /sys, and never replace an existing unrelated file.
        nonlocal gpu_bridge_ready
        if gpu_bridge_ready:return
        directory='files/imagefs/usr/tmp';name='bioshock-gpu-busy'
        target='/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage'
        if call('shell','run-as',PACKAGE,'test','-d',directory,check=False).returncode:return
        current=call('shell','run-as',PACKAGE,'readlink',directory+'/'+name,check=False)
        if current.stdout.decode().strip()!=target:
            exists=call('shell','run-as',PACKAGE,'test','-e',directory+'/'+name,check=False).returncode==0
            link=call('shell','run-as',PACKAGE,'test','-L',directory+'/'+name,check=False).returncode==0
            if exists or link:
                print('GPU counter bridge skipped: existing path retained.',flush=True)
                gpu_bridge_ready=True;return
            result=call('shell','run-as',PACKAGE,'ln','-s',target,directory+'/'+name,check=False)
            if result.returncode:return
            manifest['created'].append({'directory':directory,'name':name,'private':True})
        value=read(directory,name,True,check=False).stdout.decode(errors='replace').strip()
        manifest['gpuLoadBridge']={'path':directory+'/'+name,'target':target,'sample':value}
        (backup/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
        gpu_bridge_ready=True
        print('GPU counter bridge ready:',value,flush=True)

    if args.rollback:
        restore(json.loads((backup/'manifest.json').read_text()))
        print('Rollback verified.'); return
    if not apk.is_file() or not (game/'d3d9.dll').is_file(): raise RuntimeError('Build the package before installing.')
    for local in (game/'BioshockVR.ini',game/'Default.ini',
                  game.parents[1]/'Content/FlashMovies/HUDPC.swf',
                  game.parents[1]/'Content/FlashMovies/HUDRadial.swf'):
        if not local.is_file():raise RuntimeError('Incomplete Quest package: '+str(local))
    if installed.startswith('package:'):
        print(f'Backing up the installed APK and game files to {backup}',flush=True)
        previous=installed.splitlines()[0].removeprefix('package:')
        if not args.files_only:
            call('pull',previous,backup/'previous.apk',timeout=600)
            manifest['apk']=True
        # A new launcher profile resets Wine's generated settings, so keep
        # those too. Saves are untouched by this installer and the launcher.
        for filename in ('Bioshock.ini','User.ini'):
            found=call('shell','run-as',PACKAGE,'find','files/imagefs/home','-iname',filename,check=False)
            for index,path in enumerate(found.stdout.decode().splitlines()):
                if not path.startswith('files/imagefs/home/') or not path.endswith('/'+filename): continue
                directory=path.rsplit('/',1)[0]
                result=read(directory,filename,True,check=False)
                if result.returncode: continue
                relative=f'wine-{index}/{filename}'
                destination=backup/relative; destination.parent.mkdir(exist_ok=True); destination.write_bytes(result.stdout)
                manifest['files'].append({'directory':directory,'name':filename,'private':True,'backup':relative,'sha256':hashlib.sha256(result.stdout).hexdigest()})
        (backup/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
        # Diagnostic overrides can change Winlator preferences. Restore those
        # bytes too on rollback, before launching the previous override.
        preference='com.winlator.cmod_preferences.xml'
        result=read('shared_prefs',preference,True,check=False)
        if result.returncode==0:
            relative='preferences/'+preference;destination=backup/relative
            destination.parent.mkdir(exist_ok=True);destination.write_bytes(result.stdout)
            manifest['files'].append({'directory':'shared_prefs','name':preference,'private':True,
                                     'backup':relative,'sha256':hashlib.sha256(result.stdout).hexdigest()})
    # Shared cache can outlive the APK. Back it up even on a new application
    # install; private paths only exist when the package is already present.
    names=['d3d9.dll','BioshockVR.ini','Default.ini','BioshockVR.launch',
           *[p.name for p in game.glob('SWING-LICENSE.txt')],
           *[p.name for p in game.glob('dxvk-port.conf')],*[p.name for p in game.glob('DefUser*.ini')]]
    locations=[('shared',SHARED,False),('shared-flash','/sdcard/BioshockMQ2/Content/FlashMovies',False),
               ('shared-root','/sdcard/BioshockMQ2',False)]
    if installed.startswith('package:'):
        locations += [('internal',INTERNAL,True),('internal-flash','files/game/Content/FlashMovies',True),
                      ('internal-root','files/game',True)]
    for label,directory,private in locations:
        selected=('port_override.json',) if label.endswith('-root') else (('HUDPC.swf','HUDRadial.swf') if label.endswith('-flash') else names)
        for name in selected:
            snapshot_file(read,manifest,backup,label,directory,name,private)
    (backup/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    call('shell','am','force-stop',PACKAGE,check=False)
    try:
        if not args.files_only:
            print('Installing the signed APK...',flush=True)
            result=call('install','-r',apk,timeout=600)
            print(result.stdout.decode(errors='replace'),flush=True)
        call('shell','appops','set',PACKAGE,'MANAGE_EXTERNAL_STORAGE','allow')
        existing=call('shell','test','-f',SHARED+'/Bioshock.exe',check=False).returncode==0
        if not existing:
            print('First install: transferring the complete game cache...',flush=True)
            call('push',game.parents[1],'/sdcard/',timeout=3600)
        else:
            names=['d3d9.dll','BioshockVR.ini','Default.ini',*[p.name for p in game.glob('SWING-LICENSE.txt')],*[p.name for p in game.glob('dxvk-port.conf')],*[p.name for p in game.glob('DefUser*.ini')]]
            internal=call('shell','run-as',PACKAGE,'test','-f',INTERNAL+'/Bioshock.exe',check=False).returncode==0
            for name in names:
                transfer(SHARED,name,False,game/name)
                if internal: transfer(INTERNAL,name,True,game/name)
            override=game.parents[1]/'port_override.json'
            if override.exists():
                transfer('/sdcard/BioshockMQ2','port_override.json',False,override)
                if internal: transfer('files/game','port_override.json',True,override)
            for name in ('HUDPC.swf','HUDRadial.swf'):
                flash=game.parents[1]/'Content/FlashMovies'/name
                if flash.exists():
                    transfer('/sdcard/BioshockMQ2/Content/FlashMovies',name,False,flash)
                    if internal:transfer('files/game/Content/FlashMovies',name,True,flash)
        manifest['status']='installed-bytes-verified'
        manifest['installedDllSha256']=hashlib.sha256((game/'d3d9.dll').read_bytes()).hexdigest()
        manifest['installedIniSha256']=hashlib.sha256((game/'BioshockVR.ini').read_bytes()).hexdigest()
        flash=game.parents[1]/'Content/FlashMovies/HUDPC.swf'
        if flash.exists():manifest['installedHudSha256']=hashlib.sha256(flash.read_bytes()).hexdigest()
        manifest['installedHudMovies']={name:hashlib.sha256((game.parents[1]/'Content/FlashMovies'/name).read_bytes()).hexdigest() for name in ('HUDPC.swf','HUDRadial.swf')}
        (backup/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
        print('Updated APK and game files verified. Starting BioShock...',flush=True)
        token=uuid.uuid4().hex
        manifest['launchId']=token
        launch=backup/'BioshockVR.launch'; launch.write_text(token+'\n',encoding='ascii')
        transfer(SHARED,'BioshockVR.launch',False,launch)
        if call('shell','run-as',PACKAGE,'test','-d',INTERNAL,check=False).returncode==0:
            transfer(INTERNAL,'BioshockVR.launch',True,launch)
        # A detached Wine process may retain an old log handle briefly after
        # force-stop. Rename that inode so it cannot certify the new launch.
        call('shell','run-as',PACKAGE,'mv',INTERNAL+'/BioshockVR.log',INTERNAL+'/BioshockVR.before_install_start.log',check=False)
        call('shell','run-as',PACKAGE,'rm','-f','files/imagefs/tmp/xr/version',check=False)
        ensure_gpu_bridge()
        call('shell','am','start','-n',PACKAGE+'/com.bioshockport.Launcher')
        started=time.monotonic(); checked='' ; fps=[]; cinema_since=None; qualified=[]; seen_samples=0
        while time.monotonic()-started<args.wait_seconds:
            time.sleep(5)
            ensure_gpu_bridge()
            result=read(INTERNAL,'BioshockVR.log',True,check=False)
            if result.returncode: continue
            fresh=result.stdout.decode(errors='replace')
            if 'Launch: '+token not in fresh: continue  # the launcher may copy a cached old log
            checked=fresh
            (backup/'startup.log').write_text(checked,encoding='utf-8')
            if 'Device created, Present/Reset hooked' in checked and 'postLaunchFiles' not in manifest:
                manifest['postLaunchFiles']=verify_runtime_files(game,read)
                print('Post-launch DLL, INI and both HUD movies match in private and shared storage.',flush=True)
            # A sleep or blocking level load is not an ongoing menu FPS sample.
            fps=[float(value) for value,worst in re.findall(r'FPS: ([0-9.]+) \(menu\), worst frame ([0-9.]+) ms',checked)
                 if float(worst)<1000]
            precision='full-precision Android pose bridge active' in checked
            cinema='Theater: screen texture' in checked and 'XrAPI: headset data arriving' in checked and (precision or not require_precision)
            if cinema and cinema_since is None: cinema_since=time.monotonic()
            rendered=active_menu_samples(checked)
            if cinema_since is not None and time.monotonic()-cinema_since>=args.settle_seconds:
                for value in rendered[seen_samples:]:
                    if value is None:qualified.clear()
                    else:qualified.append(value)
            seen_samples=len(rendered)
            if len(qualified)>=3 and all(value<10 for value in qualified[-3:]):
                raise RuntimeError('Menu startup failed: three settled active VR samples below 10 FPS')
            if cinema and len(qualified)>=3 and min(qualified[-3:])>=args.min_menu_fps:
                manifest['status']='vr-cinema-startup-verified'
                manifest['menuFps']=qualified[-1]
                manifest['qualifiedMenuFps']=qualified[-3:]
                print(f'Active VR cinema: three settled samples {qualified[-3:]}. Headset visual check is still needed.',flush=True)
                break
        else:
            online=call('get-state',check=False,timeout=10).stdout.strip()==b'device'
            if not online:
                manifest['status']='installed-verification-interrupted'
                manifest['verificationInterrupted']='ADB disconnected after byte-verified installation'
                print('Installation verified; USB disconnected during startup verification. Candidate retained for inspection.',flush=True)
            elif 'Device created, Present/Reset hooked' not in checked:
                power=call('shell','dumpsys','power',check=False).stdout
                vr_alive=call('shell','pidof',PACKAGE+':vr_process',check=False).stdout.strip()
                if b'mWakefulness=Asleep' in power and vr_alive:
                    manifest['status']='installed-headset-sleeping'
                    print('VR process is alive; Quest sleep paused game startup. Retaining the verified installation for a worn-headset check.',flush=True)
                else:
                    raise RuntimeError('Device creation was not confirmed before the startup timeout')
            else:
                manifest['status']='vr-performance-check-needed' if 'Theater: screen texture' in checked else 'graphics-ready-headset-tracking-needed'
            manifest['menuFps']=fps[-1] if fps else None
            if manifest['status'] not in ('installed-headset-sleeping','installed-verification-interrupted'):
                print('Graphics initialized; VR visibility and performance are still unverified. Inspect the headset.',flush=True)
        (backup/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
        (ROOT/'dist'/'quest'/'last-install.json').write_text(json.dumps({'backup':str(backup),**manifest},indent=2)+'\n')
        print(f'Backup/rollback: {backup}',flush=True)
    except Exception:
        if call('get-state',check=False,timeout=10).stdout.strip()!=b'device':
            manifest['status']='update-interrupted-device-disconnected'
            (backup/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
            print('ADB disconnected. Backup retained; recovery has not been performed.',flush=True)
            raise
        fallback=backup if args.fallback_current else args.fallback
        reference=ROOT/'config'/'quest'/'recovery.json'
        if fallback is None and reference.is_file():
            fallback=ROOT/json.loads(reference.read_text())['backup']
        if fallback and (fallback/'manifest.json').is_file():
            # Preserve this attempt's backup; a prior unverified candidate is
            # not a reliable recovery target.
            manifest['status']='failed-restored-recovery'
            (backup/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
            backup=fallback.resolve()
            restore(json.loads((backup/'manifest.json').read_text()))
        elif manifest['files'] or manifest['apk']:
            restore(manifest)
        raise

if __name__=='__main__':
    try: main()
    except Exception as error: print(f'ERROR: {error}',file=sys.stderr); sys.exit(1)
