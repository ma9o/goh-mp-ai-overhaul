"""Launch the local Vehicle Control mission and collect evidence from this process."""

import configparser
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOKEN = b'"ai_test_harness:0"'
MODS = re.compile(rb'\{mods\b(?:[^{}"]|"(?:[^"\\]|\\.)*")*\}', re.IGNORECASE)
CROSSOVER = Path("/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine")


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
    if end < 0 or not data.lstrip().startswith(b"{options") or data[end + 1 :].strip():
        raise ValueError("unexpected options.set root")
    newline = b"\r\n" if b"\r\n" in data else b"\n"
    return data[:end] + b"\t{mods " + TOKEN + b"}" + newline + data[end:]


def profile_mod(path, write, restore=False):
    backup = path.with_name("options.set.ai_test_harness.bak")
    data = path.read_bytes()
    if restore:
        if not backup.exists():
            return
        original = backup.read_bytes()
        # Preserve settings the player or game changed during the run.
        # The game serializes the mods block on multiple lines when it exits.
        atoms = re.compile(rb'"(?:[^"\\]|\\.)*"|[{}]|[^\s{}"]+')
        same_options = atoms.findall(data) == atoms.findall(set_mod(original, True))
        changed = original if same_options else set_mod(data, False)
        if data != changed:
            write(path, changed)
        backup.unlink()
    else:
        changed = set_mod(data, True)
        if data != changed:
            if not backup.exists():
                with backup.open("xb") as out:
                    out.write(data)
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


def run(a, game, settings, harness):
    harness.verify_game(game)
    options = profile_options(a.profile)
    game_log = a.game_log or options.parents[2] / "log/game.log"
    binaries = game / "binaries/x64"
    if not all((binaries / name).is_file() for name in ("version.dll", "goh_ai_mod.dll")):
        raise ValueError("install the built version.dll and goh_ai_mod.dll beside call_to_arms.exe")
    if a.timeout <= 0 or settings.get(("telemetry", "enabled"), "true").lower() not in (
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
    harness.install(game, a.suite, a.speed, settings, write_ini=True)
    cfg = configparser.ConfigParser(strict=False, interpolation=None, inline_comment_prefixes=(";", "#"))
    cfg.read(binaries / "goh_ai.ini", encoding="utf-8-sig")
    telemetry = binaries / cfg.get("telemetry", "filename", fallback="goh_telemetry.jsonl")
    ai = binaries / cfg.get("logging", "filename", fallback="goh_ai.log")
    if telemetry.parent != binaries or ai.parent != binaries:
        raise ValueError("expected log filenames beside the DLL")
    offsets = {p: p.stat().st_size if p.exists() else 0 for p in (telemetry, ai)}
    log = GameLog(game_log)
    runs = ROOT / "local-work/level1/runs"
    runs.mkdir(parents=True, exist_ok=True)
    evidence = Path(tempfile.mkdtemp(prefix="run-", dir=runs))
    print(f"Run evidence {evidence}", file=sys.stderr)
    errors = []
    process = None
    stopped = True
    profile_mod(options, harness.atomic_write)
    try:
        with (evidence / "launcher.log").open("w") as out:
            process = subprocess.Popen(
                [*prefix, str(binaries / "call_to_arms.exe"), "-no_reload_caching"],
                cwd=binaries,
                env=env,
                stdout=out,
                stderr=subprocess.STDOUT,
            )
            deadline = time.monotonic() + a.timeout
            drive(ui, process, log, a.suite, deadline)
            mid = (90 + harness.mission.SUITES.index(a.suite)) * 100 + 1
            wait_witness(telemetry, offsets[telemetry], mid, deadline)
    except (ValueError, OSError, subprocess.SubprocessError) as exc:
        errors.append(str(exc))
    finally:
        if process:
            try:
                stopped = stop_game(ui, process, log)
            except (OSError, subprocess.SubprocessError) as exc:
                errors.append(str(exc))
                stopped = False
            if not stopped:
                errors.append("game is still open after the exit request")
        if stopped and a.restore_options:
            profile_mod(options, harness.atomic_write, restore=True)

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
            harness.active_tuning(binaries / "goh_ai.ini", settings),
        )
    for error in errors:
        report["checks"].append({"phase": "run", "check": "launcher", "passed": False, "evidence": error})
        report["passed"] = False
    report["evidence_dir"] = str(evidence)
    return report
