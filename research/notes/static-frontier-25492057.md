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
| Entity/component structure | Constructor-backed Fsm/Cover/Brain/Sensor/Weaponry/Chassis pointers; direct component clone/load ownership; extender collection/cache/attach/clone/rebind; inventory cache and nested weight chain; placer ownership/clone and separate physical link tree; SoftLinkable receiver invalidation; selected IDs/fields; 392/391 native RTTI roots plus newly followed dependencies | Component replacement/removal and factory completeness; world enumeration, actor IDs/flags, subtype/lifetime relationships; inventory holder/item ownership |
| Sensor/perception | Candidate commit/categories, memory/eviction, ally sharing, grid rays, unmask rules; Recon registration/waiting counts, six searcher slots,60-sector rings and renewal; all eight signal names; sound/action definition parsers and repeat publishing; injury/corpse/hit/death-cry producers; mission signal condition/reset | Sound source cancellation/lifetime, area registration lifetime; residual Recon completed-status consumers; grid/material construction/query state; environment factor; linked-actor type; unmask inventory/appearance/examine lifetimes |
| Relationships | Neutral/enemy/ally values, 18-player defaults, symmetric setter, scene diplomacy and multiplayer setup | Ownership changes and other brain flag15 producers (placement TurnOff.Targetable now bound); downstream relationship consumers |
| Target choice | Eligibility; ranking groups and exact predicate IDs; numeric keys; weapon/ammunition policy and multi-pass evaluator | Class/attacking/threating flag producers; all sorter branch equivalence; weapon target volume/ground resolution; reservation handles; other brain pickers |
| Human decision states | State hierarchy/dispatch/hazards; reviewed idle/battle/cover/search flow; stance thresholds, attack return codes, tactical movement, search coordination, healing and medic gating, following and weapon changes; detour pose/cover/point stages and budgets; temporary bone/pose trials; idle request gate; dropped-weapon memory, proficiency score and sort | Detour bound construction and pather route acceptance; animation state/tag producers and completion counter; full cover masks/healing validator; Veterancy property binding; full event producers; residual action-order helpers |
| Cover | Eight terms and rank; named kinds, source geometry, grid height query, occupancy claim/release, resident and in-progress destination lifecycle; discrete height probes, height/side-clearance kind selection, interval boundary bisection | Descriptor +8/+C consumer roles; full edge spacing and alternate offsets; geometry-query/grid construction |
| Path/movement | Move phase lifecycle, foot traversal, requested/actual stance/mode/speed, mode profiles, pather context read set, grenade escape planning | Path request/acceptance, grid costs/probes, terrain/animation application, movement enum binding, ability/health speed modifiers; crush/obstacle order builders |
| Weapon behavior | AttackRate; runtime/definition split; weapon selection; spread profiles/scales; motion accuracy/zeroing; named aim modes and ballistic solvers; burst sampler and stateful preview; adaptive geometry trace; live projectile fields/pool/flight and contact-to-damage bridge; TargetAccessor result gates; occupied-slot readiness; named crew slot flags, Sealed exposure, priority assignment and reservations | Place/profile/owner lifetime; bone limit solvers and sway/reload; charger and animation producers; request role/count parsers and blocking byte; full contact/material/ricochet outcomes, guided steering and synchronization; other shooter types |
| Health/morale | Vitality attachment; raw/effective health and stamina; incapacitation/dying/final death/blinding flags and brain/sensor propagation; revival half-health; load-dependent stamina rates and inventory producer; Heal/Medic; damage subclass dispatch/formulas; first-eligible armor lookup and holder parser; armor thickness/overmatch/quality formulas; two-stage component damage; event-layer/extender dispatch; timed knockdown/recovery/serialization; Avoidance parameters and lookup | Material coefficient units, wounds/repair and component event names; remaining outer damage gates; concrete conditional/action-list execution; blinding producers; full throwoff simulation/landing; carried-patient predicate; replication thresholds; full avoidance consumers; morale/suppression links; medic statistics; melee defence/counterattack and blow effects |
| Orders | Move/cover sequence; base phase0/1/2 lifecycle and retry/cleanup; Attack equipment/range/detour/fire transitions, WeaponFire/WeaponAiming, Advance movement/engagement and subject marks, Fight selection/timing/blow application | Remaining action families; Advance byte39 reachability; controller/charger and body-turn helpers; complete vehicle/cannon/group variants; scripted blow effects and melee defence/counterattack |
| Scheduling/time | Owner-grouped 0x50-byte callbacks, phase ordering, serial/parallel execution and wait; bucket-allocated task intervals; editor worker-mutation assertion | Quant-clock producer/pause/time scale, manager bucket creation, context/RNG setup, entity lifetime through queued work; skip setter now mapped |
| Group/vehicle behavior | Type roots; selected cannon orders; place readiness/assignment; crew groups, sealing, boarding reservations and occupant perception effects | Squad/leader coordination, vehicle/airborne brains, mounted weapons; Board order/walker, cancellation, remaining availability flag producers |
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
[state hierarchy/hazards/movement](movement-map-25492057.md), and
[infantry policy/search/healing](human-states-map-25492057.md), and
[entity components/vitality/signals](entity-health-map-25492057.md), and
[signals/damage/resistance/load](signals-damage-map-25492057.md), and
[armor/component damage/interruption events](armor-events-map-25492057.md), and
[infantry geometry/firing trials/equipment choice](human-geometry-map-25492057.md), and
[engagement orders/readiness/melee](engagement-orders-map-25492057.md), and
[aiming/ballistics/projectile lifetime](ballistics-map-25492057.md), and
[crew placement/assignment/entity links](placement-map-25492057.md). Their open questions
are historical snapshots; this frontier tracks the continuing objective.

## Immediate static leads

- Entity/health: armor helpers, holder `+770/+774` parsers and component health
  transitions now mapped; `+758/+760` corrected to volume tags, equipped item `+3D8`.
  Follow material `+E0` parser, wound `0x7EA860/0x7EB780`, component event IDs
  `+CC/+CE/+D0` producers, outer damage cover/distance `0xA5AA50`, blinding callers,
  and limits-sort partition. Interaction list `0x761B50` calls slot2 of `+10`
  subobject; resolve its concrete action/conditional implementations. Knockdown
  expiry `0x939EC0`/recovery `0x93ABE0` now traced; continue FSM `0x95FC00`,
  throwoff trajectory `0x93BDD0`, motion `0x93DCE0`, and landing callbacks.
- Signals: named type/action/sound parsers and scene condition now mapped.
  Follow source pointer ownership/cancellation in `0x4D2810/0x4D1D20`, area lookup
  `0x9D2020`, receiver threshold binding; remaining perception publishers and
  vehicle response `0x98B200`. Human direct brain callback `0x903690` is empty.
- Resistance/inventory: `0x975890/0x975CD0` named levels and first-applicable
  lookup `0x9744E0` mapped; follow ability info setup/level selection; drop-weapon/melee/fire consumers
  `0x936C60/0x95D820/0xA75CD0`. Inventory handle weight `0x7E2420` includes
  secondary definition/count; resolve identity and lifetime.
- Human states: detour stages `0x93ED30`, feasibility `0x940130`, pose helpers,
  idle request `0x95FCC0` and pickup score/sort now mapped. Continue bound
  intersection `0x6144E0`, network setup `0x8C6390/0x8C6400`, route refine sampler
  `0x8C76B0`, animation tag/handler `+424/+42C` producers and `+560` decrement,
  healing-cover validator. Search cleanup flag `+3E1`
  has no set-side located in Execute. Healing-restore gate is confirmed in both binaries.
- Action orders: WeaponFire, Advance, Fight and Attack inner work now mapped in
  the engagement pass. Continue WaitReady, body-turn `0x8D68E0`, AttackProximityCharge,
  boarding factory `0x8E3B00`, cannon states/crew and group effects. Advance byte39
  is cleared at start; its set-side and state2 reachability remain open.
  Melee loader/parser `0x95ED00/0x95D230`, variant lookup, damage/knockdown
  calculation and animation timing now connected; follow defence/counterattack,
  stroke-mask `0x977D90`, animation completion and blow event action lists.
- Movement: path request `0x8C8F60`, route probe `0x8AE780`, moving-entity
  registry `0x8AA360`; profile initialization/config; low-ratio speed modifier
  reads `0x925010/0x924F90`, abilities `0x936580/0x9744E0`.
- Target selection: class at actor +0xDE8; subject +0x14 bits7/8 writers;
  `0x913230/0x912EB0` ground/volume resolution; sorter partition/heap branches.
- Weapons: aim modes `direct/curved/adaptive/nocurve`, solvers `0x5E8290/0x5E7E80`,
  arc length `0x5E8050`, sampler `0x818A10` burst/anisotropic/target-plane branches,
  context commit and adaptive trace `0x83A460..0x83B040` now mapped. Preview sampler
  consumes RNG and can reset burst state. Continue bone limits/constraints
  `0x5C5760/0x5C5400/0x5C5F50`, geometry solver `0x823470`, aimer+11C/+48 producers,
  charger+10/+14 and slot1 object+DC, animation firing transitions, other shooters.
  Controller `0x781D50` readiness and `0x781F60` assignment feasibility now mapped;
  `Entity::ePlacer` binding, slot bit20=Sealed, priority assignment and Include/Exclude
  side effects are bound. Feasibility mutates temporary flags and some early exits
  bypass cleanup. Follow request role/count parsers (weapon place+2C8/+608/+948,
  chassis+80), placer blocked+48 producer, other slot flags and sync reception.
  Projectile initialization, pool, manager, deferred retirement, FalloffMeter/
  FalloffStrength and live contact bridge are now mapped; continue precise
  ATGM steering `0x833DE0`, ricochet material tables and perturbation, all contact
  outcomes `0x836B40`, synchronized terminal flags and definition/owner lifetime.
  Broad/narrow collision `0xA21E50/0xA21EB0/0xA21F50/0x795270/0xA24080`, contact
  sorting `0x839630/0x83BE10`, owner-specific region `0xA27FD0/0xA26850` remain.
- Perception: signal-type publishers, eReconBase zone bookkeeping, unmasking
  helper `0x7C97E0` and appearance/examine object lifetime.
- Geometry: cover kind chooser and discrete height/interval helpers now mapped;
  finish edge spacing `0x8A73B0`, route clearance `0x8AC3F0`, geometry ray
  `0xA23D60`; grid exclusion `0x882C20/0x882C70` and construction;
  environment query `0x6F44B0` and class bindings.
- Clock: containing global `0x1556E00`; manager `0x153BEE0`, bucket creation.
  Direct `0x1556E48` writes only found initialization, so follow parent aliases.
- Components: mapped default/concrete clone/load/rebind for actor and extenders.
  Chassis replacement `0x851910` and sibling methods, extender release/removal,
  actor subtype and outer linked-owner lifetime changes remain. New placement pass
  maps entity+560 parent/bone/child component, transform following and unlink/rebind;
  SoftLinkable destruction nulls occupant receivers but not raw reservations. Follow
  `Order::eBoard`, boarder walker/cancellation, `0x78C750/0x785720`, raw reservations
  on destruction, `0x778C40` alternate skeleton path, `0x779180/0x779E80` mode gates.
  Placer blocked+48 literal writer lead `0x72AEA0` was an unrelated parser object;
  `0xAEC8C0` belongs to an interface action path, not established as a placer setter.
