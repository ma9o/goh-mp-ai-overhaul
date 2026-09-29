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
| Entity/component structure | Actor brain/sensor pointer, selected IDs/fields; 392 game and 391 editor native RTTI roots with base layouts | Name every AI-used component; world enumeration, actor IDs/flags, subtype/lifetime relationships |
| Sensor/perception | Four category vectors, subject insertion/event listener, scheduled visor/spatial scans, A/B inputs | Candidate evaluation, visibility producers, stale observations, retention and category changes |
| Relationships | ID comparison and matrix indexing | Matrix initialization, diplomacy names, ownership changes and brain flag 15 |
| Target choice | Picker and target-reference refresh | Eligibility, ranking, weapon suitability, reservation/resource handles |
| Human decision states | Numeric state rules and movement consumer | Every human state/context virtual method, interrupts and transition side effects |
| Cover | Eight terms, rank choice; common bounds/navigation/occupancy gates; object/raster/terrain sources; numerical kind descriptors | Name kinds/descriptor columns; edge/circle candidate geometry, ray-height detector, occupancy writers, cover entry/exit |
| Path/movement | Order creation and path-cost score | Path request/acceptance, stance/speed/chassis state, obstacles and motion completion |
| Weapon behavior | AttackRate estimates | Weapon selection, aiming, accuracy/sway, firing/reload, ammunition, ballistics inputs |
| Health/morale | Named morale dispatch and scalar update | Event IDs, damage/suppression propagation, health/stamina/ability calculations, recovery |
| Orders | Move/cover sequence construction and update | Full lifecycle, all AI action families, cancellation, rejection and ownership |
| Scheduling/time | Owner-grouped 0x50-byte callbacks, phase ordering, serial/parallel execution and wait; bucket-allocated task intervals; editor worker-mutation assertion | Quant-clock producer/pause/time scale, task skip count, context/RNG setup, entity lifetime through queued work |
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
[scheduling/sensors/cover sources](systems-map-25492057.md). Their open questions
are historical snapshots; this frontier tracks the continuing objective.

## Immediate static leads

- Perception: candidate evaluator `0x87AE10`, post-scan sensor slot 13
  `0x873B00`, subject-removal slot 20 `0x87A4D0`, additional scanner `0x879630`.
- Geometry: cover-kind parser `0x8A9720`; circle/edge producers
  `0x8A7140/0x8A73B0`; detector angle gate/ray walk `0x87D7E0/0x87DD70`.
- Relationships: all direct users of matrix `0x159BA10` and source matrix
  `0x159BA28`; diplomacy serialization and setters.
- Clock: direct writers of `0x1556E48`; scheduled-task capacity `+0x70`,
  skip count `+0x6C`, manager bucket creation.
- Components: connect actor defaults `0x157D860/0x157D4F0/0x157D910/0x157D0C0`
  with Fsm/Sensor/Weaponry/Cover construction, and trace actor subtype ownership.
