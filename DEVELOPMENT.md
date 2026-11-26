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

The command installs the overlay, enables telemetry, activates `ai_test_harness:0` in the player profile and launches the game with `-no_reload_caching`. It compiles a Win32 UI helper with MinGW GCC into `local-work/build-ai/`, skips the intro, selects Bootcamp → Vehicle Control, and dismisses the loading prompt. It waits for the final GEM marker and completion witness, closes the game, and saves the three logs, table and JSON grade under `local-work/level1/runs/`. Exit status 0 means every check passed, 1 means a run check failed, and 2 means setup failed.

Each phase starts with a wide overview that includes its initial positions and later waves.

Use `--profile PATH` to select among profiles and `--game-dir PATH` or `GOH_GAME_DIR` for another Steam library. CrossOver defaults to the Steam bottle. Use `--wine PATH` or `--bottle NAME` for another installation. The game window receives normalized menu clicks using the English UI. `--restore-options` restores the prior mod selection on exit while preserving other option changes. Otherwise `clean` restores the saved selection and removes the overlay.

The serial observer opens the configured JSONL on the first actor callback. It checks the INI once per second for module, tuning, logging and telemetry changes. The regular log filename and extra smoke lists are set at startup. Both `run` and `sweep` restore the original INI on exit. A manual install with `--write-ini` persists its settings.

```sh
python3 scripts/live_harness.py install --suite full --speed fast --write-ini
python3 scripts/live_harness.py grade --suite full --game-log "/path/to/log/game.log"
python3 scripts/live_harness.py clean
```

For a manual install, activate the mod and restart Vehicle Control after switching suites. Standalone grading uses the latest telemetry session or mission restart. Disable the overlay before returning to multiplayer.

### Mirrored AI-vs-AI calibration and profile sweeps

`calibration` pits Player 2 (Side A, west) against Player 3 (Side B, east) on Vehicle Control. Both use AI control, identical German forces, and reflected starting positions, cover and orders. Player 0 spectates. Both teams are hostile to each other and to the spectator. Each trial temporarily sets the player profile to Heroic (`{difficulty heroic}`) and `{fog_of_war fog_off}` (restored byte-for-byte on exit) so both teams get the same 1.0 accuracy and 0.7 health multiplier and the live battle stays visible to the spectator camera.

| Engagement | Sim time | Outcome under test |
|---|---:|---|
| Infantry grenade line | 65 s | Each six-man squad holds its line with a rear medic and faces one direct and one off-axis F1 throw. |
| Armor flank ambush | 75 s | Each side advances two tanks and four infantry under cover of a flanking AT gun. |
| Sustained logistics | 85 s | Low-ammo infantry and a BA-64 clash, withdraw and re-engage with an AT gun, medic and supply truck on each side. |
| Combined assault | 75 s | Loaded halftracks, supporting tanks and infantry escorts converge on a central flag. |

Run one configuration, compare the five built-ins, or include a custom profile. `--runs` specifies repetitions per profile. Each repetition launches a fresh game for every selected profile.

```sh
scripts/live_harness.py run --suite calibration --speed fast --restore-options
scripts/live_harness.py sweep --profiles vanilla,default,tight,tactical,wide --runs 1 --swap-sides --speed fast --restore-options
scripts/live_harness.py sweep --profiles vanilla,default --profile local:medic_search_radius_m=30,modules.attack_detour=false --runs 3 --speed fast --restore-options
```

Use a DLL that implements `[testing] ai_player_mask`. The observer records both teams. `vanilla` disables all 40 modules and sets mask `0` for a stock-vs-stock baseline. The other built-ins enable all modules and set mask `4` (Player 2 only), leaving Player 3 on stock AI. `default` uses DLL tuning defaults. Custom profiles also start with mask `4` and accept an explicit override such as `--profile east:testing.ai_player_mask=8` to put the improvements on Player 3. Pass `--swap-sides` to run each modded profile on both West (`ai_player_mask=4`, Player 2) and East (`ai_player_mask=8`, Player 3) and average from the modded side's perspective (exact `50.00` vanilla reference, cancelling residual terrain asymmetry). Without `--swap-sides`, scores use Player 2’s perspective. Use `--player-profile PATH` to select the player’s `options.set`. Calibration automatically restores its pretrial bytes after the game closes, including difficulty and mod selection.

**50.0 means parity.** Each combat class compares the two teams using 60% survival rate and 40% remaining HP across its original roster. Infantry and vehicles have equal weight when both deploy. Support survival contributes 10% in scenarios with medics or supply vehicles. The scenario score is 50 plus half of Side A’s signed percentage-point advantage. The four scenarios contribute equally to the overall score. Absence from the last five simulation seconds or zero HP counts as a casualty. The grader checks the difficulty reported at mission start and verifies AI control on observed combatants. The scored infantry include the assault passengers. Interior crew, reserve hull riders and scripted throwers are excluded.

Find the side-to-side survival and HP margins, score change from `vanilla`, and scenario winners in `local-work/level1/sweeps/sweep-*/comparison.txt` and `summary.json`. Baseline runs measure natural variation on the stock terrain. Each completed trial archives its effective INI, logs, and per-side scorecard in `grade.json` and `grade.txt`. Action counters are phase-wide and runaway actions fail the trial. Rankings and baseline deltas require all requested trials for the profile to pass. To regrade a mirrored trial, pass its `--telemetry`, `--ai-log` and `--game-log` paths to `grade --suite calibration`.

For offline stock validation, run `scripts/live_harness.py generate --suite calibration --game-dir "$GOH_GAME_DIR" > /dev/null` and repeat for `full`. This checks the executable fingerprint, installed stock archives and Vehicle Control terrain. The archived build 26734796 results describe earlier functional runs and the retired ten-probe suite. [Historical live verification results](research/notes/level1-live-verification-26734796.md)

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
