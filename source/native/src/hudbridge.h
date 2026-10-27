#pragma once
#include "hudlayout.h"
struct IDirect3DDevice9;struct IDirect3DTexture9;
namespace hudbridge {
bool Present(IDirect3DDevice9*,IDirect3DTexture9*,const hudlayout::Source&);
void Release();
}
