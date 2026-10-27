#pragma once
namespace flashpointer {
// Original 2007 FlashMovie input; never uses viewport CursorX/CursorY.
bool Install();
bool Ready();
void Target(bool active,int x,int y,int width,int height);
}
