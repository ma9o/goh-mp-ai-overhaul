# Equipment operations and holder callbacks — build25492057

Static review; no completeness or runtime claim. Addresses/offsets are
hexadecimal RVAs/member offsets; virtual slot indices are decimal. This pass
continues inventory-ownership and repair-tooling.

## Child-operation wrappers

Base phaseD0 starts1; -1 is inactive. The dispatcher runs childE0 before the
parent and loops when the parent's phase changes. A parent may set itself-1
while leaving a child that still has to finish. Do not interpret phase-1 alone
as proof that a submitted action has left the manager queue.

| Operation / step | Behavior |
|---|---|
|PutFromRight7D5690|Phase1 creates `_PutFromRight`, goes0. Phase0, after that child, swaps hands if left holder is nonempty, then goes-1.|
|`_PutFromRight`7D5830|Sets-1, snapshots current right-hand location; invalid/reserved stops. Item FSM type`hand-only`→DropFromHand; otherwise UnmountRight.|
|PutFromLeft7D59D0|Phase1 requires right holder unreserved, invokes PutFromRight and goes2; phase2 invokes PutFromRight again and goes-1. It uses the right-hand wrapper's automatic swap to get to the left item.|
|FreeHands7D7060|Phase1 PutFromRight→2. Phase2, if right remains nonempty, `_PutFromRight`; then-1.|
|Unmount7D5440|Invalid source ends. Right-hand source→PutFromRight, left-hand source→PutFromLeft, then-1. Other sources send event3 (and synchronization); local event handler7D5400 returns the item to inventory once, sets118 and phase-1.|
|DropFromHand7D76D0|Phase1 rejects invalid/reserved source. Ordinary actor state creates `_DropFromHand`→0; vitality174 low2 bits instead invokes immediate world placement7D8350 and ends. Phase0 swaps if left is occupied and right empty, then ends.|

Hand predicates7D0420/7D0460 require linked entity+holder and compare holder name8
against`hand_left`/`hand_right`; these are different native names from attachment
bones`left_hand`/`right_hand`.7E4F60 receives the definition's embedded inventory
view at40 and compares its FSM type string with`hand-only`; it is not a footprint
size predicate.

`Inventory::eOperationHuman` slot7 at7D41F0 is empty in this build. Being an
OperationHuman is not itself evidence of an animation callback implementation.

## Preparing an item and coexistence in hands

PrepareForItem7D6C40 phase1 requires a valid source, a nonempty right hand and
both holders unreserved; otherwise it ends. With left empty and compatible
current-right/incoming items it creates SwapHands and ends, preserving the
current weapon in the left hand. Otherwise it stows current right and goes2.
For current-right special tag`rocketlauncher`, first weapon slot's object2B8,
definition98+15F8 bit0, and charger predicate80BF00 true, it instead drops the
launcher and retries phase1.80BF00 returns true when80BF90 accepts an absent
weapon/definition or filling EC8 tag`dummy`15768E0 (initializer53EC0); otherwise it requires itemDC=0
and charger14 bit4 clear.7C9270 directly reads itemDC, not total stack count48.
Its precise loaded/chamber semantics remain open.
Phase2 checks incoming item against any left item; incompatibility creates
SwapHands and goes0. Phase0 stows a right-hand item unless empty or identical
to the incoming item, then ends.

Compatibility7D8BA0 takes current item and incoming item. Incoming must have a
definition and nonzero count. Current definition special tags11E0 must contain
`weapon` and must not contain`itemin2hands`. Incoming must have special tag
`smallitemin1hand` or matching tag`itemin1hand` inEC8. Current must not have special
tag`nokeep`; incoming EC8 must match neither`weapon` nor`nokeep`. These are concrete
equipment constraints on the AI's choice to retain a weapon while taking another
item, not a general two-item capacity check.

## UnmountRight and SwapHands

UnmountRight factory7D5B10 allocates120, clears118=transferred,119=reserved.
Step7D5C00 phase1 snapshots right location, rejects invalid/reserved, sets cleanupD4,
reserves source through7CF690 and goes0. Phase0 requests the item's FSM definition
118+120 state using7FBD70 mode1. Param`put_by_fsm`1579850 (initializer55650) changes
flags to2 and the request owner token; otherwise owner is native`unmount`.
When request is ready or118 already true it sends event3. Event7D5B80 releases
source reservation, returns valid item to inventory via7C98D0 and sets118 once.
When transferred, the step restores the current equipment state with mode0/flag1
unless put_by_fsm, then ends. Cleanup7D5F20 releases reservation and unmount-owned
FSM requests. Slot5 query7D5B70 sets result1 for query kind1.

SwapHands factory7D5FC0 allocates120 and clears118=transferred,119=request latched,
11A=reservations held. Phase1 in7D60B0 requires exactly one holder empty and both
unreserved; otherwise ends. Source is the occupied holder, destination the empty
one. It sets cleanupD4, reserves both through7D8D20 and goes0. Phase0 requires valid
source and requests`switch-weapon`157A1D0 (55D60) using mode1/owner`change`. Once
ready it latches119 and sends event3; while latched and not transferred it waits.
Event7D6030 releases reservations, mounts the source item in the destination
holder, sets118 and clears cleanupD4. The step restores current equipment state
and ends after transfer. Cancellation cleanup7D63A0 releases reservations and
change-owned FSM requests. Empty/invalid/failure completion is still not a success
result at the outer inventory-action-ID layer.

## Animated dropping

`_DropFromHand`7D73A0 phase1 sets cleanupD4 and reservation119, goes2. Phase2
requests`ground-drop`157F6B0 (58320) with mode1. Readiness causes event3/local or
synchronized.7D7200 applies that event once (118): materialize/detach7C9D80,
copy an owner virtual predicate to world38 bit21, then either place using a
hand bone's transform or detach physical linkage and send entity
event2D when world1C0 bit28 is set. It sets phase0 and118. Phase0 releases the
source reservation, restores current equipment state and ends.7D75D0 cleanup
releases reservation and the drop request's FSM owner token. The bone branch
selects`right_hand` when7D03C0 recognizes **either** source hand-holder name,
otherwise`left_hand`; it is not a direct left/right source mapping.

`properties/human_fsm/inventory.inc` binds stand/lie drop states to`ground-drop`
and returns through corresponding *_drop_end states. This establishes authored
FSM tag routing; the exact animation timeline and event producer are still open.

## UseItem dispatch

UseItem7D42E0 operates at phase1, optionally emits synchronized eventFF, sets-1,
then requires valid source, an actor source entity and vitality174 bit0 clear.
For`ammo`, it finds the first weapon whose definition has nonempty438/440 and
whose allowed filling tags120 match, then invokes charger80C520.
That call can cancel earlier loading/unloading, acquire the charger role through
781410, write the selected filling definition to item50, set charger14 bit4,
and emit commandF mode2. It is not an immediate inventory-count decrement.
Its complete predicates and charger progression remain open.
For human owners: `bandage` interrupts order component with405 and installs a
Order::eHeal (8FCA10,sizeB0); native`skin` installs Order::ePutOn
(8EA1D0,size78, location38). Both factories write those native vtables. Head/body item tags
select corresponding named holders, requiring unreserved and empty before mount.
Otherwise, a different item with entity resource28 produces TakeToRight. The
fallback requires count1, traverses weapon slots and resolves their named holder,
requires a nonempty holder maskA0/3B8, matches item tags and checks reservation
80/88, stows an existing held item, then mounts only if now empty.
Event callback7D42A0 ticks the operation if still phase1, then sets phase to the
incoming event value; this enables synchronized eventFF reception.

## Holder callbacks and passive-read boundaries

Left holder mount7D2290 performs7D2380 physical attachment then base item3D8/event12
update. Attach materializes the item, finds owner`left_hand` and item`handle` bones,
and uses physical-link helpers779FB0/779FE0. Unmount detaches through7792E0.
Refresh7D22E0 can materialize and reattach again; it is not a getter.

Right holder7D25B0 recognizes item definition special tag`weapon` through7D2A40.
Weapons mount/unmount through810A00/810AB0 on the first weapon slot; other items
use7D2720, which materializes and selects token1579190 for smallitemin1hand,
or15791F0 otherwise. Both tokens initialize to`right_hand` in this build
(54060 and53FD0); the branch does not establish different attachment bones.
Slot2 callback7D2630 invokes810B40 for
weapons or physical attach for other items. Retirement7D26B0 clears the item and,
if left is occupied, constructs a SwapHands builder and destroys it.
**Builder destructor7CC810 submits before cleanup**. This closes that apparently
missing submit call and clarifies the previous pass's builder destructor contract.

Weapon holder3E0 caches the named weapon slot via806260 during refresh7D2AC0.
Its empty predicate7D2D80 is based on slot2B8 and that object's slot3 item accessor,
not only holder3D8. Mount7D2B20 rejects an occupied weapon slot and clears incoming
item38; otherwise calls810A00 and base holder update. Unmount calls810AB0 first.
810A00 allocates a268-byte weapon object if slot2B8 absent, binds item809AA0,
updates equipment812060, and conditionally starts charger behavior80C6A0.
810AB0 clears slotD31 bit0/effects, destroys weapon object via slot4, clears2B8,
refreshes weaponry806D40 and emits owner event19.810B40 toggles slotD31 bit0 and
calls effect handlers810C70/8111F0; full effect and weapon-item lifetime are open.

Armor holder7D1660 updates item3D8 first, then7D16F0 refreshes its skeleton model.
Refresh7D16B0 calls the same helper for an occupied holder.7D16F0 resolves the
configured bone3E0 (nonempty name430 required), removes an existing child named
`armor_` plus holder name, resolves the held definition's entity resource, clones
or retains its skeleton, applies appearance, renames/reparents it and emits owner
event7. It does not create a separately linked inventory world entity through
7C9330 in this path. Full skeleton clone/appearance propagation remains open.

Reservation7CF690 with no incoming item reserves an existing direct item, holder,
or occupied box cell. With an incoming item it also checks that cell capacity
matches the complete required count. Box destination helper7DD7B0 can create a
reserved placeholder item in an empty cell and append it to the box vector.
Placeholder factory7C8D00 constructs from the incoming handle and explicitly sets
count48=0. Holder accessor7D0FF0 returns the held item's embedded handle at40 or a
global empty handle; it does not return a stuff-definition pointer directly.
Unreserve7DD8A0 removes/deletes an empty placeholder only when its reservations
are gone. Reserving capacity can therefore change box contents before transfer.
The source-only cell reserve7DD760 assumes an occupied cell; it has no local null
guard after lookup. Caller invariants matter.

## FSM and synchronization boundary

7FBD70 returns true immediately when FSM670 is disabled. Otherwise it obtains a
F8-byte request record7FD9E0. If recordE5 is not active, it sets ownerE8 and flagsF0
and calls7FE0C0. Mode1 sets recordE0=1; if readyE4, it calls7FDBB0 and returns1,
else0. Mode2 setsE0=0 and returnsE4; mode3 returnsE4; other modes return1.
7FBFB0 cancels active records matching the owner token and resets FSM D50 when
equal. Empty request names resolve through7FD9E0 to embedded fallbackD70, with
ready/active bytes both1 and DC cleared. Other names are looked up by7FDAB0;
unknown names reach the native fatal diagnostic.
Current-equipment state9388D0 returns held definition118+180 or`hold-nothing`.

Request start7FE0C0 stores mode at0, protectionC8 (API modes2/3), and sets
readyE4=0/activeE5=1. The C9 branch can attempt an immediate route unless an
already active C9 record has CC at least as high. A nonnegative switch indexD4
selects an80-byte mapping record; directionD8 selects the translation direction.
It translates queued/current state indices, resets the context and invalidates
incompatible ready requests. This is a reviewed slice, not a complete route solver.
7FDF00 sets E4 and immediately consumes mode0 requests.7FDDE0 clears E4/E5,
conditionally transfers ownerE8 to FSM D50 (flag1 uses`none`), emits entity event6
with positive DC, then clears DC. The meaning/producer of DC remains open here.
7FDBB0 consumes the request and, for mode2, can translate queued states or request
the configured exit state before reconsidering routes.

Arbitration7FE6E0 first calls7FEE10 and proceeds only with A18 clear. It collects
active, unready requests whose effective owner matches D50 (flagsF0 zero selects
`none`, otherwise E8), sorts them with800E30, and tries routes through7FCEE0.
For every active, ready record with protectionC8, a proposed route that leaves
that record's name tag is rejected when candidateCC<=protectedCC. A strictly
higher CC instead allows its ready byte to be cleared. The first accepted route
sets ownerD50 and is committed by7FD210. Full route construction, comparator,
readiness refresh, field-name parser binding and animation completion remain
productive static leads.

The installed request.inc authors decimal priorities (dead50, equipment hold45,
most inventory/swap/drop43, stance40, reload35, move30, fire25, aim20). This pass
does not yet bind those authored values to recordCC. Stand/inventory resources
connect`switch-weapon` and`ground-drop` to state tags, not to a universal fixed
animation duration.

Command14's native RTTI is misspelled`eInvetoryOpStateSyncCommand`. Constructor
3D9720 stores root action ID18, child sub-ID1C, event byte20, optional payload
presence21, location28 and vector68/70/78. Encoder3D9820 writes entity ID,32-bit
root/sub-ID, event byte and wire presence bits0=location/1=plan. Native assembly
3D9897/3D989A confirms bool conversion then OR, despite ambiguous decompiler
precedence. Plan count is uint16; each8-byte in-memory row sends cell bytes0/1
and int32 count4 (six wire bytes).

Decoder3D9950 accepts an optional trailing payload when data remains and stream
status permits it. A plan requires an included generic-box location7D0340
(live entity/box, no holder, sentinel cell), nonzero count, and count<=grid area.
After decoding it looks up action18 and sub-ID0 through7CA860. Missing root
means no operation is applied. For an included location and currently existing
target sub-ID, it replaces that operation's destination48 and passes its plan
to slot4. It then appends `{event32,subID32}` using7D32F0 and ticks the root.
Event byte20 is **sign extended** to int, so FF becomes-1.7D3370 searches only
the single childE0 chain;7D3620 requeues unmatched event sub-IDs. Payload replacement
is only attempted before that tick; a future child can receive the deferred
event without that earlier destination/plan assignment. Caller sequencing remains
an open invariant, not a proven live failure. Slot4's generic entity forwarding
is separate from these decode-time side effects.

## Comparison, validation and remaining work

The manifest records102 game contracts/slices and15 reviewed editor counterparts,
with54 fields and6522 instruction locations. The focused exports contain247 game
and144 editor functions; the extra discovery functions are not claimed reviewed.
Fifteen selected comparisons cover the wrappers, hand preparation/coexistence,
FSM request API and command decode. Editor656FE0/6574A0 contain fatal diagnostics
for invalid sources in UnmountRight/SwapHands transfer callbacks; the game skips
the transfer. GEM therefore provides useful additional invariant evidence but is
not proof of identical game behavior.

Structural verification passed for both binary hashes, exact entries,
instruction addresses,50 RTTI tables,20 initializer byte sequences,4 owner-token
pointer tables and3 installed resource members. Independent LLVM disassembly
matched4178 instruction starts in38 selected functions after normalizing LLVM's
standalone LOCK prefixes. These checks do not execute the game or prove semantics,
ABI safety, animation timing, callback ordering or exhaustive coverage.

Continue with the FSM request parser, readiness refresh7FEE10, route builder7FCEE0,
commit7FD210, sort comparator800E30 and state/animation event producers. Also open:
charger/itemDC progression, complete weapon object/effect lifetime, remaining
holder replacement paths, PutOn body beyond its factory, synchronization payload
ordering, inventory kick13 and the wider static frontier. The frozen exports and
manifest preserve this pass's bounded evidence while those leads continue.
