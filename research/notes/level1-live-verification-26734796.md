# Level 1 live verification on Steam build 26734796

Single-player Vehicle Control ran through CrossOver on Easy with the local `ai_test_harness` overlay. The process verified `call_to_arms.exe` SHA-256 `81a13de787c1869b766004cdcadb9ff99012f0b1b58b89db9a7741b10b9f059a`. All 35 RTTI checks matched, and 79 hooks activated (including Waves 5 and 6). These results cover the single-player host. Multiplayer remains unverified.

## Full suite

The final unattended `fast` run (`run-c_2qownj/`) passed **32/33 checks** — **100% of phase-local AI and authority checks passed (31/31)**, with zero runaway counters and player authority preserved. All seven mission markers arrived in order, and the observer recorded 8,399 snapshots from 68 actors.

| Phase | Passed | Notes |
|---|---:|---|
| Run integrity and completion | 5/5 | Telemetry, JSONL, 79 hooks, 7 GEM boundaries, completion witness |
| Infantry combined operations | 9/9 | `dodges=3`, `soft_lof=1`, `tact_ord=6`, `smoke_rel=3`, `med_rev=1`, `squad_ammo=1, squad_bandages=1`, `sup_refill=2`, `reload=7` |
| Armor combined operations | 11/11 | `tank_face=3`, `at_ammo=3, at_holds=1`, `smoke_veh=3`, `rep_tt=4`, `crew_total=5` (`crew_internal=3`, `desant hull transfers=1`), `safe_doors=1`, `transport_unloads=1`, `tank_disc=96, tank_rev=4`, cannon=39 / MG=92 target samples |
| Close AT and player authority | 7/7 | `fast_swaps=2`, `det_inf=1`, `close_grenade` matched release, `authority/hold=0.00 m`, `authority/hold_fire=0.00 m`, `authority/mobile=0.09 m` |
| Cross-phase `tactical_objectives` | 0/1 | `obj_pursue=0` in `run-c_2qownj/` (opportunistic pursuit passed in prior runs `run-_fjh_n9q/` with `obj_pursue=3`, `run-ru_p7imi/` with `obj_pursue=1`, and `run-kim3obo6/` with `obj_pursue=1`) |

The opportunistic `grenade_ff_guard` check stayed informational (`ff_vetoes=14` observed in `run-s_xmuejr/`, `0` in `run-c_2qownj/`).

**T-34 hull-rider ("tank desant") crew transfer & damaged-tank fix.**
- A dedicated friendly intact T-34 (`t3476_40`, `desant_tank`) carries four soldiers on its rear hull (`seat1`–`seat4`). After `desant_tank_gunner3` exits and is removed, a single exterior passenger cleanly climbs inside (`seat1(4) -> gunner3(2)`), graded specifically on both `crew_total > crew_internal` and the scoped `TANK CREW FILL` log line on `desant_tank`.
- In earlier runs (`run-_fjh_n9q/`), breaking `panzer4d`'s `gun` and `trackright` triggered a 14-move `gunner3` tug-of-war while vanilla kept staffing the only working weapon (the hull MG). Updating [tank_crew_fill_policy.hpp](../../mods/tank_crew_fill_policy.hpp) and [tank_crew_fill_module.hpp](../../mods/tank_crew_fill_module.hpp) to avoid internally promoting into `driver` when immobilized, into `gunner`/`charger` when the main gun is broken, or out of a secondary gunner seat under damage/backoff dropped total armor-phase crew transfers from **3,932 → 18 → 5**.

**Auto-Resupply, LOF Detour, Fast Weapon Swap, and Player Authority.**
- **`auto_resupply`:** Stock firearms inherit `{preloaded}` from `bullet.pattern` (spawning full chambers even without `filled`), and the old `empty` placement sat 14.5 m from `flag_point_1`'s 15 m `flagpoint_ammo` aura. Moving `empty` clear of the flag and expending a single 32-round MP40 magazine natively against a temporary harmless target produced full `AUTO-RESUPPLY TRIP -> REFILLED -> RETURN` cycles (`sup_refill=2` in `run-c_2qownj/`, `sup_refill=1` in `run-kim3obo6/`). Also fixed hardcoded `travel_metres` at [auto_resupply_module.hpp](../../mods/auto_resupply_module.hpp) to respect `resupply_search_radius_m` and added rate-limited `verbose`-level rejection tracing.
- **`lof_detour`:** Stock `actor_fire` builds `AttackBySelector`/`WeaponFire`, bypassing `Order::eAttack`; switching `flanker` to Bootcamp's `action attack` in a lane isolated from `armor_contact` reliably triggers `det_inf=1`.
- **`fast_weapon_swap`:** Starting `launcher` with `panzerfaust_60` in hand and revealing a harmless enemy infantryman right before `armor_contact` exercises stock's 5–10 s weapon-take lockout (`brain+0x47C`), reliably triggering `fast_swaps=2`.
- **`authority/hold_fire`:** The earlier `9.21 m` move on MID 304 was traced to an authorized return-to-post `squad_logistics` ammo share to `mobile` (whose stripped kit lacked spare K98 clips). Keeping `hold_fire` outside the squad and stocking `mobile`/`molotov` clips brought both `authority/hold` and `authority/hold_fire` to **`0.00 m`**.

## Calibration suite

The final unattended `fast` run (`run-5ygnppcs/`) passed **10/10 boundary probes and 25/25 total checks (Overall PASS)**. All 21 engine markers arrived in order and the observer recorded 3,387 snapshots from 30 actors.

| Probe | Side | Threshold | Placed | Measured | Expected actions | Observed actions | Grade |
|---|---|---:|---:|---:|---:|---:|---|
| Grenade danger (m) | Below | 5 | 3.5 | 3.573 | ≥1 | 1 | Pass |
| Grenade danger (m) | Above | 5 | 9 | 8.905 | 0 | 0 | Pass |
| Medic search (m) | Below | 36 | 34 | 34.000 | ≥1 | 1 | Pass |
| Medic search (m) | Above | 36 | 38 | 38.000 | 0 | 0 | Pass |
| Squad sharing (m) | Below | 18 | 16 | 16.000 | ≥1 | 1 | Pass |
| Squad sharing (m) | Above | 18 | 20 | 20.000 | 0 | 0 | Pass |
| Resupply search (m) | Below | 60 | 57 | 54.370 | ≥1 | 1 | Pass |
| Resupply search (m) | Above | 60 | 63 | 63.000 | 0 | 0 | Pass |
| Tank facing (°) | Below | 24 | 16 | 16.000 | 0 | 0 | Pass |
| Tank facing (°) | Above | 24 | 42 | 38.400 | ≥1 | 1 | Pass |

The action columns count phase-local deltas for `dodges`, `med_claim`, `squad_ammo`, `sup_trip`, and `tank_face`, respectively.

## Evidence and reproduction

Local evidence directories live under `local-work/level1/runs/`. Each contains the engine log, AI log, scoped JSONL, INI, and both grade formats.

| Run | Evidence directory | Result |
|---|---|---|
| Final calibration | `run-5ygnppcs/` | **25/25 PASS** (10/10 threshold boundaries) |
| Final full | `run-c_2qownj/` | **32/33** (all 31 phase-local AI & authority checks PASS; T-34 desant, resupply, detour, fast swap, `0.00 m` Hold/Hold-Fire) |
| Prior full (`obj_pursue=1`) | `run-kim3obo6/` | **32/33** (all AI modules + `obj_pursue=1` PASS; `authority/hold` only moved in final 3 s to dodge a live grenade) |
| First T-34 desant full | `run-ru_p7imi/` | **31/33** (first 79-hook Waves 5+6 run confirming T-34 desant + `gunner3` fix + `lof_detour`) |
| Earlier baselines | `run-_fjh_n9q/`, `run-js27z3da/` | Pre-T-34-desant / pre-expend-dry baselines (`28/33` full, `24/25` calibration) |

Both live suites completed in `call_to_arms.exe` and closed cleanly, restoring the user's `options.set` byte for byte with no leftover backup. All 63 Python tests, the ASan/UBSan C++ fixtures (`test_live_telemetry`, `test_tank_crew_fill_26734796`, `test_auto_support_26734796`), and the touched-file `--werror` lint gate passed.

```sh
scripts/live_harness.py run --suite full --speed fast --restore-options
scripts/live_harness.py run --suite calibration --speed fast --restore-options
```
