# Development Guide

Build, lint, test, and static-analysis workflows for **Gates of Hell: MP AI Overhaul** (host-only native AI extender for **Call to Arms: Gates of Hell - Ostfront**).

See [README.md](README.md) for gameplay changes and [AGENTS.md](AGENTS.md) for coding-agent rules and full multiplayer non-desync invariants.

## Repository layout

| Path | Role |
| --- | --- |
| `mods/goh_ai_mod.cpp`, `mods/tu_*.cpp`, `mods/ai_modules.hpp` | Unified host-only AI DLL for Steam build `26734796` split across 4 translation units (coordinator, combat, infantry/support, tanks; exports `ShutdownAndUnload`). |
| `mods/game_runtime.hpp`, `mods/dynamic_store.hpp`, `mods/config_and_log.hpp`, `mods/game_classifiers.hpp`, `mods/actor_coordinator.hpp` | Shared build-`26734796` runtime/PE32+ RTTI verifier/hook-drain gates, heap-backed growable tables, optional `goh_ai.ini` config + leveled logger, unified entity/weapon/smoke classifiers, and actor priority coordinator. |
| `mods/*_module.hpp`, `mods/*_policy.hpp`, `mods/ai_support_26734796.hpp` | Native hook adapters, pure C++17 policies, and shared build-`26734796` order-guard helpers. |
| `mods/grenade_dodge.cpp` | Thin standalone grenade-dodge DLL wrapper. |
| `proxy/version_proxy.cpp` | Drop-in `version.dll` proxy for `binaries/x64/`: lazily forwards all 17 `System32\version.dll` exports outside the loader lock and auto-loads sibling `goh_ai_mod.dll` into `call_to_arms.exe` (skipping `call_to_arms_ed.exe`). |
| `scripts/lint_cpp.sh`, `scripts/build_ai_dll.sh` | C++ formatting (`clang-format`), cross-compile warnings (`x86_64-w64-mingw32-g++`), `clang-tidy` gate, and incremental parallel multi-TU MinGW DLL builder. |
| `research/notes/` | Build-`25492057` baseline maps, decision catalog, and build-`26734796` feature contracts. |
| `research/artifacts/build-<id>/` | Compact tracked PE scans, compatibility baseline (`build-25492057/compatibility-profile.json`), fine maps, and validation receipts. |
| `research/scripts/` | PE scanner, headless Ghidra driver, update auditor, and map/catalog verifiers. |
| `research/tests/` | Python unit tests (`test_*.py`) and C++17 offline policy/adapter fixtures (`test_*.cpp`). |
| `research/compatibility/` | Compatibility contract schema and [runtime smoke scenarios](research/compatibility/runtime-smoke.md). |
| `build/`, `local-work/` | Untracked binaries, logs, injectors, MinHook checkout, venvs, focused Ghidra exports, and `.gpr`/`.rep/` databases. |

## Runtime constraints

Every module in `mods/` must keep host and vanilla clients in sync:

- **Zero `resource/` edits:** Gate all hooks on exact `call_to_arms.exe` PE SHA-256 (`81a13de787c1869b766004cdcadb9ff99012f0b1b58b89db9a7741b10b9f059a` on build `26734796`) and live PE32+ MSVC x64 RTTI vtable verification.
- **Host-only, serial main thread:** Check `!IsRemoteClient()`, `!Core::IsQuantWorkerThread()`, and live `Actor::eHumanBrain` / `Actor::eVehicleBrain` vtables (never `*Client` stubs or worker/render threads).
- **Replicating outputs only:** Act through host brain policy fields, direct goals, or native orders/setters that emit wire commands under `IsHostEmit()`. Never write raw chassis, cover, position, bone, health, or ammo fields.
- **Respect authority & RNG:** Bail on Direct Control, brain inhibition (`(brain+0x39C & 0x3F) != 0`), and manual chassis/weaponry flags. Never draw from engine RNG (`synced_global` or default thread RNG) or poll RNG-drawing helpers.
- **Clean lifecycle:** Roll back partial hook staging in reverse order, drain all 79 active hooks (one coordinator, 23 combat, 46 infantry and nine tank hooks) cleanly via `runtime::global_inflight` in `ShutdownAndUnload`, release heap-backed state tables after unhook, and read/write optional `goh_ai.ini` and logs beside the loaded DLL (never hardcoded local paths).

## Building, linting, and testing

### C++ lint & warning gate

Run before finishing any C++ edit in `proxy/` or `mods/`:

```sh
./scripts/lint_cpp.sh --fix --werror <touched files>
# Or check all modified tracked / all on-disk C++ files:
./scripts/lint_cpp.sh --werror
./scripts/lint_cpp.sh --all-files --werror
```

Enforces `.clang-format` (`SortIncludes: Never` to preserve Windows header order), cross-compiles with `x86_64-w64-mingw32-g++ -std=c++17 -fsyntax-only` warning flags, and runs `clang-tidy` (`.clang-tidy`) when installed. Annotate `printf`-style helpers with `__MINGW_PRINTF_FORMAT` / `format(printf, ...)`.

### Building the unified AI DLL & `version.dll` proxy

Build the 4-TU unified AI DLL alongside the drop-in `version.dll` proxy (or standalone `grenade_dodge.dll`) incrementally with MinGW-w64 against MinHook (`local-work/minhook` or `build/_deps/minhook-src`):

```sh
# Unified 4-TU AI DLL + drop-in version.dll proxy (caches .o/.d under local-work/build-ai/obj):
./scripts/build_ai_dll.sh --out local-work/build-ai/goh_ai_mod.dll

# Clean rebuild, proxy-only, AI-only, or standalone grenade-dodge DLL:
./scripts/build_ai_dll.sh --clean --out local-work/build-ai/goh_ai_mod.dll
./scripts/build_ai_dll.sh --proxy-only --out local-work/build-ai/goh_ai_mod.dll
./scripts/build_ai_dll.sh --no-proxy --out local-work/build-ai/goh_ai_mod.dll
./scripts/build_ai_dll.sh --standalone-grenade --out local-work/build-ai/grenade_dodge.dll
```

### C++ offline tests

`research/tests/test_*.cpp` run offline without loading the game. Pure policy and architecture suites (`test_architecture_26734796.cpp`, `test_tank_crusher.cpp`, `test_tactical_infantry_26734796.cpp`, `test_tactical_objectives_26734796.cpp`) build directly with host `clang++`. Adapter suites that include `<windows.h>` build with MinGW-w64/MSVC, or on macOS/Linux under ASan + UBSan with `-D__fastcall=` and a minimal `<windows.h>` TLS/SRWLOCK shim directory (e.g. `local-work/test-bin`):

```sh
mkdir -p local-work/test-bin
for src in research/tests/test_*.cpp; do
  out="local-work/test-bin/$(basename "$src" .cpp)"
  clang++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
    -Wall -Wextra -Werror -D__fastcall= -Imods -Ilocal-work/test-bin "$src" -o "$out" && "$out"
done
```

`test_engine_fixes_26734796.cpp` exercises all eight new hooks, the shared hook extensions and the post-quant reload through synthetic actors and native-call substitutes. Use the sanitizer flags above. [Build contract](research/notes/engine-fixes-26734796.md)

`test_wave4_combat_logistics_26734796.cpp` covers weapon changes, safe exit selection, passenger unloads and squad transfers. The grenade-release cases run in `test_close_grenade_26734796.cpp`. To check both Wave 4 entries with MinHook's instruction copier, build the Wave 4 fixture with `-DGOH_WAVE4_TRAMPOLINES` and the same MinHook objects and platform definitions used by the penetration fixture. [Build contract](research/notes/combat-logistics-26734796.md)

`test_wave5_combat_hazards_26734796.cpp` covers Advance and Attack orders, blast reactions, crush avoidance and hazard priority. The six-entry trampoline check uses `-DGOH_WAVE5_TRAMPOLINES` with the same MinHook objects and platform definitions as Wave 4. Pass six binary files as arguments, each containing the first 64 bytes at the corresponding RVA in the fingerprinted PE. The argument order is `0x8E79F0`, `0x904930`, `0xA7B5F0`, `0x91F2C0`, `0x91EA20`, `0x96E260`. The fixture checks instruction boundaries, jump-back destinations and detour relays. The architecture suite checks the four module switches, vtable contracts and 79-hook total. [Build contract](research/notes/combat-hazards-26734796.md)

Wave 6 uses the existing post-quant callbacks and repair hooks. Run `test_wave6_repair_recrew_scavenge_26734796.cpp` with the standard sanitizer and warning flags above. It exercises non-tank and gun repair dispatch, same-player seat claims, manual exit detection, weapon collection, corpse transfer ownership and Hold Position returns. The architecture suite checks the switches, tuning limits, coordinator priorities and the unchanged 79-hook total. [Build contract](research/notes/repair-recrew-scavenge-26734796.md)

On non-Windows hosts, compile MinHook’s `hde64.c` with the sanitizer flags too. HDE reads immediates and displacements directly from arbitrary byte offsets in the x86 instruction stream. In its C-only Windows shim, declare `UINT16`, `UINT32` and `UINT64` as 16-, 32- and 64-bit unsigned types with Clang’s `__attribute__((aligned(1)))`. This models those byte-aligned reads on the host and keeps UBSan alignment checks enabled for the entire suite.

Extra fixture flags on non-Windows hosts:

- `test_tactical_infantry_26734796.cpp`: `-DGOH_TACTICAL_ADAPTER_FIXTURES`
- `test_tactical_objectives_26734796.cpp`: `-DGOH_TACTICAL_OBJECTIVES_ADAPTER_TEST`
- `test_penetration_at_26734796.cpp`: `-DGOH_AT_TRAMPOLINES` plus MinHook's `trampoline.c` / `hde64.c`

### Python tests & map verifiers

```sh
# Core stdlib tests (Python 3.10+):
python3 -m unittest discover -s research/tests -v

# Full suite including Capstone update-audit tests:
python3 -m venv local-work/compat-venv
local-work/compat-venv/bin/python -m pip install -r research/requirements-compat.txt
local-work/compat-venv/bin/python -m unittest discover -s research/tests -v
```

Verify a build fine-map against local game binaries, resources, and focused exports:

```sh
python3 research/scripts/verify_fine_map.py \
  --map research/artifacts/build-26734796/<feature>-26734796.json \
  --binaries-dir "$GOH_BINARIES" \
  --exports-dir local-work/exports/build-26734796 \
  --resources-dir "$GOH_RESOURCES"
```

## Level 1 live verification

For Steam build 26734796, place the built `goh_ai_mod.dll` and `version.dll` beside `binaries/x64/call_to_arms.exe`. With Steam running and the game closed, start a full single-player verification run.

```sh
python3 scripts/live_harness.py run --suite full --speed fast --restore-options
```

The command installs the overlay, enables telemetry, activates `ai_test_harness:0` in the player profile, and launches the game with `-no_preload` (skipping multiplayer CRC preload). Opt-in `[testing]` INI settings (off by default for regular players) disable intro videos (`skip_intro=true`), auto-launch Bootcamp → Vehicle Control natively (`auto_mission=single/00-bootcamp/02_vehicle-control:0`), auto-dismiss the loading prompt (`auto_dismiss_loading=true`), suppress cursor warping and window focus stealing (`suppress_cursor_grab=true`), and exit cleanly via native `Events::cmQuit` (`quit_seq`) without moving the mouse cursor or stealing focus (falling back to the MinGW Win32 UI helper in `local-work/build-ai/` only if an older DLL is installed). It waits for the final GEM marker and completion witness, closes the game, and saves the three logs, table, and JSON grade under `local-work/level1/runs/`. Exit status 0 means every check passed, 1 means a run check failed, and 2 means setup failed.

Each phase starts with a wide overview that includes its initial positions and later waves.

Use `--profile PATH` to select among profiles and `--game-dir PATH` or `GOH_GAME_DIR` for another Steam library. CrossOver defaults to the Steam bottle. Use `--wine PATH` or `--bottle NAME` for another installation. `--restore-options` restores the prior mod selection on exit while preserving other option changes. Otherwise `clean` restores the saved selection and removes the overlay.

For rapid iteration, `run` and `sweep` default to `--speed fast`, persistent single-process sessions (`--keep-open` and `--reuse` on by default), shadow-copy DLL hot-swap (`[testing] hot_reload=true`), and native in-process `Events::Scene::cmGameRestart` (`restart_seq`). Pass `--close` to close the game cleanly via `Events::cmQuit` after completion, or `--fresh-process` to cold-boot per trial:

```sh
# Fast day-to-day iteration (boots once, keeps open, auto-reuses & hot-swaps rebuilt DLLs on subsequent runs):
python3 scripts/live_harness.py sweep
python3 scripts/live_harness.py sweep

# Manually hot-swap a rebuilt goh_ai_mod.dll and restart the mission in-process:
python3 scripts/live_harness.py reload --force-dll

# Close the persistent game cleanly via cmQuit and restore options.set / goh_ai.ini (or pass --close to run/sweep):
python3 scripts/live_harness.py stop
```

The serial observer opens the configured JSONL on the first actor callback. It checks the INI once per second for module, tuning, logging, telemetry, and testing lifecycle changes (`sim_speed`, `suppress_cursor_grab`, `restart_seq`, `quit_seq`). The regular log filename and extra smoke lists are set at startup. Both `run` and `sweep` restore the original INI on exit (or on `stop` for `--keep-open` sessions). A manual install with `--write-ini` persists its settings.

```sh
python3 scripts/live_harness.py install --suite full --speed fast --write-ini
python3 scripts/live_harness.py grade --suite full --game-log "/path/to/log/game.log"
python3 scripts/live_harness.py clean
```

For a manual install, activate the mod and restart Vehicle Control after switching suites. Standalone grading uses the latest telemetry session or mission restart. Disable the overlay before returning to multiplayer.

### Company-scale E2E battle and profile sweeps

Level 1 has two layers. The `full`, `infantry`, `armor`, and `close_at` suites exercise individual AI behaviors on the stock Vehicle Control map. The `e2e` suite grades one continuous 300-second battle on a flattened, cleared `z = 0` symmetric arena overlay (`map`, `map.net`, `map.ply`, `map.grs` generated read-only from stock `single.pak` into `mods/ai_test_harness/`, and removed automatically for component suites or `clean`). Player 2 (west) and Player 3 (east) command identical German forces mirrored across the central flag and symmetric lane cover. Player 0 spectates.

| Simulation time | South · close quarters | Center · flag and road | North · armor flank |
|---|---|---|---|
| 0 s | Six infantry and a rear medic fight through walls, fences and sandbags. | A loaded halftrack and BA-64 support four low-ammo infantry, a medic and an ammo truck. | A Panzer IV, T-34 with hull riders, AT gun and four infantry enter. |
| 90 s | Four infantry reinforce the line. | A second loaded halftrack and armored car advance. | A second Panzer IV and four escorts advance. |
| 180–300 s | Survivors continue fighting. | The flag remains contested. | A T-34 with hull riders and four assault infantry push toward the flag. |

A sweep defaults to `e2e`, a single-side `default` Modded-vs-Vanilla run placing our modded AI on the slightly disadvantaged East side (Player 3, mask 8 vs Stock West P2), and 8× simulation speed (~1m 40s per trial on the flat symmetric arena), scored from East P3’s perspective (`> 50` means Modded East beat Vanilla West despite West’s slight ~1–5 pt first-updater edge). Only include `vanilla` (`--profiles vanilla,default`) or `--swap-sides` sporadically when changing the E2E scenario composition or checking a baseline. With `--swap-sides`, each repetition plays the modded profile once on the west (mask 4) and once on the east (mask 8) and averages from the modded side’s perspective, while `vanilla` disables all 40 modules (mask 0) and runs once per repetition (paired reference averages both perspectives to exactly 50; raw west score stays in the trial archive).

```sh
# Fast day-to-day single-side E2E iteration (~1m 40s):
scripts/live_harness.py sweep --speed fast --restore-options
# Sporadic baseline / side-swapped check after scenario changes:
scripts/live_harness.py sweep --profiles vanilla,default --swap-sides --runs 1 --speed fast --restore-options
scripts/live_harness.py sweep --profiles default,tight,tactical,wide --profile local:medic_search_radius_m=30 --sim-speed 8 --speed fast
scripts/live_harness.py run --suite e2e --set testing.sim_speed=8 --speed fast --restore-options
```

Use a DLL with `[testing] ai_player_mask` and `sim_speed`. The observer records both teams. `--sim-speed 0` keeps the mission’s stock speed setting. Values from 0.25 to 20 set the DLL’s simulation multiplier after mission start. Both `run` and `install` accept `--set testing.sim_speed=VALUE`. Automatic E2E runs temporarily set Heroic difficulty, disable fog and vertical sync, and apply low graphics. The display adapter, resolution and window mode retain their original settings. On exit the harness restores the pretrial `options.set` and `goh_ai.ini` bytes, including after a failed run.

**A score of 50 means parity.** Each side deploys 102 points across 46 scored units. Tanks are worth 10, halftracks and AT guns 5, armored cars and supply trucks 3, medics 2 and infantry 1. Halftrack passengers count as infantry. Interior crews and reserve tank riders are excluded. Remaining force value is 60% surviving points plus 40% HP-weighted points, expressed as a percentage of deployed points. The score is 50 plus half the difference between the two sides’ percentages. The exchange ratio divides opposing lost value by friendly lost value with a one-point minimum denominator.

Each trial archives the effective INI, logs, class survival and remaining HP, action counters, shutdown recrew/scavenge totals when available, and integrity checks in its evidence directory. The sweep writes `summary.json` and `comparison.txt` under `local-work/level1/sweeps/sweep-*/`. Rankings and deltas against vanilla require all planned trials for the profile to pass. For offline validation, run the following commands against the installed game archives and Vehicle Control terrain.

```sh
scripts/live_harness.py generate --suite e2e --game-dir "$GOH_GAME_DIR" > /dev/null
scripts/live_harness.py generate --suite full --game-dir "$GOH_GAME_DIR" > /dev/null
local-work/compat-venv/bin/python -m unittest discover -s research/tests -v
```

Earlier functional runs are archived in the [Level 1 verification results](research/notes/level1-live-verification-26734796.md).

## Static analysis & game updates

Point `GOH_BINARIES` at `Call to Arms - Gates of Hell/binaries/x64`, `GOH_RESOURCES` at `resource/`, and `GOH_BUILD_ID` at the Steam build ID in `appmanifest_400750.acf`. Map `call_to_arms.exe`; use `call_to_arms_ed.exe` only when an editor assertion answers an open item.

### 1. PE scan & update audit

Run existing scanners before manual diffing on a new game build:

```sh
python3 research/scripts/scan_pe.py \
  --binary "$GOH_BINARIES/call_to_arms.exe" \
  --build-id "$GOH_BUILD_ID" \
  --output-dir "research/artifacts/build-$GOH_BUILD_ID"

local-work/compat-venv/bin/python research/scripts/check_update.py \
  --profile research/artifacts/build-25492057/compatibility-profile.json \
  --binaries-dir "$GOH_BINARIES" \
  --resources-dir "$GOH_RESOURCES" \
  --build-id "$GOH_BUILD_ID" \
  --output-dir "local-work/compat/build-$GOH_BUILD_ID"
```

See [version-compatibility.md](research/notes/version-compatibility.md) for exit codes, statuses (`UNCHANGED`, `RELOCATED`, `CONTRACT_CHANGED`, `UNRESOLVED`, `AMBIGUOUS`), and matching limits.

### 2. Incremental headless Ghidra

Tested with Ghidra 12.1.4 and OpenJDK 21 (`GHIDRA_HOME` / `JAVA_HOME` or `--ghidra-home` / `--java-home` override Homebrew defaults). Import once per build into `local-work/ghidra/GoH_<build>.gpr` (Ghidra rejects hidden `.` path segments), then run focused exports:

```sh
# One-time import per build:
python3 research/scripts/run_headless.py --build-id "$GOH_BUILD_ID" \
  import --binary "$GOH_BINARIES/call_to_arms.exe"

# Focused export (two-column RVA/label TSV; optional --anchors TSV, third column 'write' for writes only):
python3 research/scripts/run_headless.py --build-id "$GOH_BUILD_ID" \
  export --program call_to_arms.exe --focused \
  --candidates local-work/candidates.tsv \
  --output "local-work/exports/build-$GOH_BUILD_ID/topic"
```

- `export` verifies PE SHA-256, requires exact function entries, and dumps CFG blocks/edges, disassembly, and decompilation without guessing types or names.
- `members` searches literal memory displacements across analyzed instructions (requires manual object-type review).
- Keep Ghidra databases, raw exports, and temporary files in `local-work/`.

## Evidence & approval levels

1. **Structural:** RTTI, vtables, xrefs, or unwind ranges are address leads only.
2. **Static reviewed:** Disassembly, decompilation, callers, and verified fine-maps back a contract (`runtime_allowed: false`).
3. **Runtime verified:** Controlled live SP/MP smoke runs on the exact fingerprinted PE confirm ABI, thread affinity, lifetimes, clean unload, and vanilla-client parity ([runtime-smoke.md](research/compatibility/runtime-smoke.md)).

Addresses in notes and manifests are **RVAs** unless labeled otherwise; map game and editor independently. Follow the scope rule in [decision-frontier-25492057.md](research/notes/decision-frontier-25492057.md).

## Research index

- **Scope & catalog:** [decision-frontier-25492057.md](research/notes/decision-frontier-25492057.md) · [decision-catalog-25492057.md](research/notes/decision-catalog-25492057.md)
- **Compatibility & smoke:** [version-compatibility.md](research/notes/version-compatibility.md) · [runtime-smoke.md](research/compatibility/runtime-smoke.md) · [upstream-audit.md](research/notes/upstream-audit.md)
- **Build `25492057` baseline:** [build](research/notes/build-25492057.md) · [fine](research/notes/fine-map-25492057.md) · [flow](research/notes/flow-map-25492057.md) · [tactics](research/notes/tactics-map-25492057.md) · [systems](research/notes/systems-map-25492057.md) · [perception](research/notes/perception-map-25492057.md) · [artifacts](research/artifacts/build-25492057/)
- **Build `26734796` contracts:** `research/notes/*-26734796.md` and `research/artifacts/build-26734796/`
