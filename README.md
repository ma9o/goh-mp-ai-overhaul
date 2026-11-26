# Gates of Hell: MP AI Overhaul

Host-only native AI overhaul for **Call to Arms: Gates of Hell - Ostfront** (Steam build `26734796`). Nothing in `resource/` or `.pak` files is touched, so vanilla multiplayer clients can join a modded host without installing the mod. Should also work in singleplayer, skirmish, and Conquest but it's only been tested in multiplayer so far. 

**Work in progress**: Still a lot of rough edges. Contributions welcome!

## Gameplay changes

Everything below works in both **Hold Position** (the GoH default) and **Move at Will**, except where noted.

### Combat & targeting

- **[Target prioritization & shell economy](research/notes/target-prioritization-26734796.md)**
  - Ranks visible enemies by weapon/ammo effectiveness, range, threat (including armor targeting nearby allies), penetrability, and clear line of fire, demoting defanged or impenetrable vehicles.
  - Vehicles with a `>= 37 mm` cannon use an eligible mounted MG first against a lone soldier instead of wasting a shell.
  - Multi-cannon tanks (`T-35`, `SMK`, `M3 Lee`/`Grant`, `Churchill Mk I`, `Panzer B2`) pick the highest-penetration gun against armor and the largest HE blast against infantry groups; secondary turrets keep engaging while an explicit `Attack` locks the main gun.
  - **Armor target memory & live allegiance checks** ([build contract](research/notes/tank-combat-improvements-26734796.md)): When an enemy tank slips into smoke or fog of war, tanks and turretless tank destroyers hold their main-gun and hull bearing for a few seconds instead of slewing away to shoot distant riflemen (while secondary guns, close AT threats, and newly spotted armor still engage immediately). Units also recheck live team ownership before autonomous fire so captured vehicles are not fired on by allies, and towed guns hold fire while hitched or being moved.
- **[Penetration-aware anti-tank AI](research/notes/penetration-at-26734796.md)**
  - Aims for lethal armor zones (hull, turret, engine, ammo) using the game's penetration profiles instead of minor components.
  - Keeps a viable chambered shell instead of needlessly reloading, checks all compatible ammo in inventory, and switches from large-blast HE to AP if friendlies are inside the HE blast radius of an open-top target.
  - Holds fire (aim-only tracking) when an unthreatened gun faces impenetrable armor beyond 35 m, when a towed/MG-less cannon only sees a lone rifleman beyond 40 m, or briefly while a slow-reloading cannon finishes settling after a wide traverse ([build contract](research/notes/tank-combat-improvements-26734796.md)).
- **[Soft-cover shoot-through](research/notes/lof-detour-soft-cover-26734796.md)**
  - Piercing rounds (AP, APHE, APCR, AT rifles, MGs, rifles) shoot through wooden walls, fences, trees, and thin masonry (`>= 75 mm`) when penetration clears every layer without detonating early.
  - Never fires through terrain, water, hard cover, other vehicles, or near friendlies; excludes HE, HEAT, AA, smoke, WP, and self-damaging HE blasts.
- **[Blocked explicit-Attack LOF detours](research/notes/lof-detour-soft-cover-26734796.md)** *(Move at Will only)*
  - Replaces blind random sidesteps for infantry and adds repositioning for blocked vehicles (which vanilla leaves idle), picking lateral or corner-clearing moves with clear LOF and low exposure while keeping the `Attack` target.
  - Stands crouched/prone infantry up in place when standing alone clears the shot; lets casemates and limited-traverse hulls rotate before moving.

### Infantry survival, tactics & grenades

- **Faster weapon changes.** A soldier who spots armor can ready a carried anti-tank weapon sooner and keep it in hand while the vehicle is nearby. When infantry close in, a soldier carrying a launcher or an empty gun can switch back to a firearm. [Build contract](research/notes/combat-logistics-26734796.md)
- **Grenade clearance.** Soldiers check the landing point before throwing a lethal grenade on their own and hold the throw if it would endanger themselves or nearby allies. Explicit Attack orders, Attack Ground orders, and manual throws take precedence. Pure smoke grenades can still screen friendly troops. [Build contract](research/notes/combat-logistics-26734796.md)
- **[Early flying-grenade dodge](research/notes/grenade-escape-26734796.md)** *(overrides Hold Position)*
  - Tracks armed grenades in flight, predicts where they will land, and wakes nearby soldiers before impact while ignoring outbound throws landing safely away.
  - Sprints standing away from both the blast and known enemies instead of crouching into nearby cover, while preserving a player's own move order when it already runs safely clear of the blast ([build contract](research/notes/survival-logistics-improvements-26734796.md)).
- **[Close-range anti-vehicle grenades & AT rush](research/notes/close-grenade-26734796.md)** *(throws in any stance; rush on Move at Will only)*
  - Prioritizes nearby vehicles and crewed guns over rifle targets, pre-equips when a vehicle closes within throw range `+7 m`, and bypasses vanilla's 5–10 s weapon-switch delay without dropping held AT launchers, AT rifles, or flamethrowers.
  - Coordinates one thrower per target with failure backoff, leads moving vehicles, and checks friendly blast clearance (without blocking throws that can't harm closed friendly armor).
  - Molotovs aim for intact engines on all vehicles (including cars); frag grenades target exposed crew and open-top vehicles, never closed tanks. When soldiers have a loaded rifle, SMG, or MG with a clear direct shot at an unshielded MG gunner, they shoot the crew instead of grenade-rushing into the arc of fire ([build contract](research/notes/squad-tactics-improvements-26734796.md)). On `Move at Will`, up to two carriers sprint along multi-angle paths (`+15 m`) into throw range while auto-smoke yields to them.
- **[Tactical infantry & squad movement](research/notes/tactical-infantry-26734796.md)** *(Move at Will only, except overflow cover)*
  - Stabilizes advance/hold/fallback posture so squads stop oscillating, pulls exposed rifle/SMG soldiers out of the open against armor, and coordinates alternating squad bounds (medics, heavy weapons, and their ammo-carrying assistants stay together on overwatch rather than bounding apart).
  - Ranks cover by threat screening and line of fire (crossing open ground only when no cover fits), tries stance changes or sidesteps when blocked, holds screened cover through distant hits, and stops stale return walks. Selected squads keep priority.
  - **Overflow cover & post-dismount spread** ([build contract](research/notes/squad-tactics-improvements-26734796.md)): After a squad finishes a move onto a trench or wall that runs out of slots, uncovered overflow soldiers take distinct nearby cover (including tightly bounded snaps on Hold Position when at least two squadmates are already in cover). Recently dismounted Move-at-Will squads fan out into nearby cover instead of bunching behind the rear door.
- **Doorway & wall-edge stall recovery** ([build contract](research/notes/movement-convoy-improvements-26734796.md))
  - Infantry and pushed guns that stall against a doorway or wall edge during a move try up to two short side or back-out nudges to clear the snag, then resume their original destination. Any new player order immediately cancels recovery.
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
  - Idle medics claim the nearest reachable downed ally within 36 m (one medic per patient), backing off temporarily from unreachable or failed revives so they can move on to other casualties ([build contract](research/notes/survival-logistics-improvements-26734796.md)).
  - If out of smoke, fetches one smoke grenade from an idle ally within 24 m (preferring spare stacks; never taking another medic's last smoke), throws a screen, waits for the plume, and revives — or revives unscreened if no donor is nearby and no enemy is within 25 m.
  - On `Hold Position`, walks back to post afterward unless interrupted or enemies close within 10 m.
- **Squad ammo, bandages, and battlefield scavenging.** An idle soldier can bring compatible ammunition to a nearby gunner running low, or a bandage to a wounded ally who has run out. Squadmates get first choice. When no living ally has spare rounds — including for picked-up weapons — a soldier running low can scavenge compatible loose ammo from a safe nearby fallen soldier, ground box, or idle friendly vehicle within 20 m and return to post on Hold Position. [Build contract](research/notes/combat-logistics-26734796.md) · [Scavenging contract](research/notes/survival-logistics-improvements-26734796.md)
- **[Auto-Resupply](research/notes/auto-resupply-26734796.md)** *(any stance; Hold Position returns to post)*
  - Dry infantry, crewed guns, and vehicles (including any empty turret on multi-cannon tanks) move up to 60 m to the nearest safe friendly flag, ammo truck, or crate (25 m enemy buffer; up to four units spaced around the point) and wait until topped off.
  - On `Hold Position`, walks back to post afterward unless interrupted or enemies close within 10 m.

### Armored vehicles & crews

- **Safer dismounts.** When the crew or a nearby ally has spotted enemies, dismounting passengers favor an open exit on the far side of the vehicle. Damaged or burning transports can unload their passengers. A stopped or immobilized transport can also unload under a close anti-tank threat. [Build contract](research/notes/combat-logistics-26734796.md)
- **Stationary tank facing** ([build contract](research/notes/tank-facing-26734796.md))
  - A stationary tracked tank can turn its hull in place toward a threat spotted by its crew or a nearby ally. It turns only when the change would substantially reduce its estimated exposure. The turret keeps its own aim.
  - Works on Hold Position and Move at Will. Your move, attack, and face orders take priority. On Hold Fire, the tank stays quiet unless it has recently taken a hit or the contact is already attacking or threatening it.
- **[Tank obstacle crushing & front-armor reversing](research/notes/tank-crusher-26734796.md)**
  - Tracked tanks push through fences and crushable cover in combat instead of detouring around them.
  - Short pullbacks (`<= 45 m`, up to `135°` turns) reverse with frontal armor facing known enemies within 180 m instead of turning around.
- **Vehicle convoy spacing & passing** ([build contract](research/notes/movement-convoy-improvements-26734796.md))
  - Following vehicles pause briefly instead of ramming into a moving friendly leader, and pass cleanly around a stopped friendly vehicle when open ground permits before resuming their destination. On narrow roads or bridges they hold behind the leader instead of swerving into obstacles.
- **[Tank Auto-Repair](research/notes/tank-auto-repair-26734796.md)** *(any stance; crew re-board automatically)*
  - **Broken hull:** Tracks the crew across delayed ejections, sends crew out in parallel (up to available kits and damaged parts) starting with the hull, and re-boards them once seats unlock.
  - **Turret/cannon/mantlet + broken track:** Sends up to two crew (preferring kit carriers, mechanics, and non-gunners while keeping working cannon gunners seated) to repair guns -> turrets -> mantlet -> remaining parts past vanilla's two-attempt cap before re-boarding.
  - Backs off briefly after manual crew disembarks (25 s) or repairer casualties (8 s).
- **Automatic crew seat swaps and passenger refill** ([build contract](research/notes/tank-crew-fill-26734796.md))
  - Surviving crew move from the least critical occupied seat to cover a vacancy. For example, a commander can take over the main gun, then a hull rider can fill the commander's seat. Main gunner and driver take priority, followed by loader, commander, and secondary gun crews.
  - Tank hull riders, halftrack and APC passengers, and soldiers seated in armored cars or trucks can fill vacancies. Crewed guns can redistribute their crew while deployed. Roof gunners keep their posts, and crews stay put while a gun is being pushed or towed.
  - A driver with no movement order can take the gun when immobilized or facing a visible enemy. A lone gunner can return to the wheel for a move order if the vehicle is mobile. Both swaps give an available passenger first choice and have a three-second cooldown.
  - Passenger fills favor crewmen and mechanics first, ordinary infantry next, and medics or soldiers carrying live AT gear last. They leave room for returning repairers, respect allied ownership, and pause for 10 seconds after a manual crew disembark.

### Engine fixes and lull reloading

- After self-healing, a soldier with an empty hand can draw the weapon they put away. Infantry caught in a fire can move to reachable ground outside the flames.
- Defenders can break a line of windowpanes when every obstruction is breakable glass. Soldiers on Hold Position can rise in place for a clear shot or to defend themselves at close range.
- Infantry can top up magazines at or below 35% during a lull. A visible enemy within 35 m takes precedence. Player-owned units can keep their firing order through a reload.
- Repairers keep their kit after an interrupted, unfinished repair and can choose separate damaged components. Followers and boarding troops tolerate small destination changes before replanning.
- Player-owned units can remember a target for up to 20 seconds. A burning vehicle that can still fight remains a candidate, and a nearby contact can no longer hide a more useful target from the target-prioritization module.

These changes have been checked offline on Steam build `26734796`. Live host and client verification is pending. [Build contract](research/notes/engine-fixes-26734796.md)

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
  - Crew refill defaults to enabled. Set `internal_crew_swap = false` for passenger fills only, `nontank_crew_fill = false` to limit the feature to tracked tanks, or `tank_crew_fill = false` to disable all crew transfers by this module.
  - Set `engine_fixes = false` to disable the pack, or `lull_reload = false` to disable only proactive reloading. Under `[tuning]`, `lull_reload_ratio` accepts 0.10–0.75 and defaults to 0.35.
  - The five combat and logistics switches default on. Set `fast_weapon_swap`, `grenade_ff_guard`, `safe_disembark`, `transport_unload`, or `squad_logistics` to `false` to disable that feature. Under `[tuning]`, `squad_share_radius_m` sets donor search range (default 18 m, range 6–40 m).
  - Additional combat, tactics, and movement switches also default on: `ammo_scavenge`, `tank_target_memory`, `cannon_aim_settle`, `allegiance_guard`, `overflow_cover`, `weapon_team_cohesion`, `mg_crew_small_arms`, `movement_recovery`, and `convoy_spacing`. Under `[tuning]`, customize `scavenge_radius_m` (default 20 m, range 6–40 m), `tank_memory_hold_s` (default 5 s, range 1–15 s), `cannon_settle_max_s` (default 1.8 s, range 0.3–5 s), `overflow_cover_radius_m` (default 8 m, range 3–20 m), `movement_stall_s` (default 2.5 s, range 1–8 s), and `convoy_gap_m` (default 8 m, range 4–20 m).
  - Set `tank_facing = false` to disable automatic hull turns. Under `[tuning]`, `tank_facing_min_angle_deg` sets the smallest turn to consider (default 24°, range 15–60°).
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
