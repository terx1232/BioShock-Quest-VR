#pragma once
#include <d3d9.h>
#include "xrapi.h"

namespace pcvr {
bool Enabled();
bool Start();
xrapi::State Get();
void Mode(bool stereo, float hapticLeft, float hapticRight);
void DeviceCreated(IDirect3DDevice9* device);
void Submit(IDirect3DDevice9* device, bool stereo, unsigned long long frameId);
void Release();
}
