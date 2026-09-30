# Spatial volumes, closest-result state and compound traversal — build 25492057

This pass maps the volume interface behind 3D spatial tests: sphere, box,
capsule, rounded rectangle (`eBoundingLozenge3`), compound volume and Handler.
It records70 game/GEM entry pairs and27 layout fields. The larger rectangle
distance routines have partial decision-region reviews; their output arithmetic
and several discrepancies are established separately in assembly.

Evidence: [manifest](../artifacts/build-25492057/spatial-volumes-map.json),
[validation](../artifacts/build-25492057/spatial-volumes-validation.json), and
[static cases](../artifacts/build-25492057/volume-static-examples.json).
Local readonly exports use suffix `-spatial-volumes`. Addresses, offsets, sizes
and shape kinds are hexadecimal; virtual slots are decimal. No native code was
executed. Static examples do not establish gameplay frequency or reachable
entity configurations.

## Layout and virtual interface

| Type / kind | Size | Fields |
|---|---:|---|
| Sphere3 / 20 | 20 | Radius+8, centerXYZ+10 |
| Box3 / 21 | 20 | MinXYZ+8, maxXYZ+14 |
| Capsule3 / 22 | 38 | Radius+8; start+10, end+1C, unit+28, length+34 |
| Lozenge3 / 23 | 48 | Radius+8; lengths+10/+14, axes+18/+24, normal+30, origin+3C |
| CompoundVolume / 24 | 50 | Owned children vector+8, cached Sphere3+20, stored position+40 |
| Handler / 50 | 20 | Same bounds layout as Box3; special outer dispatch |

The volume slots differ from the planar interface:1 writes a Box3,2 clones,
3 translates,4 sets position,5 tests sufficient AABB containment,6 tests a point,
7 returns kind,8 formats a diagnostic string,9 writes a Sphere3.

Sphere/capsule/lozenge point tests use **strict distance² < radius²**. Box3 point
containment is inclusive. Capsule uses its cached segment direction and length;
lozenge independently clamps its two local coordinates before reconstructing
the closest rectangle point. Compound point containment visits children without
a cached-sphere pretest.

Pair intersection, in contrast, compares native distance² **<= summed radii²**.
A tangent point can therefore fail point containment while touching volumes
pass pair intersection. A zero-radius sphere never contains a point through
slot6, including its own center.

Bounds and movement read only geometry in these fixed shapes. Capsule Box3 is
endpoint min/max expanded by radius; lozenge Box3 encloses four rectangle
corners then expands by radius. Capsule Sphere3 uses midpoint and half cached
length plus radius. Box3 Sphere3 uses midpoint and half diagonal.

Translate changes sphere center, capsule endpoints or lozenge origin. Set
position recenters the capsule using cached unit*half length, and sets lozenge
origin to desired center minus both half-side vectors. Queries and translations
do not repair inconsistent cached axes/lengths.

## Pair matrix and result storage

`612720/484E60` dispatches explicit ordered kind pairs. It supports all sphere,
capsule and lozenge pairings, sphere/box, box/box and compound versus any of
kinds20–24. **Box/capsule and box/lozenge are unsupported in either order** and
return false after a diagnostic.

Compound24/24 is supported: the final subtraction chain transforms2424 to1 and
selects the reversed compound branch. This differs from planar CompoundArea,
whose15/15 pair was unsupported in the previous pass.

The outer dispatcher `60EED0/4813D0` projects mixed planar/volume tests only for
sphere20, capsule22 and lozenge23. Mixed planar with Box3 or CompoundVolume is
unsupported here. Handler combinations with planar, volume or Handler return
true at this layer; separate tree/AABB filters still matter.

Native closest-result storage is a process global at `147CF30` (GEM `12F4660`):

| Offset | Meaning established by writers |
|---:|---|
| 0 | Native squared-distance scalar, also returned in XMM0 |
| 4 | XYZ vector; usually Bpoint-Apoint, sometimes a signed unit normal |
| 10 / 14 | First object's two parameter slots |
| 18 | First reconstructed XYZ point |
| 24 / 28 | Second object's two parameter slots |
| 2C | Second reconstructed XYZ point |

Total size is38. Point/point leaves every parameter slot untouched; segment
helpers write one parameter per segment, and rectangle helpers write two.
Unused slots can retain earlier values. Reversed primitive cases swap entire
parameter/point blocks and negate the XYZ vector. Radius expansion affects the
comparison threshold; these points belong to the unrounded cores.

The byte at `1555E08/13BD1C8` is set1 on entry to volume dispatch and cleared by
box/box or box/sphere cases. The outer dispatcher clears it initially. Its
direct-reference census finds stores only in those two functions. The working
anchor name `volume_distance_valid` is not a proven public validity contract.
Box-based cases do not fill the shared result; unsupported pairs also leave
prior result data behind. No body-level lock protects these globals.

## Rounded-rectangle discrepancies

`609330/47B710` computes and clamps both rectangle coordinates U and V and
stores them separately, but reconstructs its result point as:

```text
origin + U * axisU + U * axisV
```

It then calculates distance from that point. The second coordinate is not used
in this reconstruction. Game `609427` stores V, while `60942B/609430` multiply
both X-axis components using U; analogous Y/Z instructions and the editor body
confirm the same result. This is not an uninitialized-local or ABI decompiler
error.

For a2×2 rectangle at origin with ordinary XYZ basis, rounding radius0.25 and a
radius0.25 sphere centered at(1,0,0):

| Evaluation | Result |
|---|---|
| Clamped coordinates | U=1,V=0 |
| Reconstructed rectangle point | (1,1,0) |
| Native squared distance | 1, exceeding summed radii²0.25; rejects |
| Geometric core distance | 0; overlaps |
| Lozenge slot6 on the same point | Accepts, using both independent coordinates |

`6094F0/47B8D0` rectangle/segment and reviewed final output blocks of
`60A010/47C3F0` rectangle/rectangle also reuse the first coordinate for both
axes. Their plane fallback branches can compute the scalar separately from
the reconstructed points, so scalar, point distance and vector length must not
be assumed interchangeable. Full region and degenerate-case tables remain open.

The lozenge Sphere3 helper `612340/484A80` has another specific behavior:

```text
center = origin + (Lu/2)*axisU + (Lv/2)*axisV
radius = roundingRadius + max(Lu/2, Lv/2)
```

For that same2×2 rectangle with radius0.25, the sphere radius is1.25. A core
corner is squared distance2 from its center, beyond radius²1.5625. Thus this
helper can return a sphere that does not enclose even the rectangle core.
Whether affected shapes enter a particular AI query depends on the producer
and update paths, which are the next investigation.

The numeric artifact evaluates exact binary-fraction inputs from reviewed
instructions. It does not run the functions or serve as an exhaustive oracle.

## Compound ownership and query behavior

CompoundVolume owns its raw child pointers. Append `612EE0/485620` takes
ownership without cloning; destruction deletes each child. The first child
supplies the cached sphere. Later children use shared sphere scratch
`147CF68/12F4698`, followed by sphere union `6124A0/484BE0`.

For center distance d above epsilon, union uses
`t=(d-oldRadius+newRadius)/(2*d)`: negative t keeps the old sphere, t>1 copies
the new sphere, otherwise it moves center by t of the difference and increases
radius by t*d. Nearly coincident centers keep the old center and larger radius.
This union cannot repair an undersized input bound.

Compound Box3 comes from its cached sphere's slot1, rather than independently
unioning child boxes. Translation moves children, sphere and stored position.
Clone copies each child's geometry and rebuilds the sphere, but **resets stored
position+40 to zero** instead of copying it. The relationship between stored
position and serialized/source transforms still needs producer analysis.

Intersection `613120/485860` visits **all** children, calling volume dispatch
as `(other, child)`. It records any true result and retains the first successful
result unless a later successful result has strictly smaller shared scalar0.
It restores the selected38-byte result after the loop. No cached-bound pretest,
early success return, state-byte check or state-byte restoration occurs here.

This creates two precise result caveats. A successful box case can leave the
shared scalar untouched, yet the compound helper still compares/copies it.
Also, the argument order differs from primitive dispatch: a compound containing
only sphereA, queried against sphereB, stores the opposite endpoint order and
vector sign from the direct sphereA/sphereB query. The numeric artifact traces
that case. No external consumer's interpretation of this vector is established.

## Dynamic boundary and verification

`613CF0/486490` reads a dynamic object's slot3 kind40/41/42/43, asks slot2 to
fill global sphere/capsule/lozenge/compound scratch, then enters volume dispatch.
Initialization guards protect object construction, not subsequent scratch
mutation. Concrete dynamic wrappers, referenced transforms, snapshot clearing,
and entity lifetime/update ordering remain open.

Both readonly exports contain73 functions and69 exact explicit candidates.
All functions decompiled. LLVM independently matched10,264 instruction starts
across140 reviewed bodies after LOCK normalization. Structural validation
checks binaries, entries, RTTI, constants and instruction locations; it does
not establish semantic correctness or completeness.

Next: native kind parser `610050`, loaders `610A10/610B00/610DF0/6111E0`, dynamic
volume factories and snapshots, full rectangle decision regions, shared-state
aliases, and spatial registration/queued updates. The broader
[AI/entity frontier](static-frontier-25492057.md) remains active.
