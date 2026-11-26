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
LEGACY_INFO = (
    '{mod\n {name "AI Test Harness (local)"}\n {desc "Level 1 verification - Vehicle Control bootcamp"}\n}\n'
)
INFO = (
    '{mod\n {name "ai_test_harness"}\n {desc "Level 1 verification - Vehicle Control bootcamp"}\n'
    ' {minGameVersion "1.069"}\n {maxGameVersion "1.069"}\n}\n'
)
MODULES = {
    "grenade_dodge",
    "soft_cover_lof",
    "attack_detour",
    "target_prioritization",
    "auto_smoke",
    "tank_crusher",
    "tank_auto_repair",
    "auto_medic",
    "auto_resupply",
    "close_grenade",
    "tactical_infantry",
    "penetration_at",
    "tactical_objectives",
    "tank_facing",
    "engine_fixes",
    "lull_reload",
    "fast_weapon_swap",
    "grenade_ff_guard",
    "safe_disembark",
    "transport_unload",
    "squad_logistics",
    "tank_crew_fill",
    "internal_crew_swap",
    "nontank_crew_fill",
    "ammo_scavenge",
    "tank_target_memory",
    "cannon_aim_settle",
    "allegiance_guard",
    "overflow_cover",
    "weapon_team_cohesion",
    "mg_crew_small_arms",
    "movement_recovery",
    "convoy_spacing",
}


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
        elif k in ("enabled", "snapshot_interval_ms", "flush_ms", "filename"):
            section, key = "telemetry", k
        elif k in MODULES:
            section, key = "modules", k
        elif k == "level":
            section, key = "logging", k
        else:
            section, key = "tuning", k
        if section not in ("telemetry", "modules", "tuning", "logging", "smoke"):
            raise ValueError("unknown section " + section)
        result[section, key] = v
    return result


def tuning_values(settings):
    return {k: v for (section, k), v in settings.items() if section == "tuning" and k in mission.PROBES}


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


def overlay(game):
    base = safe_child(game, Path("mods") / MOD)
    info = safe_child(game, base.relative_to(game) / "mod.info")
    file = safe_child(game, base.relative_to(game) / mission.MI)
    if base.exists():
        allowed = {info, file, *file.parents}
        if (
            not info.is_file()
            or info.read_bytes() not in (INFO.encode(), LEGACY_INFO.encode())
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


def active_tuning(path, settings):
    parser = configparser.ConfigParser(interpolation=None, strict=False, inline_comment_prefixes=(";", "#"))
    if path.is_file():
        parser.read(path, encoding="utf-8-sig")
    saved = {
        key: value
        for name in parser.sections()
        if name.lower() == "tuning"
        for key, value in parser[name].items()
        if key in mission.PROBES
    }
    return {**saved, **tuning_values(settings)}


def install(game, suite, speed, settings, write_ini=False):
    verify_game(game)
    base, info, target = overlay(game)
    ini = safe_child(game, Path("binaries/x64/goh_ai.ini"))
    text = mission.generate(suite, speed, active_tuning(ini, settings))
    mission.check_stock(game, text)
    patch = merged_ini(ini, {("telemetry", "enabled"): "true", **settings}) if write_ini or settings else None
    if not base.exists():
        base.parent.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=base.parent, prefix=".level1-") as staging:
            root = Path(staging) / MOD
            atomic_write(root / "mod.info", INFO.encode())
            atomic_write(root / mission.MI, text.encode())
            root.rename(base)
    else:
        if info.read_bytes() != INFO.encode():
            atomic_write(info, INFO.encode())
        atomic_write(target, text.encode())
    if patch is not None:
        atomic_write(ini, patch)
    return target


def clean(game):
    base, info, target = overlay(game)
    if base.exists():
        target.unlink(missing_ok=True)
        for folder in target.parents:
            if folder == base:
                break
            if folder.exists():
                folder.rmdir()
        info.unlink()
        base.rmdir()
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
    for name in ("generate", "install", "clean", "grade", "run"):
        p = subs.add_parser(name)
        p.add_argument("--game-dir", help="Steam game root (or GOH_GAME_DIR)")
        if name != "clean":
            p.add_argument("--suite", choices=mission.SUITES, default="full")
            p.add_argument("--set", action="append", default=[], metavar="[SECTION.]KEY=VALUE")
        if name in ("install", "generate", "run"):
            p.add_argument("--speed", choices=mission.SPEEDS, default="normal")
        if name == "generate":
            p.add_argument("-o", "--output", type=Path, help="write a .mi file (default stdout)")
        if name == "install":
            p.add_argument(
                "--write-ini",
                action="store_true",
                help="enable telemetry beside the DLL; --set implies this",
            )
        if name in ("clean", "run"):
            p.add_argument("--profile", help="player profile directory or options.set")
        if name == "run":
            p.add_argument("--wine", help="Wine or CrossOver wine executable")
            p.add_argument("--bottle", default="Steam", help="CrossOver bottle (default Steam)")
            p.add_argument("--timeout", type=int, default=720, help="overall timeout in seconds")
            p.add_argument(
                "--restore-options",
                action="store_true",
                help="restore prior mod selection on exit",
            )
        if name == "grade":
            p.add_argument("--telemetry", type=Path, help="JSONL from the DLL")
            p.add_argument("--ai-log", "--log", type=Path, help="goh_ai.log")
        if name in ("grade", "run"):
            p.add_argument(
                "--game-log",
                type=Path,
                help="optional game.log with ordered GOH_L1 markers",
            )
            p.add_argument("--json", action="store_true")
    a = ap.parse_args(argv)
    try:
        if a.cmd == "clean":
            options = None
            if a.profile or any(
                (Path.home() / "Documents/My Games/gates of hell/profiles").glob(
                    "*/options.set.ai_test_harness.bak"
                )
            ):
                options = live_launch.profile_options(a.profile)
            print(clean(game_dir(a.game_dir)))
            if options:
                live_launch.profile_mod(options, atomic_write, restore=True)
            return 0
        settings = overrides(a.set)
        if a.cmd == "run":
            report = live_launch.run(a, game_dir(a.game_dir), settings, sys.modules[__name__])
            result_json = json.dumps(report, indent=2, allow_nan=False)
            table = live_grade.table(report)
            out = Path(report["evidence_dir"])
            (out / "grade.json").write_text(result_json + "\n", encoding="utf-8")
            (out / "grade.txt").write_text(table + "\n", encoding="utf-8")
            print(result_json if a.json else table)
            return 0 if report["passed"] else 1
        if a.cmd == "install":
            print(install(game_dir(a.game_dir), a.suite, a.speed, settings, a.write_ini))
            return 0
        if a.cmd == "generate":
            text = mission.generate(a.suite, a.speed, tuning_values(settings))
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
        tuning = tuning_values(settings)
        if directory:
            cfg = configparser.ConfigParser(
                strict=False, interpolation=None, inline_comment_prefixes=(";", "#")
            )
            cfg.read(directory / "goh_ai.ini", encoding="utf-8-sig")
            telemetry = telemetry or directory / cfg.get(
                "telemetry", "filename", fallback="goh_telemetry.jsonl"
            )
            log = log or directory / cfg.get("logging", "filename", fallback="goh_ai.log")
            tuning = active_tuning(directory / "goh_ai.ini", settings)
        with telemetry.open(encoding="utf-8") as f:
            report = live_grade.grade(
                telemetry_lines(f),
                log.read_text(errors="replace"),
                a.game_log.read_text(errors="replace") if a.game_log else None,
                a.suite,
                tuning,
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
        if a.cmd in ("grade", "run") and a.json:
            print(json.dumps({"passed": False, "error": str(e)}))
        else:
            print(str(e), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
