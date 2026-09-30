# Shape loaders, entity transforms and authored geometry — build 25492057

This pass connects native shape predicates to their text loaders and entity
update paths. It reviews28 game/GEM entry pairs,23 layout fields and66 token
bindings. The large entity property parser is reviewed only for the relevant
Body/Area/Obstacle branches. Other exported callers remain explicit leads.

Evidence: [manifest](../artifacts/build-25492057/volume-producers-map.json),
[validation](../artifacts/build-25492057/volume-producers-validation.json),
[resource census](../artifacts/build-25492057/bounding-shape-resources.json), and
[numeric cases](../artifacts/build-25492057/producer-static-examples.json).
Readonly exports use suffix `-volume-producers`. Addresses/offsets/kinds are
hexadecimal; virtual slots are decimal. Native code was not executed.

## Loading and configuration

`610050/482600` compares shape names without case sensitivity. It recognizes
the six planar types and Sphere3, Capsule3, Lozenge3, CompoundVolume. It does
not recognize Box3 or Handler. The returned shape kinds differ from the
registry's property token IDs: for example, Sphere3 is token11 but kind20.

The general factory `60FAA0/481F80` accepts all ten types; `60FD60/482290`
accepts planar types; `60FF30/482490` accepts volume types. Each pushes the
shape registry onto the parser's registry stack, loads the object, then pops.
The volume-only factory has no direct code callers in this census. The general
factory is used by entity `Area`, and the planar factory by `Obstacle`.

| Loader | Properties and relevant behavior |
|---|---|
| Circle2 | CenterXY, Radius; defaults zero |
| Box2 | OriginXY and DestinationXY stored as min/max without sorting |
| Capsule2 | Origin/Destination/Radius; rebuilds unit/length; tiny segment uses default unit(1,0), length0 |
| Obb2 | Center, Extent, Axis; default extents(5,5), unit(1,0); authored axis is not normalized |
| Polygon2 | Vertex rows, Center, Radius; Center is bookkeeping and is not added to vertices |
| Sphere3 | CenterXYZ, Radius; defaults zero |
| Capsule3 | Origin/Destination/Radius; rebuilds cached unit/length with a small-segment fallback |
| Lozenge3 | Center writes the rectangle's **origin**, Length/Width write side lengths, Radius, Psi/Theta angles |
| CompoundArea | PositionXY writes stored position only; children loaded through planar factory |
| CompoundVolume | PositionXYZ writes stored position only; children may be sphere/capsule/lozenge/nested compound |

Neither compound Position property translates children or cached bounds during
loading. This explains why stored position and child geometry must be treated
as separate state. The previously mapped CompoundVolume clone copies geometry
but resets stored position.

Lozenge angles are multiplied by the degree-to-radian constant and used to
construct the basis. There is no center-to-corner adjustment. The switch does
not accept Axis, Matrix or the third angle Eta, despite those names existing in
the shared registry. The loader does not clamp negative dimensions or radii.
Numeric conversion itself remains a parser dependency.

Capsule3's fallback is more specific than simply choosing the largest axis.
Squared length at or below approximately1e-6 yields length0. Strictly largest
absolute X selects signed X, otherwise strictly largest Y selects signed Y,
otherwise Z is selected. A tie between X and Y therefore selects Z even when Z
is smaller. Zero selects +Z. The standalone normalizer `15ECD0/105F60` follows
the same rule and returns a float length in XMM0.

## Polygon construction and real resource inputs

`6073B0/479760` stores the supplied center and vertex count, then appends one
feature per consecutive vertex pair. If **any** triple has negative signed
cross product, it reverses the entire feature sequence and rebuilds edges with
swapped endpoints. It does not compute a convex hull, split concavity, remove
duplicates, sort vertices or check self-intersection.

GEM asserts `count >= 3`; the game body has no corresponding assertion. Neither
body asserts consistent turn directions. The builder also does not clear an
existing feature vector. The reviewed loader and matrix-transform caller start
with an empty destination core.

The reproducible census script is
[catalog_bounding_shapes.py](../scripts/catalog_bounding_shapes.py). Its scope
is thirteen base archives and eleven text-oriented extensions:71,888 members,
150,713,849 bytes. It hashes every scanned input into a per-archive digest and
records individual hashes for3,842 matching members. All matching files passed
the conservative SDL parser.

| Syntactic shape declarations | Count |
|---|---:|
| Obb2 | 12,732 |
| Polygon2 | 464 |
| Circle2 | 34 |

Fourteen of these declarations sit beneath `##`/`##obstacle` ancestors; their
native enablement is not inferred. Macro/include expansion and mount resolution
are not performed. Box2 appears as a literal name but not a braced shape head.
No literal Capsule2, CompoundArea, Sphere3, Capsule3, Lozenge3 or CompoundVolume
occurs in this bounded corpus. Maps, mods, binary/procedural geometry and other
extensions remain outside the census, so this is not proof those types are
unused by the game.

All authored OBB axes checked are within1e-4 of unit squared length, and no
negative OBB extents were found. Four polygons have mixed turn signs:

- `pavlovs_house.mdl`, lines343 and369: two Area polygons.
- `generic_ruin_13.mdl`, line163: an Area polygon.
- `beach_cliff2.mdl`, line53: an Obstacle polygon.

Full archive paths, hashes and coordinates are in the census. These are concrete
inputs for the remaining feature-query analysis. Their presence does not prove
a particular query failure or which mounted resource is selected in play.

## Entity geometry updates

`721180/59BF00` updates an entity area record. It requires entity38 bit20, a
manager pointer whose byte168 is nonzero, and current geometry. When the record
is enabled and not suppressed, it reads entity placement50 and, for the cannon
case, may replace the basis with the pivot bone's basis. Translation remains
from the entity. Dirty pivot geometry is refreshed before this read.

Record flags bits5..7 select translation mode1 or matrix mode2. Source geometry
is recordC0; working geometry is record10. Scale defaults to1; a class/category
gate instead calls `6FD580/576FB0`, whose producer contract remains open.

The two helpers are `722750/59D490` for translation/scale and
`7215E0/59C360` for matrix/scale. They compare source/destination kinds, then
dispatch. They support kinds11/12/13/14/15/20/22/24/50. Box2, Box3 and Lozenge3
are unsupported here even though some are accepted by the loaders.

| Type | Translation path | Matrix path |
|---|---|---|
| Circle/Capsule | Transforms center/endpoints, scales radius; capsules rebuild unit/length | Corresponding matrix transform, with planar projection where applicable |
| OBB | Scales both axis and half-extents | Transforms half-axis tips; normalizes first projected delta and measures extents |
| Sphere | Transforms center; copies radius unchanged | Transforms center and scales radius |
| Polygon | Calls destination slot3→13 to add translation to its existing geometry; ignores source and scale | Rebuilds transformed vertices/center; leaves rounding radius unchanged |
| CompoundVolume | Recurses over paired children; transforms cached sphere center and stored position; leaves cached radius unchanged | Same cache limitation with matrix transform |

For scale2, a sphere of radius2 remains radius2 through translation but becomes
radius4 through the identity-matrix path. A unit-axis OBB with half-extents(1,1)
becomes axis(2,0), extents(2,2) through translation; the matrix path gives
axis(1,0), extents(2,2). Both effects are explicit assembly operations.

For a polygon destination initially equal to its source, translation calls
with X offsets10 and20 leave it offset30. This follows the destination-only
slot call. Whether any live caller supplies absolute positions in this case
must be established separately. The static examples execute arithmetic models,
not these native functions.

CompoundArea has an additional distinction. Translation updates its cached
circle center from the **existing destination** center, leaves the circle
radius unchanged, then loops onto the embedded Box2. That kind is unsupported
by this helper, so it diagnoses and returns without repairing the cached box.
The matrix helper derives its cached box from the transformed cached circle;
it also leaves the radius unchanged. Neither path reunions child bounds.

Polygon matrix updates use shared vertex scratch at game156EFA8/GEM13D6668,
rebuild into an empty core and move it into the destination. Scratch is cleared
at the end, not the beginning; initialization guards do not serialize mutation.

After transformation, entity update selects an ID slot at590 or594. Missing
IDs invoke `771580`; disabled/suppressed existing records invoke `7716C0`;
revision mismatch or forced-dirty bit11 invokes `771760` and clears that bit.
Registration/removal semantics, revision producers and owner lifetime remain
the next pass. Other flag selectors leave a null ID-slot pointer, so caller
invariants matter.

The placement predicate `986F70/80FB10` is also mapped: it moves a working
shape with scale1, optionally rejects an exclusion-shape overlap, then queries
the general scene or obstacle manager. Its actor path reads chassisDE0 slot56;
the owner-exclusion path compares every returned area's raw entity98 pointer.
The context constructor and outer placement search remain open.

## Dynamic volume boundary

The concrete dynamic class found is `eDynamicCompoundVolume`. Its interface
is delete/clone/StoreAsStatic/kind, distinct from fixed volumes. Clone allocates
50 bytes, zeroes the payload and clones owned children at38. Cleanup deletes
those children. The remaining payload is not mapped as an entity transform.

Game `6133C0` StoreAsStatic checks that the destination is a CompoundVolume and
then returns **without writing geometry**. GEM `485B00` additionally calls an
unreachable-code diagnostic unconditionally. The source object is not read.
Thus the previously identified dynamic dispatch is not evidence of a working
entity snapshot API.

The dynamic intersection entry has no direct code callers in the census;
dynamic-compound vtable writers are limited to clone and destruction paths.
No concrete dynamic sphere/capsule/lozenge producer was identified. Indirect
calls, hand-built objects and other modules remain outside that conclusion.

## Verification and next leads

Both readonly exports contain33 functions and28 exact explicit candidates;
all decompiled. LLVM independently matches9,105 instruction starts across56
reviewed bodies after LOCK normalization. Structural checks validate entries,
RTTI, constants, token bytes, instruction locations and the three resource
members containing the four polygon findings. These checks do not prove the
semantic interpretations or completeness.

Continue area-record ownership/setters, spatial registration and queued updates,
scale/revision producers, placement context construction, the four mixed-turn
polygon queries, and the remaining rectangle decision regions. The broader
[AI/entity frontier](static-frontier-25492057.md) remains active.
