#!/usr/bin/env python3
"""Local vehicle-control mission overlay and Level 1 telemetry grader for Steam build 26734796."""

import argparse
import configparser
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
import time
import zipfile
from pathlib import Path

if __package__:
    from . import live_grade, live_launch
    from . import live_mission as mission
else:
    import live_grade
    import live_launch
    import live_mission as mission

GAME = "Call to Arms - Gates of Hell"
MOD = "ai_test_harness"
INFO = (
    '{mod\n {name "ai_test_harness"}\n {desc "Level 1 verification - Vehicle Control bootcamp"}\n'
    ' {minGameVersion "1.069"}\n {maxGameVersion "1.069"}\n}\n'
)


def game_dir(arg=None):
    path = arg or os.environ.get("GOH_GAME_DIR")
    if path:
        return Path(path).expanduser().resolve(strict=True)
    home = Path.home()
    paths = [
        home / "Library/Application Support/CrossOver/Bottles/Steam/drive_c/Program Files (x86)/Steam",
        home / "Library/Application Support/Steam",
        home / ".local/share/Steam",
        home / ".steam/steam",
        Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Steam",
    ]
    for steam in paths:
        game = steam / "steamapps/common" / GAME
        if game.is_dir():
            return game.resolve()
    raise ValueError("provide --game-dir or GOH_GAME_DIR")


def verify_game(game):
    exe = game / "binaries/x64/call_to_arms.exe"
    h = hashlib.sha256()
    with exe.open("rb") as src:
        for block in iter(lambda: src.read(1 << 20), b""):
            h.update(block)
    if h.hexdigest() != mission.SHA:
        raise ValueError("call_to_arms.exe SHA-256 differs from build 26734796")


def overrides(args):
    result = {}
    for spec in args:
        k, sep, v = spec.partition("=")
        k, v = k.strip().lower(), v.strip()
        if (
            not sep
            or not re.fullmatch(r"[a-z][a-z_0-9]*(\.[a-z][a-z_0-9]*)?", k)
            or not v
            or any(c in v for c in "\r\n\x00")
        ):
            raise ValueError(f"invalid --set {spec!r}")
        if "." in k:
            section, key = k.split(".")
        elif k == "ai_player_mask":
            section, key = live_launch.PLAYER_MASK
        elif k == "sim_speed":
            section, key = live_launch.SIM_SPEED
        elif k in (
            "skip_intro",
            "auto_dismiss_loading",
            "suppress_cursor_grab",
            "auto_mission",
            "hot_reload",
            "restart_seq",
            "quit_seq",
        ):
            section, key = "testing", k
        elif k in ("enabled", "snapshot_interval_ms", "flush_ms", "filename"):
            section, key = "telemetry", k
        elif k in live_launch.MODULE_KEYS:
            section, key = "modules", k
        elif k == "level":
            section, key = "logging", k
        else:
            section, key = "tuning", k
        if (section, key) == live_launch.PLAYER_MASK:
            v = live_launch.player_mask(v)
        elif (section, key) == live_launch.SIM_SPEED:
            v = live_launch.sim_speed_value(v)
        elif section == "testing" and key in (
            "skip_intro",
            "auto_dismiss_loading",
            "suppress_cursor_grab",
            "hot_reload",
            "auto_mission",
            "restart_seq",
            "quit_seq",
        ):
            if key in ("restart_seq", "quit_seq") and not v.isdecimal():
                raise ValueError(f"{key} requires an unsigned integer")
        elif section not in ("telemetry", "modules", "tuning", "logging", "smoke"):
            raise ValueError("unknown section " + section)
        result[section, key] = v
    return result


def safe_child(game, rel):
    path = game / rel
    if path.resolve() != path:
        raise ValueError(f"symlink in destination {path}")
    return path


def atomic_write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as f:
        temp = Path(f.name)
        try:
            f.write(data)
            f.close()
            os.replace(temp, path)
        finally:
            temp.unlink(missing_ok=True)


def arena_paths(game, base):
    rel_dir = base.relative_to(game) / mission.MI.parent
    return {name: safe_child(game, rel_dir / name) for name in mission.E2E_MAP_FILES}


def visual_paths(game, base):
    rel = base.relative_to(game)
    files = (mission.E2E_CAMERA_REG, mission.E2E_DIFFICULTY, *mission.E2E_LOD_FILES)
    return {p: safe_child(game, rel / p) for p in files}


def overlay(game):
    base = safe_child(game, Path("mods") / MOD)
    info = safe_child(game, base.relative_to(game) / "mod.info")
    file = safe_child(game, base.relative_to(game) / mission.MI)
    visuals = visual_paths(game, base)
    arena = arena_paths(game, base)
    if base.exists():
        visual_parents = {parent for dst in visuals.values() for parent in dst.parents}
        allowed = {info, file, *visuals.values(), *arena.values(), *file.parents, *visual_parents}
        if (
            not info.is_file()
            or info.read_bytes() != INFO.encode()
            or any(p not in allowed or p.is_symlink() for p in base.rglob("*"))
        ):
            raise ValueError(f"unrecognized contents in {base}")
    return base, info, file


def merged_ini(path, settings):
    # Preserve other lines, including comments. Put the override in the last matching section.
    raw = path.read_bytes() if path.exists() else b""
    text = raw.decode("utf-8-sig")
    newline = "\r\n" if "\r\n" in text else "\n"
    lines = text.splitlines()
    for (section, key), value in settings.items():
        sections = [
            (i, m[1].strip().lower())
            for i, line in enumerate(lines)
            if (m := re.match(r"^\s*\[([^\]]+)\]\s*(?:[;#].*)?$", line))
        ]
        indices = [i for i, s in sections if s == section]
        if not indices:
            lines.extend(["", f"[{section}]", f"{key}={value}"])
            continue
        start = indices[-1]
        end = next((i for i, _ in sections if i > start), len(lines))
        keys = [i for i in range(start + 1, end) if lines[i].partition("=")[0].strip().lower() == key]
        if keys:
            lines[keys[-1]] = f"{key}={value}"
        else:
            lines.insert(end, f"{key}={value}")
    return (newline.join(lines).lstrip("\r\n") + newline).encode("utf-8")


def run_settings(suite, overrides):
    defaults = (
        {**live_launch.E2E_TELEMETRY, live_launch.PLAYER_MASK: live_launch.DEFAULT_MODDED_MASK}
        if suite == "e2e" else {("telemetry", "enabled"): "true"}
    )
    return {**defaults, **overrides}


def install(game, suite, speed, settings, write_ini=False):
    verify_game(game)
    base, info, target = overlay(game)
    arena_targets = arena_paths(game, base)
    visual_targets = visual_paths(game, base)
    ini = safe_child(game, Path("binaries/x64/goh_ai.ini"))
    text = mission.generate(suite, speed)
    mission.check_stock(game, text)
    arena_data = mission.e2e_map_files(game) if suite == "e2e" else {}
    visual_data = (
        {
            mission.E2E_CAMERA_REG: mission.birdseye_camera_reg(game),
            mission.E2E_DIFFICULTY: mission.e2e_symmetric_difficulty(),
            **mission.birdseye_lod_files(game),
        }
        if suite == "e2e"
        else {}
    )
    patch = merged_ini(ini, run_settings(suite, settings)) if write_ini or settings else None
    if not base.exists():
        base.parent.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=base.parent, prefix=".level1-") as staging:
            root = Path(staging) / MOD
            atomic_write(root / "mod.info", INFO.encode())
            atomic_write(root / mission.MI, text.encode())
            for name, payload in arena_data.items():
                atomic_write(root / mission.MI.parent / name, payload)
            for rel_path, payload in visual_data.items():
                atomic_write(root / rel_path, payload)
            root.rename(base)
    else:
        atomic_write(target, text.encode())
        for name, dst in arena_targets.items():
            if name in arena_data:
                atomic_write(dst, arena_data[name])
            else:
                dst.unlink(missing_ok=True)
        for rel_path, dst in visual_targets.items():
            if rel_path in visual_data:
                atomic_write(dst, visual_data[rel_path])
            else:
                dst.unlink(missing_ok=True)
        if not visual_data:
            for dst in visual_targets.values():
                for folder in dst.parents:
                    if folder == base / "resource" or folder == base:
                        break
                    if folder.exists():
                        try:
                            folder.rmdir()
                        except OSError:
                            break
    if patch is not None:
        atomic_write(ini, patch)
    return target


def clean(game, options=None):
    base, info, target = overlay(game)
    arena_targets = arena_paths(game, base)
    visual_targets = visual_paths(game, base)
    benchmark = options and target.is_file() and "GOH_L1_DONE_e2e" in target.read_text(encoding="utf-8")
    if base.exists():
        target.unlink(missing_ok=True)
        for dst in (*visual_targets.values(), *arena_targets.values()):
            dst.unlink(missing_ok=True)
        for leaf in (*visual_targets.values(), target):
            for folder in leaf.parents:
                if folder == base:
                    break
                if folder.exists():
                    try:
                        folder.rmdir()
                    except OSError:
                        pass
        info.unlink()
        base.rmdir()
    if options:
        live_launch.profile_mod(options, atomic_write, restore=True, heroic=bool(benchmark))
    return base


def telemetry_lines(stream):
    """Finish a partially flushed final record when grading a running DLL."""
    for line in stream:
        if not line.endswith("\n"):
            try:
                json.loads(line)
            except json.JSONDecodeError:
                deadline = time.monotonic() + 1.5
                while not line.endswith("\n") and time.monotonic() < deadline:
                    time.sleep(0.05)
                    line += stream.readline()
            yield line
            return
        yield line


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    subs = ap.add_subparsers(dest="cmd", required=True)
    for name in ("generate", "install", "clean", "grade", "run", "sweep", "stop", "reload"):
        p = subs.add_parser(name)
        p.add_argument("--game-dir", help="Steam game root (or GOH_GAME_DIR)")
        if name not in ("clean", "sweep", "stop", "reload"):
            p.add_argument("--suite", choices=mission.SUITES, default="full")
            p.add_argument("--set", action="append", default=[], metavar="[SECTION.]KEY=VALUE")
        if name in ("install", "generate"):
            p.add_argument("--speed", choices=mission.SPEEDS, default="normal")
        if name in ("run", "sweep"):
            p.add_argument("--speed", choices=mission.SPEEDS, default="fast",
                           help="mission speed mode (default fast)")
        if name == "generate":
            p.add_argument("-o", "--output", type=Path, help="write a .mi file (default stdout)")
        if name == "install":
            p.add_argument(
                "--write-ini",
                action="store_true",
                help="enable telemetry beside the DLL; --set implies this",
            )
        if name in ("clean", "run", "stop"):
            p.add_argument("--profile", help="player profile directory or options.set")
        if name == "sweep":
            p.add_argument("--suite", choices=("e2e",), default="e2e")
            p.add_argument("--sim-speed", type=float, default=8.0,
                           help="DLL simulation multiplier (default 8; 0 for stock, or 0.25–20)")
            p.add_argument("--profiles", default="default",
                           help="comma-separated tuning profiles (default: default on East P3 mask 8; add vanilla sporadically after scenario edits)")
            p.add_argument("--profile", dest="custom", action="append", default=[],
                           metavar="NAME:KEY=VALUE,...", help="define and include a custom tuning profile")
            p.add_argument("--player-profile", dest="profile", help="player profile directory or options.set")
            p.add_argument("--runs", type=int, default=1, help="trials per profile")
            p.add_argument(
                "--swap-sides",
                action="store_true",
                help="run modded profiles on both West (P2, mask=4) and East (P3, mask=8) to cancel side bias",
            )
        if name in ("run", "sweep", "stop", "reload"):
            p.add_argument("--wine", help="Wine or CrossOver wine executable")
            p.add_argument("--bottle", default="Steam", help="CrossOver bottle (default Steam)")
        if name in ("run", "sweep", "reload"):
            p.add_argument("--timeout", type=int,
                           help="timeout per launch in seconds (default 720, longer for sub-1× E2E runs)")
        if name in ("run", "sweep"):
            p.add_argument(
                "--screenshot-interval",
                type=float,
                default=2.5,
                help="seconds between periodic E2E bird's-eye screenshots in <evidence>/frames/ (0 disables; default 2.5)",
            )
            p.add_argument(
                "--restore-options",
                action="store_true",
                help="restore prior mod selection on exit (E2E always restores the player profile)",
            )
            p.add_argument(
                "--keep-open",
                dest="keep_open",
                action="store_true",
                default=True,
                help="keep call_to_arms.exe open after completion for fast reuse (default; use --close or 'stop' to exit)",
            )
            p.add_argument(
                "--close",
                "--no-keep-open",
                dest="keep_open",
                action="store_false",
                help="close call_to_arms.exe and restore profile/INI immediately after completion",
            )
            p.add_argument(
                "--reuse",
                dest="reuse",
                action="store_true",
                default=True,
                help="reuse a running persistent call_to_arms.exe session and hot-reload rebuilt DLLs (default)",
            )
            p.add_argument(
                "--no-reuse",
                dest="reuse",
                action="store_false",
                help="do not reuse an existing persistent session",
            )
            p.add_argument(
                "--fresh-process",
                action="store_true",
                help="cold-boot a fresh call_to_arms.exe per trial and close after completion",
            )
        if name == "reload":
            p.add_argument(
                "--force-dll",
                action="store_true",
                help="force a DLL hot-reload even if goh_ai_mod.dll SHA-256 has not changed",
            )
            p.add_argument(
                "--no-wait",
                action="store_true",
                help="return immediately after signaling restart instead of waiting for page_scene_game",
            )
        if name == "grade":
            p.add_argument("--telemetry", type=Path, help="JSONL from the DLL")
            p.add_argument("--ai-log", "--log", type=Path, help="goh_ai.log")
        if name in ("grade", "run", "sweep"):
            p.add_argument(
                "--game-log",
                type=Path,
                help="game.log with ordered GOH_L1 markers (required for standalone E2E grading)",
            )
            p.add_argument("--json", action="store_true")
    a = ap.parse_args(argv)
    try:
        if a.cmd == "stop":
            live_launch.stop_persistent_session(a, game_dir(a.game_dir), sys.modules[__name__])
            print("Stopped persistent game session and restored profile/INI")
            return 0
        if a.cmd == "reload":
            info = live_launch.reload_persistent_session(
                a, game_dir(a.game_dir), sys.modules[__name__], force_dll=a.force_dll, wait=not a.no_wait
            )
            print(
                f"Triggered in-process mission restart (restart_seq={info['restart_seq']}, "
                f"dll_reloaded={info['reloaded_dll']})"
            )
            return 0
        if a.cmd == "clean":
            options = None
            if a.profile or any(
                (Path.home() / "Documents/My Games/gates of hell/profiles").glob(
                    "*/options.set.ai_test_harness.bak"
                )
            ):
                options = live_launch.profile_options(a.profile)
            print(clean(game_dir(a.game_dir), options))
            return 0
        if a.cmd == "sweep":
            profiles = live_launch.sweep_profiles(a.profiles, a.custom, a.sim_speed)
            summary = live_launch.sweep(a, game_dir(a.game_dir), profiles, sys.modules[__name__])
            print(json.dumps(summary, indent=2) if a.json else live_launch.comparison_table(summary))
            return 0 if summary["passed"] else 1
        settings = overrides(a.set)
        if a.cmd == "run":
            report = live_launch.run(a, game_dir(a.game_dir), settings, sys.modules[__name__])
            live_launch.archive_grade(report, live_grade)
            print(json.dumps(report, indent=2, allow_nan=False) if a.json else live_grade.table(report))
            return 0 if report["passed"] else 1
        if a.cmd == "install":
            print(install(game_dir(a.game_dir), a.suite, a.speed, settings, a.write_ini))
            return 0
        if a.cmd == "generate":
            text = mission.generate(a.suite, a.speed)
            if a.game_dir:
                game = game_dir(a.game_dir)
                verify_game(game)
                mission.check_stock(game, text)
            if a.output:
                path = a.output.resolve()
                if path.suffix.lower() != ".mi" or any(
                    (d / "binaries/x64/call_to_arms.exe").exists() and path.is_relative_to(d / "resource")
                    for d in path.parents
                ):
                    raise ValueError("choose a .mi output outside stock resources")
                atomic_write(path, text.encode())
            else:
                sys.stdout.write(text)
            return 0

        directory = (
            None if a.telemetry and a.ai_log and not a.game_dir else game_dir(a.game_dir) / "binaries/x64"
        )
        telemetry, log = a.telemetry, a.ai_log
        if directory:
            cfg = configparser.ConfigParser(
                strict=False, interpolation=None, inline_comment_prefixes=(";", "#")
            )
            cfg.read(directory / "goh_ai.ini", encoding="utf-8-sig")
            telemetry = telemetry or directory / cfg.get(
                "telemetry", "filename", fallback="goh_telemetry.jsonl"
            )
            log = log or directory / cfg.get("logging", "filename", fallback="goh_ai.log")
        with telemetry.open(encoding="utf-8") as f:
            report = live_grade.grade(
                telemetry_lines(f),
                log.read_text(errors="replace"),
                a.game_log.read_text(errors="replace") if a.game_log else None,
                a.suite,
            )
        print(json.dumps(report, indent=2, allow_nan=False) if a.json else live_grade.table(report))
        return 0 if report["passed"] else 1
    except (
        ValueError,
        OSError,
        KeyError,
        zipfile.BadZipFile,
        configparser.Error,
        subprocess.SubprocessError,
    ) as e:
        if a.cmd in ("grade", "run", "sweep") and a.json:
            print(json.dumps({"passed": False, "error": str(e)}))
        else:
            print(str(e), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
