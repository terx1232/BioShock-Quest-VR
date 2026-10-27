"""Replay real loot/cinematic contexts and build actual Quest key bindings."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import struct
ROOT=Path(__file__).resolve().parents[2]
image=(ROOT/'dist/quest/BioshockMQ2/Builds/Release/Bioshock.exe').read_bytes()
pe=struct.unpack_from('<I',image,0x3c)[0];optional=pe+24
base=struct.unpack_from('<I',image,optional+28)[0]
table=optional+struct.unpack_from('<H',image,pe+20)[0]
def native(address,count):
    for i in range(struct.unpack_from('<H',image,pe+6)[0]):
        _,size,virtual,raw_size,raw=struct.unpack_from('<8sIIII',image,table+i*40)
        if virtual<=address-base and address-base+count<=virtual+raw_size:
            p=raw+address-base-virtual;return image[p:p+count]
    raise AssertionError('Unmapped native tutorial code')
assert native(0x10c8a5c0,13)==bytes.fromhex('6a ff 68 78 c4 f4 10 64 a1 00 00 00 00')
call=native(0x10c8aa8f,5)
assert call[0]==0xe8 and 0x10c8aa94+struct.unpack_from('<i',call,1)[0]==0x10c8a5c0
assert native(0x10c18d00,13)==bytes.fromhex('6a ff 68 18 28 f5 10 64 a1 00 00 00 00')
call=native(0x10c19949,5)
assert call[0]==0xe8 and 0x10c1994e+struct.unpack_from('<i',call,1)[0]==0x10c18d00
assert native(0x10942b50,10)==bytes.fromhex('8b c1 8b 88 e0 00 00 00 b2 01')
assert native(0x10c95710,13)==bytes.fromhex('6a ff 68 a6 82 f6 10 64 a1 00 00 00 00')
assert native(0x10c95911,2)==bytes.fromhex('ff d0')
assert native(0x10c95907,6)==bytes.fromhex('8b 82 88 02 00 00') # same native effect vtable slot
assert native(0x10c9592c,3)==bytes.fromhex('c2 08 00') # VM wrapper still consumes both parameters
assert native(0x10942be4,3)==native(0x10942be9,3)==bytes.fromhex('c2 30 00')
assert struct.unpack('<I',native(0x110521e4+0x288,4))[0]==0x10942b50
assert struct.unpack('<I',native(0x1105a3b4+12,4))[0]==0x10942720
assert native(0x1094274a,3)==native(0x10ce2c04,3)==bytes.fromhex('c2 0c 00')
print('PASS: tutorial hook signature and native VM-wrapper call match the actual game EXE')
assert struct.unpack('<I',native(0x1106b2cc+58*4,4))[0]==0x10c7b080
assert struct.unpack('<I',native(0x1106b2cc+60*4,4))[0]==0x10c7ae10
assert native(0x10c07110,7)==bytes.fromhex('d9 81 b8 0a 00 00 c3')
assert native(0x10c07120,7)==bytes.fromhex('d9 81 b4 0a 00 00 c3')
assert native(0x10c7aeeb,3)==bytes.fromhex('6a 01 50') # native completion removes exactly one item
assert native(0x10c7b164,2)==bytes.fromhex('33 c0') and native(0x10c7b171,3)==bytes.fromhex('c2 04 00')
print('PASS: original EVE Use/FinishedUsing vtable, current/max getter fields, one-item removal and zero-return ABI')
OUT=ROOT/'build/tests/quest-uistate';OUT.mkdir(parents=True,exist_ok=True)
compiler=next((ROOT/'dependencies/build').glob('llvm-mingw*/bin/i686-w64-mingw32-clang++.exe'))
minhook=next((ROOT/'dependencies/build').glob('*minhook*/include/MinHook.h')).parent
exe=OUT/'uistate.exe'
subprocess.run([str(compiler),'-std=c++17','-O2','-static','-I',str(minhook),str(ROOT/'source/native/tests/uistate_fixture.cpp'),'-o',str(exe)],check=True)
r=subprocess.run([str(exe)],capture_output=True,text=True,check=True);print(r.stdout,end='')
spec=importlib.util.spec_from_file_location('builder',ROOT/'scripts/quest/builder.py');b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)
original=b.find_game('auto')/'Builds/Release/DefUser.ini'
with tempfile.TemporaryDirectory(dir=OUT) as tmp:
    path=Path(tmp)/'Builds/Release';path.mkdir(parents=True);(path/'DefUser.ini').write_bytes(original.read_bytes())
    b.remap_keys(Path(tmp),{'vr_mode':'mod'},original.parents[2])
    sections={};current=None
    for line in (path/'DefUser.ini').read_text().splitlines():
        if line.startswith('['):current=line.strip()[1:-1];sections[current]={}
        elif current and '=' in line and not line.startswith(';'):k,v=line.split('=',1);sections[current][k]=v
    loot=sections['ContainerUIActive']
    assert loot['A']==loot['XENON_A'] and loot['A'].startswith('CollectAllContainerItems')
    assert loot['B']==loot['XENON_B']=='CloseContainer'
    assert sections['Default']['A']=='Use'
    print('PASS: actual DefUser.ini remap: gameplay A=Use, loot A=CollectAll/B=Close, native release actions retained')
    b.remap_keys(Path(tmp),{'vr_mode':'mod','quest_healing_buttons':True},original.parents[2])
    updated={};current=None
    for line in (path/'DefUser.ini').read_text().splitlines():
        if line.startswith('['):current=line.strip()[1:-1];updated[current]={}
        elif current and '=' in line and not line.startswith(';'):k,v=line.split('=',1);updated[current][k]=v
    assert updated['Default']['X']=='UseHypoOfType MedHypo'
    assert updated['Default']['Y']=='InjectBioAmmo' and updated['Default']['H']==sections['Default']['Y']=='Hack'
    assert updated['ContainerUIActive']['A']==loot['A'] and updated['ContainerUIActive']['B']==loot['B']
    for name,values in updated.items():
        if 'InjectBioAmmo' not in values.get('XENON_X',''):assert values.get('Y')!='InjectBioAmmo',name
    print('PASS: actual native heal/EVE/hack commands; inventory checks and original context restrictions retained')
(OUT/'result.log').write_text(r.stdout+'PASS: original-game loot bindings\n')
weapon=OUT/'cinematic-weapon.exe'
subprocess.run([str(compiler),'-std=c++17','-O2','-static','-ffunction-sections','-fdata-sections','-Wl,--gc-sections','-I',str(minhook),str(ROOT/'source/native/tests/cinematic_weapon.cpp'),'-o',str(weapon)],check=True)
r=subprocess.run([str(weapon)],capture_output=True,text=True,check=True);print(r.stdout,end='')
with (OUT/'result.log').open('a') as f:f.write(r.stdout)
for name in ['healing_input','instant_eve','navigation','weapon_visual','melee_fixture']:
    target=OUT/(name+'.exe')
    subprocess.run([str(compiler),'-std=c++17','-O2','-static','-I',str(minhook),str(ROOT/'source/native/tests'/ (name+'.cpp')),'-o',str(target)],check=True)
    r=subprocess.run([str(target)],capture_output=True,text=True,check=True);print(r.stdout,end='')
    with (OUT/'result.log').open('a') as f:f.write(r.stdout)
attack=OUT/'attack-tutorial.exe'
subprocess.run([str(compiler),'-std=c++17','-O2','-static','-I',str(minhook),str(ROOT/'source/native/tests/attack_tutorial.cpp'),'-o',str(attack)],check=True)
r=subprocess.run([str(attack)],capture_output=True,text=True,check=True);print(r.stdout,end='')
with (OUT/'result.log').open('a') as f:f.write(r.stdout)
loot=OUT/'loot-input.exe'
subprocess.run([str(compiler),'-std=c++17','-O2','-static','-I',str(minhook),str(ROOT/'source/native/tests/loot_input.cpp'),'-o',str(loot)],check=True)
r=subprocess.run([str(loot)],capture_output=True,text=True,check=True);print(r.stdout,end='')
with (OUT/'result.log').open('a') as f:f.write(r.stdout)
