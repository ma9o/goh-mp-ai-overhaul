# Spatial area lifetime, overlap tracking and asynchronous path updates

Build `25492057`; all analysis is static. RVAs and object offsets below are
hexadecimal; virtual slots are decimal. This pass reviews **72 paired contracts**
in game and GEM, with **69 field records**, **10 RTTI tables** and **9,329
instruction starts** independently checked with LLVM. The machine-readable map
is [area-lifecycle-map.json](../artifacts/build-25492057/area-lifecycle-map.json).

The main result is a concrete path from entity geometry to independently owned
spatial snapshots, reused area IDs, overlap lists, and queued worker updates.
This closes the earlier registration/removal and queue-execution leads. It does
not establish every entity lifetime or scene/thread teardown precondition.

## Entity zone ownership and scale

`72FB00 / 5AAC20` constructs the zone record, whose known allocation is `468`
bytes. Its embedded `eArea` starts at `8`; working geometry is at `10`, source
geometry at `C0`, an additional pather object at `C8`, its ID at `D0`, name at
`D8`, and a tag object at `138`. Full name/tag layouts are not inferred here.

| Zone flag | Established effect |
|---|---|
| bit0 / bit1 | Enabled / suppressed in the previous entity update pass |
| bits2..3 | Manager selector: 1 general scene, 2 obstacle scene |
| bit4 | Enables enter/leave tracking on the embedded area |
| bits5..7 | Transform mode: static, translation, matrix |
| bit8 | Owns the **record allocation** |
| bit9 | Owns source and working geometry |
| bit10 | Allows replacement with a null source |
| bit11 | Forces spatial refresh |

`72FB80 / 5AACA0` takes two Boolean arguments in `DL/R8B`. They control bits8/9,
respectively. The first is not source-shape ownership. Heap Area/Obstacle parser
branches pass `(1,1)`; the embedded body zone initializer passes `(0,1)`.

`72FC20 / 5AAD40` deletes the previous source, and also the working geometry when
bit9 is set. It stores the new source and marks the zone dirty. A nonzero transform
mode or an already-set bit9 causes a slot2 clone for working geometry; otherwise
working geometry aliases the source. Null replacement without bit10 leaves an
existing working shape unchanged, or diagnoses a missing one. There is no
self-alias guard on replacement.

`72FD20 / 5AAE40` selects manager `159A0A0` or `159A220`, turns bit4 into area
event mask `5` and overlap tracking, enables the zone, then binds its entity.
`7722C0 / 5F0C90` stores raw entity `98` and snapshots owner ID, parent ID,
locality, actor/human flags, player, `destroyed` tag, chassis kind and raw link
pointer. No entity/link retain occurs. Null binding leaves unrelated cached
fields stale; nonactor binding leaves actor-only fields unchanged.

`721080 / 59BE00` first disables and updates a linked enabled zone, then deletes
both shapes under bit9. It unregisters the `C8` object through `8B3400` unless
scene mode is3, deletes that object, and resets its ID. Under bit8 it destroys
tags, name and embedded area, then frees the record. The embedded body survives
this final allocation step. The deeper `C8` type/lifetime remains a lead.

Scale is also resolved further. `6FD580 / 576FB0` reads `+14` on the first model
root named `##scaleBone##`, or returns1. `70FFE0 / 58A560` can create that root,
reparenting existing roots beneath it; an empty model returns null even when
creation is requested. `6FD5C0 / 576FF0` clamps against `0.1` and a mutable upper
limit, installs a diagonal scale matrix, then emits entity events7 andB in scene1
unless entity flag `38` bit10 suppresses them. The upper-limit global
`147F3B0 / 12F6810` has zero file bytes; its runtime initializer is still open.
Those bytes must not be presented as the runtime maximum. GEM adds a bone-null
assertion.

## Registration and numeric IDs

`771580 / 5EFD50` creates a geometry-owning shared snapshot for the main manager.
It inserts that snapshot and stores the allocated ID in the original area. For
manager kind1, it creates a **second snapshot** and queues registration in the
worker's manager. Main and worker do not share one area instance.

The second registration argument is a **seed area ID for spatial indexing**.
It is not a requested allocated ID. `776D70 / 5F56A0` always obtains a new ID
from the registry; `776F90 / 5F58E0` uses the seed's intervals and neighbor pairs
to position the new entry. Negative seed IDs choose a clamped grid cell, or an
unseeded insertion when the manager's `58` mode disables that mechanism.

The registry has two banks of shared pointer pairs. Their bases are0 and`80000`;
area byte`3E` selects the second bank. `7731E0 / 5F1CC0` scans from a free-slot
cursor, reuses the first null slot, or appends. ID equals base plus slot index.
There is **no generation counter** in this allocator. `773480 / 5F1F40` clears a
slot, sets the retained old snapshot's ID to−1, and moves the scan cursor backward
so that slot can be reused. This says nothing about the separate entity registry.

Queued registration `772440 / 5F0E10` obtains worker storage through grid slot3,
then independently allocates there. GEM asserts its returned ID matches the
original captured ID; the game omits that comparison. Agreement depends on the
two managers receiving matching allocation/removal histories.

`7716C0 / 5EFEF0` queues worker removal before removing the main snapshot. It
then clears the original ID and overlap-vector length. `771760 / 5EFFE0` refreshes
owner metadata, copies into main storage, and queues an updated snapshot. The
copy/move distinction from [path-lines](path-lines-map-25492057.md) still applies:
queued move-apply preserves destination ID but clears its raw entity pointer.

## Spatial indexes and overlap events

`7752D0 / 5F3C70` creates X/Y endpoint nodes. Each node is`18` bytes: next/previous
pointers, coordinate, ID. The ID high bit distinguishes start from end.
`775430 / 5F3DF0` moves these endpoints and adds/removes candidate pairs when they
cross. `775A80 / 5F4470` requires strict overlap on the other XY axis. Endpoint
walks use strict coordinate comparisons; exact ties and insertion history need
a separate edge-case analysis before claiming a universal touching rule.

Candidate pairs are eight-byte rows in buckets indexed by
`32*(idB & 1F)+(idA & 1F)`. Insertion appends without deduplication. Pair removal
matches low31-bit IDs and sends leave notification for an active overlap.

`7762A0 / 5F4BE0` performs the intersection/event tick:

1. If either area's dirty byte`90` is set, require both enabled, at least one
   direction of category/filter matching, and strict overlap of Z intervals.
   Then run the native shape intersection. Its Boolean result becomes current
   overlap state.
2. If neither area changed, reuse previous overlap state. For active overlaps
   requesting stay events, call shape intersection for its shared result data;
   **ignore its Boolean return**. Assembly confirms there is no result test.
3. Produce enter/stay/leave event bits. A newly entered pair offers mask3
   (enter or stay), an existing pair mask2, and an exited pair mask4. Recipient
   selection prefers enter, then stay, then leave.
4. Update previous state and finally clear dirty bytes in both registry banks.

A direction is eligible when the recipient is local or the other area is
nonlocal, and `recipient.permit & other.proper` is nonzero. The second report's
side value reflects whether the first direction was eligible. It does not simply
mean that a first callback actually ran.

`7719B0 / 5F0300` maintains the area's overlap vector when tracking byte`3D` is
set. Enter removes any existing matching pointer and appends a`10`-byte row;
leave removes all matching pointers; stay does nothing. Rows contain a side
value and a **raw pointer to the other snapshot**, with no retain. This function
does not itself issue an entity or script callback. Its downstream consumers
are the next link to trace.

`777400 / 5F5D50` removes the pyramid row, frees X/Y endpoints, releases the
registry slot, removes candidate pairs, and sends leave notifications to linked
neighbors while a temporary shared handle keeps the removed snapshot alive.
The removed snapshot's ID is already−1, so its own recipient gate fails. Bulk
storage clear follows a different path without per-neighbor notifications.

## Pyramid bounds

Insertion/removal/relocation `60CC80 / 60CE50 / 60CF30` choose an XY leaf from
clamped center coordinates. Local areas skip insertion. Rows retain a shared
area and store category plus enabled-bit metadata. Relocation returns without
inserting if the old row cannot be found.

`60D360 / 47F660` rebuilds a leaf's aggregate from shape slot1 Box3 bounds. It
includes a shape only when the binary32 sum of XYZ spans is finite, then unions
bounds and categories. Disabled areas still contribute to this aggregate;
enabled filtering happens at the individual row/query layer. Empty leaves use
their cell center with Z0 and count0. Ancestors combine positive-count children.

The finite predicate `F6D30C / D5F264` takes a **double in XMM0**, despite its
integer-looking decompiler prototype. It rejects infinities and NaNs. The span
sum has already rounded as binary32 before conversion to double.

## Worker ordering and teardown

`8BE7A0 / 7440A0` is the worker **getter**. The actual execution method is
`8BE020 / 743910`, vtable slot1 of `Pather::eAsyncPatherManager`.

Its three queues live in critical resources at `A8`, `F8`, `148`. The pointers at
`C0`, `110`, `160` are temporary accessors populated while locked, not separate
permanent queue allocations. All three submission helpers require the async
flag, scene1, and pather-active flag; failing the gate does not invoke the work.

Each iteration detaches and executes all currently pending updates FIFO, then
executes at most one higher-priority job. It executes at most one lower-priority
job only if the higher queue was empty after that pop. Callbacks run outside
the queue locks. New updates wait for the next iteration. A higher backlog can
therefore delay lower work.

After callbacks, flush byte`198` deletes captures and pending nodes from updates,
higher, then lower queues without invoking them. It then clears that flag and
acknowledgement byte`199`. Already-detached or popped callbacks finish before the
flush. The optional Sleep10 uses earlier queue-emptiness observations, so newly
submitted work can be pending during that sleep.

`8B2E00 / 737E60` gates off new submissions, destroys the primary pather vector,
sets flush/ack requests, and polls until acknowledgement. Only then does it
destroy worker pathers, clear worker storage and release its grid handle. This
scene teardown leaves the worker object alive.

Worker destruction is a separate boundary. Its loop polls the global event
`1557FD0 / 13BF390` through `6978A0 / 50E8F0`, not its embedded task stop byte.
The destructor deletes queue resources before generic task cleanup waits for the
thread. Global event production and outer destruction order must be mapped
before claiming this whole sequence is safe. GEM also checks that task-start
arguments match one-time versus persistent task mode.

## Validation and next static leads

Both final exports have72 exact candidate entries; all exported functions
decompiled. The independent LLVM pass checked144 bodies and9,329 instruction
starts, normalizing LLVM's separate LOCK prefixes. Binary hashes, function
locations, RTTI slots, literal bytes and constants pass
[structural validation](../artifacts/build-25492057/area-lifecycle-validation.json).
These checks do not prove semantic interpretations or runtime reachability.

Next leads are storage initialization and seed geometry, overlap-list consumers,
entity revision writers, the `C8` pather shape, maximum-scale initialization,
global stop production, scene tick `A1B320`, and initial worker-grid copying.
Polygon/rectangle decision regions and the wider AI/entity frontier remain open.
