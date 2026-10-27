# Third-party notices

- **WinlatorXR dawn42:** [source](https://github.com/WinlatorXR/WinlatorXR/tree/winlatorxr_dawn42), fork changes GPLv3 and upstream MIT; [license](licenses/WinlatorXR.txt). The APK retains the upstream runtime; port additions are in `source/android`.
- **BioShock Trilogy VR:** [source reference](https://github.com/VR-Stereo-Hub/bioshock-trilogy-vr/tree/41f004bc6a1e4fa8be0d0e0749f9ac5a0453e75e), MIT; [adaptation notes](source/native/third_party/bioshock-vr/README.md).
- **MinHook:** source and BSD-style notice in `dependencies/build/TsudaKageyu-minhook-c3fcafd`.
- **OpenXR headers:** Khronos, Apache-2.0/MIT; source commit and license in `dependencies/build/openxr`.
- **OpenVR:** Valve; notice in `source/native/third_party/openvr`. This release targets standalone Quest.
- **Touch hand/controller reference assets:** Amazon MIT notice in `source/native/assets/LICENSE.md`.
- **Steamless 3.1.0.5:** [upstream](https://github.com/atom0s/Steamless/tree/v3.1.0.5), CC BY-NC-ND 4.0; [notice](licenses/Steamless.txt). Bundled unchanged in the installer.
- **Android platform-tools / ADB:** [source](https://android.googlesource.com/platform/packages/modules/adb/), [bundled notices](licenses/ADB-NOTICE.txt).
- **Meta ADB driver 2.0:** [upstream download](https://developers.meta.com/vr/downloads/package/oculus-adb-drivers/); its original driver files and terms are retained inside the installer.

Port modifications are supplied under GPLv3; third-party components retain their own licenses. BioShock game content is not included in the installer or repository as a playable game; the installer prepares the user's existing original installation. Embedded native lookup tables and reference geometry are part of the mod source and are not a license to the original game assets.
