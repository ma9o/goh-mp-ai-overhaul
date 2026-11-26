# Development Guide

Build, lint, test, and static-analysis workflows for **Gates of Hell — MP AI Overhaul** (host-only native AI extender for **Call to Arms: Gates of Hell — Ostfront**).

For player-facing gameplay impact and unit behavior rules, see [README.md](README.md). For coding-agent rules and non-desync invariants, see [AGENTS.md](AGENTS.md).

## Repository layout

| Path | Role |
| --- | --- |
| `mods/goh_ai_mod.cpp` | Unified host-only AI prototype DLL for Steam build `26734796` (63 MinHook entries across all active AI modules; exports `ShutdownAndUnload`). |
| `mods/*_module.hpp`, `mods/*_policy.hpp`, `mods/ai_support_26734796.hpp` | Per-feature native hook adapters, pure C++17 policy headers, and shared build-`26734796` helpers/authority guards. |
| `mods/grenade_dodge.cpp`, `mods/example_mod.cpp` | Earlier standalone grenade-dodge prototype and upstream SDK plugin sample. |
| `sdk/`, `proxy/` | Inherited script-extender loader, overlay, Lua bridge, and `version.dll` proxy scaffold (kept as reference; see [upstream-audit.md](research/notes/upstream-audit.md)). |
| `scripts/lint_cpp.sh` | C++ format (`clang-format`), cross-compile warning (`x86_64-w64-mingw32-g++`), and static-analysis (`clang-tidy`) gate. |
| `research/notes/` | Baseline build-`25492057` system maps, decision catalog, and build-`26734796` per-feature contract notes. |
| `research/artifacts/build-<id>/` | Compact tracked PE scans, compatibility baseline (`build-25492057/compatibility-profile.json`), fine maps, and validation receipts. |
| `research/scripts/` | PE scanner, headless Ghidra driver, update auditor, and map/catalog verifiers. |
| `research/tests/` | Python unit tests (`test_*.py`) and standalone C++17 offline policy/adapter test suites (`test_*.cpp`). |
| `research/compatibility/` | Compatibility contract schema and pending [runtime smoke scenarios](research/compatibility/runtime-smoke.md). |
| `build/`, `local-work/` | Untracked outputs: compiled DLLs/EXEs, logs, injectors, MinHook checkout, venvs, focused Ghidra exports, and `.gpr`/`.rep/` databases. |

## Architecture constraints

Every runtime module in `mods/` must preserve host/vanilla-client parity:

- **Zero `resource/` edits.** Ship behavior only via native hooks in a DLL gated on exact `call_to_arms.exe` PE SHA-256 (`81a13de787c1869b766004cdcadb9ff99012f0b1b58b89db9a7741b10b9f059a` on build `26734796`).
- **Host-only, serial main simulation thread.** Guard hooks on `!IsRemoteClient()`, `!Core::IsQuantWorkerThread()`, and live concrete `Actor::eHumanBrain` / `Actor::eVehicleBrain` vtables (never `*Client` stubs or worker/render threads).
- **Replicating boundaries only.** Act through host-only brain policy fields, direct goals, or native orders/setters that emit wire commands under `IsHostEmit()`. Never mutate low-level chassis, position, cover, bone, health, or ammo state directly.
- **Authority & RNG discipline.** Always honor Direct Control, brain inhibition (`(brain+0x39C & 0x3F) != 0`), manual chassis/weaponry flags, and player modes/orders. Never draw from engine RNG (`synced_global` or default thread RNG) or poll native helpers that draw RNG; use private state/PRNGs only.
- **Clean lifecycle.** Stage hooks atomically with reverse rollback on failure; support clean unload via `ShutdownAndUnload` (closing admission and draining in-flight hooks before `MH_Uninitialize`). Write logs beside the loaded module, never to hardcoded machine paths.

## Building, linting, and testing

### C++ lint and warning gate

Run before finishing any C++ change in `sdk/`, `proxy/`, or `mods/`:

```sh
./scripts/lint_cpp.sh --fix --werror <touched files>
# Or check all modified tracked C++ files / all C++ files on disk:
./scripts/lint_cpp.sh --werror
./scripts/lint_cpp.sh --all-files --werror
```

The script enforces `.clang-format` (`SortIncludes: Never` for Windows header order), compiles translation units with `x86_64-w64-mingw32-g++ -std=c++17 -fsyntax-only -Wall -Wextra -Wshadow -Wformat=2 -Wundef -Wnull-dereference -Wduplicated-cond -Wlogical-op -Werror=return-type`, and runs `clang-tidy` (`.clang-tidy`) when installed. Annotate `printf`-style helpers with `__MINGW_PRINTF_FORMAT` / `format(printf, ...)`.

### Building the unified AI prototype DLL (`mods/goh_ai_mod.cpp`)

`CMakeLists.txt` and `./build.sh` build the inherited `version.dll`, `coa_extender.dll`, and `example_mod.dll` targets. Build the active host AI prototype (`mods/goh_ai_mod.cpp`) directly with MinGW-w64 against MinHook (`local-work/minhook` or `build/_deps/minhook-src`):

```sh
MINHOOK=local-work/minhook
mkdir -p local-work/build-ai
x86_64-w64-mingw32-g++ -std=c++17 -O2 -m64 -shared -static \
  -Wall -Wextra -Werror -Wno-unknown-pragmas -Wno-cast-function-type \
  -Imods -I"$MINHOOK/include" \
  mods/goh_ai_mod.cpp \
  "$MINHOOK/src/buffer.c" "$MINHOOK/src/hook.c" "$MINHOOK/src/trampoline.c" \
  "$MINHOOK/src/hde/hde32.c" "$MINHOOK/src/hde/hde64.c" \
  -o local-work/build-ai/goh_ai_mod.dll
```

### C++ offline policy & adapter tests

All `research/tests/test_*.cpp` suites run offline with no game binary loaded. Pure policy suites (`test_tank_crusher.cpp`, `test_tactical_infantry_26734796.cpp`, `test_tactical_objectives_26734796.cpp`) build directly with host `clang++`. Adapter suites that include `<windows.h>` build natively with MinGW-w64/MSVC, or on macOS/Linux under Clang ASan + UBSan with `-D__fastcall=` and a minimal `<windows.h>` TLS/SRWLOCK shim directory (e.g. `local-work/test-bin`):

```sh
mkdir -p local-work/test-bin
for src in research/tests/test_*.cpp; do
  out="local-work/test-bin/$(basename "$src" .cpp)"
  clang++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
    -Wall -Wextra -Werror -D__fastcall= -Imods -Ilocal-work/test-bin "$src" -o "$out" && "$out"
done
```

Optional extra fixture macros on non-Windows hosts:
- `test_tactical_infantry_26734796.cpp`: `-DGOH_TACTICAL_ADAPTER_FIXTURES`
- `test_tactical_objectives_26734796.cpp`: `-DGOH_TACTICAL_OBJECTIVES_ADAPTER_TEST`
- `test_penetration_at_26734796.cpp`: `-DGOH_AT_TRAMPOLINES` plus MinHook's `trampoline.c` / `hde64.c` to verify instruction-copy trampolines against sampled PE entry bytes.

### Python tests and map verifiers

```sh
# Core stdlib tests (Python 3.10+, zero third-party deps):
python3 -m unittest discover -s research/tests -v

# Full suite including Capstone update-audit tests:
python3 -m venv local-work/compat-venv
local-work/compat-venv/bin/python -m pip install -r research/requirements-compat.txt
local-work/compat-venv/bin/python -m unittest discover -s research/tests -v
```

Verify a build fine-map against your local installed game binaries, resources, and focused exports:

```sh
python3 research/scripts/verify_fine_map.py \
  --map research/artifacts/build-26734796/<feature>-26734796.json \
  --binaries-dir "$GOH_BINARIES" \
  --exports-dir local-work/exports/build-26734796 \
  --resources-dir "$GOH_RESOURCES"
```

## Static analysis and game-update workflow

Point `GOH_BINARIES` at your installed `Call to Arms - Gates of Hell/binaries/x64`, `GOH_RESOURCES` at its `resource/` directory, and `GOH_BUILD_ID` at the Steam build ID from `appmanifest_400750.acf`. Map the **game** binary (`call_to_arms.exe`); use the editor (`call_to_arms_ed.exe`) only when an editor assertion answers an open item.

### 1. Structural PE scan & update compatibility audit

Always run existing repo scanners before manual diffing on a new game build:

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

See [version-compatibility.md](research/notes/version-compatibility.md) for exit codes, status meanings (`UNCHANGED`, `RELOCATED`, `CONTRACT_CHANGED`, `UNRESOLVED`, `AMBIGUOUS`), and matching limits.

### 2. Incremental headless Ghidra workflow

Tested with Ghidra 12.1.4 and OpenJDK 21 (`--ghidra-home`/`--java-home` or `GHIDRA_HOME`/`JAVA_HOME` override Homebrew defaults). Import **once** per build into `local-work/ghidra/GoH_<build>.gpr` (outside hidden `.` dirs, which Ghidra rejects), then reuse the project for small focused exports:

```sh
# One-time import for a new build (game binary only unless an editor assertion is needed):
python3 research/scripts/run_headless.py --build-id "$GOH_BUILD_ID" \
  import --binary "$GOH_BINARIES/call_to_arms.exe"

# Targeted export using a two-column TSV (RVA, label); add --anchors path.tsv for data/string xrefs:
python3 research/scripts/run_headless.py --build-id "$GOH_BUILD_ID" \
  export --program call_to_arms.exe --focused \
  --candidates local-work/candidates.tsv \
  --output "local-work/exports/build-$GOH_BUILD_ID/topic"
```

- `export` verifies the imported PE's SHA-256 against the scan, requires exact function entries, and emits body ranges, CFG blocks/edges, disassembly, and decompilation without renaming functions or guessing types.
- `members` searches literal memory displacements across analyzed instructions (requires manual object-type review).
- Anchor TSVs support a third `write` column to collect direct write references only.
- Keep Ghidra projects, raw exports, and temporary scratch files in `local-work/` (never commit full-binary dumps).

## Evidence & runtime-approval rules

1. **Structural:** RTTI, vtables, xrefs, or unwind records establish an address lead only — not behavior or calling convention.
2. **Static behavior reviewed:** Disassembly/decompilation, caller sites, and verified fine-maps support a contract, with ABI/lifetime assumptions stated explicitly. Still **`runtime_allowed: false`**.
3. **Runtime verified:** Controlled live observations on the exact fingerprinted executable confirm ABI, thread affinity, object lifetimes, clean unload, and host/vanilla-client multiplayer agreement (see [runtime-smoke.md](research/compatibility/runtime-smoke.md)).

**Conventions:** All addresses in notes and manifests are **RVAs** unless labeled otherwise (add module base for VAs). Map game and editor independently. Follow the scope rule in [decision-frontier-25492057.md](research/notes/decision-frontier-25492057.md): map inputs to meaning/freshness/validity and outputs to observable contract/ownership/cancellation, parking unrelated internals as one line.

## Key research references

- **Scope & master catalog:** [decision-frontier-25492057.md](research/notes/decision-frontier-25492057.md) · [decision-catalog-25492057.md](research/notes/decision-catalog-25492057.md)
- **Compatibility & runtime gate:** [version-compatibility.md](research/notes/version-compatibility.md) · [runtime-smoke.md](research/compatibility/runtime-smoke.md) · [upstream-audit.md](research/notes/upstream-audit.md)
- **Build `25492057` baseline maps:** [build](research/notes/build-25492057.md) · [fine](research/notes/fine-map-25492057.md) · [flow](research/notes/flow-map-25492057.md) · [tactics](research/notes/tactics-map-25492057.md) · [systems](research/notes/systems-map-25492057.md) · [perception](research/notes/perception-map-25492057.md) · [artifacts](research/artifacts/build-25492057/)
- **Build `26734796` feature contracts:** `research/notes/*-26734796.md` and `research/artifacts/build-26734796/`
