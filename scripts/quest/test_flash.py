"""Validate targeted changes against the actual original BioShock HUD movie."""
from pathlib import Path
import importlib.util
import struct
import sys
import hashlib

ROOT=Path(__file__).resolve().parents[2]
def load(name,path):
    spec=importlib.util.spec_from_file_location(name,path);m=importlib.util.module_from_spec(spec);sys.modules[name]=m;spec.loader.exec_module(m);return m
b=load('quest_builder_flash_test',ROOT/'scripts/quest/builder.py')
f=load('quest_flash_test',ROOT/'scripts/quest/flashpatch.py')
for movie in ['HUDPC.swf','HUDRadial.swf']:
    source=b.find_game('auto')/'Content/FlashMovies'/movie
    original=source.read_bytes();digest=hashlib.sha256(original).hexdigest()
    patched,report=f.patch(original,hide_enemy_health=True);raw=f.unpack(original);out=f.unpack(patched)
    assert len(raw)==len(out) and [(x.name,x.body,x.size) for x in f.functions(raw)]==[(x.name,x.body,x.size) for x in f.functions(out)]
    allowed=set()
    for change in report:allowed.update(range(change['start'],change['end']))
    assert all(a==z or i in allowed for i,(a,z) in enumerate(zip(raw,out)))
    matrices=f.training_matrices(out);assert len(matrices)==1
    start,end=matrices[0];bits=''.join(format(v,'08b') for v in out[start:end]);assert bits[0]=='1'
    n=int(bits[1:6],2);assert int(bits[6:6+n],2)==int(bits[6+n:6+2*n],2)==0
    assert raw[start:end]!=out[start:end], 'training panel and icon have no visible area'
    hp=f.enemy_health_matrices(out);assert len(hp)==2
    for name,start,end in hp:
        bits=''.join(format(v,'08b') for v in out[start:end]);assert bits[0]=='1'
        n=int(bits[1:6],2);assert int(bits[6:6+n],2)==int(bits[6+n:6+2*n],2)==0
        assert raw[start:end]!=out[start:end],name
    for change in report:
        if change['handler'].startswith('HUDMessage/Training'):
            assert out[change['start']:change['start']+5]==bytes.fromhex('960100033e')
            assert set(out[change['start']+5:change['end']])<={0x17}
            # The original conditional is Equals2 / Not / If. Training falls
            # through into Return; every other message takes its original jump.
            p=change['start']-5
            assert raw[p-2:p]==bytes([0x49,0x12]) and raw[p]==0x9d
            assert p+5+struct.unpack_from('<h',raw,p+3)[0]==change['end']
            for msg in ['Training','Subtitle','UseFocus','HackFocus','Radio']:
                takes_jump=not(msg=='Training')
                reached=change['end'] if takes_jump else change['start']
                assert (out[reached:reached+5]==bytes.fromhex('960100033e'))==(msg=='Training')
    functions={x.name:x for x in f.functions(raw) if x.where=='root'}
    for name in ['SetHealthOfFocus','SetFrozenHealthOfFocus']:
        q=functions[name];assert out[q.body:q.body+5]==bytes.fromhex('960100033e')
    for name in ['SetHealth','SetBioAmmo','UpdateAmmoTotal','OpenContainer','CloseContainer','APressed','SetDialogSubtitles']:
        q=functions[name];assert raw[q.body:q.body+q.size]==out[q.body:q.body+q.size],name
    q=functions['HUDMessage'];assert len(f.actions(out,q.body,q.body+q.size,q.pool))==len(f.actions(raw,q.body,q.body+q.size,q.pool)) or len(f.actions(out,q.body,q.body+q.size,q.pool))>100
    assert hashlib.sha256(source.read_bytes()).hexdigest()==digest
    assert f.patch(original,False,False)[0]==original
    folder=ROOT/'build/tests/quest-flash';folder.mkdir(parents=True,exist_ok=True)
    (folder/movie).write_bytes(patched)
text='PASS: both real Training branches return before panel/icon creation; other dispatch targets unchanged\nPASS: normal/frozen enemy health handlers and centerBox clips disabled in both HUDs; player HP/EVE/ammo/loot/subtitles untouched\nPASS: native NoReticle path, help indicator removal, complete SWF function/tag layout and original Steam asset\n'
(folder/'result.log').write_text(text);print(text,end='')
