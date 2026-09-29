# Human state hierarchy, interrupts and movement — build 25492057

Static pass after [combat](combat-map-25492057.md). Addresses are game
RVAs unless marked editor. Function exports are leads, not semantic coverage.
No executable was launched or modified.

## State objects and dispatch

The brain's +0xE0 is a root state and +0xE8 is the active state. The apparent
"context" used in prior passes is an `Actor::eBrainState` derivative. Its
+0x20 points to parent, +0x28 to active child, +0x30 to brain, and +8..+0x10
is an owned-child vector. This resolves the movement setter's cleanup dependency.

- `0x9123D0`: attach child, copy brain/parent, send event1 to child slot2,
  then append it to the parent's owned vector. Event1 is initialization.
- `0x912460`: deactivate existing child with reason1, install new child,
  write brain+0xE8, send event2 with payload0. Event2 is activation/re-entry.
- `0x9124D0`: select the existing child in brain+0xE8 and send event2 with
  payload1. Despite its use in parent execution, it does not call child slot3.
- `0x912520`: send event4 with a reason, recursively deactivate descendants
  with reason2, clear parent's active-child pointer, set brain flag21, move the
  active state to parent, and send parent event3 with the original reason.
  The state object remains attached; this is not object destruction.

`0x912110` sends one event to the supplied state (slot2), with both payload
qwords zero. It does **not** traverse children itself; its decompiler's apparent
zero first argument is contradicted by RCX in assembly. `0x912170` sends an
existing event to that state and then follows active-child +0x28 downward.

Base reconsideration `0x903B10` walks parents from the active state into a
temporary list, then invokes slot4 **root first**, stopping when brain flag21
is set. This is the interrupt method, independently named by diagnostics.
If interrupted, it calls `0x912110(...,5)` and sets brain flag25. Otherwise,
when there is no current order and no immediate queued order, it calls the
active state's slot3. It gates this whole block on low-six brain flags clear,
flag20 clear and a root state existing.

`0x9121C0` implements named interrupt ordering: put the candidate label pointer
in brain+0x110; if it equals brain+0x108, set flag22; return NOT flag22.
This is pointer identity, not a string comparison. `0x912200` on a true
condition sets flag21, clears the current order through brain slot13, and
selects the state. `0x912260` performs the same interrupt preparation without
the condition. Writing an action label or state pointer alone skips this work.

The same base reconsideration refreshes aggregate brain flags from sensor
category1: subject +0x14 bits8/7/9 contribute to brain bits14/16/15; subject
mask0x12==2 sets brain bit13. Subject +0x6C suppresses these contributions.
Its alert-like field +0x1E0 changes 0→1 under configured +0x1E4 flag gates,
then emits state event8. Exact external names of these fields remain open.

## Location hierarchy

Soldier root initialization `0x9570F0` obtains named states
`soldier_outdoors`, `soldier_cover_vertical`, `soldier_cover_horizontal` and
attaches them. Root selector `0x957420` uses cover group:

| Cover group | State pointer | Behavior family |
|---:|---:|---|
| 0, 3, 4 | root +0x3D8 | Outdoors |
| 1 | root +0x580 | Vertical cover |
| 2 | root +0x588 | Horizontal cover |

Other values have no switch action in this game method; editor0x7DE920 instead
calls an unreachable-code diagnostic for that case. Civilian root selector
`0x957680` always selects its outdoors state.

Outdoors location initialization `0x949D40` creates idle, battle and find-enemy
children at +0x3D8/+0x3E0/+0x3E8. Horizontal/vertical location initialization
`0x949020/0x949880` creates corresponding cover-idle and cover-battle objects.
Shared selection `0x9526B0` runs only without an active child. In alert-like
mode1 it chooses battle if the brain has target +0x3B8, otherwise potentially
find-enemy with `0x942CB0` and remembered subject/Recon state; otherwise idle.
The previous selected state is +0x3F0.

Outdoors interrupt `0x949AA0` handles `enemy_alarm`: it can deactivate the
current child, attempt cover when firing is held and movement is allowed, or
make a rotate/prepare order toward the chosen subject's remembered position,
followed by a state event. Cover-idle interrupt `0x947200` exits on a selected
target; a motion-required path releases resident cover and deactivates the
state. Full per-state execution branches are still being reviewed.

## Root hazards and saved orders

Root constructor `0x9577C0` embeds these typed scanner objects:

| Offset | RTTI type | Active byte |
|---:|---|---:|
| +0x3E0 | Actor::eCrushScannerVehicle | +0x3F0 |
| +0x418 | Actor::eInObstacleScanner | +0x428 |
| +0x438 | Actor::eBurnScanner | +0x448 |
| +0x468 | Actor::eGrenadeScanner | +0x478 |
| +0x498 | Actor::eRetreatOutOfWorldBoundScanner | +0x4A8 |

Scanner +8 is the brain, slot1 obtains an order/action, slot2 updates detection,
slot3 initializes. Soldier root interrupt `0x957380` updates all five before
`0x955A90` arbitrates in this order: restore pending order, actor burning,
obstacle, nearby burn, crush, grenade, retreat outside bounds, restore after
avoidance, stand from water, cover expiry, interface/user orders. Named-gate
semantics apply, so this ordering is more than five independent booleans.

The root can detach the current order from brain+0xF0, call its end method,
and save it at root+0x568. Grenade avoidance additionally records a restore
mode at +0x4C0: mode2 saves the resident cover descriptor at +0x4E0; mode1
saves position +0x4C4, direction +0x4D0, and movement type +0x4DC. This depends
on hold/user control or resident cover byte+0x50. Other user orders can cause
the saved order to be destroyed. `0x956F20` performs this snapshot only when
there is neither an existing restore mode nor a saved order. The cover copy is
0x88 bytes, from cover+0x20 through +0xA7. Saved direction is actor direction
times10, for constructing a point in front of the restored position.

`0x956B70` builds the restoration sequence: type0/mode2 with wait, then either
MoveToCover(saved descriptor) or Move(saved position), Rotate(saved position
plus saved direction), MovementChange(saved type, unchanged mode). Both end
with state event0x10. `0x956B20` saves/intercepts the old order and installs
a supplied replacement through brain slot13. These paths preserve more than
a destination coordinate.

Grenade scan `0x907760` visits all four sensor categories, reading each
subject's underlying entity +0x20. A grenade-specific path requires category3,
visible or within configured detection distance, entity bit28 clear, an
`Entity::eStuffExtender` with an item and nonzero +0x90, a matching item tag,
and low entity velocity. The tag is `grenade`, bound by initializer0x54090.
A second explosives path uses entity flag13 and its
own visibility/distance test. Both require distance below `bomb/danger_zone`.
It stores entity at scanner+0x28 and marks active+0x10. It breaks the current
category loop, then keeps scanning later categories: this is not nearest-target
selection. `0x907A30` binds `bomb/detect_explosives` (+0x1C),
`bomb/detect_grenades` (+0x20) and `bomb/danger_zone` (+0x18).

Obstacle scan `0x907000` requires entity mask0x10000004==4 and no resident cover,
builds a pather context, and queries its current point with `0x8AC2B0`. Burn
scan `0x907460` queries the global spatial index around the actor's bounding
box, tests candidate bounding spheres and stores enlarged 2D danger circles.
The burn escape method `0x907150` has a confirmed static early-exit path: its
ring counter is initialized to -1, incremented to zero, and the `<=0` branch
compares two copies of read-only float 100000 (`0x105FFC4`) before returning null.
It never reaches the candidate loop from normal entry with these bytes. Editor
`0x78D810` has the same branch and constant. This concerns nearby burn avoidance,
separate from the actor-burning random movement branch. It does not establish
what players see when another behavior or script reacts to the same fire.

Crush scan `0x906A80` reads moving-entity records at global0x1585070. It tests
vehicle eligibility, broad 2D bounds and distance to each path segment,
inflated by actor width plus an extra margin for non-allies. Its slot4
`0x906EF0` requires actor type bit3 and NOT `0x841010`. It retains threatening
actors for the order builder; those helper semantics remain open.

Out-of-world scanner `0x907B30` checks a global enable, human/AI eligibility,
behavior state below2 and world-bound containment, with a one-unit timer before
activation. Its slot1 `0x907C00` calls entity deletion wrapper `0x6FCDC0(actor,1)`
and returns no order. RTTI `eEntityDeleteCommand` and deletion diagnostics name
that operation: it is not a move-back-inside request. Local removal and event
propagation remain a separate entity-lifetime dependency.

`Order::eAvoidGrenade` is 0x58 bytes; factory `0x8FD870` installs the tracked
entity at +0x38 and danger distance +0x48. Exec `0x8FD8F0` saves chassis flag8,
sets it to disable strafing, marks cleanup required, and advances any child
order before invoking `0x8FDD60`. Cleanup restores that flag and drops the
`grenade_alert` Fsm chain if +0x50 was set. Planner state +0x4C is0/1/2: state2
waits until +0x54; loss of the grenade/explosive schedules that terminal delay;
a resident cover outside danger radius takes a cover branch; entity bit28 can
hold state1; otherwise `0x8FE840` plans escape, with false→failure2. Escape
geometry uses `0x8FE840`: create `Pather::Task::eAreaOutside` around the explosive
with radius **current 3D actor-to-explosive distance +2**, request a route through
`0x8C8F60`, set type0/mode2 and install a Move child if successful. This is an
incremental escape step, not a one-shot path to the configured danger distance.
`0x8FEC10`, explicitly named `eAvoidGrenade::Hide`, requests a grenade-alert Fsm
chain and sets planner state1. Once the explosive is gone, the terminal delay
is randomized between 0.5 and 1.5 via the engine counter conversion.

## Movement orders and foot chassis

Order owner +8 is the actor's `eOrdered` subobject at actor+0xAF0. Consequently
order-owner+0x2F0 is actor+0xDE0 (chassis). Do not treat the adjusted pointer
as an entity base. `Order::eMovementChange::Exec` `0x8F7330` uses +0x38 for
movement type, +0x3C for movement mode, -1 as unchanged, +0x40 as wait flag.
Phase0 calls chassis slots45/44. Waiting returns2 if requested type differs
from chassis+0x30, returns1 when also equal to actual type+0x2C, else returns0.

Foot slot44 `0x848490`, named `SetMovementMode`, writes +0x460, clears +0x468,
and recomputes movement. Slot45 `0x848710`, named `SetMovementType`, writes
requested type+0x30 and starts Fsm transitions. It can reject a request because
of network role, terrain or inventory; type3 is explicitly **swim** in diagnostics.
The vision rule previously grouped types1 and3 together as `crawl`; that rule's
classification must not be mistaken for the movement enum itself.

`0x84E050`, named `eChassisFoot::RequestMode`, resolves desired mode/speed via
`0x84E540` and writes requested mode +0x44. Once requested and actual movement
types match, a changed mode or speed causes an Fsm transition or `0x84DBA0`.
The resolver has a speed-limited mode (+0x468==1), capped at +0x464, which
chooses the corresponding movement mode via `0x847F10`. Otherwise it combines
slot41 and +0x460 through the eight-column table0x1481170. Non-standing types
coerce resolved3/6/7 to0 and4/5 to2. Speed is slot39(mode, requested type)
times chassis+0x38. `0x84DBA0` applies the resolved mode, speed and angular-speed
profile locally when the network-role gate permits, and can emit a
`eChassisFootSetModeCommand` for synchronization.

The lookup's four rows (slot41 selects0..3) are:
`[0,0,0,3,0,0,6,7]`, `[0,1,1,3,4,5,6,7]`,
`[0,1,2,3,4,5,6,7]`, `[3,3,3,3,3,3,6,7]`.
Slot41 `0x849400` combines +0x440 and +0x40: either3→3, else either0→0,
else either1→1, else2. The property names of these two limits remain open.

Slot39 `0x847AE0` obtains the first float of a 12-byte profile from `0x84E660`.
Types0/2 share profiles: mode3 at+0x46C, mode7 at+0x478, mode6 at+0x484,
modes4/5 at+0x490, modes0..2 at+0x49C+12*mode. Type1 uses+0x4C0+12*mode;
type3 uses+0x4E4+12*mode. For humans, `Human::eParams_Speed` multiplies the first
and third values. A component ratio from `0x925010/0x924F90` below 0.3 applies
the further +0xA28 factor to both; its health binding is a follow-up dependency.
`0x847F10` chooses the nearest speed among modes `[3,0,1,2]` for types0/2,
keeping the earlier choice on ties; for other types it applies the three
profile thresholds in order. `0x84DC80`, named `SetModeSynced`, writes actual
speed+0x34, mode+0x3C/requested mode+0x44 and angular speed+0x43C, then emits
actor event0x18 with subcode7 and the mode.

`Order::eMove::Exec` `0x8F3E00`:

- Phase0 rejects a second start, an invalid route flag, or fewer than two route
  points. It leaves cover, prepares the brain, marks cleanup required, installs
  temporary chassis flags, and calls chassis slot65 to build a traversal path.
- It owns raw route data at +0x40, compiled path at +0x158, current path cursor
  at +0x180. A null compiled path is failure. Client/synchronized paths differ.
- Chassis movement state +0x64 uses 2/3/4 for internal progress/block/wait paths.
  It periodically checks the moving-entity registry and probes the route.
  Route checks can fail, request a rotate child, or alter the route.
- Chassis slot66 returns an internal code, converted through seven initialized
  dwords at0x1482978: `[2,2,0,0,0,2,1]`. Thus internal6 is order success1,
  internal5 is failure2, and internal2/3/4 are still-running0.
- Phase2 removes temporary flags, calls slot66 in cleanup mode1, restores
  collision/activity state, zeroes movement state, and sends a speed event.

Foot slot65 `0x84B880` converts the reversed route's XY points into a compiled
path. A special two-point standing path can choose a movement animation by
direction, length and phase rules; otherwise it builds/simplifies segments and
adjusts them to pather geometry. Foot slot66 `0x84AD30`, named `PerformMove`,
advances that cursor by quant step and movement speed, invokes movement Fsm
states, computes position and facing, and returns6 at the end. It can yield3
while awaiting animation readiness or5 when blocked/interrupted. The full
animation/path/obstacle contracts remain productive static leads.

Fsm tags are bound by explicit static initializers: `0x586E0`→`move`,
`0x58A70`→`rush`, `0x58CB0`→`swim`. Thus the special traversal path uses `rush`;
do not infer that every high-level run command follows that animation path.

## Pather context read set

`0x8C9C30/0x8C9D50/0x8C9E10` construct an actor-derived context. The profile
selector byte is actor+0xDB4's low byte; profile size is0xF8, indexed from
global0x1585310. The context retains a shared grid/query handle at+0xD8/+0xE0;
it must be released, not copied as an unowned pointer. Selected context fields:

| Offset | Input/source |
|---:|---|
| +0/+1/+2 | actor-context flag; chassis/bounds eligibility; `0x7EF670(actor)` |
| +8/+0x20/+0x38/+0x50 | four bounding boxes copied from world-bound configuration |
| +0x68 | owner player byte from actor+0xDA8 |
| +0x6C | actor position XYZ |
| +0x78/+0x7C | actor+0x594 and actor+0xC0 identifiers |
| +0x80 | actor+0xDAA word |
| +0x88 | diagnostic entity description string |
| +0xA8 | actor+0x588 link |
| +0xB0..+0xC0 | 24-byte entries of cloned/shared bounding objects plus a dword |
| +0xC8/+0xD0 | profile selector/profile pointer |
| +0xE8 | query flags, including derived actor and brain conditions |

The extra bounds are selected from actor+0xA00..+0xA08 when actor flag14 is
set, filtering child record flags before cloning its bounding object. Point
query `0x8AC2B0` first permits a profile-specific grid test under flags, then
builds a line/query context and compares its cost against100000. The bounds
predicate `0x8CA0A0` chooses context/world/player bounds according to actor,
ignore-bounds and world-mode predicates. Precise grid costs, bounds edge
inclusion and the cloned-object consumer remain open static work.

## Open work

State-event producers and the remaining human idle/battle/find-enemy helpers;
crush and obstacle order builders; movement enum/config bindings; route
request/compilation/probing and grid costs; animation/terrain application;
chassis ability/health inputs and entity terrain/obstacle interfaces; cover
kind chooser. These are productive static leads, not runtime-only questions.

## Evidence and validation

[movement-map.json](../artifacts/build-25492057/movement-map.json) contains
73 reviewed/partially reviewed game method entries, four editor counterparts,
65 field records and 977 selected instruction locations. The broader exports
contain 246 game and 14 editor functions; the difference is deliberately left
as unreviewed discovery. [Validation](../artifacts/build-25492057/movement-validation.json)
checks exact entries, local executable hashes, instruction membership, six
constants, 11 RTTI tables and three byte sequences. It does not prove semantic
interpretations or a callable ABI.

Independent LLVM disassembly checks 615 instruction starts across game burn
escape, state deactivation, movement-change execution, grenade escape planning,
the healing-restore gate, and editor burn escape. Six extra LLVM rows are
standalone `LOCK` prefixes split from `XADD`; after that normalization every
instruction start agrees. The burn early exit was inspected in both assembly
renderings. No game/editor executable was run or changed.
