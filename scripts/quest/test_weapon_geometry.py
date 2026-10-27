"""Actual weapon geometry stereo on a hidden host GPU window; no game input."""
from pathlib import Path
import importlib.util
import subprocess
import struct
import ctypes
import re
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/tests/quest-weapon-geometry';OUT.mkdir(exist_ok=True)
hand_spec=importlib.util.spec_from_file_location('quest_hand_geometry_asset',ROOT/'scripts/quest/handmask.py')
hand=importlib.util.module_from_spec(hand_spec);hand_spec.loader.exec_module(hand)
_,chunks=hand.read(hand.ensure_psk())
size,count,faces=chunks['FACE0000'];indices=[]
for i in range(count):indices.extend(struct.unpack_from('<HHH',faces,i*size))
(OUT/'hand-indices.bin').write_bytes(struct.pack('<%dH'%len(indices),*indices))
size,count,raw=chunks['RAWWEIGHTS'];weights={}
for i in range(count):
    weight,point,bone=struct.unpack_from('<fii',raw,i*size)
    if 6<=bone<=21:weights[point]=weights.get(point,0)+weight
size,count,wedges=chunks['VTXW0000']
(OUT/'hand-flags.bin').write_bytes(bytes(int(weights.get(struct.unpack_from('<H',wedges,i*size)[0],0)>=.45) for i in range(count)))
# Validate production routing against real x86 call instructions, not guessed
# topology addresses. Each return site follows the matching D3D9 vtable call.
exe=ROOT/'dist/quest/BioshockMQ2/Builds/Release/Bioshock.exe'
if exe.exists():
    image=exe.read_bytes();pe=struct.unpack_from('<I',image,0x3c)[0]
    sections=struct.unpack_from('<H',image,pe+6)[0];optional=pe+24
    base=struct.unpack_from('<I',image,optional+28)[0]
    table=optional+struct.unpack_from('<H',image,pe+20)[0]
    def at(address,count):
        rva=address-base
        for i in range(sections):
            _,size,virtual,raw_size,raw=struct.unpack_from('<8sIIII',image,table+i*40)
            if virtual<=rva and rva+count<=virtual+raw_size:return image[raw+rva-virtual:raw+rva-virtual+count]
        raise AssertionError(f'Unmapped game code {address:x}')
    for site,opcode,slot in [(0x10b917b4,0xd2,82),(0x10b91818,0xd1,82),(0x10b91888,0xd1,82),
                              (0x10b918d3,0xd1,82),(0x10b917cd,0xd0,81),(0x10b91838,0xd1,81)]:
        assert at(site-2,2)==bytes([0xff,opcode]),f'Changed mesh call at {site:x}'
        assert struct.pack('<I',slot*4) in at(site-32,30),f'Wrong D3D9 draw slot at {site:x}'
    print('PASS: all six mesh routes match actual BioShock EXE draw instructions')
spec=importlib.util.spec_from_file_location('shader_compile',ROOT/'source/native/tools/compile_shaders.py')
compiler_module=importlib.util.module_from_spec(spec);spec.loader.exec_module(compiler_module)
vertex='''float4 rows[4]:register(c0);
struct O {float4 p:POSITION;float4 color:COLOR0;};
O main(float4 p:POSITION,float4 c:COLOR0){O o;
o.p=float4(dot(p,rows[0]),dot(p,rows[1]),dot(p,rows[2]),dot(p,rows[3]));
o.color=c;return o;}'''
pixel='float4 main(float4 c:COLOR0):COLOR{return float4(c.rgb,0);}'
for name,target,source in [('weapon-vs2','vs_2_0',vertex),('weapon-vs3','vs_3_0',vertex),
    ('weapon-vs3-branch','vs_3_0',vertex.replace('o.color=c;','if(p.x>0)o.color=c;else o.color=c*float4(1,1,1,0);')),
    ('weapon-vs3-skin','vs_3_0','float4 bones[24]:register(c4);\n'+vertex.replace('O o;','O o;p.xyz+=bones[(int)c.a].xyz;')),
    ('weapon-ps','ps_3_0',pixel),('weapon-hdr-ps','ps_3_0',pixel.replace('c.rgb','c.rgb*4')),
    ('post-half','ps_3_0','sampler2D s:register(s0);float4 main(float2 uv:TEXCOORD0):COLOR{return tex2D(s,uv)*.5;}'),
    ('post-tone','ps_3_0','sampler2D s:register(s0);sampler2D exposure:register(s1);float4 main(float2 uv:TEXCOORD0):COLOR{float3 c=tex2D(s,uv).rgb*tex2D(exposure,float2(.5,.5)).r;return float4(c/(1+c),0);}')]:
    path=OUT/(name+'.hlsl');path.write_text(source)
    (OUT/(name+'.cso')).write_bytes(compiler_module.compile_hlsl(path,target=target))
clang=next((ROOT/'dependencies/build').glob('llvm-mingw*/bin/i686-w64-mingw32-clang++.exe'))
target=OUT/'weapon-geometry.exe'
subprocess.run([str(clang),'-O2','-std=c++17','-static',str(ROOT/'source/native/tests/weapon_geometry.cpp'),
                str(ROOT/'source/native/src/weaponlayer.cpp'),'-o',str(target),'-ld3d9','-ldxguid'],check=True)
args=[]
cache=ROOT/'dist/quest/BioshockMQ2/Builds/Release/ShaderCache.pcs'
if cache.exists():
    data=cache.read_bytes();marker=b'\x00\x03\xfe\xff';start=0;programs=[]
    folder=OUT/'native-shaders';folder.mkdir(exist_ok=True)
    while (start:=data.find(marker,start))>=0:
        cursor=start+4;limit=min(len(data),start+65536)
        while cursor+4<=limit:
            token=struct.unpack_from('<I',data,cursor)[0];op=token&65535
            if op==65535:
                path=folder/(str(len(programs))+'.cso');path.write_bytes(data[start:cursor+4]);programs.append(str(path));break
            count=(token>>16)&32767 if op==65534 else (token>>24)&15
            if not count and op not in (0,27,28,29,38,39,42,43,44):break
            cursor+=4*(1+count)
        start+=4
    manifest=OUT/'native-shaders.txt';manifest.write_text('\n'.join(programs)+'\n');args=[str(manifest)]
    # Independently verify usage against Microsoft's disassembler for the
    # actual game's pixel shaders, rather than mirroring the new C++ decoder.
    compiler=ctypes.WinDLL('d3dcompiler_47.dll');disassemble=compiler.D3DDisassemble
    disassemble.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_uint,ctypes.c_char_p,ctypes.POINTER(ctypes.c_void_p)]
    disassemble.restype=ctypes.c_long
    pixels=[];pixel_seen=set()
    start=0;marker=b'\x00\x03\xff\xff'
    while (start:=data.find(marker,start))>=0:
        cursor=start+4;limit=min(len(data),start+65536);program=None
        while cursor+4<=limit:
            token=struct.unpack_from('<I',data,cursor)[0];op=token&65535
            if op==65535:program=data[start:cursor+4];break
            count=(token>>16)&32767 if op==65534 else (token>>24)&15
            if not count and op not in (0,27,28,29,38,39,42,43,44):break
            cursor+=4*(1+count)
        start+=4
        if not program or program in pixel_seen:continue
        pixel_seen.add(program)
        buffer=ctypes.create_string_buffer(program);blob=ctypes.c_void_p()
        if disassemble(buffer,len(program),0,None,ctypes.byref(blob))!=0:continue
        vt=ctypes.cast(ctypes.cast(blob,ctypes.POINTER(ctypes.c_void_p))[0],ctypes.POINTER(ctypes.c_void_p))
        pointer=ctypes.WINFUNCTYPE(ctypes.c_void_p,ctypes.c_void_p)(vt[3])(blob)
        size=ctypes.WINFUNCTYPE(ctypes.c_size_t,ctypes.c_void_p)(vt[4])(blob)
        assembly=ctypes.string_at(pointer,size).decode(errors='replace')
        ctypes.WINFUNCTYPE(ctypes.c_ulong,ctypes.c_void_p)(vt[2])(blob)
        slots={int(x) for x in re.findall(r'^\s*dcl_\w+\s+s(\d+)\b',assembly,re.M)}
        mask=sum(1<<s for s in slots);pixels.append((mask,program))
    assert pixels,'No real native pixel shaders verified'
    # One binary pack avoids thousands of Windows/antivirus file opens.
    pixel_pack=OUT/'native-pixels.bin'
    with pixel_pack.open('wb') as stream:
        stream.write(struct.pack('<I',len(pixels)))
        for mask,program in pixels:stream.write(struct.pack('<II',mask,len(program))+program)
    args.append(str(pixel_pack))
result=subprocess.run([str(target),*args],cwd=OUT,capture_output=True,text=True,timeout=45)
(OUT/'result.log').write_text(result.stdout+result.stderr)
print(result.stdout+result.stderr,end='')
if result.returncode:raise RuntimeError('Weapon geometry stereo regression failed')
