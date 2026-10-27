#pragma once

namespace stallsampler {
// Call on the game's main thread; samples it until Stop().
void Start();
void Stop();
}
