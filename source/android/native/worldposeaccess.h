#pragma once
#include "worldposecache.h"
namespace worldpose {
// GL-thread caller; returns only a pose in the current reference-space epoch.
int SelectImage(unsigned tag,Pair& pair,unsigned* flags=nullptr);
}
