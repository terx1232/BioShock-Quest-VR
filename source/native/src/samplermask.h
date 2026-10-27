#pragma once
#include <d3d9.h>
#include <cstddef>

// PS2/3 sampler registers identify the only texture stages the program can
// read. Keep all stages for fixed function, older or unrecognised bytecode.
// https://learn.microsoft.com/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-registers-sampler
namespace samplermask {
constexpr unsigned all=0xffff;
inline unsigned Read(const DWORD* code,size_t count){
    if(!code||count<2||count>16384||(code[0]>>16)!=0xffff)return all;
    unsigned version=D3DSHADER_VERSION_MAJOR(code[0]);
    if(version<2||version>3)return all;
    unsigned mask=0;
    for(size_t i=1;i<count;){
        unsigned op=code[i]&D3DSI_OPCODE_MASK;
        if(op==D3DSIO_END)return i==count-1?mask:all;
        if(op==D3DSIO_COMMENT){
            size_t n=(code[i]&D3DSI_COMMENTSIZE_MASK)>>D3DSI_COMMENTSIZE_SHIFT;
            if(n>count-i-1)return all;
            i+=n+1;continue;
        }
        unsigned n=(code[i]&D3DSI_INSTLENGTH_MASK)>>D3DSI_INSTLENGTH_SHIFT;
        if(op>D3DSIO_BREAKP||n>count-i-1)return all;
        if((op==D3DSIO_DCL&&n!=2)||((op==D3DSIO_DEF||op==D3DSIO_DEFI)&&n!=5)||
           (op==D3DSIO_DEFB&&n!=2))return all;
        if(!n&&op!=D3DSIO_NOP&&op!=D3DSIO_RET&&op!=D3DSIO_ELSE&&op!=D3DSIO_ENDIF&&
           op!=D3DSIO_ENDLOOP&&op!=D3DSIO_ENDREP&&op!=D3DSIO_BREAK)return all;
        // DEF's float/int/bool values and DCL's semantic token are not register
        // operands. In particular their bits must not invent texture stages.
        unsigned last=(op==D3DSIO_DEF||op==D3DSIO_DEFI||op==D3DSIO_DEFB)?1:n;
        for(unsigned j=op==D3DSIO_DCL?2:1;j<=last;j++){
            DWORD token=code[i+j];
            unsigned type=((token&D3DSP_REGTYPE_MASK)>>D3DSP_REGTYPE_SHIFT)|
                          ((token&D3DSP_REGTYPE_MASK2)>>D3DSP_REGTYPE_SHIFT2);
            if(type==D3DSPR_SAMPLER){
                unsigned reg=token&D3DSP_REGNUM_MASK;
                if(reg>=16||(token&D3DSHADER_ADDRESSMODE_MASK))return all;
                mask|=1u<<reg;
            }
        }
        i+=n+1;
    }
    return all;
}
}
