# Contact solver, body inputs and convex support — build 25492057

This pass follows contact groups into velocity correction, traces their physical
body reads/writes, and maps the convex support and distance calculations that
produce contact witnesses. It covers 40 paired methods at explicitly stated
scopes. The body-state producers and simulator lifecycle remain active leads.
No game or editor execution is involved.

Independent LLVM decoding agrees with 14,486 Ghidra instruction starts across
80 methods after LOCK-prefix normalization. Structural checks pass for both
binary fingerprints, exact entries, 43 field references, 18 RTTI tables, six
read-only constants and the two direction tables. This validates evidence
locations and bytes, not the interpretations or numerical completeness.

Artifacts: [map](../artifacts/build-25492057/contact-solver-map.json),
[validation](../artifacts/build-25492057/contact-solver-validation.json).
Previous pass: [contact queues and geometry helpers](physical-response-map-25492057.md).
Addresses and offsets are hexadecimal; virtual slots are decimal.

## Queue ordering does not survive extraction unchanged

The initial sorter orders by unsigned owner ID `F8`, then unsigned owner ID
`FC`, then ascending penetration `E4` for ordinary finite inputs. Null owner
sentinel `FFFFFFFF` sorts after ordinary IDs. It does not canonicalize A/B order.
Arrays of at most 32 contacts use stable insertion. Larger arrays use partition
and heap paths; no general tie stability is claimed.

Moving a record into a connected component appends its value, swaps the source
slot with the source's final record, and reduces its length. This scrambles the
remaining order. The resulting solver sequence should therefore not be inferred
from the original sort alone.

Queue growth copies the inserted record and old prefix/suffix into new storage,
then frees old storage and updates the vector pointers. Capacity normally grows
to the greater of size plus one and one-and-a-half old capacity. There are no
body reference-count operations. Raw pointers into the vector can invalidate.
The large-sort pivot helper `734CB0` remains a bounded container lead.

## Body state read by a contact

| Body offset | Read/write contract |
|---|---|
| `10` | Vector added as half-delta contribution to friction tangents; producer still unnamed |
| `28` | Position, exposed through virtual slot9 |
| `38` | Four-float orientation quaternion |
| `48` | Linear velocity |
| `58` | Angular velocity |
| `88` | Float mass divisor; solver uses its reciprocal |
| `CC`, `DC`, `EC` | Three matrix rows, three floats each, used as inverse inertia |
| `1B4` | Set to one by the position/orientation/velocity setters |

The matrix getter skips each row's fourth float. Its mathematical role is clear
from the impulse equations; construction, symmetry and units still need their
own producer evidence. The mass getter returns `XMM0`, despite Ghidra's
integer-looking type.

Position and linear-velocity setters copy three floats without a finite check.
The angular setter checks all three components using a double-precision finite
predicate. It commits all three together only when all pass, but sets `1B4`
even on rejection. The predicate receives its value in `XMM0`; its actual bit
test excludes infinity and NaN and permits finite subnormals and zero.

The orientation setter copies then normalizes its four components. All-zero
input becomes `(1,0,0,0)`. Other inputs divide by their length without a finite
check. These helpers contain no direct entity notification or ownership change.

## Preparation and iterative velocity correction

For each present body, preparation copies inverse inertia and computes
`r = contactPoint - bodyPosition`. Its velocity at the contact is:

`v_contact = linearVelocity + angularVelocity × r`

The contact's normal velocity is `(v_A - v_B) · normal`.
Nonzero friction also constructs two tangents and projects relative velocity
onto them. Only these tangent terms include `.5 * positiveDelta * body10`.
The normal term does not receive that contribution.

The selected scene mode is two. Preparation subtracts
`penetration / control` from normal velocity, then resets the contact's two
response-cache flags. This supplies penetration correction as a velocity bias.

`73B6B0 / 5B6AB0` then performs at most four times the component's contact
count iterations. Each iteration chooses the strictly lowest
`(1 + restitution) * normalVelocity` below negative velocity threshold.
Ordinary finite ties keep the earlier contact. If none qualifies, iteration
stops. The requested change is `threshold - selectedScore`.

After changing the selected bodies, it propagates
`deltaLinear + deltaAngular × otherContactLeverArm` into every contact sharing
either body. Contributions add for the other contact's A side and subtract for
its B side. This is why the connected-component step matters: resolving one
contact changes the remaining contacts' urgency.

## Friction and the response matrix

With zero friction, the scalar response is the sum over present bodies of:

`1/m + (r × n) · inverseInertia · (r × n)`

The requested velocity change divided by that scalar gives impulse magnitude.
Linear and angular deltas are applied with opposite signs to A and B. There is
no local zero-mass or singular-response guard.

With friction, the basis is normal, tangent1, tangent2, with
`tangent2 = normal × tangent1`. The tangent construction chooses an axis using
the normal's X/Y magnitudes and assumes a valid nonzero normal.
The solver builds a symmetric three-by-three response matrix, factors it, and
solves for normal and two tangential impulse components. If tangent magnitude
exceeds friction times normal impulse, it switches to a sliding calculation
and limits the tangent magnitude. A negative recomputed normal impulse falls
back to a restitution-only normal correction with zero tangents.

The manifest records matrix fields and factor formulas. The first application
builds and caches them; later iterations reuse them. Body positions do not
change during this scene mode's velocity loop.

One implementation detail must not be silently replaced with an assumed
symmetric formula: in the nonzero-friction, exactly-one-body path, the world
impulse is applied positively to whichever body exists. The B-only branch has
no explicit sign inversion, unlike the two-body and zero-friction paths.
Assembly confirms this. Whether the caller ordering reaches that B-only case
remains a separate question.

The solver returns requested deltas for propagation. If the angular setter
rejects a non-finite result, the outer loop still uses the returned angular
delta. This is an exceptional-input contract, not an observed gameplay fault.

## The positional loop is a different mode

Modes other than two run another loop, again limited to four times contact
count, selecting the greatest penetration above a threshold. Mode zero splits
correction between inverse mass and angular contribution, caps angular share
at `.1` times the lever arm's tangent length, moves the excess into linear
correction, and updates position and quaternion. Other modes use linear
inverse-mass weighting alone.

The only direct component-solver caller located in either binary supplies mode
two and skips this loop. Its branch structure and state effects are mapped;
the detailed mode-zero quaternion angle formula remains open if another entry
path establishes relevance.

## Shape sampling and shape support are separate interfaces

Each `TraceVolume` primitive contains a `DT_Convex` interface at offset `50`.
Its slot5 returns a support point: a point farthest along a supplied direction.
The primary `TraceVolume` slot5 instead appends geometric sample vertices.
RTTI establishes both interfaces; treating them as one would call the wrong ABI.

| Support implementation | Ordinary finite behavior |
|---|---|
| Transform | Transpose-transform direction, query inner shape, forward-transform result |
| Box | Signed half-extents; zero direction components choose positive faces |
| TraceVolume sphere | Radius times normalized direction; zero direction returns origin |
| Standalone DT sphere | Same normalization; zero direction returns `(radius,0,0)` |
| Cylinder | Radius along XY; zero XY chooses `(radius,0)`; negative Z chooses lower cap |
| Polyhedron | Scan stored vertices for largest dot product; first finite tie wins |
| DT Minkowski | Sum both child support points in the same direction |

Polyhedron support reads vertex zero before checking the count. Empty geometry
is not handled locally. Transform support needs no inverse matrix: it uses the
transpose of the forward linear matrix for the direction. The transform and
Minkowski wrappers borrow their child pointers.

## GJK state and witnesses

GEM assertion paths explicitly name `lib/gjk3/DT_GJK.h` and `DT_Convex.cpp`.
This establishes provenance without assuming equivalence to an external
library version.

The simplex stores four A-support points, four B-support points, their four
differences, squared lengths, a four-by-four edge table and weights indexed by
subset mask. Insertion updates edges and weights for subsets involving the new
vertex. Pair, triangle and tetrahedron cases are unrolled. The native arithmetic
order is preserved; a generic algebraic recurrence would not automatically be
bit-for-bit equivalent.

Closest-point and witness helpers divide weighted sums by total weight.
Witnesses average the saved A and B points separately. GEM asserts positive
sums; the game helpers do not independently guard a zero divisor.

The distance query starts with zero direction and `FLT_MAX` squared distance.
It requests support A opposite the direction and B along it. It rejects a
provably distant pair against its supplied squared-distance limit, detects
duplicate support differences and relative lack of progress, and chooses valid
subsets in descending mask order. Included weights must be positive; excluded
points must have a nonpositive weight in the expanded subset.

When ordinary progress fails, it tries all positive-weight nonempty subsets
and chooses the smallest squared distance. A full tetrahedron or the scaled
near-zero threshold also terminates. There is no fixed iteration limit.
It returns squared distance in `XMM0`, writing witnesses only when that value
is within the supplied limit. The early far exit returns `FLT_MAX` without
writing witnesses.

## Penetration uses 42 directions followed by distance refinement

`5FC5E0 / 46E280` scans a fixed table of 42 approximately unit directions.
The game/editor tables are byte-identical. For each direction it obtains
support A opposite it and B along it, then chooses the smallest projected
span. Strict ties retain the earlier table entry.

It casts the inputs to transform wrappers, copies A's matrix, and displaces A
by `direction * (chosenSpan + 1)`. It then calls the GJK distance query with
squared-distance limit `1e30`. The result witnesses are used to reconstruct
the original pair's contact points. The exact displacement/projection formula
is recorded in the manifest.

This function always returns success after that calculation. It ignores the
distance return value, does not check the first cast before dereferencing it,
and has no local recovery when the lower query leaves witnesses unwritten.
The enclosing call constructs the expected transform types; exceptional shape
and numeric reachability remain open. Shared tolerance and diagnostic globals
also require the owning scene's scheduling to establish concurrency guarantees.

Next producers: physical body initialization and mass/inertia updates,
the `10` vector, changed/contact flags, integration and sleep, and the route
from simulator motion back to entity placement. Shape loading and the remaining
chassis contact/landing callbacks are also active static leads.
