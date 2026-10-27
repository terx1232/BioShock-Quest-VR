# BioShock Quest VR

An experimental standalone VR port of **BioShock (2007)** for Meta Quest, built on WinlatorXR. A Windows installer prepares your own game and installs everything over USB.

[**Download the latest installer**](https://github.com/terx1232/BioShock-Quest-VR/releases/latest)

[![Support on Ko-fi](assets/support-kofi.svg)](https://ko-fi.com/kirillbaal)

## Features

- Head tracking, depth-based stereo world and separate 3D weapons.
- VR menu, loading and pause panels with a white pointer ray.
- Weapon and plasmid aiming reticles; player stats on the left wrist.
- One EXE with the APK and installation tools included.

## Install

1. Install the original **BioShock 2007 v1.1** on your Windows PC. Remastered is not compatible. Allow about **7 GB on PC and 14 GB on the headset**.
2. Connect Quest to the PC using a **USB data cable** and allow **USB debugging** in the headset.
3. Download and run **BioShockVR-Setup.exe** from Releases. Choose **Quest 3 / Quest 3S**, then your USB device. The installer finds the game, prepares a separate copy and installs it automatically. Keep USB connected until it finishes.
4. Open **BioShock VR** from **Library → Unknown Sources**. First launch prepares the Windows environment; allow it time to finish.

No separate Python, Java or Android SDK is needed. If USB is missing, rescan or choose the bundled driver option. If the game is not found, select its folder when asked.

## Status

Release **90** has an English installer and the weapon/plasmid reticles from gameplay build 88. Tested during development on Quest 2; Quest 3/3S hardware testing is pending. Steady 72 FPS is not guaranteed. Pico is not included.

## How it works

WinlatorXR runs the Windows game through Wine, CPU translation and DXVK. The native D3D9 mod supplies head/hand tracking, depth stereo, weapon rendering and VR HUD; an Android OpenXR layer presents the world and menu panels. The installer leaves the original Steam folder unchanged.

[Controls](docs/CONTROLS.md) · [Source and building](docs/BUILDING.md) · [Third-party notices](THIRD_PARTY.md)

BioShock belongs to its respective owners. Game files are not included; you need your own copy. Donations are optional.
