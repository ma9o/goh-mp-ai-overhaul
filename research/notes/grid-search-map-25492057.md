# Grid search, goal callbacks and route refinement — build 25492057

This pass maps the grid solver selected when a pather profile lacks Network,
the seven non-network goal types, and the route they produce. All evidence is
static. Addresses/offsets below are hexadecimal RVAs; virtual slots are decimal.
The [manifest](../artifacts/build-25492057/grid-search-map.json) records 49 paired
contracts, field reads/writes, 14 RTTI tables and remaining dependencies.

## Search preparation and state

Solver construction `8AF8C0 / 7347A0` creates a reusable area query at `+8`,
capsule at `+C0`, bounds at `+E8`, and copied context at `+210`. Node storage at
`+120` initially is null. The eight direction rows at `+128`, stride `1C`, are:

```
(0,1), (1,1), (1,0), (1,-1), (0,-1), (-1,-1), (-1,0), (-1,1)
```

Preparation `8B0F50 / 735E40` assigns the goal's context, resolves the spatial
manager through shared-grid slot3, and fixes grid spacing at **16 native units**.
It clamps both corners of the requested bounds, then shifts the lower corner
so the requested start sits at a cell center. Per axis, with clamped lower `L`,
start `S` and cell size 16:

```
origin = L - (trunc((S-L)/16)*16 + L + 8 - S)
firstCenter = origin + 8
dimension = trunc((upper-origin)/16)
```

The upper corner is not shifted. If the start's computed cell index equals a
dimension, that dimension is incremented. It allocates and zeroes eight bytes
per node, and copies the goal target into solver `+208`. Preparation does not
free an earlier node allocation; the reviewed task wrappers construct a fresh
solver, run it once and destroy it.

| Solver offset | Meaning |
|---|---|
| `F0 / F8` | Shifted origin XY / clamped upper XY |
| `100` | First cell center XY |
| `108 / 10C` | Integer width / height |
| `110 / 114 / 118` | Cell size / half-cell / inverse cell size |
| `11C / 120` | Node count / owned node allocation |
| `128` | Eight integer delta, world delta, unit vector, length rows |
| `208 / 210` | Heuristic target XY / copied context |

The node's `+0` is accumulated float cost `g`; `+4` packs:

- Bits **0..26**: heap index; popped entries receive literal `07FFFFD1`.
- Bits **27..29**: incoming direction, used to find the parent.
- Bit **30**: currently queued.
- Bit **31**: has been popped. Reopening a node does not clear this bit.

The heap has `10`-byte rows containing priority at `+0` and node pointer at `+8`.
It starts with capacity 512 and doubles. Sift helpers `8B1200 / 736140` and
`8B1320 / 736260` update node index bits during swaps. For ordinary finite keys,
equal values do not swap; equal child keys favor the left child in downward
selection. This is not a globally stable ordering guarantee.

## Expansion, edge costs and termination

`8AFA00 / 7348F0` calls goal slot1 to initialize the goal against the solver,
then derives all eight world deltas, normalized directions and lengths. The
normalizer `793E00 / 6140D0` takes destination RCX, source RDX, scale XMM2 and
returns length in XMM0. Calls here use scale 1.

An out-of-range start goes directly to goal slot2 without expansion. Otherwise
the search caches candidate obstacles, queues the zero-cost start node with
Euclidean start-to-target priority, and repeats:

1. Pop the lowest-priority node, clear its queued bit and mark it popped.
2. Call goal slot3 with point in RDX, **cost in XMM2**, and **node index in R9D**.
   The decompiler omits those last two arguments in the solver's C output.
3. If the callback returns true, stop. Otherwise apply the optional enclosure
   stop rule, then evaluate the eight neighboring cells in fixed order.
4. A new or strictly cheaper node receives `g = current.g + edgeCost`, incoming
   direction, and heap key `g + EuclideanDistance(neighbor,target)`.

The enclosure rule is enabled by goal byte `+12C`. Relative to the target cell,
popped nodes on its column set bits 4/8 for `y<=targetY` / `y>targetY`; nodes on
its row set bits 1/2 for `x<=targetX` / `x>targetX`. All four bits stop the search.
An empty heap also stops. Goal slot2 runs after every exit, including invalid
start. A callback returning true is **not** equivalent to a successful route.

### Terrain and object contributions

For an in-bounds neighbor, the search obtains the heightmap through shared-grid
slot2. Endpoint terrain-cell coordinates use integer truncation of world XY
plus map offsets, followed by signed integer division by the terrain cell size.
It takes:

```
N = max(abs(endCellX-startCellX), abs(endCellY-startCellY)) + 1
```

Samples run from `current + delta/N` through the neighbor at equal intervals.
The helper retains the **maximum** sampled terrain multiplier. Any sample at
least `100000` rejects the edge; otherwise terrain contribution is this maximum
times direction length. This differs from the length-weighted line integration
mapped in the [previous pass](path-lines-map-25492057.md).

Although the decompilation contains a radius-offset loop, assembly advances its
offset pointer once, immediately reaching the end: **only the zero lateral
offset is sampled here**. Radius still matters to the capsule used for objects.
There is no separate orthogonal-neighbor check before a diagonal edge's own
terrain/capsule evaluation.

For terrain-accepted edges the reusable capsule receives current/neighbor XY,
unit direction, length and profile radius. The temporary obstacle cache returns
intersections, reduced by `8AE3A0` with a null result record. Object cost is added
to terrain contribution. Only edges below `100000` are relaxed. The new
accumulated `g` can exceed that threshold; the goal callback handles that later.

There are no direct RNG draws in core expansion or terrain sampling. Route
construction and shortcut queries do call the RNG-consuming line helper.
The geometry callees remain separate review boundaries.

## Goal contracts

All non-network goals share copied context `+10`, start `+100`, target `+108`,
requested bounds `+118..124`, and optional enclosure flag `+12C`. Generic
constructor field `+128` is initialized to `100000`; the reviewed solver and
callbacks do not read that field. Generic slots1..3 are pure-call entries.

SinglePoint constructor `8B1910 / 736850` resets the supplied output and expands
the endpoint min/max bounds by half their distance plus `14776C0 * 25` (the
image scale 20 makes the latter **500 native units**). It enables enclosure.
SingleArea obtains its target from the area's bounding-circle slot10 and stores
a raw area pointer at `+140`. The common begin callback stores the solver,
clamps start/target indexes to grid dimensions, and clears output success.

| Goal | Visit/finish behavior |
|---|---|
| SinglePoint | Below cost limit, only target-cell index matches; reconstruct toward exact target. Cost at least 100000 stops without constructing. |
| SingleArea | Below limit, target-cell match **or** area slot11 point containment accepts; reconstruct at popped point. |
| AvoidArea | Below limit, accept the first popped point outside area slot11. Start and target are context `+6C`; extra radius expands bounds; enclosure is disabled. |
| TouchArea | Below limit, target-cell match **or** a profile-radius circle intersecting the area accepts. If current node cost is nonzero, back off one recorded parent and reconstruct to that parent's center. |
| NearestPoint | Track strictly decreasing Euclidean target distance for popped nodes, then apply the SinglePoint visit rule. Finish may construct a fallback. |
| ManyPoints | Update matching pending destination records; optionally construct their paths. Enclosure is disabled. |
| Generic | Abstract callback interface and shared context cleanup. |

Area/output/vector pointers are raw. Deleting destructors release the copied
context and optionally the goal allocation; they do not retain or delete the
supplied area or output objects.

### NearestPoint fallback

The best record starts with distance `1e38` and index `-1`. It is updated **before**
the visit's cost-limit check, so even the node that stops on excessive `g` can
replace the best record. When output is still unsuccessful and a best node exists,
`8B20D0 / 737040` does the following:

- If byte `+150` is true, construct the best node directly.
- Otherwise let `a=distance(start,best)`, `b=distance(best,target)`,
  `c=distance(start,target)` and `r=profile radius`.
- If `b+4r <= c` or `10r <= b`, return no path when `a <= b`; otherwise construct
  the best node.
- In the remaining case, construct the start node and position.

Finish has no independent check that the chosen best node's cost is below the
threshold. The Point task's grid branch (`8C8B00 / 74E980`) writes
`goal150 = !task18`. This is a specific fallback policy, not proof of a globally
nearest reachable point. Early goal/enclosure stops constrain which nodes were
considered.

### ManyPoints records and reachability

Its `20`-byte records contain XYZ at `+0`, cost `+C`, optional output pointer
`+10`, and integer grid index `+18`. Begin writes terrain Z and clamps each
index. It resets nonnull outputs but **does not initialize record cost**. Only
records whose cost equals exactly `100000` are treated as pending.

On a matching popped index, the callback stores `g`; if output is nonnull it
also reconstructs and decrements the supplied remaining-path count at goal
`+138`, stopping immediately when that count drops below 1. Null outputs receive
cost without decrement. Multiple records can share a node index. Mismatching
pending records keep the search running; an empty/completed list stops it.

The constructor-reference census found no direct code references to this
constructor in either binary, only data metadata. That is not proof of absent
indirect reachability. Its callers and pending-cost initialization remain leads.

## Route output and refinement

`8B13D0 / 736310` assigns the goal context to output `+20`, stores selected node
cost at `+18`, and sets success `+110` **before** testing the exact endpoint.
If the selected center is within squared distance `16*16*2` of the requested
endpoint, a line query can add that exact endpoint when cost is below `100000`.
It receives the selected node's existing `g`; connector cost is not added.
Failure or excessive distance simply skips that endpoint, leaving success set.

Backtracking appends center XY and node `g` while `g != 0`, using direction bits
to subtract `dy*width+dx`. It then appends original start with zero cost. Fewer
than two rows causes a duplicate start row. Rows are `C` bytes and stored
destination-to-start, with descending accumulated costs. The zero-cost
termination assumes the intended positive-cost route behavior; there is no
separate parent sentinel test in this loop.

Postprocessing runs `dbg_pathSmooth` times (`1482290 / 12F96F0`, initialized **0**),
then optional `dbg_pathOptimize` (initialized **1** in the profile pass).

The shared shortcut predicate `8C7A80 / 74DA20` evaluates a line from lower-cost
row A to higher-cost row B and accepts strictly when:

```
lineCost < profileFactor * (B.cost - A.cost)
```

`8C9AD0 / 74FA40` selects by profile **Human**, not the LinearPath flag:

| Native setting | Selection | Initial binary32 value |
|---|---|---|
| `g_linearPathThresholdHuman` | Human flag `F0 & 4` | approximately 1.05 |
| `g_linearPathThresholdVehicle` | Every other profile | approximately 1.30 |

These are mutable settings. The predicate has no separate `lineCost < 100000`
gate. Its line calls consume RNG as already mapped.

Smoothing `8C7180 / 74D150` visits consecutive triples in place. If the endpoint
shortcut passes, it moves the middle XY to their midpoint. It leaves the row's
cost unchanged; later triples see the earlier coordinate changes.

Pruning `8C7820 / 74D7F0` walks from start toward destination, extending a chord
past the next candidate point. Passing drops that intermediate point; failure
keeps it and makes it the new anchor. It preserves both endpoints, then reverses
its temporary output back to destination-to-start order. It copies old cost
values. Neither routine recalculates output total cost.

## Temporary obstacle cache

Core search collects candidate raw areas once into an `eSpatialGrid` with **64**
native-unit cells, distinct from the solver's spacing of 16. The cell cache owns
its pointer containers; it does not own the referenced areas.

Constructor `8CC620 / 7527A0` creates `B0`-byte cells with inline capacity for
16 area pointers and vector state at `+90`. Insertion `8CC890 / 752970` asks the
area shape for AABB3, converts min/max XY into clamped cache coordinates, and
adds the area to every cell in that inclusive range. Its C decompilation loses
the virtual output and falsely suggests identical min/max; assembly reads
distinct stack locations for them.

Collection `8CCA00 / 752AE0` scans the query AABB's cache cells, filters enabled
state and category against query `+34`, deduplicates raw pointers in first-seen
order, then applies native shape intersection. The [shape follow-up](spatial-shapes-map-25492057.md)
shows that this is not universally exact geometric overlap. It appends to the supplied vector
without clearing it; core search supplies a fresh empty vector per edge.
Collection does not retain returned areas.

Reusable area rebind `771310 / 5EFAA0` writes manager, geometry, category/filter,
enabled/status, unlinked ID and null entity pointer. It is not construction:
owner info, obstacle type, owned-shape flag and other fields remain unchanged.
The reviewed solver starts with a freshly constructed query.

## Validation and remaining work

Readonly exports contain 50 functions from each binary. All explicit candidates
are exact entries and every function decompiled. The 98 reviewed bodies have
7,862 Ghidra instruction starts, independently compared with LLVM after LOCK
normalization. Structural validation checks fingerprints, entries, constants,
literal bytes and RTTI; semantic conclusions remain reviewed interpretations.

Continue shape/area slot semantics and intersection leaves, graph construction,
terrain/slope producers, shared worker/area lifetime, remaining task fallback
branches, output status `+111`, many-goal callers and all other entries in the
[active frontier](static-frontier-25492057.md). Grid coverage does not exhaust
the wider static AI/entity mapping objective.
