# Detour bounds, reachable nodes and movement-cost inputs

Build25492057; static evidence only. Addresses and offsets are hexadecimal,
virtual slots decimal. This pass continues the movement and human-geometry maps.
The game/editor executables and readonly Ghidra database remain unchanged.

Follow-up: the [path profiles and vaulting pass](path-profiles-map-25492057.md)
binds the profile/obstacle flag names and terrain IDs, establishes the15-entry
Height authoring count, and maps edge8B9670, bound8BD570 and Move8F5E70.
Open questions below are this pass's historical scope.

Evidence: [manifest](../artifacts/build-25492057/path-acceptance-map.json),
[validation](../artifacts/build-25492057/path-acceptance-validation.json), both
`path-acceptance-pass.tsv`/`path-acceptance-anchors.tsv` pairs and frozen local
exports with suffix `-path-acceptance`.49 contracts include46 method pairs,
57 fields and16 RTTI tables.49/52 candidates are exact entries;62/56 exported
functions decompile completely. LLVM agrees with11,265 instruction starts in95
method bodies after LOCK normalization. Four25-byte RNG initializer leaves are
recorded separately as raw assembly, without inventing Ghidra functions.
Structural checks do not validate semantics, ABI, runtime effects or completeness.

## Detour geometry and random sampling

Bound combiner6144E0 /486C90 takes circles A/B with XY at8/C and radius10.
For ordinary finite nonnegative radii and center distance d:

1. If `d > rA+rB`, return false without writing the output circle.
2. If one contains the other, copy the smaller circle.
3. For other overlap, when both radii≤d, compute
   `x=(rB²-rA²+d²)/(2d)`, center `B+(A-B)*(x/d)`, radius `sqrt(rB²-x²)`.
4. Otherwise copy the smaller circle.

This constructs a **sampling bound**, not the exact lens-shaped intersection.
GEM adds `y2 >= 0` assertion before the square root. There is no general local
validation of caller radii.

Detour93ED30 /7C63C0 centers D8 on the target's XY and sets radiusE8 from weapon
range helper80A9C0. It centers F0 on actor74 and sets radius100 from its caller's
movement limit. It combines F0/D8, then optional C0 when D0 is nonzero. The same
operation appears in bound refresh940DC0 /7C8430. Individual cover and point
candidates still test every original active bound. Those tests are essential:
the sampling circle alone does not establish membership in the original regions.

Circle vslot11 B91D0 /A9450 tests squared XY distance≤radius², including boundary.
Sampler vslot14 6143E0 /486B90 consumes **two** selected-RNG draws:

```text
state = state * 0x7FF8A3ED + 0x2AA01D31  (mod 2^32); counter++
U = (state >> 8) / 16777216
r = (1-U1²) * radius
angle = 2*pi*U2
point = center + r*(cos(angle), sin(angle))
```

Four getter calls appear but only two state updates occur. The radial formula
is not uniform disk sampling. Raw initializers3B1D0/61DC0 (GEM2B600/54AA0)
establish the reciprocal16777216 used by the circle/cost samplers. These streams
use the selected getter554B50, unlike sound asset selection's default getter.

## How the detour establishes reachability

`Pather::Network::Goal::eEvaluateNetwork` constructor8C6390 /74C1F0 copies actor
context, startXY and the cost limit at10C. The detour supplies
`distance(actorXY,sampling_circle_center)+sampling_circle_radius` as that limit.
It stores1 at goal108, but the reviewed stop/heuristic predicates do not use
that byte; its wider meaning is not assumed.

Execute8C6400 /74C240 first clamps start through8CA0A0 /750090. A changed start
prevents this search. Otherwise it selects the network indexed by contextC8,
temporarily toggles context extra bounds, finds the start node, calls search with
start and target indices equal, then toggles the bounds back.

The clamp helper8CA3B0 /7503F0 uses `[minX,maxX-1]` and `[minY,maxY-1]` in native
units and reports whether it changed either coordinate. Context dispatcher
8CA0A0 selects context/world/player bounds under actor-context, ignore-bounds,
player and world-mode predicates. It mutates the input point; it is not just a
boolean containment query.

Network search8B3F60 /739070 uses38-byte nodes and1C-byte edges:

| Node offset | Established meaning |
|---|---|
| 0/8/10 | Adjacency vector of32-bit edge indices |
| 18 | Accumulated best edge cost |
| 1C | Predecessor edge index; FFFFFFFF at start |
| 20 | Low20 bits heap index; bit20 queued; bit21 has been popped; upper10 generation |
| 24 | Sign bit can exclude a neighbor under contextE8 bit0 |
| 2C/30 | XY |
| 34 | Movement-cost multiplier used in reconstruction |

Edges contain node indices0/4,float cost8, portal originC/10 and portal delta14/18.
Search increments network28 generation; after999 it sets1 and clears node20
across the node vector. The start has cost0 and parentFFFFFFFF. A popped node
gets bit21 **before** the goal stop callback. Queued nodes can be revisited; bit21
means it has been popped, not a universal proof of optimal finalization.

For each adjacent edge, search selects the opposite endpoint. A different
generation initializes neighbor cost to100000. It relaxes only a strict lower
`current.cost+edge.cost` inside inclusive XY bounds, stores the predecessor edge,
then adds or updates the min-heap. Heap records are10 bytes, float key0/nodeptr8;
helpers8C3260/8C34F0 (GEM749070/749300) maintain low20 node indices during swaps.

Goal vslot3 controls the heuristic. EvaluateNetwork's8A5080 /729F20 returns
false, so its queue uses accumulated cost. Its stop8C6490 /74C2D0 returns true
when popped cost **strictly exceeds**10C; equality continues. The terminating
over-limit node already has bit21. Detour samples require the current generation
and bit21, in addition to geometry/environment/firing tests. This connects the
earlier unexplained node bits to actual graph exploration.

Area goals enable a heuristic: `key=g+2*distance_to_target*factor`. Factor is1
unless `g_pnetDirectionHeuristicsScale`1482310 /12F9770 exceeds1; then it is
`(2-dot(normalized_target_direction,normalized_edge_direction))*scale`.
Initializer62300 binds that native name and sets0. This weighted key does not
justify claiming optimal A* behavior for every goal.

Point→node lookup8B53B0 /73A4D0 uses truncating integer conversion of
`(point-network_origin)/cell_size`, checks signed cell bounds, then traverses
the cell's18-byte partition rows with8C4CC0 /74AB40. The split is `ax+by+c<=0`.
Negative children encode another row index under7FFFFFFF; nonnegative children
are node IDs.8B5200 /73A340 converts a valid ID into a node pointer, otherwise0.
Truncation is not floor for negative coordinates.

## Temporary bounds change graph costs

Context8CA1B0 /7501A0 walks B0..B8 rows of size18, calling8B3590 /738620 for
each stored bound index across active networks. Setting the toggle sets bound
row14 bit2. Covered node34 is recomputed as1 plus costs of covering bounds whose
bit2 is clear, stopping once the sum reaches100000; incident edge costs are then
recomputed by8B9670. Clearing the toggle restores inclusion and recalculates.

This is a graph mutation around a query. It does not save and restore arbitrary
prior bytes. Bound inclusion8BD570, edge-cost computation8B9670, nested/concurrent
use and all bound-registration producers remain open static work.

Context copy8AF0C0 /733F50 also matters for a replacement AI: it copies owned
description and bound vectors, increments each bound shared-control count and
the grid handle's control count, and copies raw actor-link/profile pointers.
It assigns only the low six E8 flag bits. GEM outlines the first part into734050
and the generic network constructor into74C620. A raw memcpy would not reproduce
this ownership behavior. Complete destruction/replacement paths still need review.

## Routes and firing-point refinement

Reconstructor8C4F70 /74ADF0 follows predecessor edges from the selected node
toward the start. Its temporary20-byte rows hold node0, portal origin8, delta10,
fraction18 and cumulative movement cost1C. Caller target and start are included.
Initial fractions are0.5. It has optional portal adjustment/shortcut branches
under task8 and configuration globals. Their controlling branches and output are
reviewed; predicates8C69E0/8C6B00/8C6C00 and shortcut cost8AC950/8ACA00 remain
dependencies, so this is a partial reconstruction contract.

The output copies context at20, stores total cost18, sets valid110 and appends
12-byte rows: **X,Y,cumulative movement cost**, not XYZ. Construction proceeds
target toward start; temporary reversals in the reviewed branches are paired.
8C7150 /74D120 copies all12 bytes per append.

Length8C6EC0 /74CE90 gates on valid110 and at least two points, then sums XY
segment lengths. It ignores the stored cost column. Sampler8C76B0 /74D680 walks
from the last point toward the first. For a positive-length segment it writes
interpolation only if `requested_distance < accumulated+segment_length`.
Exact total length and beyond return false without writing output. Negative
requested distances are not clamped to zero by this helper.

Refiner940780 /7C7DF0 uses seven midpoint firing trials, treating result3 as
feasible, then chooses at least one spatial unit along the route. It ignores the
sampler's return and pre-fills the trial with a route point. At a boundary failure
that prefilled point can survive. This remains a bounded search over potentially
nonmonotonic firing feasibility, not proof of the earliest firing location.

## AreaOutside is one task family

8C8F60 /74EDE0 is `Pather::Task::eAreaOutside` vslot2, rather than the universal
path-request entry. Its base constructor8C8C50 /74EAD0 clones the supplied shape
through vslot2, owns it at28 and obtains its bounding circle at10 through vslot10.
Task8 starts1 and C starts100000.

ProfileD0→F0 bit0 chooses between:

- Network `Goal::eAreaOutside`: stop at the first popped node for which the
  shape's vslot11 containment returns false; save node118 and reconstruct output.
  Inside-area goal uses the opposite test and its center as heuristic target.
- Stack-local grid solver8AFA00 /7348F0 with `Goal::eAvoidArea`. Its constructor
  receives the shape and expands the search rectangle by twice cached radius20.
  The grid search implementation and goal callbacks are not covered by this pass.

Both return output110. The game decompiler omits part of the argument list;
assembly preserves the fourth start-point argument and fifth constructor float.

Grenade escape8FE840 /785000 supplies a circle centered on explosiveXY with
radius `3D actor-to-explosive distance+2`. Success selects type0/mode2 and creates
a Move child. This is an incremental escape distance, not the configured danger
zone. The wrapper destroys the task's cloned shape afterward.

## What the movement probe reads and changes

8AE780 /7335A0 takes a copy of the20-byte compiled-route cursor. Its fields
include segment index0,float fraction4,float progress8,total distance10 and a
raw segment-ring pointer18. Segment vslot6 supplies XY. The real actor cursor
is not advanced, but the probe consumes the selected RNG.

While segments/fraction remain, accumulated cost<100000 and remaining requested
distance>1e-5, it draws once and chooses `min(remaining,(U+0.5)*10)` native units.
Cursor helper86E4A0 /6F2210 crosses at most one boundary per call and returns a
remainder. The caller reduces its remaining distance by requested step minus that
remainder, samples XY and integrates cost. At a route endpoint the helper zeroes
the remainder, so the reported accounted distance is not guaranteed to equal
exact geometric distance when a requested step overshoots the endpoint.

The return is accumulated float cost in XMM0; game C incorrectly says void.
Output distance is requested−remaining. GEM asserts both a valid cursor and
`quantControl.InsideQuant()`. Each loop consumes one RNG draw, even where an
unconsumed part of the prior step crossed a boundary; this is not fixed-spacing
deterministic sampling independent of the RNG stream.

Integrator8ADD70/8ADE80 (GEM732BA0/732CB0) copies context, remembers lastXY and
creates a capsule query whose radius comes from profile20. Each call updates
lastXY, computes `terrain_cost(endpoint)*XY_segment_length`, and, when that is
below100000, adds queried object costs. Otherwise it sets result kind3/object0.
The numeric100000 terrain sentinel is multiplied by length first; this is not an
immediate boolean rejection for every positive sub-unit segment in such a cell.

Terrain lookup8AEBC0 /733A30 reads the first heightmap LOD. A null scene global
returns0. Each4-byte cell supplies terrain=`byte2&15` and slope=`word2>>11&15`.
It rejects with100000 when contextE8 bit0 requires an unavailable profileE0
terrain permission, or either selected profile cost is negative. Otherwise it
returns `1+profile60[terrain]+profile24[slope]`.

These are read windows, not proof of authored array lengths: slope index15 would
read profile60, also the terrain table's first value. Producers may constrain
the slope index further; that needs tracing. Grid coordinate conversion truncates
and clamps. **Only one offset pair is sampled in these binaries**: game starts
at15850A8 and advances8 to its end marker15850B0, even on the radius>2*cell-size
branch. GEM agrees at13EC778/13EC780. The offset pair is zero-initial BSS with no
direct writers located; alias writes are not ruled out.

## Object costs and Move results

Object reducer8AE3A0 /7331D0 walks query results backward. Sum reaching100000
returns100000 and kind1/current raw object. With profileF0 bit2 and type20 bit4,
an initially empty result becomes kind2/current object; a later block overwrites
it. The reducer does not retain the reported object.

Object policy8AE060 /732E90 has several independent read sets:

- Object3F type−1 skips cost. Actor-associated objects can skip matching owner IDs
  against context7C, or skip through actor-link A8 masks64/68 combined with
  object28→78, flags1C/1D and relationship1. These masks' native names and full
  producer semantics remain unbound.
- For object1C+1E, sufficiently distant bounding-box center (≥10 spatial units
  from contextXYZ) is skipped.
- Type lookup8CA360 /750360 addresses40-byte rows; GEM asserts the index.
  Type20 flags and type28 per-profile cost array then govern the result.
- Context mask2/type flag1 skips; context mask4/type flag2 blocks; context mask10/
  type flag4 skips. Ordinary acceptance additionally depends on type flag8,
  object10/1F and context mask20.
- ProfileF0 bit2/type flag10 activates a direction test: require shape kind13,
  nonsentinel direction and nonblocking type cost, select/rotate the shape axis
  from10/14 according to extents18/1C, and accept only
  `abs(axis dot direction)<=sqrt(2)/2`. Other cases return100000.

The manifest retains the exact numeric branch contract. Shape13 and type flag
names are left unresolved rather than guessing labels such as door or fence.

Move8F3E00 /779FA0 supplies chassis68 as lookahead distance, cursor180 and the
kind/object output. The reached branch ignores the float return: kind0 clears
1AD; kind1 or3 reaches order failure2; kind2 can make a Rotate child or call
8F5E70 with the object. Full special-object handling and lookahead producers
remain static leads.

Additional assembly corrections:86E4A0 uses float fraction/progress despite
integer-looking C casts; GEM739070's73977A is an INC, not the decompiler's heap
count assignment1. These are recorded so later bridge work does not copy the
decompiler's signatures or expressions unquestioningly.

## Remaining static work

Continue grid solver/goal callbacks, network creation and edge/bound cost
producers, portal/shortcut helpers, profile/type parsers, terrain/slope metadata
construction, spatial-query object lifetimes and Move's special-object action.
The wider frontier still includes actor enumeration/ID reuse, squad coordination,
damage/material outcomes and remaining AI-readable properties. This pass closes
specific dependencies; it does not exhaust static AI mapping.

## Spatial argument correction

The [line/spatial follow-up](path-lines-map-25492057.md) resolves a mislabeled
argument in the segment-probe manifest. Integrator `+80` is embedded context
`+78` (actor `+594`), forwarded through `771860` and unused by collector `7779D0`.
The actual collection filter is query `+34`, initialized to `40`; category `+30`
is `80`. The manifest and structural validation have been refreshed without
changing the frozen exports. The follow-up also maps line costs, portal pruning,
owner metadata, enable snapshots and raw-pointer collection boundaries.
