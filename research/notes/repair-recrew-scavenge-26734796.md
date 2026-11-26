# Field repair, heavy-weapon recrewing and scavenging on build 26734796

`runtime_allowed: false`

Steam build `26734796`, x64 `call_to_arms.exe`, SHA-256 `81a13de787c1869b766004cdcadb9ff99012f0b1b58b89db9a7741b10b9f059a`. Static inspection and offline fixtures cover these behaviors. Live multiplayer replication and private-lobby smoke scenarios remain pending.

## Execution and authority

Wave 6 adds zero hooks. The four translation units still stage 79 in total, with one coordinator, 23 combat, 46 infantry and nine tank hooks. The existing serial actor callback observes crew after the vehicle’s native update. Infantry post-quant processes medic and squad work, recrewing, resupply, scavenging, then tactical objectives. Actor identities are rechecked between calls that can synchronously change the scene.

The host checks the exact build, concrete human or vehicle brain, simulation thread, owner, inhibition counter and Direct Control before issuing work. Existing player orders take priority. Modules request native orders and inventory operations. The code consumes no custom engine RNG draws. The default-on switches are `nontank_auto_repair`, `recrew_weapons` and `battlefield_scavenge`.

## Non-tank vehicles and guns

The existing repair state machine admits repairable ground vehicles and deployed crewed guns. It checks the concrete vehicle brain, upright orientation, vehicle condition, manual control and class-7 exclusion at `0x855D60`. Fire, destruction, abandonment and the opt-out tag exclude a vehicle. The original `tank_auto_repair` switch controls tanks independently.

| Vehicle and damage | Crew response |
| --- | --- |
| Tank with a broken body, or a broken track and weapon damage | Existing full-hull or turret-and-track repair path |
| Truck, armored car or halftrack with a broken body | Full repair, including previously observed crew who were forced out |
| Motor vehicle with weapon, engine, track or wheel damage | Component repair when the crew can safely dismount |
| Crewed gun with weapon, shield or wheel damage | Component repair, including guns without a separate body row |

A visible enemy within 25 m prevents a non-tank crew from dismounting for component repairs. A two-person crew can keep its working gun manned while a loader repairs. The native picker prioritizes guns, turrets, mantlets, engines, then running gear before other broken or damaged components. Accepted work uses native Emit (`0x8FC550`), SmartRepair (`0x915070`) and Board (`0x8FADC0`). [Repair state machine](tank-auto-repair-26734796.md)

## Same-player recrewing

A crew watch distinguishes a death or disappearance from a surviving soldier stepping off the gun. Loss to combat permits immediate replacement. A confirmed live exit blocks new volunteers for 30 simulation seconds. The exception applies only to a departing crewman assigned to repair that same gun. If another crewman leaves during the repair, the 30-second timer still starts. An owner change or a recycled entity invalidates the old watch.

A volunteer and the gun must belong to the exact same player. The target must be an upright, deployed `Entity::eCannonExtender` (`0x110F040`) with a usable gun, ammunition or an eligible supply zone, and an unlocked crew seat. A decrewed gun may have lost its operable bit while retaining its owner. A player move, a repair episode, an active exit walker, a tow, fire, a wreck or a nearby visible enemy blocks new boarding. The soldier must be idle, with no competing support task. Medics and soldiers carrying specialist weapons keep their roles.

Move at Will searches up to `recrew_search_radius_m` (28 m by default, 8–50 m). Hold Position caps the distance at 8 m. The enemy clearance is 14 m around the volunteer and gun. Jobs reserve individual seats, prefer the gunner, and limit recruitment to the vacancies needed for a two-person crew. `0x8FADC0(gun, seat_name, 0)` copies the selected seat name into the Board order at `+0x158`. The human brain’s slot 13 (`+0x68`) owns the resulting order. Seating is the success receipt. A failed attempt uses a ten-second backoff and an outstanding claim has an 18-second deadline.

## Ground weapons and corpse inventory

After other support work, an idle soldier can search every 1.5 seconds within `scavenge_search_radius_m` (16 m by default, 6–35 m). A visible enemy within 18 m of either end blocks a new errand. A wholly disarmed or dry soldier first looks for a usable firearm. A regular rifleman can take a loaded MG or anti-tank weapon. A wounded soldier without bandages can take up to two from a body. A soldier low on ammunition can take up to two magazine loads of compatible loose rounds. Hold Position records the starting point for a watched return. Success gives a 15-second cooldown, failure eight seconds, and each leg has a 14-second timeout.

**Weapon pickup.** The stuff extender must refer back to the unheld ground entity. Its item must meet the weapon and ammunition checks. An occupied right hand first goes through the native `PutFromRight` inventory operation. The hand-location constructor is `0x7E1A40`, the operation builder is `0x7DF910`, and `0x7DFEE0` binds the location. Submission at `0x7DFBF0` emits the host’s inventory command (`0x10`). The builder and location are destroyed at `0x7DFE00` and `0x7E1EC0`. The native inner step at `0x7E9970` takes a drop branch for an item whose FSM type is `hand-only`. Such equipment is excluded. The regular unmount event at `0x7E9CC0` calls `0x7DCAB0`, which requires the item’s owning box. The scavenger checks that the old item is still in that box before continuing. The scavenger waits for the inventory queue to clear and for a confirmed empty hand before issuing `0x8E2910(ground_entity, 0)`. The stock Collect order approaches the entity and submits `PickupToRight` at `0x8E3980`. It records the action ID at order `+0x48` and sets stage `+0x50` to 1. In that stage the engine can reserve or unlink the ground entity. The scavenger waits for the exact item to appear in hand and the stock order to finish before counting the pickup or returning to post.

**Corpse transfer.** Both scavenging paths require final death (vitality `+0x174`, bit 0). The stock predicate at `0x379260` uses bits 1 and 2 for a revivable patient, so testing either low bit would let a scavenger loot an unconscious soldier. The corpse retains the inventory box and its items. On this build the box owner is `box+0x50`. The scavenger approaches within 3.5 m through a watched MoveTask (`0x90A8C0`). An explicit quantity and source cell are passed through the existing exchange builder. For corpse loot the builder’s preferred owner at `+0x80` is the living scavenger. At `0x7DFBF0` that field becomes the second argument to `0x7E6CA0`, which writes it to `operation+0x88`. The submitter then looks up that entity’s inventory manager at `entity+0x578 → +0x28`, queues the operation and emits `eInventoryTransferCommand` under the host gate. The transfer watch tracks the exact operation and action ID on that manager. The scavenger’s increased inventory count confirms delivery after the operation drains. Ordinary donor exchanges keep their existing owner. [Inventory ownership map](inventory-ownership-map-25492057.md)

The exact PE’s MSVC RTTI identifies the Collect complete-object vtable at `0x1109BB8` (locator `0x12A0628`, `.?AUeCollect@Order@@`) and the stuff extender at `0x10FD200` (locator `0x12990D0`, `.?AVeStuffExtender@Entity@@`). Both are verified in the shared build table.

## Coordination and verification

Recrewing has coordinator priority 32, between squad logistics (35) and resupply (30). Battlefield scavenging has priority 28, between resupply and tactical infantry (20). Squad logistics continues to search living donors, ground boxes and vehicles. When its fallback selects a corpse inside the battlefield search radius with both features enabled, the battlefield policy can first consider a usable dropped weapon. The two searches respect active source reservations. Tank facing, movement recovery and support work use the same entity-scene pointer for coordinator leases, so a vehicle update preserves a volunteer’s reservation until its release, preemption or scene change.

The shutdown summary counts non-tank repair dispatches (`repNonTank`), confirmed boardings (`recrew`), successful firearm pickups (`rearm`, `heavyPickup`) and confirmed corpse transfers (`corpseLoot`). The new jobs stop accepting work during shutdown and honor outstanding order and transfer watchers. [Wave 6 fixture](../tests/test_wave6_repair_recrew_scavenge_26734796.cpp) · [Build and verification commands](../../DEVELOPMENT.md)

All 19 offline C++ suites passed under Clang ASan and UBSan, including the trampoline and tactical adapter fixtures. All 61 Python tests and the full C++ lint gate (`--all-files --werror`) passed. Both x64 DLLs built. The unified DLL exported `ShutdownAndUnload`, and the version proxy exported all 17 expected functions.
