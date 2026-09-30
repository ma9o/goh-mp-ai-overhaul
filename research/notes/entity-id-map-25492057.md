# Entity IDs, registration and deferred destruction — build 25492057

Static contracts for52 paired methods/slices and9 entries present in only one
side of this map. The [manifest](../artifacts/build-25492057/entity-id-map.json)
records113 function entries,40 fields and12 RTTI tables. Scene/frame functions
are explicitly scoped to identity and disposal ordering; their entire behavior
is not claimed. RVAs and field offsets below are hexadecimal, except explicitly
decimal ID ranges. Vtable slots and bit indices are decimal. No game execution
or binary changes.

The [validation](../artifacts/build-25492057/entity-id-validation.json) checks
fingerprints, entries, constants, tables, raw bytes and instruction locations.
Independent LLVM disassembly checks all113 function bodies /12,219 instruction
starts and two raw regions. Discovery exports contain90 game/95 editor functions;
the extra callers are not all semantically reviewed.
These are evidence checks, not proof of semantics, runtime ABI or completeness.

## Three different meanings of “alive”

An ID lookup returns a **borrowed raw pointer**. It does not retain the object,
check pending deletion/retirement or verify a generation. The actor lookup
`A5DE20/8E66C0` uses ushort actorDAA; zero returns null. The game-entity lookup
`A5FBD0/8E8690` uses uint32 entityC0. Actor-map nodes have ushort key20 and pointer28;
game-entity-map nodes use uint32 key20 and pointer28. The maps are separate.

The earlier [retirement map](inventory-ownership-map-25492057.md) established:

1. Entity event4 marks38 bit11 and requests scene cleanup.
2. Cleanup removes the entity from the scene vector, invokes retirement callbacks,
   unregisters its IDs and releases the scene reference.
3. Final eligible reference release invokes or queues the destructor.
4. Base destruction finally invalidates remaining SoftLinkable receivers.

A linked pointer can therefore survive unregistration, and an entity still in a
lookup map can already have a deletion request. A custom brain cannot treat a
nonnull receiver or a remembered numeric ID as a complete validity test.

## ID banks and class selection

The actor and entity registrator singletons have four48-byte descriptors at
10/58/A0/E8. Each holds count0, reuse preference delay4, base8, owned pool10,
remap tree18/20 and string28. Names are `map`, `mission`, empty and empty.
Class selection is1→10,4→A0,5→E8,0/2/3→58. Range lookup searches nonempty banks
for `base <= ID < base+count`; game falls back to58 on failure. GEM diagnoses
invalid ranges/classes before the same fallback. This is not input validation
for arbitrary callers.

Actor banks, in **decimal**:

| Descriptor | Count | Base | Normal allocated IDs |
|---|---:|---:|---|
|10, map|9999|10000|10001–19998|
|58, mission|9999|0|1–9998|
|A0|0|0|none|
|E8, special|9999|20000|20001–29998|

Actor SceneCreate reserves local0 in every nonempty bank. Entity SceneCreate
does not reserve it. Each entity bank has count7FFF, giving these **hexadecimal**
ranges: `0..7FFE`, `8000..FFFE`, `10000..17FFE`, `18000..1FFFE`.
Entity invalid ID isFFFFFFFF. Those are normal allocation bounds; reserve,
release and explicit rekey do not themselves guard every invalid index.

`A5EFD0/8E7780` and `A60F30/8E9C00` construct the singleton descriptors, while
SceneCreateFB1 creates their pools. SceneDestroyFB3 frees all pools and remaps.
GEM's entity destroy handler also asserts that the game-entity lookup map and
polygon-extractor vector are empty. The registrar RTTI is a struct (`AU`),
with event-handler slot1 and a secondary singleton subobject at130.

## Allocation and reuse are preferences

Pool constructor `5543D0/3DB850` stores the exact count, allocates a used-bit
array and an8-byte-node tree `{freeCount,priority}`. Partial final words are
bounded by free counts, rather than pre-marking every padding bit. Allocation
`554560/3DB9E0` initializes its output toFFFFFFFF, then calls recursive search
`554790/3DBC40`. The native success return is **AL**, despite Ghidra's void
prototype; GEM checks it at registration/allocation sites.

Search first prefers the child with smaller `max(0,priority-reference)`;
ties prefer left. If that child cannot supply an ID it tries the other. A leaf
takes its lowest clear bit. Reserve sets a previously clear bit and decrements
free counts. Release clears a set bit, increments counts and writes
`priority=reference+delay` along the path. Their AL return reports a change.

**A positive delay does not prohibit reuse.** Search does not wait for a deadline.
An ID in a positively ranked branch remains eligible and can be selected during
the next allocation. These IDs contain no generation in the reviewed format.

Reference18 starts0 and advances once per scene Quant: `A1B320/8A2FB0` publishes
cmQuantEntitySync104E then calls `A5FE20/8E8960`, which advances four entity pools
and then the four actor pools. No time delta is passed. Above2^30−1, the rebase
helper replaces each priority by `max(0,priority-oldReference)` and resets the
reference to0.

Actor delays start0. Entity singleton construction captures `int(T*60)` for
three banks and `int(T*10)` for A0. T's file-initial value is1000, giving60000 and
10000 **counter increments**. Calling these millisecond cooldowns would combine
two incorrect assumptions: the counter is per-Quant and the preference is soft.

## Registration, collisions and changing identity

`A60150/8E8C70` has actor registration as one part of entity registration.
Entity30 bit1 selects the actor role; bit0 selects game-entity lookup membership.
Registration adds the raw entity to scene48 and, for definitionB0+4D0 bit10,
the polygon-extractor vector. Unregistration removes lookup entries and releases
valid pool IDs; removal from scene48 happens in scene cleanup, outside this helper.
Both directions publish cmSC_EntityRegister1050 with bool38 and raw entity40.

Actor registration `A5DF80/8E6820` allocates an ID when DAA is0. An existing free
ID is reserved; an occupied ID gets a new local ID stored in a remap tree keyed
by the old local ID. Repeated collisions on the same key can overwrite the
remapped value. Actor unregistration frees its pool ID only if actorDB4 bit10
is set, and does not clear DAA. The map stores raw actor pointers without a retain.

Entity assignment `A5FEE0/8E8A20` follows equivalent collision logic, with an
additional entity38 bit16 path that releases the old ID and allocates from class4.
On a previously invalid ID, the special argument chooses class5. Otherwise
bit16 overrides the ordinary low-two-bit class with4.

**Decompiler correction:** the game's valid-ID/bit16 path retains EDI=4 from
CMOVNZ atA5FF18. Ghidra instead prints the original low-two-bit class in one
switch. Two case blocks atA5FF94..A5FFAB are outside its function body entirely.
Both switch tables, those blocks and independent assembly establish the actual
class4 selection. The manifest preserves the table bytes and raw code region.

Remap helpers `A5DDA0/8E6640` and `A5FB60/8E8620` mutate an ID in place after one
exact old-local lookup; they do not follow chains or resolve a pointer. Scene
creation clears the first three banks' remaps. LoadMap1017 with bool38true clears
map remaps; LoadMission101A true clears mission and A0. The fourth bank is not
cleared by those load branches. Loading publishes false before and true after
the load unless its event-suppression flag is set. GEM also clears first-three
remaps before its reviewed object-import path9AC5B0.

The following operations are distinct:

| Operation | Relevant side effects |
|---|---|
|Actor numeric setter7EEE70/671070, slot57|Entity event2C before DAA write; prior actor handler forwards this to chassis slot59|
|Entity numeric setter6FC830/5761A0|Writes C0 and reseeds RNG at entity160; RNG draw counter persists|
|Actor rekeyA5E5F0/8E6D80|Updates map keys, releases/reserves pool IDs, then slot57; equal-ID early return|
|Entity rekeyA60410/8E8FD0|Optional actor rekey, game-entity map update, pool release/reserve, C0 setter|
|Class settersA5E760/A60590|Allocate from requested bank, rekey, then store `class & 3` in entity38|

The numeric setters alone do not update lookup maps. Explicit rekey routines
rely on caller invariants; they are not safe general collision-resolution APIs.
The located game class-set caller429F50 passes1 in its autosave-related path.
Special class4/5 would truncate to0/1 in the stored class field, and actor bank4
is empty; this map does not assert that arbitrary class inputs are valid.

## Deferred destruction and phase ordering

`652A10/4C7E10` receives a raw pointer. With global mode1 it locks a mutex,
appends that pointer to the active vector and unlocks. It retains nothing and
does not remove duplicates. With mode0 it invokes nonnull object's slot0 with
deleting flag1 immediately. The file-initial mode is1.

Normal drain `652AE0/4C7EE0` swaps active and secondary vectors, captures the old
active bounds, destroys nonnull pointers in forward order and clears the drained
vector's end. Destructors can enqueue into the new active bank; those objects
wait for a subsequent drain, which can occur during the same frame.

Shutdown drain `652BA0/4C7FB0` sets mode0 before destroying the captured batch.
Nested disposal therefore becomes immediate. Neither drain takes the enqueue
mutex or waits for workers itself. External phase ordering is necessary; the
enqueue lock alone does not establish concurrent drain safety.

Located normal drain sites are after FrameEnd20, after FrameFinallyQuant23 and
after SceneDestroyFB3. Scene teardown first marks closing, snapshots its entity
vector, retires/unregisters/releases each entity and clears the vector. Its later
SceneDestroy publication frees ID pools before the deferred destructor drain.
The main loop publishes **cmAppStarted13 with bool38false** at shutdown, then
uses disable-and-drain. It is not a separately named cmAppQuit event.

Game atexitFD79A0 frees the two vectors' backing storage without destroying
queued objects. GEM initializer2DD80 registers raw callbackE5D830, which invokes
the vector-destructor iterator twice with4C8380; that destructor likewise only
frees storage. The raw callback is fingerprinted and independently disassembled
without inventing a Ghidra function entry.

## Continuing leads

The next identity-related dependencies are worker quiescence/event-router
lifetime, drain reentrancy and caller constraints around rekey/remapping. The
player setter7EEEC0 and actorID notification's derived chassis callbacks connect
this pass to the remaining ownership, brain flags and group-coordination work.
The [frontier](static-frontier-25492057.md) remains active: this closes a concrete
set of identity/disposal contracts, not the whole static investigation.
