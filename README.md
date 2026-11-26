# Gates of Hell: MP AI Overhaul

Host-only native AI overhaul for **Call to Arms: Gates of Hell - Ostfront** (Steam build `26734796`). Nothing in `resource/` or `.pak` files is touched, so vanilla multiplayer clients can join a modded host without installing the mod. Also works in singleplayer, skirmish, and Conquest.

Work in progress: no packaged release yet, and live multiplayer testing is still ongoing.

## Gameplay changes

Everything below works in both **Hold Position** (the GoH default) and **Move at Will**, except where noted.

### Combat & targeting

- **[Target prioritization & shell economy](research/notes/target-prioritization-26734796.md)**
  - Ranks visible enemies by weapon/ammo effectiveness, range, threat (including armor targeting nearby allies), penetrability, and clear line of fire, demoting defanged or impenetrable vehicles.
  - Vehicles with a `>= 37 mm` cannon use an eligible mounted MG first against a lone soldier instead of wasting a shell.
  - Multi-cannon tanks (`T-35`, `SMK`, `M3 Lee`/`Grant`, `Churchill Mk I`, `Panzer B2`) pick the highest-penetration gun against armor and the largest HE blast against infantry groups; secondary turrets keep engaging while an explicit `Attack` locks the main gun.
- **[Penetration-aware anti-tank AI](research/notes/penetration-at-26734796.md)**
  - Aims for lethal armor zones (hull, turret, engine, ammo) using the game's penetration profiles instead of minor components.
  - Keeps a viable chambered shell instead of needlessly reloading, checks all compatible ammo in inventory, and switches from large-blast HE to AP if friendlies are inside the HE blast radius of an open-top target.
  - Holds fire (aim-only tracking) when an unthreatened gun faces impenetrable armor beyond 35 m, or when a towed/MG-less cannon only sees a lone rifleman beyond 40 m.
- **[Soft-cover shoot-through](research/notes/lof-detour-soft-cover-26734796.md)**
  - Piercing rounds (AP, APHE, APCR, AT rifles, MGs, rifles) shoot through wooden walls, fences, trees, and thin masonry (`>= 75 mm`) when penetration clears every layer without detonating early.
  - Never fires through terrain, water, hard cover, other vehicles, or near friendlies; excludes HE, HEAT, AA, smoke, WP, and self-damaging HE blasts.
- **[Blocked explicit-Attack LOF detours](research/notes/lof-detour-soft-cover-26734796.md)** *(Move at Will only)*
  - Replaces blind random sidesteps for infantry and adds repositioning for blocked vehicles (which vanilla leaves idle), picking lateral or corner-clearing moves with clear LOF and low exposure while keeping the `Attack` target.
  - Stands crouched/prone infantry up in place when standing alone clears the shot; lets casemates and limited-traverse hulls rotate before moving.

### Infantry survival, tactics & grenades

- **[Early flying-grenade dodge](research/notes/grenade-escape-26734796.md)** *(overrides Hold Position)*
  - Tracks armed grenades in flight, predicts where they will land, and wakes nearby soldiers before impact while ignoring outbound throws landing safely away.
  - Sprints standing away from both the blast and known enemies instead of crouching into nearby cover.
- **[Close-range anti-vehicle grenades & AT rush](research/notes/close-grenade-26734796.md)** *(throws in any stance; rush on Move at Will only)*
  - Prioritizes nearby vehicles and crewed guns over rifle targets, pre-equips when a vehicle closes within throw range `+7 m`, and bypasses vanilla's 5–10 s weapon-switch delay without dropping held AT launchers, AT rifles, or flamethrowers.
  - Coordinates one thrower per target with failure backoff, leads moving vehicles, and checks friendly blast clearance (without blocking throws that can't harm closed friendly armor).
  - Molotovs aim for intact engines on all vehicles (including cars); frag grenades target exposed crew and open-top vehicles, never closed tanks. On `Move at Will`, up to two carriers sprint along multi-angle paths (`+15 m`) into throw range while auto-smoke yields to them.
- **[Tactical infantry & squad movement](research/notes/tactical-infantry-26734796.md)** *(Move at Will only)*
  - Stabilizes advance/hold/fallback posture so squads stop oscillating, pulls exposed rifle/SMG soldiers out of the open against armor, and coordinates alternating squad bounds (medics and heavy weapons cover from overwatch).
  - Ranks cover by threat screening and line of fire (crossing open ground only when no cover fits), tries stance changes or sidesteps when blocked, holds screened cover through distant hits, and stops stale return walks. Selected squads keep priority.
- **[Local pursuit & flag-capture finish](research/notes/tactical-objectives-26734796.md)** *(Move at Will only)*
  - Idle squads follow recent enemy sightings within 50 m (55 m leash from their anchor) when morale and local strength allow.
  - Once local threats clear, up to four squadmates finish an uncontested capture point within 35 m. Combat, grenades, and support jobs always take priority.

### Support & defensive reactions

- **[Auto-Smoke](research/notes/auto-smoke-26734796.md)** *(any stance; soldiers stay on Hold Position)*
  - **Vehicles:** Pop smoke dischargers when tracks, engine, or any cannon are damaged or after a heavy/penetrating hit under AT threat (25 s cooldown).
  - **Pinned squads:** One squadmate throws smoke 6–12 m toward the threat when suppressed, losing morale, or taking heavy fire (25 s squad / 20 m group cooldown).
  - **Medics:** Screen downed allies toward the threat before reviving.
  - Never triggers White Phosphorus (WP) grenades or WP vehicle launchers.
- **[Auto-Medic](research/notes/auto-medic-26734796.md)** *(any stance; Hold Position returns to post)*
  - Idle medics claim the nearest reachable downed ally within 36 m (one medic per patient).
  - If out of smoke, fetches one smoke grenade from an idle ally within 24 m (preferring spare stacks; never taking another medic's last smoke), throws a screen, waits for the plume, and revives — or revives unscreened if no donor is nearby and no enemy is within 25 m.
  - On `Hold Position`, walks back to post afterward unless interrupted or enemies close within 10 m.
- **[Auto-Resupply](research/notes/auto-resupply-26734796.md)** *(any stance; Hold Position returns to post)*
  - Dry infantry, crewed guns, and vehicles (including any empty turret on multi-cannon tanks) move up to 60 m to the nearest safe friendly flag, ammo truck, or crate (25 m enemy buffer; up to four units spaced around the point) and wait until topped off.
  - On `Hold Position`, walks back to post afterward unless interrupted or enemies close within 10 m.

### Armored vehicles & crews

- **[Tank obstacle crushing & front-armor reversing](research/notes/tank-crusher-26734796.md)**
  - Tracked tanks push through fences and crushable cover in combat instead of detouring around them.
  - Short pullbacks (`<= 45 m`, up to `135°` turns) reverse with frontal armor facing known enemies within 180 m instead of turning around.
- **[Tank Auto-Repair](research/notes/tank-auto-repair-26734796.md)** *(any stance; crew re-board automatically)*
  - **Broken hull:** Tracks the crew across delayed ejections, sends crew out in parallel (up to available kits and damaged parts) starting with the hull, and re-boards them once seats unlock.
  - **Turret/cannon/mantlet + broken track:** Sends up to two crew (preferring kit carriers, mechanics, and non-gunners while keeping working cannon gunners seated) to repair guns -> turrets -> mantlet -> remaining parts past vanilla's two-attempt cap before re-boarding.
  - Backs off briefly after manual crew disembarks (25 s) or repairer casualties (8 s).
- **[Tank hull-rider crew refill](research/notes/tank-crew-fill-26734796.md)**
  - Moves friendly soldiers riding on a tank's hull directly into empty interior crew slots (even while moving), skipping exterior roof MGs and APC seats.
  - Fills `gunner` -> `driver` -> `charger` -> `commander` -> secondary cannons -> loaders -> hull MGs in order, preferring tank crewmen and mechanics first, ordinary infantry next, and medics or soldiers carrying live AT gear last.
  - Reserves seats for returning repairers, backs off 10 s after manual disembarks, and won't flip ownership of a crewless allied tank.

## Loading the mod

1. **Build the DLLs** (requires MinGW-w64 and MinHook; see [DEVELOPMENT.md](DEVELOPMENT.md)):
   ```sh
   ./scripts/build_ai_dll.sh
   ```
   This outputs `version.dll` and `goh_ai_mod.dll` under `local-work/build-ai/`.
2. **Copy both files** (`version.dll` and `goh_ai_mod.dll`) into `Call to Arms - Gates of Hell/binaries/x64/`.
3. **Launch normally through Steam.** `version.dll` loads `goh_ai_mod.dll` into `call_to_arms.exe` (skipping the editor `call_to_arms_ed.exe`), verifies the exact Steam build `26734796` binary before hooking, and writes `version_proxy.log` and `goh_ai.log` beside the DLLs.
   - **Linux / macOS (Proton / Wine):** Set Steam launch options to `WINEDLLOVERRIDES="version=n,b" %command%`.
   - **Multiplayer:** Only the **host** installs the mod; vanilla clients need no files.
   - **Uninstall:** Remove `version.dll` and `goh_ai_mod.dll` from `binaries/x64/`.
   - *Note:* Drop-in `version.dll` auto-loading is still awaiting a launch smoke test on build `26734796`; `goh_ai_mod.dll` can also be injected directly into a running `call_to_arms.exe` with any x64 DLL injector.

### Optional configuration (`goh_ai.ini`)

No config file is required — everything runs with balanced defaults out of the box. To customize behavior, place a `goh_ai.ini` file beside the DLL:

- **`[modules]`:** Enable or disable any individual module (e.g., `auto_smoke = false`, `tactical_objectives = false`).
- **`[tuning]`:** Adjust search radii, cooldowns, and grenade detection range (invalid or extreme values are safely clamped).
- **`[logging]`:** Set log verbosity (`off`, `error`, `summary`, or `verbose`; defaults to `summary`) and optional log filename beside the DLL.
- **`[smoke]`:** Optionally list extra non-WP smoke grenade or launcher names from third-party mods (`extra_grenades`, `extra_programs`); White Phosphorus and explosive items remain strictly blocked.

## Acknowledgments

All AI modules are implemented from scratch as native engine hooks without borrowing scripts or assets, while several quality-of-life and tactical ideas were inspired by community Steam Workshop mods for *Gates of Hell: Ostfront*:

- **[Auto Medic](https://steamcommunity.com/sharedfiles/filedetails/?id=2960496242)** and **[Auto Medic-AA-Artillery & Weather Switch](https://steamcommunity.com/sharedfiles/filedetails/?id=2829744930)** (Nikral晓杉同学), and **Auto Medic Plus** (Era [FR]): autonomous medic revives with threat checks and return-to-post behavior.
- **[Auto Resupply & Return](https://steamcommunity.com/sharedfiles/filedetails/?id=3814488828)** (Slayer): automatic ammo trips to nearby supply points with return to original position.
- **[AI simple modification](https://steamcommunity.com/sharedfiles/filedetails/?id=3253307216)** / **[I.A.E.M.](https://steamcommunity.com/sharedfiles/filedetails/?id=3225523571)** (222222): individual combat AI tweaks for smoke use, AT grenades, target priority, and cover reactions.
- **[Code:X AI Overhaul](https://steamcommunity.com/sharedfiles/filedetails/?id=3636883799)** (xFizzle) and **[Conquest Enhanced v2](https://steamcommunity.com/sharedfiles/filedetails/?id=2899823515)** (hawka & the CEv2 team): tactical squad posture, defensive smoke support, and smarter tank self-preservation.
- **[Tank/Shell Collision Optimization](https://steamcommunity.com/sharedfiles/filedetails/?id=3008573921)** (HOBOE): letting tanks push cleanly through fences, poles, and minor obstacles.

## Development

- [DEVELOPMENT.md](DEVELOPMENT.md): repo layout, build/lint/test commands, Ghidra, and game-update workflows.
- [AGENTS.md](AGENTS.md): coding-agent rules and multiplayer non-desync invariants.
