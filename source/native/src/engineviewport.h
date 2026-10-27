#pragma once
namespace engineviewport {
// Start discovery only after D3D device creation: an asleep headset may delay
// that creation indefinitely after the camera DLL has been loaded.
void DeviceCreated();
bool Ready();
bool SetSize(unsigned width,unsigned height,unsigned backWidth,unsigned backHeight);
bool Matches(unsigned width,unsigned height);
unsigned Height();
unsigned Width();
// Native TEMPSETRES, called only from the game's window thread, outside Present.
bool Resize(unsigned width,unsigned height,bool windowed);
// False while level travel has detached UPlayer::Actor.
bool CanResize();
}
