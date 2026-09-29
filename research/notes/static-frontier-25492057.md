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
| Sensor/perception | Candidate collection/commit, category rules, visibility and last-known memory, eviction, ally sharing, rule predicates and grid rays | Signal publishers, auxiliary observation manager, accumulator producer, grid/material construction and query state, reveal table loader, environment factor, linked-actor type |
| Relationships | Neutral/enemy/ally values, 18-player defaults, symmetric setter, scene diplomacy and multiplayer setup | Ownership changes and brain flag 15; downstream relationship consumers |
| Target choice | Picker and target-reference refresh | Eligibility, ranking, weapon suitability, reservation/resource handles |
| Human decision states | Numeric state rules and movement consumer | Every human state/context virtual method, interrupts and transition side effects |
| Cover | Eight terms and rank; named kinds, source geometry, grid height query, occupancy claim/release, resident and in-progress destination lifecycle | Kind selection inside 8A8040; descriptor +8/+C consumer roles; height sampling and precise edge spacing |
| Path/movement | Order creation and path-cost score | Path request/acceptance, stance/speed/chassis state, obstacles and motion completion |
| Weapon behavior | AttackRate estimates | Weapon selection, aiming, accuracy/sway, firing/reload, ammunition, ballistics inputs |
| Health/morale | Named morale dispatch and scalar update | Event IDs, damage/suppression propagation, health/stamina/ability calculations, recovery |
| Orders | Move/cover sequence construction and update | Full lifecycle, all AI action families, cancellation, rejection and ownership |
| Scheduling/time | Owner-grouped 0x50-byte callbacks, phase ordering, serial/parallel execution and wait; bucket-allocated task intervals; editor worker-mutation assertion | Quant-clock producer/pause/time scale, manager bucket creation, context/RNG setup, entity lifetime through queued work; skip setter now mapped |
| Group/vehicle behavior | Type roots only | Squad/leader coordination, vehicle/airborne brains, mounted weapons and crew |
| Configuration/scripting | Named mode/ratio fields, score registry | All AI-readable properties, entity definitions, abilities, mission controls and current script-facing APIs |
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
[perception/memory/entity inputs](perception-map-25492057.md). Their open questions
are historical snapshots; this frontier tracks the continuing objective.

## Immediate static leads

- Target selection: `0x912AE0`, `0x9136E0`, `0x957C00/0x957C70`, brain target refresh
  `0x943560/0x9437A0/0x944070`; weaponry, attack-order and accuracy RTTI roots.
- Perception: accumulator `0x891C00`, auxiliary manager `0x88BE20`, signal-type
  publishers, enemy visibility notification `0x9036A0`, reveal configuration loader.
- Geometry: cover kind chooser `0x8A8040`, height helpers `0x8A7C10/0x8A7E40`;
  grid exclusion `0x882C20/0x882C70` and construction; environment query `0x6F44B0`.
- Clock: containing global `0x1556E00`; manager `0x153BEE0`, bucket creation.
  Direct `0x1556E48` writes only found initialization, so follow parent aliases.
- Components: concrete attachment and release of defaults now identified at
  actor `+DB8/+DC0/+DD0/+DD8/+DE0`; actor subtype owner/lifetime changes.
