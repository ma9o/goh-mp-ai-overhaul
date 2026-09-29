# Static AI/entity investigation frontier

User objective: exhaust productive static mapping of AI behavior and the entities
and state it reads. This work remains **active**. This workflow uses file analysis;
runtime experiments and game modifications remain separate from the static map.

A type/entry inventory is discovery, not semantic coverage. A topic can close
only when its relevant decision branches, data producers, consumers and object
relationships have evidence, or the remaining questions have an explicit reason
why static evidence cannot answer them. Indirect calls, unrecognized leaf code,
data-driven registries and helper routines are leads to investigate, not automatic
runtime boundaries. Common allocation/container/math dependencies can be bounded
after their behavior and effect on the AI contract are established.

## Worklist

| Area | Current evidence | Open static work |
|---|---|---|
| Entity/component structure | Constructor-backed Fsm/Cover/Brain/Sensor/Weaponry/Chassis pointers; selected IDs/fields; 392/391 native RTTI roots | Concrete component attachment and replacement; world enumeration, actor IDs/flags, subtype/lifetime relationships |
| Sensor/perception | Candidate commit/categories, visibility memory/eviction, ally sharing, grid rays; unmask accumulator/rules; eReconBase registrations; reveal loader and reciprocal visibility | Signal publishers; Recon zone bookkeeping; grid/material construction/query state; environment factor; linked-actor type; unmask inventory/appearance/examine helper lifetimes |
| Relationships | Neutral/enemy/ally values, 18-player defaults, symmetric setter, scene diplomacy and multiplayer setup | Ownership changes and brain flag 15; downstream relationship consumers |
| Target choice | Eligibility; ranking groups and exact predicate IDs; numeric keys; weapon/ammunition policy and multi-pass evaluator | Class/attacking/threating flag producers; all sorter branch equivalence; weapon target volume/ground resolution; reservation handles; other brain pickers |
| Human decision states | State hierarchy, attach/activate/deactivate, ordered interrupts, root hazards, saved-order restoration; initial idle/battle/search branch review | Remaining per-state helpers and event producers; healing/following/boarding; active branch reachability; full search coordination |
| Cover | Eight terms and rank; named kinds, source geometry, grid height query, occupancy claim/release, resident and in-progress destination lifecycle | Kind selection inside 8A8040; descriptor +8/+C consumer roles; height sampling and precise edge spacing |
| Path/movement | Move phase lifecycle, foot traversal, requested/actual stance/mode/speed, mode profiles, pather context read set, grenade escape planning | Path request/acceptance, grid costs/probes, terrain/animation application, movement enum binding, ability/health speed modifiers; crush/obstacle order builders |
| Weapon behavior | AttackRate; runtime/definition split; weapon selection; spread profiles/scales; motion accuracy producer; zeroing after-shot producer; firing/projectile-creation path | Place attachment/lifetime; detailed aiming/sway/reload; full sampler and ballistics; projectile collision/damage; actual weapon safety/readiness helpers |
| Health/morale | Named morale dispatch and scalar update | Event IDs, damage/suppression propagation, health/stamina/ability calculations, recovery |
| Orders | Move/cover sequence; base phase0/1/2 lifecycle and retry/cleanup; attack outer lifecycle and WeaponAimTarget | Internal attack states; all AI action families; concrete cancellation/rejection and ownership |
| Scheduling/time | Owner-grouped 0x50-byte callbacks, phase ordering, serial/parallel execution and wait; bucket-allocated task intervals; editor worker-mutation assertion | Quant-clock producer/pause/time scale, manager bucket creation, context/RNG setup, entity lifetime through queued work; skip setter now mapped |
| Group/vehicle behavior | Type roots only | Squad/leader coordination, vehicle/airborne brains, mounted weapons and crew |
| Configuration/scripting | Named mode/ratio fields, score registry; exact-shape property-token catalog (1229 game/1254 editor); reviewed combat property registries | Remaining AI-readable properties, entity definitions, abilities, mission controls and current script-facing APIs; token discovery is not parser coverage |
| Cross-binary comparison | Selected game/editor contracts | Compare newly reviewed contracts; distinguish outlining/inlining and behavior differences |

## Working method

1. Catalog RTTI roots and vtable slots in both binaries, preserving fingerprints.
2. Trace callers, callees, field writes and configuration bindings for each topic.
3. Record fine-grained contracts and unresolved dependencies in focused manifests.
4. Use assembly and independent disassembly to challenge decompiler assumptions.
5. Validate evidence, update this worklist, and commit completed investigation passes.

Existing completed passes: [initial structure](build-25492057.md),
[brain fields/states](fine-map-25492057.md), [input-to-order flow](flow-map-25492057.md),
 [combat estimates/cover scoring](tactics-map-25492057.md), and
[scheduling/sensors/cover sources](systems-map-25492057.md), and
[perception/memory/entity inputs](perception-map-25492057.md), and
[target/weapon/accuracy/unmasking](combat-map-25492057.md), and
[state hierarchy/hazards/movement](movement-map-25492057.md). Their open questions
are historical snapshots; this frontier tracks the continuing objective.

## Immediate static leads

- Human states: cover battle `0x948160`, outdoors battle helpers `0x952AB0`,
  `0x9537C0/0x9538A0/0x954B40/0x954F80`, search `0x94DD50` and Recon
  `0x88AEA0/0x88B110/0x88B170/0x88B660`. Healing restore `0x9520B0` has
  an address-equals-negative-offset gate requiring a reachability/editor audit.
- Movement: path request `0x8C8F60`, route probe `0x8AE780`, moving-entity
  registry `0x8AA360`; profile initialization/config; low-ratio speed modifier
  reads `0x925010/0x924F90`, abilities `0x936580/0x9744E0`.
- Target selection: class at actor +0xDE8; subject +0x14 bits7/8 writers;
  `0x913230/0x912EB0` ground/volume resolution; sorter partition/heap branches.
- Weapons: sampler `0x818A10` ballistic/burst branches, projectile constructor
  `0x8321F0`, initialization `0x8327B0`; readiness `0x8174D0/0x8107D0`;
  attack inner states `0x8ED670/0x8EC470` and aiming `0x81F030`.
- Perception: signal-type publishers, eReconBase zone bookkeeping, unmasking
  helper `0x7C97E0` and appearance/examine object lifetime.
- Geometry: cover kind chooser `0x8A8040`, height helpers `0x8A7C10/0x8A7E40`;
  grid exclusion `0x882C20/0x882C70` and construction; environment query `0x6F44B0`.
- Clock: containing global `0x1556E00`; manager `0x153BEE0`, bucket creation.
  Direct `0x1556E48` writes only found initialization, so follow parent aliases.
- Components: concrete attachment and release of defaults now identified at
  actor `+DB8/+DC0/+DD0/+DD8/+DE0`; actor subtype owner/lifetime changes.
