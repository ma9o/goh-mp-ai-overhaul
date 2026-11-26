# Development Guide

Build, lint, test, and static-analysis workflows for **Gates of Hell: MP AI Overhaul** (host-only native AI extender for **Call to Arms: Gates of Hell - Ostfront**).

See [README.md](README.md) for gameplay changes and [AGENTS.md](AGENTS.md) for coding-agent rules and full multiplayer non-desync invariants.

## Repository layout

| Path | Role |
| --- | --- |
| `mods/goh_ai_mod.cpp`, `mods/tu_*.cpp`, `mods/ai_modules.hpp` | Unified host-only AI DLL for Steam build `26734796` split across 4 translation units (coordinator, combat, infantry/support, tanks; exports `ShutdownAndUnload`). |
| `mods/game_runtime.hpp`, `mods/dynamic_store.hpp`, `mods/config_and_log.hpp`, `mods/game_classifiers.hpp`, `mods/actor_coordinator.hpp` | Shared build-`26734796` runtime/PE32+ RTTI verifier/hook-drain gates, heap-backed growable tables, optional `goh_ai.ini` config + leveled logger, unified entity/weapon/smoke classifiers, and actor priority coordinator. |
| `mods/*_module.hpp`, `mods/*_policy.hpp`, `mods/ai_support_26734796.hpp` | Native hook adapters, pure C++17 policies, and shared build-`26734796` order-guard helpers. |
| `mods/grenade_dodge.cpp`, `mods/example_mod.cpp` | Thin standalone grenade-dodge DLL wrapper and upstream SDK sample. |
| `sdk/`, `proxy/` | Inherited script-extender loader, overlay, Lua bridge, and `version.dll` proxy scaffold ([upstream audit](research/notes/upstream-audit.md)). |
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
- **Clean lifecycle:** Roll back partial hook staging in reverse order, drain all 63/63 active hooks cleanly via `runtime::global_inflight` in `ShutdownAndUnload`, release heap-backed state tables after unhook, and read/write optional `goh_ai.ini` and logs beside the loaded DLL (never hardcoded local paths).

## Building, linting, and testing

### C++ lint & warning gate

Run before finishing any C++ edit in `sdk/`, `proxy/`, or `mods/`:

```sh
./scripts/lint_cpp.sh --fix --werror <touched files>
# Or check all modified tracked / all on-disk C++ files:
./scripts/lint_cpp.sh --werror
./scripts/lint_cpp.sh --all-files --werror
```

Enforces `.clang-format` (`SortIncludes: Never` to preserve Windows header order), cross-compiles with `x86_64-w64-mingw32-g++ -std=c++17 -fsyntax-only` warning flags, and runs `clang-tidy` (`.clang-tidy`) when installed. Annotate `printf`-style helpers with `__MINGW_PRINTF_FORMAT` / `format(printf, ...)`.

### Building the unified AI DLL

`CMakeLists.txt` and `./build.sh` build the upstream `version.dll`, `coa_extender.dll`, and `example_mod.dll` targets. Build the 4-TU unified AI DLL (or standalone `grenade_dodge.dll`) incrementally with MinGW-w64 against MinHook (`local-work/minhook` or `build/_deps/minhook-src`):

```sh
# Unified 4-TU AI DLL (caches .o/.d under local-work/build-ai/obj):
./scripts/build_ai_dll.sh --out local-work/build-ai/goh_ai_mod.dll

# Clean rebuild or standalone grenade-dodge DLL:
./scripts/build_ai_dll.sh --clean --out local-work/build-ai/goh_ai_mod.dll
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
