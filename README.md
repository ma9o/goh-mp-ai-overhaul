# Gates of Hell AI Research

Host-only native AI overhaul prototype for **Call to Arms: Gates of Hell — Ostfront** (Steam build `26734796`). All behavior runs through a native DLL with zero `resource/` or `.pak` edits, so vanilla multiplayer clients can join a modded host without installing anything.

> **Status:** Early prototype (not a public release yet). Static binary contracts and offline test suites pass; live single-player and private multiplayer verification is still pending.

## Gameplay changes

All features work in both **Hold Position** (the GoH default) and **Move at Will** unless noted inline below.

### Combat & targeting

- **Target prioritization & shell economy** ([contract](research/notes/target-prioritization-26734796.md))
  - Infantry and vehicles rank visible enemies by weapon/ammo effectiveness, range, threat (including armor threatening nearby allies), penetrability, and clear line of fire, demoting defanged or impenetrable armor.
  - Vehicles with a `>= 37 mm` single-shot cannon try an eligible mounted MG first against an isolated soldier instead of wasting a shell.
  - Multi-cannon tanks (`T-35`, `SMK`, `M3 Lee`/`Grant`, `Churchill Mk I`, `Panzer B2`) pick the highest-penetration cannon against armor and the largest HE blast against infantry clusters; secondary turrets stay active even when an explicit `Attack` locks the main gun.
- **Penetration-aware anti-tank AI** ([contract](research/notes/penetration-at-26734796.md))
  - Aims at the best visible armor volume using the game's projectile and armor profiles, preferring lethal hull/turret/engine/ammo hits over minor components.
  - Keeps a viable chambered shell instead of needlessly reloading, checks all compatible ammo in vehicle inventory, and switches from large-blast HE to clean AP if friendlies sit inside the HE blast radius of an open-top target.
  - Tracks without firing (instead of giving away position) when an unthreatened gun faces impenetrable armor beyond 35 m, or when a towed/MG-less cannon only sees a lone rifleman beyond 40 m.
- **Soft-cover shoot-through** ([contract](research/notes/lof-detour-soft-cover-26734796.md))
  - Kinetic/piercing rounds (AP, APHE, APCR, AT rifle, MG, rifle) fire through penetrable soft cover—wooden walls, fences, trees, and thin masonry for `>= 75 mm`—when penetration cleanly clears every layer without premature fuse detonation.
  - Never shoots through terrain/water, hard cover, non-target vehicle hulls, or near friendly units; vetoes HE, AA, smoke, WP, and HEAT/bazooka/panzerfaust rounds as well as self-damaging HE blasts.
- **Blocked explicit-Attack LOF detours** — *`Move at Will` only* ([contract](research/notes/lof-detour-soft-cover-26734796.md))
  - Replaces blind random sidesteps for infantry and adds repositioning for blocked vehicles (which vanilla leaves idle), picking lateral or corner-clearing moves with clear LOF and low enemy exposure while keeping the explicit `Attack` target active.
  - Stands prone/crouched infantry up in place when standing alone clears the shot; lets casemates and limited-traverse vehicles rotate hulls before moving.

### Infantry survival, tactics & grenades

- **Early flying-grenade dodge** — *overrides `Hold Position`* ([contract](research/notes/grenade-escape-26734796.md))
  - Tracks armed grenades in flight, predicts the landing spot, and wakes nearby soldiers before impact.
  - Ignores outbound throws landing safely away so throwers and nearby allies do not dodge their own grenades.
  - Sprints standing away from both the predicted blast and known enemies instead of crouching into nearby cover.
- **Close-range anti-vehicle grenades & AT rush** — *throws in any stance; sprint rush on `Move at Will` only* ([contract](research/notes/close-grenade-26734796.md))
  - Prioritizes nearby vehicles and crewed guns over rifle targets (ignoring low cover that only blocks flat gunfire), pre-equips when a vehicle closes within throw range `+7 m`, and bypasses vanilla's 5–10 s weapon-switch delay without dropping held rocket launchers, AT rifles, or flamethrowers.
  - Coordinates one thrower per vehicle with failure backoff, leads moving targets, and checks friendly blast clearance (allowing tight `hull + 1.5 m` clearance for AT grenades/Molotovs near closed friendly heavy armor and ignoring frag against frag-immune closed friendly armor).
  - **Molotovs** aim for intact engines on all vehicles (including cars); **frag grenades** target exposed crew and verified open-top vehicles, never closed tanks. On `Move at Will`, up to two carriers sprint along multi-angle approaches (`+15 m`) to throw range while auto-smoke yields to them.
- **Tactical infantry & squad movement** — *`Move at Will` only* ([contract](research/notes/tactical-infantry-26734796.md))
  - Stabilizes advance/hold/fallback posture so squads stop oscillating, moves exposed rifle/SMG soldiers into cover instead of freezing in the open against armor, and coordinates alternating squad bounds (medics and heavy weapons provide overwatch).
  - Ranks cover by threat screening and line of fire (crossing open ground only when no cover fits), tries stance changes or lateral steps when blocked, holds screened cover through distant hits, and drops stale return walks. Selected squads keep priority.
- **Local pursuit & flag-capture finish** — *`Move at Will` only* ([contract](research/notes/tactical-objectives-26734796.md))
  - Idle squads follow recent enemy sightings within 50 m (55 m leash from their anchor) when morale and local odds allow.
  - Once local threats clear, up to four squadmates move in to finish an uncontested capture point within 35 m. Combat, grenade actions, and support jobs always take priority.

### Support & defensive reactions

- **Auto-Smoke** — *any stance (does not move soldiers off `Hold Position`)* ([contract](research/notes/auto-smoke-26734796.md))
  - **Vehicles:** Fire native smoke dischargers when tracks, engine, or any cannon are damaged or after a heavy/penetrating hit under active AT threat (25 s cooldown).
  - **Pinned squads:** When suppressed, losing morale, or taking heavy fire, **one** squadmate throws a smoke screen 6–12 m toward the threat (25 s squad / 20 m group cooldown so squads don't dump all grenades at once).
  - **Medics:** Screen downed allies toward the threat before reviving.
  - **Never uses White Phosphorus (WP):** Strictly limited to the 8 non-lethal smoke grenades and pure-smoke vehicle launchers.
- **Auto-Medic** — *any stance; `Hold Position` medics return to post* ([contract](research/notes/auto-medic-26734796.md))
  - Idle medics claim the nearest reachable downed ally within 36 m (one medic per patient).
  - If out of smoke, fetches one non-WP smoke grenade from a nearby idle ally within 24 m (preferring spare stacks; never taking another medic's last smoke), throws a screen, waits for the plume, and revives—or revives unscreened when no donor is available and no enemy is within 25 m.
  - On `Hold Position`, walks back to its original post after finishing nearby revives, aborting return on a new player order, stance change, or enemies within 10 m of the post.
- **Auto-Resupply** — *any stance; `Hold Position` units return to post* ([contract](research/notes/auto-resupply-26734796.md))
  - Dry infantry, crewed guns, and vehicles (including any empty cannon turret on multi-cannon tanks) move up to 60 m to the nearest safe friendly flag, ammo truck, or crate (25 m enemy threat buffer; up to four units spaced around the supply point) and wait until topped off.
  - On `Hold Position`, walks back to its original post afterward unless overridden, stance-changed, or enemies close within 10 m of the post.

### Armored vehicles & crews

- **Tank obstacle crushing & front-armor reversing** ([contract](research/notes/tank-crusher-26734796.md))
  - Tracked tanks push through fences and crushable cover smoothly in combat instead of detouring around them.
  - Short tactical pullbacks (`<= 45 m`, up to `135°` turns) reverse with frontal armor facing known enemies within 180 m instead of spinning 180°.
- **Tank Auto-Repair** — *any stance; crew re-board automatically* ([contract](research/notes/tank-auto-repair-26734796.md))
  - **Broken hull:** Tracks the crew across delayed ejections, sends crew out in parallel (up to available kits and damaged parts) starting with the hull, and automatically re-boards them once seats unlock.
  - **Turret/cannon/mantlet + broken track:** Sends up to two crew (preferring kit carriers, mechanics, and non-gunners while keeping working cannon gunners seated) to repair guns → turrets → mantlet → remaining parts past vanilla's two-attempt cap before re-boarding.
  - Pauses after manual crew disembarks (25 s) or repairer casualties (8 s), and yields to grenade dodges.
- **Tank hull-rider crew refill** ([contract](research/notes/tank-crew-fill-26734796.md))
  - Moves friendly soldiers riding on a tank's hull directly into freed interior crew slots—even while moving—honoring the 8 s post-death seat lockout and excluding exterior roof MG stations and APC crew seats.
  - Fills `gunner` → `driver` → `charger` → `commander` → secondary cannons → loaders → hull MGs in order, preferring tank crewmen and mechanics first, ordinary infantry next, and medics/AT specialists last.
  - Reserves seats for returning repairers, backs off 10 s after manual disembarks, and won't flip ownership of a crewless allied tank.

## Development & license

- [DEVELOPMENT.md](DEVELOPMENT.md) — repo layout, build/lint/test commands, static analysis, Ghidra, and game-update workflows.
- [AGENTS.md](AGENTS.md) — coding-agent rules and multiplayer non-desync invariants.

Forked from [NorthHead-Digital's script extender](https://github.com/NorthHead-Digital/coa_script_extender). The upstream repository mentions MIT without a standalone license file; this fork preserves that state and assigns no new license to upstream code or game-derived material.
