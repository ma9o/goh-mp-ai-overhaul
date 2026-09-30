# Body integration, settling and mass inputs — build 25492057

This pass maps how a simulated entity moves, which state the contact solver
reads, how motion is classified as settled, and how entity geometry and inventory
supply physical parameters. It covers 53 paired methods, 54 fields and six RTTI
tables. Ground/water callbacks and the remaining geometry producers are active
leads. The binaries and Ghidra databases were not changed; no game was run.

Independent LLVM decoding agrees with 9,640 Ghidra instruction starts in the
106 reviewed functions. Two additional 25-byte initializer regions were decoded
and fingerprinted directly because Ghidra has no function definitions there.
Structural validation checks evidence locations, constants, tables and binary
fingerprints. It does not prove the semantic interpretation or all input cases.

Artifacts: [map](../artifacts/build-25492057/body-state-map.json),
[validation](../artifacts/build-25492057/body-state-validation.json).
Previous pass: [contact solver and body reads](contact-solver-map-25492057.md).
Addresses and offsets below are hexadecimal; virtual slots are decimal.

## State and ownership

The base simulator allocates `320` bytes and embeds an `eRigidBody` at `+8`.
That body occupies `300` bytes. Its own raw entity pointer is `1B8`; the
simulator has another entity pointer at `308`. The factory initializes both to
null. Destruction clears the body pointer and frees its four motion-history
vectors. The binding producer and full owning-scene lifetime still need tracing.

| Body offset | Meaning |
|---|---|
| `10` | Base acceleration, initially gravity in the entity wrapper |
| `20`, `24` | Linear and angular damping time constants |
| `28` | World centre position |
| `38` | Scalar-first orientation quaternion |
| `48`, `58` | Linear and angular velocity |
| `68`, `78` | Integration result caches; their units differ between paths |
| `88`, `8C..94` | Mass and three local principal moments |
| `9C/AC/BC` | Padded rows of world inertia |
| `CC/DC/EC` | Padded rows of inverse inertia |
| `FC/10C/11C` | Padded rotation rows |
| `1B4` | State changed; normalization/matrix refresh clears it |
| `1C0` | Local offset from entity origin to physical centre |
| `1D0..1D8` | Extents measured from cached interior samples |
| `1DC` | Contact flag for this step |
| `1DE`, `1DF` | Ready to sleep and pending qualification |

Base construction initializes current and persistent **world** force/torque,
pending impulses, pose and velocities. It sets both local-force enable flags to
zero; the local force/torque vectors themselves are not initialized there.
Derived construction sets damping to `2 * T`, where file-initial time unit
`T=1000`. Its default mass is20 and default box dimensions are45,40,70.

`73C310 / 5B7710` accepts the mass in **XMM3**. It rejects negative principal
moments and a moment strictly greater than the sum of the other two. Equality
and zero pass, despite stricter wording in its diagnostic. The COMISS/JA checks
do not reject NaN, and mass has no local validity check. Successful initialization
stores the parameters and refreshes the matrices.

## Acceleration, forces and integration

`73F5B0 / 5BA9B0` closes the inverse-inertia producer from the preceding pass.
It builds rotation from the quaternion, forms symmetric world inertia
`Rᵀ * diag(principalMoments) * R`, and computes its cofactor inverse. There is no
zero-determinant guard in that inverse calculation.

`73FC40 / 5BB040` computes, for ordinary finite inputs:

- Angular acceleration: `I⁻¹ * (torque - angularVelocity × (I*angularVelocity))`.
- Positive angular damping time adds
  `angularVelocity * (exp(-dt/tauAngular)-1) / dt`.
- Nonpositive linear damping time gives `force/mass + baseAcceleration`.
- Positive linear damping time gives
  `(velocity-(force/mass+baseAcceleration)*tauLinear) * (exp(-dt/tauLinear)-1)/dt`.

The damping math calls `expf`; reduction constants and its power-of-two table
support that identification. CRT exceptional-input handling remains bounded.
Force reset rebuilds the current vector from its persistent world vector and,
when enabled, rotated local vector. Torque reset works the same way.

Pending linear impulse adds `impulse/mass`; pending angular impulse is rotated,
divided by principal moments, and rotated back. Both pending vectors are cleared.
The integrators apply these impulses when their third integer argument is zero.
A zero time step still consumes pending impulses and refreshes matrices.

`737100 / 5B23F0` snapshots state, clamps speed, then chooses integration from the
contact flag. The linear cap is `250 * velocityUnit`; the angular cap is
`10 * angularUnit`. Both branches receive `XMM1=dt` and `R8D=0`.

With contact, virtual slot2 calls `73C3F0 / 5B77F0`: compute acceleration,
update velocities, advance position using the **new** velocity, and advance the
quaternion using the new angular velocity. Force/torque reset precedes the final
rotation refresh in this path.

Without contact, `73C560 / 5B7960` uses four acceleration evaluations and
half/half/full trial states. All acceleration calls use the full `dt` in their
damping formula. If `Ki=dt*ai`, final velocity uses
`(K1+2*K2+2*K3+K4)/6`. Position uses the original velocity plus
`(linearK1+linearK2+linearK3)/6`; the quaternion step uses the corresponding
angular expression. Each trial normalizes and rebuilds rotation/inertia.
This documents the native algorithm without assuming equivalence to a generic
Runge–Kutta implementation for rotations.

A material interface caveat: the multistage path writes weighted linear velocity
change **divided by dt** to `68`, but writes weighted angular velocity change
**without dividing by dt** to `78`. Both binaries' assembly confirms this.
The contact path stores actual angular acceleration at `78`. These fields cannot
be exposed as uniformly typed acceleration readings without accounting for the
integration path.

## Transform import and recovery

Matrix import computes the world physical centre from entity matrix and local
centre offset. Export subtracts the rotated offset to recover the entity origin.
The body therefore does not always share the entity's position.

The matrix-to-quaternion helper uses a negative scalar convention. In its
nonnegative-trace branch, `s=sqrt(trace+1)`, scalar is `-s/2`, and vector terms
are antisymmetric matrix differences times `.5/s`. The negative-trace branches
select the largest diagonal, using the first on ordinary ties. The negative-half
constant and branch signs were checked in the binary. The wrapper normalizes
the result; neither helper validates orthogonality or finiteness of the matrix.

The snapshot contains13 floats: quaternion4, position3, angular velocity3 and
linear velocity3. Restore copies all of them and normalizes the quaternion. It
bypasses the finite-component rejection in the ordinary angular-velocity setter.

The recovery guard tests finiteness of the **float sum** of the six velocity
components. Finite components can overflow that sum. It also constructs the
entity-origin matrix and calls the affine inverse helper. A failed test restores
the snapshot and performs a zero-delta integration refresh.

The inverse helper rejects a finite absolute determinant below `1e-10`, leaving
output untouched. Equality passes. Its COMISS/JA sequence also permits an
unordered NaN determinant to reach the stores and return true when floating-point
exceptions are masked. Consequently this guard does not establish finite pose.
The centre-offset setter ignores inverse failure; the mass builder similarly
continues after its conditional inverse store. Upstream invalid-input reachability
has not been established.

`737770 / 5B2A60` is a guarded **zero-delta refresh**, not a motion-history reset.
It can consume pending impulses. This corrects its provisional discovery label.

The outer simulator tick imports the entity matrix, converts integer delta
`1556E4C` directly to float, integrates, exports, applies the placement query,
and sends `6FF260(entity,4,matrix)`. There is no division by1000 at this call.
If the sleep flag is set, it requests simulation stop through `726E60(entity,1)`.

## What counts as settled

The entity body has four `48`-byte motion histories, at `1E0`, `228`, `270` and
`2B8`. They track the centre and centre plus each rotation basis multiplied by
its sampled extent. Rotation can therefore keep a body awake even when its
centre moves little.

Each history stores a vector of16-byte rows: signed32 timestamp and XYZ velocity.
The first sample uses the baseline vector, initially zero. Later samples use
position difference divided by the integer timestamp difference, without a
zero-interval guard. The decompiler makes part of this look like float timestamp
arithmetic; both assemblies use integer subtraction and then conversion.

For multiple samples, it trapezoid-integrates adjacent velocities and divides by
at least one elapsed time unit. The accumulator starts from global `1556DE8 /
13BE1A8`, in zero-initialized storage; the direct write census finds no writes,
which does not exclude bulk or aliased stores. The sample window is initialized
with the existing duration conversion of1.0, initially1000 time units.

Pruning happens **after** averaging. Scanning backwards from the second-last
sample, the helper discards the prefix through the first sample older than the
window. The latest sample survives. Thus the average can still reflect samples
that have just been removed until the next update.

After accumulated delta exceeds the elapsed threshold, a qualifying check needs
contact in the original step and all four squared average-velocity magnitudes
strictly below the motion threshold. Unordered comparisons fail these checks.
The first success sets pending and clears sleep-ready. A subsequent success sets
sleep-ready, clears pending, resets elapsed and empties all histories. The first
success does **not** reset elapsed: these are two qualifying checks, not two
separate elapsed windows. Failure clears both flags but keeps elapsed/history.

`phys_rest_speed` initializes to `.001 * velocityUnit`, initially about`.00002`.
The consumer compares squared velocity directly against it; initialization does
not square it. The elapsed threshold initializes to `.2*T`, initially200.
Both are writable state, and these are static initial values rather than observed
runtime settings. The elapsed initializer is the raw `49490 / 3A640` region.

## Entity mass and geometry introduce more inputs

Simulation mass is entity `A2C + A30`. Joining existing evidence identifies
`A2C` as current inventory load and `A30` as authored `Mass`. The latter is token
`9C` in the entity registry; another registry has a different `Mass` token `9E`.
Both game and editor parsers write `A30` from case`9C`. Inventory recomputation
and parent propagation were reviewed in the earlier game-only damage/load pass.
This closes an uncertainty repeated in the later contact notes without extending
the scope of that older game-only evidence.

The centre builder visits every entity volume, with no enabled/collision flag
filter. Each contributes `shape28 * volumeDefinition400` times its world origin.
Those two weight-factor producers are still open. The centre is the weighted
mean of volume origins, not the mean of the random samples below.

The moment builder transforms each volume's interior samples into entity-local
coordinates relative to this centre. Each point receives its volume weight
divided by sample count, then contributes the usual pair of squared coordinates
to each diagonal moment. The result is rescaled by total entity mass divided by
total geometry weight. There is no off-diagonal covariance or principal-axis
eigensolve here. Extents are the maximum absolute coordinates of these same
samples, not necessarily the full bounding box.

If geometry weight is nonpositive, the fallback uses the entity origin and unit
box moments. That branch does not overwrite extents: they start at zero in the
constructor but could retain an earlier value on recalculation. Both branches
finish with import and zero-delta refresh, not history clearing.

`5E9FA0 / 45BA30` lazily obtains the interior cache at shape`10..20`. This is a
different path from virtual slot5's transformed surface/vertex enumeration.
`5EA800 / 45C290` reserves16 points and repeatedly draws XYZ candidates inside
local bounds. It retains a point only when primary slot4 returns1, and stops at
16 accepted points. There is no retry limit. Nonempty caches are reused; broader
invalidation and construction paths remain open.

Each attempt advances the selected RNG three times, including its draw counter.
The third draw supplies X, the second Y and the first Z. An empty cache therefore
consumes at least48 draws and more when points are rejected. No RNG-context
replacement occurs inside this routine. The previously mapped selected RNG
getter may fall back to the default stream. Reproducing physical setup needs
this cache state and RNG ordering as well as the visible entity properties.

GEM explicitly asserts that cache creation is outside a Quant worker thread and
that every bound extent exceeds`.01` in absolute value. The game omits those
assertions. The paired worker query gets the current thread ID, looks up its
record, and tests record`C==3`; null yields false. Invalid geometry reaching the
unbounded rejection loop remains a producer/precondition question.

## Closed edges and next work

The previous contact-sort pivot is now mapped: it compare-exchanges three raw
records using unsigned owner IDs then finite penetration depth. Ground contact
submission uses body A only, null B, maximum material friction and minimum
restitution. This particular path therefore cannot produce the solver's B-only
friction ambiguity; other caller ordering remains open.

The simulator environment setter changes damping to`.2*T` and gravity to half
strength when true, or restores`2*T` and full gravity when false. Its caller still
needs tracing before attaching a water-specific name to that input. The ground
scan contains water-category checks, an`.08` duration timer, summary callbacks,
terrain-normal queries and material lookup; only this outer flow is covered here.

Continue with `72FF00`, `733320`, `7331B0`, `704400`, simulator owner binding,
shape-weight and material producers, force/impulse writers, and the human
throw/landing callbacks. The [frontier](static-frontier-25492057.md) remains active.
