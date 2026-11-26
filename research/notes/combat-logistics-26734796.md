# Combat logistics and vehicle dismounts

Steam build `26734796`, `call_to_arms.exe`, SHA-256 `81a13de787c1869b766004cdcadb9ff99012f0b1b58b89db9a7741b10b9f059a`. **runtime_allowed: false**. The evidence is static analysis of the game executable and offline tests. All addresses below are RVAs in that executable. Distances in metres use the simulation's spatial scale.

## Hook budget and authority

| Entry | Role | Whole instructions relocated |
| --- | --- | --- |
| `0x95CEA0` | Human brain's native weapon-choice update | 5 bytes, `48 89 5C 24 10` |
| `0x79D3C0` | Boarder's exit-door choice | 7 bytes, `48 8B C4 48 89 58 10` |

The DLL stages 73 hooks. The grenade check shares the existing release at `0x8313B0`. Transport unload and squad delivery run in the existing serial actor update. The latter runs between medic and resupply processing. All five features have independent switches. The host and simulation-thread checks, concrete brain vtables, owner and Direct Control checks apply before policy changes or order submission. Gameplay outputs use the existing host brain, shooter, boarder and inventory paths.

## Weapon swap and grenade release

The native chooser at `0x95CEA0` checks the right-hand missile definition through `0x95E590` and can stow it when the deadline at `brain+0x3A4` expires. A recently observed operational enemy vehicle within 60 m can extend the deadline by 2.5 s. The observation comes from the soldier's committed sensor. Pure smoke keeps its stock timer.

The stock weapon take writes a 5–10 s lockout at `brain+0x47C`. Armor newly relevant to a soldier with a carried anti-tank weapon, or infantry facing a soldier with a launcher or dry gun, can bring a pending deadline forward. A dry rifle or MG at close range can yield to a loaded pistol or SMG. The engine still makes its own choice and builds its own order. A private one-second throttle limits repeat attempts. The swap counter requires a fresh `eWeaponTake` in the output context at `+0x08` (vtable `0x110AA28`). Stock stow and ground-item retrieval can also return success.

At release, an AI throw that belongs to the close-grenade module keeps its existing tracked-shot checks. For a vanilla throw, an active `Order::eAttack` in the brain’s current order chain (Attack or Attack Ground) or manual weapon mode (`int32` at `weapon+0x208` equals 1) passes straight to the stock release. This preserves player intent even at a delayed animation marker on the established serial simulation thread.

For autonomous throws, the guard checks the throwing shooter and resolves the right-hand definition through `0x7E04E0`. A point aim comes from `weapon+0xD4`, and an entity aim uses the target at `weapon+0xE0`. The guard can suppress known lethal payloads, including incendiaries and white phosphorus, when the thrower, allied infantry or a vulnerable vehicle is inside the effect envelope plus 0.5 m of clearance. Smoke screens retain the stock release path. [Close-grenade contract](close-grenade-26734796.md)

## Vehicle exits and passenger unload

The boarder root is resolved by `0x79D790`. The seat and place node use `0x799150` and `0x79E930`. Root doors occupy `+0xA8..0xB0` at stride `0xF8`. A candidate needs a valid bone, graph connectivity through `0x79E8A0/0x79EDE0`, a free reservation, an expired failure deadline and a successful native obstacle check. The candidate point comes from the updated bone at `+0xCC`.

The obstacle helper at `0x8C1AD0` samples the engine RNG on the stock three-trial path. The adapter constructs the stock passenger path context at `0x8DF010`, passes the bone point, negative exit normal, native range and trial count 3, then releases the context through `0xBF640`. It runs that path once per stock candidate in stock order. When fewer than two usable exits survive the checks, it returns the stock result from that same pass. It ranks eligible exits using the direction away from threats observed by the vehicle or allies within 45 m. Observations must be visible or at most 10 s old and within 120 m of the vehicle. The selected native door feeds the boarder emit and StartOut commands `0x31` and `0x32` via `0x797B60`.

The native caller at `0x9AD5D0` constructs an `eEmitOnAttack` callback with `0x9AC4E0(vehicle)`, an emit order with `0x8FC550(-1, 1, callback)`, and installs it through the brain's `+0x68` virtual slot. The first argument is the count, the second the mode. Mode 1 selects passengers. The group selection at `0x8FCCD0` uses the engine's crew flag, so the adapter checks the people the native order could actually move before issuing it.

Burning, broken body or broken engine can trigger unloading. A stopped or immobilized transport can also unload when a nearby anti-tank contact is actively threatening it or the brain's threat deadline is active. A new unload requires an idle order path, eligible passengers, an unlocked placer and no exit already under way. Successful dispatch has a 15 s vehicle cooldown. Battle tanks and crewed guns are outside this policy. Engine emergency crew evacuation remains available through its own path.

## Squad transfers

An idle human can seek a donor every 0.75 s, within 18 m by default. Both participants yield to player orders and higher-priority work. The sharing priority is 35, above flag resupply (30) and below vehicle repair (40) and medical work (50). The receiver stays in position. The donor can transfer within 3.5 m or approach through the native move-task factory `0x90A8C0`. A delivery leg has a 12 s deadline. A nearby enemy or player interruption can end the job. Hold Position donors record their post for the return trip.

Ammunition must match the receiver's weapon through `0x565A50(item_def+0xEC8, gun_def+0x120)`. The normal need threshold is half a magazine, with a minimum of one round. Rocket and single-shot needs require empty reserves. Same-squad donors take precedence, then dedicated ammunition carriers, then distance. A carrier can give up to two loads from an available stack. A donor using the same ammunition must retain a full load. A soldier below 70% health with no bandage can receive one from a donor with at least two, or three for a medic.

The transfer uses the same native Xchange builder and destructor watch as Auto-Medic. The amount is supplied to `0x7E1D50`. The donor and receiver locations go into the `0x7DF910` builder and are submitted by `0x7DFBF0`. The native inventory action owns command `0x10`. The receipt requires the marked action to drain and the receiver’s loose stack total to increase by the donated amount. Delivery does not write item counts or health. The receiver cooldown is 10 s after a successful share and 5 s after failure. Scene change or a backwards simulation clock invalidates private bookkeeping. [Inventory and watched-order contract](auto-medic-26734796.md)

## Verification

The Wave 4 fixture exercises the two hook adapters, vehicle and inventory policy, and native-call substitutes. The existing grenade fixture covers release clearance. Architecture tests cover switch parsing, search-radius bounds and priority. The optional MinHook fixture checks both new trampoline cuts and jump-back addresses. Shutdown reports `fastSwp`, `mslHold`, `ffVeto`, `safeDr`, `trUnld`, `sqAmmo` and `sqBand`. Live host/client verification of the choices, wire commands, interruption and unload on this fingerprint remains pending. [Wave 4 fixtures](../tests/test_wave4_combat_logistics_26734796.cpp) · [Grenade fixtures](../tests/test_close_grenade_26734796.cpp) · [Runtime protocol](../compatibility/runtime-smoke.md)
