#pragma once
#include "hudlayout.h"
#include "xrapi.h"
struct IDirect3DDevice9;
struct IDirect3DTexture9;
namespace gameplayhud {
struct Options {bool ammoOnLeft=false,loot=false;};
// No input injection. Uses the exact head/hand snapshot associated with the world.
bool Render(IDirect3DDevice9*,IDirect3DTexture9*,const hudlayout::Source&,
            const hudlayout::Layout&,const xrapi::State&,float fov,int views,float scale,Options options={});
void Release();
}
