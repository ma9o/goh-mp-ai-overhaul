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
| Human decision states | State hierarchy/dispatch/hazards; reviewed idle/battle/cover/search flow; stance thresholds, attack return codes, tactical movement, search coordination, healing and medic gating, following and weapon changes; detour pose/cover/point stages and budgets; circle-bound construction and RNG sampling, graph generation/processed-node reachability; temporary bone/pose trials; idle request gate; dropped-weapon memory, proficiency score and sort | Network construction, remaining geometry dependencies; animation state/tag producers and completion counter; full cover masks/healing validator; Veterancy property binding; full event producers; residual action-order helpers |
| Cover | Eight terms and rank; named kinds, source geometry, grid height query, occupancy claim/release, resident and in-progress destination lifecycle; discrete height probes, height/side-clearance kind selection, interval boundary bisection | Descriptor +8/+C consumer roles; full edge spacing and alternate offsets; geometry-query/grid construction |
| Path/movement | Move phase lifecycle, foot traversal, requested/actual stance/mode/speed, mode profiles, pather context read set/copy ownership, grenade AreaOutside solver dispatch; graph search/heap/node marks, bounds clamp/cost toggles, edge cost formula, route reconstruction outputs and distance sampling; RNG-consuming cost probe; named profile/obstacle parsers, 12 native terrain IDs, 116 base profiles and21 obstacle types; vault geometry and PlayAnimation handoff; line/point costs and RNG, portal pruning, owner metadata snapshots, spatial enabled masks and collection; grid search, seven goal types, fallback rules, parent tracing, smoothing and shortcut settings; planar/volume shape ABI and predicates, polygon features, compound ownership and closest-result state; loaders, entity transform paths and bounded authored-shape census; zone ownership, spatial ID reuse, insertion/removal, overlap events and asynchronous queue execution | Graph construction; packed slope and remaining obstacle metadata producers; spatial seed initialization, overlap consumers, shape producers and rectangle/polygon decision regions, outer worker teardown and raw-pointer lifetime; terrain ratio/priority consumers, movement enum binding, ability/health speed modifiers; profile reload lifetime; remaining PlayAnimation handler/request/command and crush builders |
| Weapon behavior | AttackRate; runtime/definition split; weapon selection; spread profiles/scales; motion accuracy/zeroing; named aim modes and ballistic solvers; burst sampler and stateful preview; adaptive geometry trace; live projectile fields/pool/flight and contact-to-damage bridge; TargetAccessor result gates; occupied-slot readiness; named crew slot flags, Sealed exposure, priority assignment and reservations; gunner/charger/commander/driver requests and minWorkers; cannon animation completion counter | Place/profile/owner lifetime; bone limit solvers and sway/reload; charger and animation producers; weaponry86 state; full contact/material/ricochet outcomes, guided steering and synchronization; other shooter types |
| Health/morale | Vitality attachment; raw/effective health and stamina; incapacitation/dying/final death/blinding flags and brain/sensor propagation; revival half-health; load-dependent stamina rates and inventory producer; Heal/Medic; damage subclass dispatch/formulas; first-eligible armor lookup and holder parser; armor thickness/overmatch/quality formulas; two-stage component damage; RepairTime/RepairOrder, countdown/restoration and repair command59; event-layer/extender dispatch; timed knockdown/recovery/serialization; Avoidance parameters and lookup | Material coefficient units, wound-list removal and component event names; remaining outer damage gates; concrete conditional/action-list execution; blinding producers; full throwoff simulation/landing; carried-patient predicate; replication thresholds; full avoidance consumers; morale/suppression links; medic statistics; melee defence/counterattack and blow effects |
| Orders | Move/cover sequence; base phase0/1/2 lifecycle and retry/cleanup; Attack equipment/range/detour/fire transitions, WeaponFire/WeaponAiming, Advance movement/engagement and subject marks, Fight selection/timing/blow application; vehicle allocation/root/emergency/Hideout/SmartRepair dispatcher; Repair/KitExchange/Collect/TakeItem/OrderTake and transfer/equip operations | Remaining action families; Advance byte39 reachability; controller/charger and body-turn helpers; Board/Bind/Emit now mapped; Repair phase6 reachability and group variants; scripted blow effects and melee defence/counterattack |
| Scheduling/time | Owner-grouped 0x50-byte callbacks, phase ordering, serial/parallel execution and wait; bucket-allocated task intervals; editor worker-mutation assertion; animation manager pending/active membership, scene phase and immediate request-side Quant; clock/delta producer, pause tokens, explicit stepping, slowdown stack, deadline codecs, default/selected/synced RNG context | Frame event-router/worker lifecycle, registered timer registration/unlink, slowdown guard and packet producers; manager bucket creation, entity lifetime through queued work; default/entity RNG disposal and scope completeness; full deferred mesh drain; skip setter now mapped |
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
[human FSM handler dispatch/action effects](human-fsm-actions-map-25492057.md), and
[animation control/completion/frame markers/entity motion](animation-control-map-25492057.md), and
[pose/walking/idle profiles and human animation declarations](human-animation-profiles-map-25492057.md), and
[scripted idle policy/native conditions/interaction list execution](idle-actions-map-25492057.md), and
[binary animation timelines/bone tracks/resource and marker audit](animation-resources-map-25492057.md), and
[human marker actions/condition inputs/sound perception bridge](marker-actions-map-25492057.md), and
[deferred interactions/context/cleanup/extender dispatch](interaction-lifecycle-map-25492057.md), and
[sound-folder inheritance/asset RNG/channel lifetime/base sound audit](sound-definitions-map-25492057.md), and
[human marker path expressions/branch guards/inherited signal join](marker-sound-paths-25492057.md), and
[sound loop owners/alias movement/deferred FX queue/event names](sound-owners-map-25492057.md), and
[clock/pause/slowdown/timer codecs/RNG context](clock-map-25492057.md), and
[detour bounds/network reachability/route and cost inputs](path-acceptance-map-25492057.md), and
[path profiles/obstacle names/terrain IDs/vault animation orders](path-profiles-map-25492057.md), and
[line costs/portal pruning/spatial owner snapshots](path-lines-map-25492057.md), and
[grid search/goal callbacks/route refinement](grid-search-map-25492057.md), and
[planar shape predicates/polygon features/compound ownership](spatial-shapes-map-25492057.md), and
[volume predicates/closest-result state/compound traversal](spatial-volumes-map-25492057.md), and
[shape loading/entity updates/authored geometry](volume-producers-map-25492057.md), and
[area lifetime/overlap events/worker queues](area-lifecycle-map-25492057.md), and
[scene spatial state/automatic obstacles/contact dispatch](area-scene-map-25492057.md), and
[collider profiles/entity contacts/trace volume queries](entity-contact-map-25492057.md), and
[physical contact queues/batch reduction/geometry helpers](physical-response-map-25492057.md), and
[contact iteration/body inputs/convex support/GJK](contact-solver-map-25492057.md). Their open questions
are historical snapshots; this frontier tracks the continuing objective.

## Immediate static leads

- Entity/health: armor helpers, holder `+770/+774` parsers and component health
  transitions now mapped; `+758/+760` corrected to volume tags, equipped item `+3D8`.
  Follow material `+E0` parser, wound `0x7EA860/0x7EB780`, component event IDs
  `+CC/+CE/+D0` producers, outer damage cover/distance `0xA5AA50`, blinding callers,
  and limits-sort partition. Interaction list `0x761B50` now resolves to embedded
  eComplexAction+10; ordered children, first-match If/else, the concrete idle
  conditions and SetIdleAnimation are mapped. Continue other concrete action and
  condition implementations used by damage/marker events. Knockdown
  expiry `0x939EC0`/recovery `0x93ABE0` now traced; continue FSM `0x95FC00`,
  throwoff trajectory `0x93BDD0`, motion `0x93DCE0`, and landing callbacks.
- Signals: named type/action/sound parsers and scene condition now mapped.
  Sound wrappers/alias handle replacement, publication gates and explicit-stop
  repeat cancellation are mapped; destroy:ge event3 reaches handler clear7676B0,
  stopping linked alias handles and their repeats. Folder inheritance4CF490,
  path/asset selection4D3BC0/4CF9F0 and channel retirement4D2E00 are now mapped.
  Channel48 points to asset0 then the folder's inline14/18 type/radius, correcting
  the earlier marker map.548 base folder declarations/551 hashed text resources
  join20,883 WAV entries in2,943 immediate directories. Gear-first normal/fast
  folders inherit movement8; playback volume does not scale AI signal radius.
  Multi-asset directory choices consume the default thread RNG with a no-repeat cycle;
  this bypasses the selected context pointer (5549F0 versus554B50), as established
  by the clock/context follow-up, correcting the earlier current-context label.
  explicit files and one-asset directories skip that draw. Pause does not itself
  erase repeats; retirement removes the matching repeat before handle reuse.
  Six direct loop owners now mapped in both binaries: Medic, Speaker and two
  SnipingAimer paths pass null AI source; aliases pass context owner; scene queue
  passes raw row38. The only located queue producer is eFXEntity vslot32 and
  forces one-shot. Entity vslot33 updates alias sound positions from owner74.
  Speaker cleanup uses entityevent6, not an explicit stop in its C++ destructor.
  FB3/FB4 are SceneDestroy/SceneInit; FD2/FD3 are Quant/NonPausedQuant;
  FD7/FE4 are Pause/MuteVolume. Ordinary executor publishes FD3 outside the pause
  gate; explicit-step65F3D0 invokes FD2 without a local pause gate.
  Follow FX198 source writers/lifetime and initialization callers, alias callback
  reachability/replacement, default-loudness108
  relocation and mounted folder/cache lifetime. The marker path-expression join
  now resolves147 potential sound rows/64 normalized paths; all three speed/sand
  paths plus gear_rattle/lying lack a base asset. Movement start_sound integer1
  remains one-shot; only fast breath is explicitly looped, with no base AI signal.
  Native handling of unused omitted macro arguments and wider alias bindings remain
  open. Continue
  area lookup
  `0x9D2020`, receiver threshold binding; remaining perception publishers and
  vehicle response `0x98B200` now mapped (mandie triggers exposed-gunner withdrawal and pending passenger exit). Human direct brain callback `0x903690` is empty.
- Resistance/inventory: `0x975890/0x975CD0` named levels and first-applicable
  lookup `0x9744E0` mapped; follow ability info setup/level selection; drop-weapon/melee/fire consumers
  `0x936C60/0x95D820/0xA75CD0`. Inventory handle mass `0x7E2420` includes
  filling contents; filling tags/unit/capacity and item/world ownership are now mapped.
- Human states: detour stages `0x93ED30`, feasibility `0x940130`, pose helpers,
  idle request `0x95FCC0` and pickup score/sort now mapped. Bound helper6144E0,
  network setup8C6390/8C6400 and route sampler8C76B0 are now mapped: sampling
  bound is not exact lens, point sampler uses two RNG draws, and detour requires
  current-generation nodes with popped bit21. Continue reconstruction shortcut/
  portal dependencies and equipment replacement/lifetime around callback
  `0x939C10`→`0x95FAD0`.
  `0x938830` returns equipped-definition EC8 tags; `0x961710` updates finger
  animations; pose cache `0x96A4B0`, all pose class branches, walking `0x973010`,
  health/subject flag refresh, idle/repair RNG and deadlines are now mapped. Handler424/42C have13 class/six modifier tag bindings and
  exact subset selection. Handler560 is native activeBlends, decremented by entity
  event6/subkind6; blend scheduling and synchronous completion now mapped. Continue
  handler callback reentrancy and replacement lifetime (concrete animation
  Quant/cancellation/destruction differences now mapped),
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
- Movement:8C8F60 is AreaOutside task vslot2, choosing network/grid by profile
  F0bit0; grenade uses it. Network search8B3F60, temporary bound-cost toggles,
  route output XY+cost and reverse distance sampling are now mapped.8AE780
  copies the cursor, draws RNG per sampled segment step and returns floatcost;
  Move consumes kind/object results. Terrain cost reads cell terrain/slope classes;
  object policy reads identity/masks/profile/type flags. Edge8B9670 and bound8BD570
  are now mapped. ProfileF8/type40 loaders bind Network/Human/BattleCrusher/
  SkipImpregnable and five obstacle flags, with116 profiles/21 base obstacle
  descriptions. Terrain cost uses1/sqrt(K)-1; negative obstacle declarations
  leave the prior/default100000. Native terrain IDs0..B are bound byA44070.
  Move8F5E70 resolves to vault geometry and getover_middle PlayAnimation order;
  938410/938520 use a separate human_flying line probe and optional temporary
  obstacle disable/re-enable. Continue packed slope producers, profileA0 ratio/F4
  priority consumers, registry order and reload lifetime, spatial registration
  and chassis68 lookahead. PlayAnimation
  executor8D3070 is mapped; follow handler95FC40, request157EBD0, command53
  reception and Move1AF reset.
  Line8AC950/8ACA00, blocked8AC3F0 and point8AC2B0 now mapped: objects first,
  terrain samples every length/16 with selected-RNG jitter; zero-length terrain
  contributes0; combined cost is unclamped and kind is not a complete threshold
  encoding. GEM player bounds8CF350 alwaysfalse; game reads player record1E1.
  Portal fraction/cost/chord helpers and modern/old pruning branches now mapped.
  Spatial owner snapshot records destroyed tag, ID/player/chassis flags, but
  retains raw link28; queued move clears entity98. Enable refreshes leaf category
  and enabled signbit. Query argument context78 is forwarded but ignored; actual
  filter is query34=40. Spatial775430/60CF30, insertion/removal and area ID reuse are now mapped.
  8BE7A0 is the worker getter; execution is8BE020. Continue seed storage setup,
  overlap-list consumers, entity revisions and outer worker shutdown; player1E1/entity1C0
  mask18000 producers and raw-link lifetime remain leads.
  Grid8AFA00 now maps16-unit8-neighbor search, packed node flags and strict-g
  relaxation; terrain max sample times length plus object cost, one lateral zero
  offset. Temporary cache uses64-unit cells and raw areas. Seven goal types have
  constructors/callbacks/destruction mapped. Nearest fallback is conditional;
  stoptrue differs from outputsuccess. ManyPoints has no direct constructor code
  refs in the census; pendingcostC initialization/indirect callers remain.
  Reconstruction marks success before exact endpoint clearance, traces directions
  untilg0; smoothing/pruning preserve old rowcosts. g_linearPathThresholdHuman
  initial1.05 versus nonhuman Vehicle1.30, selected byHuman flag; dbg_pathSmooth0.
  Planar616100 pair dispatch, slots1/2/5/9/10/11/12/13 and polygon feature
  layout/walk/ownership now mapped. OBB/capsule four-axis projection admits a
  separated corner; Box2/capsule horizontal edge clamps use height, which can
  reject a nonsquare-box overlap. Both have static numeric examples and paired
  assembly evidence; native intersection is not universally exact. Polygon move
  leaves sourcecount intact after clearing vector pointers. Compound owns child
  pointers and caches conservative bounds; append uses radiusR1+R2+distance,
  with shared global scratch. Compound15/15 intersection is unsupported.
  Volume612720 now maps kinds20..24, slots1..9, fixed geometry/transform/bounds
  and compound traversal. Box/capsule and box/lozenge pairs are unsupported;
  compound24/24 is supported. Point predicates are strict, pair thresholds inclusive.
  Rectangle-point609330 stores both parameters but uses the first on both axes;
  lozenge sphere612340 uses max half-side rather than half diagonal. Static cases
  show predicate disagreement and a core corner outside the returned sphere.
  Shared result147CF30 has partially written parameters; box cases leave it stale.
  Compound scans all children as(other,child), retaining minimum native scalar,
  without checking the state byte; primitive/compound orientation can differ.
  Compound clone resets stored position, while copying world geometry children.
  Dynamic613CF0 snapshots through slot2 into shared fixed-shape scratch.
  Ten shape loaders/factories and27 shape-token bindings perbinary now mapped.
  EntityArea uses generalfactory;Obstacle uses planar. CompoundPosition onlystores
  bookkeeping;LozengeCenter isorigin, OBBaxisnotnormalized. Polygon6073B0 appends
  edges/reversesallifanynegative turn;GEM assertscount>=3,neither checksconvexity.
  Census13archives/71,888members:12,732OBB,464Polygon,34Circle syntacticshapes;
  14beneath##ancestors haveunresolvednativeenablement. No literal3Dshapes inthat
  scope;fourPolygon declarations havemixedturns (Pavlov2,Ruin13,BeachCliff2).
  Dynamiccompound StoreAsStatic doesno geometrywrites ingame;GEM unreachablediagnostic.
  No directcallers ofdynamicintersection;otherconcretewrappersnotidentified.
  Entity721180 selects722750translation or7215E0matrix byrecordflags. Translation
  scalesOBBaxisandextents, leavesSphereradius, shiftsPolygonDEST ratherthansource;
  matrixdiffers. Compoundsleavecachedradius;CompoundArea translationfallsonto
  unsupportedBox2cache, matrixrebuildsboxfromcircle. Threeexactnumericcasesrecorded.
  Placement986F70 readsworking/sourcegeometry,rawactor/chassis56 andowner98.
  Continue record72FB00/setters,771580/7716C0/771760registration,scalar6FD580,
  revisions/absolute-vs-delta assumptions,986DB0/983930placement andA22AD0query.
  Full6094F0/60A010 rectangle regions and608380/608B20 polygon clipping/degeneracy,
  authoredmixed-turn cases, scratch aliases/serialization andcacheinvalidation remain;
  full AreaInside task8C8CB0 line/fallback,
  goal128 and output111 consumers, graph creation and worker/cache lifetime.
  Moving-entity registry8AA360, low-ratio speed modifier925010/924F90 and
  abilities936580/9744E0 remain leads.
- Clock/context: core65F440 increments48 by4C beforeFD2 even during explicit
  paused steps; normal583250 gates it with pausecount0/flags4. FD3 sits outside
  that gate. Mainframe57B750 derives/clamps delta1..100 after slowdown division,
  but nonzero override154C158 bypasses calculation and clamp. Slowdown request
  rows18 use last-token precedence, per-quant steps and no empty-stack factor
  reset. Deadline relative codecs and scenequant/speed serialization are mapped.
  Follow registered list6601D0 (separate1556E60, not quant58), event router6532B0,
  guard15A4A80 writers, multiplayer frame packet producers, default RNG registry
  cleanup and all entity RNG scopes. Context push/pop switches selected stream
  only on changed boolean; default asset getter5549F0 bypasses selected554B50.
  65E710 converts its argument, correcting the sniping cue's former clock label.
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
  finish edge spacing `0x8A73B0`, shape intersection `0x616100/0x612720`, geometry ray
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
  finger animation refresh are mapped. Concrete sequence/blend/aimset/blendspace/UV
  control, completion/loop/idle protocol, variant RNG, frame markers, resumable
  retention, cancellation/destruction and scene animation scheduling are now mapped.
  Sequence movement can write entity50..7C; movement is not purely visual.
  Marker event5 reaches the interaction definition's marker-name map48;
  completion event6/subkind2 uses animation-name map60. Pose/walk selection
  `0x96A4B0/0x973010` and idle/repair update are now mapped, including variation
  counter side effects, AI/player/manual shoot_walking tags, health<0.3 and
  callback4's health-bit16 gate (not target bit14). The three base animation text
  resources yield1545 unique declarations/790 event rows;123 of125 selector names
  match. Sequence file/directory lookup and explicit registration are now mapped;
  request lookup does not automatically register missing files, and observed
  explicit insertion is nested blendspace registration. `stand_idle_empty` and
  `stand_idle_strap_4` remain unmatched in the base declarations/archive scope;
  mounted overrides and upstream registration context remain open.
  Base human select_idle is now expanded (2 macros/8 invocations,70 conditions,
  13 else branches,21 animation names); script uses numbered empty idles but
  still names unmatched strap4. Every predicate/action in that script now has
  native field/RNG evidence: low health tests idle mode, stuff tests right-hand
  definition tags, even one-name choice consumes RNG. Continue global1483008
  bypass writer, event-layer/dynamic receiver lifecycle, action registry1571B18
  teardown and actor3A bit0/RNG context producers. Event execution selects native
  contexts named synced_global/entity_local and restores the previous context.
  EANM headers/FRMN/FRM2, first-frame pose-track binding, sparse carry-forward,
  visibility/mesh actions, placement binding and rate normalization50/14776C8 are
  now mapped. All1545 declarations join1292 files under explicit base-prefix
  assumption.1606 human ANM scanned:1605 parse,77 sparse, one empty; two smoke
  files update initially unbound tracks. The three anomalous files are not joined
  by these declarations.27 marker rows lack stored coordinates (26 out of bounds,
  one sparse pistol prone_to_stand); board_plane_right has two markers at100,
  while dispatch chooses one sorted match. Sorter5D5540/44C9C0 preserves equal
  keys for arrays<=32, resolving this nine-row declaration to metal_footstep
  before jump; the duplicate jump row is not dispatched through this path.
  Larger sort branches remain open where relevant.31 base human marker handlers
  now have literal call/macro closure; most play sound, while jump reaches eject.
  Sound/tags/call/view/delay contracts and velocity/terrain/rain/season/altitude
  inputs are mapped. GEM season predicate is alwaysfalse, unlike game; terrain
  height returnsXMM0 despite empty game decompilation. Quant766FE0 now maps
  effect-before-action batches; callbacks mutate live vectors, so cancellation
  cannot find still-detached rows. Context copy owns tokens only; frozen bit16
  does not retain entities. Thaw rebinds volume/component by name. Named cancel
  does not release captures in its local path; full clear does. GEM handler
  destruction requires calls.empty(). Dynamic receiver766ED0 uses4-byte ID/index
  rows and extender slot8, with light/shield/cannon/structure registrations mapped.
  Continue context serialization/raw references, global scratch recursion and
  concurrency, receiver rebuild on extender replacement, full registry reload,
  light79F5E0/79F870/79FE90, shield8A/8B/982A10, structure callbacks,
  spawn7538F0, physical throw/landing and handler bit0 consumption. Four base
  catalog markers lack handlers in human.inc; inherited layers remain open.
  Rotation uses bone-map ordinal as track index and a shared
  binaryED once gate; four base rotation declarations have matched ordinal0 or
  absent body, so no observed ordinal mismatch there. Continue mount precedence,
  directory ordering and shared-resource lifetime. Revolute binding50 now has
  paired start/stop events: slot22 estimates steps as abs(228-200)/1F8 (equality
  returns1556DE8), compared to threshold147D69C initial1. Activity transitions
  generate extra forward/reverse token128 and paired IDs consumed by cannonC50.
  Follow step/target/reference producers, sentinel, bone replacement and callbacks.
  Continue death animation feasibility/selection `0x938410/0x9648E0/0x9645D0`,
  aimset/blendspace parameter producers, remaining inherited marker action lists,
  root/bone transforms `0x5D3820/0x5D7410/0x5D8170/0x5D8250` and entity slot25,
  actor1420 bit3/handler flags and cache invalidation, movement mode enum binding,
  counter34/scale27C writers, reference bone558 binding, mounted resource overrides,
  command2F/idle synchronization, serialization,
  handler/actor replacement lifetime and callback reentrancy. Deferred mesh updates
  retain model pointers and swap two vectors on event-object kind26 (`0x5D5DE0`);
  release/cancel/apply are reviewed, full drain and registration remain open.
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

- Area lifecycle follow-up: registration creates separate main/worker snapshots,
  whose independently allocated numeric IDs are asserted equal only by GEM.
  Register parameter2 seeds interval indexing, not the allocated ID. IDs use
  two banks0/80000 without generation counters. Zone bit8 owns its record,
  bit9 its source/working geometry. Pair tick7762A0 caches unchanged overlap,
  checks dirty pairs with strict Z overlap, and updates raw overlap rows. Trace
  storage/overlap dependencies below and zoneC8 pather shapes. Worker8BE020 executes updates FIFO,
  higher then eligible lower work;198 flush/199 acknowledgement govern scene
  teardown8B2E00. Scene pass maps global stop697870/6978D0/6978F0/697940,
  generic task thread and worker exit thunk; inner application697A40, quit
  handlersBEE460/BEE560 and complete join/destruction order remain open.
- Scene spatial follow-up: A20 is current placement frame, A18 last area refresh
  (corrects prior field names). Slot33 incrementsA20; refresh-all always copies
  it even when obstacle zones are skipped. Contact pass maps71F560 procedural
  refresh and720070 patherC8 production; continue captured callbacks/per-pather
  storage and84FA80/8798D0/887760/A75CD0 revision consumers.
  Automatic720B40 fits combined/per-volume circle or sampledOBB clouds;
  CompoundObstacles bit25 and obstacle_ignore_height are bound, with bounded
  authored declaration census. Contact pass maps795890/795AF0 volume refresh,
  TraceVolume slot4/5 and terrain704380/A3E8D0 sampling; follow underlying matrix,
  sample-cache, face/support geometry and heightmap producers. Pointappend723830
  is bounded as12-byte vector insertion. Mounted/inherited properties remain open.
  Seeds are50x50 radius5 spheres; worker storagecopy concatenates both banks
  and uses loop-index IDs. Follow second-bank preconditions and worker heightmap
  A3C2D0/modeA6D1D0, queued pather captures and8C2650/8C2BE0/8C2900 internals.
  Body overlap rows sort by cached ownerID and dispatch727520/727370 into stack
  contact context727BD0. Contact pass maps727D60 outer resolver,727410 direction
  flags and9843C0 ancestry exclusions. Continue93BDD0 knockdown consumer and
  physical callbacks below. Raw area,
  entity and link lifetimes need their owning-scene ordering established.
  Scene tickA1B320 prologue maps general-manager overlap tick after animation;
  full callees/later phases remain. Scale cap initializer45540 sets configurable
  g_maxEntityScale=10; full setter, bone updates and transform propagation open.
- Entity contact follow-up: collider588 profile is88 bytes; native properties
  CanPush/CanPushBroken/CollideWith/NoContactWith/UseRamVolume and Blocking are
  bound. Basecolliders.set resolves41 profiles/30 categorytags; masks add through
  inheritance and registry numbering depends on insertion history. Follow raw
  profile lifetime/reload6964F0, default bindings, profile60 consumer and mounted
  entity selection. Contact effects precede rechecking volume bit5/disabled flags;
  729070/7295D0 passcontactdata to733690/733EC0 or chassis slot23. Map that solver,
  batch733810/733880, material98/9C names/units, chassis3C0bit10 consumer and
  slow_on_crush request29 reception; entityA2C/A30/A34 and ram flags producers.
  TraceVolume interfaces are distinct from spatial eBoundingVolume: investigate
  constructors/loaders, slot3 ray queries, support/solver5F81F0, face5F23E0,
  spherecache5F4BE0/5F4060 (startup3A6E0/3A710), matrix/inverse helpers and revision
  producers. Volume28bit6 displacement globals remain unnamed. TerrainA3E8D0 has
  a full bilinear assembly implementation despite empty Ghidra C; investigate
  signed16-bit cell construction/units and query bounds. Continue contact_effect,
  in_hole and volume eventE4/E6 actions/reset/drain, simulation command28 reception,
  SoftLinkableA68 coupling removal and owning-scene lifetime through callbacks.
- Physical-response follow-up: contact records are1C0-byte values with raw body
  pointers; GEM assertions identify D0 friction and D4 restitution, connecting
  material98/9C to those roles. Queue100 is all-local, not stationary/dynamic.
  Batch reduction limits each appended bank tail tofour: deepest first, then
  penetration plus distance spread; zero-score fallback index0 is preserved as
  an edge case.733EF0 removes both banks' contacts on simulator deactivation.
  734200 partitions by connected nonnull bodies and invokes73B6B0. Continue
  sorter736410, move7369D0, grow7345E0 and solver73B6B0; body1DC reset/consumers,
  mass/inertia/velocity/impulse and simulator lifetime. Scene passes integer
  delta converted directly tofloat; unit factor1547DAC=(K*L)/(H*T),1547DB0=L/T
  in real arithmetic (native float operation order retained). Matrix SIMD ABI
  and determinant cache gate are mapped, including NaN fallthrough and stale
  inverse on small finite determinant. Sphere initial caches now resolved12/42
  vertices and20/80 faces; reserve helper preserves prefixes. Face/segment point
  distance branches are mapped. Continue5FC5E0 penetration,5FAA60/5F9240 simplex,
  DT support implementations, higher sphere levels if reachable, shape loaders,
  unit/config writers and bone revision/invalid-transform preconditions.
- Contact-solver follow-up: queue move7369D0 swaps withlast, so initial unsigned
  owner-ID/depth ordering doesnot remain intact. Grow7345E0 copiesvalues without
  body retention; sortpivot734CB0 remains open.73B6B0 iterates atmost4*N closing
  contacts, propagating requested linear/angular changes through shared bodies.
  Mode2 biases normalvelocity bypenetration/control andskips positional loop;
  it is the only located directcaller mode. Massdivisor88, inverseinertiaCC/DC/EC,
  position28, orientation38, velocity48/angular58 andsetterchanged1B4 are mapped.
  Angular setter rejectsnonfinitevalues but stillmarkschanged; propagation can
  stilluse rejectedrequested angular delta. Continue producers/integration/sleep,
  one-bodyfrictionB ordering andmode0 quaternionmath ifreachable. TraceVolume+50
  issecondary DT support interface; primaryslot5 enumeratesvertices. Allfour
  primitive support methods andDT wrappers nowmapped. Simplex edges/weights,
  subsetselection/witnesses anddistance query5F7A40 mapped withexplicitnumeric
  limits.5FC5E0 chooses42 directions then shiftsA andrefines withdistance query;
  alwaysreturnsALtrue, lowerfloatignored. Continue shape constructors/loaders/rays,
  exceptionalgeometrypreconditions, sharedtolerance/global scheduling andbody
  simulator→entity placement/landing/contactcallback lifecycle.
