#pragma once
#include <d3d9.h>
#include <vector>

// Preserve the game's complete vertex program (materials, model transforms,
// skinning). Move only its final clip-space position to the other eye. This is
// geometry stereo: no reconstructed silhouette or capped depth for the gun.
namespace vertexstereo {
struct Program { std::vector<DWORD> code; unsigned constant=0,temp=0,sampler=0; bool textureParams=false; const char* error=nullptr; };
inline unsigned Type(DWORD token){return ((token&D3DSP_REGTYPE_MASK)>>D3DSP_REGTYPE_SHIFT)|((token&D3DSP_REGTYPE_MASK2)>>D3DSP_REGTYPE_SHIFT2);}
inline DWORD Register(DWORD type,unsigned number){return 0x80000000u|((type&7)<<28)|((type&24)<<8)|number;}
inline Program Patch(const DWORD* input,size_t count){
    Program p;
    auto fail=[&](const char* why){p.error=why;p.code.clear();return p;};
    if(!input||count<2||count>16384||(input[0]>>16)!=0xfffe)return fail("not a bounded vertex program");
    unsigned version=D3DSHADER_VERSION_MAJOR(input[0]);
    if(version<2||version>3)return fail("vertex model must be 2 or 3");
    DWORD output=Register(version==3?D3DSPR_OUTPUT:D3DSPR_RASTOUT,0);
    bool foundOutput=version==2,foundEnd=false,written=false;
    bool temps[32]={},constants[256]={},samplers[4]={},relative=false;
    for(size_t i=1;i<count;){
        DWORD op=input[i]&D3DSI_OPCODE_MASK;
        if(op==D3DSIO_END){foundEnd=i==count-1;break;}
        if(op==D3DSIO_COMMENT){size_t n=(input[i]&D3DSI_COMMENTSIZE_MASK)>>D3DSI_COMMENTSIZE_SHIFT;if(i+1+n>count)return fail("truncated shader comment");i+=1+n;continue;}
        unsigned n=(input[i]&D3DSI_INSTLENGTH_MASK)>>D3DSI_INSTLENGTH_SHIFT;
        if(i+1+n>count)return fail("truncated shader instruction");
        if(!n&&op!=D3DSIO_NOP&&op!=D3DSIO_ELSE&&op!=D3DSIO_ENDIF&&op!=D3DSIO_ENDLOOP&&op!=D3DSIO_ENDREP&&op!=D3DSIO_BREAK)return fail("unknown zero-operand instruction");
        if(op==D3DSIO_CALL||op==D3DSIO_CALLNZ||op==D3DSIO_RET||op==D3DSIO_LABEL)return fail("shader subroutines need explicit epilogues");
        if(op==D3DSIO_DCL&&version==3&&n==2&&Type(input[i+2])==D3DSPR_OUTPUT&&
           (input[i+1]&(D3DSP_DCL_USAGE_MASK|D3DSP_DCL_USAGEINDEX_MASK))==D3DDECLUSAGE_POSITION){output=input[i+2]&(D3DSP_REGNUM_MASK|D3DSP_REGTYPE_MASK|D3DSP_REGTYPE_MASK2);output|=0x80000000u;foundOutput=true;}
        unsigned last=(op==D3DSIO_DEF||op==D3DSIO_DEFI||op==D3DSIO_DEFB)?1:n;
        for(unsigned j=op==D3DSIO_DCL?2:1;j<=last;j++){
            DWORD token=input[i+j];unsigned type=Type(token),reg=token&D3DSP_REGNUM_MASK;
            if(token&D3DSHADER_ADDRESSMODE_MASK)relative=true;
            if(type==D3DSPR_TEMP&&reg<32)temps[reg]=true;
            if(type==D3DSPR_CONST&&reg<256)constants[reg]=true;
            if(type==D3DSPR_SAMPLER&&reg<4)samplers[reg]=true;
        }
        i+=1+n;
    }
    if(!foundEnd||!foundOutput)return fail("missing final position or shader end");
    int temp=-1,constant=-1;
    for(unsigned i=0;i<(version==2?12u:32u);i++)if(!temps[i]){temp=(int)i;break;}
    for(int i=0;i<256;i++)if(!constants[i]){constant=i;break;}
    int parameterTemp=-1,sampler=-1;
    if(relative){
        // Skeletal programs dynamically index the bone constant bank. A
        // seemingly unused c-register can still contain a bone. Fetch the
        // two eye parameters from a separate 1x1 vertex texture instead;
        // the original skinning constants remain completely untouched.
        if(version!=3)return fail("relative VS2 needs a vertex-texture capable program");
        for(unsigned i=0;i<32;i++)if(!temps[i]&&(int)i!=temp){parameterTemp=(int)i;break;}
        for(unsigned i=0;i<4;i++)if(!samplers[i]){sampler=(int)i;break;}
        if(parameterTemp<0||sampler<0)return fail("no spare skinning parameter registers");
        p.textureParams=true;p.sampler=sampler;
    }
    if(temp<0||(!relative&&constant<0))return fail("no unused stereo registers");
    p.temp=temp;p.constant=constant<0?0:constant;p.code.assign(input,input+count-1);
    const DWORD identity=D3DSP_REGNUM_MASK|D3DSP_REGTYPE_MASK|D3DSP_REGTYPE_MASK2;
    for(size_t i=1;i<p.code.size();){
        DWORD op=p.code[i]&D3DSI_OPCODE_MASK;
        unsigned n=op==D3DSIO_COMMENT?(p.code[i]&D3DSI_COMMENTSIZE_MASK)>>D3DSI_COMMENTSIZE_SHIFT:(p.code[i]&D3DSI_INSTLENGTH_MASK)>>D3DSI_INSTLENGTH_SHIFT;
        if(op!=D3DSIO_COMMENT&&op!=D3DSIO_DCL&&op!=D3DSIO_DEF&&op!=D3DSIO_DEFI&&op!=D3DSIO_DEFB)
            for(unsigned j=1;j<=n;j++)if((p.code[i+j]&identity)==(output&identity)){
                p.code[i+j]=(p.code[i+j]&~identity)|Register(D3DSPR_TEMP,temp);written=true;
            }
        i+=1+n;
    }
    if(!written)return fail("position is never written");
    DWORD t=Register(D3DSPR_TEMP,temp),c=Register(D3DSPR_CONST,constant);
    auto emit=[&](std::initializer_list<DWORD> words){p.code.insert(p.code.end(),words);};
    if(relative){
        p.code.insert(p.code.begin()+1,{(2u<<24)|D3DSIO_DCL,0x80000000u|D3DSTT_2D,
                                      Register(D3DSPR_SAMPLER,sampler)|D3DSP_WRITEMASK_ALL});
        c=Register(D3DSPR_TEMP,parameterTemp);
        // All coordinates/LOD select the same clamped single texel.
        emit({(3u<<24)|D3DSIO_TEXLDL,c|D3DSP_WRITEMASK_ALL,t|D3DSP_NOSWIZZLE,
              Register(D3DSPR_SAMPLER,sampler)|D3DSP_NOSWIZZLE});
    }
    // GPS relocation changes homogeneous depth without changing its native
    // x/w and y/w. Weapons pass exactly 1, preserving their physical scale.
    emit({(3u<<24)|D3DSIO_MUL,t|D3DSP_WRITEMASK_ALL,t|D3DSP_NOSWIZZLE,c|0x00aa0000u});
    emit({(3u<<24)|D3DSIO_ADD,t|D3DSP_WRITEMASK_0,t,c}); // clip.x += eye offset * focal length
    // Independent, smaller weapon near plane; z'=w+scale*(z-w). It does not
    // move the world near plane or change physical size, eye separation or w.
    emit({(3u<<24)|D3DSIO_ADD,t|D3DSP_WRITEMASK_2,t|0x00aa0000u,t|0x01ff0000u});
    emit({(4u<<24)|D3DSIO_MAD,t|D3DSP_WRITEMASK_2,t|0x00aa0000u,c|0x00550000u,t|0x00ff0000u});
    // VS2 requires a complete position write, rather than partial oPos masks.
    emit({(2u<<24)|D3DSIO_MOV,output|D3DSP_WRITEMASK_ALL,t|D3DSP_NOSWIZZLE});
    p.code.push_back(D3DSIO_END);return p;
}
}
