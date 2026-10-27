#pragma once
#include <d3d9.h>
// BioShock's Flash renderer writes RGB only on an opaque game backbuffer.
// In a transparent target we must also accumulate coverage, independently of
// the original RGB blend. Restore immediately: the engine caches its states.
struct HudCoverage {
    IDirect3DDevice9* d;
    static constexpr D3DRENDERSTATETYPE keys[]={D3DRS_COLORWRITEENABLE,D3DRS_SEPARATEALPHABLENDENABLE,
        D3DRS_SRCBLENDALPHA,D3DRS_DESTBLENDALPHA,D3DRS_BLENDOPALPHA};
    DWORD old[5]{},changed=0;
    explicit HudCoverage(IDirect3DDevice9* device,bool capturing):d(device){
        if(!capturing)return;
        d->GetRenderState(keys[0],&old[0]);
        if(!(old[0]&7))return; // stencil/mask-only draws must remain colorless
        const DWORD desired[]={old[0]|8,TRUE,D3DBLEND_ONE,D3DBLEND_INVSRCALPHA,D3DBLENDOP_ADD};
        for(unsigned i=0;i<5;i++){
            if(i)d->GetRenderState(keys[i],&old[i]);
            if(old[i]!=desired[i]){changed|=1u<<i;d->SetRenderState(keys[i],desired[i]);}
        }
    }
    ~HudCoverage(){for(unsigned i=0;i<5;i++)if(changed&(1u<<i))d->SetRenderState(keys[i],old[i]);}
};
