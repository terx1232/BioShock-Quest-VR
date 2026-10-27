# Source and building

## Layout

| Folder | Purpose |
| --- | --- |
| `source/native` | 32-bit D3D9 mod, stereo, weapons, HUD, HLSL and tests |
| `source/android` | Java launcher and ARM64 OpenXR presentation layer |
| `scripts/quest` | Game preparation, APK adaptation and developer checks |
| `scripts/distribution` | Offline terminal installer, transaction tests and packer |
| `config/quest` | Release launch settings and VR profile |
| `dependencies/build` | MinHook source and OpenXR headers with their notices |

The release installer also embeds port sources in `PORT_SOURCE.zip`. The commercial game and private APK signing key are excluded from this repository.

## Developer requirements

Use Windows, Python 3.14+, a JDK 17+, Android SDK with platform/build tools and an ARM64 NDK, and [llvm-mingw](https://github.com/mstorsjo/llvm-mingw/releases) for 32-bit Windows. Set `JAVA_HOME` and `ANDROID_HOME`. Extract llvm-mingw under `dependencies/build/llvm-mingw-...`.

Download **WinlatorXR dawn42** from [upstream releases](https://github.com/WinlatorXR/WinlatorXR/releases), apktool 3.0.3 and Steamless 3.1.0.5 into `dependencies/runtime`. Select the dawn42 APK explicitly in the `[tools] winlator_apk` setting; other versions may fail the renderer signature checks. This project preserves upstream runtime binaries and adds its own bridge; see [notices](../THIRD_PARTY.md).

```powershell
python source/native/build.py --release
python scripts/quest/builder.py --config config/quest/release.ini --game 'YOUR_BIOSHOCK_FOLDER' --yes
```

The first command produces `build/native/quest-release/d3d9.dll`; the second prepares the game and `dist/quest/BioShock_VR.apk`. It creates a local signing key if absent. Keep your own key private; a different key cannot update an existing installation in place.

## Installer

Place the release APK at `dist/release/BioShock_VR.apk`. The packer also needs Steamless files under `dependencies/runtime/Steamless`, Meta ADB driver files under `dependencies/runtime/MetaADB`, and Android platform-tools. In `scripts/distribution/pack.py`, set `sdk` to your platform-tools folder (the development default is `D:/Android/Sdk/platform-tools`).

```powershell
python -m pip install pyinstaller
python scripts/distribution/test_console.py
python scripts/distribution/test_installer.py
python scripts/distribution/pack.py
```

The result is `dist/installer/BioShockVR-Setup.exe`. The packer embeds the APK, DLL, profiles, source archive, ADB, Steamless, driver and notices; it rejects game EXEs and keystore files in the payload. The installer passed 7 terminal-flow and 8 transaction tests; its embedded DLL/profile match the reticles accepted during headset testing.

Developer diagnostics remain source-only and are excluded or disabled in production builds. Some historic developer tests refer to local snapshots that are not included here; use the two installer tests above on a fresh checkout. Use the packaged installer for normal headset installation.
