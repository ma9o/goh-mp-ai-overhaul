# Line costs, portal smoothing and spatial owner snapshots — build 25492057

This pass connects the pather's line/point tests to copied entity metadata and
spatial collection, and follows the route optimizer's actual acceptance tests.
All evidence is static. Addresses and offsets below are hexadecimal RVAs; virtual
slots are decimal. Game/editor pairs are in
[the manifest](../artifacts/build-25492057/path-lines-map.json).

The reviewed set is 45 contracts, including 43 game/editor pairs and two game-only
player-policy helpers. A paired entry means reviewed correspondence, not identical
behavior: the player boundary helper is deliberately different in GEM.

## Line and point contracts

| Operation | Game / GEM | Contract |
|---|---|---|
| Construct line query | `8AC950 / 731760` | Copy context, store endpoints, clamp end |
| Evaluate line | `8ACA00 / 731820` | Boundary rejection, objects first, terrain second |
| Line blocked | `8AC3F0 / 731200` | Return `cost >= 100000` |
| Point blocked | `8AC2B0 / 7310D0` | Optional network bound test, then zero-length line |
| Terrain component | `8ACA80 / 7318A0` | RNG-jittered samples weighted by segment length |
| Object component | `8ACD40 / 731B40` | Circle/capsule query; reverse-order object reduction |

The line object contains a copied context at `+8`, start XY at `+F8`, end XY at
`+100`, and a clamp-result byte at `+0`. Context copying retains the shared grid
as established in the earlier pass. If the original endpoints were equal and
clamping changes the end, construction restores end=start but preserves the
clamp-result byte. Evaluation can consequently reject this point using boundary
policy even though its stored endpoints are equal.

Evaluation first applies `8CA160` when the clamp byte is nonzero. Rejection
returns `100000` without writing the optional result. Otherwise it evaluates
objects; only an object cost below `100000` permits terrain evaluation. The
sum of those two components is **not clamped**.

The optional result has semantic fields kind at `+0` and raw area pointer at `+8`.
Neither construction nor evaluation initializes that caller-owned result:

- Object accumulation reaching the threshold writes kind **1**, current area.
- A Human profile encountering a Vaultable obstacle can change initial kind **0**
  to **2**, current area; a later block overwrites it.
- Terrain accumulation reaching the threshold writes kind **3**, null area.
- Combined object+terrain cost can reach the threshold while each component
  remains below it, leaving kind **0** or **2**.

Thus kind is not a complete representation of the returned float's threshold.
Padding bytes `+4..7` are not a defined semantic field. The area pointer is not
retained for the result.

`8AC3F0` returns true for **blocked**, resolving the predicate used by animation
clearance in the [profile/vault pass](path-profiles-map-25492057.md).

### Terrain sampling and RNG

Let `L` be XY line length and `N = trunc(L / 16) + 1`, using the native constant
16 in both binaries. For each `i = 1..N`:

1. Start with `t = i/N`.
2. When `i < N`, draw one value `U` from the selected RNG and add
   `(2*U - 1) * 0.5/N` to `t`.
3. Sample `P = start + t*(end-start)`.
4. Add `terrainCost(P) * distance(previousSample, P)`.

The last sample is exactly the endpoint and consumes no draw. An uninterrupted
query makes `N-1` draws. There are two selected-RNG getter calls around a draw,
but only one LCG state update. Blocking in the object stage skips these draws;
early terrain termination reduces their number.

When terrain accumulation reaches `100000`, this helper returns exactly
`100000`. This is visible in assembly despite the game's erroneous `void`
decompilation: the early branch preserves the threshold register; normal exit
replaces it with the accumulator before returning in XMM0.

A zero-length line uses `N=1`, consumes no terrain RNG and contributes zero
terrain cost, even if the terrain lookup returns `100000`. The point helper's
other gates therefore matter; zero-length integration alone does not reject a
blocked terrain cell. These descriptions assume ordinary finite coordinates;
the GEM point helper contains additional self-comparison/NaN branches.

### Boundary and network policy

The clamped-target reject condition is:

```
context[0] && !context[1]
  && (context[2] || playerPolicy(&context[0x68]))
  && sceneBoundsMode()
```

Scene mode requires the scene singleton returned by `A6CF30` to exist and have
byte `+908` nonzero. The game's `A46770` obtains singleton `15A83C0` through
`D0F070`, then calls `D0C360`: scan the raw player-pointer vector at `+160/+168`,
match the player ID at row `+4`, return its byte `+1E1`. Missing singleton or ID
returns false. The name and producer of that flag remain open.

**GEM's corresponding `8CF350` always returns false.** GEM cannot verify that
game-specific part of the boundary policy by equivalent execution.

Point rejection additionally invokes `8B5080 / 73A1C0` when context `E8 & 8`
and the profile's Network flag are set. It converts XY into a network cell by
truncation using origin `+18/+1C` and global cell size `14822C8`. Outside the
`+F0/+F4` grid returns false. A cell's bound-ID vector identifies `50`-byte bounds
in network `+A0`. A bound blocks only if:

- Its owner is `-1` or differs from context owner `+7C`.
- The directed halfspace containment helper accepts the point.
- It is not skipped by context SkipDamage (`E8 & 2`) plus bound `14 & 1`.
- Its float cost at `+10` is at least `100000`.

## Portal adjustment and pruning

Portal rows are `20` bytes: node pointer `+0`, origin XY `+8`, delta XY `+10`,
crossing fraction `+18`, remaining cost `+1C`. `8C69E0 / 74C9B0` solves the
neighboring-point chord intersection and clamps the fraction to `[.05,.95]`.
Near-parallel denominator starts from fraction zero, hence clamps to `.05`.
The C output's apparently uninitialized clamp maximum is `.95` in assembly.

`8C6B00 / 74CAD0`, named by the diagnostic `BuildMoveCost()`, sets the selected
last row's cost to zero and walks backward:

```
P(row) = origin + fraction * delta
previous.cost = distance(P(previous), P(current)) * current.node[0x34]
                + current.cost
```

`8C6C00 / 74CBD0` checks intermediate portals against a start/end chord. For
nonparallel intersections both chord and portal fractions must be in `[0,1]`.
A near-parallel portal is skipped without a collinearity test. This is a portal
test; it does not query objects or terrain.

The smoothing slice of `8C4F70 / 74ADF0` distinguishes these policies:

- Task byte `+8` and `dbg_pathOptimize` gate adjustment/pruning.
- `g_pathAlignPortals` (`1482508 / 12F9968`, initialized to 1) adjusts interior
  portals forward, then backward.
- Modern mode can reverse the rows using the front-to-back option, rebuild
  costs, and try chords with midpoint span reduction. It accepts a chord when
  its line cost is at most the existing remaining-cost difference. There is no
  separate `lineCost < 100000` condition at that decision. Accepted intermediate
  rows are removed, costs rebuilt, and passes repeat until row count stops
  changing. The measured chord cost is not stored: rebuilding uses node weights.
- Old mode extends an end index from start+2 while the portal-chord predicate
  succeeds, then removes intermediates of the last accepted chord. This branch
  does not make a line-cost query or visibly rebuild costs after its pruning.

These are reviewed algorithm slices, not a proof of route optimality. Assembly
confirms the old pruning loop uses integer indices despite a misleading C cast.

## Entity data copied into eArea

The path query builds an unlinked `B8`-byte area with category `80`, filter `40`,
ID `-1`, enabled byte 1, no owner, and circle/capsule geometry. Radius is profile
`+20`. Candidate reduction uses the existing `8AE060` policy.

`7714B0 / 5EFC80` refreshes the owner-information record:

| Area offset | Producer/read |
|---|---|
| `10` | Owner pointer nonnull; null leaves the other cached values stale |
| `11` | Entity `3A` bit0, owner locality |
| `14` | Entity `C0` ID |
| `18` | Parent entity `C0` through `560->8`, or -1 |
| `1C / 20` | Entity `30` bits1/2, actor/human |
| `1D` | Actor's exact `destroyed` tag through `984430 / 80CEA0` |
| `1E` | Actor chassis `DE0->64 == 2` |
| `1F` | Entity `1C0 & 18000` nonzero; property binding still open |
| `21` | Actor `DA8` player byte |
| `28` | Raw entity `588` link pointer |

The `destroyed` tag is bound by initializers `48830 / 39960`; this predicate
does not directly read health. Actor-only fields refresh only when actor bit1
is set. Other key area fields are category `30`, filter `34`, enabled `3C`, local
`3E`, obstacle type `3F`, owned-shape flag `41`, ID `44`, cached axis intervals
`48/60/78`, and raw entity pointer `98`.

This is **not a pointer-free entity snapshot**. Earlier object policy dereferences
the copied link at area `+28` to read its `+78` category. No retention of that link
or the raw entity pointer is established by these operations.

### Enable, copy and queued apply

`771880 / 5F0150` does nothing when enabled already equals the requested value.
Otherwise it stores `3C`; an unlinked area returns immediately. For linked areas:

1. If manager `168` is nonzero, refresh owner info and copy into spatial storage
   through `777690 / 5F5FD0`, then update bounds.
2. If manager `178 == 1` as well, make a shared area snapshot and capture it in a
   callback submitted through `8BDD70 / 743670`.
3. Refresh the spatial leaf flags through `777930 / 5F6270`.

GEM adds linked/locality assertions. Copy assignment `772010 / 5F09E0` destroys
previous owned geometry, clones source geometry through virtual slot2, and copies
owner info, ID, masks, status, raw entity pointer. Shared snapshot construction
`772AA0 / 5F14C0` allocates `C8` bytes: a shared control prefix and area at `+10`.

Move assignment `772110 / 5F0AE0` behaves differently: it transfers geometry,
clears the source geometry/ownership, **clears destination entity `+98`**, copies
owner metadata and masks, and **preserves destination ID `+44`**. The queued apply
`777720 / 5F6060` looks up the destination by the incoming snapshot ID, performs
this move, refreshes bounds, and releases both shared handles.

The callback queue accepts work only when global `14822B0`, scene `159AE78 == 1`
and global `15850D0` permit it. It locks worker `+A8`, appends a `50`-byte list node
with a moved function object to the queue at `+C0`, and unlocks. There is no local
fallback invocation when the gate fails. The concrete captured callback's RTTI
and invoke slot2 are recorded in the manifest. Invocation obtains the worker
grid, calls its manager slot3, and applies the captured snapshot. Queue drain,
replacement and cancellation ordering remain leads.

The entity dispatcher `71F1D0 / 59A050` selects entries in `A00/A08` when pointed
entry `38 & mask3` or `3C & mask4`, then enables `entry+8`. Cover construction
`8A27B0 / 7274F0` saves and restores prior enabled state around its temporary
disable. The earlier animation clearance routine instead re-enables true.

## Spatial collection and lifetime boundaries

Area lookup uses two ranges of `10`-byte shared pointer pairs: first base ID at
registry `+0`, vector `+8`, second base/threshold at `+28`, vector `+30`. Lookup
`772FB0 / 5F1A60` retains a shared control block without local range checks.
The separate validator `773030 / 5F1AF0` checks relative index, bounds, nonnull
pointer and matching area `+44`. Allocation and ID reuse are still open.

Bounds update `7777F0 / 5F6130` asks geometry slot1 for AABB3, updates X/Y interval
indices through `775430`, copies Z bounds, and relocates the tree entry using old
and new centers. `7710E0 / 5EF880` computes **center XYZ** from the three interval
midpoints; it is not a rectangle converter.

Leaf flag refresh `60D280 / 47F580` locates the area by ID in the center's leaf
and writes `(area.category & 7FFFFFFF) | (area.enabled << 31)`. Its cell conversion
uses the tree origin, world extents, and `1 << maxLevel`, clamping coordinates to
the grid. A missing leaf entry is not inserted by this routine.

Collection `7779D0 / 5F6310` clears output and chooses:

- **Unlinked query:** `60D160 / 47F460` reserves at least 64 output slots, obtains
  query AABB3 and traverses the spatial tree with the query filter.
- **Linked query:** get related candidate IDs through `7749E0`, resolve each
  shared area, release the temporary handle, then test the raw area category,
  enabled byte and shape intersection. Output stores raw pointers without retain.

Tree traversal `60D7C0 / 47FAB0` checks node activity, aggregate category mask,
AABB broadphase, then four children per interior node. Leaf entries require
category/filter intersection and the enabled sign bit. A query-shape slot5
acceptance of the node AABB skips individual intersection tests; otherwise the
shape kinds from slot7 select `60EED0 / 4813D0`. Slot5's precise geometric
contract remains to be mapped. Collection itself consumes no RNG.

The outer intersection dispatcher routes 2D pairs to `616100 / 4888F0` and 3D
pairs to `612720 / 484E60`. Mixed cases project supported 3D kinds to circle,
capsule or polygon. Exact leaf geometry is the next dependency.

### Correction to the earlier acceptance map

The argument at line/integrator `+80` aliases embedded **context `+78`**, sourced
from actor `+594`; it is not context `+80` or the collection filter. Wrapper
`771860 / 5F0130` forwards it in R9D, but both target collectors ignore incoming
R9. The actual query filter is area `+34`, initialized to `40`; category `+30` is
initialized to `80`. The earlier manifest has been corrected and revalidated,
and its dependent profile manifest hash refreshed.

## Evidence and remaining work

The readonly focused exports contain 71 game and 68 editor functions, all exact
candidate entries with completed decompilation. The 88 reviewed function bodies
contain 6,613 Ghidra instruction starts, compared independently with LLVM after
normalizing standalone LOCK prefixes. The verifier checks fingerprints, exact
entries, evidence locations, constants, literals and callback RTTI; it does not
prove these interpretations or full behavior.

Continue grid search/goal callbacks, packed terrain producers, spatial insertion
and removal, interval updates, worker queue drain, shape leaves, player `1E1`
and entity flag producers, and the lifetime of raw owner/link pointers. This
pass does not close the broader static frontier.
