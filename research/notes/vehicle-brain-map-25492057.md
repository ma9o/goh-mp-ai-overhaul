# Vehicle decisions, crew withdrawal and emergency orders — build 25492057

This pass connects vehicle perception and weapon selection to movement, crew
withdrawal, abandonment, repair and last-contact search. Addresses are RVAs in
`call_to_arms.exe`. The companion manifest records exact entries, instruction
locations, data bytes and selected editor counterparts. This is static evidence,
not an ABI specification or a claim that live pointers remain valid across ticks.
The [frontier](static-frontier-25492057.md) continues beyond this pass.

## Identity, ownership and configuration

`Actor::eVehicleBrain` allocates `428` bytes (`A7550`, constructor `988EE0`). Its
primary vtable is `FFFC30`; the resource/reflection subobject begins at `+10`.
The `eVS_Root` state is a separate `400`-byte object (`992BE0`), vtable `10F2BA0`.
Its registration at `1484BE0` pairs the string `vehicle` with that factory.
Root event/execute/interrupt slots2/3/4 are `98EEC0/990870/98F0A0`.

`LookAround`, token `2B9`, registry `1598D10`, initializer `6FFC0`, parses into
brain `+3DC` through `98B6F0`. Reflection `98B0C0` exposes `look_around` using
`this+3CC` because this pointer is the **+10 subobject**. Its comparison default
is `98D910`: owner definition tags `+548 -> +330/+338` contain `tank`
(`1570140`, initializer `48E60`). Constructor `988EE0` itself clears `+3DC`;
the reflection default is not evidence that every newly constructed tank has
already enabled the byte. Slot6 `98B1F0` is empty.

The following actor abilities are dynamically registered indices into the
32-bit actor `+DAD` bitset, not fixed bit numbers:

| Index global | Initializer | Native name |
|---|---|---|
| `1598D58` | `70670` | `emit_on_attack` |
| `1598D5C` | `706F0` | `emit_passangers_on_enemy` (native spelling) |
| `1598D60` | `70630` | `emit_crew_on_injure` |
| `1598D64` | `706B0` | `emit_on_mandie` |
| `1598D68` | `705F0` | `burning` |
| `1598D6C` | `70790` | `sinking` |
| `157DFC0` | `57CE0` | `repaired` |

`properties/vehicle.ext` defines `brain_vehicle` with brain/state `vehicle` and
overrides both crush constants to **1**. Code defaults are time **10**, length
**3**. The same resource enables `emit_passangers_on_enemy`, `repaired`,
`ally_crew`; cannon definitions disable the passenger flag. `bicycle.ext`
enables `emit_on_attack`; `ammo_box.ext` and `searchlight.ext` enable
`emit_crew_on_injure`. These are definition examples, not a complete resolved
inheritance graph for every spawned entity.

## Vehicle brain read set and lifetimes

| Brain offset | Representation / observed role |
|---|---|
| `3A0/3A8/3B0` | Candidate subject pointer vector, stride8; rebuilt by `988FC0` |
| `3B8/3C0/3C8` | Weapon evaluation rows, stride `1B8`; rebuilt by `98A520` |
| `3D0` | Cached weaponry `+84` revision, uint16 |
| `3D4` | Approach range float; reset to −1 each weapon evaluation |
| `3D8` | Failed firing-target deadline, used by root detour |
| `3DC` | LookAround configuration byte |
| `3DD` | Crew-threat/injury response byte; crew scan clears then can set it |
| `3DE` | Pending mandie response; consumed by root interrupt |
| `3E0/3E8/3F0` | **Retained** crew-perceived subjects; refcounted, sorted, no deduplication |
| `3F8` | Withdrawal/threat deadline |
| `3FC` | Last enemy location pending search |
| `400/404/408` | Last enemy XYZ copied from subject `+28/+2C/+30` |
| `40C` | Last enemy uncertainty/radius copied from subject `+34` |
| `410` | Next crew-protection sample |
| `420` | Prefer withdrawn exposed gunners during normal weapon choice |
| `424` | Cannon idle clock |

Weapon row `+0` is used/assigned for the current allocation pass; `+1` means its
cached target remains usable; `+8` is the runtime weapon pointer; `+10` is its
firing deadline; `+18` holds an `eTarget`. Four ammunition records start at
`+98`, stride `48`: ammunition pointer followed by inventory iterator storage.
The iterator starts are `A0/E8/130/178`. Rebuilding preserves the deadline,
cached target and byte1 **by weapon identity**. Row addresses are not stable.
Old targets and inventory iterators are destroyed when the old vector is freed.
The independent retained subject vector is released during its rebuild and the
brain destructor (`A7F70`).

`988FC0` snapshots weaponry revision, rebuilds weapon rows, empties candidates,
then, with a valid scene and brain `39C` bit26 clear, takes eligible sensor
category1 subjects through `912AE0`. It sorts through `9136E0` when weapon places
exist and prepends `903050`'s preferred subject if present. No deduplication is
visible at this insertion. It calls base reconsideration `903B10`, rebuilds
weapon rows again if the revision changed, and evaluates weapons when the scene,
weapon places, fire policy `902A70` and candidate list permit it.

`98A520` includes weapons passing `80A070` and, in manual weaponry mode, place
`D30` bit6 (`AI_In_Manual`). It gathers at most four distinct ammunition pointers
from current ammunition/inventory according to charger state. New rows get
`now+0.1s` before any preserved deadline replaces it.

The protection sample starts at `now+0.5s`; subsequent due samples advance the
**old deadline by 0.5s**, not necessarily now. `877AE0(sensor, ownerXYZ, out, 0)`
sums `876F60`'s two-channel estimates for category1 subjects satisfying either
`flags14.bit1`, or `!flags14.bit0 && flags10.bit28`. The channels connect to the
Human/Tank estimates in the [tactics map](tactics-map-25492057.md).
`8129D0(out, {1,0})` therefore gives `min(out.Human,10)` for ordinary finite
values. Protection is enabled when this is greater than **4**, the brain `3F8`
deadline is still ahead, or the occupied-seat-ID vector contains exactly one
short. It then tries withdrawing exposed gunners of weapons in mode2.

## Exposed gunners withdraw through real placement operations

`8108B0(place)` finds whether any name in its gunner request names a placer slot
with a nonnull `+240` bone. It does not test the `Sealed` bit.

`7819F0(placer, request)`, called by `8109E0`, requires at least one occupied
slot with **no bone** and rejects the result if any named request slot has
both a bone and an occupant. This is the observed withdrawn-worker predicate.

`781720(placer, request)` is **a mutation**, not a readiness query. For each
occupied bone-linked request slot, it skips a source protected by an active
request whose bytes `340/341` are both set. It seeks an empty destination with:
Disable bit4 clear; no reservation `+290`; Sealed bit20 clear; no `+240` bone;
compatible `+60` group/name; and the additional `+170` eligibility condition.
On the local execution path it unbinds/rebinds the occupant through
`77B950/77C140`; it can send `ePlacerMoveCommand` opcode39. It returns after the
first eligible transfer. A session-role branch can report success without
performing the local transfer, so callers cannot treat true as a universal
proof of immediate local seat movement.

`810940(place, duration)` calls this withdrawal helper. On success it removes
the gunner crew request (`7815F0`), clears place `D31` bit1 and calls
`80A730(weapon,1)`. That cancels attack/aim targets, clears weapon state,
resets shooter/charger state, sends event26 and may synchronize cancellation.
A nonzero duration sets place `C88=now+duration` even if no transfer succeeded.
The previously mapped `8107D0` requests the gunner again when this timer is zero
or due. These operations do **not** directly set a seat's Sealed flag.

Signal slot25 `98B200` handles signal6 (`mandie`): set `3DD/3DE`, set `3F8` to
now plus random **10–30s**, then try withdrawing each exposed/not-yet-withdrawn
gunner with a common random **1–2s** reassignment delay.

`9895D0` clears retained crew threats and `3DD`. For occupied actor seats whose
brain `39C.bit15` is set, crew-role predicate `781230(...,0)` can set `3DD` and
refresh `3F8` for **10–20s**. If the vehicle's `emit_crew_on_injure` flag enabled
collection, it retains each such occupant's category1 subject with `flags14.bit9`.
The sort `98D0D0` uses squared XYZ distance from the vehicle: insertion
`98BDC0`, partition `98C330`, heap `98CE10`; closest comes first. Equal-distance
identity ordering and duplicate elimination are not promised. The partition's
median helper remains a separate small follow-up.

## Weapon selection and issuing attacks

`98ABA0(brain, subject, priority, out, mode)` selects an unused row and one of
its ammunition rules. It rejects weapons already targeted at higher-priority
work according to runtime weapon `+D0/+208/+214`. Mode1 additionally requires
distance from aimer origin inside `[minRange,maxRange)`. Mode0 suppresses
already-withdrawn exposed gunner places while protection byte420 is set.
Other mode values bypass those specific two branches.

The aiming trial `81F030` must return something other than6. A special charger
predicate `80BF90` can select the row immediately, without an ammunition-rule
pointer. Otherwise up to four ammunition records are evaluated through
`9140A0/914180`. Nonnegative rule priority `+390` competes by strict greater-than;
ties keep the previous choice. The reload/ammunition restriction depends on
`902950` and mode0. Output holds row pointer, rule pointer, ammunition pointer
and copied inventory iterator. The shared suitability rules are detailed in the
[combat map](combat-map-25492057.md).

`989A80` repeatedly visits candidates while new rows are assigned. Its first
accepted selection has special distance treatment: if outside `80A910`'s
range, it records brain `3D4` and consumes the row for approach rather than
issuing the normal firing path. Later selections can continue for other weapons.
When changed ammunition has a usable inventory iterator, it requests the switch.
It builds or reuses the cached target, observes per-row deadlines and charger
timing, and invokes `912D60` for an actual firing target when ready.

A failed firing-target construction remembers a fallback subject/row and starts
brain `3D8=now+random(2,4)s` on the first selection. A successful target calls
`80A370(weapon,target, fireOption, mode2, -1)` and refreshes brain `+18`; the
normal fire option is3, special charger path0. Before readiness, it can issue
option0 aiming at the cached target or the subject position. Successful work
clears the detour deadline and marks the row used. The final fallback can issue
position-only aiming if its row remained unused and no weapon already targets
that actor. At the end, each row's retained `eTarget` is either cleared or copied
from the weapon's active target according to byte1.

Weapons with fired bit `264.bit2` and definition `15F8.bit1` start the row's
next deadline from definition floats `1544 ± 1548*uniform(-1,1)`. This consumes
RNG only when the variation is non-negligible. Exact property names and all
upstream timer writers remain follow-ups. Slot21 `989460` separately returns
the maximum effective range over eligible weapons/ammunition; it returns a
float in XMM0 despite prototype uncertainty.

## Vehicle interrupt priority and consequences

Each named stage calls `9121C0`; a false result ends this interrupt invocation.
`912200` performs the shared conditional interruption. The sequence matters:

| Stage | Main inputs | Consequence when accepted |
|---|---|---|
| `emit_on_enemy` | Alive gate, candidate, native passenger ability, `8E4FF0(...,1)` | State `emit_passengers`, event10, OnAttack callback, Emit mode1 |
| `emit_on_burn` | Native burning ability | `burn_crew_mp` interaction, event9, random1–3s Wait then AndBurn/Emit mode3; returns |
| `emit_on_sink` | Native sinking ability | Random0.3–0.8s Wait then OnSink/Emit mode3 |
| `emit_on_upside_down` | Angle between entity up and global up >70° | `upside_down` interaction, same delayed OnSink/Emit mode3 |
| `emit_wrong_crew` | 2.5s sampler or retry byte3F0; boarding ownership check | Instantly eject first incompatible occupant; set retry |
| `interface` | Brain request count88 or queue98/A0 | Interrupt with label `interface` |
| `emit_on_attack` | Alert1, brain39C.bit14, ability | Event10, OnAttack/Emit mode1 |
| `emit_crew_on_injure` | Alert1, brain3DD, ability, weaponry806340 result, retained threat | Event10, OnAttack/Emit mode3 |
| `emit_on_mandie` | Brain3DE and ability; passenger emission available | Consume3DE, OnAttack/Emit mode1 |
| `emit_on_destroy` | `984430` checks entity tag `destroyed` | Event10, OnAttack/Emit mode3; clear nonzero owner player through slot58 |
| `finding_enemy` | Root3BA and newly present candidate | Interrupt and clear root3BA |
| `moving` | Root3B8, move bit6, increased point danger and recent brain18 | Interrupt movement |
| `following` | Root3B9, follow target moved sufficiently | Rebuild follow movement or clear flag |
| `repairer` | Timer, scene/alive, no BAA, no candidate, actor1C0.bit2, repaired ability, eligible damage/tools, no `no_auto_repair` | Emit a repairer through `992950`; reset polling to1–3s |
| `crush` | Low2 policy !=1; `992290` danger capsule | OnAttack/Emit mode3 |
| `check_last_enemy` | Active engagement and `912C60` subject gate | Save last enemy XYZ/radius; set pending-search3FC |

Sink and overturn stages do not immediately return after scheduling their order
sequence; later stage gating still controls execution. The wrong-crew wrapper
`7FA9A0(vehicle, occupant)` passes the occupant's owner byte to `7FA9D0`; assembly
at `98F9AE` preserves the occupant in RDX where decompilation omits the argument.

For injury emission, a non-cannon needs the alive gate and threat distance less
than **3 spatial units**. A cannon instead scans occupant vitality: effective
health/max health <=0.5, then tests positive dot `(vehiclePosition-threatPosition)`
with the vehicle forward vector. It uses the closest retained threat and stops
at the first qualifying unhealthy occupant. No divide-by-zero guard is added.

Movement danger compares the **second/Tank channel** of `877AE0` at current
position and a point **5%** toward brain130's destination, sampled at0.5s.
The interruption condition also requires elapsed brain18 <1s. Follow rebuilds
when target displacement squared exceeds half the current follower-to-target
distance squared.

`992290` excludes a `tank`-tagged owner, then examines category1 enemy subjects
with live actor pointers, actor1C0.bit2 clear, tank definition tag, nonzero
velocity and a geometry bound. `907C30` constructs the impending-crush capsule
using the root's two crush constants. A containment test on the vehicle position
decides evacuation. Capsule construction remains a geometry follow-up.

## Root execution, pursuit and search

Event1 `98EEC0` loads crush constants, clears move/follow/search flags, initializes
the prior-position sentinel, starts repair polling at **1s**, and initializes
look-at-point delay to1–2s. Event10 invokes repairer emission; eventB completes
interface work; eventC requests reconsideration and clears movement flags;
eventD clears the chassis83FB00 latch.

`990870` gates combat movement on scene/alive, actorBAA clear, fire policy,
candidate availability and `991BD0` (chassis readiness and movement policy not
1000). Its branches include:

- Explicit look-at-point bit29, no candidate: periodic BodyTurn to brain140,
  with the next delay4–8s. Idle LookAround can run separately when no candidate.
- ActorBAA: label `waiting_for_repair` and return.
- Follow bit9: `992630`; explicit move bit6: `9924C0`.
- Aimer state7 with suitable scene/actor/chassis gates: set chassis83FB00 latch,
  build a movement target with `8D6A60`, move with tolerance `3*1547DE0`, then
  eventD. The native interpretation of aimer state7 is not inferred here.
- Approach: when brain3D4 >=0, make a circle around the subject with radius
  `0.95*3D4`; movement-zone policy2000 intersects it with brain124/128's zone
  through `6144E0`, then builds a bounded Move (`8F3750`).
- Detour: due brain3D8 and eligible target. Try **four** candidate points. At
  distance <=10 spatial units, sample radius `distance+3 .. distance+12` and
  angle ±90° around the target-to-owner bearing. Beyond10, use radius factors
  0.7,0.84,1.008,1.2096 and bearing+20°. Probe `8AC2B0`, apply movement-zone
  containment, build a pather task with `8C88D0`; nonzero route distance allows
  path-based Move `8F34F0`. Full path-acceptance semantics remain open.
- Return to zone: with active scene/alive, no candidate and `9031F0`, move to
  brain128's zone position.
- Find last enemy: pending3FC, alive/chassis policy, weapon place, no candidate,
  no explicit move and the additional `156FCC0` tag gate. Clear pending3FC,
  sample a point around stored400 using40C and random angle, reject outside a
  constrained zone, set root3BA, then Wait0.5–1.5s and Move there.
- Otherwise consume explicit brain orders `905AB0`, set chassis speed/stance
  from brain118/398, and append eventB, or clear the displayed state label.

`9924C0` cancels a stalled move unless refresh bit8 is set, consumes that bit,
and suppresses restart for5s after brain18 when danger byte3C4 is set. It records
current position, optionally inserts mode-setting Move when bit7 is set, then
Move to brain130 and eventC. `992630` checks follow validity, consumes bit10 or
rejects unchanged owner position, and stops following within **15 spatial units**.
Otherwise it moves into a circle of radius13.5 around the leader, saving the
leader position and appending eventC.

Idle `991C20` uses the first weapon place and LookAround byte3DC. Root `3D8`
here is a **different field from brain3D8**. Initial delay is5–10s. With chassis
48.bit4, scan direction is owner bearing plus integer-truncated random−30..30°,
delay1.5–3.5s. Otherwise a shared global counter alternates a full random angle
with a turn of signed integer90..180° from the old target direction; delay2.5–8.5s.
It cancels all mode2 weapon activity and, if the chosen weapon is now idle,
issues option0/mode2 aiming at a point20 spatial units from the aim bone origin.
The static shared counter makes this more than independent per-vehicle RNG.

The cannon-specific quant path `9891A0` refreshes brain424 while moving, manual,
busy or other named-bit gates apply. After **4s** without refresh it requests
cannon state0 through scope `92D100/92D1C0`. Command5E receive `B1880` resolves
the cannon extender and invokes slot9 only when the state differs; `B1800`
decodes the actor and state but does not itself apply the change.

## Repairer selection and SmartRepair

`992950` scans occupied human seats. It obtains the `Mechanic` ability's rate
when its level requirement is met, otherwise1. It selects the **first human
carrying repair_kit**, or retains the last human considered when none has one.
It emits that individual seat with AndRepair carrying the chosen rate. The
callback's later SmartRepair/Board sequence is in the
[crew-actions map](crew-actions-map-25492057.md).

SmartRepair is an `E0`-byte order. Factory `8FC870` sets rate38, target40,
actionID C0=−1, next timeC4, and zeroes five branch counters C8/CC/D0/D4/D8.
`8FB210` start sets flag18.bit18, adds owner tag `repairing` via `8FC460`, and
sets its scheduling time. It waits for any inventory action and child order;
when free and the deadline is due it calls `8FB330`. Cleanup removes the tag
and cancels inventory activity. The tag change can publish command6E.

`8FB330` requires a live target actor, `repaired` ability and at least one
component row of stride28 with state24==1, or state24==0 and health14<max1C;
flag25.bit0 must be clear and `8953B0`'s integer result at least writable
global147D69C (initial1). It then distinguishes:

- Owner has a repair kit (`8FC260`) and is within10 spatial units: if the
  right-hand item has repair_kit, create Repair (`8F9240`, size138), bind target,
  set rate12C (<=epsilon becomes0.5). Otherwise request `TakeToRight`, store
  inventory action ID and the old human13C0 item handle.
- Kit available but too far: child `8E0500` approaches the actor with distance
  `0.6*spatialScale`.
- No kit: acquire through `8FB860`, with a retry branch allowing3 attempts.
- No qualifying damage: return the kit with `KitExchange(...,false)` or cancel
  inventory activity, then complete.

Retry helper `8FC690` increments the branch counter, fails above its limit,
and delays only attempts after the first. Repair/approach use2 attempts with
2–6s spacing; equip/return2 with2–3s; acquisition3 with1.5–3s. Counters are
cumulative within this factory-created order, not reset on every successful
child completion.

`8FBC30(out, preferred, centerActor)` returns a vector of16-byte provider records
with kind0=item entity or kind1=actor inventory. If preferred already has a kit,
it returns that actor alone. Otherwise it queries entities in a20-unit circle
around centerActor. Inventory providers must be allied and either nonhuman or
have vitality174.bit0 (final death); a living allied soldier is not selected by
that branch. World stuff can qualify when its wrapped definition has repair_kit
and entity1C0.bit28 is clear. `8FB860` chooses the nearest provider in XYZ;
kind0 builds pickup `8CD4C0`, kind1 builds `eKitExchange` (`8FCA70`) with option1.
With no provider it instead builds a random movement away from the repair target.
Actual Repair and KitExchange execution are the next static layer, not closed
by discovering these factories.

## Hideout, cleanup and other root identities

`8CE8A0` Hideout phase0 searches cover around current or explicitly supplied
center (flag4), using radius40 and search option flag1. It selects with bias0.5,
installs MoveToCover, and removes that descriptor from the candidate vector.
With flag2 and an owner who is group leader, it distributes remaining distinct
cover descriptors to other nonvehicle group members, clears their brain39C.bit27
and queues MoveToCover with options5. Search failure returns2. Phases0/1 tick
the child; phase2 completes.

ResetWaypointOnly Execute `8EF250` returns1, but destructor `98DAE0` calls
`903920(ownerBrain,1)`. Restoration therefore occurs during destruction,
including cleanup/cancellation; it is not limited to a successful Execute path.

`eABS_Root` is registered as **airborne** at1484B10, factory988DC0, size3A8;
event/execute are988D20/988AA0 and interruptA74C0 is empty. It consumes explicit
orders, or with alert1/scene/chassis gates and no look-at-point bit, finds aimer
state7, sets the chassis latch, moves toward the weapon target and appends eventC.
`eAirborneBrain` and `eHelicopterBrain` factories988A00/988A50 both allocate3A0
and largely inherit the base brain. Flight/chassis policy and the misspelled
RTTI `eAirboneBrainClient` require their own continuation.

`eAS_Root` is **animal**, not aircraft: registration1484A58, factory9814F0,
size410. It embeds `eCrushScannerVehicleHuman` at3A8 and `eInObstacleScanner`
at3E8. Interrupt980F10 checks obstacle, crush, then panic from brain39C.bit16;
panic expiry is5s. Execute9810E0 chooses panic movement on a radius200–300
circle, consumes explicit orders, or randomly selects idle animation/wait3–6s
versus a random point in the assigned zone (or radius300 when unconstrained).
Helper981660 emits `avoid` and sets chassis movement mode2. Its scanners,
definition-tag gate and animal animation state are open static dependencies.

## Verification and remaining scope

Selected game/editor pairs preserve the reviewed field, event, phase and command
contracts; editor assertions and function outlining differ. Structural validation
checks 94 game contracts/slices, 19 editor pairs, 64 fields and 12,783 recorded
instruction locations, plus executable hashes, RTTI, constants, registration/
property bytes and five resource hashes. Independent LLVM disassembly checked
30 central functions and 6,649 instruction starts, all matching after standalone
LOCK-prefix normalization.
Neither check proves semantics, full transitive equivalence or hook safety.

Open leads include Repair/KitExchange and component repair updates; full root
registration/brain factory selection; airplane/helicopter chassis; animal
scanners; weapon definition1544/1548 bindings; full lifetime of raw exit callback
sources; pending subject classification writers; pather acceptance and crush
capsule geometry; the last-enemy exclusion tag; and all remaining crew/charger
availability producers. These are still productive static work.
