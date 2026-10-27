#pragma once
#include <d3d9.h>
namespace weaponlayer {
using DrawIndexedFn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT);
using DrawPrimitiveFn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT);
using DrawUPFn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,const void*,UINT);
// Called for the selected named model in every native mesh pass. The layer
// validates the actual target and owns safe fallback to the original draw.
// Optional navigation-only homogeneous relocation preserves original screen
// size/position and material. Ordinary weapons always use the default 1.
bool DrawIndexed(IDirect3DDevice9*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT,DrawIndexedFn,float relocation=1);
bool DrawPrimitive(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT,DrawPrimitiveFn,float relocation=1);
// Verified original hand topology; only left palm/fingers, never the arms.
bool DrawPlasmidHand(IDirect3DDevice9*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT,DrawIndexedFn);
void Composite(IDirect3DDevice9*,IDirect3DTexture9* worldDepth=nullptr);
bool SourceRemovalReady(); // legacy diagnostic: a complete per-eye image was displayed
// Replay the game's own postprocessing with per-eye color inputs. Same shader,
// constants, viewport and UVs; small exposure textures stay shared with world.
void ReplayPostProcess(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,const void*,UINT,DrawUPFn);
struct ReplayWork {unsigned calls=0,textureReads=0,replayedDraws=0;};
ReplayWork TakeReplayWork(); // bounded diagnostic counters; no GPU readback
void EndFrame();
void InvalidateModel();
void Release();
}
