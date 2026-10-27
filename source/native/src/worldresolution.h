#pragma once
#include <windows.h>
namespace worldresolution {
using Resize=bool(*)(unsigned,unsigned,bool);
using Safety=bool(*)();
void Attach(HWND window,unsigned menuWidth,unsigned menuHeight,unsigned worldWidth,unsigned worldHeight,Resize resize,bool worldWindowed=false,Safety safety=nullptr);
void DeviceReady(unsigned width,unsigned height);
// True only once the requested game-sized device exists (or bounded fallback).
bool WorldReady();
bool MenuReady();
unsigned MenuWidth();
unsigned MenuHeight();
void Update(bool inGame,DWORD now);
}
