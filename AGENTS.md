# Agent instructions

Development layout, build/lint/test commands, Ghidra/PE-scan/update-audit workflows, and evidence standards live in [DEVELOPMENT.md](DEVELOPMENT.md). Read it alongside [research/notes/decision-frontier-25492057.md](research/notes/decision-frontier-25492057.md) before starting work.

## Working rules

- **Static mapping scope:** Follow the scope and working rules in `research/notes/decision-frontier-25492057.md` (which supersede `research/notes/static-frontier-25492057.md`). Map the game binary (`call_to_arms.exe`); use the editor only when an editor assertion answers an open item. Work one open item at a time in order; add a new item only when it names the catalog row and blank it fills, and park everything else as one line.
- **Artifact retention:** Keep active research notes, compatibility baselines, and core scripts/tests unless explicitly asked to prune them (historical `25492057` dumps and stale upstream files remain in Git history).
- **Git discipline:** Stage files by path, only those changed for the item. Start commit messages with the item ID. Do not push unless explicitly instructed.
- **Keep docs synchronized:**
  - When gameplay behavior or player-facing rules in `mods/` are added or materially changed, update [README.md](README.md) in plain player-facing terms (no raw RVAs or internal symbols) with a link to the build contract note.
  - When build, lint, test, or static-research workflows change, update [DEVELOPMENT.md](DEVELOPMENT.md).

## Multiplayer AI goals and non-desync constraints

The goal is to improve unit AI for SP and multiplayer without causing lobby **Resource desynchronization** or in-match host/client state desynchronization. Every research contract and hook in `mods/` must obey these invariants (exact per-build RVAs live in shared module headers and `research/artifacts/build-<id>/`; all contracts stay subject to runtime verification):

- **Never modify `resource/` files.** Do not edit `.pak` archives, `resource/set/`, `properties/`, FSMs, `ammo.set`, `select.set`, or breed definitions. Deliver all AI behavior through native runtime hooks gated on exact `call_to_arms.exe` PE SHA-256 fingerprints so vanilla clients need no mod files.
- **Host-only execution; passive on remote clients.** Guard every hook on `!IsRemoteClient()` and verify the live concrete brain vtable (`Actor::eHumanBrain` / `Actor::eVehicleBrain`, never `*Client` stubs).
- **Serial main simulation thread only.** Run AI decisions and mutations only on the main simulation thread during the serial actor/brain update phase (after phase-2 quant workers join and sensor commit runs). Never mutate AI, sensor, or order state from quant worker threads (`!Core::IsQuantWorkerThread()`) or the render thread (`Present`).
- **Output exclusively through native replicating boundaries.** Steer units only via host-only brain policy fields, direct goals, or native orders/setters that emit wire commands under `IsHostEmit()` (e.g., `Order::eMove` with sync flag `+0x3C`, `Order::eMoveToCover`, `SetMovementMode`/`SetMovementType`/`Order::eMovementChange`, strafe disable, `Order::eRotate`, `Order::eWeaponFire`, `Order::eEmit`/`eBoard`/`eRepair`/`eMedic`, `Xchange`, `eBoarder::Move`). Never write raw low-level chassis, cover, position, bone, health, or ammo fields directly.
- **Respect Direct Control and player authority.** Always bail out when the 6-bit brain inhibition counter `(brain+0x39C & 0x3F) != 0` (Direct Control, incapacitation, boarding, inventory exam) or when `chassis+0x18 != 0` / `weaponry+0x70` bit 1 is set. Do not silently overwrite human-player `brain+0x398` (`fire_mode`/`move_mode`) UI toggles (raw stores do not emit wire UI sync). Player-owned units in skirmish and MP have `control == user` (`(brain+0x398 & 3) == 1`) — do **not** exclude them from autonomous AI improvements. Instead, preserve player intent: never hijack an active explicit `Order::eAttack`, respect `fire_mode == hold` (and unalerted `return`), and respect `move_mode == hold` / active user move orders (`brain+0x39C` bit 28) for movement unless handling an emergency defensive reaction (such as dodging a live grenade) or an authorized return-to-post support errand, while keeping autonomous firing and targeting active during Hold Position or movement.
- **Do not perturb engine RNG.** Never draw from `synced_global` or the default thread RNG in custom AI code; use private state/PRNGs only. Never poll native helpers that consume engine RNG as if they were pure read-only queries.
- **Keep static vs. runtime status explicit.** Static analysis never grants runtime approval (`runtime_allowed: false` until live smoke scenarios pass on the exact fingerprinted build). Before live multiplayer use, verify host/client gates across MP modes (host, client, `-no_server`), confirm no separate state-checksum kick, verify order sync flags, and test first in private host-controlled lobbies.

## Static tooling & scoping discipline

See [DEVELOPMENT.md](DEVELOPMENT.md) for exact commands and flags.

- **Reuse existing repo tooling first.** On a game update, run `scan_pe.py` and `check_update.py` against the latest compatibility profile before manual binary diffing.
- **Keep Ghidra work incremental.** Run `run_headless.py import` once per build (`call_to_arms.exe` only unless an editor assertion is needed), then reuse `local-work/ghidra/GoH_<build>.gpr` with `export --focused` and small candidate/anchor TSVs.
- **Avoid giant scratch dumps.** Never dump full-binary `.asm` files or extract old binaries into `/tmp` when existing exports or scanners answer the question; remove any temporary `/tmp` scratch files when done.
- **Stay scoped.** Reuse closed contracts from `research/notes/decision-catalog-25492057.md` and existing maps; trace only blanks needed for the active goal and park unrelated leads as one line.

## C++ and code hygiene

See [DEVELOPMENT.md](DEVELOPMENT.md) for full build, lint, and test invocations.

- **Run the C++ lint/warning gate before finishing any C++ edit:** `./scripts/lint_cpp.sh --fix --werror <touched files>` (or `--werror` for tracked changes). Preserve Windows header ordering (`SortIncludes: Never`) and annotate `printf`-style loggers with `__MINGW_PRINTF_FORMAT` / `format(printf, ...)`.
- **Share verified build constants and authority guards across `mods/`.** Put per-build RVA tables, PE SHA-256 gates, `read<T>()`, and host/inhibition/Direct-Control guards in shared headers rather than copy-pasting them across modules.
- **No machine-specific paths in tracked source.** Derive log paths relative to the loaded module or via environment/config fallback — never hardcode local user paths.
- **Run tests after changes.** Run `python3 -m unittest discover -s research/tests` (and relevant `verify_*.py` scripts) after editing `research/scripts/` or `research/compatibility/`, and run the relevant C++ offline fixtures under `research/tests/` after editing `mods/`.
- **Keep binaries and scratch outputs untracked.** Keep compiled `.dll`/`.exe`/`.o`/`.pdb`, logs, injectors, and Ghidra `.gpr`/`.rep/` databases under `build/` or `local-work/`. Keep `research/artifacts/build-<id>/` limited to compact JSON/TSV contracts, PE scans, and validation summaries.
