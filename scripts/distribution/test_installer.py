"""Offline installer transaction regressions; all ADB operations are simulated."""
from pathlib import Path
import json
import shlex
import subprocess
import tempfile
import unittest
import sys
sys.path.insert(0,str(Path(__file__).parent))
import core

class Tests(unittest.TestCase):
 def setup_engine(self,folder,model='Quest 2',state='device',failure=None):
  root=Path(folder);payload=root/'payload';payload.mkdir();(payload/'BioShock_VR.apk').write_bytes(b'new APK')
  engine=core.Installer(payload=payload,workspace=root/'work');engine.workspace.mkdir()
  engine.job=root/'work/job';engine.job.mkdir();engine.cache=engine.job/core.CACHE;engine.cache.mkdir()
  engine.manifest={'version':87,'files':{'BioShock_VR.apk':{'sha256':core.sha(payload/'BioShock_VR.apk')}}}
  engine.prepared={'target':'quest3','bytes':100,'critical':{}}
  calls=[];failed=False
  def runner(args,check=True,timeout=60):
   nonlocal failed
   calls.append(args);tail=args[1:]
   if tail==['devices','-l']:text='List of devices attached\nselected\t'+state+'\n'
   elif args[-1]=='ro.product.model':text=model
   elif args[-1]=='ro.product.manufacturer':text='Oculus'
   elif 'shell' in args:
    command=shlex.split(args[-1]);text=''
    if command[:2]==['df','-k']:text='Filesystem 1K-blocks Used Available Use% Mounted\n/dev/data 200000000 1000 199999000 1% /sdcard\n'
    elif command[:2]==['pm','path']:text='package:/data/app/previous/base.apk\n'
    elif failure=='commit' and command[0]=='mv' and command[1].startswith('/sdcard/.bioshock-vr-install-') and not failed:
     failed=True;raise core.InstallError('simulated USB failure during commit')
   elif 'pull' in args:Path(args[-1]).write_bytes(b'previous APK');text=''
   elif 'install' in args and args[-1].endswith('BioShock_VR.apk') and failure=='apk':raise core.InstallError('INSTALL_FAILED_UPDATE_INCOMPATIBLE')
   else:text='Success'
   return subprocess.CompletedProcess(args,0,text)
  engine.runner=runner;return engine,calls

 def test_no_model_gate_user_choice_is_honored(self):
  for model in ('Quest 3','Quest 3S','Quest 2','Unknown headset'):
   with self.subTest(model=model),tempfile.TemporaryDirectory() as folder:
    engine,calls=self.setup_engine(folder,model=model);r=engine.install('selected')
    self.assertEqual(r['status'],'installed');self.assertTrue(any('install' in c for c in calls))
    self.assertFalse(any('uninstall' in c for c in calls))

 def test_unselected_or_unauthorized_device_cannot_receive_writes(self):
  for state,serial in [('unauthorized','selected'),('device','not-selected')]:
   with tempfile.TemporaryDirectory() as folder:
    engine,calls=self.setup_engine(folder,state=state)
    with self.assertRaises(core.InstallError):engine.install(serial)
    self.assertFalse(any(any(word in c for word in ('install','push','pull')) for c in calls))

 def test_commit_failure_restores_old_cache_and_apk(self):
  with tempfile.TemporaryDirectory() as folder:
   engine,calls=self.setup_engine(folder,failure='commit')
   with self.assertRaisesRegex(core.InstallError,'simulated USB'):engine.install('selected')
   shell=[shlex.split(c[-1]) for c in calls if 'shell' in c]
   self.assertTrue(any(c[0]=='mv' and '.backup-' in c[1] and c[2]=='/sdcard/BioshockMQ2' for c in shell))
   self.assertTrue(any('install' in c and c[-1].endswith('previous.apk') for c in calls))
   self.assertEqual(json.loads((engine.workspace/'last-install.json').read_text())['status'],'restored')

 def test_signature_failure_never_moves_existing_game_or_uninstalls(self):
  with tempfile.TemporaryDirectory() as folder:
   engine,calls=self.setup_engine(folder,failure='apk')
   with self.assertRaisesRegex(core.InstallError,'UPDATE_INCOMPATIBLE'):engine.install('selected')
   shell=[shlex.split(c[-1]) for c in calls if 'shell' in c]
   self.assertFalse(any(c[0]=='mv' for c in shell));self.assertFalse(any('uninstall' in c for c in calls))
   self.assertTrue(all(c[2].startswith('/sdcard/.bioshock-vr-install-') for c in shell if c[:2]==['rm','-rf']))

 def test_failed_restore_is_reported_to_user_and_receipt(self):
  with tempfile.TemporaryDirectory() as folder:
   engine,calls=self.setup_engine(folder,failure='commit');original=engine.runner
   def broken_restore(args,**kwargs):
    if 'install' in args and args[-1].endswith('previous.apk'):raise core.InstallError('device offline')
    return original(args,**kwargs)
   engine.runner=broken_restore
   with self.assertRaisesRegex(core.InstallError,'Rollback could not be completed'):engine.install('selected')
   receipt=json.loads((engine.workspace/'last-install.json').read_text())
   self.assertEqual(receipt['status'],'restore-needs-attention');self.assertEqual(receipt['restoreError'],'device offline')

 def test_cancel_after_package_commit_restores_before_cache_switch(self):
  with tempfile.TemporaryDirectory() as folder:
   engine,calls=self.setup_engine(folder);original=engine.runner
   def cancel_after_commit(args,**kwargs):
    result=original(args,**kwargs)
    if 'install' in args and args[-1].endswith('BioShock_VR.apk'):engine.cancel.set()
    return result
   engine.runner=cancel_after_commit
   with self.assertRaises(core.Cancelled):engine.install('selected')
   self.assertTrue(any('install' in c and c[-1].endswith('previous.apk') for c in calls))
   self.assertFalse(any(shlex.split(c[-1])[0]=='mv' for c in calls if 'shell' in c))
   self.assertEqual(json.loads((engine.workspace/'last-install.json').read_text())['status'],'restored')

 def test_payload_hash_and_path_validation(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);(root/'a').write_bytes(b'data');m={'files':{'a':{'size':4,'sha256':core.sha(root/'a')}}}
   (root/'manifest.json').write_text(json.dumps(m));core.verify_payload(root)
   (root/'a').write_bytes(b'bad!')
   with self.assertRaises(core.InstallError):core.verify_payload(root)
   m['files']={'../outside':{'size':0,'sha256':''}};(root/'manifest.json').write_text(json.dumps(m))
   with self.assertRaises(core.InstallError):core.verify_payload(root)

 def test_bad_game_cannot_be_patched(self):
  with tempfile.TemporaryDirectory() as folder:
   p=Path(folder)/'Bioshock.exe';p.write_bytes(b'not a supported game')
   with self.assertRaisesRegex(core.InstallError,'2007'):core.validate_game(p,[{'address':0x10932bd0,'bytes':'83ec1c'}])

if __name__=='__main__':unittest.main(verbosity=2)
