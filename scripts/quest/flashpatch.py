"""Targeted AVM1 changes to the original HUD; never re-encode graphics/fonts."""
from dataclasses import dataclass
import struct
import zlib

@dataclass
class Action:
    start:int
    end:int
    op:int
    values:tuple=()

@dataclass
class Function:
    name:str
    body:int
    size:int
    pool:tuple
    where:str

def unpack(data):
    if data[:3]==b'CWS':data=b'FWS'+data[3:8]+zlib.decompress(data[8:])
    if data[:3]!=b'FWS' or len(data)<12 or struct.unpack_from('<I',data,4)[0]!=len(data):
        raise ValueError('Unsupported/incomplete SWF')
    return data

def actions(data,start,end,pool=(),functions=None,where=''):
    result=[];p=start
    while p<end:
        first=p;op=data[p];p+=1
        if op==0:break
        size=0
        if op>=128:
            size=struct.unpack_from('<H',data,p)[0];p+=2
        stop=p+size
        if stop>end:raise ValueError('Action escapes its container')
        values=[]
        if op==0x88:
            n=struct.unpack_from('<H',data,p)[0];q=p+2;strings=[]
            for _ in range(n):
                e=data.index(0,q,stop);strings.append(data[q:e].decode('utf-8'));q=e+1
            pool=tuple(strings)
        elif op==0x96:
            q=p
            while q<stop:
                kind=data[q];q+=1
                if kind==0:
                    e=data.index(0,q,stop);values.append(data[q:e].decode('utf-8'));q=e+1
                elif kind in (8,9):
                    i=data[q] if kind==8 else struct.unpack_from('<H',data,q)[0];q+=1 if kind==8 else 2
                    values.append(pool[i])
                elif kind==4:values.append(('reg',data[q]));q+=1
                elif kind==5:values.append(bool(data[q]));q+=1
                elif kind in (2,3):values.append(None)
                elif kind in (1,7):values.append(struct.unpack_from('<I',data,q)[0]);q+=4
                elif kind==6:values.append(('double',));q+=8
                else:raise ValueError('Unknown Push encoding')
        elif op in (0x9b,0x8e):
            e=data.index(0,p,stop);name=data[p:e].decode('utf-8');q=e+1
            n=struct.unpack_from('<H',data,q)[0];q+=2
            if op==0x8e:q+=3
            for _ in range(n):
                if op==0x8e:q+=1
                q=data.index(0,q,stop)+1
            length=struct.unpack_from('<H',data,q)[0];body=q+2
            if body+length>end:raise ValueError('Function escapes action stream')
            if functions is not None:functions.append(Function(name,body,length,pool,where))
            stop=body+length
        result.append(Action(first,stop,op,tuple(values)));p=stop
    return result

def functions(data):
    found=[]
    def tags(p,end,where):
        while p+2<=end:
            h=struct.unpack_from('<H',data,p)[0];p+=2;kind=h>>6;size=h&63
            if size==63:size=struct.unpack_from('<I',data,p)[0];p+=4
            stop=p+size
            if stop>end:raise ValueError('Tag escapes SWF')
            if kind in (12,59):actions(data,p+(2 if kind==59 else 0),stop,functions=found,where=where)
            if kind==39:tags(p+4,stop,where+'/'+str(struct.unpack_from('<H',data,p)[0]))
            p=stop
            if kind==0:break
    bits=data[8]>>3;start=8+(5+bits*4+7)//8+4
    tags(start,len(data),'root');return found

def placements(data):
    """Named PlaceObject2 clips, retaining byte offsets and sprite ownership."""
    found=[]
    def bits_end(p,color=False):
        bit=p*8
        def take(n):
            nonlocal bit
            value=0
            for _ in range(n):value=(value<<1)|((data[bit//8]>>(7-bit%8))&1);bit+=1
            return value
        if color:
            add=take(1);mult=take(1);n=take(4)
            take(n*4*(add+mult))
        else:
            if take(1):take(take(5)*2)
            if take(1):take(take(5)*2)
            take(take(5)*2)
        return (bit+7)//8
    def tags(p,end,parent=0):
        while p+2<=end:
            h=struct.unpack_from('<H',data,p)[0];p+=2;kind=h>>6;size=h&63
            if size==63:size=struct.unpack_from('<I',data,p)[0];p+=4
            stop=p+size
            if stop>end:raise ValueError('Placement escapes SWF')
            if kind==26:
                flags=data[p];q=p+3;character=0
                if flags&2:character=struct.unpack_from('<H',data,q)[0];q+=2
                if flags&4:
                    first=q;q=bits_end(q);last=q
                    if flags&8:q=bits_end(q,True)
                    if flags&16:q+=2
                    if flags&32:
                        name=data[q:data.index(0,q,stop)].decode('utf-8')
                        found.append((name,first,last,parent,character))
            if kind==39:tags(p+4,stop,struct.unpack_from('<H',data,p)[0])
            p=stop
            if kind==0:break
    bits=data[8]>>3;tags(8+(5+bits*4+7)//8+4,len(data))
    return found

def training_matrices(data):
    return [(start,end) for name,start,end,parent,character in placements(data) if name=='trainingTextHolder']

def enemy_health_matrices(data):
    records=placements(data)
    center=[character for name,start,end,parent,character in records if name=='centerBox' and parent==0]
    if len(center)!=1:raise ValueError('Expected one root centerBox')
    return [(name,start,end) for name,start,end,parent,character in records
            if parent==center[0] and name in ('EnemyType','FrozenHealth')]

def collapsed_matrix(size):
    # Valid SWF MATRIX with scale X/Y=0, no rotation, zero translation.
    # Preserve its byte length, so every tag/action/font offset is unchanged.
    for n in range(1,32):
        bits='1'+format(n,'05b')+'0'*(n*2)+'0'+'00001'+'00'
        if (len(bits)+7)//8==size:
            bits=bits.ljust(size*8,'0')
            return int(bits,2).to_bytes(size,'big')
    raise ValueError('Cannot preserve clip MATRIX length')

def patch(data,hide_training=True,hide_reticle=True,hide_enemy_health=False):
    raw=unpack(data);output=bytearray(raw);all_functions=functions(raw);report=[]
    def get(name):
        matches=[f for f in all_functions if f.name==name and f.where=='root']
        if len(matches)!=1:raise ValueError('Expected unique HUD function '+name)
        return matches[0]
    def erase(start,end,label):
        # Returning undefined exits only the selected handler. No offsets,
        # tags, font/vector data or other dispatch branches change.
        if end-start<5:raise ValueError('Handler too short')
        # Keep valid instructions after Return, rather than an embedded
        # ActionEnd which some loaders treat as the end of the whole stream.
        output[start:end]=b'\x96\x01\x00\x03\x3e'+b'\x17'*(end-start-5)
        report.append({'handler':label,'start':start,'end':end})
    if hide_training:
        f=get('HUDMessage');stream=actions(raw,f.body,f.body+f.size,f.pool);matches=[]
        for i,a in enumerate(stream[:-3]):
            if a.op==0x96 and a.values==( ('reg',3),'Training') and [x.op for x in stream[i+1:i+4]]==[0x49,0x12,0x9d]:
                jump=stream[i+3];target=jump.end+struct.unpack_from('<h',raw,jump.end-2)[0]
                if not jump.end<target<=f.body+f.size:raise ValueError('Invalid Training branch')
                matches.append((jump.end,target))
        if len(matches)!=2:raise ValueError('Training dispatch branches changed')
        for index,region in enumerate(matches):erase(*region,'HUDMessage/Training'+str(index))
        for name in ('HelpTagAccessActive','ShowHelpTagInfoNow'):
            f=get(name);erase(f.body,f.body+f.size,name)
        matrices=training_matrices(raw)
        if len(matrices)!=1:raise ValueError('Expected one named trainingTextHolder placement')
        for start,end in matrices:
            output[start:end]=collapsed_matrix(end-start)
            report.append({'handler':'trainingTextHolder/NoVisiblePanel','start':start,'end':end})
    if hide_reticle:
        f=get('SetReticleInfo');stream=actions(raw,f.body,f.body+f.size,f.pool);match=[]
        for a,b in zip(stream,stream[1:]):
            if a.op==0x96 and a.values==('LockType',) and b.op==0x1c:match.append((a.start,b.end))
        if len(match)!=1 or 'NoReticle' not in f.pool:raise ValueError('Native reticle dispatch changed')
        start,end=match[0];replacement=b'\x96\x03\x00\x09'+struct.pack('<H',f.pool.index('NoReticle'))
        if end-start not in (6,7):raise ValueError('Unexpected LockType parameter encoding')
        output[start:end]=replacement+b'\x4b'*(end-start-len(replacement))
        report.append({'handler':'SetReticleInfo/NoReticle','start':start,'end':end})
    if hide_enemy_health:
        for name in ('SetHealthOfFocus','SetFrozenHealthOfFocus'):
            f=get(name);erase(f.body,f.body+f.size,name)
        matrices=enemy_health_matrices(raw)
        if sorted(name for name,start,end in matrices)!=['EnemyType','FrozenHealth']:
            raise ValueError('Expected normal and frozen focus-health clips in centerBox')
        for name,start,end in matrices:
            output[start:end]=collapsed_matrix(end-start)
            report.append({'handler':name+'/NoEnemyHealthPanel','start':start,'end':end})
    if len(output)!=len(raw):raise AssertionError('SWF layout changed')
    # Preserve the input compression format and complete native movie.
    result=b'CWS'+bytes(output[3:8])+zlib.compress(output[8:]) if data[:3]==b'CWS' else bytes(output)
    return result,report
