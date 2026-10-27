"""
BioShock (PC) -> Meta Quest 2 port builder on top of WinlatorXR.

What it does, in order:
  1. finds the BioShock install (Steam libraries, registry, every drive),
  2. builds the game "cache" folder: hard links to the original files (no 5 GB copy, the
     original install is never modified) plus adapted copies of the files it changes,
  3. tunes Default.ini for the most FPS on Quest 2 at the chosen refresh rate,
  4. builds the VR mod into the game folder: ReShade + BioshockVR.fx depth stereo shader,
  5. patches the WinlatorXR APK: own launcher that hides WinlatorXR's UI and starts the game
     straight in VR, renamed package, BioShock name and icon, port settings baked in; signs it,
  6. optionally installs the APK and uploads the cache folder to the root of the headset.

Run it with BUILD.bat (or: py -3 -I bioshock_quest_port.py [--yes] [--install] [--game PATH]).
"""

import argparse
import configparser
import glob
import hashlib
import io
import json
import os
import re
import secrets
import shutil
import string
import struct
import subprocess
import sys
import tarfile
import time
import urllib.request
import zipfile
from pathlib import Path

try:
    import winreg
except ImportError:  # not on Windows
    winreg = None

try:
    from compression import zstd  # Python 3.14+
except ImportError:
    zstd = None

ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "dependencies" / "runtime"
OUTPUT = ROOT / "dist" / "quest"
WORK = ROOT / "build" / "work"
ANDROID_SRC = ROOT / "source" / "android"
VRMOD = ROOT / "source" / "legacy-reshade"

STEAM_APP_ID = "7670"
EXE_RELATIVE = r"Builds\Release\Bioshock.exe"
MARKER = ".bioshockport"
ORIGINAL_PACKAGE = "com.winlator.cmod"

# Top-level parts of the install the headset never needs
SKIP_TOP_DIRS = {"directx", "vcredist"}
SKIP_TOP_FILES = re.compile(r"(?i)^(.*\.txt|instscript\.vdf)$")

ASSUME_YES = False


# --------------------------------------------------------------------------------------------
# console helpers
# --------------------------------------------------------------------------------------------

def step(text):
    print(f"\n=== {text} ===", flush=True)


def info(text):
    print(f"  {text}", flush=True)


def warn(text):
    print(f"  [!] {text}", flush=True)


class PortError(Exception):
    pass


def ask(question, default=False):
    if ASSUME_YES:
        info(f"{question} -> да (--yes)")
        return True
    if not sys.stdin or not sys.stdin.isatty():
        info(f"{question} -> нет (нет консоли для ответа; запустите с --yes)")
        return False
    hint = "[Д/н]" if default else "[д/Н]"
    try:
        answer = input(f"  {question} {hint}: ").strip().lower()
    except EOFError:
        return default
    if not answer:
        return default
    return answer[0] in ("y", "д", "1")


def run(cmd, cwd=None, check=True, quiet=False):
    shown = " ".join(f'"{c}"' if " " in str(c) else str(c) for c in cmd)
    if not quiet:
        info(f"> {shown}")
    result = subprocess.run([str(c) for c in cmd], cwd=cwd, capture_output=True, text=True,
                            encoding="utf-8", errors="replace")
    if check and result.returncode != 0:
        raise PortError(f"Команда завершилась с ошибкой {result.returncode}:\n{shown}\n"
                        f"{result.stdout[-3000:]}\n{result.stderr[-3000:]}")
    return result


def download(url, dest, what):
    info(f"Скачиваю {what}: {url}")
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_suffix(dest.suffix + ".part")
    request = urllib.request.Request(url, headers={"User-Agent": "bioshock-quest-port"})
    with urllib.request.urlopen(request, timeout=60) as response, open(tmp, "wb") as out:
        total = int(response.headers.get("Content-Length") or 0)
        done = 0
        while True:
            chunk = response.read(1 << 20)
            if not chunk:
                break
            out.write(chunk)
            done += len(chunk)
            if total:
                print(f"\r    {done * 100 // total:3d}%  {done >> 20} / {total >> 20} МБ", end="", flush=True)
    print()
    tmp.replace(dest)
    return dest


def github_latest_asset(repo, pattern):
    url = f"https://api.github.com/repos/{repo}/releases"
    request = urllib.request.Request(url, headers={"User-Agent": "bioshock-quest-port",
                                                   "Accept": "application/vnd.github+json"})
    with urllib.request.urlopen(request, timeout=30) as response:
        releases = json.load(response)
    for release in releases:
        if release.get("draft"):
            continue
        for asset in release.get("assets", []):
            if re.search(pattern, asset["name"], re.I):
                return asset["browser_download_url"], asset["name"], asset.get("size", 0)
    return None


# --------------------------------------------------------------------------------------------
# configuration
# --------------------------------------------------------------------------------------------

def load_config(path):
    config = configparser.ConfigParser(inline_comment_prefixes=(";",))
    config.read(path, encoding="utf-8")
    get = lambda s, k, d: config.get(s, k, fallback=d).strip()
    yes = lambda s, k, d: get(s, k, d).lower() in ("yes", "true", "1", "да", "on")

    resolution = get("display", "resolution", "1280x720").lower()
    match = re.fullmatch(r"(\d{3,4})x(\d{3,4})", resolution)
    if not match:
        raise PortError(f"Неверное разрешение в {path.name}: {resolution}")
    refresh = int(get("display", "refresh_rate", "90"))
    if refresh not in (72, 80, 90, 120):
        raise PortError("refresh_rate должен быть 72, 90 или 120")
    # WinlatorXR's system image has /data/data/com.winlator.cmod written into hundreds of files
    # (imagefs, Proton, the XR API), so the port has to keep that package name.
    package = ORIGINAL_PACKAGE
    if get("port", "package", ORIGINAL_PACKAGE) != ORIGINAL_PACKAGE:
        warn(f"package в {path.name} игнорируется: WinlatorXR работает только как {ORIGINAL_PACKAGE}")
    cache_name = get("port", "cache_name", "BioshockMQ2")
    if not re.fullmatch(r"[\w.\-]+", cache_name):
        raise PortError("cache_name: только латиница, цифры, точка, дефис и подчёркивание")
    preset = get("graphics", "preset", "maxfps").lower()
    if preset not in ("maxfps", "balanced", "original"):
        raise PortError("preset должен быть maxfps, balanced или original")
    controls = get("input", "controls", "gamepad").lower()
    if controls not in ("gamepad", "mouse"):
        raise PortError("controls должен быть gamepad или mouse")

    return {
        "game_path": get("game", "path", "auto"),
        "app_name": get("port", "app_name", "BioShock VR"),
        "package": package,
        "cache_name": cache_name,
        "version": get("port", "version", "1"),
        "refresh": refresh,
        "width": int(match.group(1)),
        "height": int(match.group(2)),
        "desktop_resolution": get("display","desktop_resolution","auto"),
        "preset": preset,
        "skip_intro": yes("graphics", "skip_intro", "yes"),
        "vr_mode": get("vr", "mode", "mod").lower(),
        "mod_stereo": yes("vr", "mod_stereo", "no"),
        "snap_turn": yes("vr", "snap_turn", "yes"),
        "turn_speed": float(get("vr", "turn_speed", "120")),
        "immersive": yes("vr", "immersive", "yes"),
        "stereo": yes("vr", "stereo", "yes"),
        "stereo_strength": float(get("vr", "stereo_strength", "0.025")),
        "screen_depth": float(get("vr", "screen_depth", "0.015")),
        "controls": controls,
        "winlator_apk": get("tools", "winlator_apk", "auto"),
        "apktool": get("tools", "apktool", "auto"),
        "reshade_dll": get("tools", "reshade_dll", "auto"),
        "steamless": get("tools", "steamless", "auto"),
        "adb_install": get("install", "adb_install", "ask").lower(),
        "debuggable": yes("install", "debuggable", "no"),
        "dxvk": get("performance", "dxvk", "2.6.2-binsem-gplasync"),
        "gpu_tiling": yes("performance", "gpu_tiling", "yes"),
        "render_threading": yes("performance", "render_threading", "yes"),
        "vulkan_placed": yes("performance", "vulkan_placed", "no"),
        "dxvk_compiler_threads": max(1,min(8,int(get("performance","dxvk_compiler_threads","2")))),
        "dxvk_reported_memory": max(128,min(2048,int(get("performance","dxvk_reported_memory","512")))),
        "fex_cache": yes("performance", "fex_cache", "yes"),
        "wine_debug": yes("diagnostics", "wine_debug", "no"),
        "wine_debug_channels": get("diagnostics","wine_debug_channels","timestamp,pid,tid,seh,process"),
        "dxvk_debug": yes("diagnostics", "dxvk_debug", "no"),
        "native_dll": get("vr", "native_dll", "auto"),
        "native_ini": get("vr", "native_ini", "config/quest/profiles/candidate.ini"),
    }


# --------------------------------------------------------------------------------------------
# 1. find the game
# --------------------------------------------------------------------------------------------

def is_game_dir(path):
    return path and (Path(path) / EXE_RELATIVE).is_file() and (Path(path) / "Content").is_dir()


def steam_roots():
    roots = []
    if winreg:
        for hive, key, value in ((winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam", "SteamPath"),
                                 (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\WOW6432Node\Valve\Steam", "InstallPath"),
                                 (winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Valve\Steam", "InstallPath")):
            try:
                with winreg.OpenKey(hive, key) as k:
                    roots.append(Path(winreg.QueryValueEx(k, value)[0]))
            except OSError:
                pass
    roots.append(Path(r"C:\Program Files (x86)\Steam"))
    libraries = []
    for root in roots:
        vdf = root / "steamapps" / "libraryfolders.vdf"
        if vdf.is_file():
            text = vdf.read_text(encoding="utf-8", errors="replace")
            for p in re.findall(r'"path"\s+"([^"]+)"', text):
                libraries.append(Path(p.replace("\\\\", "\\")))
        libraries.append(root)
    seen, unique = set(), []
    for lib in libraries:
        key = str(lib).lower()
        if key not in seen:
            seen.add(key)
            unique.append(lib)
    return unique


def find_game(configured):
    if configured and configured.lower() != "auto":
        path = Path(configured)
        if is_game_dir(path):
            return path
        raise PortError(f"В {path} нет {EXE_RELATIVE}")

    # Steam: the app manifest names the install folder
    for lib in steam_roots():
        manifest = lib / "steamapps" / f"appmanifest_{STEAM_APP_ID}.acf"
        if manifest.is_file():
            m = re.search(r'"installdir"\s+"([^"]+)"', manifest.read_text(encoding="utf-8", errors="replace"))
            if m and is_game_dir(lib / "steamapps" / "common" / m.group(1)):
                return lib / "steamapps" / "common" / m.group(1)
        for candidate in (lib / "steamapps" / "common").glob("*ioshock*") if (lib / "steamapps" / "common").is_dir() else []:
            if is_game_dir(candidate):
                return candidate

    # Retail / GOG registry entries
    if winreg:
        for key in (r"SOFTWARE\WOW6432Node\2K Games\BioShock", r"SOFTWARE\2K Games\BioShock",
                    r"SOFTWARE\WOW6432Node\GOG.com\Games\1806891286"):
            for value in ("InstallLocation", "path", "Install Path"):
                try:
                    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, key) as k:
                        path = Path(winreg.QueryValueEx(k, value)[0])
                        if is_game_dir(path):
                            return path
                except OSError:
                    pass

    # Last resort: look for a folder named like the game on every drive, a few levels deep
    info("Не нашёл через Steam/реестр, ищу по имени на всех дисках...")
    for letter in string.ascii_uppercase:
        drive = Path(f"{letter}:\\")
        if not drive.exists():
            continue
        for pattern in ("*ioshock*", "*/*ioshock*", "*/*/*ioshock*", "*/*/*/*ioshock*"):
            try:
                for candidate in drive.glob(pattern):
                    if "remaster" not in candidate.name.lower() and is_game_dir(candidate):
                        return candidate
            except OSError:
                continue
    raise PortError("BioShock не найден. Укажите путь в port_config.ini -> [game] path")


def has_steamstub(exe):
    data = exe.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    optional = struct.unpack_from("<H", data, pe + 20)[0]
    table = pe + 24 + optional
    names = [data[table + i * 40: table + i * 40 + 8].rstrip(b"\0") for i in range(count)]
    return b".bind" in names


def pe_machine(data):
    try:
        if data[:2] != b"MZ":
            return None
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        if data[pe:pe + 4] != b"PE\0\0":
            return None
        return struct.unpack_from("<H", data, pe + 4)[0]
    except struct.error:
        return None


# --------------------------------------------------------------------------------------------
# 2. game cache folder
# --------------------------------------------------------------------------------------------

def replace_file(path, data):
    """Writes a fresh file. Never writes through a hard link, so the original game stays untouched."""
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() or path.is_symlink():
        path.unlink()
    path.write_bytes(data)


def stage_game(game, cache):
    cache.mkdir(parents=True, exist_ok=True)
    linked = copied = skipped = 0
    can_link = True
    files = []
    for dirpath, dirnames, filenames in os.walk(game):
        rel_dir = Path(dirpath).relative_to(game)
        if rel_dir == Path("."):
            dirnames[:] = [d for d in dirnames if d.lower() not in SKIP_TOP_DIRS]
            filenames = [f for f in filenames if not SKIP_TOP_FILES.match(f)]
        for name in filenames:
            files.append(rel_dir / name)

    total = sum((game / f).stat().st_size for f in files)
    done = 0
    for rel in files:
        src, dst = game / rel, cache / rel
        st = src.stat()
        done += st.st_size
        if dst.exists():
            dt = dst.stat()
            if dt.st_size == st.st_size and int(dt.st_mtime) == int(st.st_mtime):
                skipped += 1
                continue
            dst.unlink()
        dst.parent.mkdir(parents=True, exist_ok=True)
        if can_link:
            try:
                os.link(src, dst)
                linked += 1
                continue
            except OSError:
                can_link = False
                warn("Жёсткие ссылки недоступны (другой диск?) - копирую файлы целиком")
        shutil.copy2(src, dst)
        copied += 1
        print(f"\r    {done * 100 // max(total, 1):3d}%", end="", flush=True)
    if copied:
        print()
    info(f"Файлов: {len(files)} (ссылок: {linked}, копий: {copied}, без изменений: {skipped}),"
         f" {total / 2 ** 30:.1f} ГБ")


# --------------------------------------------------------------------------------------------
# 3. graphics / ini tuning
# --------------------------------------------------------------------------------------------

class UnrealIni:
    """Line based editor: Unreal ini files repeat keys, which configparser cannot keep."""

    def __init__(self, data):
        text = data.decode("latin-1")
        self.newline = "\r\n" if "\r\n" in text else "\n"
        self.lines = text.split(self.newline)

    def _section(self, section):
        header = f"[{section}]".lower()
        for i, line in enumerate(self.lines):
            if line.strip().lower() == header:
                end = i + 1
                while end < len(self.lines) and not self.lines[end].strip().startswith("["):
                    end += 1
                return i, end
        return None

    def set(self, section, key, value):
        found = self._section(section)
        if found is None:
            self.lines += ["", f"[{section}]", f"{key}={value}"]
            return
        start, end = found
        for j in range(start + 1, end):
            line = self.lines[j].strip()
            if "=" in line and not line.startswith(";") and line.split("=", 1)[0].strip().lower() == key.lower():
                self.lines[j] = f"{key}={value}"
                return
        insert = end
        while insert > start + 1 and not self.lines[insert - 1].strip():
            insert -= 1
        self.lines.insert(insert, f"{key}={value}")

    def data(self):
        return self.newline.join(self.lines).encode("latin-1")


def tune_ini(cache, cfg):
    path = cache / "Builds" / "Release" / "Default.ini"
    ini = UnrealIni(path.read_bytes())
    w, h, hz = cfg["width"], cfg["height"], cfg["refresh"]
    client = "WinDrv.WindowsClient"
    d3d9, d3d10 = "D3DDrv.D3DRenderDevice", "D3DDrv10.D3DRenderDevice10"
    render = "Engine.RenderConfig"

    # Always: resolution, refresh, controller, no vsync (the headset compositor paces frames)
    for key, value in (("FullscreenViewportX", w), ("FullscreenViewportY", h),
                       ("WindowedViewportX", w), ("WindowedViewportY", h),
                       ("MenuViewportX", w), ("MenuViewportY", h),
                       ("StartupFullscreen", "True"), ("CaptureMouse", "True"),
                       ("UseController", "True" if cfg["controls"] == "gamepad" else "False")):
        ini.set(client, key, value)
    for section in (d3d9, d3d10):
        ini.set(section, "DesiredRefreshRate", hz)
        ini.set(section, "UseVSync", "False")
        # The mouse lag fixes stall the CPU every frame waiting for the GPU
        ini.set(section, "ReduceMouseLag", "False")
        ini.set(section, "DoubleBufferMouseLag", "False")
    if cfg["skip_intro"]:
        # A movie path that does not exist is skipped, the same way the stock BioshockLogo.bik is
        ini.set("Bink", "IntroMoviePC", r'"..\BinkMovies\_skipped_intro.bik"')

    if cfg["preset"] in ("maxfps", "balanced"):
        for section in (d3d9, d3d10):
            ini.set(section, "LevelOfAnisotropy", 1)
            ini.set(section, "UseTrilinear", "False")
            ini.set(section, "SuperHighDetailActors", "False")
        ini.set(d3d10, "UseHighDetailShadowMaps", "False")
        for key in ("Shadows", "RealTimeReflection", "UseRippleSystem", "UseDistortion",
                    "UseHighDetailSoftParticles", "UseSpecCubeMap"):
            ini.set(render, key, "False")
        ini.set(render, "HardwareOcclusion", "True")
        ini.set(client, "Coronas", "False")
        ini.set(client, "Projectors", "False")
        ini.set(client, "TextureDetailLightmap", "Normal")
        # Below this frame rate the engine drops detail on its own to hold the rate
        ini.set(client, "MinDesiredFrameRate", f"{hz * 0.6:.6f}")
        ini.set("Engine.GameEngine", "CacheSizeMegs", 64)

    if cfg["preset"] == "maxfps":
        for section in (d3d9, d3d10):
            ini.set(section, "HighDetailActors", "False")
        ini.set(render, "HighDetailShaders", "False")
        ini.set(render, "PostProcessing", "False")
        ini.set(client, "DecoLayers", "False")

        for key in ("TextureDetailWorld", "TextureDetailTerrain", "TextureDetailPlayerSkin",
                    "TextureDetailWeaponSkin", "TextureDetailRenderMap"):
            ini.set(client, key, "Lower")

    if cfg["vr_mode"] == "mod" and cfg["preset"] != "original":
        # Native BioShock setting, verified in EXE RenderConfig (+0x44).
        # A controlled comparison can remove the engine render command queue;
        # DXVK and the headset compositor retain their own worker threads.
        ini.set(render, "UseMultithreading", "True" if cfg["render_threading"] else "False")
        # Reducing shadows/shaders is useful on Quest; low texture mips and
        # bilinear-only minification unnecessarily blur the player's view.
        for section in (d3d9, d3d10):
            ini.set(section, "LevelOfAnisotropy", 4)
            ini.set(section, "UseTrilinear", "True")
        for key in ("TextureDetailWorld", "TextureDetailTerrain", "TextureDetailPlayerSkin", "TextureDetailWeaponSkin"):
            ini.set(client, key, "Normal")

    replace_file(path, ini.data())
    info(f"Default.ini: {w}x{h} @ {hz} Гц, пресет «{cfg['preset']}», геймпад: {cfg['controls'] == 'gamepad'}")


KEYS_MARKER = "; BioshockVR: keys follow the Quest 2 buttons"


def patch_game_hud(cache,cfg,game):
    if cfg['vr_mode']!='mod':return
    profile=configparser.ConfigParser();profile.read(ROOT/cfg['native_ini'],encoding='utf-8')
    import importlib.util
    spec=importlib.util.spec_from_file_location('quest_flashpatch',ROOT/'scripts/quest/flashpatch.py')
    module=importlib.util.module_from_spec(spec);sys.modules[spec.name]=module;spec.loader.exec_module(module)
    movies={}
    for name in ('HUDPC.swf','HUDRadial.swf'):
        relative=Path('Content/FlashMovies')/name;original=(game/relative).read_bytes()
        patched,report=module.patch(original,profile.getboolean('VR','HideAttackTutorial',fallback=False),profile.getboolean('VR','HideCrosshair',fallback=False),profile.getboolean('VR','HideEnemyHealth',fallback=False))
        replace_file(cache/relative,patched)
        movies[name]={'sourceSha256':hashlib.sha256(original).hexdigest(),'patchedSha256':hashlib.sha256(patched).hexdigest(),'changes':report}
    replace_file(cache/'Builds/Release/SWING-LICENSE.txt',(ROOT/'source/native/third_party/bioshock-vr/LICENSE').read_bytes())
    destination=ROOT/'build/diagnostics';destination.mkdir(exist_ok=True,parents=True)
    (destination/'hud-flash-patch.json').write_text(json.dumps({**movies['HUDPC.swf'],'movies':movies},indent=2)+'\n')
    info('HUDPC + HUDRadial: patched training/reticle handlers; graphics/fonts and other UI preserved')


def remap_keys(cache, cfg, game=None):
    """Action keys renamed to the Quest 2 button letters, so the game's own hints ("Press A to use")
    name the controller button: Use -> A, Reload -> B, MedHypo -> X, Hack -> Y, walking -> arrows.
    The VR mod presses these keys from the controllers."""
    if cfg["vr_mode"] != "mod":
        return
    profile=configparser.ConfigParser()
    if cfg.get("native_ini"):profile.read(ROOT/cfg["native_ini"],encoding="utf-8")
    controller=cfg.get("quest_controller_bindings",profile.getboolean("VR","QuestControllerBindings",fallback=False))
    healing=cfg.get("quest_healing_buttons")
    if healing is None:
        healing=profile.getboolean("VR","QuestHealingButtons",fallback=False)
    moves = {"Up": "W", "Down": "S", "Left": "A", "Right": "D", "A": "E", "B": "R", "X": "F", "Y": "V"}
    for path in sorted((cache / "Builds" / "Release").glob("DefUser*.ini")):
        # всегда от оригинала игры: правки раскладки не накладываются друг на друга
        original = (Path(game) / "Builds" / "Release" / path.name) if game else None
        if original and original.is_file():
            text = original.read_bytes().decode("latin-1")
        else:
            text = path.read_bytes().decode("latin-1")
            if KEYS_MARKER in text:
                continue
        out, section, block = [], "", []

        def flush():
            if not block:
                return
            values = {}
            for line in block:
                m = re.match(r"^([A-Za-z0-9_]+)=(.*)$", line)
                if m:
                    values[m.group(1)] = m.group(2)
            touched = any(k in values for k in moves.values())
            lines = []
            for line in block:
                m = re.match(r"^([A-Za-z0-9_]+)=(.*)$", line)
                key = m.group(1) if m else None
                if touched and key in moves:
                    src = moves[key]
                    if src in values:
                        value = values[src]
                    elif key in ("Up", "Down", "Left", "Right"):
                        value = values.get(key, "")  # стрелки меню остаются
                    else:
                        value = ""
                    lines.append(f"{key}={value}")
                elif touched and key in ("W", "S", "D", "E", "R", "F", "V"):
                    lines.append(f"{key}=")
                else:
                    lines.append(line)
            # keys the section never listed but now needs
            if touched:
                for key, src in moves.items():
                    if src in values and not any(re.match(rf"^{key}=", l) for l in block):
                        lines.append(f"{key}={values[src]}")
            # курки как на геймпаде: J - оружие (переключиться и стрелять), G - плазмид
            if touched:
                for key, src in (("J", "XENON_RT"), ("G", "XENON_LT")):
                    if values.get(src) and not values.get(key):
                        lines = [l for l in lines if not re.match(rf"^{key}=", l)] + [f"{key}={values[src]}"]
            if section.lower()=="containeruiactive":
                for key,src in (("A","XENON_A"),("B","XENON_B")):
                    if values.get(src):
                        lines=[l for l in lines if not re.match(rf"^{key}=",l)]+[f"{key}={values[src]}"]
            if healing and touched:
                # Preserve per-context restrictions from the original game.
                # Never assign EVE in menus or scripted contexts which lack it.
                commands={"H":values.get("V","")}
                if "InjectBioAmmo" in values.get("XENON_X",""):commands["Y"]="InjectBioAmmo"
                if "UseHypoOfType MedHypo" in values.get("XENON_B",""):commands["X"]="UseHypoOfType MedHypo"
                for key,value in commands.items():
                    lines=[l for l in lines if not re.match(rf"^{key}=",l)]+[f"{key}={value}"]
            if controller and touched and values.get("Shift")=="LaunchPCWeaponSelection":
                # The native controller hint action opens the map in PC
                # mode, even when a focused object has its own help page.
                # Only contexts which already
                # permit equipment selection gain these gameplay shortcuts.
                commands={"M":"HintButtonPressed | onRelease HintButtonReleased","I":"LaunchQuestsScreen",
                          "P":"HintButtonAlias","PageUp":"AmmoSelectionUp","PageDown":"AmmoSelectionDown"}
                for key,value in commands.items():
                    lines=[l for l in lines if not re.match(rf"^{key}=",l)]+[f"{key}={value}"]
            if controller and section.lower().endswith("uiactive") and section.lower()!="containeruiactive":
                # Physical face buttons and grips also reach the original
                # contextual UI commands. B keeps the established Escape/back.
                for key,src in (("A","XENON_A"),("X","XENON_X"),("Y","XENON_Y"),
                                ("H","XENON_X"),("Q","XENON_RB"),
                                ("Up","XENON_DPAD_UP"),("Down","XENON_DPAD_DOWN"),
                                ("Left","XENON_DPAD_LEFT"),("Right","XENON_DPAD_RIGHT")):
                    if values.get(src):lines=[l for l in lines if not re.match(rf"^{key}=",l)]+[f"{key}={values[src]}"]
            out.extend(lines)
            block.clear()

        for line in text.splitlines():
            if line.startswith("["):
                flush()
                section=line.strip()[1:-1]
                out.append(line)
            else:
                block.append(line)
        flush()
        nl = "\r\n"
        replace_file(path, (KEYS_MARKER + nl + nl.join(out) + nl).encode("latin-1"))
    info("Кнопки Quest 2: A - использовать, B - перезарядка, X - аптечка, "+("Y - EVE, удержание Y - взлом" if healing else "Y - взлом")+"; J/G - оружие/плазмид")


def unpack_steamstub(cache, cfg):
    exe = cache / EXE_RELATIVE
    if not has_steamstub(exe):
        info("SteamStub в Bioshock.exe нет - игра запустится без Steam")
        return True
    tool = None
    if cfg["steamless"].lower() != "auto":
        tool = Path(cfg["steamless"])
    else:
        found = glob.glob(str(TOOLS / "**" / "Steamless.CLI.exe"), recursive=True)
        if not found:
            # The release zip as downloaded, from tools\ or Downloads: unpacked into tools\Steamless
            zips = sorted(list(TOOLS.glob("Steamless*.zip")) + list((Path.home() / "Downloads").glob("Steamless*.zip")),
                          key=lambda p: p.stat().st_mtime, reverse=True)
            for archive in zips:
                with zipfile.ZipFile(archive) as z:
                    if any(n.endswith("Steamless.CLI.exe") for n in z.namelist()):
                        info(f"Распаковываю {archive.name} в tools\\Steamless")
                        z.extractall(TOOLS / "Steamless")
                        break
            found = glob.glob(str(TOOLS / "**" / "Steamless.CLI.exe"), recursive=True)
        tool = Path(found[0]) if found else None
    if not tool or not tool.is_file():
        warn("Bioshock.exe защищён SteamStub: без запущенного Steam он не стартует.")
        warn("Положите Steamless (github.com/atom0s/Steamless) в tools\\Steamless\\ и запустите сборку снова -")
        warn("он снимет защиту с ВАШЕЙ копии exe в папке порта (оригинал игры не трогается).")
        return False
    run([tool, "--quiet", exe], check=False)
    unpacked = exe.with_name(exe.name + ".unpacked.exe")
    if not unpacked.is_file():
        warn("Steamless не создал распакованный exe")
        return False
    data = unpacked.read_bytes()
    unpacked.unlink()
    replace_file(exe, data)
    if has_steamstub(exe):
        warn("После Steamless секция .bind осталась - проверьте вывод Steamless")
        return False
    info("SteamStub снят с копии Bioshock.exe")
    return True


# --------------------------------------------------------------------------------------------
# 4. VR mod (ReShade + stereo shader)
# --------------------------------------------------------------------------------------------

def tzst_members(blob):
    if zstd is None:
        return {}
    files = {}
    with tarfile.open(fileobj=io.BytesIO(zstd.decompress(blob)), mode="r:") as tar:
        for member in tar.getmembers():
            if member.isfile():
                files[member.name.lstrip("./")] = tar.extractfile(member).read()
    return files


def reshade32_from_apk(apk):
    """WinlatorXR bundles ReShade in its assets; takes a 32-bit build out of it if there is one."""
    if zstd is None:
        warn("В этом Python нет zstd (нужен 3.14+) - ReShade из APK не достать")
        return None
    with zipfile.ZipFile(apk) as z:
        for name in z.namelist():
            if not re.search(r"(?i)reshade.*\.tzst$", name):
                continue
            for member, data in tzst_members(z.read(name)).items():
                if member.lower().endswith(".dll") and pe_machine(data) == 0x14C and b"ReShade" in data:
                    info(f"ReShade (32 бит) найден в APK: {name} -> {member}")
                    return data
    return None


def reshade32_from_web():
    try:
        with urllib.request.urlopen(urllib.request.Request(
                "https://reshade.me/", headers={"User-Agent": "bioshock-quest-port"}), timeout=30) as r:
            page = r.read().decode("utf-8", "replace")
    except Exception as e:
        warn(f"reshade.me недоступен: {e}")
        return None
    m = re.search(r'href="(/downloads/ReShade_Setup_[\d.]+\.exe)"', page)
    if not m:
        warn("Не нашёл ссылку на установщик на reshade.me")
        return None
    url = "https://reshade.me" + m.group(1)
    if not ask(f"Скачать официальный ReShade ({url}) для стерео-режима?", True):
        return None
    setup = download(url, TOOLS / Path(m.group(1)).name, "ReShade")
    # The installer is an exe with a zip archive appended; ReShade32.dll lives in it
    with zipfile.ZipFile(setup) as z:
        for name in z.namelist():
            if name.lower().endswith("reshade32.dll"):
                data = z.read(name)
                (TOOLS / "ReShade32.dll").write_bytes(data)
                return data
    return None


def find_reshade32(cfg, apk):
    if cfg["reshade_dll"].lower() != "auto":
        path = Path(cfg["reshade_dll"])
        if not path.is_file():
            raise PortError(f"Нет файла {path}")
        return path.read_bytes()
    local = TOOLS / "ReShade32.dll"
    if local.is_file():
        return local.read_bytes()
    if apk:
        data = reshade32_from_apk(apk)
        if data:
            return data
    return reshade32_from_web()


MOD_DIR = ROOT / "source" / "native"


def install_native_mod(cache, cfg):
    """Встраивает VR-мод (d3d9.dll): камера по шлему, геймпад из контроллеров, меню плоским экраном."""
    game_bin = cache / "Builds" / "Release"
    dll = ROOT / "build" / "native" / "quest" / "d3d9.dll" if cfg["native_dll"] == "auto" else ROOT / cfg["native_dll"]
    sources = (list((MOD_DIR / "src").glob("*.*")) + list((MOD_DIR / "shaders").glob("*.hlsl")) +
               [MOD_DIR / "build.py", MOD_DIR / "tools" / "compile_shaders.py"])
    if cfg["native_dll"] == "auto" and (not dll.is_file() or any(s.stat().st_mtime > dll.stat().st_mtime for s in sources)):
        info("Собираю VR-мод (mod/build.py)...")
        run([sys.executable, "-I", MOD_DIR / "build.py"])
    for name in ("ReShade.ini", "BioshockVR_preset.ini", "reshade-shaders/Shaders/BioshockVR.fx", "ReShade.log"):
        if (game_bin / name).exists():
            (game_bin / name).unlink()
    replace_file(game_bin / "d3d9.dll", dll.read_bytes())
    if not dll.is_file(): raise PortError(f"Нет VR-мода: {dll}")
    ini = (ROOT / cfg["native_ini"]).read_text(encoding="utf-8")
    ini = re.sub(r"(?m)^Stereo=.*$", f"Stereo={int(cfg['mod_stereo'])}", ini)
    ini = re.sub(r"(?m)^SnapTurn=.*$", f"SnapTurn={int(cfg['snap_turn'])}", ini)
    ini = re.sub(r"(?m)^TurnSpeed=.*$", f"TurnSpeed={cfg['turn_speed']:g}", ini)
    replace_file(game_bin / "BioshockVR.ini", ini.replace("\r\n", "\n").replace("\n", "\r\n").encode("utf-8"))
    replace_file(game_bin / "dxvk-port.conf", (
        "# Quest: compiler concurrency and the memory budget reported to D3D9.\n"
        "# maxAvailableMemory is not a physical RAM/VRAM limit.\n"
        f"dxvk.numCompilerThreads = {cfg['dxvk_compiler_threads']}\n"
        f"d3d9.maxAvailableMemory = {cfg['dxvk_reported_memory']}\n").encode("ascii"))
    depth_stereo = bool(re.search(r"(?m)^DepthStereo=1\s*$", ini))
    stereo_mode = "по буферу глубины, оба глаза в одном кадре" if depth_stereo else "поочерёдные глаза" if cfg['mod_stereo'] else "моно"
    info(f"VR-мод встроен: камера и голова 6DoF, геймпад из контроллеров, стерео: {stereo_mode}")
    return True


def install_vr_mod(cache, cfg, apk):
    game_bin = cache / "Builds" / "Release"
    if cfg["vr_mode"] == "mod":
        return install_native_mod(cache, cfg)
    reshade_files = ["d3d9.dll", "ReShade.ini", "BioshockVR_preset.ini", "reshade-shaders/Shaders/BioshockVR.fx"]
    if cfg["vr_mode"] == "flat" or not cfg["stereo"]:
        for name in reshade_files:
            if (game_bin / name).exists():
                (game_bin / name).unlink()
        info("Стерео выключено в настройках - только иммерсивный 3DoF режим")
        return False

    dll = find_reshade32(cfg, apk)
    if not dll:
        warn("ReShade (32 бит) не найден - стерео выключено, остаётся иммерсивный 3DoF режим.")
        warn("Положите ReShade32.dll в tools\\ и пересоберите, чтобы включить стерео.")
        return False
    if pe_machine(dll) != 0x14C:
        raise PortError("ReShade DLL не 32-битная, а BioShock - 32-битная игра")

    # The game runs in DX9 (-dx9); ReShade sits in front of DXVK's d3d9 as the game's own d3d9.dll
    replace_file(game_bin / "d3d9.dll", dll)
    replace_file(game_bin / "reshade-shaders" / "Shaders" / "BioshockVR.fx",
                 (VRMOD / "BioshockVR.fx").read_bytes())
    depth_defs = ("RESHADE_DEPTH_INPUT_IS_UPSIDE_DOWN=0,RESHADE_DEPTH_INPUT_IS_REVERSED=0,"
                  "RESHADE_DEPTH_INPUT_IS_LOGARITHMIC=0,RESHADE_DEPTH_LINEARIZATION_FAR_PLANE=1000.0")
    reshade_ini = f"""[GENERAL]
EffectSearchPaths=.\\reshade-shaders\\Shaders
TextureSearchPaths=.\\reshade-shaders\\Textures
PresetPath=.\\BioshockVR_preset.ini
PerformanceMode=1
SkipLoadingDisabledEffects=1
PreprocessorDefinitions={depth_defs}
NoReloadOnInit=0

[INPUT]
KeyOverlay=36,0,0,0
KeyEffects=145,0,0,0

[OVERLAY]
TutorialProgress=4
ShowClock=0
ShowFPS=0
ShowFrameTime=0
NoFontScaling=1

[DEPTH]
DepthCopyBeforeClears=0
UseAspectRatioHeuristics=1
"""
    preset = f"""PreprocessorDefinitions={depth_defs}
Techniques=BioshockVR@BioshockVR.fx
TechniqueSorting=BioshockVR@BioshockVR.fx

[BioshockVR.fx]
Divergence={cfg['stereo_strength']:.3f}
ScreenDepth={cfg['screen_depth']:.3f}
SwapEyes=0
ShowDepth=0
"""
    replace_file(game_bin / "ReShade.ini", reshade_ini.replace("\n", "\r\n").encode())
    replace_file(game_bin / "BioshockVR_preset.ini", preset.replace("\n", "\r\n").encode())
    info("VR-мод встроен: ReShade (d3d9.dll) + BioshockVR.fx, стерео side-by-side")
    return True


# --------------------------------------------------------------------------------------------
# 5. APK
# --------------------------------------------------------------------------------------------

def find_java():
    candidates = []
    if os.environ.get("JAVA_HOME"):
        candidates.append(Path(os.environ["JAVA_HOME"]) / "bin")
    which = shutil.which("java")
    if which:
        candidates.append(Path(which).parent)
    for base in (r"C:\Program Files\Eclipse Adoptium", r"C:\Program Files\Java",
                 r"C:\Program Files\Microsoft", r"C:\Program Files\Android\Android Studio\jbr"):
        candidates += [Path(p) for p in glob.glob(base + r"\*\bin")] + [Path(base) / "bin"]
    for bin_dir in candidates:
        if (bin_dir / "javac.exe").is_file() and (bin_dir / "keytool.exe").is_file():
            return bin_dir
    raise PortError("Нужен JDK 17+ (java, javac, keytool). Установите Eclipse Temurin JDK.")


def find_sdk():
    roots = [os.environ.get("ANDROID_HOME"), os.environ.get("ANDROID_SDK_ROOT"),
             os.path.join(os.environ.get("LOCALAPPDATA", ""), "Android", "Sdk")]
    for root in filter(None, roots):
        root = Path(root)
        tools = [d for d in (root / "build-tools").glob("*")
                 if (d / "lib" / "d8.jar").is_file() and (d / "lib" / "apksigner.jar").is_file()
                 and (d / "zipalign.exe").is_file()]
        platforms = [p for p in (root / "platforms").glob("android-*") if (p / "android.jar").is_file()]
        if tools and platforms:
            key = lambda p: [int(x) if x.isdigit() else 0 for x in re.split(r"[.\-]", p.name.replace("android-", ""))]
            adb = root / "platform-tools" / "adb.exe"
            return {"build_tools": max(tools, key=key),
                    "android_jar": max(platforms, key=key) / "android.jar",
                    "adb": adb if adb.is_file() else (Path(shutil.which("adb")) if shutil.which("adb") else None)}
    raise PortError("Не найден Android SDK (build-tools + platforms). Поставьте Android Studio или cmdline-tools.")


def find_apktool(cfg):
    if cfg["apktool"].lower() != "auto":
        return Path(cfg["apktool"])
    jars = sorted(TOOLS.glob("apktool*.jar"))
    if jars:
        return jars[-1]
    if not ask("apktool не найден. Скачать последнюю версию с GitHub (iBotPeaches/Apktool)?", True):
        raise PortError("Положите apktool.jar в tools\\")
    asset = github_latest_asset("iBotPeaches/Apktool", r"^apktool_[\d.]+\.jar$")
    if not asset:
        raise PortError("Не нашёл apktool в релизах GitHub - скачайте вручную в tools\\")
    return download(asset[0], TOOLS / asset[1], "apktool")


def is_winlator_xr(apk):
    try:
        with zipfile.ZipFile(apk) as z:
            for name in z.namelist():
                if re.fullmatch(r"classes\d*\.dex", name) and b"Lcom/winlator/xr/runtime/MetaQuest;" in z.read(name):
                    return True
    except (zipfile.BadZipFile, OSError):
        pass
    return False


def find_winlator_apk(cfg):
    if cfg["winlator_apk"].lower() != "auto":
        apk = Path(cfg["winlator_apk"])
        if not is_winlator_xr(apk):
            raise PortError(f"{apk} - не APK WinlatorXR")
        return apk
    places = [TOOLS, ROOT, Path.home() / "Downloads"]
    candidates = []
    for place in places:
        candidates += [p for p in place.glob("*.apk") if re.search(r"(?i)winlator", p.name)]
    for apk in sorted(candidates, key=lambda p: p.stat().st_mtime, reverse=True):
        if is_winlator_xr(apk):
            return apk
    info("APK WinlatorXR не найден в tools\\ и Загрузках.")
    if ask("Скачать последний релиз WinlatorXR с GitHub (WinlatorXR/WinlatorXR)?", True):
        try:
            asset = github_latest_asset("WinlatorXR/WinlatorXR", r"\.apk$")
        except Exception as e:
            asset = None
            warn(f"GitHub недоступен: {e}")
        if asset:
            apk = download(asset[0], TOOLS / asset[1], "WinlatorXR")
            if is_winlator_xr(apk):
                return apk
    raise PortError("Скачайте APK WinlatorXR (winlatorxr.github.io) и положите в tools\\")


def patch_manifest(path, cfg):
    text = path.read_text(encoding="utf-8")
    new = cfg["package"]

    if new != ORIGINAL_PACKAGE:
        # Relative class names (".saves.X") would resolve against the new package, so they are
        # spelled out first; the classes themselves keep their WinlatorXR names.
        text = re.sub(r'(android:(?:name|targetActivity)=")\.', r"\g<1>" + ORIGINAL_PACKAGE + ".", text)
        text = re.sub(r'(<manifest\b[^>]*?\bpackage=")[^"]+"', r"\g<1>" + new + '"', text, count=1)
        # Authorities and permissions must be unique per installed app
        text = re.sub(r'(android:authorities=")' + re.escape(ORIGINAL_PACKAGE), r"\g<1>" + new, text)
        text = re.sub(r'(<(?:permission|uses-permission)\b[^>]*android:name=")' + re.escape(ORIGINAL_PACKAGE),
                      r"\g<1>" + new, text)

    # WinlatorXR's own launcher entries go, ours becomes the only icon
    text = re.sub(r'\s*<category android:name="android\.intent\.category\.LAUNCHER"\s*/>', "", text)
    # Debuggable lets "adb shell run-as" read the container (game settings, logs) for diagnosis
    text = re.sub(r'\s*android:debuggable="[^"]*"', "", text)
    if cfg.get("debuggable"):
        text = re.sub(r"<application\b", '<application android:debuggable="true"', text, count=1)
    label = cfg["app_name"].replace("&", "&amp;").replace('"', "&quot;")
    text = re.sub(r'(<application\b[^>]*?)android:label="[^"]*"', rf'\1android:label="{label}"', text, count=1)
    text = re.sub(r'(<application\b[^>]*?)android:icon="[^"]*"', r'\1android:icon="@drawable/bioshockport_icon"', text, count=1)
    text = re.sub(r'(<application\b[^>]*?)android:roundIcon="[^"]*"', r'\1android:roundIcon="@drawable/bioshockport_icon"', text, count=1)

    activities = f"""
        <activity android:name="com.bioshockport.Launcher" android:exported="true" android:label="{label}"
            android:icon="@drawable/bioshockport_icon" android:screenOrientation="landscape"
            android:theme="@android:style/Theme.Black.NoTitleBar.Fullscreen"
            android:configChanges="keyboard|keyboardHidden|orientation|screenSize|screenLayout|smallestScreenSize|density|navigation">
            <intent-filter>
                <action android:name="android.intent.action.MAIN"/>
                <category android:name="android.intent.category.LAUNCHER"/>
            </intent-filter>
        </activity>
        <activity android:name="com.bioshockport.VrGame" android:exported="true"
            android:configChanges="density|orientation|screenSize|keyboard|keyboardHidden|uiMode"
            android:launchMode="singleTask" android:resizeableActivity="false"
            android:screenOrientation="landscape" android:process=":vr_process"
            android:theme="@style/AppThemeFullscreen">
            <intent-filter>
                <action android:name="android.intent.action.VIEW"/>
                <category android:name="com.oculus.intent.category.VR"/>
            </intent-filter>
        </activity>
    """
    if "com.bioshockport.Launcher" not in text:
        text = text.replace("</application>", activities + "</application>", 1)
    path.write_text(text, encoding="utf-8")


def compile_launcher(java_bin, sdk, out_dex):
    classes = WORK / "classes"
    if classes.exists():
        shutil.rmtree(classes)
    classes.mkdir(parents=True)
    sources = [str(p) for p in ANDROID_SRC.rglob("*.java")]
    run([java_bin / "javac.exe", "--release", "11", "-nowarn", "-encoding", "UTF-8",
         "-cp", sdk["android_jar"], "-d", classes] + sources, quiet=True)
    dex_dir = WORK / "dex"
    if dex_dir.exists():
        shutil.rmtree(dex_dir)
    dex_dir.mkdir()
    # Only the port's classes go into the dex; the WinlatorXR stubs are compile-time only
    ours = [str(p) for p in (classes / "com" / "bioshockport").glob("*.class")]
    run([java_bin / "java.exe", "-cp", sdk["build_tools"] / "lib" / "d8.jar", "com.android.tools.r8.D8",
         "--release", "--min-api", "26", "--lib", sdk["android_jar"], "--classpath", classes,
         "--output", dex_dir] + ours, quiet=True)
    shutil.copy2(dex_dir / "classes.dex", out_dex)


def verify_worldpose_renderer():
    # These private pose-history offsets are specific to the packaged runtime.
    with zipfile.ZipFile(WORK/"apktool_out.apk") as base:
        actual=hashlib.sha256(base.read("lib/arm64-v8a/libxr.so")).hexdigest()
    expected="4d5788da9b5dd6fec603073f62a6654ee98978bb2dcfbdbf1808ed2de7743d8d"
    if actual!=expected:
        raise PortError("WinlatorXR libxr.so changed; verify world-pose history layout before packaging this native integration")
    return actual


def compile_native_cinema(sdk,release=False):
    verify_worldpose_renderer()
    sdk_root = sdk["build_tools"].parents[1]
    candidates = sorted((sdk_root / "ndk").glob("*/toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe"))
    if not candidates:
        raise PortError("Для VR-панели нужен Android NDK в Android SDK/ndk.")
    compiler = candidates[-1]
    target = ROOT / "build" / "android" / "arm64-v8a" / "libbioshockcinema.so"
    target.parent.mkdir(parents=True,exist_ok=True)
    run([compiler,*(["-DBIOSHOCK_RELEASE"] if release else []),"--target=aarch64-linux-android26","-std=c++17","-O2","-shared","-fPIC",
         "-static-libstdc++","-fvisibility=hidden","-I",ROOT/"dependencies"/"build"/"openxr"/"include",
         ANDROID_SRC/"native"/"cinema.cpp",ANDROID_SRC/"native"/"gameperformance.cpp",ANDROID_SRC/"native"/"worldpose.cpp","-o",target,"-lGLESv3","-lEGL","-llog","-ldl"],quiet=True)
    info("Нативный VR-кинотеатр OpenXR собран (arm64)")
    return target


def port_json(cfg, stereo):
    cinema_start = False
    desktop_width,desktop_height=cfg["width"],cfg["height"]
    if cfg["vr_mode"] == "mod":
        profile = configparser.ConfigParser()
        profile.read(ROOT / cfg["native_ini"], encoding="utf-8")
        cinema_start = (not profile.getboolean("VR", "FlatMenus", fallback=True)
                        and profile.getboolean("VR", "MenuStereo", fallback=False))
        world=re.fullmatch(r"(\d+)x(\d+)",profile.get("VR","WorldBackbuffer",fallback="0x0"))
        if world and 1280<=int(world[1])<=4096 and 720<=int(world[2])<=2160:
            desktop_width=max(desktop_width,int(world[1]));desktop_height=max(desktop_height,int(world[2]))
    desktop=cfg["desktop_resolution"]
    if desktop!="auto":
        size=re.fullmatch(r"(\d+)x(\d+)",desktop)
        if not size or not (640<=int(size[1])<=4096 and 480<=int(size[2])<=2160):
            raise PortError("desktop_resolution: auto или размер от 640x480 до 4096x2160")
        desktop_width=max(desktop_width,int(size[1]));desktop_height=max(desktop_height,int(size[2]))
    extras = {
        "execArgs": "-dx9",
        "refreshRate": str(cfg["refresh"]),
        "fullscreenStretched": "1",
        "autoclose": "1",
        "use_xr_fps": "0",
        "use_xr_gamepad": "1" if cfg["controls"] == "gamepad" else "0",
        "use_xr_mouse": "1",
        "use_xr_relative_mouse": "1",
        "use_xr_sbs_stretch": "1",
        "bioshockport_version": cfg["version"],
    }
    native = cfg["vr_mode"] == "mod"
    if native:
        # VR-режимом и геймпадом в игре управляет мод; WinlatorXR в меню даёт лазерную мышь
        extras.update({
            # Desktop mode list must admit the game-only native SETRES.
            # Default.ini still starts the actual game/menu at cfg width/height.
            "screenSize": f"{desktop_width}x{desktop_height}",
            # Android may keep Wine children in background (0-3,7) even while
            # the XR activity is top-app (4-6). Restricting to 4-7 leaves only 7.
            "cpuList": "0,1,2,3,4,5,6,7",
            "use_xr_gamepad": "0",
            "use_xr_keys": "0",
            "use_xr_mouse": "1", # retain the mouse device during Wine initialization
            "use_xr_lightgun": "0" if cinema_start else "1", # cinema mod owns the ray/cursor
            "use_xr_relative_mouse": "0",      # абсолютный указатель для меню
            "xr_screen_distance": "6",           # меню, пауза, загрузка - панель шлема в 6 м
            "use_cs": "0",                      # плоская панель, не изогнутая
            "sharpening_level": "2",           # резкость компоновщика шлема: текст меню читается лучше
            "use_pt": "0",                     # без passthrough: вокруг кадра чёрное, а не комната
            "use_xr_fov_passthrough": "0",
        })
        # Производительность: перекрывает переменные контейнера (ярлык применяется поверх)
        env = ["TU_DEBUG=noconform" if cfg["gpu_tiling"] else "TU_DEBUG=noconform,sysmem",
               "FEX_DISKCACHE=1" if cfg["fex_cache"] else "FEX_DISKCACHE=0",
               "WINE_FAST_YIELD=1", "DXVK_CONFIG_FILE=D:/Builds/Release/dxvk-port.conf"]
        if cfg["vulkan_placed"]:
            # Wine's placed-map path reserves a 32-bit address before asking
            # the native Vulkan wrapper to map it. Box64's broad mmap hack
            # otherwise disables that path in Winlator's launcher.
            env += ["BOX64_MMAP32=0", "WRAPPER_DISABLE_PLACED=0"]
        extras["envVars"] = " ".join(env)
        if cfg["dxvk_debug"]:
            # DXVK is a Windows DLL here: use a path in the Wine drive map.
            extras["envVars"] += " DXVK_LOG_LEVEL=info DXVK_LOG_PATH=D:/Builds/Release"
        else:
            extras["envVars"] += " DXVK_LOG_LEVEL=none WINEDEBUG=-all"
        if cfg["dxvk"]:
            extras["dxwrapper"] = "dxvk"
            extras["dxwrapperConfig"] = (f"version={cfg['dxvk']},framerate=0,maxDeviceMemory=0,async=1,asyncCache=1,"
                                         "vkd3dVersion=2.12-0,vkd3dLevel=12_1")
    elif stereo:
        # 3 = ReShade is on for this game but WinlatorXR leaves the files the port put there alone
        extras["useReshade"] = "3"
    return {
        "title": cfg["app_name"],
        "version": f"{cfg['version']}-{cfg['refresh']}-{cfg['width']}x{cfg['height']}-{int(stereo)}",
        "cacheName": cfg["cache_name"],
        "marker": MARKER,
        "exeRelative": EXE_RELATIVE,
        "shortcutName": "BioShock",
        "container": {
            "name": cfg["app_name"],
            "screenSize": f"{desktop_width}x{desktop_height}",
            "refreshRate": cfg["refresh"],
            "showFPS": False,
            "fullscreenStretched": True,
            "relativeMouseMovement": cfg["vr_mode"] != "mod",
        },
        "shortcutExtras": extras,
        # The DLL enters VR after its first valid Present. Starting the XR
        # layer before Wine's window exists can suspend its helper processes.
        "vr": {"immersive": False, "sbs": False} if native else {"immersive": cfg["immersive"], "sbs": stereo},
        # Быстрый профиль FEX для игры: многоблочная трансляция, облегчённый порядок памяти, быстрая x87
        "fexAppConfig": {"exe": "Bioshock.exe", "config": {"Config": {
            "Multiblock": "1", "TSOEnabled": "1", "VectorTSOEnabled": "0", "MemcpySetTSOEnabled": "0",
            "HalfBarrierTSOEnabled": "1", "X87ReducedPrecision": "1"}}},
    }


def keystore(java_bin):
    ks = TOOLS / "bioshockport.keystore"
    secret_file = TOOLS / "bioshockport.keystore.txt"
    if ks.is_file() and secret_file.is_file():
        return ks, secret_file.read_text().strip()
    password = secrets.token_hex(12)
    TOOLS.mkdir(exist_ok=True)
    run([java_bin / "keytool.exe", "-genkeypair", "-keystore", ks, "-alias", "bioshockport",
         "-keyalg", "RSA", "-keysize", "2048", "-validity", "10000", "-storepass", password,
         "-keypass", password, "-dname", "CN=BioShock Quest Port"], quiet=True)
    secret_file.write_text(password)
    info(f"Создан ключ подписи {ks.name} (храните его: обновления APK должны быть подписаны им же)")
    return ks, password


# WinlatorXR after every Quest recenter (it happens when the headset wakes up) rebuilds the
# reference spaces it reports the app's poses in, offset by the current head pose, while the frame
# layer stays in the plain local space: from then on the game renders with one pose and the headset
# shows the frame with another, and the picture swims and breaks on every head turn until restart.
# The branch in initFrame that skips that rebuild is made unconditional (as on the first launch).
# Tried 2026-10-12 and made it worse: that rebuild is what keeps the app's poses in line with
# the frame layer after a recenter, so it stays. List kept empty on purpose.
LIBXR_PATCHES = [
    # (0x37634, bytes.fromhex("601a0054"), bytes.fromhex("d3000014")),  # b.eq 0x37980 -> b 0x37980
]


def patch_libxr(data):
    data = bytearray(data)
    for offset, old, new in LIBXR_PATCHES:
        if data[offset:offset + len(old)] == old:
            data[offset:offset + len(new)] = new
        elif data[offset:offset + len(new)] != new:
            raise PortError(f"libxr.so: unexpected bytes at 0x{offset:x}, WinlatorXR version changed")
    return bytes(data)


def build_apk(cfg, game, apk, stereo, quick=False):
    java_bin = find_java()
    sdk = find_sdk()
    WORK.mkdir(exist_ok=True)
    # WinlatorXR repacked by apktool with the port's manifest and icon, without the port's own
    # launcher and settings: those are added below, so --quick only redoes that part
    base = WORK / "apktool_out.apk"
    stamp = WORK / "apktool_out.src"
    if apk is None:
        if not quick or not base.is_file() or not stamp.is_file():
            raise PortError("Нет проверенной основы APK; используйте обычную сборку без --cached-apk")
        source_id = stamp.read_text(encoding="utf-8")
        if not source_id.endswith(f"|{cfg['app_name']}|{cfg['debuggable']}"):
            raise PortError("Основа APK собрана с другими именем/отладкой; нужна обычная сборка")
    else:
        source_id = f"{apk.resolve()}|{apk.stat().st_size}|{apk.stat().st_mtime_ns}|{cfg['app_name']}|{cfg['debuggable']}"
    if quick and base.is_file() and stamp.is_file() and stamp.read_text(encoding="utf-8") == source_id:
        info("--quick: основа APK уже собрана, обновляю только загрузчик и настройки")
    else:
        apktool = find_apktool(cfg)
        info(f"JDK: {java_bin}\n  SDK build-tools: {sdk['build_tools'].name}, {sdk['android_jar'].parent.name}\n  apktool: {apktool.name}")
        src = WORK / "apk_src"
        if src.exists():
            shutil.rmtree(src)
        # -s keeps the dex files as they are: nothing of WinlatorXR's code is decompiled or rebuilt
        run([java_bin / "java.exe", "-jar", apktool, "d", "-f", "-s", "-o", src, apk])

        patch_manifest(src / "AndroidManifest.xml", cfg)
        yml = src / "apktool.yml"
        text = yml.read_text(encoding="utf-8")
        # Resources keep following the manifest package (apktool 2 and 3 keys), so WinlatorXR's
        # getIdentifier(..., getPackageName()) lookups keep working
        text = re.sub(r"renameManifestPackage:.*", "renameManifestPackage: null", text)
        text = re.sub(r"(?m)^(\s+)packageName:.*$", rf"\g<1>packageName: {cfg['package']}", text)
        yml.write_text(text, encoding="utf-8")

        icon = src / "res" / "drawable-nodpi" / "bioshockport_icon.png"
        icon.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(game / "Builds" / "Release" / "Bioshock-MCE.png", icon)

        run([java_bin / "java.exe", "-jar", apktool, "b", "-f", "-o", base, src])
        stamp.write_text(source_id, encoding="utf-8")

    dex = WORK / "bioshockport.dex"
    compile_launcher(java_bin, sdk, dex)
    cinema_lib = compile_native_cinema(sdk,release=not cfg.get("debuggable",False))
    settings = json.dumps(port_json(cfg, stereo), indent=2, ensure_ascii=False).encode("utf-8")

    unsigned = WORK / "unsigned.apk"
    with zipfile.ZipFile(base) as src_zip, zipfile.ZipFile(unsigned, "w") as out:
        numbers = []
        for item in src_zip.infolist():
            m = re.fullmatch(r"classes(\d*)\.dex", item.filename)
            if m:
                numbers.append(int(m.group(1) or 1))
            if item.filename == "assets/bioshockport/port.json":
                continue
            # Entries keep their compression: stored libraries and resources.arsc must stay stored
            data = src_zip.read(item)
            if item.filename == "lib/arm64-v8a/libxr.so" and LIBXR_PATCHES:
                data = patch_libxr(data)
            out.writestr(item, data, compress_type=item.compress_type)
        name = f"classes{max(numbers) + 1}.dex"
        out.write(dex, name, compress_type=zipfile.ZIP_DEFLATED)
        out.write(cinema_lib,"lib/arm64-v8a/libbioshockcinema.so",compress_type=zipfile.ZIP_STORED)
        out.writestr("assets/bioshockport/port.json", settings, compress_type=zipfile.ZIP_DEFLATED)
    info(f"Загрузчик порта добавлен как {name}")

    aligned = WORK / "aligned.apk"
    run([sdk["build_tools"] / "zipalign.exe", "-f", "-p", "4", unsigned, aligned], quiet=True)
    ks, password = keystore(java_bin)
    final = OUTPUT / f"{re.sub(r'[^\w.-]+', '_', cfg['app_name'])}.apk"
    OUTPUT.mkdir(exist_ok=True)
    run([java_bin / "java.exe", "-jar", sdk["build_tools"] / "lib" / "apksigner.jar", "sign",
         "--ks", ks, "--ks-key-alias", "bioshockport", "--ks-pass", f"pass:{password}",
         "--key-pass", f"pass:{password}", "--out", final, aligned], quiet=True)
    run([java_bin / "java.exe", "-jar", sdk["build_tools"] / "lib" / "apksigner.jar", "verify", final], quiet=True)
    info(f"APK готов: {final} ({final.stat().st_size / 2 ** 20:.0f} МБ)")
    return final, sdk


# --------------------------------------------------------------------------------------------
# 6. install
# --------------------------------------------------------------------------------------------

def write_install_bat(apk, cache, cfg, adb=None):
    bat = OUTPUT / "INSTALL_TO_QUEST.bat"
    bat.write_text('@echo off\ncall "%~dp0..\\..\\QUEST.bat" install %*\n', encoding="utf-8")
    return bat


def adb_install(sdk, apk, cache, cfg):
    adb = sdk and sdk.get("adb")
    if not adb:
        warn("adb не найден - используйте output\\INSTALL_TO_QUEST.bat с adb из platform-tools")
        return
    run([sys.executable, "-I", ROOT / "scripts" / "quest" / "install.py", "--adb", adb])


# --------------------------------------------------------------------------------------------

def main():
    global ASSUME_YES
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(encoding="utf-8", errors="replace")
        except (AttributeError, ValueError):
            pass
    parser = argparse.ArgumentParser(description="BioShock -> Meta Quest 2 (WinlatorXR) port builder")
    parser.add_argument("--config", default=str(ROOT / "config" / "quest" / "port.ini"))
    parser.add_argument("--game", help="путь к папке BioShock (иначе ищется автоматически)")
    parser.add_argument("--yes", action="store_true", help="отвечать «да» на вопросы о скачивании")
    parser.add_argument("--install", action="store_true", help="сразу установить в шлем по adb")
    parser.add_argument("--no-apk", action="store_true", help="собрать только папку игры")
    parser.add_argument("--quick", action="store_true", help="не пересобирать WinlatorXR через apktool, обновить только загрузчик и настройки")
    parser.add_argument("--cached-apk", action="store_true", help="переподписать уже подготовленную основу APK без исходного WinlatorXR APK")
    args = parser.parse_args()
    ASSUME_YES = args.yes

    cfg = load_config(Path(args.config))
    if args.game:
        cfg["game_path"] = args.game

    step("1/6 Поиск игры")
    game = find_game(cfg["game_path"])
    info(f"BioShock: {game}")

    step("2/6 Папка игры (кеш для шлема)")
    cache = OUTPUT / cfg["cache_name"]
    if cache.resolve().is_relative_to(game.resolve()):
        raise PortError("Папка вывода не может быть внутри папки игры")
    stage_game(game, cache)
    (cache / MARKER).write_text(json.dumps({"port": "bioshock-quest", "version": cfg["version"]}), encoding="utf-8")

    step("3/6 Адаптация файлов и графики")
    tune_ini(cache, cfg)
    remap_keys(cache, cfg, game)
    patch_game_hud(cache,cfg,game)
    drm_ok = unpack_steamstub(cache, cfg)

    apk = None
    if not args.no_apk and not args.cached_apk:
        apk = find_winlator_apk(cfg)
        info(f"WinlatorXR: {apk}")

    step("4/6 VR-мод")
    stereo = install_vr_mod(cache, cfg, apk)
    if cfg["vr_mode"] == "mod":
        launch = port_json(cfg, stereo)
        # Override also updates an already installed launcher without another
        # gigabyte APK transfer and replaces old flat-menu startup overrides.
        # Files-only updates must apply performance/diagnostic extras too,
        # not silently keep the environment embedded in an older APK.
        override = {**launch["vr"], "shortcutExtras": launch["shortcutExtras"],
                    "prefs": {"enable_wine_debug": cfg["wine_debug"],
                              "wine_debug_channels": cfg["wine_debug_channels"]}}
        replace_file(cache / "port_override.json", json.dumps(override, indent=2).encode("utf-8"))

    final_apk, sdk = None, None
    if not args.no_apk:
        step("5/6 Сборка APK")
        final_apk, sdk = build_apk(cfg, game, apk, stereo, args.quick or args.cached_apk)

    step("6/6 Установка")
    if final_apk:
        bat = write_install_bat(final_apk, cache, cfg, sdk.get("adb") if sdk else None)
        info(f"Скрипт установки: {bat}")
        want = args.install or cfg["adb_install"] == "yes" or (
            cfg["adb_install"] == "ask" and ask("Установить в шлем сейчас по USB?", False))
        if want:
            adb_install(sdk, final_apk, cache, cfg)

    print("\n" + "=" * 70)
    print(f"  Папка игры : {cache}")
    print(f"               -> положить в КОРЕНЬ памяти шлема (/sdcard/{cfg['cache_name']})")
    if final_apk:
        print(f"  APK        : {final_apk}")
    print(f"  Частота    : {cfg['refresh']} Гц, разрешение {cfg['width']}x{cfg['height']}, пресет {cfg['preset']}")
    print(f"  VR         : иммерсивный 3DoF {'да' if cfg['immersive'] else 'нет'}, стерео 3D {'да' if stereo else 'нет'}")
    if not drm_ok:
        print("  [!] Bioshock.exe всё ещё под SteamStub - см. предупреждение выше")
    print("=" * 70)


if __name__ == "__main__":
    try:
        main()
    except PortError as e:
        print(f"\n[ОШИБКА] {e}")
        sys.exit(1)
    except KeyboardInterrupt:
        print("\nПрервано")
        sys.exit(130)
