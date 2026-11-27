"""Launch the local Vehicle Control mission and collect evidence from this process."""

import base64
import configparser
import ctypes
import hashlib
import json
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import zipfile
from contextlib import contextmanager, nullcontext
from pathlib import Path
from statistics import mean

ROOT = Path(__file__).resolve().parents[1]
TOKEN = b'"ai_test_harness:0"'
MODS = re.compile(rb'\{mods\b(?:[^{}"]|"(?:[^"\\]|\\.)*")*\}', re.IGNORECASE)
CROSSOVER = Path("/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine")
AUTO_MISSION_PATH = "single/00-bootcamp/02_vehicle-control:0"
SKIP_INTRO = ("testing", "skip_intro")
AUTO_DISMISS_LOADING = ("testing", "auto_dismiss_loading")
SUPPRESS_CURSOR_GRAB = ("testing", "suppress_cursor_grab")
AUTO_MISSION = ("testing", "auto_mission")
HOT_RELOAD = ("testing", "hot_reload")
RESTART_SEQ = ("testing", "restart_seq")
QUIT_SEQ = ("testing", "quit_seq")
SESSION_FILE_REL = Path("local-work/level1/session.json")

# Use the same accepted keys and numeric ranges as the DLL's INI parser.
_NATIVE_CONFIG = (ROOT / "mods/config_and_log.hpp").read_text(encoding="utf-8")
MODULE_KEYS = tuple(re.findall(r"\bm\.(\w+)\s*=\s*detail::parse_bool", _NATIVE_CONFIG))
TUNING_RANGES = {
    key: (kind, float(low), float(high))
    for kind, key, low, high in re.findall(
        r"detail::parse_(float|u32)\(val, t\.(\w+), ([.\d]+)f?, ([.\d]+)f?\)", _NATIVE_CONFIG
    )
}
PROFILE_KNOBS = (
    "grenade_danger_radius_m", "medic_search_radius_m", "squad_share_radius_m",
    "resupply_search_radius_m", "recrew_search_radius_m", "scavenge_search_radius_m",
    "tank_facing_min_angle_deg",
)
BUILTIN_PROFILES = {
    "vanilla": (), "default": (),
    "tight": (4, 22, 12, 32, 18, 10, 32),
    "tactical": (7, 28, 18, 45, 24, 14, 20),
    "wide": (10, 52, 28, 85, 40, 24, 15),
}
PLAYER_MASK = ("testing", "ai_player_mask")
# West (P2, mask 4) holds a slight first-updater edge on the flat symmetric arena;
# single-side E2E runs put our modded AI on the disadvantaged East side (P3, mask 8).
DEFAULT_MODDED_MASK = "8"
SIM_SPEED = ("testing", "sim_speed")
# At 20x, 200 ms of wall time spans four simulation seconds.
E2E_TELEMETRY = {("telemetry", "enabled"): "true", ("telemetry", "snapshot_interval_ms"): "200"}


def player_mask(value):
    """Write decimal uint32 values, including overrides entered in hexadecimal."""
    if not re.fullmatch(r"(?:[0-9]+|0[xX][0-9a-fA-F]+)", value):
        raise ValueError("ai_player_mask requires an unsigned 32-bit integer")
    mask = int(value, 16 if value.lower().startswith("0x") else 10)
    if mask > 0xFFFFFFFF:
        raise ValueError("ai_player_mask exceeds 32 bits")
    return str(mask)


def sim_speed_value(value):
    try:
        speed = float(value)
    except (TypeError, ValueError):
        speed = math.nan
    if speed == 0:
        return "0"
    if not 0.25 <= speed <= 20:
        raise ValueError("sim_speed must be 0 (stock) or between 0.25 and 20")
    return str(speed)


def sweep_profiles(selected, custom=(), sim_speed=8.0):
    """Each profile starts from DLL defaults, independent of the player's INI."""
    speed = sim_speed_value(sim_speed)
    names = [name.strip() for name in selected.split(",")]
    if len(names) != len(set(names)):
        raise ValueError("duplicate profile in --profiles")
    overrides = {
        name: {("tuning", k): str(v) for k, v in zip(PROFILE_KNOBS, values)}
        for name, values in BUILTIN_PROFILES.items()
    }
    for spec in custom:
        name, sep, items = spec.partition(":")
        if (not sep or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]*", name)
                or name.lower() in {n.lower() for n in overrides}):
            raise ValueError(f"invalid or duplicate custom profile name {name!r}")
        settings = {}
        for item in items.split(","):
            key, eq, value = (part.strip() for part in item.partition("="))
            section, key = key.split(".", 1) if "." in key else ("tuning", key)
            if not eq or (section, key) in settings:
                raise ValueError(f"invalid or repeated override {item!r}")
            if (section, key) == PLAYER_MASK:
                value = player_mask(value)
            elif (section, key) == SIM_SPEED:
                value = sim_speed_value(value)
            elif section == "modules" and key in MODULE_KEYS:
                value = value.lower()
                if value in ("on", "yes", "1"):
                    value = "true"
                elif value in ("off", "no", "0"):
                    value = "false"
                elif value not in ("true", "false"):
                    raise ValueError(f"{key} requires true or false")
            elif section == "tuning" and key in TUNING_RANGES:
                kind, low, high = TUNING_RANGES[key]
                if not re.fullmatch(r"[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?", value, re.ASCII):
                    raise ValueError(f"invalid numeric value for {key}")
                if (kind == "u32" and not value.isdecimal()) or not low <= float(value) <= high:
                    raise ValueError(f"{key} must be in [{low:g}, {high:g}]")
            else:
                raise ValueError(f"unknown profile setting {section}.{key}")
            settings[section, key] = value
        overrides[name] = settings
        if name not in names:
            names.append(name)
    profiles = {}
    for name in names:
        if name not in overrides:
            raise ValueError(f"unknown profile {name!r}")
        enabled = "false" if name == "vanilla" else "true"
        profiles[name] = {
            **{("modules", key): enabled for key in MODULE_KEYS},
            **E2E_TELEMETRY,
            PLAYER_MASK: "0" if name == "vanilla" else DEFAULT_MODDED_MASK,
            SIM_SPEED: speed,
            **overrides[name],
        }
    return profiles


def render_profile(settings):
    settings = dict(settings)
    speed = sim_speed_value(settings.pop(SIM_SPEED, 0))
    if speed != "0":
        settings[SIM_SPEED] = speed
    lines = []
    for section in ("telemetry", "testing", "modules", "tuning"):
        lines.append(f"[{section}]")
        lines.extend(f"{key}={value}" for (s, key), value in settings.items() if s == section)
        lines.append("")
    return "\n".join(lines).encode("utf-8")


@contextmanager
def temporary_ini(path, data, write):
    original = path.read_bytes() if path.exists() else None
    try:
        write(path, data)
        yield
    finally:
        if original is None:
            path.unlink(missing_ok=True)
        else:
            write(path, original)


def profile_options(profile=None):
    if profile:
        path = Path(profile).expanduser().resolve()
        path = path if path.name == "options.set" else path / "options.set"
    else:
        profiles = Path.home() / "Documents/My Games/gates of hell/profiles"
        choices = list(profiles.glob("*/options.set"))
        if len(choices) != 1:
            raise ValueError("select the active player profile with --profile PATH")
        path = choices[0]
    if not path.is_file():
        raise ValueError(f"missing game profile {path}")
    return path


def set_mod(data, enabled):
    blocks = list(MODS.finditer(data))
    if len(blocks) > 1:
        raise ValueError("ambiguous mods blocks in options.set")
    if blocks:
        block = blocks[0]
        old = block[0]
        if (TOKEN in old) == enabled:
            return data
        new = old[:-1] + b" " + TOKEN + b"}" if enabled else old.replace(TOKEN, b"")
        if not enabled and not new[5:-1].strip():
            new = b""
        return data[: block.start()] + new + data[block.end() :]
    if not enabled:
        return data
    end = data.rstrip().rfind(b"}")
    if end < 0 or not data.removeprefix(b"\xef\xbb\xbf").lstrip().startswith(b"{options") or data[end + 1 :].strip():
        raise ValueError("unexpected options.set root")
    newline = b"\r\n" if b"\r\n" in data else b"\n"
    return data[:end] + b"\t{mods " + TOKEN + b"}" + newline + data[end:]


# Direct children of the GEM video block. Keep the adapter, window mode,
# resolution subtree and options in other sections byte-for-byte intact.
LOW_VIDEO = {
    b"preset": b"low", b"detail": b"detail_low", b"shadow": b"off",
    b"filtering": b"bilinear", b"model_quality": b"low", b"fx_quality": b"low",
    b"water_quality": b"low", b"bumpType": b"none", b"water_reflection": b"none",
    b"clip_distance": b"1.0", b"lens_effects_options": b"low", b"fxaa": b"0",
    b"hdr": b"0", b"bloom": b"0", b"waterBump": b"0", b"vls": b"0",
}
VIDEO_FLAGS = (
    b"vertical_sync antialiasing grass dof glow rim_light filmGrain specular environment "
    b"lens_refraction softIntersection fake_translucency alphaToCoverage _ssao"
).lower().split()


def benchmark_options(data):
    """Patch the E2E game and video settings, including nested effect blocks."""
    # Locate whole brace records, skipping braces in strings and GEM comments.
    tokens = rb';[^\r\n]*|"(?:[^"\\]|\\.)*"|\{\s*([^\s{}"]*)|\}'
    spans = {}
    stack = []
    for token in re.finditer(tokens, data):
        if token[1] is not None:
            stack.append((token[1].lower(), token.start()))
        elif token[0] == b"}":
            if not stack:
                raise ValueError("unbalanced options.set")
            path = tuple(name for name, _ in stack)
            _, start = stack.pop()
            if len(path) <= 3:
                spans.setdefault(path, []).append((start, token.end()))
    root = (b"options",)
    if stack or len(spans.get(root, ())) != 1:
        raise ValueError("unexpected options.set root")

    edits = []
    for section, settings in ((b"game", {b"difficulty": b"heroic", b"fog_of_war": b"fog_off"}),
                              (b"video", LOW_VIDEO)):
        path = (*root, section)
        parents = spans.get(path, ())
        if len(parents) > 1:
            raise ValueError(f"ambiguous {section.decode()} block in options.set")
        missing = []
        for name, value in settings.items():
            block = b"{" + name + b" " + value + b"}"
            existing = spans.get((*path, name.lower()), ())
            if existing:
                edits.append((*existing[0], block))
                edits.extend((lo, hi, b"") for lo, hi in existing[1:])
            else:
                missing.append(block)
        if missing:
            addition = b" ".join(missing)
            if not parents:
                addition = b"{" + section + b" " + addition + b"}"
            pos = (parents or spans[root])[0][1] - 1
            edits.append((pos, pos, addition))
    for name in VIDEO_FLAGS:
        edits.extend((lo, hi, b"") for lo, hi in spans.get((*root, b"video", name), ()))
    for lo, hi, replacement in sorted(edits, reverse=True):
        data = data[:lo] + replacement + data[hi:]
    return data


def options_backup(path):
    return path.with_name("options.set.ai_test_harness.bak")


def profile_mod(path, write, restore=False, *, heroic=False):
    backup = options_backup(path)
    if restore:
        if not backup.exists():
            return
        original = backup.read_bytes()
        data = path.read_bytes() if path.exists() else None
        atoms = re.compile(rb'"(?:[^"\\]|\\.)*"|[{}]|[^\s{}"]+')
        changed = original
        # Component runs retain unrelated options the game saved on exit.
        if not heroic and data is not None:
            active = set_mod(original, True)
            actual = atoms.findall(data)
            same_options = actual == atoms.findall(active)
            if not same_options and b"heroic" in data:
                same_options = actual == atoms.findall(benchmark_options(active))
            if not same_options:
                changed = set_mod(data, False)
        if data != changed:
            write(path, changed)
        backup.unlink()
    else:
        data = path.read_bytes()
        changed = set_mod(data, True)
        if heroic:
            changed = benchmark_options(changed)
        if heroic or data != changed:
            if not backup.exists():
                with backup.open("xb") as out:
                    out.write(data)
            if data != changed:
                write(path, changed)


def wine_prefix(args):
    if os.name == "nt":
        return [], os.environ.copy()
    exe = args.wine or (str(CROSSOVER) if CROSSOVER.exists() else shutil.which("wine"))
    if not exe:
        raise ValueError("supply --wine PATH to the CrossOver or Wine launcher")
    if "CrossOver" in str(Path(exe).resolve()):
        return [exe, "--bottle", args.bottle, "--dll", "version=n,b"], os.environ.copy()
    env = os.environ.copy()
    env["WINEDLLOVERRIDES"] = "version=n,b;" + env.get("WINEDLLOVERRIDES", "")
    return [exe], env


def build_helper():
    src = ROOT / "scripts/live_ui_helper.c"
    out = ROOT / "local-work/build-ai/live_ui_helper.exe"
    if out.exists() and out.stat().st_mtime_ns >= src.stat().st_mtime_ns:
        return out
    cc = shutil.which("x86_64-w64-mingw32-gcc") or (shutil.which("gcc") if os.name == "nt" else None)
    if not cc:
        raise ValueError("install x86_64-w64-mingw32-gcc to compile live_ui_helper.c")
    out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=out.parent) as tmp:
        binary = Path(tmp) / out.name
        subprocess.run(
            [
                cc,
                "-std=c11",
                "-O2",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-static",
                str(src),
                "-o",
                str(binary),
            ],
            check=True,
        )
        binary.replace(out)
    return out


class GameLog:
    def __init__(self, path):
        self.path = path
        self.before = path.read_bytes() if path.exists() else b""
        self.stamp = path.stat().st_mtime_ns if path.exists() else None

    def read(self):
        if not self.path.exists():
            return ""
        data = self.path.read_bytes()
        if data.startswith(self.before):
            data = data[len(self.before) :]
        elif self.path.stat().st_mtime_ns == self.stamp:
            return ""
        return data.decode(errors="replace")


def _wait_log(log, message, deadline, *, process=None, ui=None, pulse=None, seconds=90):
    stop = min(deadline, time.monotonic() + seconds)
    last_alive_check = time.monotonic()
    while time.monotonic() < stop:
        data = log.read()
        if "***************** Exception" in data:
            detail = data.partition("***************** Exception *****************")[2].strip()
            reason = detail.partition("\n")[0] or "during game startup or mission execution"
            raise ValueError(f"GEM exception {reason}")
        if message in data:
            return data
        if process is not None:
            if process.poll() is not None:
                raise ValueError(f"game exited before {message}")
        elif ui is not None and time.monotonic() - last_alive_check >= 3.0:
            last_alive_check = time.monotonic()
            if ui("present") != 0:
                raise ValueError(f"game exited before {message}")
        if pulse:
            pulse(data)
        time.sleep(0.35)
    raise ValueError(f"timed out waiting for {message}")


def enable_silhouettes_bytes(data):
    """Ensure {highlight_silhouettes} is enabled in settings.set for E2E unit coloring."""
    updated, count = re.subn(rb"\{highlight_silhouettes\s+0\}", b"{highlight_silhouettes}", data)
    if count or b"{highlight_silhouettes}" in data:
        return updated
    end = data.rstrip().rfind(b"}")
    if end < 0:
        return data
    newline = b"\r\n" if b"\r\n" in data else b"\n"
    return data[:end] + b"\t{highlight_silhouettes}" + newline + data[end:]


@contextmanager
def ensure_silhouettes(settings_path, write):
    """Temporarily ensure unit silhouettes are enabled in settings.set during E2E runs."""
    if not settings_path.is_file():
        yield
        return
    original = settings_path.read_bytes()
    patched = enable_silhouettes_bytes(original)
    if patched != original:
        write(settings_path, patched)
    try:
        yield
    finally:
        if patched != original:
            write(settings_path, original)


def game_window_id():
    """Return the macOS CGWindowNumber of the Gates of Hell window, or None."""
    if sys.platform != "darwin":
        return None
    try:
        cg = ctypes.CDLL("/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics")
        cf = ctypes.CDLL("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation")
        cf.CFArrayGetCount.argtypes = [ctypes.c_void_p]
        cf.CFArrayGetCount.restype = ctypes.c_long
        cf.CFArrayGetValueAtIndex.argtypes = [ctypes.c_void_p, ctypes.c_long]
        cf.CFArrayGetValueAtIndex.restype = ctypes.c_void_p
        cf.CFDictionaryGetValue.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
        cf.CFDictionaryGetValue.restype = ctypes.c_void_p
        cf.CFStringCreateWithCString.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_uint32]
        cf.CFStringCreateWithCString.restype = ctypes.c_void_p
        cf.CFStringGetCString.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_long, ctypes.c_uint32]
        cf.CFStringGetCString.restype = ctypes.c_bool
        cf.CFNumberGetValue.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p]
        cf.CFNumberGetValue.restype = ctypes.c_bool
        cf.CFRelease.argtypes = [ctypes.c_void_p]
        cg.CGWindowListCopyWindowInfo.argtypes = [ctypes.c_uint32, ctypes.c_uint32]
        cg.CGWindowListCopyWindowInfo.restype = ctypes.c_void_p

        utf8 = 0x08000100
        k_num = cf.CFStringCreateWithCString(None, b"kCGWindowNumber", utf8)
        k_name = cf.CFStringCreateWithCString(None, b"kCGWindowName", utf8)
        k_owner = cf.CFStringCreateWithCString(None, b"kCGWindowOwnerName", utf8)
        k_layer = cf.CFStringCreateWithCString(None, b"kCGWindowLayer", utf8)
        k_bounds = cf.CFStringCreateWithCString(None, b"kCGWindowBounds", utf8)
        k_w = cf.CFStringCreateWithCString(None, b"Width", utf8)
        k_h = cf.CFStringCreateWithCString(None, b"Height", utf8)
        # Use kCGWindowListExcludeDesktopElements (16) without OnScreenOnly so
        # windowed_fullscreen Wine windows on another macOS Space are still found.
        arr = cg.CGWindowListCopyWindowInfo(16, 0)
        wid = None
        fallback_wid = None
        fallback_area = 0.0
        try:
            buf = ctypes.create_string_buffer(256)
            for i in range(cf.CFArrayGetCount(arr)):
                d = cf.CFArrayGetValueAtIndex(arr, i)
                layer_p = cf.CFDictionaryGetValue(d, k_layer)
                layer = ctypes.c_int32(0)
                if layer_p and cf.CFNumberGetValue(layer_p, 3, ctypes.byref(layer)) and layer.value != 0:
                    continue
                num_p = cf.CFDictionaryGetValue(d, k_num)
                val = ctypes.c_int32(0)
                if not (num_p and cf.CFNumberGetValue(num_p, 3, ctypes.byref(val)) and val.value > 0):
                    continue
                name_p = cf.CFDictionaryGetValue(d, k_name)
                if name_p and cf.CFStringGetCString(name_p, buf, 256, utf8) and b"gates of hell" in buf.value.lower():
                    wid = val.value
                    break
                owner_p = cf.CFDictionaryGetValue(d, k_owner)
                if owner_p and cf.CFStringGetCString(owner_p, buf, 256, utf8) and b"call_to_arms" in buf.value.lower():
                    bounds_p = cf.CFDictionaryGetValue(d, k_bounds)
                    w = ctypes.c_double(0)
                    h = ctypes.c_double(0)
                    if bounds_p:
                        wp = cf.CFDictionaryGetValue(bounds_p, k_w)
                        hp = cf.CFDictionaryGetValue(bounds_p, k_h)
                        if wp:
                            cf.CFNumberGetValue(wp, 6, ctypes.byref(w))
                        if hp:
                            cf.CFNumberGetValue(hp, 6, ctypes.byref(h))
                    area = w.value * h.value
                    if w.value >= 400 and h.value >= 300 and area > fallback_area:
                        fallback_area = area
                        fallback_wid = val.value
        finally:
            for ref in (arr, k_num, k_name, k_owner, k_layer, k_bounds, k_w, k_h):
                if ref:
                    cf.CFRelease(ref)
        return wid or fallback_wid
    except OSError:
        return None


def capture_frame(dst):
    """Capture a silent JPEG screenshot of the game window (best-effort, never fails a run)."""
    if sys.platform != "darwin":
        return False
    wid = game_window_id()
    if not wid:
        return False
    cmd = ["screencapture", "-x", "-o", "-t", "jpg", "-l", str(wid), str(dst)]
    try:
        res = subprocess.run(cmd, check=False, timeout=5, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return res.returncode == 0 and dst.is_file()
    except (OSError, subprocess.SubprocessError):
        return False


def _import_live_visual():
    if __package__:
        from . import live_visual
    else:
        import live_visual
    return live_visual


def _wait_battle_with_screenshots(log, suite, deadline, *, process=None, ui=None,
                                  frames_dir=None, screenshot_interval=0.0, telemetry_path=None):
    live_visual = _import_live_visual()

    snap_pulse = None
    meta = {}
    if frames_dir is not None and screenshot_interval > 0:
        frames_dir.mkdir(parents=True, exist_ok=True)
        battle_t0 = time.monotonic()
        state = {"next": battle_t0 + 0.7, "idx": 0}

        def take_frame(_data=None):
            now = time.monotonic()
            if now >= state["next"]:
                elapsed = int(round(now - battle_t0))
                dst = frames_dir / f"frame-{state['idx']:03d}_{elapsed:03d}s.jpg"
                tick = live_visual.latest_sim_tick(telemetry_path)
                if capture_frame(dst):
                    if tick is not None:
                        meta[dst.name] = tick
                    state["idx"] += 1
                state["next"] = now + screenshot_interval

        snap_pulse = take_frame
    _wait_log(log, f"GOH_L1_DONE_{suite}", deadline, process=process, ui=ui,
              pulse=snap_pulse, seconds=max(0, deadline - time.monotonic()))
    if frames_dir is not None and screenshot_interval > 0:
        elapsed = int(round(time.monotonic() - battle_t0))
        dst = frames_dir / f"frame-{state['idx']:03d}_{elapsed:03d}s_end.jpg"
        tick = live_visual.latest_sim_tick(telemetry_path)
        if capture_frame(dst) and tick is not None:
            meta[dst.name] = tick
        if meta:
            (frames_dir / "frames_meta.json").write_text(json.dumps(meta, indent=2) + "\n", encoding="utf-8")


def drive(ui, process, log, suite, deadline, *, auto_launch=False, frames_dir=None,
          screenshot_interval=0.0, telemetry_path=None):
    def wait(message, pulse=None, seconds=90):
        return _wait_log(log, message, deadline, process=process, ui=ui, pulse=pulse, seconds=seconds)

    def click(x, y):
        if ui("click", str(x), str(y)):
            raise ValueError("game window did not accept a menu click")
        time.sleep(0.65)

    start_marker = f"Start mission {AUTO_MISSION_PATH}"
    if auto_launch:
        main_seen_at = None
        fallback_needed = False
        stop = min(deadline, time.monotonic() + 180)
        while time.monotonic() < stop:
            data = log.read()
            if "***************** Exception" in data:
                detail = data.partition("***************** Exception *****************")[2].strip()
                reason = detail.partition("\n")[0] or "during game startup or mission execution"
                raise ValueError(f"GEM exception {reason}")
            if start_marker in data:
                if "Mod: activate mod ai_test_harness" not in data:
                    raise ValueError("game did not mount ai_test_harness")
                break
            if "GUI: Activate page page_main" in data:
                if "Mod: activate mod ai_test_harness" not in data:
                    raise ValueError("game did not mount ai_test_harness")
                if main_seen_at is None:
                    main_seen_at = time.monotonic()
                elif time.monotonic() - main_seen_at >= 8.0:
                    fallback_needed = True
                    break
            if process.poll() is not None:
                raise ValueError(f"game exited before {start_marker}")
            time.sleep(0.25)
        else:
            raise ValueError(f"timed out waiting for {start_marker}")
        if fallback_needed:
            click(287, 347)  # Singleplayer
            wait("GUI: Activate page page_singleplayer", seconds=10)
            click(251, 95)  # Campaign
            click(252, 145)  # Select campaign
            wait("GUI: Activate page eCampaignArmySelectionPopup", seconds=10)
            click(282, 402)  # Tutorials
            wait("GUI: Close active overlay page eCampaignArmySelectionPopup", seconds=10)
            click(252, 98)  # Basic Combat Training
            click(246, 113)  # Vehicle Control
            wait(start_marker, seconds=12)
    else:
        # Only send an intro key before the main page is active. Alternate keys for both movies.
        keys = iter(("esc", "space") * 150)
        wait("GUI: Activate page page_main", lambda _: ui(next(keys, "space")), 180)
        if "Mod: activate mod ai_test_harness" not in log.read():
            raise ValueError("game did not mount ai_test_harness")
        click(287, 347)  # Singleplayer
        wait("GUI: Activate page page_singleplayer", seconds=10)
        click(251, 95)  # Campaign
        click(252, 145)  # Select campaign
        wait("GUI: Activate page eCampaignArmySelectionPopup", seconds=10)
        click(282, 402)  # Tutorials
        wait("GUI: Close active overlay page eCampaignArmySelectionPopup", seconds=10)
        click(252, 98)  # Basic Combat Training
        click(246, 113)  # Vehicle Control
        wait(start_marker, seconds=12)

    prompt_keys = iter((("space",), ("click", "449", "548")) * 100)
    begin_seen_at = None

    def loading_prompt(data):
        nonlocal begin_seen_at
        if "GOH_L1_BEGIN_" in data:
            if auto_launch:
                if begin_seen_at is None:
                    begin_seen_at = time.monotonic()
                elif time.monotonic() - begin_seen_at >= 8.0:
                    ui(*next(prompt_keys, ("space",)))
            else:
                ui(*next(prompt_keys, ("space",)))

    wait("GUI: Activate page page_scene_game", loading_prompt, seconds=150)
    print("Vehicle Control is running", file=sys.stderr)
    _wait_battle_with_screenshots(
        log, suite, deadline, process=process, ui=ui,
        frames_dir=frames_dir, screenshot_interval=screenshot_interval, telemetry_path=telemetry_path,
    )


def drive_restart(ui, process, log, suite, deadline, *, frames_dir=None,
                  screenshot_interval=0.0, telemetry_path=None):
    """Wait for an in-process cmGameRestart trial to load and finish."""
    start_marker = f"Start mission {AUTO_MISSION_PATH}"
    _wait_log(log, start_marker, deadline, process=process, ui=ui, seconds=30)
    prompt_keys = iter((("space",), ("click", "449", "548")) * 100)
    begin_seen_at = None

    def loading_prompt(data):
        nonlocal begin_seen_at
        if "GOH_L1_BEGIN_" in data:
            if begin_seen_at is None:
                begin_seen_at = time.monotonic()
            elif time.monotonic() - begin_seen_at >= 8.0:
                ui(*next(prompt_keys, ("space",)))

    _wait_log(log, "GUI: Activate page page_scene_game", deadline, process=process, ui=ui, pulse=loading_prompt,
              seconds=120)
    print("Vehicle Control restarted", file=sys.stderr)
    _wait_battle_with_screenshots(
        log, suite, deadline, process=process, ui=ui,
        frames_dir=frames_dir, screenshot_interval=screenshot_interval, telemetry_path=telemetry_path,
    )


def stop_game(ui, process, log, *, ini=None, write=None, merge_ini=None):
    def is_dead():
        if process is not None:
            return process.poll() is not None
        return ui("present") != 0

    if is_dead():
        return True
    baseline = len(log.read())
    normal_exit = ui("dismiss") != 0
    if not normal_exit:
        for _ in range(65):
            if is_dead():
                return True
            time.sleep(0.25)
        return is_dead()

    suppress_ui_clicks = False
    if ini is not None and write is not None and merge_ini is not None and ini.exists():
        cfg = configparser.ConfigParser(strict=False, interpolation=None, inline_comment_prefixes=(";", "#"))
        cfg.read(ini, encoding="utf-8-sig")
        suppress_ui_clicks = cfg.getboolean("testing", "suppress_cursor_grab", fallback=False)
        next_quit = cfg.getint("testing", "quit_seq", fallback=0) + 1
        try:
            write(ini, merge_ini(ini, {QUIT_SEQ: str(next_quit)}))
            for _ in range(40):
                if is_dead():
                    return True
                time.sleep(0.25)
        except OSError:
            pass

    ui("close")
    if suppress_ui_clicks:
        for _ in range(12):
            if is_dead():
                return True
            time.sleep(0.25)
        if process is not None:
            try:
                process.terminate()
                process.wait(timeout=5)
            except (OSError, subprocess.SubprocessError):
                try:
                    process.kill()
                except OSError:
                    pass
        return is_dead()

    confirmed = False
    for attempt in range(65):
        if is_dead():
            return True
        dialog = "Activate page genereic_status_dialog" in log.read()[baseline:]
        if not confirmed and dialog:
            confirmed = ui("click", "483", "321") == 0
        elif attempt == 5 and not dialog:
            # During a scenario the game can defer its exit confirmation until Escape.
            ui("esc")
        time.sleep(0.25)
    return is_dead()


def wait_witness(path, offset, mid, deadline):
    """Only accept a complete record appended during this launch."""
    while time.monotonic() < deadline:
        if path.exists():
            with path.open("rb") as rows:
                rows.seek(offset)
                for line in rows:
                    if not line.endswith(b"\n"):
                        break
                    offset += len(line)
                    try:
                        row = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    if row.get("event") == "actor_snapshot" and row.get("mid") == mid:
                        return
        time.sleep(0.25)
    raise ValueError("completion marker arrived without a sampled completion witness")


def launch_timeout(suite, config, requested):
    if requested is not None:
        return requested
    if suite == "e2e":
        try:
            speed = config.getfloat("testing", "sim_speed", fallback=0)
        except ValueError:
            speed = 0  # The native parser also accepts "off".
        if 0.25 <= speed < 1:
            return 720 + math.ceil(300 * (1 / speed - 1))
    return 720


def file_sha256(path):
    h = hashlib.sha256()
    with path.open("rb") as src:
        for block in iter(lambda: src.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def session_path():
    return ROOT / SESSION_FILE_REL


def load_session():
    path = session_path()
    if not path.is_file():
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None


def save_session(state, write):
    payload = {k: v for k, v in state.items() if k != "process"}
    write(session_path(), (json.dumps(payload, indent=2) + "\n").encode("utf-8"))


def clear_session():
    session_path().unlink(missing_ok=True)


def extract_session_start(data):
    found = None
    for line in data.splitlines():
        if b"session_start" not in line:
            continue
        try:
            row = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(row, dict) and row.get("event") == "session_start":
            found = line.decode("utf-8", errors="replace")
    return found


def extract_ai_init_block(text):
    marker = "=== goh_ai InitThread starting"
    idx = text.rfind(marker)
    if idx < 0:
        return None
    line_start = text.rfind("\n", 0, idx)
    block = text[line_start + 1 :] if line_start >= 0 else text[idx:]
    end = re.search(r"=== \d+ hooks active [^\n]*===", block)
    if not end:
        return None
    return block[: end.end()].strip()


def make_ui(prefix, helper, binaries):
    def ui(*args):
        return subprocess.run(
            [*prefix, str(helper), *args],
            check=False,
            cwd=binaries,
            env=None,
            timeout=15,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        ).returncode

    return ui


def _make_ui_env(prefix, helper, binaries, env):
    def ui(*args):
        return subprocess.run(
            [*prefix, str(helper), *args],
            check=False,
            cwd=binaries,
            env=env,
            timeout=15,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        ).returncode

    return ui


def testing_automation_settings(*, hot_reload=False, restart_seq=None, suppress_cursor_grab=True):
    extra = {
        SKIP_INTRO: "true",
        AUTO_DISMISS_LOADING: "true",
        AUTO_MISSION: AUTO_MISSION_PATH,
    }
    if suppress_cursor_grab:
        extra[SUPPRESS_CURSOR_GRAB] = "true"
    if hot_reload:
        extra[HOT_RELOAD] = "true"
    if restart_seq is not None:
        extra[RESTART_SEQ] = str(restart_seq)
    return extra


def wait_hot_reload(binaries, session, deadline, *, force=False):
    dll = binaries / "goh_ai_mod.dll"
    current_sha = file_sha256(dll)
    if not force and current_sha == session.get("dll_sha256"):
        return False
    proxy_log = binaries / "version_proxy.log"
    ai_log = binaries / session.get("ai_filename", "goh_ai.log")
    proxy_off = proxy_log.stat().st_size if proxy_log.exists() else 0
    ai_off = ai_log.stat().st_size if ai_log.exists() else 0
    trigger = binaries / "goh_ai.reload"
    trigger.write_text("reload\n", encoding="utf-8")
    stop = min(deadline, time.monotonic() + 30)
    reloaded = False
    while time.monotonic() < stop:
        if proxy_log.exists():
            raw = proxy_log.read_bytes()
            tail = raw[proxy_off:].decode("utf-8", errors="replace") if len(raw) >= proxy_off else ""
            if "Hot-reload complete gen=" in tail:
                reloaded = True
                break
            if "Hot-reload aborted" in tail or "ShutdownAndUnload deferred/failed" in tail:
                raise ValueError("DLL hot-reload failed (see version_proxy.log)")
        time.sleep(0.25)
    if not reloaded:
        raise ValueError("timed out waiting for version_proxy.log hot-reload confirmation")
    while time.monotonic() < stop:
        if ai_log.exists():
            raw = ai_log.read_bytes()
            tail = raw[ai_off:].decode("utf-8", errors="replace") if len(raw) >= ai_off else ""
            init_block = extract_ai_init_block(tail)
            if init_block:
                session["dll_sha256"] = current_sha
                session["ai_init_block"] = init_block
                session["session_start_line"] = None
                print("Hot-reloaded goh_ai_mod.dll in running game", file=sys.stderr)
                return True
            if "ABORT:" in tail:
                raise ValueError("hot-reloaded DLL aborted during initialization")
        time.sleep(0.2)
    raise ValueError("timed out waiting for hot-reloaded goh_ai_mod.dll hooks active")


def _archive_and_grade(
    evidence, telemetry, ai, offsets, log, binaries, suite, harness, errors, session=None, *, ini_bytes=None
):
    for src, name in ((telemetry, "goh_telemetry.jsonl"), (ai, "goh_ai.log")):
        chunk = b""
        if src.exists():
            with src.open("rb") as inp:
                if src.stat().st_size >= offsets[src]:
                    inp.seek(offsets[src])
                chunk = inp.read()
        if session is not None and name == "goh_telemetry.jsonl":
            # A persistent process keeps appending to the telemetry stream; strip any
            # partial stdio-buffered fragment at either end of the byte slice.
            if offsets[src] > 0 and chunk and not chunk.startswith(b"{"):
                nl = chunk.find(b"\n")
                chunk = chunk[nl + 1 :] if nl >= 0 else b""
            if chunk and not chunk.endswith(b"\n"):
                nl = chunk.rfind(b"\n")
                chunk = chunk[: nl + 1] if nl >= 0 else b""
            found = extract_session_start(chunk)
            if found:
                session["session_start_line"] = found
            else:
                cached = session.get("session_start_line")
                if not cached and src.exists() and offsets[src] > 0:
                    # A prior hot-reload or standalone `reload` command may have emitted
                    # session_start before this trial's offset; recover the latest header.
                    with src.open("rb") as prev:
                        prefix_bytes = prev.read(offsets[src])
                    cached = extract_session_start(prefix_bytes)
                    if cached:
                        session["session_start_line"] = cached
                if cached:
                    chunk = cached.encode("utf-8") + b"\n" + chunk
        elif session is not None and name == "goh_ai.log":
            found_block = extract_ai_init_block(chunk.decode("utf-8", errors="replace"))
            if found_block:
                session["ai_init_block"] = found_block
            elif session.get("ai_init_block"):
                chunk = session["ai_init_block"].encode("utf-8") + b"\n" + chunk
        (evidence / name).write_bytes(chunk)
    game_text = log.read()
    (evidence / "game.log").write_text(game_text, encoding="utf-8")
    if ini_bytes is not None:
        (evidence / "goh_ai.ini").write_bytes(ini_bytes)
    else:
        shutil.copyfile(binaries / "goh_ai.ini", evidence / "goh_ai.ini")
    with (evidence / "goh_telemetry.jsonl").open(encoding="utf-8") as rows:
        report = harness.live_grade.grade(
            rows,
            (evidence / "goh_ai.log").read_text(errors="replace"),
            game_text,
            suite,
        )
    for error in errors:
        report["checks"].append({"phase": "run", "check": "launcher", "passed": False, "evidence": error})
        report["passed"] = False
    if errors and "tactical" in report:
        report["tactical"]["overall_score"] = None
    report["evidence_dir"] = str(evidence)
    if suite == "e2e" and (evidence / "frames").is_dir():
        try:
            _import_live_visual().annotate_e2e_frames(evidence)
        except Exception:  # noqa: BLE001 - visual annotation is best-effort
            pass
    return report


def _restore_session_files(session, harness):
    binaries = Path(session["game_dir"]) / "binaries/x64"
    ini = binaries / "goh_ai.ini"
    for shadow in binaries.glob("goh_ai_mod.live.*.dll"):
        try:
            shadow.unlink(missing_ok=True)
        except OSError:
            pass
    if session.get("had_ini"):
        harness.atomic_write(ini, base64.b64decode(session["ini_backup_b64"]))
    else:
        ini.unlink(missing_ok=True)
    options = Path(session["options"])
    benchmark = bool(session.get("heroic"))
    restore_opts = bool(session.get("restore_options"))
    if session.get("settings_backup_b64") is not None:
        harness.atomic_write(options.with_name("settings.set"), base64.b64decode(session["settings_backup_b64"]))
    if benchmark or restore_opts:
        prior = session.get("prior_selection_b64")
        if prior is not None:
            harness.atomic_write(options, base64.b64decode(prior))
        else:
            profile_mod(options, harness.atomic_write, restore=True, heroic=benchmark)


def close_persistent_session(session, harness, ui=None):
    errors = []
    options = Path(session["options"])
    game_log = Path(session["game_log"])
    binaries = Path(session["game_dir"]) / "binaries/x64"
    if ui is None:
        args = type("Args", (), {"wine": session.get("wine"), "bottle": session.get("bottle", "Steam")})()
        prefix, env = wine_prefix(args)
        ui = _make_ui_env(prefix, build_helper(), binaries, env)
    log = GameLog(game_log)
    ini = binaries / "goh_ai.ini"
    stopped = True
    try:
        stopped = stop_game(
            ui, session.get("process"), log, ini=ini, write=harness.atomic_write, merge_ini=harness.merged_ini
        )
        if not stopped:
            errors.append("game is still open after the exit request")
    except (OSError, subprocess.SubprocessError) as exc:
        stopped = False
        errors.append(str(exc))
    finally:
        if stopped or session.get("heroic"):
            _restore_session_files(session, harness)
        clear_session()
    return stopped, errors


def _make_evidence_dir(evidence):
    if evidence is not None:
        return evidence
    runs = ROOT / "local-work/level1/runs"
    runs.mkdir(parents=True, exist_ok=True)
    return Path(tempfile.mkdtemp(prefix="run-", dir=runs))


def _start_persistent_session(a, game, harness, binaries, options, ui, prefix, env, profile_ini, evidence):
    ini = harness.safe_child(game, Path("binaries/x64/goh_ai.ini"))
    had_ini = ini.exists()
    original_ini = ini.read_bytes() if had_ini else b""
    benchmark = a.suite == "e2e"
    prior_selection = options.read_bytes() if benchmark and options_backup(options).exists() else None
    game_log = a.game_log or options.parents[2] / "log/game.log"

    harness.atomic_write(ini, profile_ini)
    cfg = configparser.ConfigParser(strict=False, interpolation=None, inline_comment_prefixes=(";", "#"))
    cfg.read(ini, encoding="utf-8-sig")
    telemetry = binaries / cfg.get("telemetry", "filename", fallback="goh_telemetry.jsonl")
    ai = binaries / cfg.get("logging", "filename", fallback="goh_ai.log")
    if telemetry.parent != binaries or ai.parent != binaries:
        raise ValueError("expected log filenames beside the DLL")

    offsets = {p: p.stat().st_size if p.exists() else 0 for p in (telemetry, ai)}
    log = GameLog(game_log)
    evidence = _make_evidence_dir(evidence)
    print(f"Run evidence {evidence}", file=sys.stderr)

    session = {
        "game_dir": str(game),
        "options": str(options),
        "game_log": str(game_log),
        "had_ini": had_ini,
        "ini_backup_b64": base64.b64encode(original_ini).decode("ascii"),
        "prior_selection_b64": (
            base64.b64encode(prior_selection).decode("ascii") if prior_selection is not None else None
        ),
        "restore_options": bool(getattr(a, "restore_options", False)),
        "suite": a.suite,
        "heroic": benchmark,
        "restart_seq": cfg.getint("testing", "restart_seq", fallback=0),
        "dll_sha256": file_sha256(binaries / "goh_ai_mod.dll"),
        "ai_filename": ai.name,
        "telemetry_filename": telemetry.name,
        "session_start_line": None,
        "ai_init_block": None,
        "wine": getattr(a, "wine", None),
        "bottle": getattr(a, "bottle", "Steam"),
        "process": None,
    }
    errors = []
    auto_launch = bool(cfg.get("testing", "auto_mission", fallback="").strip())
    screenshot_interval = float(getattr(a, "screenshot_interval", 2.5 if benchmark else 0.0) or 0.0)
    frames_dir = (evidence / "frames") if (benchmark and screenshot_interval > 0) else None
    settings_path = options.with_name("settings.set")
    if benchmark and settings_path.is_file():
        orig_settings = settings_path.read_bytes()
        patched_settings = enable_silhouettes_bytes(orig_settings)
        if patched_settings != orig_settings:
            session["settings_backup_b64"] = base64.b64encode(orig_settings).decode("ascii")
            harness.atomic_write(settings_path, patched_settings)
    try:
        profile_mod(options, harness.atomic_write, heroic=benchmark)
        launcher_log = (evidence / "launcher.log").open("w")
        try:
            popen_kw = {
                "cwd": binaries,
                "env": env,
                "stdin": subprocess.DEVNULL,
                "stdout": launcher_log,
                "stderr": subprocess.STDOUT,
            }
            if os.name == "nt":
                popen_kw["creationflags"] = getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0)
            else:
                popen_kw["start_new_session"] = True
            process = subprocess.Popen(
                [*prefix, str(binaries / "call_to_arms.exe"), "-no_preload"],
                **popen_kw,
            )
            session["process"] = process
            deadline = time.monotonic() + launch_timeout(a.suite, cfg, a.timeout)
            drive(ui, process, log, a.suite, deadline, auto_launch=auto_launch,
                  frames_dir=frames_dir, screenshot_interval=screenshot_interval, telemetry_path=telemetry)
            mid = (90 + harness.mission.SUITES.index(a.suite)) * 100 + 1
            wait_witness(telemetry, offsets[telemetry], mid, deadline)
        finally:
            launcher_log.close()
    except (ValueError, OSError, subprocess.SubprocessError) as exc:
        errors.append(str(exc))

    report = _archive_and_grade(evidence, telemetry, ai, offsets, log, binaries, a.suite, harness, errors, session)
    return session, report, errors


def _run_reused_trial(session, a, game, harness, binaries, ui, profile_ini, evidence):
    harness.install(game, a.suite, a.speed, {})
    ini = harness.safe_child(game, Path("binaries/x64/goh_ai.ini"))
    evidence = _make_evidence_dir(evidence)
    print(f"Run evidence {evidence} (reusing running game)", file=sys.stderr)

    # Parse requested trial INI and bump restart_seq after checking DLL hot-reload.
    cfg = configparser.ConfigParser(strict=False, interpolation=None, inline_comment_prefixes=(";", "#"))
    cfg.read_string(profile_ini.decode("utf-8-sig"))
    deadline = time.monotonic() + launch_timeout(a.suite, cfg, a.timeout)
    telemetry = binaries / cfg.get("telemetry", "filename", fallback=session.get("telemetry_filename", "goh_telemetry.jsonl"))
    ai = binaries / cfg.get("logging", "filename", fallback=session.get("ai_filename", "goh_ai.log"))
    if telemetry.parent != binaries or ai.parent != binaries:
        raise ValueError("expected log filenames beside the DLL")
    game_log = Path(session["game_log"])
    errors = []
    offsets = {p: p.stat().st_size if p.exists() else 0 for p in (telemetry, ai)}
    log = GameLog(game_log)
    try:
        wait_hot_reload(binaries, session, deadline)
        offsets = {p: p.stat().st_size if p.exists() else 0 for p in (telemetry, ai)}
        log = GameLog(game_log)
        next_seq = int(session.get("restart_seq", 0)) + 1
        session["restart_seq"] = next_seq
        # Apply automation keys onto the trial's exact profile_ini (not prior ini contents).
        with tempfile.NamedTemporaryFile(dir=evidence, delete=False) as tmp:
            tmp_path = Path(tmp.name)
            try:
                tmp.write(profile_ini)
                tmp.close()
                effective_ini = harness.merged_ini(
                    tmp_path,
                    {
                        RESTART_SEQ: str(next_seq),
                        AUTO_DISMISS_LOADING: "true",
                        SUPPRESS_CURSOR_GRAB: "true",
                        HOT_RELOAD: "true",
                    },
                )
            finally:
                tmp_path.unlink(missing_ok=True)
        harness.atomic_write(ini, effective_ini)
        benchmark = a.suite == "e2e"
        screenshot_interval = float(getattr(a, "screenshot_interval", 2.5 if benchmark else 0.0) or 0.0)
        frames_dir = (evidence / "frames") if (benchmark and screenshot_interval > 0) else None
        drive_restart(ui, session.get("process"), log, a.suite, deadline,
                      frames_dir=frames_dir, screenshot_interval=screenshot_interval, telemetry_path=telemetry)
        mid = (90 + harness.mission.SUITES.index(a.suite)) * 100 + 1
        wait_witness(telemetry, offsets[telemetry], mid, deadline)
    except (ValueError, OSError, subprocess.SubprocessError) as exc:
        errors.append(str(exc))
    session["suite"] = a.suite
    return _archive_and_grade(evidence, telemetry, ai, offsets, log, binaries, a.suite, harness, errors, session)


def reload_persistent_session(a, game, harness, *, force_dll=False, wait=True):
    harness.verify_game(game)
    binaries = game / "binaries/x64"
    prefix, env = wine_prefix(a)
    ui = _make_ui_env(prefix, build_helper(), binaries, env)
    if ui("present") != 0:
        raise ValueError("no running call_to_arms.exe found to reload")
    session = load_session()
    ini = harness.safe_child(game, Path("binaries/x64/goh_ai.ini"))
    deadline = time.monotonic() + (a.timeout or 150)
    reloaded = False
    game_log = None
    if session and Path(session["game_dir"]) == game:
        game_log = Path(session["game_log"])
        reloaded = wait_hot_reload(binaries, session, deadline, force=force_dll)
        log = GameLog(game_log)
        next_seq = int(session.get("restart_seq", 0)) + 1
        session["restart_seq"] = next_seq
        patched = harness.merged_ini(
            ini,
            {
                RESTART_SEQ: str(next_seq),
                AUTO_DISMISS_LOADING: "true",
                SUPPRESS_CURSOR_GRAB: "true",
                HOT_RELOAD: "true",
            },
        )
        harness.atomic_write(ini, patched)
        save_session(session, harness.atomic_write)
    else:
        cfg = configparser.ConfigParser(strict=False, interpolation=None, inline_comment_prefixes=(";", "#"))
        if ini.exists():
            cfg.read(ini, encoding="utf-8-sig")
        next_seq = cfg.getint("testing", "restart_seq", fallback=0) + 1
        if force_dll:
            temp = {"dll_sha256": "", "ai_filename": cfg.get("logging", "filename", fallback="goh_ai.log")}
            reloaded = wait_hot_reload(binaries, temp, deadline, force=True)
        try:
            options = profile_options(getattr(a, "profile", None))
            game_log = options.parents[2] / "log/game.log"
        except ValueError:
            game_log = None
        log = GameLog(game_log) if game_log else None
        patched = harness.merged_ini(
            ini,
            {RESTART_SEQ: str(next_seq), AUTO_DISMISS_LOADING: "true", SUPPRESS_CURSOR_GRAB: "true"},
        )
        harness.atomic_write(ini, patched)
    if wait and log is not None:
        start_marker = f"Start mission {AUTO_MISSION_PATH}"
        _wait_log(log, start_marker, deadline, process=None, ui=ui, seconds=30)
        _wait_log(log, "GUI: Activate page page_scene_game", deadline, process=None, ui=ui, seconds=120)
    return {"reloaded_dll": reloaded, "restart_seq": next_seq}


def stop_persistent_session(a, game, harness):
    binaries = game / "binaries/x64"
    prefix, env = wine_prefix(a)
    ui = _make_ui_env(prefix, build_helper(), binaries, env)
    session = load_session()
    if session and Path(session["game_dir"]) == game:
        stopped, errors = close_persistent_session(session, harness, ui=ui)
        if errors:
            raise ValueError("; ".join(errors))
        return stopped
    if ui("present") != 0:
        clear_session()
        return True
    options = profile_options(getattr(a, "profile", None))
    game_log = getattr(a, "game_log", None) or options.parents[2] / "log/game.log"
    stopped = stop_game(
        ui, None, GameLog(game_log), ini=binaries / "goh_ai.ini", write=harness.atomic_write, merge_ini=harness.merged_ini
    )
    clear_session()
    if not stopped:
        raise ValueError("game is still open after the exit request")
    return True


def run(a, game, settings, harness, *, evidence=None, profile_ini=None):
    harness.verify_game(game)
    options = profile_options(a.profile)
    binaries = game / "binaries/x64"
    if not all((binaries / name).is_file() for name in ("version.dll", "goh_ai_mod.dll")):
        raise ValueError("install the built version.dll and goh_ai_mod.dll beside call_to_arms.exe")
    if (a.timeout is not None and a.timeout <= 0) or settings.get(("telemetry", "enabled"), "true").lower() not in (
        "true",
        "on",
        "yes",
        "1",
    ):
        raise ValueError("run requires a positive timeout and enabled telemetry")
    prefix, env = wine_prefix(a)
    helper = build_helper()
    ui = _make_ui_env(prefix, helper, binaries, env)
    ini = harness.safe_child(game, Path("binaries/x64/goh_ai.ini"))
    fresh_process = bool(getattr(a, "fresh_process", False))
    real_launch = _record_run is _REAL_RECORD_RUN
    keep_open = bool(getattr(a, "keep_open", False)) and not fresh_process and real_launch
    reuse = bool(getattr(a, "reuse", False)) and not fresh_process and real_launch
    benchmark = a.suite == "e2e"

    if fresh_process and real_launch:
        stale = load_session()
        if stale and ui("present") == 0:
            close_persistent_session(stale, harness, ui=ui)
        elif stale:
            _restore_session_files(stale, harness)
            clear_session()

    if reuse:
        session = load_session()
        if session and ui("present") == 0:
            compatible = Path(session["game_dir"]) == game and bool(session.get("heroic")) == benchmark
            if compatible:
                if profile_ini is None:
                    base_ini = ini
                    if session.get("had_ini"):
                        with tempfile.NamedTemporaryFile(dir=binaries, delete=False) as tmp:
                            tmp_path = Path(tmp.name)
                            try:
                                tmp.write(base64.b64decode(session["ini_backup_b64"]))
                                tmp.close()
                                profile_ini = harness.merged_ini(
                                    tmp_path,
                                    {**testing_automation_settings(hot_reload=True), **harness.run_settings(a.suite, settings)},
                                )
                            finally:
                                tmp_path.unlink(missing_ok=True)
                    else:
                        profile_ini = harness.merged_ini(
                            binaries / ".nonexistent_goh_ai.ini",
                            {**testing_automation_settings(hot_reload=True), **harness.run_settings(a.suite, settings)},
                        )
                report = _run_reused_trial(session, a, game, harness, binaries, ui, profile_ini, evidence)
                if keep_open and ui("present") == 0:
                    save_session(session, harness.atomic_write)
                else:
                    stopped, close_errs = close_persistent_session(session, harness, ui=ui)
                    for err in close_errs:
                        report["checks"].append({"phase": "run", "check": "launcher", "passed": False, "evidence": err})
                        report["passed"] = False
                return report
            # Incompatible running session (e.g. switching e2e <-> full): close it first before relaunching.
            close_persistent_session(session, harness, ui=ui)
        elif session:
            _restore_session_files(session, harness)
            clear_session()

    status = ui("present")
    if status != 3:
        raise ValueError(
            "close the existing call_to_arms.exe before running" if status == 0 else "UI helper failed"
        )
    harness.install(game, a.suite, a.speed, {})
    if profile_ini is None:
        auto = testing_automation_settings(hot_reload=keep_open, restart_seq=0 if keep_open else None)
        profile_ini = harness.merged_ini(ini, {**auto, **harness.run_settings(a.suite, settings)})
    if keep_open:
        session, report, errors = _start_persistent_session(
            a, game, harness, binaries, options, ui, prefix, env, profile_ini, evidence
        )
        alive = session.get("process") is not None and session["process"].poll() is None
        if alive and not errors:
            save_session(session, harness.atomic_write)
            print("Persistent game session kept open (next run/sweep reuses it; run 'stop' to close and restore)",
                  file=sys.stderr)
        else:
            _, close_errs = close_persistent_session(session, harness, ui=ui)
            for err in close_errs:
                report["checks"].append({"phase": "run", "check": "launcher", "passed": False, "evidence": err})
                report["passed"] = False
        return report
    with temporary_ini(ini, profile_ini, harness.atomic_write):
        return _record_run(a, harness, binaries, options, ui, prefix, env, evidence)


def _record_run(a, harness, binaries, options, ui, prefix, env, evidence):
    game_log = a.game_log or options.parents[2] / "log/game.log"
    cfg = configparser.ConfigParser(strict=False, interpolation=None, inline_comment_prefixes=(";", "#"))
    cfg.read(binaries / "goh_ai.ini", encoding="utf-8-sig")
    telemetry = binaries / cfg.get("telemetry", "filename", fallback="goh_telemetry.jsonl")
    ai = binaries / cfg.get("logging", "filename", fallback="goh_ai.log")
    if telemetry.parent != binaries or ai.parent != binaries:
        raise ValueError("expected log filenames beside the DLL")
    offsets = {p: p.stat().st_size if p.exists() else 0 for p in (telemetry, ai)}
    log = GameLog(game_log)
    evidence = _make_evidence_dir(evidence)
    print(f"Run evidence {evidence}", file=sys.stderr)
    errors = []
    process = None
    stopped = True
    benchmark = a.suite == "e2e"
    auto_launch = bool(cfg.get("testing", "auto_mission", fallback="").strip())
    # A functional run may have left the overlay selected. Preserve its
    # pending backup as well as the profile actually in use at trial entry.
    prior_selection = options.read_bytes() if benchmark and options_backup(options).exists() else None
    screenshot_interval = float(getattr(a, "screenshot_interval", 2.5 if benchmark else 0.0) or 0.0)
    frames_dir = (evidence / "frames") if (benchmark and screenshot_interval > 0) else None
    ini_path = binaries / "goh_ai.ini"
    ini_snapshot = ini_path.read_bytes() if ini_path.exists() else None
    try:
        profile_mod(options, harness.atomic_write, heroic=benchmark)
        with (
            ensure_silhouettes(options.with_name("settings.set"), harness.atomic_write)
            if benchmark
            else nullcontext()
        ), (evidence / "launcher.log").open("w") as out:
            process = subprocess.Popen(
                [*prefix, str(binaries / "call_to_arms.exe"), "-no_preload"],
                cwd=binaries,
                env=env,
                stdout=out,
                stderr=subprocess.STDOUT,
            )
            deadline = time.monotonic() + launch_timeout(a.suite, cfg, a.timeout)
            drive(ui, process, log, a.suite, deadline, auto_launch=auto_launch,
                  frames_dir=frames_dir, screenshot_interval=screenshot_interval, telemetry_path=telemetry)
            mid = (90 + harness.mission.SUITES.index(a.suite)) * 100 + 1
            wait_witness(telemetry, offsets[telemetry], mid, deadline)
    except (ValueError, OSError, subprocess.SubprocessError) as exc:
        errors.append(str(exc))
    finally:
        try:
            if process:
                stopped = False
                try:
                    stopped = stop_game(
                        ui, process, log, ini=ini_path, write=harness.atomic_write, merge_ini=harness.merged_ini
                    )
                except (OSError, subprocess.SubprocessError) as exc:
                    errors.append(str(exc))
                if not stopped:
                    errors.append("game is still open after the exit request")
        finally:
            if benchmark or (stopped and a.restore_options):
                if prior_selection is not None:
                    harness.atomic_write(options, prior_selection)
                else:
                    profile_mod(options, harness.atomic_write, restore=True, heroic=benchmark)

    return _archive_and_grade(
        evidence, telemetry, ai, offsets, log, binaries, a.suite, harness, errors, ini_bytes=ini_snapshot
    )


_REAL_RECORD_RUN = _record_run


def archive_grade(report, grader):
    out = Path(report["evidence_dir"])
    (out / "grade.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    (out / "grade.txt").write_text(grader.table(report) + "\n", encoding="utf-8")


def pooled_side(scenarios, side):
    metrics = {}
    for group, label in (("infantry", "infantry"), ("vehicles", "vehicle"), ("support", "support")):
        units = [s["sides"][side][group] for s in scenarios]
        deployed = sum(u["total"] for u in units)
        alive = sum(u["alive"] for u in units)
        metrics[label + "_survival_pct"] = 100 * alive / deployed if deployed else None
        hp = math.fsum(u["remaining_hp_pct"] * u["total"] for u in units if u["total"])
        metrics[label + "_hp_pct"] = hp / deployed if deployed else None
    values = [s["sides"][side] for s in scenarios]
    metrics["force_value_pct"] = mean(s["force_value"]["remaining_pct"] for s in values) if values else None
    metrics["exchange_ratio"] = mean(s["exchange_ratio"] for s in values) if values else None
    metrics["flag_units"] = mean(s["flag_units"] for s in values) if values else None
    return metrics


def orient_tactical(tactical, side="A"):
    """View a trial's tactical summary from the evaluated side's perspective."""
    if side == "A":
        return tactical
    scenarios = {}
    for phase, sc in tactical["scenarios"].items():
        score = None if sc["score"] is None else round(100.0 - sc["score"], 2)
        flipped = {
            **sc,
            "score": score,
            "sides": {"A": sc["sides"]["B"], "B": sc["sides"]["A"]},
        }
        if "infantry_stage" in sc:
            st1 = sc["infantry_stage"]
            s1 = None if st1["score"] is None else round(100.0 - st1["score"], 2)
            flipped["infantry_stage"] = {
                **st1,
                "score": s1,
                "sides": {"A": st1["sides"]["B"], "B": st1["sides"]["A"]},
            }
        scenarios[phase] = flipped
    overall = tactical["overall_score"]
    return {
        **tactical,
        "overall_score": None if overall is None else round(100.0 - overall, 2),
        "scenarios": scenarios,
    }


def compare_trials(trials, planned_runs=None):
    """Pool counts across valid trials. Only complete profiles can win a comparison."""
    groups = {}
    for trial in trials:
        groups.setdefault(trial["profile"], []).append(trial)
    profiles = {}
    for name, items in groups.items():
        complete = [t for t in items if t["report"]["passed"] and t["report"]["tactical"]["overall_score"] is not None]
        # A symmetric control needs one physical launch. Both perspectives
        # enter its paired comparison, while the archive retains the raw west score.
        valid = [
            orient_tactical(t["report"]["tactical"], side)
            for t in complete for side in (("A", "B") if t.get("side") == "both" else (t.get("side", "B"),))
        ]
        scenarios = [s for result in valid for s in result["scenarios"].values()]

        sides = {side: pooled_side(scenarios, side) for side in ("A", "B")}
        st1 = [s["infantry_stage"] for s in scenarios if "infantry_stage" in s and s["infantry_stage"]["score"] is not None]
        st1_sides = {side: pooled_side(st1, side) for side in ("A", "B")} if st1 else None
        profiles[name] = {
            "runs": len(items),
            "valid_runs": len(complete),
            "score": mean(t["overall_score"] for t in valid) if valid else None,
            "infantry_stage_score": mean(s["score"] for s in st1) if st1 else None,
            "infantry_stage_advantage_pp": {
                key: a - st1_sides["B"][key] if a is not None and st1_sides["B"][key] is not None else None
                for key, a in st1_sides["A"].items() if key.endswith("_pct")
            } if st1_sides else {},
            "infantry_stage_flag_delta": (
                st1_sides["A"]["flag_units"] - st1_sides["B"]["flag_units"] if st1_sides else None
            ),
            "flag_delta": (
                sides["A"]["flag_units"] - sides["B"]["flag_units"]
                if sides["A"]["flag_units"] is not None and sides["B"]["flag_units"] is not None
                else None
            ),
            "scenarios": {
                phase: mean(t["scenarios"][phase]["score"] for t in valid)
                for phase in (valid[0]["scenarios"] if valid else ())
            },
            "sides": sides,
            "advantage_pp": {
                key: a - sides["B"][key] if a is not None and sides["B"][key] is not None else None
                for key, a in sides["A"].items() if key.endswith("_pct")
            },
        }

    def expected_for(name, p):
        if isinstance(planned_runs, dict):
            return planned_runs.get(name, p["runs"])
        return planned_runs or p["runs"]

    eligible = {
        name: p for name, p in profiles.items()
        if p["valid_runs"] and p["valid_runs"] == expected_for(name, p)
    }
    baseline = eligible.get("vanilla", {}).get("score")
    for name, p in profiles.items():
        p["delta_vs_vanilla"] = p["score"] - baseline if name in eligible and baseline is not None else None

    def winners(scores):
        top = max(scores.values(), default=None)
        return [name for name, score in scores.items() if math.isclose(score, top, abs_tol=1e-9)]

    phases = dict.fromkeys(phase for p in profiles.values() for phase in p["scenarios"])
    seen_sides = {t.get("side", "B") for t in trials}
    evaluated_side = (
        "both" if "both" in seen_sides
        else next(iter(seen_sides)) if len(seen_sides) == 1
        else "mixed" if seen_sides
        else "B"
    )
    return {
        "evaluated_side": evaluated_side,
        "profiles": profiles,
        "best": {
            "overall": winners({name: p["score"] for name, p in eligible.items()}),
            "infantry_stage": winners({
                name: p["infantry_stage_score"] for name, p in eligible.items()
                if p["infantry_stage_score"] is not None
            }),
            "scenarios": {
                phase: winners({name: p["scenarios"][phase] for name, p in eligible.items()})
                for phase in phases
            },
        },
    }


def comparison_table(summary):
    profiles = summary["profiles"]
    best = summary["best"]
    rows = [["Metric", *profiles, "Best"]]
    rows.append(["Valid trials", *(f"{p['valid_runs']}/{p['runs']}" for p in profiles.values()), ""])

    def metric(label, values, winners=(), fmt=".2f"):
        rows.append([label, *(format(v, fmt) if v is not None else "—" for v in values), ", ".join(winners)])

    if "ai_player_masks" in summary:
        rows.append(["AI player mask", *(str(summary["ai_player_masks"][name]) for name in profiles), ""])
    evaluated = "both" if summary.get("swap_sides") else summary["evaluated_side"]
    header, delta_label = {
        "both": (
            "1v1 Oryol Flag Battle (side-swapped crossover: Modded plays SW P2 + NE P3 vs Vanilla)",
            " Modded−Vanilla (pp)",
        ),
        "B": (
            "1v1 Oryol Flag Battle   Evaluated = Side B / Player 3 (NE) vs Side A / Player 2 (SW)",
            " NE−SW (pp)",
        ),
        "A": (
            "1v1 Oryol Flag Battle   Evaluated = Side A / Player 2 (SW) vs Side B / Player 3 (NE)",
            " SW−NE (pp)",
        ),
        "mixed": (
            "1v1 Oryol Flag Battle   Each profile scored for its modded side (SW P2 mask 4 / NE P3 mask 8)",
            " Modded−Vanilla (pp)",
        ),
    }[evaluated]
    if any(p.get("infantry_stage_score") is not None for p in profiles.values()):
        metric("Stage 1 Infantry score (0-90s, 50 = parity)",
               (p.get("infantry_stage_score") for p in profiles.values()),
               best.get("infantry_stage", ()))
        metric("Stage 1 Infantry survival" + delta_label,
               (p["infantry_stage_advantage_pp"].get("infantry_survival_pct") for p in profiles.values()), fmt="+.1f")
        metric("Stage 1 Flag units delta",
               (p.get("infantry_stage_flag_delta") for p in profiles.values()), fmt="+.1f")
    metric("Final E2E score (0-240s, 50 = parity)", (p["score"] for p in profiles.values()), best["overall"])
    if "vanilla" in profiles:
        metric("Delta vs vanilla", (p["delta_vs_vanilla"] for p in profiles.values()), fmt="+.2f")
    for label, key in (
        ("Remaining force value", "force_value_pct"),
        ("Infantry survival", "infantry_survival_pct"),
        ("Infantry HP", "infantry_hp_pct"),
        ("Vehicle survival", "vehicle_survival_pct"),
        ("Vehicle hull HP", "vehicle_hp_pct"),
        ("Support survival", "support_survival_pct"),
        ("Support HP", "support_hp_pct"),
    ):
        metric(label + delta_label, (p["advantage_pp"][key] for p in profiles.values()), fmt="+.1f")
    metric("Final Flag units delta", (p.get("flag_delta") for p in profiles.values()), fmt="+.1f")
    metric("Mean force-value exchange ratio", (p["sides"]["A"]["exchange_ratio"] for p in profiles.values()))
    widths = [max(map(len, col)) for col in zip(*rows)]
    lines = [header]
    lines.extend("  ".join(v.ljust(w) for v, w in zip(row, widths)).rstrip() for row in rows)
    if "evidence_dir" in summary:
        lines.append(f"Evidence  {summary['evidence_dir']}")
    return "\n".join(lines)


def sweep(a, game, profiles, harness):
    if a.runs < 1:
        raise ValueError("--runs must be positive")
    swap_sides = bool(getattr(a, "swap_sides", False))
    root = ROOT / "local-work/level1/sweeps"
    root.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="sweep-", dir=root))
    trials = []

    single_sides = {
        "A" if (int(s[PLAYER_MASK]) & 12) == 4 else "B"
        for s in profiles.values()
        if (int(s[PLAYER_MASK]) & 12) in (4, 8)
    }
    default_side = "A" if single_sides == {"A"} else "B"

    def profile_sides(settings):
        mask = int(settings[PLAYER_MASK])
        if swap_sides:
            if (mask & 12) in (4, 8):
                other_players = mask & ~12
                return (("A", str(other_players | 4)), ("B", str(other_players | 8)))
            return (("both", str(mask)),)
        if (mask & 12) == 4:
            return (("A", str(mask)),)
        if (mask & 12) == 8:
            return (("B", str(mask)),)
        return ((default_side, str(mask)),)

    assignments = {name: profile_sides(s) for name, s in profiles.items()}
    assigned_sides = {side for sides in assignments.values() for side, _ in sides}
    evaluated_side = (
        "both" if swap_sides
        else next(iter(assigned_sides)) if len(assigned_sides) == 1
        else "mixed"
    )
    planned = {name: a.runs * len(sides) for name, sides in assignments.items()}
    mask_labels = {
        name: ",".join(mask for _, mask in sides) if len(sides) > 1 else int(sides[0][1])
        for name, sides in assignments.items()
    }

    def trial_summary(t):
        report = t["report"]
        score = report.get("tactical", {}).get("overall_score")
        compared = score
        if score is not None and t["side"] != "A":
            compared = 50.0 if t["side"] == "both" else round(100 - score, 2)
        return {
            "profile": t["profile"], "run": t["run"], "side": t["side"],
            "passed": report["passed"], "west_score": score, "score": compared,
            "directory": Path(report["evidence_dir"]).name,
        }

    def save():
        summary = compare_trials(trials, planned)
        summary.update(
            suite=a.suite, speed=a.speed, runs_per_profile=a.runs, requested_profiles=list(profiles),
            difficulty="heroic", swap_sides=swap_sides, evaluated_side=evaluated_side, ai_player_masks=mask_labels,
            sim_speed=a.sim_speed, profile_sim_speeds={n: float(s[SIM_SPEED]) for n, s in profiles.items()},
            evidence_dir=str(out),
            passed=len(trials) == sum(planned.values()) and all(t["report"]["passed"] for t in trials),
            trials=[trial_summary(t) for t in trials],
        )
        harness.atomic_write(out / "summary.json", (json.dumps(summary, indent=2, allow_nan=False) + "\n").encode())
        harness.atomic_write(out / "comparison.txt", (comparison_table(summary) + "\n").encode())
        return summary

    plan = [
        (repetition, name, side, mask, settings)
        for repetition in range(1, a.runs + 1)
        for name, settings in profiles.items()
        for side, mask in assignments[name]
    ]
    keep_open = bool(getattr(a, "keep_open", False))
    reuse = bool(getattr(a, "reuse", False))
    fresh_process = bool(getattr(a, "fresh_process", False))
    use_persistent = (
        not fresh_process
        and _record_run is _REAL_RECORD_RUN
        and (len(plan) > 1 or keep_open or reuse)
    )

    if not use_persistent:
        if fresh_process and _record_run is _REAL_RECORD_RUN:
            stale = load_session()
            if stale:
                prefix, env = wine_prefix(a)
                ui = _make_ui_env(prefix, build_helper(), game / "binaries/x64", env)
                if ui("present") == 0:
                    close_persistent_session(stale, harness, ui=ui)
                else:
                    _restore_session_files(stale, harness)
                    clear_session()
        for repetition, name, side, mask, settings in plan:
            suffix = f"-side{side}" if len(assignments[name]) > 1 else ""
            evidence = out / f"trial-{repetition:02d}-{name}{suffix}"
            evidence.mkdir()
            run_settings = {**testing_automation_settings(), **settings, PLAYER_MASK: mask}
            ini = render_profile(run_settings)
            (evidence / "goh_ai.ini").write_bytes(ini)
            try:
                report = run(a, game, {}, harness, evidence=evidence, profile_ini=ini)
            except (ValueError, OSError, subprocess.SubprocessError, configparser.Error, zipfile.BadZipFile,
                    KeyboardInterrupt) as exc:
                # Leave a record even when setup failed before the process started.
                error = str(exc) or type(exc).__name__
                (evidence / "error.txt").write_text(error + "\n", encoding="utf-8")
                trials.append({"run": repetition, "profile": name, "side": side, "report": {
                    "passed": False, "evidence_dir": str(evidence),
                }})
                save()
                print(f"Sweep stopped. Evidence {out}", file=sys.stderr)
                raise
            archive_grade(report, harness.live_grade)
            trials.append({"run": repetition, "profile": name, "side": side, "report": report})
            summary = save()
        return summary

    harness.verify_game(game)
    options = profile_options(a.profile)
    binaries = game / "binaries/x64"
    if not all((binaries / f).is_file() for f in ("version.dll", "goh_ai_mod.dll")):
        raise ValueError("install the built version.dll and goh_ai_mod.dll beside call_to_arms.exe")
    if a.timeout is not None and a.timeout <= 0:
        raise ValueError("sweep requires a positive timeout")
    prefix, env = wine_prefix(a)
    ui = _make_ui_env(prefix, build_helper(), binaries, env)
    active_session = None
    completed_cleanly = False
    if reuse:
        saved = load_session()
        if saved and ui("present") == 0:
            if Path(saved["game_dir"]) == game and bool(saved.get("heroic")):
                active_session = saved
            else:
                close_persistent_session(saved, harness, ui=ui)
        elif saved:
            _restore_session_files(saved, harness)
            clear_session()
    if active_session is None:
        status = ui("present")
        if status != 3:
            raise ValueError(
                "close the existing call_to_arms.exe before running" if status == 0 else "UI helper failed"
            )
    try:
        for repetition, name, side, mask, settings in plan:
            suffix = f"-side{side}" if len(assignments[name]) > 1 else ""
            evidence = out / f"trial-{repetition:02d}-{name}{suffix}"
            evidence.mkdir()
            try:
                if active_session is None:
                    harness.install(game, a.suite, a.speed, {})
                    ini = render_profile(
                        {**testing_automation_settings(hot_reload=True, restart_seq=0), **settings, PLAYER_MASK: mask}
                    )
                    (evidence / "goh_ai.ini").write_bytes(ini)
                    active_session, report, errs = _start_persistent_session(
                        a, game, harness, binaries, options, ui, prefix, env, ini, evidence
                    )
                    proc = active_session.get("process")
                    if errs or proc is None or proc.poll() is not None:
                        close_persistent_session(active_session, harness, ui=ui)
                        active_session = None
                else:
                    ini = render_profile({**testing_automation_settings(hot_reload=True), **settings, PLAYER_MASK: mask})
                    (evidence / "goh_ai.ini").write_bytes(ini)
                    report = _run_reused_trial(active_session, a, game, harness, binaries, ui, ini, evidence)
                    proc = active_session.get("process")
                    if (proc is not None and proc.poll() is not None) or (proc is None and ui("present") != 0):
                        close_persistent_session(active_session, harness, ui=ui)
                        active_session = None
            except (ValueError, OSError, subprocess.SubprocessError, configparser.Error, zipfile.BadZipFile,
                    KeyboardInterrupt) as exc:
                error = str(exc) or type(exc).__name__
                (evidence / "error.txt").write_text(error + "\n", encoding="utf-8")
                trials.append({"run": repetition, "profile": name, "side": side, "report": {
                    "passed": False, "evidence_dir": str(evidence),
                }})
                save()
                print(f"Sweep stopped. Evidence {out}", file=sys.stderr)
                raise
            archive_grade(report, harness.live_grade)
            trials.append({"run": repetition, "profile": name, "side": side, "report": report})
            summary = save()
        completed_cleanly = True
    finally:
        if active_session is not None:
            proc = active_session.get("process")
            alive = (proc.poll() is None) if proc is not None else (ui("present") == 0)
            if keep_open and completed_cleanly and alive:
                save_session(active_session, harness.atomic_write)
                print("Persistent game session kept open (next run/sweep reuses it; run 'stop' to close and restore)",
                      file=sys.stderr)
            else:
                close_persistent_session(active_session, harness, ui=ui)
    return summary
