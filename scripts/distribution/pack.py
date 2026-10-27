"""Developer build of the all-in-one, offline Windows installer."""
from pathlib import Path
import configparser
import hashlib
import json
import shutil
import subprocess
import sys
import urllib.request
import zipfile

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/distribution';PAYLOAD=OUT/'payload'
def digest(path):
 h=hashlib.sha256()
 with path.open('rb') as f:
  for data in iter(lambda:f.read(4<<20),b''):h.update(data)
 return h.hexdigest()
def copy(source,name):
 target=PAYLOAD/name;target.parent.mkdir(parents=True,exist_ok=True)
 # The bundled ADB server can still be running after a device test. Reuse
 # identical components instead of rewriting an executable locked by Windows.
 if target.is_file() and target.stat().st_size==source.stat().st_size and digest(target)==digest(source):return
 shutil.copy2(source,target)
def main():
 PAYLOAD.mkdir(parents=True,exist_ok=True)
 copy(ROOT/'dist/release/BioShock_VR.apk','BioShock_VR.apk')
 copy(ROOT/'build/native/quest-release/d3d9.dll','d3d9.dll')
 copy(ROOT/'config/quest/release.ini','config/quest/release.ini')
 copy(ROOT/'config/distribution.json','config/distribution.json')
 copy(ROOT/'config/quest/profiles/release.ini','profiles/release.ini')
 copy(ROOT/'source/native/third_party/bioshock-vr/LICENSE','licenses/bioshock-vr.txt')
 sdk=Path('D:/Android/Sdk/platform-tools')
 for name in ('adb.exe','AdbWinApi.dll','AdbWinUsbApi.dll','libwinpthread-1.dll','NOTICE.txt','source.properties'):copy(sdk/name,'tools/'+name)
 steamless=ROOT/'dependencies/runtime/Steamless'
 for name in ('Steamless.CLI.exe','Steamless.CLI.exe.config','source.json'):copy(steamless/name,'tools/Steamless/'+name)
 for path in (steamless/'Plugins').glob('*.dll'):
  if 'x64' not in path.name and 'Example' not in path.name:copy(path,'tools/Steamless/Plugins/'+path.name)
 for path in (ROOT/'dependencies/runtime/MetaADB').rglob('*'):
  if path.is_file():copy(path,'tools/MetaADB/'+path.relative_to(ROOT/'dependencies/runtime/MetaADB').as_posix())
 for name,url in {'WinlatorXR':'https://raw.githubusercontent.com/WinlatorXR/WinlatorXR/winlatorxr_dawn42/LICENSE','Steamless':'https://raw.githubusercontent.com/atom0s/Steamless/master/LICENSE'}.items():
  target=PAYLOAD/'licenses'/str(name+'.txt')
  if not target.exists():
   request=urllib.request.Request(url,headers={'User-Agent':'BioShockVR-build'})
   with urllib.request.urlopen(request,timeout=30) as f:target.write_bytes(f.read())
 profile=configparser.ConfigParser();profile.read(ROOT/'config/quest/release.ini',encoding='utf-8');version=profile.getint('port','version')
 (PAYLOAD/'SOURCES.txt').write_text(f'BioShock VR release{version}: standalone Quest port, offline installer and exit fix.\n'
  'WinlatorXR dawn42: https://github.com/WinlatorXR/WinlatorXR/tree/winlatorxr_dawn42\n'
  'Steamless v3.1.0.5: https://github.com/atom0s/Steamless/tree/v3.1.0.5\n'
  'ADB: https://android.googlesource.com/platform/packages/modules/adb/\n'
  'Meta ADB driver2.0: https://developers.meta.com/vr/downloads/package/oculus-adb-drivers/\n'
  'Port sources and build scripts are embedded in PORT_SOURCE.zip. No game data or signing keys are included.\n',encoding='utf-8')
 with zipfile.ZipFile(PAYLOAD/'PORT_SOURCE.zip','w',zipfile.ZIP_DEFLATED) as z:
  for folder in ('source/native','source/android','scripts/quest','scripts/distribution'):
   for path in (ROOT/folder).rglob('*'):
    if path.is_file() and '__pycache__' not in path.parts and path.suffix not in ('.pyc','.exe','.dll','.so'):z.write(path,path.relative_to(ROOT).as_posix())
  for name in ('config/quest/release.ini','config/quest/profiles/release.ini','config/distribution.json'):z.write(ROOT/name,name)
  minhook=next((ROOT/'dependencies/build').glob('*minhook*/include/MinHook.h')).parents[1]
  for path in minhook.rglob('*'):
   if path.is_file() and (path.suffix in ('.h','.c') or path.name.lower().startswith('license')):z.write(path,path.relative_to(ROOT).as_posix())
  for path in (ROOT/'dependencies/build/openxr/include').rglob('*.h'):z.write(path,path.relative_to(ROOT).as_posix())
  z.write(PAYLOAD/'SOURCES.txt','SOURCES.txt')
 files={}
 for path in PAYLOAD.rglob('*'):
  if not path.is_file() or path.name=='manifest.json':continue
  if 'keystore' in path.name.lower() or path.name.lower()=='bioshock.exe':raise RuntimeError('Private/game file in release payload')
  files[path.relative_to(PAYLOAD).as_posix()]={'size':path.stat().st_size,'sha256':digest(path)}
 manifest={'version':version,'game':'BioShock 2007 v1.1','targets':['quest3','quest3s'],'modelEnforcement':False,'package':'com.winlator.cmod','files':files,
  'gameSignatures':[{'address':0x10932bd0,'bytes':'83 ec 1c 53 55 8b 6c 24 28 8b 45 00'},{'address':0x109cf490,'bytes':'6a ff 68 94 12 f5 10 64 a1 00 00 00 00'},{'address':0x10c7b080,'bytes':'6a ff 68 fe 6e f6 10 64 a1 00 00 00 00'}]}
 (PAYLOAD/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
 target=ROOT/'dist/installer';target.mkdir(parents=True,exist_ok=True)
 command=[sys.executable,'-m','PyInstaller','--noconfirm','--clean','--onefile','--console','--noupx','--exclude-module','tkinter',
  '--name','BioShockVR-Setup','--distpath',str(target),'--workpath',str(OUT/'pyinstaller'),
  '--specpath',str(OUT),'--paths',str(ROOT/'scripts/quest'),'--paths',str(ROOT/'scripts/distribution'),
  '--add-data',str(PAYLOAD)+';payload','--hidden-import','compression.zstd','--hidden-import','_zstd',
  str(ROOT/'scripts/distribution/installer.py')]
 subprocess.run(command,check=True,cwd=ROOT)
 exe=target/'BioShockVR-Setup.exe'
 (target/'SHA256.txt').write_text(digest(exe)+'  '+exe.name+'\n',encoding='ascii')
 print('BUILT',exe,exe.stat().st_size,flush=True)
if __name__=='__main__':main()
