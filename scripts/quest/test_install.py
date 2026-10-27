"""Installer regressions without USB writes, APK changes or gameplay input."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
import contextlib
import io
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('quest_installer_test',ROOT/'scripts/quest/install.py')
installer=importlib.util.module_from_spec(spec);spec.loader.exec_module(installer)

def result(stdout=b'',stderr=b'',code=0):
    return subprocess.CompletedProcess([],code,stdout,stderr)

class InstallerTests(unittest.TestCase):
    def test_phone_rejected_before_backup_or_mutation(self):
        calls=[]
        def fake_run(command,**kwargs):
            calls.append(command)
            if command[-1]=='devices':return result(b'List of devices attached\nphone\tdevice\n')
            if command[-1]=='ro.product.model':return result(b'SM_A075F\n')
            if command[-1]=='ro.product.device':return result(b'a07\n')
            self.fail('Unexpected operation before device validation: '+str(command))
        with tempfile.TemporaryDirectory() as temp,patch.object(installer,'ROOT',Path(temp)),\
             patch.object(installer.sys,'argv',['install.py']),patch.object(installer.subprocess,'run',fake_run):
            with self.assertRaisesRegex(RuntimeError,'targets Meta Quest 2'):installer.main()
            self.assertEqual(list(Path(temp).iterdir()),[])
        self.assertEqual(len(calls),3)

    def test_real_quest_identity(self):
        def call(*args):return result(b'Quest 2\n' if args[-1].endswith('model') else b'hollywood\n')
        self.assertEqual(installer.require_quest2(call)['device'],'hollywood')

    def test_missing_old_hud_is_created_not_backup_failure(self):
        manifest={'files':[],'created':[]}
        def read(*args,**kwargs):return result(stderr=b'cat: files/game/Content/FlashMovies/HUDPC.swf: No such file or directory\n',code=1)
        with tempfile.TemporaryDirectory() as temp:
            installer.snapshot_file(read,manifest,Path(temp),'internal-flash','files/game/Content/FlashMovies','HUDPC.swf',True)
            self.assertEqual(manifest['files'],[])
            self.assertEqual(manifest['created'][0]['name'],'HUDPC.swf')

    def test_unreadable_old_hud_aborts_without_created_record(self):
        manifest={'files':[],'created':[]}
        def read(*args,**kwargs):return result(stderr=b'cat: HUDPC.swf: Permission denied\n',code=1)
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaisesRegex(RuntimeError,'Cannot back up'):
                installer.snapshot_file(read,manifest,Path(temp),'shared-flash','/sdcard/BioshockMQ2/Content/FlashMovies','HUDPC.swf',False)
            self.assertEqual(manifest,{'files':[],'created':[]})

    def test_shared_files_saved_even_without_private_app_data(self):
        manifest={'files':[],'created':[]};data=b'previous shared DLL'
        def read(*args,**kwargs):return result(data)
        with tempfile.TemporaryDirectory() as temp:
            backup=Path(temp)
            installer.snapshot_file(read,manifest,backup,'shared',installer.SHARED,'d3d9.dll',False)
            self.assertEqual((backup/'shared/d3d9.dll').read_bytes(),data)
            self.assertFalse(manifest['files'][0]['private'])

    def test_post_launch_copy_is_checked_for_both_hud_movies(self):
        with tempfile.TemporaryDirectory() as temp:
            cache=Path(temp)/'BioshockMQ2';game=cache/'Builds/Release'
            game.mkdir(parents=True);flash=cache/'Content/FlashMovies';flash.mkdir(parents=True)
            for name in ('d3d9.dll','BioshockVR.ini'):(game/name).write_bytes(name.encode())
            for name in ('HUDPC.swf','HUDRadial.swf'):(flash/name).write_bytes(name.encode())
            def read(directory,name,private):return result(name.encode())
            checked=installer.verify_runtime_files(game,read)
            self.assertEqual(len(checked),8)
            def stale_launcher(directory,name,private):
                return result(b'old radial HUD' if private and name=='HUDRadial.swf' else name.encode())
            with self.assertRaisesRegex(RuntimeError,'Post-launch byte verification failed.*HUDRadial'):
                installer.verify_runtime_files(game,stale_launcher)

    def test_sleep_and_untracked_fps_do_not_trigger_active_failure(self):
        def sample(fps,tracking='0/72',cinema='0/72',worst='40.0'):
            return f'FPS: {fps} (menu), worst frame {worst} ms; tracking {tracking}, cinema {cinema}\n'
        text=sample('2.0')*3+sample('3.0','72/72','72/72')*3
        self.assertEqual(installer.active_menu_samples(text),[None]*3+[3.0]*3)
        self.assertEqual(installer.active_menu_samples(sample('72','0/0','0/0')),[None])
        self.assertEqual(installer.active_menu_samples(sample('72','72/72','72/72','2000')),[None])
        self.assertEqual(installer.active_menu_samples(sample('72','69/72','70/72')),[72.0])
        self.assertEqual(installer.active_menu_samples(sample('72','68/72','70/72')),[None])

    def test_existing_winlator_without_cache_completes_and_ignores_sleep_fps(self):
        # Run the real installer, including backup, first cache transfer,
        # launcher copy and startup classification, against an in-memory ADB.
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);stage=root/'dist/quest/BioshockMQ2';game=stage/'Builds/Release'
            game.mkdir(parents=True);flash=stage/'Content/FlashMovies';flash.mkdir(parents=True)
            (root/'dist/quest/BioShock_VR.apk').write_bytes(b'candidate APK')
            for name in ('Bioshock.exe','d3d9.dll','Default.ini'):(game/name).write_bytes(name.encode())
            (game/'BioshockVR.ini').write_text('[VR]\nRawPose=1\n')
            for name in ('HUDPC.swf','HUDRadial.swf'):(flash/name).write_bytes(name.encode())
            files={};writes=[];clock=[0.0]
            def fake_run(command,**kwargs):
                command=list(map(str,command))
                if command[1:]==['devices']:return result(b'List of devices attached\nquest\tdevice\n')
                self.assertEqual(command[1:3],['-s','quest'])
                args=command[3:]
                if args[:2]==['shell','getprop']:
                    return result(b'Quest 2\n' if args[-1].endswith('model') else b'hollywood\n')
                if args==['shell','pm','path',installer.PACKAGE]:return result(b'package:/data/app/previous.apk\n')
                if args[0]=='pull':Path(args[2]).write_bytes(b'previous APK');return result()
                if args[0]=='install':writes.append('apk');return result(b'Success\n')
                if args[0]=='push':
                    local=Path(args[1]);target=args[2];writes.append('push')
                    if local.is_dir():
                        for path in local.rglob('*'):
                            if path.is_file():files[(False,target.rstrip('/')+'/'+local.name+'/'+path.relative_to(local).as_posix())]=path.read_bytes()
                    else:files[(False,target)]=local.read_bytes()
                    return result()
                if args==['get-state']:return result(b'device\n')
                if args[:2]==['shell','appops']:return result()
                if args[:3]==['shell','am','force-stop']:return result()
                if args[:3]==['shell','am','start']:
                    for (private,path),data in list(files.items()):
                        if not private and path.startswith('/sdcard/BioshockMQ2/'):
                            files[(True,'files/game/'+path.removeprefix('/sdcard/BioshockMQ2/'))]=data
                    token=files[(True,installer.INTERNAL+'/BioshockVR.launch')].decode().strip()
                    log='Launch: '+token+'\nDevice created, Present/Reset hooked\n'
                    log+='FPS: 2.0 (menu), worst frame 80 ms, tracking 0/72, cinema 0/72\n'*3
                    files[(True,installer.INTERNAL+'/BioshockVR.log')]=log.encode()
                    return result()
                private=args[1:3]==['run-as',installer.PACKAGE]
                operation=args[3:] if private else args[1:]
                if args[0]=='exec-out' and operation[0]=='cat':
                    data=files.get((private,operation[1]))
                    return result(data) if data is not None else result(stderr=b'cat: No such file or directory\n',code=1)
                if operation[:2]==['test','-f']:return result(code=0 if (private,operation[2]) in files else 1)
                if operation[:2]==['test','-d']:return result(code=1)
                if operation[0]=='find':return result()
                if operation[0] in ('mkdir','chmod','mv','rm'):return result()
                if operation[0]=='cp':
                    files[(private,operation[2])]=files[(False,operation[1])];return result()
                self.fail('Unexpected ADB operation: '+str(args))
            def advance(seconds):clock[0]+=seconds
            with patch.object(installer,'ROOT',root),patch.object(installer.sys,'argv',
                    ['install.py','--serial','quest','--wait-seconds','10','--fallback-current']),\
                 patch.object(installer.subprocess,'run',fake_run),patch.object(installer.time,'sleep',advance),\
                 patch.object(installer.time,'monotonic',lambda:clock[0]),contextlib.redirect_stdout(io.StringIO()):
                installer.main()
            saved=installer.json.loads((root/'dist/quest/last-install.json').read_text())
            self.assertEqual(saved['status'],'graphics-ready-headset-tracking-needed')
            self.assertEqual(len(saved['postLaunchFiles']),8)
            self.assertEqual(saved['menuFps'],2.0)
            self.assertTrue(any(x['name']=='HUDRadial.swf' for x in saved['created']))
            self.assertEqual(writes.count('apk'),1,'Sleeping startup must not trigger rollback')

if __name__=='__main__':unittest.main(verbosity=2)
