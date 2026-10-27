"""Offline release installer: the user's game is patched in a separate folder."""
from pathlib import Path
import configparser
import hashlib
import json
import os
import re
import shlex
import shutil
import struct
import subprocess
import sys
import threading
import time
import uuid

if not getattr(sys,'frozen',False):sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'quest'))
import builder
import flashpatch

PACKAGE='com.winlator.cmod'
CACHE='BioshockMQ2'
TARGETS={'quest3':'Quest 3','quest3s':'Quest 3S'}
ROOT=Path(__file__).resolve().parents[2] if not getattr(sys,'frozen',False) else Path(sys._MEIPASS)
PAYLOAD=(Path(sys._MEIPASS)/'payload') if getattr(sys,'frozen',False) else ROOT/'build/distribution/payload'

class InstallError(RuntimeError):pass
class Cancelled(InstallError):pass

def sha(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(4<<20),b''):h.update(block)
    return h.hexdigest()

def verify_payload(payload=PAYLOAD):
    payload=Path(payload).resolve();manifest=json.loads((payload/'manifest.json').read_text(encoding='utf-8'))
    for name,record in manifest['files'].items():
        path=(payload/name).resolve()
        if not path.is_relative_to(payload) or not path.is_file():raise InstallError('A bundled component is missing: '+name)
        if path.stat().st_size!=record['size'] or sha(path)!=record['sha256']:raise InstallError('A bundled component is damaged: '+name)
    return manifest

def fast_find_game():
    candidates=[]
    for lib in builder.steam_roots():
        if str(lib).startswith('\\\\'):continue
        manifest=lib/'steamapps/appmanifest_7670.acf'
        try:
            text=manifest.read_text(encoding='utf-8',errors='replace') if manifest.is_file() else ''
            match=re.search(r'"installdir"\s+"([^"]+)"',text)
            if match:candidates.append(lib/'steamapps/common'/match[1])
        except OSError:pass
        candidates.append(lib/'steamapps/common/Bioshock')
    for drive in ('C:','D:','E:','F:'):
        for folder in ('Games/Bioshock','GOG Games/Bioshock','SteamLibrary/steamapps/common/Bioshock'):
            candidates.append(Path(drive+'/')/folder)
    for path in candidates:
        if builder.is_game_dir(path):return path.resolve()
    return None

def validate_game(exe,signatures):
    data=Path(exe).read_bytes()
    try:
        if data[:2]!=b'MZ':raise ValueError()
        pe=struct.unpack_from('<I',data,0x3c)[0]
        if data[pe:pe+4]!=b'PE\0\0' or struct.unpack_from('<H',data,pe+4)[0]!=0x14c:raise ValueError()
        opt=pe+24;base=struct.unpack_from('<I',data,opt+28)[0]
        count=struct.unpack_from('<H',data,pe+6)[0];table=opt+struct.unpack_from('<H',data,pe+20)[0]
        for sig in signatures:
            expected=bytes.fromhex(sig['bytes']);found=False
            for i in range(count):
                _,_,va,size,offset=struct.unpack_from('<8sIIII',data,table+i*40)
                rva=sig['address']-base
                if va<=rva and rva+len(expected)<=va+size:
                    at=offset+rva-va;found=data[at:at+len(expected)]==expected;break
            if not found:raise ValueError()
    except (ValueError,struct.error,IndexError):
        raise InstallError('Original BioShock 2007 v1.1 is required. This Bioshock.exe is not compatible with the mod. Remastered is not supported.')

class Installer:
    def __init__(self,payload=PAYLOAD,workspace=None,progress=None,cancel=None,runner=None):
        self.payload=Path(payload).resolve()
        self.workspace=Path(workspace or Path(os.environ.get('LOCALAPPDATA',str(Path.home())))/'BioShockVRInstaller').resolve()
        self.progress=progress or (lambda percent,text:None)
        self.cancel=cancel or threading.Event();self.runner=runner
        self.adb=self.payload/'tools/adb.exe';self.serial=None;self.manifest=None;self.job=None;self.cache=None

    def check(self):
        if self.cancel.is_set():raise Cancelled('Installation cancelled. The original game and saves were preserved.')

    def run(self,args,check=True,timeout=60,ignore_cancel=False,transfer=False):
        args=list(map(str,args))
        if not ignore_cancel:self.check()
        if self.runner:return self.runner(args,check=check,timeout=timeout)
        creation=getattr(subprocess,'CREATE_NO_WINDOW',0)
        proc=subprocess.Popen(args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,creationflags=creation)
        start=time.monotonic();last=-1
        while True:
            try:
                output=proc.communicate(timeout=.2)[0];break
            except subprocess.TimeoutExpired as pending:
                if not ignore_cancel and self.cancel.is_set():proc.terminate();proc.communicate();raise Cancelled('Transfer cancelled. You can run the installer again.')
                if time.monotonic()-start>timeout:proc.kill();proc.communicate();raise InstallError('Operation timed out: '+Path(args[0]).name)
                if transfer and pending.output:
                    values=re.findall(rb'\[\s*(\d+)%\]',pending.output)
                    if values:
                        value=int(values[-1])
                        if value!=last:self.progress(57+value*.28,'Copying the game to the headset: '+str(value)+'%');last=value
        text=output.decode('utf-8',errors='replace')
        result=subprocess.CompletedProcess(args,proc.returncode,text)
        if check and result.returncode:raise InstallError(text.strip()[-2500:] or 'Command failed: '+Path(args[0]).name)
        return result

    def adb_run(self,*args,**kwargs):
        return self.run([self.adb,*(['-s',self.serial] if self.serial else []),*args],**kwargs)

    def shell(self,*args,**kwargs):return self.adb_run('shell',shlex.join(list(map(str,args))),**kwargs)

    def devices(self):
        output=self.run([self.adb,'devices','-l']).stdout
        result=[]
        for line in output.splitlines():
            fields=line.split()
            if len(fields)<2 or fields[1] not in ('device','unauthorized','offline'):continue
            serial,state=fields[:2]
            if not re.fullmatch(r'[A-Za-z0-9_.:-]+',serial):continue
            model=''
            if state=='device':
                model=self.run([self.adb,'-s',serial,'shell','getprop','ro.product.model'],check=False).stdout.strip()
            result.append({'serial':serial,'state':state,'model':model or serial})
        return result

    def prepare(self,game,target):
        if target not in TARGETS:raise InstallError('Choose Meta Quest 3 or Quest 3S.')
        game=Path(game).resolve()
        if not builder.is_game_dir(game):raise InstallError('The selected folder does not contain original BioShock 2007.')
        self.progress(1,'Checking bundled components…');self.manifest=verify_payload(self.payload)
        self.check();self.workspace.mkdir(parents=True,exist_ok=True)
        self.job=self.workspace/'jobs'/uuid.uuid4().hex;self.job.mkdir(parents=True)
        self.cache=self.job/CACHE
        if self.cache.resolve().is_relative_to(game) or game.is_relative_to(self.cache):raise InstallError('The working folder must be separate from the original game.')
        paths=[]
        for folder,dirs,files in os.walk(game):
            rel=Path(folder).relative_to(game)
            if rel==Path('.'):
                dirs[:]=[d for d in dirs if d.lower() not in builder.SKIP_TOP_DIRS]
                files=[f for f in files if not builder.SKIP_TOP_FILES.match(f)]
            for name in files:
                if name.lower().endswith(('.log','.dmp','.tmp','.part')) or name.lower() in ('vrcmd.txt','vrshot.bmp','hudshot.bmp','bioshockvr.launch'):continue
                paths.append(rel/name)
        total=sum((game/p).stat().st_size for p in paths);done=0
        if shutil.disk_usage(self.workspace).free<total+(1<<30):raise InstallError('At least %.1f GiB of free space is required on this PC.'%((total+(1<<30))/2**30))
        for rel in paths:
            self.check();src=game/rel;dst=self.cache/rel;dst.parent.mkdir(parents=True,exist_ok=True)
            try:os.link(src,dst)
            except OSError:shutil.copy2(src,dst)
            done+=src.stat().st_size
            self.progress(3+done/max(1,total)*27,'Preparing a separate copy of the game…')
        cfg=builder.load_config(self.payload/'config/quest/release.ini')
        cfg.update(native_dll='d3d9.dll',native_ini='profiles/release.ini',steamless=str(self.payload/'tools/Steamless/Steamless.CLI.exe'))
        builder.ROOT=self.payload;builder.info=lambda text:None;builder.warn=lambda text:None
        builder.run=lambda args,cwd=None,check=True,quiet=False:self.run(args,check=check,timeout=120)
        # Steamless operates only on the staged copy. Break the executable's
        # hard link first, even if a third-party tool writes it in-place.
        exe=self.cache/'Builds/Release/Bioshock.exe'
        if builder.has_steamstub(exe):
            builder.replace_file(exe,exe.read_bytes())
            if not builder.unpack_steamstub(self.cache,cfg):raise InstallError('Could not prepare your copy of Bioshock.exe.')
        validate_game(exe,self.manifest['gameSignatures']);self.check()
        self.progress(35,'Applying the VR mod and controls…')
        builder.tune_ini(self.cache,cfg);builder.remap_keys(self.cache,cfg,game)
        profile=configparser.ConfigParser();profile.read(self.payload/'profiles/release.ini',encoding='utf-8')
        for name in ('HUDPC.swf','HUDRadial.swf'):
            patched,_=flashpatch.patch((game/'Content/FlashMovies'/name).read_bytes(),True,True,True)
            builder.replace_file(self.cache/'Content/FlashMovies'/name,patched)
        builder.install_native_mod(self.cache,cfg)
        marker={'port':'bioshock-quest','version':self.manifest['version'],'target':target}
        builder.replace_file(self.cache/'.bioshockport',json.dumps(marker).encode())
        launch=builder.port_json(cfg,True)
        override={**launch['vr'],'shortcutExtras':launch['shortcutExtras'],'prefs':{'enable_wine_debug':False,'wine_debug_channels':''}}
        builder.replace_file(self.cache/'port_override.json',(json.dumps(override,indent=2)+'\n').encode())
        builder.replace_file(self.cache/'Builds/Release/SWING-LICENSE.txt',(self.payload/'licenses/bioshock-vr.txt').read_bytes())
        critical=['Builds/Release/'+n for n in ('Bioshock.exe','d3d9.dll','BioshockVR.ini','Default.ini','dxvk-port.conf')]
        critical += ['Builds/Release/'+p.name for p in (self.cache/'Builds/Release').glob('DefUser*.ini')]
        critical += ['Content/FlashMovies/HUDPC.swf','Content/FlashMovies/HUDRadial.swf','port_override.json','.bioshockport']
        self.prepared={'target':target,'game':str(game),'cache':str(self.cache),'bytes':sum(p.stat().st_size for p in self.cache.rglob('*') if p.is_file()),'critical':{n:sha(self.cache/n) for n in critical}}
        (self.job/'prepared.json').write_text(json.dumps(self.prepared,indent=2),encoding='utf-8')
        self.progress(45,'Game prepared. Connect '+TARGETS[target]+' over USB.')
        return self.prepared

    def install(self,serial):
        if not self.cache or not self.manifest:raise InstallError('Prepare the game first.')
        candidates=[d for d in self.devices() if d['serial']==serial and d['state']=='device']
        if len(candidates)!=1:raise InstallError('Choose a connected device and allow USB debugging. Device data was not changed.')
        self.serial=serial;self.check()
        nonce=uuid.uuid4().hex;parent='/sdcard/.bioshock-vr-install-'+nonce
        incoming=parent+'/'+CACHE;current='/sdcard/'+CACHE;backup='/sdcard/'+CACHE+'.backup-'+nonce
        self.progress(48,'Checking free space on the headset…')
        disk=self.shell('df','-k','/sdcard').stdout.splitlines()
        try:free=int(disk[-1].split()[3])*1024
        except (ValueError,IndexError):raise InstallError('Could not check free space on the headset.')
        required=self.prepared['bytes']*2+(2<<30)
        if free<required:raise InstallError('The headset needs %.1f GiB of free space for the game cache and installation.'%(required/2**30))
        old=self.shell('pm','path',PACKAGE,check=False).stdout.strip();previous=self.job/'previous.apk'
        if old.startswith('package:'):
            self.progress(49,'Backing up the installed APK for rollback…')
            self.adb_run('pull',old.splitlines()[0][8:],previous,timeout=300)
        self.shell('am','force-stop',PACKAGE,check=False)
        had_cache=self.shell('test','-d',current,check=False).returncode==0
        old_moved=new_moved=apk_changed=False
        receipt={'serial':serial,'model':candidates[0]['model'],'version':self.manifest['version'],'job':str(self.job),'sharedBackup':backup if had_cache else None,'temporary':parent,'status':'transferring'}
        (self.workspace/'last-install.json').write_text(json.dumps(receipt,indent=2,ensure_ascii=False),encoding='utf-8')
        try:
            self.shell('mkdir','-p',parent)
            self.progress(55,'Copying the game cache over USB…')
            self.adb_run('push',self.cache,parent+'/',timeout=3600,transfer=True)
            self.progress(87,'Verifying files on the headset…')
            for name,expected in self.prepared['critical'].items():
                actual=self.shell('sha256sum',incoming+'/'+name).stdout.split()[0]
                if actual!=expected:raise InstallError('File verification failed: '+name)
            self.progress(91,'Installing the bundled APK…')
            self.check()
            # Android may finish a package commit after its ADB client is killed.
            # Let it finish, then honor cancellation with the known APK result.
            self.adb_run('install','--no-streaming','-r',self.payload/'BioShock_VR.apk',timeout=600,ignore_cancel=True);apk_changed=True
            self.check()
            # These commands form a bounded commit. Cancellation is checked
            # before it; any failed rename restores the previous cache.
            if had_cache:self.shell('mv',current,backup,ignore_cancel=True);old_moved=True
            self.shell('mv',incoming,current,ignore_cancel=True);new_moved=True
            self.shell('rmdir',parent,check=False,ignore_cancel=True)
            self.shell('appops','set',PACKAGE,'MANAGE_EXTERNAL_STORAGE','allow',check=False,ignore_cancel=True)
            receipt['status']='installed';receipt['apkSha256']=self.manifest['files']['BioShock_VR.apk']['sha256']
            self.progress(100,'Done. BioShock VR is available under Unknown Sources on the headset.')
        except Exception as original:
            # Keep source files/saves and the previous APK. Never uninstall a
            # pre-existing Winlator or silently erase its private application data.
            try:
                if new_moved:self.shell('mv',current,'/sdcard/.bioshock-vr-failed-'+nonce,ignore_cancel=True)
                if old_moved:self.shell('mv',backup,current,ignore_cancel=True)
                if apk_changed and previous.is_file():self.adb_run('install','--no-streaming','-r',previous,timeout=600,ignore_cancel=True)
                if re.fullmatch(r'/sdcard/\.bioshock-vr-install-[0-9a-f]{32}',parent):self.shell('rm','-rf',parent,check=False,ignore_cancel=True)
                receipt['status']='restored'
            except Exception as error:
                receipt['status']='restore-needs-attention';receipt['restoreError']=str(error)
                raise InstallError(str(original)+'\n\nRollback could not be completed: '+str(error)+'\nReconnect the same device. Backup files were preserved; details: '+str(self.workspace/'last-install.json')) from original
            raise
        finally:
            (self.workspace/'last-install.json').write_text(json.dumps(receipt,indent=2,ensure_ascii=False),encoding='utf-8')
        return receipt
