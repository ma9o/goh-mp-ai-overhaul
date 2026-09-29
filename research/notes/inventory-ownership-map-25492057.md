# Inventory queues and item ownership — build 25492057

Static review of117 game contracts/slices,25 paired editor contracts plus an
outlined editor decoder, and83 fields. All addresses/member offsets below are
hexadecimal game RVAs/offsets; slot indices are decimal. This continues the
[repair-tooling pass](repair-tooling-map-25492057.md). The
[manifest](../artifacts/build-25492057/inventory-ownership-map.json) and
[validation](../artifacts/build-25492057/inventory-ownership-validation.json)
check binary fingerprints, exact entries,30 RTTI tables,23 byte sequences and
11,893 instruction locations. Independent LLVM comparison covers33 functions /
3,053 instruction starts after normalizing standalone LOCK prefixes. These checks
validate evidence structure and bytes, not semantic correctness or runtime ABI.
Discovery exports contain226 game/123 editor functions; that is not equivalent
to semantic coverage. No game execution, hooks or binary changes were used.

## Location identity and lifetime

The operation source/destination descriptors occupy40 bytes each. Constructors
`7CE320/7CE470/7CE4D0/7CE5C0/7CE710/7CE780` and copy/assignment `7CE360/7D00B0` establish:

| Offset | Meaning |
|---|---|
|0|kind:0 empty,1 world entity,2 inventory box/cell,3 holder,4 item|
|8/10|entity SoftLinkable receiver|
|18|direct/pinned item pointer, protected by a shared-object reservation|
|20|raw holder pointer|
|28|raw box pointer|
|30/31|two byte cell coordinates, initialized from sentinel1480BE0|
|34|requested amount, initially-1|
|38|box iteration cursor, initially0; incremented by tag search, INT_MAX at exhaustion|

Holder construction links holder3D0's owner; box construction links box38's
owner. A held item may also carry its box and cell coordinates, so the holder
and box fields are not mutually exclusive. Cell construction snapshots amount
from item48 when supplied amount<0 and dynamically casts its holder to eHolder.
Item construction7CE4D0 uses its existing box/owner/cell when item30 is nonnull;
otherwise it reserves the actual item in direct pointer18. Kind4 therefore does
not imply pointer18 is populated, and a direct item is not necessarily a template.

`7CE970` resolves direct item18 first. Otherwise, a live entity link is required:
box/cell takes priority, then a nonempty holder via slot3, then the world's
stuff extender88. Thus an entity receiver becoming null disables access through
its raw box/holder pointers. It does not by itself prove those pointers survive
replacement while the entity remains alive. `7CEA30` is invalid/empty when the
resolved item, definition40 or count48 is absent/zero.

Copy `7CE360` and destructor `7CE8F0` register/unregister direct item18 using
`{location address, Inventory::eLocation RTTI}` in the item's blocker+8. After
release, the item can be deleted when the blocker is empty, world entity60 is
null and holder38 is null. The destructor then unlinks entity8/10. Pointer18
is not an ordinary borrowed pointer or a reference-counted handle.
Setter `7D01F0` releases/deletes the old direct item before reserving the new one,
without an equality guard. Assignment7D00B0 also has no self-assignment test.
Calling these on the same location/item therefore needs the caller's lifetime
invariants; they are not safe generic pointer-copy primitives.

Search `7CEC50` walks the box's item vector using cursor38 and7D9F40, advances
the cursor, matches definition tagsEC8 (mode0 helper556F20, mode1 helper556FC0),
optionally evaluates a callback, then records cell30 and amount34. Empty filter
accepts any item. Exhaustion sets cursor=INT_MAX, sentinel coordinates and amount=-1.
Thus a search mutates the location and must be reset/recreated to restart.

`7CF620` checks reservations on holder80/88 or the item at a box cell, only with
a live entity receiver. It does not inspect the direct item18 reservation list.
`7CF360` extracts according to representation. Box extraction uses requested
amount (default1). Holder/world forms return the current item. Direct item18
instead copies its20-byte handle, releases its reservation, deletes that item,
and constructs a new E8-byte item with requested/default1 count. That path can
replace identity even when item definition remains identical.

## Operation queue and cancellation

`Inventory::eManager` is the inventory extender at entity cache578+28, sizeD0.
Constructor `7CA460` clears holder vector88/90/98, boxA0, operation vectorA8/B0/B8,
next action ID C0, byteC4 and exclusive tokenC8. `7CB6D0` rebinds each holder's
owner3D0 and the box's owner38 to extender owner18.

Operation **builder** (`7CC380`) is separate from a running operation: source0,
destination40, preferred owner80, operation name88, parameters90, ownership byte98,
optional interaction stringA0, remote-allowedC0 and requested action ID C4=-1.
`7CC600` constructs the named operation and enqueues it through the resolved
actor's inventory manager; it can emit transfer command10 and relinquishes the
builder's operation-name/ownership state. Name ownership/freeing and
command payload are described below. The owned-name byte98 controls freeing of
the operation name; submit then clears name88.

`7D34E0` creates a running operation from the name registry, copies source8 and
destination48, selects an owner (explicit, else source entity, else destination
entity), and sets parentE8. It obtains sub-ID F4 from the root's incrementing F8.
**D8 is the retained Inventory::eParams pointer**, not a parent pointer.
`7D33E0` creates a child using the same rules, inherits params, and stores childE0.
`7D33B0` follows parentE8 to the root; this is the exclusive gate token used by
Xchange. The repair pass's vague D8/E0 bookkeeping description is narrowed here.

`7CAA10` enqueues a non-null operation. An ownerless manager ticks and deletes it
immediately, returning action ID -1. Otherwise an unspecified ID uses nextC0;
an explicit ID raises nextC0 to at least ID+1. It writes operationF0, appends to
A8/B0 and enables quant updates (`798E20`). No duplicate-ID check is visible here.

`7CAC40` handles manager eventB. It scans queued operations in order, considering
only operations permitted by exclusive tokenC8. After a virtual slot3-blocking
operation is encountered, a later eligible operation waits until another update,
even if the blocking operation finishes during this update. Eligible operations
run `7D30D0`; inactive ones are deleted and nulled, then null slots are compacted.
It re-reads vector bounds during iteration and disables updates when empty.

`7CABB0` cancels and deletes every operation, erasing each row. `7CB640` instead
handles an exclusive operation owned by another actor: it gets that operation's
action ID and sends actor event16 to its owner. Game-entity event16 routes to
`7CAB00`.

Manager event0 refreshes holders and the box; event6 invokes external-token
cancellation, cancels local operations and invokes box cleanup. Destructor
`7CA600` does the same cancellation first, releases holder references through
their adjusted resource subobject+68, releases the box, frees vectors and invokes
the base extender cleanup. Slot0 `7CBE70` deletes the D0-byte manager.

`7CAB00` cancels the first matching action ID, deletes it and erases its queue
row. `7D31D0` recursively cancels/deletes the child, invokes virtual slot8,
drains deferred events7D3620, runs slot10 if cleanupD4 was set, then can emit
cancel command11 for a valid actionF0. Xchange slot8 (`7D48F0`, thunk7D8EE0)
releases its exclusive actor gates and linked receivers. The base operation's
slot8 is empty. Base destructor7D3AE0 releases paramsD8, deferred-event storage
and all three location descriptors; it does not itself perform the child unwind.

Transfer command10 encode3D8DD0 writes entity identity, builder and action ID.
**Decode3D8E20 enqueues work immediately**: it reconstructs a local builder,
sets requested IDC4, enables remote-allowedC0 and calls7CC600. Cancel command11
decode3D8F20 similarly calls the actor cancel wrapper after reading its ID.
Their slot4 forwarding to the generic entity-command receiver is not the point
where transfer/cancellation occurs. Generic receiver14BFC0 (actor thunkB9CB0)
handles entity deletion25 and26; command25 invokes6FEBC0 with its mode18.

Builder codec7CC8F0/7CC410 carries operation name, preferred owner entityC0 ID,
source and destination locations, serialized params and the optional interaction
string. Decode allocates an owned name and, for a nonempty parameter string,
an eParams object. Location codec7CF970/7CFB80 writes kind as a byte, followed by
presence bits:0 entity ID,1 direct item's handle,2 that item's world entity ID,
3 holder name,4 cell bytes,5 amount,6 box. The box is recovered from the linked
entity; no raw address is serialized. A direct item is reconstructed as a new
E8-byte item from its handle, even when a world identity is included. Amount34
is int32 in memory but only uint16 on this wire path; iteration cursor38 is not
serialized. Treat this as a command representation, not a full pointer snapshot.

## Item, holder and world entity

Items are E8 bytes (`7C8B50/7CA350`). Fields28=stack-compatible enable byte
(initial1),29/2A=cell,30=box,38=holder,40..5F=two-part item handle,
60/68=world entity receiver,70=incrementing item identity, D8=placement kind,
E0=split naming counter. Constructor7C8B50's DC value uses an explicit nonzero
argument clamped to nonnegative, else definition470 when definition47C is true,
else0; its native meaning remains open.
`7C99C0` changes the world entity receiver and registers/unregisters this item
with entity delivery collection+198 using subscription mask4. Item delivery
callback `7CA240` removes it from the box's item vector, notifies its holder
through slot0, then clears world linkage. Holder slot0 is7D10C0: clear item3D8,
then owner event12. It is not a deleting destructor. Holder destruction is slot4
7D12C0, also reached through adjusted resource/blocker subobject destructors.

Item destruction detaches its placement (holder slot1 or box notification),
sets placement0/holdernull, calls `7CA2D0` to disconnect any world stuff extender,
destroys the name token78, world receiver60 and reservation base8.
`7CA2D0` calls `7B32E0` on the linked stuff extender before clearing the entity.
`7DA1C0` erases an item pointer from box10/18 and emits owner event11 unless
box30 suppresses notifications.

Stuff-extender destructor `7B43A0` first clears its item's world link and then
**deletes the underlying item**, clears88, releases its other state/actor
reservation and destroys the extender (size148). This establishes the final
ownership relation. Rebinding7B3290 also deletes the previous item when it differs
from the replacement. Unlink7B32E0 simply clears extender88.

Materialization7C9330 returns an existing world receiver or builds an entity from
the primary item definition, links it, and replaces the new stuff extender's
item through7B3290. The entity-builder internals remain outside this contract.
Wrapper7C9D80 then calls7C9DC0, which detaches holder/box placement and extracts
the item from its previous box. Neither function is a passive read accessor.
An existing world entity goes through6FC850 (spawn initialization, guarded by
entity38 bit13) and6FD200(...,0), which changes entity38 bit9 and invokes slot40
when changed (vtable byte offset140; slot40 decimal). The slot's full
visibility/physics effects are not reviewed here.

7C9720 only acts on a linked world entity with entity1C0 bit28 clear. A special
world-preservation predicate7C9280 chooses6FD200(...,1); otherwise it clears
the stuff extender's item pointer through7CA2D0 and requests world deletion.
7C9790 unconditionally performs the latter unlink/delete sequence when linked.
Thus a visual world entity can be retired while the inventory item survives.
Repair phase5 instead calls6FCDC0 directly and does not perform that unlink;
final stuff-extender destruction can consequently dispose of the kit item.
Exact destruction time depends on entity/extender reference release below.
7C9280 rejects a linked entity when the item's definition special-tag vector
11E0/11E8 contains`keepentity` (54600→15753E0), or the entity has1C0 bits15/16,
nonempty entity4E8/4F0, or actor bit1
in entity30. With no world entity it accepts. These are preservation/stacking
conditions; do not equate this predicate with visual visibility.

`7C9C30` splits an item stack. Full-count extraction delegates to7C9DC0 and
returns the same pointer. Partial extraction constructs another item from the
handle, may clone a materialized world entity (with generated `iic-...` identity),
sets new count to requested amount and subtracts it from the original. It has no
local positive/range guard; callers must provide that invariant.

`7C9100` identifies the secondary handle as **filling stuff**: it validates
filling-definition tagsEC8 against the primary definition's allowed filling
tags120, copies secondary definition/count at item50/58, and caps count against
primary definition474. Diagnostics explicitly name item filling. Property
reflection `7B3300` exposes `stuff` and `amount`, using this setter and definition
470/474 constraints. Thus the previously unnamed secondary weight contribution
is inventory filling contents, not another independently carried item.

## Box capacity and stack mutation

The item-definition parser7E0DE0 and registry157AD00 bind `Mass`9E to10,
`Inventory/Size`A7 to40/41 and `Inventory/Weight`A8 to108. The latter is the
box capacity denominator; it is not the physical mass. `Filling`AB parses tags120,
quantity unit470 (default1), and capacity474 rounded down to a multiple of that
unit. `Preloaded`AD sets47C. No local zero-divisor guard is visible in the filling
rounding branch. `stackable` (54D50→15754A0) is searched in the definition's
11E0/11E8 special-tag vector by7E0A60; this is separate from item byte28.

Box constructor7D9D80 establishes size40: item pointer vector10/18/20, byte grid
dimensions28/29, float`weight`2C, notification suppression30 and owner38.
Parser7DC820 and registry157AB30 bind tokens37A=`item`,37B=`size`,37C=`weight`,
37D=`cell`,37E=`user`,37F=`clear`. Parser entry/exit -1/-2 toggles suppression.
Cell lookup7DD410 linearly tests each item's footprint against its origin29/2A;
it accepts interior cells, not only origins. Footprint bytes come from item
definition40/41. Invalid coordinates or no overlap return the vector-end iterator.
Index lookup7D9F40 bounds-checks and returns null outside the vector.

Capacity7DA890 first bounds-checks the cell and rejects an occupied reserved item
(reservation vector10/18 nonempty). It computes an integer quantity from
`box2C / itemDefinition108`, subtracts any existing stack count, and caps against
the incoming count. Every cell in the incoming footprint must lie inside the
grid and resolve to the same existing item (or all be empty). Nonempty existing
items must be unreserved and compatible through7C9B00. The reviewed helper does
not clamp negative results or protect the division; ordinary callers need valid
definitions and stack limits. Definition108 is native `Inventory/Weight`,
distinct from `Mass`10 used by7E2420.

Stack compatibility7C9B00 requires neither item to have a holder. An empty
destination is accepted; otherwise primary definition40, filling definition50
and filling count58 must agree, both items must satisfy7C9280 and both enable
bytes28 must be set. Equal weapon definitions alone do not establish compatibility.

Auto-position7DAAB0 visits rows then columns, returning the first nonzero capacity.
With its rearrangement option enabled it clears global plan157ABB0/8/C0 and may
call7DB260. That planner rejects any reserved existing item, uses resource
clone/retain machinery, sorts existing items plus the incoming item, simulates
placements, and emits16-byte records `{item pointer, cell bytes at8, count atC}`;
the incoming item's pointer is represented as null. Failure clears the plan.
The sorting policy remains open. The optional path is
not a pure capacity read and uses shared mutable scratch storage.
Box clone helper7DC670 clears the destination, copies size/weight, and clones
items only when copy mode bit3 is clear. The planner requests modeA, which sets
bit3 and therefore starts from an empty copied box when the clone branch runs.
The resource virtual clone-policy query can alternatively retain the original;
the complete resource machinery remains outside this reviewed contract.
Item clone7C8D50 constructs a new item from the20-byte handle. With clone-world
enabled and7C9280 false, it increments sourceE0, clones the world actor under
an`iic-...` name, links the new item and sets entity38 bit9. With clone-world=false,
as used by rearrangement, it creates no world clone. This function does not copy
the source item's reservation list, holder pointer or box pointer.

7DD630 inserts at a specified cell or merges into its occupant. It can split the
incoming stack, set box/cell, return it to inventory placement, append its pointer
and emit owner event11. Its return is the leftover pointer, not a success bool;
it does not itself call the capacity/compatibility checker.7DC550 extracts up to
the requested count, removing the vector entry and clearing placement/cell for
a full stack or splitting for a partial stack, then emits the same event.
Neither is a safe unrestricted public mutation primitive.
Merge7C9B90 first runs source world hide/delete logic, then moves
`min(requested,sourceCount)` into the destination count. It does not check stack
compatibility, destination capacity or negative counts itself.

Location insert7CEFC0 consumes and deletes the incoming item when destination18
is populated. For automatic box placement it tries compatible existing stacks
then first-fit cells, returning any remainder. For an explicit cell it calls
7DD630 directly with the full incoming count. Its optional output byte reports
particular deletion/consumption paths, not general transfer success. Wrapper
7CEF80 additionally deletes a leftover lacking a world entity and returns that
byte. Box destructor7D9E00 deletes every stored item; direct-location reservations
do not establish independent ownership against box destruction.
Clear7D9F70 is stronger: for an owner pending deletion (38 bit11) without bit12,
it first invokes7DBAC0; then it moves the box vector aside, unlinks/deletes each
remaining item's world entity, deletes the item and emits owner event11.

7DBAC0 runs only for a human-type owner (entity30 bit2) or an owner tagged
`spread_items` (56030→157ABD0). For humans it selects held items or those rejected
by7C9280. For other owners it selects items rejected by7C9280 or having a nonempty
entity resource at definition28. A one-item count or native`stackable` item is
materialized/detached as one world object at the owner's position and receives
interaction`crush_container` (55EE0→157AB50). Other stacks can produce individual
world objects, with a per-definition count capped by writable global1480BC8;
its file initial value is0 and runtime/configuration producer remains open.
That path samples random positions/height, invokes the same interaction and can
send inventory-kick command13. Detached survivors have left the box before clear
deletes its remaining items. Full scatter geometry and synchronization remain
outside this slice; the helper is not a pure deletion notification.

## Repair and entity connections

`895E40` resolves a component name by scanning28-byte records and comparing the
first definition name at328, returning the first match or null. This closes the
repair command's name-resolution helper.

Base entity event4 `6FE6F0` sets entity38 bit11 and global159A030. This requests
deferred work rather than invoking the destructor directly. Game-entity event4
`71A7C0` also releases attachment/placement state and clears ownership through
`71F1D0`; actor event4 `7F1AF0` adds control/attachment/mover cancellation.
Entity command dispatcher `14C120` calls virtual slot28 (+E0), implemented by
`14BFC0` for entity/game entity and `B9CB0` for actor (also human). Deletion command
slot4 uses this route; transfer/cancel work happens during decode as above.
Scene quant-finalization `A1C980` checks global159A030, clears it, and invokes
`A1CC10` inside the native `entity cleanup` phase. The cleanup implementation
sets cleanup-active159A031, removes entities with38 bit11 from scene vector48/50,
then retires each through6FCD50, updates its registry throughA60400, and releases
the scene's entity reference. Final reference plus destruction flagC bit0 invokes
its destructor, or schedules it through652A10 when flagC bit1 is set. Actual
deallocation is therefore conditional, not immediate at the delete request.

6FCD50 emits entity event3 unless38 bit5 already marks retirement or bit10
suppresses the event, then sets bit5. Game-entity event3 sends delivery mask7,
tears down placement state and invokes extender event6, which cancels inventory
operations. Game-entity destructor7198F0 releases extender collection578 through
7990E0: each extender reference10 is decremented; only its final eligible
reference invokes/schedules its destructor. Delivery send7E6EF0 walks subscribers
in reverse registration order and invokes slot1 when subscription mask intersects
the sent mask. A mask containing4 latches sender24 bit1; bit0 marks delivery in
progress. Unsubscribe during delivery nulls the matching row and sets bit2; the
sender compacts null rows afterwards. The item subscription4 therefore receives
the retirement mask7 before extender cancellation/cleanup. The subscription
number is a mask in this separate delivery system, not entity event4.

Base entity destructor6FC6A0 finally calls31AC70 on SoftLinkable+18, clearing both
words of every remaining receiver. Thus general linked entity pointers can stay
nonnull throughout the retirement callbacks; the item's explicit callback clears
its own world receiver earlier. A live SoftLinkable pointer alone does not prove
an entity is still registered or available for orders. The delete flag and scene
membership must be considered separately. A60150 also removes actor/ID registry
entries and emits Scene::cmSC_EntityRegister event1050 with registration=false;
the underlying actor registry and ID allocator are further dependencies.

## Editor comparison and remaining dependencies

Selected pairs agree on manager owner fields, queue ordering/enqueue IDs,
cancellation, submit/decode side effects, item receiver/disposal, holder retirement,
box copy/clear and size/weight parsing. The manifest records25 paired contracts
plus the editor's outlined builder decoder64D2A0; it does not assert full
transitive equivalence. In particular game7CC410 includes work split between
editor64CD70 and64D2A0. Editor operation creation654730 also retains a native
assertion when the name registry fails to create an operation.

Continue the remaining operation variants, derived holder callbacks, rearrangement
sort7DF600, resource clone-policy implementation, raw owner/box/holder replacement,
itemDC meaning, count-cap1480BC8 producers and kick command13. Scene ID reuse,
actor registry changes and deferred destruction652A10 remain separate lifetime
questions. Component/volume replacement, repair phase6 and clock aliases remain
in the global frontier. No runtime correctness or complete AI surface is claimed.
