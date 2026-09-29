# Crew roles, cannon states, permissions and exit actions — build 25492057

Static continuation of the [placement](placement-map-25492057.md) and
[boarding](boarding-map-25492057.md) passes. Addresses are RVAs in the pinned
game executable unless explicitly labelled editor. No game or editor execution,
binary patches, hooks or runtime ABI validation was used. The accompanying
manifest distinguishes bounded contracts from partial slices; an exported
function or discovered vtable is not automatically a reviewed contract.

## Permission and role bindings

`0x7FA9D0(actor, player)` is the access predicate used by boarding. Null actors,
the `abandoned` tag in actor `+4E8/+4F0`, and boarders without an owner fail.
The tag name is initialized by `0x484A0` at global `0x1570860`.
If actor `+DB4` bit8 is clear, access succeeds. Otherwise it succeeds for the
same player byte `+DA8`, for the tagged flag `any_crew`, or for `ally_crew`
with relationship query `0xA468A0` returning ally value2.

These flags are **registered indices**, not fixed masks: `0x579C0` binds
`ally_crew` to global `0x157DFB4`, `0x57A40` binds `any_crew` to `0x157DFB8`,
and `0x57D20` binds `select` to `0x157DFC8`. Actor bits start at `+DAD`.
`0x7FACF0` finds/appends names in the shared tagged-flag registry and diagnoses
more than32 registrations. Do not substitute a guessed compile-time mask.

Selection predicate `0x7FA570` additionally rejects entity `+38` bit11;
it tests `select` with scene predicate `0xA3BB20`, and for humans checks the
component at `+578/+18`, field `+174` bit0. It accepts matching player ownership
or a matching-player actor among occupied human places. These are selection
rules, not an assertion that all boarding or order-issuance rules are identical.
Base chassis slot36 `0x842490` returns true; derived overrides remain a lead.

`0x781110` parses a crew request's name list, setting required count `+338`
to the parsed name count, then accepts optional `minWorkers` to replace it.
The identifier path uses `0x66D0E0(stream,1)` and normalization helper
`0x698000`; `0x3ED940` contains the ICU `ucasemap_utf8ToLower_56` path.
Thus the resource spelling `minWorkers` reaches the lowercase comparison.
This parser itself does not clamp the explicit count to the list length.

Weapon place `+2C8/+608/+948` are now bound to **gunner / charger / commander**:

| Request | Default name initializer | Configuration | Registration priority |
|---|---|---|---|
| `+2C8` | `0x4F6D0`, global `0x15722A0`, `gunner` | `Gunner`, token98/`0x62` | 3 |
| `+608` | `0x4F490`, global `0x15724E0`, `charger` | `Charger`, token99/`0x63` | 2 |
| `+948` | `0x4F4C0`, global `0x1572060`, `commander` | No separate case in the reviewed place parser | 4 |

Constructor `0x810480` calls `0x812710` for each default, producing one requested
name and required count1. `0x8103C0` registers all three with the placer.
Parser `0x811980`, registry `0x1580040`, replaces Gunner/Charger through
`0x781110`. Clone `0x8115A0` copies all three request definitions, including
priority/ID/count fields; registration subsequently supplies runtime priority.
The absence of a commander case here does not exclude other definition edits.

Other reviewed place parser bindings: Rotate88→`D30.bit2`, Foresight89→`D8`,
Shooter90→`138`, RestoreIKAfterAim91→bit3, SequentialAim92→bit4,
Weapon93/Ammo94→item at`2B8`, LinkBone95→`1F8`, LinkAnimation96→`258`,
Barrels97→`2C0`, Basic100→bit1, Type101→`78`, StationaryFiring102→bit5,
AccuracyByMotion103→`C90`, AimBone105→`198`, AI_In_Manual106→bit6,
Bullets107→item ammunition count. Sealable104's case skips its structure here;
this is not a positive sealability field binding. Serialization `0x811E00`
was reviewed only for its weapon/ammo/motion-profile branches.

Chassis parser `0x8402F0` routes token`0xB6` (Driver) through
`0x781110(chassis+80)` and falls through to set chassis `+3C0.bit11`;
token`0xB7` (DisableMoveWhileFiring) sets the same bit. The registry is
`0x1582050`, used by dispatch `0x840210`.
`0x83F420` registers the chassis request at priority1. The remaining token
names and subtype-specific movement policy belong to the movement continuation.

## Cannon extender: definition versus live instance

`Entity::eCannonExtender` has size`CE8` (`0x92EB50`). Primary vtable
`0x10EB820` is at offset0; its resource interface vtable `0x10EB878` is at+8.
Slot9 of the primary is SetState `0x9296F0`; slot2 is reflection/configuration
`0x92A550`; slot7 is entity events `0x92B1B0`; slot8 is animation events
`0x92B980`. The same slot numbers on the secondary interface have different
meanings. Factory and clone return the adjusted+8 resource pointer.

| Offset | Representation and role |
|---|---|
| `18` | Owner entity pointer |
| `88 / A0 / B8 / D0` | Vectors of animation names: move/fire/aim/reload |
| `E8` | Switch animation name |
| `150 / 490` | Attack crew request / attack obstacle name |
| `4F0 / 830` | Move crew request / move obstacle name |
| `890 / BD0` | Trailing crew request / trailing obstacle-name storage |
| `C30` | Native state: fire0, move1, trailing2 |
| `C34 / C38` | Cached linear and angular motion inputs |
| `C40 / C48` | Shared integer pointer / shared-pointer control block |
| `C50` | Paired animation-event activity count |
| `C54` | Last motion-sampling clock |
| `C58` | Rotate-bone name |
| `CB8 / CBC` | Disable byte / rotate IK limit in radians |
| `CC0 / CC8` | hasMoved byte / next pivot-obstacle poll clock |
| `CD0 / CD8` | Callback label / pivot_front bone pointer |
| `CE0 / CE2 / CE4` | Pivot revision / gun and mgun event IDs |

Default-definition constructor `0x92A0E0` differs from live constructor
`0x929EC0`: the former supplies fire-state defaults, attack workers
gunner/commander, move worker names, trailing seat1/seat2, switch `close`,
rotate-bone defaults and event subscriptions. Its default worker append helper
`0x92E520` adds names without increasing required count: all three counts remain0
in this constructor until configuration/copying changes them. This differs from
weapon-place defaults. The latter initializes empty
collections and allocates the shared animation counter; it relies on later
definition application for several policy fields. Do not treat either alone
as the final initialized runtime object. `0x92CE80` copies state, requests,
obstacle/animation names, rotation policy and hasMoved, but does not share
the source instance's live counter, pivot pointer or timing caches.

Configuration `0x92A550` uses state conversion `0x92DA70/0x92E060`. Native
enum table `0x10EB7A0` is `{fire:0, move:1, trailing:2}`. Text matching is
case-insensitive; invalid enum values map to−1 in the converter. Property
state changes go through SetState when different. Animation aliases
`drivers`/`move`, `gunners`/`fire`, `aimers`/`aim` share vector bindings;
`reload` has its own vector. `move`, `attack`, `trailing` sections parse
workers using `0x781110`; move/attack also parse obstacle names. This reviewed
trailing section does not parse its obstacle storage. `rotate_ik_Limit` is
multiplied by `0.01745329238474369` (degrees→radians).

The packaged `properties/cannon_mgun.ext`, `ammo_box.ext`, and `stretcher.ext`
use move workers `driver1 driver2 minWorkers 2`. These are concrete data
examples; they do not establish all vehicle definitions or mods' values.

## State transitions and crew effects

`0x9296F0` always stores `+C30`, then returns without live effects if owner
`+38.bit13` is clear. Otherwise it refreshes vehicle-brain `+424` if that
subtype exists, may publish command`0x5E`, applies obstacles and place-enable
policy, and activates the extender in fire/move but deactivates it in trailing.
Owner `+1C0.bit24` is set in fire and cleared in move/trailing.

| Destination state | Crew transfer and other effects |
|---|---|
| fire0 | Stop move crew animations; play switch in reverse; move→attack places; restore rotate IK limits |
| move1, previous trailing2 | trailing→move places |
| move1, other previous state | Play switch forward; attack→move places; update rotate-bone/entity transforms and zero its limits |
| trailing2 | move→trailing places |

`0x92BE60` pairs source/destination request names **by list index**, bounded by
the shorter list. It resolves both place IDs and invokes boarder move
`0x78D0B0` for the same owner. The remote-side predicate `0xC0B630` suppresses
this path. This is not a name-equality join or a search for a semantically
equivalent role.

`0x92C080` writes placer-slot `+248.bit4` (Disable), closing a previous
availability-flag producer. For each name across the three lists, trailing
membership takes precedence, then move, then attack. A trailing member is
enabled only in state2, move only in state1, attack only in state0. Overlapping
names therefore follow that precedence; they are not enabled by any matching
active group.

`0x92BCC0` toggles named obstacle groups only if **both** move and attack names
are nonempty: fire disables move and enables attack; move/trailing do the
opposite. `0x71F250` scans owner `+A00/+A08`, matches each entry's names at
`+138`, changes flags at entry0, invalidates geometry via `0x721180`, and
conditionally rebuilds obstacle state. With this caller's fourth argument0,
bit0 is the changed enable bit. Its alternative bit1 policy and full obstacle
construction are separate geometry leads.

Order `eCannonState`, size40, state at+38, Execute `0x92D310`: requires an actor,
the cannon extender, non-disabled state, current state other than trailing,
nonempty move worker list, and actor guard `0x7F11E0` false. That guard reads
weaponry `+86`; its precise state producer is a remaining weapon lead. In phases0/1 it
waits for the switch animation; if requested state differs, it calls SetState
and returns wait0. Matching state completes1. Missing/forbidden inputs fail2.
Other phases complete after the common guards. Parser `0x92EA90` stores an
integer without validation in this routine. Factory `0x92D220` accepts that
state; the no-argument factory `0x92D430` leaves it for subsequent loading.

### Motion and animation dependencies

`0x92EC60` accepts a cache update when absolute difference exceeds1e−5 and
either the new magnitude is below1e−5 (write zero), or relative difference
`abs((old-new)/new)` exceeds0.02. This is the motion-change threshold, not an
AI attack probability.

`0x92C2C0` first resets the shared completion counter to0, then pairs move
workers with move animation names. It calls each occupied actor's brain slot19
with3; for active motion it queues an animation with rate equal to signed
linear contribution plus signed angular contribution times worker-owner
distance. The signs depend on actor orientation and side of the owner.
Units derive from `0x2FA60/0x4C0C00`; their writable globals have pinned
initial distance scale20 and time scale1000, giving linear conversion50 and
distance conversion0.05. These initial bytes do not prove the globals immutable. This helper sends orders and
changes shared state; it is not a pure speed query.

`0x92C6A0` pairs a request list with fire/reload animation names, stops aim
animation via `0x92CA90(...,false)`, queues an animation for each occupied
named role, increments the shared counter, and appends `eAnimationCompleted`.
That order's Execute `0x8EF250` immediately completes1. Its destructor
`0x92E600` decrements the shared integer and releases its control block.
Consequently cleanup/cancellation can decrement it too; it is not a hardware
weapon-fired counter. Aim animation `0x92CA90` only acts when that counter is0.
The broader order-queue lifetime remains relevant when resets overlap cleanup.

`0x92CD30` finds a revolute rotate bone. Negative limit clears bone`+12.bit6`;
otherwise it sets that bit and writes symmetric limits at`+208/+20C`.
The move transition's matrix correction is reviewed as a bounded side-effect
slice, not a completed general bone-transform solver.

Entity events `0x92B1B0` bind `gun`/`mgun` animation event IDs, find
`pivot_front`, apply state, subscribe definition event pairs, and copy a
nonempty move request into chassis+80. Tick event`0xB` can return an empty
moving cannon to fire; samples motion, queues crew motion animations, polls
pivot revision and refreshes geometry. Link-change event`0x11` chooses move
for link kind0 and trailing for kind6 when local policy permits. Event`0x12`
uses attack workers with reload animations. Animation callback `0x92B980`
uses gun/mgun for firing animations and counts paired start/end IDs for aim
activity. The event-number producers and full skeleton scheduling are not
inferred merely from these consumers.

## Exit destinations and post-exit orders

The registry is a linked list rooted at`0x153B348`, initialized/accessed through
`0x8E6370`. Each node contains next0 and registration8; registration contains
name0 and factory8. `0x8E4D60` matches names case-insensitively, constructs an
object and calls slot7 to read it; unknown names diagnose and return null.
`0x8E63E0` performs lookup only. Direct writers include inlined registration
code, so following calls to the registry accessor alone misses registrations.

| Registered object | Factory | Main effect |
|---|---|---|
| eEmitToPoint | `8E5400` | Move to explicit XYZ after exit |
| eEmitToWaypoint | `19DA00` | Set the actor brain's named waypoint after exit |
| eEmitOnSink | `98DDB0` | Choose a side exit point, move away and reset waypoint |
| eEmitOnAttack | `98DE10` | Same with its additional human-response option |
| eEmitAndBurn | `98DF10` | Burning response on traversal start, scatter on finish |
| eEmitAndRepair | `98DF60` | Repair source, then queue boarding back into it |

Common refcount fields are+8/+C; option flags+10 default4. Callback slots3/4/5
are start/finish/position in the reviewed boarder callers. The base slot3 is
empty (`0x19BDC0`). These callback objects must remain retained through the
walker lifecycle described in the boarding pass.

Point object size28 has XYZ+18. Finish `0x8E4EE0` queues Move from `0x8F3600`
using order options `{1, clock sentinel}`. Position `0x8E4F30` copies XYZ;
read/save `0x8E4F50/0x8E4FA0` use three floats.

Waypoint object size80 has option byte18 and name20. Position `0x19BF30`
looks up `0x996270` and copies waypoint+20/+28; missing names diagnose and
return zero. Finish `0x19BF00` calls brain slot32 only for a nonempty name and
non-null brain. Human override `0x942BB0` clears the pather for a nonempty
waypoint, then calls base `0x905940`: brain slot19 receives`0x107`, waypoint
state at+1E8 is rebound with the actor, option+395 is stored, and an existing
state object receives event6. Parser/save `0x19BFF0/0x19C020` include the
option byte as a parsed integer boolean.

Helper order builder `0x19C560` constructs the waypoint callback. For actors,
EMIT_PASSENGERS maps to mode1, EMIT_CREW to2, and default/all to3; explicit
actor-ID lists construct negative-mode Emit. For non-actors it uses direct
boarder emission and diagnoses unsupported passenger/crew modes. This binds
the semantic names of the mode1/mode2 branches from the boarding pass.

The four additional callbacks share point+18, source actor pointer+28, and
lazy actor-ID short+30. Read/save `0x98DB60/0x98DBA0` append the source actor
ID after point data; lookup mutates the pointer and clears the saved ID.
This is not the two-pointer SoftLinkable receiver layout. Full source-actor
lifetime protection is still an open lead.

Repair callback has float argument+38 default1. Its finish `0x98DC50` creates
`Order::eSmartRepair` via `0x8FC870` with source/argument, then a Board order with an empty place
name. Its exit-position callback `0x98DBF0` returns source position.
Burn callback's `0x98E330` samples an angle over2π and radius from3 to5 times
source virtual slot16's returned extent, preserving source Z and storing the
resulting point. Start `0x98E7D0` uses a mode-dependent 0.3 random threshold or
`tankman`-name condition, passes source bounding/fire state to burning helpers,
clears `select` on the exiting actor, and may publish UnitBurn command`0x78`.
Finish `0x98EA60` queues Move and `Order::eHideout` from `0x8CD420`, with
distance200 (=20×10), flags1 and no explicit point.

Attack and sink objects are size40 with flags5; attack has byte38=1, sink0.
They share position `0x98E720` and finish `0x98EAE0`. **Decompiler omission:**
both pass a float side argument in XMM2 to `0x98E4F0`, derived from random
sampling or the sign of the relative angle. That argument is not unused RNG.
`0x98E4F0` computes heading + `trunc(60+30u)` degrees times that side, and
radius `(1.5+2v)` times source virtual slot16's extent; it preserves source Z.
Position chooses side from the sign of `2u−1` with a1e−5 dead zone. Finish
instead uses the signed angle from the source's reverse facing to the exiting
actor; within10degrees of the center it randomizes the side. It calls
`0x903920(brain,0)` to reset alert-like state+1E0 and publish event8, and the
attack option calls `0x9442A0` for humans with a0.4–0.9-second value. That helper
only sets brain+3E0 to current clock+converted duration when currently0.
Finish queues Move and `Order::eResetWaypointOnly` after these calls. Position
queries themselves consume RNG; a custom brain must not poll them as pure
readers. The timer's full behavioral consumers remain an explicit lead.

## Boarder command payloads and reception

Every encoder first writes the routed owner entity through `0x7037C0`; every
receiver first resolves it through `0x703810` into command+10 and returns if
it is absent. Place IDs are exactly two bytes via `0x77A620/0x77A5F0`.
Actor IDs and general entity IDs are distinct representations here.

| ID / command | Payload after routed owner, in wire order | Receiver/action |
|---|---|---|
|31 Emit|place16, flags32, door string|`15AA10` → EmitInstant `7849D0`, explicit door and zero reference point|
|32 StartOut|actorID16, door string, place16|`15ACC0` → `78CC60`, OnExit=null|
|33 Collect|entityID32, place16|`15AE90` → placer collect `77D360`|
|34 StartIn|actorID16, door string, place16, option8|`15B090` → reserve place, capture canonical-root door, StartIn|
|35 Move|sourceEntityID32, sourcePlace16, destEntityID32, destPlace16|`15B7A0` → boarder move `78D0B0`|
|36 Swap|place16, place16|`15B910` → placer swap `77CEE0`|
|38 Cancel|entityID32|`15BB20` → clear place reservation, release canonical-root door|

IDs in this table are hexadecimal. WalkerSync37 is mapped in the boarding
pass. Cancel does **not** itself stop a walker or erase its door queue entry.
StartIn receives its option independently from the place ID. StartOut's
receiver supplies no callback object, so peer-side callback execution cannot
be assumed from the locally queued OnExit object.

Encoders: `15A930/15ABD0/15AE30/15AF90/15B730/15B8C0/15BAE0`.
Constructors: `15A8E0/15AB80/15AE00/15AF30/15B6F0/15B890/15BAB0`.
Editor counterparts retain matching reviewed payload widths and apply paths;
editor diagnostics additionally assert non-local routed entities and provide
more StartIn logging. This does not establish transport reliability, ordering
or malformed-input behavior across all router helpers.

## Remaining static frontier

- Derived chassis boarding predicates, full tagged-flag definition/ownership
  writers and component-specific selection gates.
- Full source lifetime of exit callbacks; helper vehicle-brain triggers and
  fire/repair/avoidance order bodies; waypoint queue cancellation semantics.
- Cannon state command reception, bone-transform correction, event producers,
  move-request replacement and timing around shared animation-counter resets.
- Other command-router identity/stream helpers and delivery ordering.
- Full obstacle construction and dirty-state propagation; individual weapon
  charger/controller/animation scheduling and all request reconfiguration.

The frozen evidence contains139 reviewed game entries/slices,27 editor pairs,
93 field records and12,163 instruction locations. Structural validation passed
for both binary hashes,38 RTTI tables, the cannon enum,13 constants,61 byte
sequences and three packaged resource members. Independent LLVM disassembly
matched4,807 instruction starts in33 selected routines after normalization of
standalone LOCK prefixes. A preliminary place-serializer lead inside a function
was discarded; the actual two-byte wire serializer is `0x77A620`.

These checks verify evidence locations and layouts, not runtime safety or
exhaustiveness of the overall AI map. Unreviewed anchor callers remain discovery
material in the local exports and are excluded from the reviewed-entry count.
