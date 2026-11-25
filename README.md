# Gates of Hell AI research

Static analysis of **Call to Arms: Gates of Hell — Ostfront**, starting from
[NorthHead-Digital's script extender](https://github.com/NorthHead-Digital/coa_script_extender)
at `d994e1750de1c015ed2557250ab461ece73cd721`.

The goal is to recover enough of the native actor/brain/order interfaces to build
our own tactical decision system: cover, attack/retreat, target selection, and
weapon behavior. The upstream loader and SDK are retained as reference material.
**This is a research fork, not an updated or working extender release.**

## AI changes (running total)

Host-only runtime prototype (`mods/goh_ai_mod.cpp`, Steam build `26734796`, zero `resource/` edits, PE SHA-256 gated):

1. **Early flying-grenade dodge & directional sprint** (`mods/goh_ai_mod.cpp`, [contracts](research/notes/grenade-escape-26734796.md))
   - Tracks armed grenades in flight (`eGrenadeSimulator::Tick` / `~dtor`), predicts landing point with terrain refinement, wakes nearby soldiers immediately, and triggers `Order::eAvoidGrenade` before impact.
   - Ignores outbound airborne/away-rolling grenades predicted to land outside the soldier's danger radius so throwers and nearby allies do not dodge their own throws.
   - Bypasses the vanilla cover-hide trap on flying grenades and sprints standing (`SetMovementType(0)` + `SetMovementMode(2)`) away from both the predicted landing spot and known enemy threats via native replicating `Order::eMoveTask`.
2. **General soft-cover shoot-through LOF override (autonomous & explicit fire)** (`mods/goh_ai_mod.cpp`, [contracts](research/notes/lof-detour-soft-cover-26734796.md))
   - Extends live `TargetAccessor::Try` (`0x835790`) bullet trajectory previews (`0x400000`) for both autonomous firing and explicit `Order::eAttack` with zero extra RNG draws (skipping speculative detour probes and respecting Direct Control / Hold Fire).
   - Overrides trajectory-blocked verdicts (`5 → 3`) for **piercing/kinetic rounds only** (AP, APHE, APCR, AT-rifle, MG, rifle — explicitly vetoing HE/AA/smoke/WP via `possibleDamageRadius`/`detonationThickness` and HEAT/bazooka/panzerfaust via `cumulative > 0`) when only soft/penetrable cover (wooden walls, fences, trees; thin masonry for `>= 75 mm`) lies in front of the target and kinetic penetration cleanly clears every layer with residual energy remaining.
   - Strictly vetoes terrain/water, friendly actors anywhere along/near the trajectory, non-target vehicle hulls, hard/immune materials, and HE shells whose true max blastwave radius (`PossibleDamageRadius` + all effect descriptors) reaches the shooter.
3. **Smarter explicit-Attack LOF detour for infantry and vehicles** (`mods/goh_ai_mod.cpp`, [contracts](research/notes/lof-detour-soft-cover-26734796.md))
   - Replaces blind random sampling for blocked explicit `Order::eAttack` orders (and adds repositioning for blocked vehicles, which vanilla otherwise leaves idle) using structured lateral and blocking-corner candidates scored by RNG-free segment LOF (`0xA409C0`/`0xA40A20`/`0xA40AC0`) and committed enemy-threat exposure.
   - Stands prone/crouched infantry up in place when standing alone clears LOF; lets limited-traverse/casemate vehicles rotate hulls (`eLookAt`) first when traverse-limited.
   - Issues parent-owned replicating `Order::eMoveTask` children so the unit keeps its explicit Attack target, while respecting Direct Control, inhibition counters, and Hold Position (`move_mode == 1`).
4. **Autonomous target prioritization & MG-first shell economy** (`mods/target_prioritization_module.hpp`, [contract](research/notes/target-prioritization-26734796.md))
   - Infantry and vehicles weigh visible enemies against their available weapons and ammo, range, threat, and current aim. Up to four line-of-fire checks per actor tick can promote a clear target. Explicit Attack orders, Direct Control, and Hold Fire retain authority.
   - Against an isolated soldier, a vehicle with a shell gun of 50 mm or larger gives an eligible mounted MG the first attempt. If the MG cannot engage, native weapon and ammo selection handles the fallback.
5. **Auto-Smoke for vehicles, pinned infantry squads, and medics** (`mods/auto_smoke_module.hpp`, [contracts](research/notes/auto-smoke-26734796.md))
   - **Vehicles:** Triggers native replicating smoke dischargers (`eControl` action `0x11` / `deploy_smoke`) when a vehicle's tracks, engine, or gun is damaged or it takes a penetrating/heavy hull hit under active enemy AT threat (25 s per-vehicle cooldown; respects Direct Control and Hold Fire).
   - **Pinned infantry squads:** When a squad is suppressed, losing morale (`< 50` or recent morale drop/injury), or taking fire from an enemy vehicle, MG, or enemy squad, **one** squad member carrying a smoke grenade throws a screen 6–12 m between the squad and the primary threat (25 s per-squad / 20 m group cooldown so squads don't dump all grenades at once).
   - **Medics / revives under fire:** Screens downed allies toward the active threat while approaching/reviving.
   - **Strict White Phosphorus (WP) exclusion:** Positively whitelists only the 8 audited non-lethal smoke grenade keys (`anm8_smoke`, `m24_smoke`, `m43_smoke`, `rdg1_smoke`, `svkkr_pm`, `nbk39`, `nbks`, `no8_smoke`) and audited pure-smoke vehicle launcher programs (blocking WP grenades like `m15_wp`/`m42_wp`/`no77_wp` and WP vehicle launchers like `2in_wp` / M24 Chaffee).
6. **Tank BattleCrusher & front-armor reverse pathing** (`mods/tank_crusher_module.hpp`, [contracts](research/notes/tank-crusher-26734796.md))
   - Activates combat obstacle crushing (`ctx+0xE8 |= 0x10`) and dampens excessive per-step grid-solver obstacle penalties for light, medium, heavy, and amphibious tracked tanks via a pure read-only host hook on `Pather::object_policy` (`0x8C3650`)—leaving `can_crush` physics (`0x76AC64`), uncrushables (`>= 100000`), `fire`, `human`, and `marine` untouched for full vanilla-client parity.
   - Biases `eChassisTrack::EstimatePathTime` (`0x86F430`) on tactical pullbacks (`<= 45 m`, up to `135°` corner turns) so tanks reverse keeping frontal armor toward fresh known hostiles within 180 m instead of spinning 180° (supporting both AI and player right-click / Hold Position moves while bailing on Direct Control / inhibition).

## Start here

- [Version compatibility](research/notes/version-compatibility.md): run a static
  update audit, see changed mappings and coverage gaps, and prepare a new baseline.
- [Upstream audit](research/notes/upstream-audit.md): what is implemented, missing,
  or unverified in the inherited SDK.
- [Current build findings](research/notes/build-25492057.md): evidence from the
  installed game and GEM editor, with addresses separated by executable.
- [Fine-grained brain map](research/notes/fine-map-25492057.md): field masks,
  enum values, small predicates, setter side effects, and inlined state rules.
- [Input-to-order trace](research/notes/flow-map-25492057.md): subject-score
  formulas, the morale connection, and state-dependent movement/cover orders.
- [Combat estimates and cover selection](research/notes/tactics-map-25492057.md):
  Human/Tank coefficients, relationship flags, cover weights and rejection rules,
  and the randomized choice among ranked candidates.
- [Scheduling, sensors and cover sources](research/notes/systems-map-25492057.md):
  entity-owned work queues, scheduler intervals, observation structure and
  geometric/terrain candidate generation.
- [Perception, memory and entity inputs](research/notes/perception-map-25492057.md):
  vision predicates, last-known positions, ally information sharing, relationship
  values, component identities, and cover occupancy.
- [Active static frontier](research/notes/static-frontier-25492057.md): remaining
  AI/entity questions and the criteria for exhausting productive static work.
- [Build artifacts](research/artifacts/build-25492057): fingerprints, RTTI,
  vtables, string anchors, and candidate methods.
- [Original upstream README](README.upstream.md): historical build and loader
  instructions. Its compatibility and verification claims have not been
  reproduced here.

## Reproduce the structural scan

Python 3.10+; no third-party Python dependencies. Point `GOH_BINARIES` at your
own installed game's `binaries/x64` directory. Use the Steam build ID from your
local `appmanifest_400750.acf`.

```sh
export GOH_BINARIES='/path/to/Call to Arms - Gates of Hell/binaries/x64'
python3 research/scripts/scan_pe.py --binary "$GOH_BINARIES/call_to_arms.exe" --build-id 25492057 --output-dir research/artifacts/build-25492057
python3 research/scripts/scan_pe.py --binary "$GOH_BINARIES/call_to_arms_ed.exe" --build-id 25492057 --output-dir research/artifacts/build-25492057
python3 -m unittest discover -s research/tests -v
```

The scanner reads files without executing them. It checks PE section boundaries,
x64 unwind ranges, relative MSVC RTTI locators and their self-references, class
hierarchies, and vtable code pointers. A structural match is a lead, not proof of
a function's behavior or calling convention.

## Ghidra workflow

For game updates, the [compatibility audit](research/notes/version-compatibility.md)
compares new binaries with a versioned profile and reports changed, relocated,
ambiguous, unresolved, and unchecked mappings. It includes targeted field/mask
checks, resource hashes, capability dependencies, and candidate TSVs for review.
See that guide for setup, the one-command audit, exit codes, and coverage limits.
A static pass is not runtime hook approval.

Test environment: Ghidra 12.1.4, OpenJDK 21, macOS arm64. The wrapper defaults to
the local Homebrew installations; use `--ghidra-home` and `--java-home`, or
`GHIDRA_HOME` and `JAVA_HOME`, on another machine.

```sh
python3 research/scripts/run_headless.py --build-id 25492057 import --binary "$GOH_BINARIES/call_to_arms_ed.exe" --binary "$GOH_BINARIES/call_to_arms.exe"
python3 research/scripts/run_headless.py --build-id 25492057 export --program call_to_arms_ed.exe
python3 research/scripts/run_headless.py --build-id 25492057 export --program call_to_arms.exe
```

Imports create `local-work/ghidra/GoH_25492057.gpr`. Open this project in Ghidra
after headless work finishes. Ghidra rejects project paths containing hidden
directory elements, so the project is deliberately outside `.work/`.

Exports check the imported executable's SHA-256 against the survey, require
exact function entries for candidate RVAs, and collect references, calls,
assembly, and decompilation. Schema-2 exports also include body ranges, basic
blocks, and outgoing flow edges so that inlined decisions can be mapped.
They do not rename functions or apply guessed types. Additional RVAs can be
supplied in a two-column TSV (`RVA`, `label`) via
`export --candidates path.tsv --output local-work/exports/followup`.
An optional `--anchors path.tsv` follows references to additional data/string
RVAs. The `members` command searches literal memory displacements across all
analyzed instructions; matches need manual pointer/type review. See the
[input-to-order workflow](research/notes/flow-map-25492057.md#reproduce-and-verify).
Use `--focused` to omit the original survey anchors. A third anchor TSV column
`write` selects direct write references only. Imported calls are represented
as external targets with names, not image RVAs.

`catalog_ai_types.py --binary "$GOH_BINARIES/call_to_arms.exe" --build-id 25492057
--output research/artifacts/build-25492057/call_to_arms.ai-types.json` inventories
selected native AI/entity RTTI roots, base layouts and vtable slots. Repeat for
the editor. The present catalogs contain 392 game and 391 editor roots. These
are structural worklists; neither the filter nor vtable scanning establishes
complete AI coverage or callable interfaces.

The checked-in `call_to_arms.followup.tsv` and `call_to_arms_ed.followup.tsv`
add the flag-clearing wrapper and actor dispatch path identified during the
first review. For example:

```sh
python3 research/scripts/run_headless.py --build-id 25492057 export --program call_to_arms.exe --candidates research/artifacts/build-25492057/call_to_arms.followup.tsv --output local-work/exports/build-25492057/game-followup
```

The `*.fine-pass.tsv` files extend the investigation into predicates, mode
parsers, target selection, and state transitions. The
[fine-map reproduction commands](research/notes/fine-map-25492057.md#reproduction-and-checks)
also run `verify_fine_map.py`, which checks binary hashes, enum tables, constants,
and instruction evidence locations. Maps with `resource_members` also require
`--resources-dir` and verify the named archive members by size and SHA-256.
Passing these checks does not prove the
analyst's semantic interpretations or a callable ABI.

Game binaries, Ghidra databases, logs, and decompiled bodies stay untracked in
`local-work/`. Commit tools, small address/evidence manifests, and analyst notes.
The importer does not overwrite existing programs; use a new build ID after a
game update and regenerate the survey before comparing it.

## Evidence rules

1. **Structural:** RTTI, references, vtables, or unwind records identify an
   address relationship. This does not identify the method's purpose.
2. **Static behavior reviewed:** disassembly/decompilation and callers support
   a proposed role. State the evidence and unresolved ABI/lifetime details.
3. **Runtime verified:** observations on the exact fingerprinted executable
   confirm the call, object, thread, and lifetime assumptions. Nothing in this
   research has this status yet.

All addresses in new reports are **RVAs** unless explicitly labeled otherwise.
Add the executable's module base to obtain a virtual address. Game and editor
addresses must be mapped independently; a matching vtable slot is a comparison
anchor, not a compatibility guarantee. Do not transfer research candidates into
the loader's hook table before their signatures and lifecycle are established.

Map the smallest behavior supported by evidence: individual bit fields,
predicates, score calculations, state transitions, and side effects, including
operations inlined into larger functions. Record unknown meanings explicitly;
pair game/editor behavior even when compilation changes function boundaries.

The upstream repository mentions MIT but contains no standalone license file;
this fork preserves that state and does not assign a new license to upstream
code or game-derived material.
