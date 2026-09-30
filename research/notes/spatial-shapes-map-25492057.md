# Spatial shapes and native geometry predicates — build 25492057

This pass maps the geometry consumed by path obstacle collection, touch-area
goals and planar area membership. It records 96 contracts (95 paired game/GEM
entries plus one game assignment whose editor equivalent is inlined), 36 layout
fields and eight concrete RTTI tables in each binary. Two polygon transition
routines remain partial reviews, and the volume slot9 wrapper has an open callee.

The main practical result is that **native shape intersection cannot be treated
as universally exact geometric overlap**. Both binaries contain a permissive
oriented-box/capsule projection test and a height-based clamp in a horizontal
axis-aligned-box edge calculation. Concrete static examples appear below.

Evidence: [manifest](../artifacts/build-25492057/spatial-shapes-map.json),
[validation](../artifacts/build-25492057/spatial-shapes-validation.json), and
[numeric cases](../artifacts/build-25492057/shape-static-examples.json).
Local readonly exports use suffix `-spatial-shapes`. Addresses, offsets, sizes
and shape kinds below are hexadecimal; virtual slots are decimal. No game code
was executed, patched or loaded into a running process.

## Shape interface and read set

| Kind / native type | Allocation | Geometry after vptr at +0 |
|---|---:|---|
| 10 / eBoundingBox2 | 18 | MinXY +8, maxXY +10 |
| 11 / eBoundingCircle2 | 18 | CenterXY +8, radius +10 |
| 12 / eBoundingCapsule2 | 28 | StartXY +8, endXY +10, cached unit +18, length +20, radius +24 |
| 13 / eBoundingObb2 | 20 | CenterXY +8, unit axis +10, half extents +18/+1C |
| 14 / eBoundingPolygon2 | 38 | Polygon core +8, rounding radius +30 |
| 15 / eCompoundArea | 58 | Owned children vector +8, cached circle +20, cached box +38, stored position +50 |
| 21 / eBoundingBox3 | 20 | MinXYZ +8, maxXYZ +14 |
| 50 / eBoundingHandler | 20 | MinXYZ +8, maxXYZ +14 |

Planar slots9/10/11 supply a bounding box, bounding circle and point predicate.
Slot12 sets position; slot13 translates. Slot7 supplies kind. Slot2 clones.
The same slot number on a volume need not have the planar meaning: Box3/Handler
slot9 forwards to `6123F0/484B30`, still an open volume helper.

Planar slot1 (`613FE0/486790`) calls slot9 then writes a Box3 with the planar XY
bounds and Z limits `±binary32(1e38)`. These are finite values. The grid-search
Handler query has both Z bounds at the negative sentinel. Its plane therefore
intersects planar slabs; Z was not disabled by the broadphase.

Broadphase `604C30/477000` checks inclusive overlap on **all three axes**. Its
optional third argument is an output pointer for “A contains B,” not an ignore-Z
flag. If supplied, containment can return early with output1; otherwise the
output is0 and ordinary XYZ overlap is tested.

Slot5 is the containment fastpath used to accept whole spatial-tree nodes:

- Box2 contains the node's min/max XY, ignoring node Z.
- Circle2 checks `distance(circleCenter,nodeCenter) + nodeHalfDiagonal <= radius`.
  This circumscribed-circle test is sufficient, but stricter than testing the
  actual farthest corner for an off-center circle.
- Capsule2, OBB2, Polygon2 and CompoundArea inherit the always-false default.
- Box3 and Handler require inclusive XYZ containment.

Slot9 bounds use endpoint min/max plus radius for capsules, four corners for
OBBs, and polygon feature vertices plus rounding radius. Polygon bounds start
from its stored core center. Slot10 uses midpoint/half diagonal for boxes,
midpoint/half length plus radius for capsules, and the maximum vertex distance
from the stored core center plus radius for polygons. Compound slots9/10 copy
caches; they do not visit children again.

Point membership uses inclusive box bounds, squared circle/capsule distances,
OBB local-axis extents, or the polygon's native signed distance. Compound point
membership first tests the cached circle, then accepts any child's slot11.

## Pair dispatch and two reproducible static discrepancies

`616100/4888F0` enters `616730/488F40` with a “not yet swapped” byte. Dispatch
uses `(kindA << 8) + kindB`; a missing case swaps both arguments once. A second
miss diagnoses an unknown combination and returns false. All unordered pairs
among kinds10–14 exist. Compound15 against kinds10–14 exists; **15 against15
does not**. A compound child that is itself a compound can encounter this same
restriction during recursive dispatch.

The manifest records every pair. Circle/capsule combinations generally use
segment-point or segment-segment squared distance and summed radii. Polygon
combinations build temporary quad/segment cores as needed and compare native
polygon distance with the sum of rounding radii. These helpers use cached unit
axes and lengths; queries do not normalize malformed input.

For an OBB and capsule, `615EB0/4886A0` checks four projection axes: the box's
two axes and the capsule's longitudinal/perpendicular axes. Radius expands each
bound. No extra closest-corner distance test follows.

Take a square from (-1,-1) to (1,1), also represented as an OBB with center(0,0),
axis(1,0) and half extents(1,1). Let a radius1 capsule run from (1.75,1.75) to
(2.25,1.75). Its unit is(1,0) and length0.5.

| Static evaluation | Result |
|---|---|
| OBB/capsule four separation residuals | Each is -0.25, so accepts |
| Box/capsule right-edge branch | Squared distance1.125 > radius²1, so rejects |
| Geometric clearance squared | 1.125, so the shapes are separated |

The axis-aligned routine `603F50/476320` has a different discrepancy. It computes
box height in XMM6 and width in XMM13, but uses **height** for top/bottom edge
parameter clamps as well as vertical ones. This is present in assembly, not
just the decompiler's variable reuse.

Take a nonsquare box from (-2,-1) to (2,1), width4/height2. Let a radius1 capsule
run vertically from (-1,1.75) to (-1,2.25). Both endpoints are above the top
edge, so this branch bypasses the edge-selection helper. The horizontal edge
parameter3 is clamped to2, leaving residual(-1,0.75). The native squared result
is1.5625, which rejects; geometric squared clearance is0.5625, which overlaps.

Relevant game instructions are `603F7F/603F84` (height), `603FB7/603FBD`
(width), `6043F5/6043F8` and `604415/604418` (horizontal clamps).
GEM counterparts are in the numeric artifact. All branch-determining values in
these examples are exact binary fractions. These are **static evaluations**,
not native execution, proof of producer reachability, or claims about gameplay
frequency or developer intent. The previous grid map's “exact intersection”
wording has been corrected accordingly.

## Polygon features, decisions and ownership

A polygon core has a vector at+0, centerXY at+18 and signed count at+20. Count is
used separately from vector end. Each feature row is3C bytes:

| Offset | Value |
|---:|---|
| 0 / 8 | Edge startP / endQ |
| 10 | Unnormalized delta Q-P |
| 18 | Start plane: unit u and constant -dot(u,P) |
| 24 | End plane: -u and constant dot(u,Q) |
| 30 | Side plane: normal(u.y,-u.x), constant -dot(normal,P) |

`606D40/479110` normalizes delta when its **squared** length exceeds roughly
1e-5; otherwise it copies a global axis initially(1,0). The fallback globals
are writable; full writer closure is not claimed. Segment projection uses a
different comparison: XY **length** versus the same epsilon.

The quad constructor `607070/479430` averages four vertices for center and
uses the first three to select winding. It builds four directed features,
reversing order when needed; no convexity/duplicate validation was found here.
`606F40/479310` builds a two-feature polygon with opposing segment edges.

Point distance `607600/4799E0` starts at vertex0 with a budget of2N. Its cursor
stores core pointer, type1 vertex/type2 edge, current index, next and previous
indexes. Vertex-cone plane violations move to an adjacent edge. The edge
helper `607E80/47A260` moves to an endpoint if outside its start/end planes.
For a point behind the side plane, it tries nearer neighboring edges, then
can scan all side planes for their greatest signed value, retaining the first
equal candidate. Nonzero helper status or exhausted budget ends the walk.

The returned float is vertex distance or a **signed** side-plane distance.
Projection outputs accompany it. Ghidra's apparent integer casts are wrong:
the result is carried in XMM0. An interior point can produce a negative result,
which the radius comparison accepts.

Polygon/polygon `6078E0/479CC0` uses two cursors and a budget of3(NA+NB).
It dispatches vertex/vertex, vertex/edge, reversed vertex/edge or edge/edge.
A repeated pair of edge indexes forces one final helper call and exits. Budget
exhaustion is not a separately reported failure. Final results are vertex
distance, signed vertex/edge distance, or zero for edge/edge with computed
crossing/projection points. The final edge/edge division has no local zero
denominator guard. Empty cores already lack the count required by initial
modulo operations.

`608160/47A540` has a mapped four-plane vertex/vertex decision sequence.
`608380/47A760` and `608B20/47AF00` have reviewed endpoint/clipping/side-plane
branches but still need a complete degenerate-case decision table. Their
statuses1/2 both stop the outer walk;2 should not be exported as a universally
meaningful “overlap” enum.

Polygon clone uses a real feature-vector copy (`60F260/481760`). Game
`60F4B0` copies center/count/radius; GEM inlines that portion in clone481B80.
In contrast, `617000/489800` **moves** vector storage, frees old destination
storage and zeros source pointers, while leaving source center/count. Cleanup
also zeros pointers without clearing count. A moved-from core must not be
treated as a complete queryable shape.

Translation updates center and feature endpoints, preserves edge delta/unit,
and recomputes plane constants. Set-position calculates a delta from center.
Capsule set-position moves its midpoint while preserving endpoint half-delta;
cached unit/length/radius are unchanged. These contracts assume a distinct
input position/delta; input aliasing has not been audited.

## Compound areas and shared scratch

`617520/489D20` appends a raw child pointer and assumes ownership. Cleanup calls
each child's deleting destructor. Append does not clone or retain the child;
compound clone explicitly calls every child slot2 before appending the copies.

The first child supplies both cached bounds. Each later child expands the box
by min/max union, but combines circles using:

```text
center = (oldCenter + childCenter) / 2
radius = oldRadius + childRadius + distance(oldCenter, childCenter)
```

This is a conservative, order-dependent bound, not a minimal enclosing-circle
calculation. Stored position+50 is separate from the cached circle's center.
Translation moves children and both caches; set-position subtracts stored+50
then invokes slot13. Clone sets that position before appending cloned children.
No automatic cache-invalidation subscription to later child mutation appears
in these bodies.

Append uses process-global temporary circle/box objects at
`147D028/147D040` (GEM `12F4758/12F4770`). Thread initialization guards protect
construction, not subsequent scratch writes. OBB/polygon distance likewise
uses global corner storage at `1555E80` (GEM `13BD240`). Worker serialization,
reentrant child calls and concurrent mutation remain static leads; this pass
does not conclude that a race happens in play.

## Validation and next leads

Readonly exports contain107 game and110 editor functions, with97/96 exact
explicit candidates. Every export decompiled. LLVM independently matched
10,388 instruction starts across191 reviewed bodies after LOCK normalization.
Structural verification checks fingerprints, entries, RTTI, constants and
instruction locations; it does not prove semantic interpretations.

Continue volume dispatch612720 and its leaves, polygon degenerate/clipping
cases, entity shape construction/refresh and compound cache invalidation,
spatial registration/ID reuse, queued updates and raw-area lifetime. The
[active frontier](static-frontier-25492057.md) still contains substantial
productive static work across AI and entity systems.
