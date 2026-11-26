# Gates of Hell AI Research

Host-only native AI overhaul prototype for **Call to Arms: Gates of Hell — Ostfront** (Steam build `26734796`), forked from [NorthHead-Digital's script extender](https://github.com/NorthHead-Digital/coa_script_extender) (`d994e17`).

All AI changes run inside a PE SHA-256-gated native DLL without modifying any `resource/` or `.pak` files, so vanilla multiplayer clients can join a modded host without installing anything.

> **Current status:** Research prototype (`runtime_allowed: false`). Static binary contracts and offline C++ policy/adapter test suites pass; live single-player and private multiplayer smoke testing is still pending. This is not a finished release yet.

For build, lint, test, Ghidra, and game-update workflows, see [DEVELOPMENT.md](DEVELOPMENT.md).

---

## Multiplayer & player-control rules

- **Host-only, vanilla-client compatible:** Active in Singleplayer, Skirmish, Conquest, and on multiplayer hosts; completely passive on remote clients. Uses only native replicating orders and commands, makes zero `resource/` edits, and never perturbs engine RNG.
- **Direct Control & explicit orders always win:** Taking Direct Control immediately suspends custom AI on that unit. Explicit `Attack` targets and manual move orders are never hijacked; autonomous firing and target prioritization stay active while holding position or moving.
- **Fire modes respected:** `Hold Fire` suppresses autonomous firing overrides, target prioritization, auto-smoke, and auto-medic (auto-resupply still refills dry units). `Return Fire` stays passive until the unit is alerted.
- **Stance behavior (`Hold Position` vs. `Move at Will`):** Player units default to `Hold Position` in GoH, so autonomous movement obeys strict stance rules:

| Stance | Autonomous behaviors active |
| --- | --- |
| **Both `Hold Position` & `Move at Will`** | Emergency flying-grenade dodge, soft-cover shoot-through, autonomous target prioritization & MG-first shell economy, penetration-aware AT aiming & ammo choice, close-range anti-vehicle grenades (pre-equips when a vehicle closes within throw range `+7 m`), defensive auto-smoke (vehicles & pinned squads), tank obstacle crushing & front-armor reverse pathing, tank auto-repair (crew re-board automatically), exterior tank-rider crew refill. |
| **`Hold Position` errand (returns to post)** | **Auto-Medic** (revives downed allies within 36 m) and **Auto-Resupply** (dry units refill within 60 m and top off). Both walk back to their original post afterward, aborting return immediately on player override, stance change, or if an enemy closes within 10 m of the post. |
| **`Move at Will` only** | Blocked explicit-`Attack` LOF repositioning detours, tactical infantry posture/cover/alternating squad bounds, 1–2 carrier AT-grenade rushes (`+15 m`), local pursuit of recent contacts (50 m / 55 m anchor leash), and finishing nearby uncontested flag captures (35 m, up to 4 squadmates). Selected units keep priority over autonomous tactical movement. |

---

## Gameplay changes

### Combat & targeting

- **Autonomous target prioritization & shell economy** ([contract](research/notes/target-prioritization-26734796.md))
  - Infantry and vehicles rank visible enemies by weapon/ammo effectiveness, range, threat (including armor threatening nearby allies), penetrability, and current aim, demoting defanged or impenetrable armor and running up to four line-of-fire checks per tick to promote clear shots.
  - Against an isolated soldier, vehicles with a single-shot cannon of `>= 37 mm` give an eligible mounted MG first priority and mask shell cannons once covered.
  - Multi-cannon tanks (`T-35`, `SMK`, `M3 Lee`/`Grant`, `Churchill Mk I`, `Panzer B2`) pick the highest-penetration cannon against armor and the largest HE blast against infantry clusters (leaving MGs free), and keep unreserved secondary turrets active even when an explicit `Attack` locks the main gun.
- **Penetration-aware anti-tank AI** ([contract](research/notes/penetration-at-26734796.md))
  - Ranks visible vehicle armor volumes using the game's projectile and first-contact armor profiles, preferring lethal body/turret/engine/ammo hits over incidental component hits.
  - Keeps a viable chambered shell instead of needlessly reloading, switches from large-blast HE to clean AP when friendlies lie inside the HE blast radius of an open-top target, considers compatible inventory beyond the four cached ammo slots, and uses aim-only tracking instead of giving away position when firing at impenetrable armor beyond 35 m or a lone rifleman beyond 40 m with an unthreatened towed/MG-less gun.
- **Soft-cover shoot-through** ([contract](research/notes/lof-detour-soft-cover-26734796.md))
  - Allows kinetic/piercing rounds (AP, APHE, APCR, AT rifle, MG, rifle) to shoot through penetrable soft cover—wooden walls, fences, trees, and thin masonry for `>= 75 mm`—when penetration cleanly clears every obstacle layer without premature fuse detonation.
  - Strictly vetoes HE, AA, smoke, WP, and HEAT/bazooka/panzerfaust rounds, terrain/water, friendly units anywhere near the trajectory, non-target vehicle hulls, hard cover, and self-damaging HE blasts.
- **Smarter explicit-Attack LOF detours** ([contract](research/notes/lof-detour-soft-cover-26734796.md))
  - Replaces blind random repositioning when an explicit `Attack` is obstructed (and adds repositioning for blocked vehicles, which vanilla leaves idle). Evaluates structured lateral and corner-clearing steps scored by clear line of fire and enemy exposure while keeping the explicit Attack target active.
  - Stands prone or crouched infantry up in place when standing alone clears the shot, and lets casemates/limited-traverse vehicles rotate hulls before repositioning. Respects `Hold Position`.

### Infantry survival, tactics & grenades

- **Early flying-grenade dodge** ([contract](research/notes/grenade-escape-26734796.md))
  - Tracks live grenades in flight, predicts the landing point, and wakes nearby soldiers before impact.
  - Ignores outbound throws predicted to land safely away so throwers and nearby allies do not dodge their own grenades.
  - Sprints standing away from both the predicted blast and known enemy threats instead of ducking into nearby cover.
- **Close-range anti-vehicle grenades & AT rush** ([contract](research/notes/close-grenade-26734796.md))
  - Prioritizes nearby vehicles and crewed guns over ordinary rifle targets (ignoring flat-trajectory bullet obstruction behind low cover) and bypasses the vanilla 5–10 s weapon-switch lockout without dropping held rocket launchers, AT rifles, or flamethrowers.
  - Coordinates single-thrower claims with failure backoff so a blocked soldier yields to squadmates, predicts moving-target lead, and checks self/friendly blast clearance (exempting frag-immune closed friendly armor and allowing tight `hull + 1.5 m` clearance for AT grenades/Molotovs near closed friendly heavy armor).
  - **Molotovs** target intact engine compartments across all vehicle classes (including cars); **frag grenades** target exposed crew and verified open-top vehicles while strictly rejecting closed tanks. On `Move at Will`, up to two carriers sprint along multi-angle approaches (`+15 m`) to throw range while auto-smoke yields to active rushers.
- **Tactical infantry & squad Move at Will** ([contract](research/notes/tactical-infantry-26734796.md))
  - Stabilizes advance/hold/fallback posture with hysteresis so squads stop oscillating, moves exposed rifle/SMG soldiers out of the open when facing armor, and coordinates alternating squad bounds (with medics and heavy weapons providing overwatch).
  - Ranks cover by threat screening and line of fire (crossing open ground only when cover is unavailable), tries stance changes or lateral steps when blocked, holds screened cover through distant hits, and suppresses stale AI return walks.
- **Move-at-Will local pursuit & capture** ([contract](research/notes/tactical-objectives-26734796.md))
  - Idle `Move at Will` infantry pursue recent enemy sightings within 50 m (staying within a 55 m leash of their anchor) when morale and local odds allow.
  - Once local threats clear, up to four squadmates move to finish an uncontested capture point within 35 m. Player orders, visible combat, grenade actions, and support jobs always take priority.

### Support & defensive reactions

- **Auto-Smoke (vehicles, pinned squads & medics)** ([contract](research/notes/auto-smoke-26734796.md))
  - **Vehicles:** Pop native smoke dischargers when tracks, engine, or any cannon are damaged or after a heavy/penetrating hit under active enemy AT threat (25 s cooldown).
  - **Pinned infantry squads:** When suppressed, losing morale, or taking heavy fire from vehicles, MGs, or enemy squads, **one** squadmate throws a smoke screen 6–12 m toward the threat (25 s per-squad / 20 m group cooldown so squads never dump all grenades at once).
  - **Medics:** Screen downed allies toward the active threat before reviving.
  - **Zero White Phosphorus (WP):** Strictly whitelists only the 8 non-lethal smoke grenades and pure-smoke vehicle launchers—WP grenades and WP mortars/launchers are never triggered automatically.
- **Auto-Medic** ([contract](research/notes/auto-medic-26734796.md))
  - Available medics claim the nearest reachable downed ally within 36 m (one medic per patient).
  - If out of smoke, the medic fetches one non-WP smoke grenade from an eligible idle ally within 24 m (preferring spare stacks and never taking another medic's last smoke), throws a screen, waits for the smoke plume to form, and revives—or revives unscreened when no donor is available and no enemy threat is within 25 m.
  - `Hold Position` medics chain nearby revives and return to their original post afterward.
- **Auto-Resupply** ([contract](research/notes/auto-resupply-26734796.md))
  - Dry infantry, crewed guns, and vehicles (including any empty cannon turret on multi-cannon tanks) move up to 60 m to the nearest safe friendly capture flag, ammo truck, or supply crate (25 m enemy threat buffer; up to four inbound units spaced around the supply radius).
  - `Hold Position` units wait until topped off and then return to their post.

### Armored vehicles & crews

- **Tank BattleCrusher & front-armor reverse pathing** ([contract](research/notes/tank-crusher-26734796.md))
  - Enables combat obstacle crushing and dampens excessive pathfinder obstacle penalties for tracked tanks so they push through fences and crushable cover smoothly—leaving crush physics, uncrushables, fire, infantry, and water unchanged.
  - Biases short tactical pullbacks (`<= 45 m`, up to `135°` turns) to reverse with frontal armor facing known enemies within 180 m instead of exposing rear armor by spinning 180°.
- **Tank Auto-Repair** ([contract](research/notes/tank-auto-repair-26734796.md))
  - **Broken hull:** Tracks the tank's crew roster across delayed ejections and dispatches crew in parallel (up to available kits and damaged parts) starting with the hull, then automatically re-boards idle crew once seats unlock.
  - **Turret/cannon/mantlet + broken track:** Dispatches up to two crew (preferring kit carriers, mechanics, and non-gunners while keeping working cannon gunners seated) to repair guns → turrets → mantlet → remaining parts in parallel, chaining past vanilla's two-attempt repair cap before returning kits and re-boarding.
  - Respects manual crew disembarks (25 s backoff), repairer casualties (8 s backoff), and grenade dodges.
- **Tank exterior-passenger crew refill** ([contract](research/notes/tank-crew-fill-26734796.md))
  - Moves friendly hull-top tank riders directly into freed interior crew slots—even while the tank is moving—while respecting the 8 s post-death seat lockout and excluding exterior roof MG stations and APC crew seats.
  - Fills critical roles in order (`gunner` → `driver` → `charger` → `commander` → secondary cannons → loaders → hull MGs), prioritizing tank crewmen and mechanics first, ordinary infantry next, and medics/AT specialists last.
  - Coordinates with active repair and boarding orders so returning repairers never lose their seat, backs off 10 s after manual crew disembarks, and guards against accidental allied ownership takeover on crewless tanks.

---

## Documentation & license

- [DEVELOPMENT.md](DEVELOPMENT.md) — repository layout, build/lint/test commands, static analysis, Ghidra, and game-update workflows.
- [AGENTS.md](AGENTS.md) — coding-agent instructions and multiplayer non-desync invariants.

The upstream repository mentions MIT but contains no standalone license file; this fork preserves that state and does not assign a new license to upstream code or game-derived material.
