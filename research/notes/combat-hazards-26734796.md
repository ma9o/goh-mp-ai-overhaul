# Combat orders and battlefield hazards on build 26734796

`runtime_allowed: false`

**Target** is the x64 `call_to_arms.exe` with SHA-256 `81a13de787c1869b766004cdcadb9ff99012f0b1b58b89db9a7741b10b9f059a`. This contract covers static inspection and offline fixtures. Live host, remote-client and multiplayer replication checks remain pending.

## Hook selection

All addresses are RVAs in the fingerprinted executable. The copied lengths include the complete instruction crossing MinHook’s five-byte threshold.

| Entry | TU | RVA | Copied bytes | Length |
| --- | --- | --- | --- | --- |
| Advance engagement update | Combat | `0x8E79F0` | `48 89 5C 24 08` | 5 |
| Attack tick | Combat | `0x904930` | `48 89 5C 24 10` | 5 |
| World signal publication | Combat | `0xA7B5F0` | `48 89 5C 24 08` | 5 |
| Crush scanner update | Infantry | `0x91F2C0` | `41 57 48 81 EC D0 01 00 00` | 9 |
| Crush order builder | Infantry | `0x91EA20` | `48 8B C4 48 89 48 08` | 7 |
| Generic root execution | Infantry | `0x96E260` | `40 55 57 48 8D AC 24 F8 FC FF FF` | 11 |

At `0x96E260` the first three instructions are `push rbp`, `push rdi` and `lea rbp,[rsp-0x308]`. Each relocates without a relative branch or RIP-relative operand. The soldier wrapper at `0x96FB50` calls the crush, obstacle, grenade, burn and out-of-bounds scanner updates, then checks the brain timer and calls this one-argument root function. Applying priority at the inner call preserves all five native updates and their order. The root hook checks for the concrete soldier root before touching its scanner flags.

The build table verifies these additional complete-object vtables against the PE’s MSVC x64 RTTI.

| Type descriptor name | Vtable | Complete object locator |
| --- | --- | --- |
| `.?AUeAdvance@Order@@` | `0x110A070` | `0x12A09E0` |
| `.?AVeCrushScannerVehicle@Actor@@` | `0x1110BC0` | `0x12A4800` |
| `.?AVeHS_RootSoldier@Human@@` | `0x1110CD0` | `0x12A4B10` |

## Advance and explicit Attack

- **Advance.** The original updater runs first. For infantry held at engagement level 2, the adapter waits at least 1.5 seconds after the halt and two seconds after combat activity, and leaves an executing child alone. It checks the selected subject’s life, visibility, last sighting and weapon feasibility. A non-cannon vehicle can leave level 2 after three seconds without fire once both active aim and the committed visible in-range threat list are clear. A cannon can leave level 1 after its deadline and four quiet seconds. Only the order’s engagement level at `+0x40` changes. The parent’s existing transition resumes the move.
- **Feasibility and RNG.** The infantry branch of `0x8E7560` reaches aim-point and pose trials, including RNG calls beneath `0x958ED0`. Repeating it would change the simulation’s random stream. The adapter uses committed target/weapon state and the existing deterministic terrain and solid-volume query. For a concrete vehicle cannon, the stock distance comparison uses the first subject in the vehicle list and a strict cutoff of 75% of weapon range. The adapter uses the range already committed at `brain+0x14C`.
- **Fog of war.** The existing Attack fire check at `0x9032F0` still owns the timeout. The adapter records a live actor and target identity on an actual player sighting through `0x809B30`. While that pair remains valid it can lend the native check a refreshed timestamp at `order+0xEC`, then restores the true timestamp. At 14 seconds since the last sighting, the native check sees the original expired timestamp. Death, destruction, replacement, scene changes and a backwards clock invalidate the extension. Ground-point attacks have no entity to track.
- **Close combat.** For a visible live enemy soldier within 6 m, the adapter lends the original Attack tick a deadline one second in the future at `order+0xF8`. Once stock returns, the expired deadline is available again, so losing the firearm or the target immediately restores normal handling. The selected rifle, SMG, pistol, revolver or MG must belong to the actor and pass the stock empty and dummy-charger checks at `0x820CE0` and `0x820C50`. Reloading, launchers, melee weapons and dry firearms keep native handling. The original tick executes once, including its early broken-volume and reachability decisions. The loaded-firearm path takes the existing native shooting branch.

## Grenades and heavy explosions

- **Trapped grenade.** The existing avoid-grenade tick at `0x916570` attempts escape first. If escape fails while the grenade is still armed or hot and the soldier remains in danger, native `SetMovementType(chassis,1,0)` requests prone, then `eAvoidGrenade::Hide` at `0x917420` requests the grenade-alert FSM. Its host path emits `eFsmRequestCommand` (`0x2F`). A private episode record lets stock maintain Hide on subsequent ticks for this soldier, order and grenade.
- **Blast signal.** The four-argument signal publisher forwards the original call exactly once. A mutex-protected 64-entry ring captures finite type-5 explosions of radius at least 15 m on the serial host, excluding client-local sources. Records expire after two seconds. Scene changes and a backwards clock clear private state. Publication records observations only.
- **Hold Position.** On serial human post-quant, an idle soldier can react to a blast up to one second old, inside three quarters of its radius, capped at 22 m. A visible enemy within 10 m, a move or support task, reloading, water, existing prone posture or Direct Control prevents the reaction. The native chassis setter at vtable offset `0x170` requests prone. Hold-stance recovery respects the reaction for 4.5 seconds, subject to a close enemy within 7.5 m. Each soldier has a ten-second cooldown.

## Vehicle clearance and hazard priority

- The native crush update runs first. A private, identity-bound record supplements its threat vector when a moving ground vehicle’s projected corridor crosses the soldier. The query includes allied and manually driven vehicles and uses actual velocity, including reverse motion. The velocity helper at `0x73A2A0` returns displacement per tick, converted using the simulation time scale. The corridor considers speeds of at least 1.2 m/s, separation up to 12 m, vertical clearance under 3 m and up to 2.5 seconds or 10 m of travel. It includes the vehicle radius, 0.5 m for the soldier and a margin of 0.85 m for allies or 1.5 m otherwise.
- The vehicle index caches up to 512 identities per tick and rechecks motion for each observer. A bounded scene walk handles index overflow. The sidestep evaluates up to eight simultaneous corridors and yields when that limit is exceeded. A stock-populated vector goes through the original builder. For a fresh custom threat, a deterministic perpendicular sidestep uses a native sequence (constructor `0x918560`) with movement-change (`0x90ABF0`), move-task (`0x90A8C0`) and terminal brain-update (`0x8E2C50`) children, appended through `0x919270`. Ownership, order installation and replication stay with the engine. Scanner activity and the private episode carry the custom decision without a direct write to the chassis or a mutation of the engine’s vehicle vector.
- With an active grenade, the soldier root temporarily suppresses nearby-fire avoidance when the soldier is clear of active burning and the obstacle scanner is inactive. A grenade within 6 m also takes priority over crush avoidance unless a threatening vehicle is already within 3.5 m. The original root executes once and the hook restores each masked activity byte before returning.

## Authority, lifecycle and verification

The crush scanner remembers each soldier’s issued escape order and destination. For 2.5 seconds it preserves that order while the destination is outside every current threat lane. A replacement normally waits at least 1.5 seconds. Only a vehicle within its hull radius plus 1.2 m can shorten that interval to 0.5 seconds. Scene changes and clock rewinds clear the records.

The grenade escape order holds its movement child at `+0x30`. The stock order executor at `0x916100` advances that child. Subsequent calls through the hooked inner tick at `0x916570` can continue the stock escape. A soldier in cover can plan an escape when the child is absent and the same order, owner and grenade have gone 0.8 seconds without a plan. Projectile observations cover every player even during single-side benchmarks.

The four independent, default-on INI switches are `advance_responsive`, `attack_order_polish`, `hit_the_dirt` and `crush_avoidance`. Decisions run under the verified host and serial simulation context. The receiving actor must have a live concrete brain and pass the ownership, inhibition and Direct Control guards. A manually driven vehicle is observed as a hazard to an independently authorized infantry actor. Replication uses stock movement, stance, FSM and order boundaries. Custom decisions draw no engine RNG.

The unified DLL stages 79 hooks across coordinator (1), combat (23), infantry (46) and tanks (9). The six new detours participate in the global in-flight drain. Private state is released after hook removal. The shutdown counters are `advUnfrz`, `atkPrst`, `atkNoMel`, `trapHide`, `hitDirt`, `crshEsc` and `hazPrio`.

The offline suite covers authority failures, stock forwarding, time and distance boundaries, simultaneous hazards, rollback and the six raw PE entries through MinHook’s trampoline builder. [Wave 5 fixture](../tests/test_wave5_combat_hazards_26734796.cpp) · [Verification commands](../../DEVELOPMENT.md)

On this fingerprint, all 18 C++ suites passed under Clang ASan and UBSan, including all six Wave 5, both Wave 4 and both AT trampoline checks. All 61 Python tests and the full `--all-files --werror` C++ lint gate passed. The unified x64 DLL exported `ShutdownAndUnload`, and the x64 proxy exported all 17 expected version functions.
