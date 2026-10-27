"""Check retail formatter ABI, native labels and the actual contextual bindings."""
from pathlib import Path
import importlib.util
import subprocess
import tempfile
import sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'research'))
from pe import data,va2off
def code(address,count):return data[va2off(address):va2off(address)+count]
assert code(0x109cf490,13)==bytes.fromhex('6a ff 68 94 12 f5 10 64 a1 00 00 00 00')
assert code(0x109cf8c3,3)==bytes.fromhex('c2 1c 00') # hidden result + two owned 12-byte strings
assert code(0x1090aff0,9)==bytes.fromhex('55 8b ec 56 8b f1 8b 4d 08')
assert code(0x109cf892,5)[0]==0xe8 and code(0x109cf8a5,5)[0]==0xe8 # destroys both by-value arguments
out=ROOT/'build/tests/quest-controls';out.mkdir(parents=True,exist_ok=True)
compiler=next((ROOT/'dependencies/build').glob('llvm-mingw*/bin/i686-w64-mingw32-clang++.exe'))
minhook=next((ROOT/'dependencies/build').glob('*minhook*/include/MinHook.h')).parent
target=out/'prompts.exe'
subprocess.run([str(compiler),'-std=c++17','-O2','-static','-I',str(minhook),str(ROOT/'source/native/tests/quest_prompts.cpp'),'-o',str(target)],check=True)
r=subprocess.run([str(target)],capture_output=True,text=True,timeout=15)
(out/'prompts.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr,end='')
if r.returncode:raise RuntimeError('Native prompt handler regression')
spec=importlib.util.spec_from_file_location('builder',ROOT/'scripts/quest/builder.py');b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)
game=b.find_game('auto');cfg=b.load_config(ROOT/'config/quest/port.ini')
with tempfile.TemporaryDirectory(dir=out) as folder:
    release=Path(folder)/'Builds/Release';release.mkdir(parents=True)
    original=game/'Builds/Release/DefUser.ini';(release/'DefUser.ini').write_bytes(original.read_bytes())
    b.remap_keys(Path(folder),cfg,game)
    def sections(path):
        result={};current=None
        for line in path.read_text().splitlines():
            if line.startswith('['):current=line.strip()[1:-1];result[current]={}
            elif current and '=' in line and not line.startswith(';'):k,v=line.split('=',1);result[current][k]=v
        return result
    actual=sections(release/'DefUser.ini');native=sections(original)
    default=actual['Default']
    assert default['M']=='HintButtonPressed | onRelease HintButtonReleased' and default['I']=='LaunchQuestsScreen'
    assert default['Shift']=='LaunchPCWeaponSelection' and default['T']=='PlayOldestUnreadLog'
    assert default['Tilde']=='ShowContextHelp' and default['P']=='HintButtonAlias' and default['Z']=='ZoomCycle'
    assert default['PageUp']=='AmmoSelectionUp' and default['PageDown']=='AmmoSelectionDown'
    assert default['H']=='Hack' and default['X']=='UseHypoOfType MedHypo' and default['Y']=='InjectBioAmmo'
    assert actual['ContainerUIActive']['A'].startswith('CollectAllContainerItems') and actual['ContainerUIActive']['B']=='CloseContainer'
    for name,values in native.items():
        if name.lower().endswith('uiactive') and name.lower()!='containeruiactive':
            for key,source in [('A','XENON_A'),('X','XENON_X'),('Y','XENON_Y'),('H','XENON_X'),('Q','XENON_RB')]:
                if values.get(source):assert actual[name][key]==values[source],(name,key)
    first=(release/'DefUser.ini').read_bytes();b.remap_keys(Path(folder),cfg,game)
    assert first==(release/'DefUser.ini').read_bytes(),'Bindings must be reproducible from the untouched original'
print('PASS: original map/quests/equipment/log/help/zoom/ammo commands, contextual UI actions, healing/loot preserved, repeatable installer output')
