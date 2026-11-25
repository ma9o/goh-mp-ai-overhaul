# Agent instructions

Research plan and scope: `research/notes/decision-frontier-25492057.md`. Read
it before starting any work. Its scope rule and working rules govern all
static mapping here. They take precedence over
`research/notes/static-frontier-25492057.md` (now a historical index) and over
the README's game/editor pairing rule: map the game binary, and use the editor
only when one of its assertions answers an open item.

- Work one open item at a time, in order. Add a new item only when it names the
  catalog row and blank it fills; park everything else as one line.
- Keep active research notes, compatibility baselines, and core scripts/tests
  unless explicitly asked to prune them (historical `25492057` dumps and stale
  upstream files were pruned from the working tree and remain in Git history).
- Static analysis only. Do not launch or modify the game or the editor.
- Stage files by path, only those you changed for the item. Start commit
  messages with the item ID. Do not push.

## Multiplayer AI goals and non-desync constraints

The repository goal is to improve unit AI for multiplayer without causing lobby
**Resource desynchronization** or in-match host/client state desynchronization.
All future research and prototype/hook designs must obey these constraints
(build 25492057 static contracts; still subject to runtime verification):

- **Never modify `resource/` files.** Do not edit `.pak` archives,
  `resource/set/`, `properties/`, FSMs, `ammo.set`, `select.set`, or breed
  definitions to ship AI changes. Deliver all AI behavior through native runtime
  hooks (DLL extender) gated on exact PE SHA-256 fingerprints
  (`compatibility-profile.json`), so vanilla clients can join without installing
  any mod files.
- **Host-only execution; stay passive on remote clients.** GEM2 unit AI is
  host-authoritative: when `0xC0B630()` (remote client gate) is true, the game
  spawns stub `*Client` brains/sensors (`eHumanBrainClient` with `state.client`
  and empty slot 23 `0xA7720`, `eSensorClient`) and skips sensor look-around and
  morale processing. Guard every custom AI hook on `!0xC0B630()` and verify the
  live concrete brain type (`Actor::eHumanBrain` / `Actor::eVehicleBrain`, never
  `*Client`).
- **Serial main simulation thread only.** Run AI decisions and mutations only on
  the main simulation thread during the actor/brain update phase (e.g., around
  `Actor::eHumanBrain` slot 35 `0x941D00` / `0x9035C0` in `eActor` slot 23
  `0x7EFD10`, after phase-2 `eSceneQuantTask` workers join and sensor commit
  `0x873B00` runs). Never mutate AI, sensor, or order state from quant worker
  threads (`0x0A6740` / `!Core::IsQuantWorkerThread()`) or the render thread
  (`Present`).
- **Output exclusively through native replicating boundaries.** Steer units only
  via host-only brain policy fields (`+0x52C/+0x530`, `+0x520..+0x528`), direct
  goals (`0x902FB0`), or native orders/setters that emit wire commands via
  `0x14C010` under `0xC0B5D0()` (e.g., `Order::eMove` with sync flag `+0x3C` ->
  `eOrderMoveCommand`, `Order::eMoveToCover` / `0x898AD0` / `0x898C50` ->
  `eCoverSectorCommand`, `SetMovementMode` `0x848490` / `SetMovementType`
  `0x848710` / `Order::eMovementChange` -> `eSetMovementModeCommand` /
  `eSetMovementTypeCommand` / `eChassisFootSetModeCommand`, strafe disable ->
  `eSetStrafeDisabledCommand`, `Order::eRotate`, `Order::eWeaponFire`). Never
  write raw low-level chassis (`+0x460`/`+0x30`), cover, position, bone, health,
  or ammo fields directly.
- **Respect Direct Control and player authority.** Always bail out when the
  6-bit brain inhibition counter `(brain+0x39C & 0x3F) != 0` (Direct Control
  `0xB7EDC0`, incapacitation, boarding, inventory exam) or when `chassis+0x18 != 0`
  (`0x83FB90`) / `weaponry+0x70` bit 1 is set (note `0x941D00` still runs before
  `0x903B10` checks that counter). Do not silently overwrite human-player
  `brain+0x398` (`fire_mode`/`move_mode`) UI toggles (`0x903140` and raw masked
  stores do not emit wire UI sync commands), and respect `control == user`,
  `hold` modes, and active user orders (`brain+0x39C` bit 28) unless explicitly
  handling an emergency defensive reaction (such as dodging a live grenade).
- **Do not perturb engine RNG.** Never draw from `synced_global` (`0x554B50` /
  `0x6FBD60`, re-seeded per MP frame packet in `0x53ACA0`) or the default thread
  RNG (`0x5549F0`) in custom AI code; use a private PRNG. Never poll native
  helpers that consume RNG draws as if they were pure read-only queries.
- **Keep static vs. runtime status explicit.** Static analysis never grants
  runtime approval (`runtime_allowed: false` until smoke scenarios pass on the
  exact fingerprinted build). Before live multiplayer use, verify
  `0xC0B630`/`0xC0B5D0` across MP modes (host, client, `-no_server`), confirm no
  separate state-checksum kick in `0x53ACA0`/network packets, verify order sync
  flags, and test first in private host-controlled lobbies.

## Static tooling and scoping discipline

- **Reuse existing repo tooling before building custom scripts.** On a game
  update, run `research/scripts/scan_pe.py` and
  `research/scripts/check_update.py` against the latest baseline
  `compatibility-profile.json` first to relocate/audit existing contracts before
  manual binary diffing.
- **Keep Ghidra work focused and incremental.** Run `run_headless.py import`
  only once per new build (game binary `call_to_arms.exe` only, unless an editor
  assertion answers an open blank), then reuse `local-work/ghidra/GoH_<build>.gpr`
  with `run_headless.py export --focused` and small targeted candidate/anchor
  TSVs.
- **Avoid giant scratch dumps and throwaway indexers.** Do not dump full-binary
  `.asm` disassemblies or extract old binaries into `/tmp` when existing
  exports, `scan_pe.py`, `check_update.py`, or focused Ghidra exports answer the
  question. Remove any temporary `/tmp` scratch files when done.
- **Stay tightly scoped to the requested task.** Reuse already-closed contracts
  from `research/notes/decision-catalog-25492057.md` and existing maps instead
  of re-deriving them; trace only the blanks needed for the active goal and park
  unrelated leads as one line.
