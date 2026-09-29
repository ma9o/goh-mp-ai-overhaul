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
| Entity/component structure | Constructor-backed Fsm/Cover/Brain/Sensor/Weaponry/Chassis pointers; direct component clone/load ownership; extender collection/cache/attach/clone/rebind; inventory queue, item/box/holder ownership, filling mass and stack limits; placer ownership/clone and physical link tree; deletion request→scene retirement→delivery callbacks→reference release→SoftLinkable invalidation; repair component policy; selected IDs/fields; 392/391 native RTTI roots plus dependencies | Component replacement/removal and factory completeness; enumeration/ID reuse/actor registry; deferred reference disposal; raw owner/holder/box replacement; derived holder behavior |
| Sensor/perception | Candidate commit/categories, memory/eviction, ally sharing, grid rays, unmask rules; Recon registration/waiting counts, six searcher slots,60-sector rings and renewal; all eight signal names; sound/action definition parsers and repeat publishing; injury/corpse/hit/death-cry producers; mission signal condition/reset | Sound source cancellation/lifetime, area registration lifetime; residual Recon completed-status consumers; grid/material construction/query state; environment factor; linked-actor type; unmask inventory/appearance/examine lifetimes |
| Relationships | Neutral/enemy/ally values, 18-player defaults, symmetric setter, scene diplomacy and multiplayer setup | Ownership changes and other brain flag15 producers (placement TurnOff.Targetable now bound); downstream relationship consumers |
| Target choice | Eligibility; ranking groups and exact predicate IDs; numeric keys; weapon/ammunition policy and multi-pass evaluator | Class/attacking/threating flag producers; all sorter branch equivalence; weapon target volume/ground resolution; reservation handles; other brain pickers |
| Human decision states | State hierarchy/dispatch/hazards; reviewed idle/battle/cover/search flow; stance thresholds, attack return codes, tactical movement, search coordination, healing and medic gating, following and weapon changes; detour pose/cover/point stages and budgets; temporary bone/pose trials; idle request gate; dropped-weapon memory, proficiency score and sort | Detour bound construction and pather route acceptance; animation state/tag producers and completion counter; full cover masks/healing validator; Veterancy property binding; full event producers; residual action-order helpers |
| Cover | Eight terms and rank; named kinds, source geometry, grid height query, occupancy claim/release, resident and in-progress destination lifecycle; discrete height probes, height/side-clearance kind selection, interval boundary bisection | Descriptor +8/+C consumer roles; full edge spacing and alternate offsets; geometry-query/grid construction |
| Path/movement | Move phase lifecycle, foot traversal, requested/actual stance/mode/speed, mode profiles, pather context read set, grenade escape planning | Path request/acceptance, grid costs/probes, terrain/animation application, movement enum binding, ability/health speed modifiers; crush/obstacle order builders |
| Weapon behavior | AttackRate; runtime/definition split; weapon selection; spread profiles/scales; motion accuracy/zeroing; named aim modes and ballistic solvers; burst sampler and stateful preview; adaptive geometry trace; live projectile fields/pool/flight and contact-to-damage bridge; TargetAccessor result gates; occupied-slot readiness; named crew slot flags, Sealed exposure, priority assignment and reservations; gunner/charger/commander/driver requests and minWorkers; cannon animation completion counter | Place/profile/owner lifetime; bone limit solvers and sway/reload; charger and animation producers; weaponry86 state; full contact/material/ricochet outcomes, guided steering and synchronization; other shooter types |
| Health/morale | Vitality attachment; raw/effective health and stamina; incapacitation/dying/final death/blinding flags and brain/sensor propagation; revival half-health; load-dependent stamina rates and inventory producer; Heal/Medic; damage subclass dispatch/formulas; first-eligible armor lookup and holder parser; armor thickness/overmatch/quality formulas; two-stage component damage; RepairTime/RepairOrder, countdown/restoration and repair command59; event-layer/extender dispatch; timed knockdown/recovery/serialization; Avoidance parameters and lookup | Material coefficient units, wound-list removal and component event names; remaining outer damage gates; concrete conditional/action-list execution; blinding producers; full throwoff simulation/landing; carried-patient predicate; replication thresholds; full avoidance consumers; morale/suppression links; medic statistics; melee defence/counterattack and blow effects |
| Orders | Move/cover sequence; base phase0/1/2 lifecycle and retry/cleanup; Attack equipment/range/detour/fire transitions, WeaponFire/WeaponAiming, Advance movement/engagement and subject marks, Fight selection/timing/blow application; vehicle allocation/root/emergency/Hideout/SmartRepair dispatcher; Repair/KitExchange/Collect/TakeItem/OrderTake and transfer/equip operations | Remaining action families; Advance byte39 reachability; controller/charger and body-turn helpers; Board/Bind/Emit now mapped; Repair phase6 reachability and group variants; scripted blow effects and melee defence/counterattack |
| Scheduling/time | Owner-grouped 0x50-byte callbacks, phase ordering, serial/parallel execution and wait; bucket-allocated task intervals; editor worker-mutation assertion | Quant-clock producer/pause/time scale, manager bucket creation, context/RNG setup, entity lifetime through queued work; skip setter now mapped |
| Group/vehicle behavior | Type roots; cannon fire/move/trailing states, role-index transfers, seat Disable and obstacle enable writers; place readiness/assignment; crew groups, sealing, entrance balancing, Board/Bind/Emit phases, traversal graph/animation/inventory, walker sync/cancel/load and occupant perception effects; six concrete exit callbacks; abandoned/ally_crew/any_crew/select bindings; boarder command reception; vehicle root/multiweapon allocation, gunner withdrawal, repairer selection, emergency triggers and basic airborne/animal roots; cannon command5E reception | Full squad/leader coordination, aircraft/helicopter chassis and animal scanners; exit source-pointer lifetime; full reservation/queue lifetime and derived chassis permissions, remaining availability flag producers; cannon matrix correction |
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
[crew placement/assignment/entity links](placement-map-25492057.md), and
[boarding/exit/traversal/crew collection](boarding-map-25492057.md), and
[crew roles/cannon states/permissions/exit actions](crew-actions-map-25492057.md), and
[vehicle weapon/root/withdrawal/repair policy](vehicle-brain-map-25492057.md), and
[component repair/tool acquisition/inventory operations](repair-tooling-map-25492057.md), and
[inventory queues/item ownership/scene retirement](inventory-ownership-map-25492057.md), and
[hand operations/holder callbacks/FSM request boundary](equipment-actions-map-25492057.md), and
[FSM routing/request expiry/equipment subsets/completion](fsm-routing-map-25492057.md), and
[expanded base human FSM graph/subset reachability](human-fsm-graph-25492057.md), and
[human FSM handler dispatch/action effects](human-fsm-actions-map-25492057.md). Their open questions
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
  vehicle response `0x98B200` now mapped (mandie triggers exposed-gunner withdrawal and pending passenger exit). Human direct brain callback `0x903690` is empty.
- Resistance/inventory: `0x975890/0x975CD0` named levels and first-applicable
  lookup `0x9744E0` mapped; follow ability info setup/level selection; drop-weapon/melee/fire consumers
  `0x936C60/0x95D820/0xA75CD0`. Inventory handle mass `0x7E2420` includes
  filling contents; filling tags/unit/capacity and item/world ownership are now mapped.
- Human states: detour stages `0x93ED30`, feasibility `0x940130`, pose helpers,
  idle request `0x95FCC0` and pickup score/sort now mapped. Continue bound
  intersection `0x6144E0`, network setup `0x8C6390/0x8C6400`, route refine sampler
  `0x8C76B0`, equipment replacement/lifetime around callback `0x939C10`→`0x95FAD0`.
  `0x938830` returns equipped-definition EC8 tags; `0x961710` updates finger
  animations, while pose cache `0x96A4B0` remains open. Handler424/42C have13 class/six modifier tag bindings and
  exact subset selection. Handler560 is native activeBlends, decremented by entity
  event6/subkind6; blend scheduling and synchronous completion now mapped. Continue
  concrete animation quant/cancellation and pending callback lifetime,
  healing-cover validator. Search cleanup flag `+3E1`
  has no set-side located in Execute. Healing-restore gate is confirmed in both binaries.
- Action orders: WeaponFire, Advance, Fight and Attack inner work now mapped in
  the engagement pass. Continue WaitReady, body-turn `0x8D68E0`, AttackProximityCharge,
  group effects and residual cannon helpers. Cannon fire/move/trailing, role transfers,
  seat enable/obstacle policy and animation completion are now mapped.
  Board/Bind/Emit now have phase, entrance,
  graph, walker, inventory/animation, cancellation and load maps, plus the
  six registered EmitTo/OnExit objects, access predicates and synchronization reception.
  Vehicle callback triggers, Hideout group cover distribution, SmartRepair dispatcher
  and ResetWaypointOnly destructor effects now mapped. Actual Repair, KitExchange,
  component duration/priority, Collect, TakeItem/OrderTake, transfer/equip
  operations and command59 are now mapped in the repair-tooling pass. Continue
  Repair phase6 setter (component-name lookup `0x895E40` now mapped), component/volume raw
  pointer lifetime, brain3E0 human deadline consumers and source-actor pointer lifetime. Advance byte39
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
  bypass cleanup. Request roles are gunner+2C8/charger+608/commander+948,
  chassis Driver+80; minWorkers parser and priorities are mapped. Cannon state writes
  slot Disable bit4 with trailing>move>attack role precedence. Follow weaponry+86,
  request replacement, other slot flags and animation/charger producers. Placer blocked+48 is now
  set by Emit mode3 execution and cleared by its destructor.
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
- Inventory: queue creation/update/blocking/cancel, action/sub-ID assignment,
  root token, all location representations, codec, direct-item reservations,
  cell capacity/stack mutation, item clone/materialization and retirement are now
  mapped. Transfer10 and cancel11 act during decode. Physical Mass differs from
  Inventory Weight. Item/world unlink allows visual retirement without item loss;
  repair's direct world deletion leaves item disposal to stuff-extender destruction.
  PrepareForItem, DropFromHand, PutFromRight/Left, Unmount, FreeHands, SwapHands and
  UseItem are now mapped, including hand coexistence tags, reservations, FSM waits,
  event3 transfer, holder attachment/equipment callbacks and command14 decode.
  Builder destruction auto-submits. GEM has extra transfer invariant diagnostics.
  Continue rearrangement sort `0x7DF600`, resource clone-policy,
  raw holder/box replacement and operation owner88/reentrancy. ItemDC meaning,
  `spread_items` scatter cap1480BC8 producers and inventory-kick command13 remain.
  `0x7D3620` requeues deferred events for absent child sub-IDs. Scene retirement
  `0xA1CC10` delivers mask7 before extender cleanup; general soft receivers only
  invalidate at final entity destruction. Follow actor registryA5DF80, ID reuse,
  deferred disposal652A10 and full entity spawn/build ownership.
- Animation/FSM: request parser (priorityCC, interruptC9, durationD0), callbackDC,
  tick-count expiry, six phases, readiness, shortest-edge route search, priority
  comparator, last-match subset rules, split/switch tables and handler barrier
  names now mapped. GEM adds changingState/current/OUTSIDE/activeBlends assertions.
  Base human graph now expands all16 resources,4 macro definitions/12 invocations
  into220 states/85 requests/1152 edges; native structural checks agree. Isolated
  request routes are modeled for all5 subsets, separating already-tagged disabled
  sources. `lie_revive` has no matching request tag; its implemented state carries
  `revive`, reachable from lying under that generic request. Trace special callers.
  All12 human handler families have195 exact name/part selectors matched against
  GEM and joined to220 base states. Entry/exit effects, reload scaling, stored
  throw variants, carried-corpse FSM changes, swimming inventory preparation and
  finger animation refresh are mapped. Continue pose/walk selection
  `0x96A4B0/0x973010`, idle/repair update, death animation feasibility/selection
  `0x938410/0x9648E0/0x9645D0`, concrete blend types and quant/cancel behind
  `0x5CD380`, actor1420 bit3/handler flags, mounted resource overrides,
  command2F/idle synchronization, serialization,
  handler/actor replacement lifetime, and tick callers/scheduling.
- Clock: containing global `0x1556E00`; manager `0x153BEE0`, bucket creation.
  Direct `0x1556E48` writes only found initialization, so follow parent aliases.
  Repair reads integer delta1556E4C (initializer3D770 sets1) and sentinel1556DE8
  (BSS, no direct WRITE xrefs). Full time producer/alias review remains necessary.
- Vehicle policy: `0x988FC0/0x989A80/0x98ABA0` candidate/weapon allocation,
  `0x98F0A0/0x990870` ordered interrupt/execute branches, `0x991C20` idle scanning,
  `0x781720/0x7819F0` actual exposed-to-internal gunner transfer,
  `0x992950` repairer selection and `0x8FB330` SmartRepair decisions now mapped.
  Last-enemy exclusion tag156FCC0=`airborne`, crew-sort median98C190 and retained
  swap98D6E0 are now mapped. Continue weapon definition1544/1548 timer names and
  full actor/brain factory selection. `eABS_Root` means
  airborne; `eAS_Root` means animal. Follow aircraft/helicopter chassis,
  misspelled `eAirboneBrainClient`, animal crush/obstacle scanners and
  definition tag15985D0. Shared idle-scan counter1484C00 warrants lifetime review.
- Components: mapped default/concrete clone/load/rebind for actor and extenders.
  Chassis replacement `0x851910` and sibling methods, extender release/removal,
  actor subtype and outer linked-owner lifetime changes remain. New placement pass
  maps entity+560 parent/bone/child component, transform following and unlink/rebind;
  SoftLinkable destruction nulls occupant receivers but not raw reservations. Follow
  all raw reservation/queue destruction paths, `0x778C40` alternate skeleton path,
  `0x779180/0x779E80` mode gates. Boarder graph uses attached entities; rebuilding
  stops active walkers. Walker records move on every Quant and cancellation.
  Follow node-definition skip+68 producer, graph/profile replacement lifetime,
  derived chassis slot36 access, inventory action sets, and skeleton-rebind phase producers.
  Access `0x7FA9D0` now binds abandoned/ally_crew/any_crew; selection binds select.
  Walker sync and StartIn/StartOut/move/collect/swap/cancel reception are mapped;
  routing IDs, delivery ordering and full failure behavior remain. Cancel releases
  reservations without stopping traversal or removing queued entrants.
  `0x8DF160` may omit approach points without pruning door names: trace
  failed-probe reachability before assuming candidate/name index alignment.
  OnExit registry `0x8E6370`, construction `0x8E4D60`, and virtual slots3/4/5
  are mapped for Point/Waypoint/OnAttack/OnSink/AndBurn/AndRepair. Position callbacks
  can consume RNG; source actor+28 is not a SoftLinkable receiver. Follow triggers,
  actor lifetime, and destruction around cannon shared-counter resets.
  GetStateForQuit `0x785F50` has stance/movement predicates.
  Finish copies brain low2 policy/player state and may transfer selection; follow
  ownership/selection gates and airborne/landing predicates.
