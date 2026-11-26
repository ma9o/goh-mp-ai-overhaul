# Tank Auto-Repair on Steam build 26734796

**Static implementation for the x64 game executable.** The module admits SHA-256 `81a13de787c1869b766004cdcadb9ff99012f0b1b58b89db9a7741b10b9f059a`. It exposes a post-quant entry point for the loader. Private-lobby admission, agreement between a host and a vanilla peer, and live unload remain untested. `runtime_allowed` is false. [Module](../../mods/tank_auto_repair_module.hpp) · [Fine map](../artifacts/build-26734796/tank-auto-repair-26734796.json) · [Validation receipt](../artifacts/build-26734796/tank-auto-repair-26734796-validation.json)

## Dispatch and completion

| Tank condition | Crew and components | When crew board |
| --- | --- | --- |
| **A. Broken body** | Dispatch up to the smallest of live eligible crew, available kits and the repairable component count, with a minimum component-count cap of one. Match stock-ejected former crew within 30 m. Prefer broken components by signed stock RepairOrder, then damaged working components. Stock priorities give body 0, guns 1, turret 2, mantlet and engine 3, and tracks 4. | Once body state is 0, an idle crewman can board when all remaining work is busy or claimed. Keep the roster through the stock delayed ejections. A second break renews the 16 s window. |
| **B. Working body, broken track, damaged gun assembly** | Dispatch up to two eligible crew, subject to kit supply. Prefer kit carriers and qualified Mechanics. With more than two live crew and a working primary gun, prefer non-gunners, with the driver first among equally qualified seats. Pick gun or gun1–gun6, turret, then mantlet, including damaged working parts. Follow with other broken and then damaged working parts, in signed RepairOrder within each tier. | Keep the assignment after the trigger clears. Continue until every repairable component is fixed. Return carried kits through native KitExchange, then queue Board with an empty place name. Retry a failed Board while the crewman remains eligible and idle. |

The stock vehicle script disables crew and passenger seats when the body breaks and schedules crew emission at 4, 8, 12 and 16 seconds. The tank script also disables selection. The delayed handlers check the transient `_abandoned` script tag and can run after the body has recovered. Body repair restores seat availability and selection. The coordinator reads the registered `repaired` ability independently of selection. The fine map fingerprints both scripts and the vehicle component definitions. [Resource evidence](../artifacts/build-26734796/tank-auto-repair-26734796.json)

On each eligible tank quant, the module records occupied crew seats. A retained identity includes the pointer, definition, entity ID and 16-bit actor route. A new borrow through `0xA7C990` must match before dispatch. The coordinator counts exiting walkers and active SmartRepair, Repair and KitExchange orders targeting the tank, including orders belonging to actors outside its roster. An owned repairer also occupies a slot between exit completion and the next order. Kits come from crew, the tank or unreserved ground items within 20 spatial units scaled by the live world length. The module can request a native one-kit transfer from a crew donor with a spare.

## Relocated native boundaries

All addresses are game RVAs. This extends the catalog's O-APPEND-SEQUENCE output contract with the ownership, continuation and replication rules for repair and boarding. The 25492057 maps supplied the starting points. [Decision catalog](decision-catalog-25492057.md) · [Repair tooling](repair-tooling-map-25492057.md) · [Vehicle brains](vehicle-brain-map-25492057.md) · [Boarding](boarding-map-25492057.md) · [Crew actions](crew-actions-map-25492057.md)

| Detour on 26734796 | Native contract | Module behavior |
| --- | --- | --- |
| `0x9159D0` Repair component picker, formerly `0x8FD1C0` | At `0x911980` the caller passes the address of its Repair pointer in RCX and stores the returned component at Repair+0xD0. Phase 3 sets component+0x25 bit 0 later. | Match the specific active Repair child and its owned SmartRepair parent. Prefer components outside other live same-tank orders' claims, then use any eligible non-busy component. Honor the native per-order rejection list. Resolve equal priorities in component vector order. |
| `0x9AC1A0` eEmitAndRepair finish, formerly `0x98DC50` | The stock callback installs SmartRepair and immediately queues Board. Walker+0x88 retains the callback. | Recognize the exact callback created for the crewman and defer the next order to post-quant. Other callbacks use the original function. |
| `0x913A20` SmartRepair Execute, formerly `0x8FB210` | On a completed child, Execute clears the child and calls the next-step function `0x913B40`. It passes a limit of two and the address of SmartRepair+0xC8 to `0x914E90`, formerly `0x8FC690`. Successful child completion preserves this cumulative counter. | Check authority and player intervention for the owned order. Let native End run. After termination, start a fresh SmartRepair if work and kits remain. |
| `0x0B5380` SmartRepair deleting destructor | SmartRepair vtable `0x10225B0`, slot 0. | Forget the owned order's identity on destruction. Component claims are reconstructed from live orders, so they expire with the child. |
| `0x9B0E80` stock single-repairer dispatcher, formerly `0x992950` | The vehicle interrupt at `0x9AD5D0` can initiate a stock crew exit ahead of the post-quant callback. | Suppress duplicate stock dispatch for a tracked, eligible repair episode, including during casualty backoff. |

For an occupied seat the vehicle owns a one-seat native Emit from `0x8FC550` with an eEmitAndRepair callback from `0x9AC630`. The module sets the selected short on this newly constructed order, binds its owner, executes its native Start once, then ends and releases it. EmitInstant and StartOut handle removal, traversal and MP emission. The boarder retains its own callback reference. For an idle ground crewman the module creates SmartRepair at `0x915070`, KitExchange at `0x915280`, or Board at `0x8FADC0`. The brain owns an installed root, and an idle native sequence owns an appended child. A qualified Mechanic uses the rate from the concrete ability record.

Repair Execute at `0x910D50` samples the remote-client and host-emission roles separately. In MP it constructs stock command **0x59**. Mode 0 carries the target actor route and rate. Mode 1 carries phase, component name and remaining time. On receipt, `0x0AF540` routes the update and `0x912640` resolves the host-selected component by name. Boarding reaches StartIn at `0x79F8E0`, which emits native command **0x34** on the MP host. The coordinator writes no component, kit inventory, seat occupancy or actor-position fields. Its policy makes no engine RNG draws. The native orders retain their usual RNG behavior.

## Authority and integration

The serial post-quant callback admits AI and player-owned tanks with concrete vehicle brains. Worker `0xA7A80` and remote client `0xC2B7A0` must both be false. Offline modes use the same admission path. Native wire code checks `0xC2B740`. Tank classification uses the Tank Crusher classes or definition tag and requires an actual track component. Destroyed bodies, excluded tags, burning, sinking and tilt beyond 70 degrees prevent dispatch.

The tank that owns Emit and every ground human receiving an order require zero inhibition and clear chassis and weaponry Direct Control gates. An occupied seat can itself contribute one inhibition count to the seated human. The placement handler at `0x94F990` toggles brain flag 19 and calls the counter updater at `0x91AB90`. Seat selection recognizes that single count only with flag 19, a matching live placement parent, and clear Direct Control gates. The seated human receives the repair order after exit. Ground crew retain priority for manual order bit 28, Hold Move, grenade avoidance and incapacitation. Bit 24 may be set during the module's own order. A manual override releases the assignment and starts a 25 s backoff. A repairer casualty delays replacement for eight seconds.

The loader calls `goh::tank_auto_repair::on_actor_post_quant(actor)` on a live actor immediately after its original slot 23 call. The module creates five repair-specific hooks through the supplied callbacks. Its tables hold 128 tanks and 32 crew per tank, with fixed-size identity and override tables indexed by actor route. Scene changes and clock rewinds invalidate the tables. On unload, deactivate the module, stop ingress from the shared actor callback and disable the five detours, drain in-flight calls, remove the hooks, then call `after_hooks_removed()`. The engine owns all installed orders and exit callback objects. The seven counters cover each dispatch scenario, emissions, initial ground assignments, chained repairs, Board submissions and picker overrides.

## Verification

The focused export reused the existing 26734796 Ghidra project. The verifier checked 81 function entries, 268 instruction locations, 16 RTTI tables, five prologues and five stock archive members. MinHook's x64 trampoline generator accepted the actual prologue bytes at every detour. The copied instruction lengths were 5, 7, 5, 5 and 5 bytes in the table's order. The offline check verified the jump back and detour relay destinations.

The MinGW C++17 probe instantiated hook preparation with MinHook and exported the post-quant and teardown entry points. It passed `-std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -fsyntax-only` and a static dependency link. PE import inspection found Kernel32 and UCRT API sets only. A separate sanitized fixture exercised 45 assertions against simulated actors and native call boundaries. It covered distinct simultaneous claims, both dispatch quotas, native-work accounting, broken-hull boarding deferral, continuation after trigger loss, kit return, personnel overrides, casualty backoff and hook rollback. [Validation results](../artifacts/build-26734796/tank-auto-repair-26734796-validation.json)

To repeat the structural check, use the game installation for `GAME_ROOT` and the repository root as the working directory.

```sh
build=26734796
map="research/artifacts/build-$build/tank-auto-repair-$build.json"
scratch="local-work/tank-repair"
exports="local-work/exports/build-$build"
mkdir -p "$scratch"
jq -r '.methods[] | [.entry_rvas.game, .id] | @tsv' "$map" > "$scratch/candidates.tsv"
python3 research/scripts/run_headless.py --build-id "$build" export --focused \
  --program call_to_arms.exe --candidates "$scratch/candidates.tsv" \
  --output "$exports/call_to_arms-tank-auto-repair"
python3 research/scripts/verify_fine_map.py --map "$map" \
  --binaries-dir "$GAME_ROOT/binaries/x64" --exports-dir "$exports" \
  --resources-dir "$GAME_ROOT/resource" --output "$scratch/fine-only.json"
```

Live verification still requires the exact-build SP, Skirmish and Conquest roles and a private MP host with a vanilla peer. Compare component names and timers, kit transfer, seat occupancy and player takeover across the delayed ejections and a sequence exceeding two repairs. Lobby admission and in-match checksum agreement remain open.
