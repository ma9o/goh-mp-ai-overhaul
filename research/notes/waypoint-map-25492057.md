# Waypoints and native script execution — build 25492057

This pass follows the squad waypoint iterator into waypoint ownership, the
individual actor's generated orders, and the native `Helper::Thread` executor.
It is static evidence from the installed game and GEM binaries, not a runtime
ABI or a complete map of all native commands. The broader investigation remains
active in [the frontier](static-frontier-25492057.md).

The machine-readable contracts, exact binary identities, fields, property
registrations, vtables and instruction evidence are in
[`waypoint-map.json`](../artifacts/build-25492057/waypoint-map.json).
The structural check is
[`waypoint-validation.json`](../artifacts/build-25492057/waypoint-validation.json).
There are 106 paired function contracts and two additional GEM editing paths.
Structural checks passed for all 214 entries, 86 fields, 74 RTTI tables,
20 property registrations and 22 read-only float constants. LLVM and Ghidra
agree on 16,678 instruction starts after LOCK-prefix normalization; four raw
RNG initializer regions also have independent LLVM decoding.
Methods explicitly labeled “scope” cover the stated branches, not every branch
of a larger scene or aircraft function. Vtable census rows are discovery evidence;
they do not imply that every slot has received a behavioral review.

All offsets and RVAs below are hexadecimal; virtual slots and bit indices are
decimal. Game/editor pairs use `/`.

## Waypoint representation and registration

Constructor `996320/81F680` allocates/initializes a `128`-byte waypoint. Its
primary DDA interface is at `0`, reference/serialization interface at `8`,
reference count at `10`, reference flags at `14`, flags at `18`, XYZ at `20`,
and model reference at `30`. It inherits the pointed-helper construction policy;
ordinary heap construction begins with reference flag `1`, not deferred flag `2`.

| Offset | Meaning |
|---|---|
| `38..48` | Vector of outgoing `F0`-byte edge records |
| `50` | Owned `80`-byte native Thread block; statement hook at block `38` |
| `58` | Name/ID tag, `60` bytes |
| `B8` | Layer tag, `60` bytes |
| `118` | Reach radius, default `200` |
| `11C` | Authored offset, default zero |
| `120` | `random_position`, default false |

The name registry is `1484C90/12FBC90`; its `88`-byte nodes contain a name at
`20` and raw waypoint pointer at `80`. Normal construction from a reader reserves
a numeric name when it consists entirely of ASCII digits, diagnoses duplicate
IDs/names, then inserts the raw pointer. Copy/import construction with a remap
map generates a fresh name and records old-to-new names. Fresh names use the
numeric bank `1598EF8/1400A38`, initialized with argument `2000` by
`70A90/64610`. Allocation/reuse mechanics join the earlier entity-ID map.

The numeric predicate and conversion are distinct: the predicate accepts a
nonempty digits-only string; the CRT conversion is passed base zero and checks
progress/range errors, not complete consumption. Thus “numeric waypoint name”
does not prove strict decimal parsing of every leading-zero spelling.

Destructor `996770/81FB50` removes the registry row, releases its numeric ID,
destroys the action block, tags and edges, then releases the model/base reference
state. No iterator/executor cancellation occurs in this local path.

`9D4920/85E620` is the waypoint's primary position-change slot. It replaces Z with
the terrain-height result and refreshes the model transform. The manager's
`9D4CC0/85E9E0` refresh performs the same operations. This corrects the earlier
working description that called it only a model transform. Terrain height joins
the paired `704380/57E110` contract in the entity-contact map.

## Parser, edges and graph lifetime

Property registry `1598EE0/1400A20` reaches parser `997640/820A30` through the
secondary dispatcher `997560/820950`.

| Token | Effect |
|---|---|
| `E9` | Store radius float, then diagnose zero; no local negative check |
| `EA` | Store offset float |
| `EB` | Append outgoing edge and read destination name |
| `EC` | Read edge filter children |
| `ED` | Single tag filter |
| `EE` | Actor-ID filter list, remapping each ID |
| `EF` | Set exclusion sense |
| `F0` | Parse action block |
| `F1` | Store integer edge weight; no positive/range validation here |
| `F2` | Read random-position boolean |

An edge contains ActorID vector `0..10`, single tag `18`, exclude byte `78`,
destination tag `80`, weight `E0`, and owned visualization reference `E8`.
Defaults are weight one, include sense, empty filters and null reference.
Reference release supports immediate/deferred destruction according to the
reference flags. Save emits nondefault values; offset is written only when its
absolute value exceeds `0.5`.

The existing [squad map](squad-map-25492057.md) supplies the forward weighted
choice, immediate-backtracking rule, actor/tag filter precedence, recursive
reverse search and lookahead/RNG effects. The newly reviewed parser supplies no
guard that would remove the zero/negative-weight concerns in that map. Nor is
there a local cycle guard added by graph loading.

Manager `1484C80/12FBC80` owns the registry structure and layer metadata. It loads
waypoints by layer through `99B0B0/8246C0`; the scene helper-file dispatcher
`992D00/81BFF0` recognizes `waypoints`. Saving writes the empty layer and then
declared layers. The current loading layer is temporarily stored at manager
`350`; new waypoints copy it to `B8`.

`99A770/823D90` removes edges whose destination name has no registry entry,
releasing removed edge references and compacting the vector. The located
executable caller is GEM's selected-waypoint deletion path `A0D480`; game
references are unwind/metadata, not a demonstrated game-load call. This is not
evidence that game load validates graph destinations or cycles.

GEM paste/import `A0F450` constructs copied points with a remap map, remaps their
outgoing names, recenters the pasted selection and optionally adds a default
edge from the previously selected point. GEM `A0DBC0` and `A0E660`, initially
listed as possible graph-mutator leads, are drawing/path-preview work in the
reviewed paths. The preview has decompiler artifacts in its null-actor branch;
that branch is not promoted into a verified movement contract.

`11C` is demonstrably an authored and rendered height offset: `996930/81FD30`
draws a vertical guide to `position.Z + offset` when `abs(offset)>0.5`. That
does not establish its gameplay meaning. The iterator's random displacement uses
radius `118`, not offset `11C`.

## Individual actor waypoint orders

`904BF0/78B2F0` takes an extended waypoint iterator and actor. The brain adapter
`905AB0/78C1D0` passes `brain+1E8` and owner `brain+38`; assembly preserves the
returned order even where the decompiler prints a void adapter.

The existing iterator occupies `1A0` bytes. Its extension contains retry count
`1A0`, previous actor XY `1A4`, active byte `1AC`, and skip-reach-check byte
`1AD`. Assignment `904B20/78B220` assigns the start/filter and sets the skip byte;
it does not itself reset the other extended fields. Explicit reset
`904B80/78B280` clears both bytes and retry count, restores default previous XY
and assigns an empty iterator.

The order-producing path is:

1. Require chassis virtual slot 35 and a current waypoint.
2. For a new leg, mark it active, record actor XY, obtain iterator lookahead and
   build a one-point or multi-point Move order.
3. For an active leg, unless skipping reach checks, compare **2D** distance
   against the squared radius. Equality remains outside. This differs from the
   reviewed squad's 3D reach check.
4. If the actor has moved by more than the `1e-5` per-axis tolerance, try a Point
   path to the waypoint. Success returns a Move built from that route and updates
   previous XY. Failed pathfinding or insufficient movement enters retry handling.
5. Retries 1 through 5 nudge stored X by one and return a Wait. The native power
   call is fed `1.5` and retry number `n`; the resulting backoff is multiplied by
   `0.5 + U`. `U` is selected-context RNG high 24 bits normalized by `2^24`.
6. At failure 6, issue the cannot-move warning, then **execute the waypoint action
   anyway and advance**. Reaching the waypoint or skipping checks reaches the
   same action/advance path without the failure warning.

The RNG scale initializers had write references but no Ghidra function owner.
Their raw instructions were decoded separately: load `1`, divide by `16777216`,
store the multiplier, return. The manifest retains their exact bytes, input
floats and static initializer-pointer references in both binaries. This avoids
treating a zero-filled on-disk global as the runtime multiplier.

An action receives the waypoint, actor and its root hook; the executor converts
the actor to a soft link. The waypoint loop clears retry/active/skip state and
advances after spawning the action. A yielded script therefore does not by itself
block this actor's waypoint progression. The loop has no local arbitrary-step
budget, so authored immediate-action cycles deserve separate graph validation.

`eOrderAttackAir` also embeds this iterator at `A0`. Its state 7 invokes the same
consumer and substitutes a two-second Wait for a null result. This pass maps that
join and its reset; the other air-attack phases remain a separate static lead.

## Executor context, ownership and scheduling

`A03410/88D5D0` creates a `60`-byte executor and runs it immediately. If its current
hook becomes null it is deleted; otherwise it is appended to the manager.
Duplicate trigger detection warns but still appends. A matching waypoint exits
the search and appends without that trigger warning in the reviewed code.

| Executor offset | Context/lifetime |
|---|---|
| `0` | Per-executor DataStorage server |
| `18`, `20` | Raw trigger and waypoint pointers |
| `28` | Current/resume hook; null means complete |
| `30` | Player byte, default `FF` |
| `34` | Squad numeric ID, default `FFFFFFFF` |
| `38`, `40` | Actor soft-link pointer and next link |
| `48`, `50` | Selector ownership boolean and pointer |
| `58` | Initial/root hook |

Actor assignment uses `B8120/A8380`, the existing invalidating soft-link contract,
not a copied entity collection. Invalidation follows final entity destruction;
it should not be confused with the earlier retirement notification. Context
builder `A03710/88D8F0` relooks up the squad by ID for each command/condition and
reads the current soft actor. Waypoint, trigger and statement hooks remain raw.

The context has waypoint `0`, trigger `8`, actor `10`, squad pointer `18`, player
byte `20`, selector `28` and hook `30`. Spawn uses the supplied hook; rebuilt
command/condition context writes hook zero. Executor destruction releases only
an owned selector, unlinks the actor and destroys its private DataStorage.

The active executor is a shared global `1599568/14010A8`, not a TLS field.
Stepping saves/restores the previous pointer around dispatch, supporting nested
calls in that local path. Serialization sets it to the executor and then zero;
it does not preserve an arbitrary prior context in the reviewed save/load paths.

Manager `1485CB8/12FCCB8` holds the vector at `10..20`. On Quant `FD2`, outside
scene mode `2`, it steps the initial row count, then deletes complete rows and
compacts. Newly spawned rows have already received their immediate step; rows
appended during the manager pass are not included in its initial-count loop.
Load appends restored executors without that immediate step. Save includes only
rows with a nonnull current hook.

Serialization stores root names, current statement path, actor ID, squad ID,
player, selector and per-statement state. Loading remaps the actor ID but retains
the squad ID directly. Root/path lookup results are dereferenced without local
missing-object recovery in the reviewed branches; matching graph structure is a
precondition, not a proven resilient reload protocol.

## Native statement semantics

Six builtin name/factory descriptors occur in both binaries: `if`, `while`,
`switch`, `loop`, `delay`, `select`. Lookup is case-insensitive. Unknown builtin
names fall through to native command creation; an unresolved name is diagnosed.
This is the builtin control inventory, not the inventory of native commands.

A block's child vector is at `20..30`; its inline hook begins at `38`. Hook
parent, name, enabled flag and cached path are at relative `8`, `10`, `18`, `20`.
Execution is hook slot 13, parent-next is slot 4. Actual context-flag setter
`9C7CE0/851500` writes hook `40` / block `78`; it does not recursively propagate
flags. Primary block slot 5 is a different property-delegation adapter.

`A03810/88D9F0` clears the resume hook, dispatches the current enabled statement,
then either climbs parent-next callbacks after completion or returns on yield.
A yielding child may select a deeper resume hook; otherwise the old hook is
restored. Disabled statements complete. There is no local instruction/time budget.

| Statement | Native behavior |
|---|---|
| Block | Run enabled children in order until one yields; retain its resume hook |
| If/Case | Reset condition, clear its cache, build context; true runs block, false/absent completes |
| While | Uses the same conditional execute method; its next-child callback returns itself after the last resumed child |
| Switch | Scan enabled children named exactly `case`, run first true case; otherwise enabled default. Parent-next always null |
| Loop | Per-executor integer starts from configured count. Positive: decrement, run block, return yield. Exhausted: complete |
| Delay | Per-executor deadline; initialize from mean plus signed uniform spread, convert/add clock, wait until due, then clear |
| Select | Install its inline selector into the executor as borrowed |
| Wrapped command | Build context, call native command slot 7 and complete |

The `while` result is narrower than the name suggests. If its condition is true
and its entire body completes immediately without installing a continuation,
the common block/conditional method returns completion and the executor advances
past the while. Repetition occurs when a yielded child resumes and reaches the
while's parent-next callback. This follows the paired control flow; no runtime
experiment was used. Likewise, `loop` should not be summarized as “one iteration
per frame” without accounting for resumed children and immediate spawn stepping.

Condition wrapper `9ED290/876F40` caches result byte `A` with valid byte `B`.
Mode byte `C`: zero calls condition virtual slot 9 with context; one forces false;
two forces true; other values preserve the prior result. The concrete predicates
behind that virtual call are the next layer, not closed by mapping this wrapper.

Loop counters and delay deadlines are private to each executor and keyed by
statement addresses. No explicit loop-counter reset on later reentry occurs in
the reviewed accessor/execution paths. Delay uses zero as uninitialized; an
initialized deadline of zero yields and can be initialized again next time.
`abs(spread)<1e-5` avoids the RNG draw. Negative/exceptional inputs are not locally
clamped by this execute routine.

Block destruction first detaches/zeros the child vector (`689D60/5005E0`), then
deletes old children through primary slot 8 and frees the detached storage.
`9CBE00/855750` alone only frees vector memory; it is not the child-deletion
routine. Statement destruction frees cached path storage. Neither local path
invalidates raw hooks retained by a live executor.

The modifier follow-up corrects the selector constructor interpretation: `10` is
the owned source pointer; the qword zero at `18` initializes two integers,
configuration `18` and current source `1C`. `8` is an allowed-source mask, `20`
the default source and `24` the ignore-captured-by-user flag. The initial
constructor-only map had incorrectly labeled `18` as another pointer.

## Normal scene cleanup and remaining leads

Event publication `6532B0/4C8730` dispatches synchronously through
`6540E0/4C95D0`. A supplied event receiver is called directly; otherwise the router
finds the event-ID vector, snapshots its initial count, skips null rows and calls
receiver slot 1. Null compaction occurs only when observed at outer dispatch
depth one. This is not the separate queued-event path and does not establish a
thread-safety guarantee for arbitrary caller phases.

Normal scene teardown `A19FF0/8A1CB0` publishes `SceneDone FB6` before later
`SceneDestroy FB3`. The thread manager clears executors on the former; waypoint
manager releases registered waypoints on the latter. Synchronous dispatch closes
that ordering link. Arbitrary live graph deletion, callback reentrancy and outer
worker quiescence still need their own evidence.

Next static work is concrete native command/condition/selector dispatch,
modifier preparation and entity filters, downstream brain movement flags and
their producers, live graph/reload invalidation, and the event router's queued
delivery/subscription lifecycle. These are productive static leads, not runtime
boundaries.
