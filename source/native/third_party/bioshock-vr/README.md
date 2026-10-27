# Wrench gesture reference

Quest build 73 adapts the head-relative gesture, re-arm latch, 300 ms cooldown,
120 ms native attack pulse and engine-pitch correction approach from:

- https://github.com/VR-Stereo-Hub/bioshock-trilogy-vr
- Commit: `41f004bc6a1e4fa8be0d0e0749f9ac5a0453e75e`
- Reference files: `src/core/input/swing.cpp`, `src/core/input/xinput_bridge.cpp`,
  `src/game/bioshock1r/camera.cpp`.
- MIT license: [LICENSE](LICENSE).

The Quest adapter is `src/remasteredswing.h`. Its threshold is 1.2 m/s, with
an extra outward-motion guard and tracking-jump rejection. It drives the
original 2007 game's attack command; damage and collision remain native.
The hidden Hands attachment keeps native damage timing. From Quest build 74,
the visible wrench stays fixed to the controller without its native animation.
This is an adaptation, not binary compatibility with the Remastered mod.

`BioVRDev/Bioshock-Remastered-VR` was also inspected as a reference. No code
from that repository was copied; a root license was not found.
