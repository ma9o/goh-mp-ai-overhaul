"""Launch the local Vehicle Control mission and collect evidence from this process."""

import configparser
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
from contextlib import contextmanager
from pathlib import Path
from statistics import mean

ROOT = Path(__file__).resolve().parents[1]
TOKEN = b'"ai_test_harness:0"'
MODS = re.compile(rb'\{mods\b(?:[^{}"]|"(?:[^"\\]|\\.)*")*\}', re.IGNORECASE)
CROSSOVER = Path("/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine")

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
    b"clip_distance": b"0.25", b"fxaa": b"0", b"hdr": b"0", b"bloom": b"0",
    b"waterBump": b"0", b"vls": b"0",
}
VIDEO_FLAGS = (
    b"vertical_sync antialiasing grass dof glow rim_light filmGrain specular environment "
    b"lens_refraction softIntersection fake_translucency alphaToCoverage _ssao lens_effects_options"
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


def drive(ui, process, log, suite, deadline):
    def wait(message, pulse=None, seconds=90):
        stop = min(deadline, time.monotonic() + seconds)
        while time.monotonic() < stop:
            data = log.read()
            if "***************** Exception" in data:
                detail = data.partition("***************** Exception *****************")[2].strip()
                reason = detail.partition("\n")[0] or "during game startup or mission execution"
                raise ValueError(f"GEM exception {reason}")
            if message in data:
                return
            if process.poll() is not None:
                raise ValueError(f"game exited before {message}")
            if pulse:
                pulse(data)
            time.sleep(0.35)
        raise ValueError(f"timed out waiting for {message}")

    def click(x, y):
        if ui("click", str(x), str(y)):
            raise ValueError("game window did not accept a menu click")
        time.sleep(0.65)

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
    wait("Start mission single/00-bootcamp/02_vehicle-control:0", seconds=12)
    prompt_keys = iter((("space",), ("click", "449", "548")) * 100)

    def loading_prompt(data):
        if "GOH_L1_BEGIN_" in data:
            ui(*next(prompt_keys, ("space",)))

    wait("GUI: Activate page page_scene_game", loading_prompt, seconds=150)
    print("Vehicle Control is running", file=sys.stderr)
    wait(f"GOH_L1_DONE_{suite}", seconds=max(0, deadline - time.monotonic()))


def stop_game(ui, process, log):
    if process.poll() is not None:
        return True
    baseline = len(log.read())
    normal_exit = ui("dismiss") != 0
    if normal_exit:
        ui("close")
    confirmed = False
    for attempt in range(65):
        if process.poll() is not None:
            return True
        dialog = "Activate page genereic_status_dialog" in log.read()[baseline:]
        if not confirmed and dialog:
            confirmed = ui("click", "483", "321") == 0
        elif normal_exit and attempt == 5 and not dialog:
            # During a scenario the game can defer its exit confirmation until Escape.
            ui("esc")
        time.sleep(0.25)
    return process.poll() is not None


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
                    row = json.loads(line)
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

    status = ui("present")
    if status != 3:
        raise ValueError(
            "close the existing call_to_arms.exe before running" if status == 0 else "UI helper failed"
        )
    ini = harness.safe_child(game, Path("binaries/x64/goh_ai.ini"))
    harness.install(game, a.suite, a.speed, {})
    if profile_ini is None:
        profile_ini = harness.merged_ini(ini, harness.run_settings(a.suite, settings))
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
    if evidence is None:
        runs = ROOT / "local-work/level1/runs"
        runs.mkdir(parents=True, exist_ok=True)
        evidence = Path(tempfile.mkdtemp(prefix="run-", dir=runs))
    print(f"Run evidence {evidence}", file=sys.stderr)
    errors = []
    process = None
    stopped = True
    benchmark = a.suite == "e2e"
    # A functional run may have left the overlay selected. Preserve its
    # pending backup as well as the profile actually in use at trial entry.
    prior_selection = options.read_bytes() if benchmark and options_backup(options).exists() else None
    try:
        profile_mod(options, harness.atomic_write, heroic=benchmark)
        with (evidence / "launcher.log").open("w") as out:
            process = subprocess.Popen(
                [*prefix, str(binaries / "call_to_arms.exe"), "-no_reload_caching"],
                cwd=binaries,
                env=env,
                stdout=out,
                stderr=subprocess.STDOUT,
            )
            deadline = time.monotonic() + launch_timeout(a.suite, cfg, a.timeout)
            drive(ui, process, log, a.suite, deadline)
            mid = (90 + harness.mission.SUITES.index(a.suite)) * 100 + 1
            wait_witness(telemetry, offsets[telemetry], mid, deadline)
    except (ValueError, OSError, subprocess.SubprocessError) as exc:
        errors.append(str(exc))
    finally:
        try:
            if process:
                stopped = False
                try:
                    stopped = stop_game(ui, process, log)
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

    for src, name in ((telemetry, "goh_telemetry.jsonl"), (ai, "goh_ai.log")):
        with (evidence / name).open("wb") as dst:
            if src.exists():
                with src.open("rb") as inp:
                    if src.stat().st_size >= offsets[src]:
                        inp.seek(offsets[src])
                    shutil.copyfileobj(inp, dst)
    (evidence / "game.log").write_text(log.read(), encoding="utf-8")
    shutil.copyfile(binaries / "goh_ai.ini", evidence / "goh_ai.ini")
    with (evidence / "goh_telemetry.jsonl").open(encoding="utf-8") as rows:
        report = harness.live_grade.grade(
            rows,
            (evidence / "goh_ai.log").read_text(errors="replace"),
            log.read(),
            a.suite,
        )
    for error in errors:
        report["checks"].append({"phase": "run", "check": "launcher", "passed": False, "evidence": error})
        report["passed"] = False
    if errors and "tactical" in report:
        report["tactical"]["overall_score"] = None
    report["evidence_dir"] = str(evidence)
    return report


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
    return metrics


def orient_tactical(tactical, side="A"):
    """View a trial's tactical summary from the evaluated side's perspective."""
    if side == "A":
        return tactical
    scenarios = {}
    for phase, sc in tactical["scenarios"].items():
        score = None if sc["score"] is None else round(100.0 - sc["score"], 2)
        scenarios[phase] = {
            **sc,
            "score": score,
            "sides": {"A": sc["sides"]["B"], "B": sc["sides"]["A"]},
        }
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
        profiles[name] = {
            "runs": len(items),
            "valid_runs": len(complete),
            "score": mean(t["overall_score"] for t in valid) if valid else None,
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
    metric("E2E force-value score (50 = parity)", (p["score"] for p in profiles.values()), best["overall"])
    if "vanilla" in profiles:
        metric("Delta vs vanilla", (p["delta_vs_vanilla"] for p in profiles.values()), fmt="+.2f")
    evaluated = "both" if summary.get("swap_sides") else summary["evaluated_side"]
    header, delta_label = {
        "both": (
            "Mirrored AI vs AI (side-swapped crossover: Modded plays West P2 + East P3 vs Vanilla)",
            " Modded−Vanilla (pp)",
        ),
        "B": (
            "Mirrored AI vs AI   Evaluated = Side B / Player 3 (east, disadvantaged) vs Side A / Player 2 (west)",
            " East−West (pp)",
        ),
        "A": (
            "Mirrored AI vs AI   Evaluated = Side A / Player 2 (west) vs Side B / Player 3 (east)",
            " West−East (pp)",
        ),
        "mixed": (
            "Mirrored AI vs AI   Each profile scored for its modded side (West P2 mask 4 / East P3 mask 8)",
            " Modded−Vanilla (pp)",
        ),
    }[evaluated]
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

    for repetition in range(1, a.runs + 1):
        for name, settings in profiles.items():
            for side, mask in assignments[name]:
                suffix = f"-side{side}" if len(assignments[name]) > 1 else ""
                evidence = out / f"trial-{repetition:02d}-{name}{suffix}"
                evidence.mkdir()
                run_settings = {**settings, PLAYER_MASK: mask}
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
