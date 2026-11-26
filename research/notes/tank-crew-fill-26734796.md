# Tank Exterior-Passenger Crew Refill on Steam build 26734796

**Static implementation for the x64 game executable.** The module admits SHA-256 `81a13de787c1869b766004cdcadb9ff99012f0b1b58b89db9a7741b10b9f059a` and installs zero additional MinHook detours (`hook_count = 0`), running from the shared serial `eActor::slot23` post-quant callback immediately after Tank Auto-Repair. Private-lobby admission, host/vanilla-client agreement, and live unload remain untested (`runtime_allowed = false`). [Policy](../../mods/tank_crew_fill_policy.hpp) · [Module](../../mods/tank_crew_fill_module.hpp) · [Fine map](../artifacts/build-26734796/tank-crew-fill-26734796.json) · [Validation receipt](../artifacts/build-26734796/tank-crew-fill-26734796-validation.json)

## Scope and selection policy

Stock `ePlacer::Plan` (`0x7954A0`) only pulls replacement crew from slots with the same `group` string (`"crew"`), so soldiers seated on top of a tank's hull as passengers (`properties/tank_crew.ext` `seat_human` -> `group "passenger"`, `LinkBone "seat01"`..`"seat12"`) stay outside even after interior crew seats die and re-enable.

| Aspect | Rule |
| --- | --- |
| **Eligible tanks** | Tracked tanks (`tank`, `tank_light`, `tank_medium`, `tank_heavy`, `tank_bullet`, or definition tag `"tank"` with an actual track component) with a concrete `Actor::eVehicleBrain` (`0x101FD68`), working hull (`body` state `0`), upright within 70°, not burning/sinking/destroyed/exploded/abandoned, and canonical root boarder (`0x79D790` returns the tank's own boarder). |
| **Eligible sources (hull passengers)** | Occupied `group == "passenger"` slots with a resolved `LinkBone` (`slot+0x170 != 0 && slot+0x240 != 0`), **no** registered weapon/chassis crew request (`!(flags & 0x1000)`), **no** visor (`slot+0x110 == 0`), and neither `Attach` nor `NoCollect` (`!(flags & 0x8200)`). This targets hull-top tank riders (`seat1`..`seat12`) and explicitly excludes exposed exterior roof MGs (`gunner_open` `gunner2`, which carry bit 12 and visor `"top"`) and passenger-group APC crew seats (e.g. `ram_kangaroo` `driver`/`commander`). |
| **Eligible destinations (freed crew slots)** | Empty, unreserved, enabled, unsealed `group == "crew"` slots passing the native `ePlacer::IsPlaceAvailable` (`0x79C2F0`) predicates and resolving to a kind-2 Place node in `eBoarder::FindNode` (`0x79E930`). Stock `tank-main.inc` disables a killed crewman's seat (`flags & 0x10`) for 8 seconds before `{crew_enable place ... 1}`, which the module honors automatically. |
| **Crew slot fill order** | `gunner` → `driver` → `charger` → `commander` → secondary cannon gunners (`>= 37 mm` via `place+0x5E0`) → secondary chargers (`charger1`..`charger6`) → other registered crew/bow-MG slots (`flags & 0x1000`) → remaining `"crew"` slots, breaking ties by slot index. |
| **Passenger priority** | Any friendly eligible soldier on top of the hull can fill a freed slot, ranked deterministically: (0) dedicated tank crewmen (`"crew"` tag at `breed+0x440` + Mechanic ability `0x107F0F8`), (1) skilled mechanics without `"crew"` tag (`rate >= 0.99`, e.g. `tankman_ace` and singleplayer `tankman`), (2) other `"crew"`-tagged personnel, (3) basic mechanics (`driver` breeds), (4) ordinary infantry, (5) infantry specialists (Medics via `0x107EEA8` and AT-weapon tags `soldier_bazooka`/`soldier_pzscheck`/`soldier_pzfaust`/`soldier_atr`/`soldier_atr_grenade`). Ties break by same-player ownership, mechanic rate, slot index, and entity ID. |

## Native boundary and multiplayer replication

All addresses are build `26734796` game RVAs (relocated from the `25492057` [placement](placement-map-25492057.md), [boarding](boarding-map-25492057.md), and [crew actions](crew-actions-map-25492057.md) maps).

| Native boundary | Build `26734796` RVA | Contract used by module |
| --- | --- | --- |
| `eBoarder::Move` | `0x7A0240` | Direct seat transfer (same path invoked by the vehicle crew UI at `0xB88BC5`). Emits source occupancy via `0x7905B0`, reserves destination via `0x7987F0`, resolves both Place nodes via `0x79E930`, invokes `StartWalker` (`0x7A1740`) → `AdvanceWalker` (`0x7A1FA0`), emits owner event `0x19`, and on MP hosts (`0xC2B740`) publishes native wire command **`0x35` (`eBoarderMoveCommand`, `0x161C10`/`0x161C50`/`0x161CC0`)**. On standard tanks where hull passenger doors (`emit3+`) do not connect to interior crew hatches (`emit0..2`), `FindNextHop` (`0x79E970`) returns null and `AdvanceWalker` synchronously calls `Finish` (`0x7A4DA0`) → `Collect` (`0x7988B0`) → `Include` (`0x78F2D0`), working smoothly even while the tank is moving. |
| `eBoarder::Root` / `FindNode` | `0x79D790` / `0x79E930` | Verifies the tank is its own canonical root boarder and that both source and destination slots have valid kind-2 runtime Place nodes before invoking `0x7A0240`. |
| `ePlacer::IsPlaceAvailable` | `0x79C2F0` | Layout contract mirrored directly in policy (`!(flags & 0x10)`, resolved bone if `linkBone` name is non-empty, `occupant == 0`, `reserved == 0`, `!(flags & 0x100000)`). |
| `eActor::CanAccess` / `IsOperatable` | `0x80F710` / `0x8067E0` | Guards non-same-player allied passengers: allied transfers are rejected whenever the tank has no same-player crewman already seated (preventing `Finish` at `0x7A4DA0` from flipping tank player ownership when `Operatable` transiently clears) or when `CanAccess` returns false. |
| `Human::eBreed` tags & abilities | `0x971B40` / `0x1BA490` / `0x977AC0` | Reads breed `{tags ...}` from `breed+0x440..+0x448` (stride `0x60`) and ability infos from `breed+0x528+0x20` (`Human::eInfo_Mechanic` `0x107F0F8`, `Human::eInfo_Medic` `0x107EEA8`). |

Before invoking `0x7A0240`, the module also verifies the passenger holds no stale reservation (`slot+0x290`) on any slot of the tank so `PickReservedPlace` (`0x7989F0`) inside `Collect` always resolves the exact target slot. At most one passenger transfers per tank quant; a failed call applies a 1 s per-tank retry cooldown.

## Authority, Direct Control, and repair/emit coordination

- **Host-only, serial main thread:** Guards on `!Core::IsQuantWorkerThread()` (`0xA7A80`) and `!IsRemoteClient()` (`0xC2B7A0`), staying active in SP/Skirmish/Conquest and on MP hosts while remaining completely passive on remote MP clients. Zero custom engine RNG draws.
- **Direct Control & manual orders:** Bails out on tank Direct Control (`chassis+0x18 != 0`), manual weaponry (`weaponry+0x70` bit 1), or any tank brain inhibition (`(brain+0x39C & 0x3F) != 0`). Accepts AI (`control == 0`) and player-owned (`control == 1`) tanks across `Move Free`, `Hold Position`, and `Move in Zone` because interior seat transfer does not displace the tank. Seated passengers pass `available_seat` (rejecting Direct Control, manual order bit 28, incapacitation, or inhibition counts beyond the single native seat-link inhibition with flag 19).
- **Repair and boarding de-confliction:** Counts live conscious returning crew from `tank_auto_repair::collect_returning_crew` deduplicated with scene-wide active `Order::eSmartRepair` (`0x10225B0`), `Order::eRepair` (`0x110D0E8`), `Order::eKitExchange` (`0x10224F8`), and `Order::eBoard` (`0x110B590`) orders targeting the tank. Transfers proceed only when `available_crew_slots > expected_returning_crew`, preventing hull riders from stealing a repairing tankman's seat while still refilling genuinely vacated slots (including if an outside repairer is killed).
- **Disembark protection:** Defers whenever `placer+0x48 != 0`, any exit walker (`final_node->kind == 1`) is active, or `Order::eEmit` (`0x110B808`) is active on the tank. A non-passenger `Order::eEmit` (`mode != 1`) starts a 10 s per-tank backoff so the module does not fight manual crew disembarkation.

## Offline verification

- **Fine-map structural check:** `verify_fine_map.py` passed (`status: "passed"`), verifying 38 exact function entries, 141 instruction locations, 12 RTTI vtables, and 6 read-only archive members (`tank_crew.ext`, `tank_unarmed.ext`, `vehicle.ext`, `tank-main.inc`, `vehicle-main.inc`, `skills.inc`).
- **Sanitized offline unit tests:** [test_tank_crew_fill_26734796.cpp](../tests/test_tank_crew_fill_26734796.cpp) passed under Clang ASan + UBSan (`-Wall -Wextra -Wpedantic -Werror`), covering policy rules, crew/passenger priority tiers, 8 s `kill_crewman` lockout, `gunner2`/`ram_kangaroo` exclusion, returning-repairer deduplication and casualty release, Direct Control/inhibition/broken-body/burning/sinking/tilt/missing-node guards, `Order::eEmit` mode-1 vs mode-2 backoff, allied ownership protection, remote-client/worker passivity, retry cooldown, and clock-rewind reset.
- **Build & lint:** `./scripts/lint_cpp.sh --all-files --werror --require-tidy` passed (clang-format, MinGW `-Werror`, clang-tidy across all sources and headers). MinGW x64 static DLL build `local-work/build-grenade/goh_ai_v4_24.dll` (SHA-256 `a2fca24a928f7f36cc76c7002aa7fa2e0d92d5485ea22afc5441027db2eff481`) exports `ShutdownAndUnload` and imports only `KERNEL32.dll` and UCRT API sets.

To repeat the structural check against local build `26734796` files:

```sh
python3 research/scripts/verify_fine_map.py \
  --map research/artifacts/build-26734796/tank-crew-fill-26734796.json \
  --binaries-dir "$GAME_ROOT/binaries/x64" \
  --exports-dir local-work/exports/build-26734796 \
  --resources-dir "$GAME_ROOT/resource"
```

Live runtime verification in SP/Skirmish/Conquest and private MP host/vanilla-client lobbies remains required before marking `runtime_allowed: true`.
