# Repair decisions, tools and inventory operations — build 25492057

Status: bounded static review, with open dependencies below. This extends the vehicle and component-damage
passes. All code addresses are RVAs; fields are hexadecimal offsets. Game and
editor exports live locally under `*-repair-tooling`. No game execution or
binary modification is involved. The findings do not establish a safe hook ABI.

## Component choice and timing

The actor's `+B90/+B98/+BA0` vector contains **28-byte component records**.
Record `+0` is the definition pointer (`Actor::eComponentGeneric`), `+8` the
owner, `+10` remaining repair time, `+14/+18` current normal/broken health,
`+1C/+20` their maxima, `+24` state and `+25` flags. The polymorphic generic
definition and the inline runtime component are different objects.

`894FA0` parses registry `15842D0`: token83=`RepairTimeLeft` into record10;
84=`State`; 85=`RepairTime` into definition340; 86=`RepairOrder` into definition344;
87=`Tags` into definition10; 88=`Health` into record14;
89=`HealthBroken` into record18. Time fields use `65E750`, not a raw float parser.
Enum table `10E53A0` binds **work=0, broken=1, destroyed=2**. Loading State also
sets flag25 bit1. `8951B0` saves state/time/health deltas. `894DB0` allocates a
358-byte generic definition, initializes RepairOrder=0 and its repair time to
the clock sentinel, and associates the supplied component name/tag.

`8953B0` produces a **repair duration**, not a repair-kit count or health score:

```
if state == work and maxHealth > 0 and health < maxHealth:
    duration = trunc(float(definition.RepairTime) * (1 - health / maxHealth))
else:
    duration = definition.RepairTime
```

`8FD610` accepts a broken component or a working one with health below maximum,
provided busy flag25 bit0 is clear and duration >= global `147D69C` (file-backed
initial integer 1). `8FD1C0` applies the same conditions to the actor's vector,
excluding pointers in this repair order's rejected list. It picks broken before
damaged-working components, then **lower signed RepairOrder**, keeping the first
component when these keys tie. Destroyed components are not candidates.

`895480` clears busy, resets remaining time to sentinel `1556DE8`, and performs
the requested state transition through `895530` (0→2 passes through broken).
`895530` sets flag25 bit1 and state24. Work restores health14=max1C and removes
matching wounds through `7EB780`; broken zeros health14; destroyed zeros both
14 and18. It updates owner component bookkeeping, dispatches definition
interaction events `CC/CE/D0`, and sends actor event1B with component/state.
The interaction IDs' names and full wound-list lifetime remain separate leads.

`896180` takes **a pointer to the actor's component vector**, not an actor or
component pointer. Repair callers pass `actor+B90` (assembly `8F8D93`). It obtains
the owner from the last 28-byte record's +8, evaluates owner38 bits14/9 and owner
virtual slot31 (+F8), updates bit14, can call `A606C0`, and dispatches event8.
The exact native name of this actor activation/state predicate is not yet bound.

The remaining-time sentinel `1556DE8` is in BSS and has no direct Ghidra WRITE
xrefs. Do not substitute an assumed numerical value into the contract. Likewise,
the tick delta `1556E4C` is mutable clock state: initializer `3D770` sets it to1;
the clock's containing-object update still needs alias tracing.

## Actual Repair order

`Order::eRepair` is 138 bytes; constructor `8F9240`, Execute `8F8730`.

| Offset | Meaning |
|---|---|
|38|phase (0 initially)|
|3C/40/44|target position snapshot, XYZ|
|48|allowed target displacement, initially two spatial units|
|50|embedded operation target; actor68 and target/name payload70|
|D0|raw pointer into target actor's component vector|
|D8/E0/E8|tried collision-volume pointers|
|F0/F8/100|rejected component pointers|
|108/110|linked target actor and its receiver linkage|
|118/120|linked world repair-kit entity and receiver linkage|
|128|inventory action ID; -1 means none|
|12C|repair speed multiplier, initially1|
|130|start_repair interaction latch|
|131/132|session-mode predicates captured at start|

The order requires a target actor with the `repaired` ability and a linked kit,
except for its explicit equipment-wait phase6. Start uses `8F9A50` to bind the
right-hand entity; that helper also reserves its stuff extender98 for the repairer.
`8F9B50/8F9C00` latch and emit `start_repair`/`stop_repair` interactions, with
`7EF670` gating the start notification. These are observable state changes.
Factory `8FC7C0` binds the target actor and sets rate12C to the supplied value,
except values <= about1e-5 become0.5. The constructor's initial1 is not the
factory's fallback policy.

| Phase | Static behavior |
|---|---|
|0|Wait for inventory availability, choose a component and approach volume. 0 from chooser installs a child and enters1; 1 finishes. The caller handles failure2, but the reviewed chooser only returns0/1.|
|1|Tick approach; success snapshots target XYZ and enters2; failure asks the chooser for another approach.|
|2|Wait for an outstanding inventory action. Free the right hand via `DropToGround` if needed; otherwise enter3 and synchronize the initial repair target/rate.|
|3|Request FSM state `repair`, mode2. Recheck component eligibility; an invalid component returns to0. Otherwise set busy, mark actorBA8 dirty, update actor state, initialize remaining time when at sentinel, enter4 and emit start.|
|4|Maintain FSM repair request, check target displacement, and decrement integer time by `trunc(float(clockDelta) * rate)`, clamped to the sentinel. At the sentinel restore the component to work/full normal health and enter5. A target displacement >= the tolerance fails the order.|
|5|Release kit reservation, request deletion of the world kit entity, clear its link, reset action ID and return to0.|
|6|Wait for an inventory action, bind the newly held kit, reset action ID and return to0.|

Phase4 FSM rejection clears busy and enters5. A preexisting sentinel also ends
work without another decrement. This is a countdown followed by restoration;
the loop does not gradually add health. The decrement is integer-truncated each
tick, so fractional rates are not equivalent to accumulating fractional work.
Phase5 deletion is also used by interrupted work that reaches that phase.
After clearing the kit and returning to0, the next ordinary tick fails the
missing-kit gate unless an external path rebinds it; it does not silently acquire
another kit. Phase6's setter/reachability remains a lead.

`8F9360` retains an eligible current component or calls the picker. It scans the
target actor's volume vector510/518 for `volume38 == component`, refreshes each
volume (`795890`), and picks the nearest untried volume center7C/80/84 by squared
XYZ distance. If none remains, it rejects that component and tries another.
It records the selected volume, links the target actor at108, and creates the
approach order through `8E0440` using volume definition30+398 and four times the
repairer's virtual extent. Neither component pointers nor volume pointers are
SoftLinkable handles: component/geometry replacement is a lifetime dependency.

The alternate session path `8F9C90` maintains the repair FSM and interaction
latch. Cleanup clears component busy (when component and linked target remain),
marks BA8 dirty, drops the target link and component pointer, and checks every
component for state0. All-work dispatches eventB and can show
`hints/vehicle_repaired`; it also refreshes selected-actor UI and emits stop.
This path does not independently reproduce the authoritative countdown.

Query slot11 `8F9670` exposes the current component only for query1 with order
flag18 bit16. Debug slot18 `8F96A0` names phases approach/free_hands/start/work/end.
Progress slot12 `8F9950` computes `1 - remaining / computedDuration` during phase4;
there is no explicit zero-duration guard in that routine. Load/save
`8FD6F0/8FD790` delegate to target serialization; runtime progress pointers are
not serialized there. Rebind `8FD710` resolves owner to actor+AF0. Destructor
`8FD0A0/8FD4C0` releases linked receivers, vectors, embedded target and base order.

Command59 `eOrderRepairCommand` has mode84. Mode0 carries target actor's short
routing ID18 and float rate80; `AEF40` resolves the target (`A5DE20`), constructs
Repair and installs it with queue mode1. Mode1 carries phase88, component-name
token20 and remaining time8C. Encode/decode are `AEC80/AEDE0`. Reception only
applies this update when actorDC8's active orderF0 is a Repair.

`8FA020` stores the incoming phase. Phase1 emits talk11; phase3 resolves the
named component (`895E40`), links target108, sets busy and copies remaining time;
phase4 updates remaining time on the current component. Phase5 clears busy,
checks that the nonempty name matches the current component, copies time, and
restores work/full health if that time is the sentinel. An unnamed phase5 does
not take this restoration branch. This explains the distinction between the
normal-completion command and the FSM-rejection/end command.

World-kit deletion `6FCDC0` checks entity38 bit11 to avoid repeat deletion,
dispatches local deletion when permitted and can emit `eEntityDeleteCommand`25.
The local helper `6FEBC0` changes bit12 only for nonzero mode, then sends entity
event4 through virtual slot35 (+118). Assembly retains the entity in RCX;
Ghidra incorrectly prints a zero receiver here. The downstream event handler,
deferred destruction and underlying inventory-item fate remain open. Deleting
the wrapper alone does not establish whether the kit's inventory count is consumed.

## Tool acquisition and transfer

SmartRepair's high-level retry/provider policy is in the vehicle-brain pass.
The lower orders perform asynchronous inventory actions; action completion is
not the same as a guaranteed successful transfer.

`Order::eKitExchange` (`8FAD60`, factory `8FCA70`) has linked other actor38/40,
direction48 and inventory action4C. It approaches the other actor within
0.6 spatial units. Direction1 takes a kit from the other actor; direction0
returns one to it. With a recipient inventory it searches the source for
`repair_kit` and queues **Xchange with amount=1**. Without a recipient inventory
it queues `PutFromRight` for the repairer. A missing source kit leaves action=-1
and returns active, permitting another approach/retry; it is not an explicit
failure at that point. An absent linked other actor fails. Completion waits
on the inventory action ID. Cleanup itself does not cancel that action.

`8CD4C0`, previously a generic world-pickup lead, constructs **Order::eCollect**,
size58. Execute `8CE230` requires a non-null world target whose 1C0 bit28 is
clear. Unless option4C suppresses it, it creates an approach with radius twice
the actor's virtual extent, multiplied by1.5 for chassis kind1. A stuff-extender
reservation98 rejects collection. After approach, it requires the right-hand
holder's reservation vector80/88 to be empty (not necessarily an empty hand),
queues PickupToRight with `try_arrange_box` parameter15798B0 set1 (initializer
556E0), records action48 and stage50=1, and waits for action completion.
Collect and TakeItem are distinct order types. Its load/save8CFC20/8D0140
serialize linked entity ID; rebind8CFF00 resolves owner actor+AF0.

`Order::eTakeItem` (`8EA920`, size370) operates on tag filter38 and action368.
It waits for inventory availability, succeeds if the held item's definition
tagsEC8 match, otherwise finds a matching inventory item, waits if reserved,
and queues TakeToRight. No matching item fails. Its load/save helpers serialize
the filter; rebind resolves owner actor+AF0.

`Order::eOrderTake` (`9B56A0`) first exchanges a matching item from linked other
actor368/370 into its owner's inventory, then equips it with TakeToRight.
Stage37C separates these steps; action378 is polled on the actor owning that
operation (other actor for transfer, own actor for equip). Missing source/actor
fails. Load/save `9B5C40/9B5CB0` preserve stage, filter and other-actor ID.

## Inventory operation state and ownership

The common constructor `7D2F40` initializes source iterator8, destination48,
operation owner88, saved source iterator90, phaseD0=1, cleanup latchD4=0,
parent/child bookkeepingD8/E0, actionF0=-1 and other operation state through110.
Iterators are 40 bytes. `7D30D0` recursively ticks a child, deletes finished
children, invokes virtual slot9 while phase changes, and calls slot10 once when
phase=-1 and cleanup latch is set. Its return value is **active**, not success.
`7D3620` drains deferred event rows `{event, operationSubID}`, searches this
operation and its child chain by F4, dispatches virtual slot6 when found, and
requeues unmatched rows at100/108/110. Thus its deferred work is event delivery,
not merely allocator cleanup.

TakeToRight (`7D6610`, factory `7D6410`, size120) binds destination to the right
hand, rejects invalid/reserved inputs, can insert PrepareForItem or PutFromRight
suboperations, and emits actor event2E with item definition tagsEC8 before its
mount animation. Chassis kind3 emits talk12 and ends instead. In its animation
phase it checks the current FSM's `board` tag (initializer `580E0`): this path
stores the definition at human13C0 and ends. Otherwise it requests the item's
mount animation via definition118+C0, and event3 transfers the item exactly once
(`7D6550`). Completion resets the FSM; `lock_item_change` parameter1579910
(initializer `55620`) can set brain47C from `weapon/change_time`. The absent
property fallback constant10004DC is about **1e38**, not10000.

TakeToRight bytes118/119/11A mean transferred/prepared destination/reservations
active. `7D6480` prepares the destination holder on event0; `7D6550` releases
reservations then mounts the item on event3. `7D8DA0` reserves/unreserves both
iterators with identity `{operation pointer, TakeToRight RTTI}`. Cleanup7D6B10
releases reservation and FSM state and unwinds a prepared but untransferred item.

Holder slot3 `7D1490` returns equipped item3D8. Slot6 `7D13B0` tests whether that
pointer/its definition40 is null or its count48 is zero; its Boolean result is
in AL despite Ghidra's inferred wider return. Holder reservation vector80/88 is
distinct. These slot contracts were first mapped in the armor-events pass.

`7CF8B0` chooses reservation storage according to iterator representation:
standalone item18→its +8 blocker, holder20→holder78 blocker, or inventory28→grid
reservation with coordinates30/31. Grid insertion rejects the sentinel position;
release uses `7DD8A0`. These paths still require container-lifetime tracing.
The common blocker `553E30` stores 18-byte records `{owner pointer, RTTI pointer,
quant/tick stamp}`. Reservation identity compares only owner pointer; repeated
reservation refreshes its stamp. Release removes the matching record or logs
`Unreserve failed`. This proves why an empty vector80/88 is a reservation check.

`7C97F0(item,holder)` detaches the previous placement, sets itemD8=2 and holder38,
calls holder slot1 with add=true, and refreshes item state (`7C9720`). Null holder
clears placement to0. `7C98D0` puts an item back into its inventory30: detach the
old holder/container, set D8=1, clear holder38, notify inventory via `7DD500` and
refresh. Item inventory30, holder38 and world entity60 have different lifetimes.

Xchange (`7D4900/7D4FA0`, constructor `7D89B0`, size168) uses event3 for transfer
and event-1 for synchronized failure/end. It validates a positive amount within
source stack count48, destination capacity and any explicit placement vector.
It can split a stack, materialize a held item into a world entity, detach/insert
inventory items, restore leftovers to the saved source iterator and enqueue
PickupToRight or UseItem on the recipient. Living humans and dead/incapacitated
sources follow different animation paths. The auto-use parameter is spelled
`taake` in the native code. Full capacity, stack and holder mutations remain open.

`7D9100` returns0 for an accepted existing destination,1 after selecting a new
destination/placement plan,2 for failure. Automatic placement scans grid rows
then columns for enough capacity; its alternative multi-location plan is stored
at150/158/160. The synchronization path transmits destination/plan only for1.
`7D97E0` deduplicates operation owner/source/destination actors, checks all their
inventory action gates before acquiring any, records a token130 and linked actor
vector138, then sets129. `7D8EE0` sets phase=-1, done128=1 and releases those gates
and links. The token/action-set helper implementations remain a static lead.
Gate check `7CA8E0` permits a free/same owner tokenC8 and requires every other
active operation in A8/B0 to remain phase1 with no childE0. `7CA940` repeats those
conditions then stores the non-null token; `7CA9B0` clears only an equal token.
The token's root-operation derivation `7D33B0` remains to be traced.

`7CCC70` reaches the actor's inventory adapter and `7CA9D0`, which returns true
when **no queued operation has matching actionF0**. It does not inspect a success
result. `7CCEB0` looks up `hand_right` (token15795B0, initializer541E0) and tests
its reservation vector80/88. Its previous broad label "active inventory work"
is narrowed to this specific right-hand reservation predicate.

DropToGround `7D78D0` delegates a hand item to DropFromHand, otherwise calls
`7D8350` to materialize/place it in the world. `7D8350` handles attachment bone,
position/orientation, perturbation and physics setup; it is a mutating helper,
not a position query. Its complete placement/physics dependencies remain open.

PickupToRight `7D7D30` rejects dead owners, invalid source, chassis3 and certain
height/reach conditions (sourceA30 + sourceA2C + ownerA2C must be <=110 unless
source30 bit2 is set), checks capacity and inserts PrepareForItem. It then
checks the stuff-extender reservation, claims it, sets cleanupD4 and emits
actor event2F. Animation uses item definition118+60 and event3 applies transfer.
`7D7AD0` checks capacity again, inserts the item and invokes post-transfer equip;
failure can still mark its operation done. `7D8250` releases the world-item
reservation and clears the FSM channel. `7D8FC0` updates stuff98/A0 using the
actor's SoftLinkable receiver chain; `7D90A0` clears it. `7D9B80` sends `head`
items to the head holder, `body` to body, skips automatic equip for `ammo` and
`bandage`, otherwise uses the right hand. It mounts only when the selected
holder is unreserved and slot6-empty. The item is found again by definition
tagsEC8 in the recipient inventory. Collect completion alone does not prove a
kit reached the hand.

## Connections closed from the vehicle pass

Initializer `484D0` binds last-enemy search exclusion tag156FCC0 to `airborne`.
Median helper `98C190` uses the same squared XYZ crew-subject distance as the
other sort branches. `98D6E0` swaps retained subject pointers with balanced
reference-count operations; the vector does not merely borrow those pointers.

## Evidence and remaining work

[Manifest](../artifacts/build-25492057/repair-tooling-map.json) records98 game
contracts/slices,14 selected editor comparisons and86 fields.
[Validation](../artifacts/build-25492057/repair-tooling-validation.json) checks
the binary hashes, 9,671 instruction locations,26 RTTI tables, both component
state enums,13 constants,33 byte sequences and five archive-member hashes.
Exports contain235 game/123 editor functions; the export count includes discovery
leads and does not mean all those functions are semantically mapped.
Independent LLVM disassembly matched5,336 instruction starts across32 functions
after normalizing separate LOCK prefixes. This validates instruction boundaries,
not the interpretations by itself. Assembly specifically confirms signed
RepairOrder comparisons, truncating time conversion, actor+B90 adjustment and
Boolean AL returns.

Editor counterparts preserve the reviewed parser fields, component ranking,
duration formula, repair factory fallback, phase messages, and collection/equipment
gates. They also add assertions and diagnostics (for example an empty component
container or a stuff extender without its item), and outline some game-inline
work. The map does not assert whole-function/transitive equivalence.

The kit definition in `gamelogic.pak:set/stuff/special/repair_kit` is a two-hand
item with `distributable` and `drop_to_vehicle_when_board` tags, size3×2 and
weight100. Vehicle/airborne definitions contain RepairTime/RepairOrder bindings;
the native picker supplies their ordering semantics. FSM inventory definitions
provide separate standing/prone pickup/drop paths. Resource values are evidence
for this installation; inheritance and mission overrides remain relevant.

Continue phase6 reachability, clock aliases, actor component/volume replacement,
component-name lookup895E40, wound removal7EB780 and interaction event names.
For inventories, continue queued-operation creation/update/cancellation, iterator
representations and invalidation, root token7D33B0, capacity/stack helpers,
PrepareForItem/DropFromHand/PutFromRight/Unmount variants, and item↔world entity
materialization7C9D80. Entity event4/deferred deletion must be followed before
claiming whether the underlying kit item is consumed. These are productive
static leads, not runtime-only boundaries; the overall investigation stays active.
