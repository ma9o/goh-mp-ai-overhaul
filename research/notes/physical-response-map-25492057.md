# Physical contact queues and geometry helpers — build 25492057

This pass follows the entity-contact handoff into native contact storage,
per-batch reduction and connected-component resolution. It also resolves the
matrix, triangle-point and installed sphere-sampling helpers used by entity
volumes. Everything here is static evidence from the installed game and GEM
binaries. The inner impulse and penetration solvers remain active leads.

The manifest contains 29 paired method contracts, 28 fields and four RTTI
tables. Independent LLVM disassembly agrees with 8,439 Ghidra instruction starts
after normalizing LLVM's separate LOCK-prefix presentation. Structural validation
also checks both binary fingerprints, exact entries, 12 read-only constants and
eight sets of mutable file-initial bytes. These checks do not prove semantics.

Artifacts: [map](../artifacts/build-25492057/physical-response-map.json),
[validation](../artifacts/build-25492057/physical-response-validation.json).
Preceding evidence: [entity contacts](entity-contact-map-25492057.md).
All addresses and offsets below are hexadecimal; virtual slots are decimal.

## Contacts are value records with borrowed bodies

`733690 / 5AE870` zeroes a `1C0`-byte record, then stores:

| Offset | Meaning |
|---|---|
| `C0`, `C8` | Optional raw physical-body pointers |
| `D0`, `D4` | Friction and restitution |
| `D8` | Contact point, three floats |
| `E4` | Penetration |
| `E8` | Contact normal, three floats |
| `F8`, `FC` | Cached owner entity IDs, or `-1` for a null body |
| `100` | All nonnull bodies are local |

GEM names `_friction`, `_restitution` and `_penetration` in assertions.
Its checks require friction and penetration to be nonnegative, restitution in
`[0,1)`, and normal length within `.01` of one. The game does not enforce those
constraints here. This resolves the preceding pass's unnamed coefficients:
the point-response path uses the larger material `98` value as friction and
the smaller `9C` value as restitution. The general response supplies friction
zero and the same restitution combiner. Material parser tokens remain open.

The constructor sets each nonnull body's `1DC` byte. It takes no owning
reference. It reads owner ID through `body1B8->C0` without an owner null check.
The locality query itself does check for a null owner: `eRigidBody` slot1,
`737B00 / 5B2E90`, returns owner-present and owner `3A` bit0. The base
`Physics::CRigidBody` implementation returns false. A null body counts as local
for contact classification, including the both-null case.

`733EC0 / 5AF160` selects the general queue (`1571558 / 13D8C18`) or local
queue (`1571570 / 13D8C30`) and forwards the record in `RDX` to the append
helper. The helper copies all `1C0` bytes. The queues do not retain a pointer to
the caller's stack record. Local means entity locality here; it does not label
stationary bodies. No locking is visible in this submission path.

## Each batch is reduced separately in each queue

The batch guard saves a maximum count and both starting queue lengths.
The entity resolver supplies four. On guard completion, each appended tail is
reduced independently; existing contacts are ordinarily outside that selection.

For an oversized tail, `733A80 / 5AED30` first selects the deepest penetration.
For each later slot it scores each remaining contact using the minimum, over
already selected contacts, of:

`candidate penetration + distance(candidate point, selected point)`

It selects the largest score, swaps complete records, then truncates to the
original length plus the batch limit. Ordinary finite depth and score ties keep
the earlier candidate. The distance-minimum accumulator starts at `FLT_MAX`.
This preserves both deep contacts and a spread of contact points.

There is a precise edge case worth retaining in the contract: later selections
start with candidate index zero and score zero. If no candidate improves that
score, index zero survives even when the batch began later in the vector. It can
therefore swap a record from the old prefix. This is a static branch result;
its gameplay reachability is not established. Unrolled comparisons also prevent
generalizing ordinary finite tie behavior to all NaNs.

Queue access returns a borrowed vector; clear rewinds both end pointers.
`733EF0 / 5AF190` stable-compacts both banks, removing records where either
body matches its argument, with no body destruction or release.
The direct caller census reaches entity event handler `71A7C0` case `D`:
event payload `+8 == 0` performs deactivation placement/obstacle work and removes
contacts for `entity558+8`. Activation instead initializes the simulator,
copies effective velocity, clears angular velocity and updates its transform.
The broader simulator lifecycle is still open.

## Resolution groups contacts connected through shared bodies

`734190 / 5AF430` processes the general queue and then the local queue.
Its delta arrives in `XMM0`, forwarded in `XMM1`. The scene call at `A1B9EF`
converts integer simulation delta `1556E4C` to float directly; there is no
division by 1000 at that call.

`734200 / 5AF4A0` sorts the bank, moves its first contact to a reusable scratch
vector, and repeatedly adds remaining contacts that share either nonnull body
with any contact already added. Its growing scan computes connected components,
so contacts involving A–B and B–C belong to one component even without A–C.
Each component reaches `73B6B0 / 5B6AB0` with count, integer two, timing controls
and `.5`. Queue sort, record move and the inner solver are the next boundaries.

The first control is `.04` at delta `<=.01`, `.12` at delta `>=.1`, and linear
between, multiplied by `14776C8`. Another is `1547DB0 * .0025`.
These are the actual scalar formulas, not an inferred seconds-based API.

The two direct writers of the blocking-speed multiplier `1547DAC` are
initializer `2FA60 / 232E0` and recalculation `4C0C00 / 361EF0`. With
`L=14776C0`, `K=14776C4`, `T=14776C8`, `H=14776CC`:

- `1547DAC = (K*L)/(H*T)`.
- `1547DB0 = 1 / ((1/L)/(1/T))`.

File-initial inputs are `20, 1000, 1000, 3600`, giving approximately
`.00555556` and `.02` respectively. These inputs are writable configuration
state; the file values are not a live observation.

## Matrix and closest-point contracts

Affine composition `1EF900 / 169110` computes `B(A(point))` with three basis
vectors and translation. Output writes interleave source reads, so arbitrary
in-place aliasing should not be assumed.

Inverse helper `1F0C30 / 16A700` uses a custom SIMD convention: four input
vectors and four result vectors in `XMM0..3`, with an optional determinant
output pointer in `RCX`. Ghidra's scalar-looking prototype loses that contract.
It computes the inverse before its caller checks determinant validity.

Cache updater `5E9E60 / 45B8E0` leaves the previous inverse untouched when
finite `abs(det) < 1e-10`; equality permits the copy. Its actual
`COMISS threshold, abs(det); JA skip` sequence allows unordered NaN to fall
through to the stores with masked floating-point exceptions. GEM adds a
`valid` assertion on the rejected path. The volume has already recorded its
bone revision before this update, so a rejected inverse can remain stale until
another refresh. Invalid-transform reachability remains unproven.

`5F6140 / 467E60` reserves XYZ storage while preserving size and existing
points. It does not clear the vector, resolving the earlier polyhedron sample
prefix question. Resize `5F36D0 / 4654D0` preserves the old prefix and exposes
uninitialized new slots; callers subsequently fill them.

Triangle point helper `5F23E0 / 464150` returns signed plane distance when
the projected point passes all three directed edge tests. At the first failed
edge it delegates to a segment projection and returns nonnegative Euclidean
distance. Its plane is built from reversed vertices, with `+Z` fallback when
the cross-product squared length is at most `1e-6`. The polyhedron's outer
negative-distance convention follows from these branches; authored winding and
convexity remain loader questions.

Segment projection divides by edge length squared without a zero-length guard.
Ordinary projection parameters clamp to `[0,1]`. Unordered NaN follows the
interpolation branch rather than an endpoint with masked exceptions. Both
distance helpers return a float in `XMM0` despite misleading decompiler types.

## Installed sphere caches contain 12 and 42 points

Startup invokes the vertex and triangle generators for subdivision levels one
and two. Both first append the twelve icosahedral vertices: two poles and two
five-point rings. Thirty edges each contribute `n-1` spherical interpolants.
The installed paths therefore yield 12 and 42 unit points. The triangle loops
produce `20*n*n` faces: 20 and 80, represented by 60 and 240 indices.

The sphere sample consumer selects 42 points only when radius exceeds
`.5*14776C0`; otherwise it uses 12. The file-initial cutoff is ten. It transforms
and appends these points. General higher-subdivision interior construction and
all index formulas are not claimed complete; the installed levels skip those
interior branches. The generators preserve existing vector contents.

The outer pair solver `5F81F0 / 469CA0` now has a scoped contract: opposite
support directions, a four-point simplex, weight/convergence rejection and
handoff to `5FC5E0 / 46E280`. Support implementations, simplex weights and the
penetration routine remain open. Algorithm names alone are not evidence that
the implementation matches a standard GJK/EPA version.

The continuing frontier includes body mass/inertia and velocity producers,
solver scratch and impulses, material binding, chassis callbacks, volume-event
lifetimes, unit configuration and transform revision producers.
