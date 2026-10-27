"""Derive a left palm/finger mask from the original NEWPlayerHands skin weights."""
from pathlib import Path
import hashlib
import struct
import subprocess
ROOT=Path(__file__).resolve().parents[2]

def ensure_psk():
    path=ROOT/'build/work/hand77/1-Medical/SkeletalMesh/NEWPlayerHands.psk'
    if path.exists():return path
    import importlib.util
    spec=importlib.util.spec_from_file_location('quest_hand_builder',ROOT/'scripts/quest/builder.py')
    builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)
    game=builder.find_game('auto')
    subprocess.run([str(ROOT/'dependencies/build/umodel/umodel.exe'),'-export','-psk','-notex','-noanim',
        '-out='+str(ROOT/'build/work/hand77'),'-path='+str(game/'Content'),'Maps/1-Medical.bsm','NEWPlayerHands','SkeletalMesh'],check=True,capture_output=True)
    if not path.exists():raise RuntimeError('Original hand mesh export not found')
    return path

def read(path):
    data=path.read_bytes();chunks={};at=0
    while at+32<=len(data):
        name=data[at:at+20].split(b'\0')[0].decode();size,count=struct.unpack_from('<ii',data,at+24)
        body=at+32;chunks[name]=(size,count,data[body:body+size*count]);at=body+size*count
    return data,chunks

def main():
    path=ensure_psk()
    data,chunks=read(path);size,n,bones=chunks['REFSKELT'];parents=[];names=[]
    for i in range(n):
        p=i*size;names.append(bones[p:p+64].split(b'\0')[0].decode());parents.append(struct.unpack_from('<i',bones,p+72)[0])
    wrist=names.index('Bip01_L_Hand');left={wrist}
    for i in range(n):
        p=i
        for _ in range(n):
            if p in left:left.add(i);break
            if parents[p]==p:break
            p=parents[p]
    weights={};size,n,raw=chunks['RAWWEIGHTS']
    for i in range(n):
        weight,point,bone=struct.unpack_from('<fii',raw,i*size)
        if bone in left:weights[point]=weights.get(point,0)+weight
    size,n,wedges=chunks['VTXW0000'];allowed=[]
    for i in range(n):allowed.append(weights.get(struct.unpack_from('<H',wedges,i*size)[0],0)>=.45)
    bits=[0]*((n+63)//64)
    for i,value in enumerate(allowed):
        if value:bits[i//64]|=1<<(i%64)
    size,faces,raw=chunks['FACE0000'];keys=[];kept=0
    for i in range(faces):
        tri=struct.unpack_from('<HHH',raw,i*size);a,b,c=sorted(tri);keys.append(a|(b<<16)|(c<<32))
        kept+=all(allowed[j] for j in tri)
    digest=1469598103934665603
    for key in sorted(keys):digest=((digest^key)*1099511628211)&((1<<64)-1)
    assert n==2180 and faces==3588 and 500<kept<2000
    text=['// Generated from the owned game asset; index/weight metadata only.',
        '// PSK SHA256: '+hashlib.sha256(data).hexdigest(),'namespace handmaskdata {',
        'inline constexpr unsigned vertices=%d,triangles=%d,palmTriangles=%d;'%(n,faces,kept),
        'inline constexpr unsigned long long topology=0x%016xULL;'%digest,
        'inline constexpr unsigned long long left[]={',
        *('    0x%016xULL,'%v for v in bits),'};','}']
    (ROOT/'source/native/src/handmask.inc').write_text('\n'.join(text)+'\n')
    print('Verified left hand mask:',sum(allowed),'vertices;',kept,'of',faces,'triangles; topology',hex(digest))

if __name__=='__main__':main()
