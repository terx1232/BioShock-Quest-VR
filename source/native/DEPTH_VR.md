# Quest native VR

Current profile: `config/quest/profiles/candidate.ini`.
Entry point: `QUEST.bat` (build, GPU and pose checks, backup, install, startup).
`QUEST.bat recover` restores the earlier installation with a visible menu.

Main menu, loading and pause use stereo cinema with a white controller ray;
gameplay reconstructs both eyes from one colour/depth frame. The upper limit
is 72 FPS. Actual FPS and head-turn stability need a headset measurement.

Build 26 adds an Android float32 pose bridge, explicit pose-index metadata,
correct dynamic render size changes and lighter reconstruction at lower size.
The exact Java encoder and C++ decoder are tested together. GPU checks cover
cinema, rays, state restoration, depth/weapon merging, dynamic sizes and Reset.

Verified results and installation status are in `docs/STATUS.md`; acceptance
scenarios are in `docs/QUEST_PLAN.md`.
