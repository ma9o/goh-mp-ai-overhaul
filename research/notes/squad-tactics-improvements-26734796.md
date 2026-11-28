# Stream C squad tactics · build 26734796

`runtime_allowed: false`

Stock `call_to_arms.exe` SHA-256 is `81a13de787c1869b766004cdcadb9ff99012f0b1b58b89db9a7741b10b9f059a`. The policies reuse existing hooks. After stock actor slot 23, the host rechecks the actor ID, serial scope and concrete human brain, then observes seat and player-move transitions before applying movement admission. Scene changes, clock rewinds and identity changes invalidate private actor history. [Tactical adapter](../../mods/tactical_infantry_module.hpp)

| Item | Admission and action |
| --- | --- |
| **#2 · Weapon teams** | Under `weapon_team_cohesion`, match the assistant's unreserved loose stacks to the nearest living squad gunner's carried support weapon through `0x565A50`. Keep the assistant on overwatch. Favor cover 2.5–8 m from the gunner and rejoin beyond 10 m. Equipment refreshes once per second. Cached pairs must still match a live gunner, entity ID and definition. |
| **#2 · Dismount** | A seat-bit or vehicle-parent transition opens a three-second window. A nearby witnessed squad exit can also admit a clumped member first seen on foot. An uncovered Move-at-Will soldier with two idle eligible allies within 2.5 m tries free cover 3.5–7 m from the centroid, then passable 5 m fan-out points. Reject movement toward the nearest known hostile. Consume one attempt even on failure. |
| **#1 · Overflow** | Under `overflow_cover`, clearing an observed player move opens a three-second window anchored at the soldier's position. Move at Will requires one nearby covered squadmate or a known enemy. Hold Position requires two covered squadmates and a slot strictly inside both 7 m and `overflow_cover_radius_m` (default 8 m). Consume one cover search per completed move. |
| **#11 · Exposed gunner** | Under `close_grenade` and `mg_crew_small_arms`, grenade priority, polling, rush and throw revalidation share one decision. Require a live targetable gunner, a usable loaded rifle, SMG or MG in range and a clear eye-to-chest segment. Terrain, hard cover or a shield hit before the gunner preserves the grenade option. An already-held frag inside throw range and strictly under 10 m remains eligible. Cars require an exposed gunner station and a mounted MG. Tanks, armored cars and identified armored carriers retain their existing policy. [Grenade adapter](../../mods/close_grenade_module.hpp) |

Both movement features require `tactical_infantry`. They recheck the free order slot, squad selection and waypoint, Direct Control, inhibition, player orders and higher-priority work. Native cover search supplies descriptors. A successful request reserves its destination for 12 seconds with 1.5 m spacing from squad feet, occupied descriptors and claims. The free queue or brain setter receives MoveToCover (`0x8E2770`) or MoveTask (`0x90A8C0`). Point fallback uses deterministic bearings and the existing dry-ground corridor check. [Movement contract](tactical-infantry-26734796.md) · [Target and range contract](target-prioritization-26734796.md)

`stat_overflow_cover` and `stat_dismount_spread` count submitted orders. `stat_mg_small_arms_defer` counts grenade candidate deferrals, potentially several per encounter. Hook teardown resets all three.

All three requested Clang ASan+UBSan fixtures and the four-file lint gate passed. A move-bit clear can also mean cancellation. Actual arrival, crew exposure, subsequent rifle selection, frame cost and host/client command and checksum parity remain live smoke checks on this fingerprint. [Stream C fixture](../tests/test_tier12_squad_tactics_26734796.cpp) · [Runtime smoke scenarios](../compatibility/runtime-smoke.md)

## Assault tactics evaluation & removed experiments

Live A/B sweeps across both the mirror-symmetric flat-field close-quarters skirmish (`--suite cqb`, 26–32 m sandbag trenches + flanking lanes; 89 valid trials) and the staged 1v1 Oryol combined-arms battle (`--suite e2e`, 40–80 m park approach + armor) evaluated four infantry assault additions built on the existing hooks. Only the two general, empirically winning additions were kept; the two regressive features were removed cleanly so they do not clutter the codebase or hurt live fights.

### Kept

| Module | Admission and tuning | Empirical result |
| --- | --- | --- |
| **`cover_frag`** ([close_grenade_module.hpp](../../mods/close_grenade_module.hpp)) | Within `7 m` to `(max_range - 0.5 m)`, ranks anti-vehicle throws strictly higher, checks friendly blast clearance, deconflicts one thrower per target zone, lifts aim `+0.35 m` to clear sandbag lips, and grants a `2.5 s` smoke-occlusion grace once a throw order is already issued so friendly smoke blooming mid-throw does not abort an aimed toss. | Strongest single infantry addition across both ranges: **9W–1L (90.0%), mean 85.36** alone on `--suite cqb` (vs **65.97** baseline), and lifted Oryol Stage 1 infantry from **56.32** to **64.27**. |
| **`offensive_smoke`** ([auto_smoke_module.hpp](../../mods/auto_smoke_module.hpp)) | Move-at-Will healthy non-medic riflemen with `>= 2` pure smoke grenades in squad reserve screen **actively firing** enemy MGs/field guns (`35–105 m`) or firing covered infantry clusters (`>= 2` at `35–50 m`, `>= 3` at `50–105 m`), with a `3.0 s` per-squad in-flight dedupe guard against immediate defensive double-throws. The `35 m + firing` gate aligns with GoH weapon envelopes (frags `<= 22 m`, SMGs `<= 35 m`, MGs/guns `35–105 m`) and stops squads from halting in the open to smoke unfiring contacts at spawn. | Restored `--suite cqb` (`cover_frag` + `offensive_smoke`) from **66.92** (with a `30 m` unfiring gate) to **80.60 (5W–1L, 11.68× exchange)** with zero premature spawn smoke, and achieved the highest Oryol Stage 1 infantry score (**65.81**, **+25.0 pp** survival, **+3.8** flag delta, **4W–0L**). |

### Evaluated and removed

| Removed experiment | Empirical failure | Root cause |
| --- | --- | --- |
| **`flanking_wedge`** *(removed from `tactical_infantry`)* | **7W–13L (35.0%), mean 38.20** across 20 `--suite cqb` trials (`0` completed wedge hops; **24.97** when combined with all 4 toggles vs **65.97** baseline). | First exposed a posture bug (`sync_squad_posture` set `advance`, walking flankers into open fire during `fire_until` dwell). Even after fixing posture to `hold` and adding uncovered cover fallthrough, committing `team->mode` to a dedicated 6 s wedge state starved `tactical_infantry`'s existing `Mode::left/right/bound` cover-to-cover advancement—which already fans cleanly into angled flank sandbags and midfield cover without locking the squad. |
| **`role_target_split`** *(removed from `target_prioritization`)* | **1W–3L (25.0%), mean 41.56** isolated on `--suite cqb` (**−24.4 pts** vs baseline), dragged `cover_frag + offensive_smoke` down by **−15.7 pts** (**62.87** vs **78.54**), and dipped Oryol Stage 1 infantry (**55.22** vs **56.53**). | Static additive bonuses (`+0.07` to `+0.12`) for enemy MGs, AT carriers, medics, and infantry clusters overrode live threat, proximity, and weapon-alignment scoring—pulling close-quarters rifles/SMGs off immediate lethal targets right in front of them and inducing frequent target switches. |
