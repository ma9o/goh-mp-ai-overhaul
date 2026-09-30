# Ground, water, simulator selection and human throwing — build 25492057

This pass connects physical movement to entity flags, named events and human
recovery. It records85 paired contracts, one game-only token initializer and one
editor-outlined landing function whose ordinary branch appears inline in the game.
The map includes49 fields and26 RTTI tables. No game or editor was executed;
binaries and Ghidra databases remain unchanged.

Independent LLVM decoding agrees with10,877 Ghidra instruction starts across
172 functions. Four additional25-byte RNG initializers were independently decoded
and fingerprinted without claiming a Ghidra function entry. Structural validation
checks addresses, bytes, constants and fingerprints; it does not prove semantic
completeness or all runtime input cases.

Artifacts: [map](../artifacts/build-25492057/ground-state-map.json),
[validation](../artifacts/build-25492057/ground-state-validation.json).
Previous pass: [body state](body-state-map-25492057.md).
All addresses/offsets below are hexadecimal; virtual slots are decimal.

## Configuration and ownership

Entity definition token`SimulatorId=9E` selects six registered implementations:

| Name | Game factory | Editor factory | Allocated bytes |
|---|---|---|---|
| generic | `730D20` | `5ABE90` | `320` |
| grenade | `731490` | `5AC620` | `328` |
| pillar | `731DA0` | `5ACF00` | `330` |
| human | `93E1F0` | `7C5870` | `320` |
| airborne | `846C90` | `6CA2E0` | `320` |
| water | `867550` | `6EB1D0` | `330` |

The parser assigns a96-byte token at definition`368`; its string at token`40`
is definition`3A8`, which creation reads. This closes the apparently different
parser and consumer offsets. `72F260/5AA2E0` reads a string, constructs a token,
assigns it and destroys the temporary. Generic normalization/hash dependencies
are bounded here; creation itself compares the resulting string case-insensitively.

`727710/5A2550` first destroys any old entity`558` simulator and nulls the pointer.
It searches registry`156F898/13D6F58`, a linked list of16-byte nodes referencing
16-byte name/factory descriptors. An unknown name errors after the old object
has been removed. On success it stores the factory result, assigns owner`308`
and invokes slot0. There is no local null-factory-result guard or owner retain.

Base slot0`730130/5AB250` copies owner`308` to the embedded body's`1B8`, or
simulator`1C0`. Dynamic registration`732590/5AD6F0` also exists: it stores the
provided name/factory into a caller-owned descriptor and registers its raw address.
The full descriptor and owning-entity lifetime remains a separate lead.

Stop`726E60/5A1CF0` acts only when entity`1C0` bit4 is set. It clears the bit,
dispatches eventD with zero payload, optionally writes the stop deadline`A80`,
and may send simulation command28. Assembly confirms that the event receives
the entity pointer; the decompiler's apparent null argument is wrong.

## Ground samples, flags and events

`72FF00/5AB020` scans the entity's volume vector, rejecting disabled volumes
(`28` bit5) and requiring the requested flag in volume`C`; ground callers use
bit6. It refreshes each volume and calls its primary virtual slot5 for world
surface points. Output records are24 bytes: XYZ, four padding bytes and a raw
volume pointer. This is distinct from the random interior cache used for inertia.
The temporary point vector is shared; its initialization guard is not proof of
per-thread storage.

Point velocity`737B40/5B2ED0` is:

`linearVelocity + angularVelocity × (point - bodyCentre)`.

The terrain normal uses four height queries around X/Y. Record`4` is a **float**
sampling displacement, while record`0` is an integer converted to normalZ.
All four height calls select level0 even when the normal helper selects another
record for spacing. The result is normalized to length1. Missing terrain uses
the file-initial up vector. Terrain cell construction and units remain open.

Category lookup`A43AB0/8CC3B0` selects level0, truncates offset+position to signed
integer, then divides by integer cell spacing and clamps indices. Four-byte cells
hold signed16-bit height and packed category/secondary fields. Categories3/4/5
are rejected when cell height exceeds scene water height. Water-surface query
`A42910/8CB160` accepts those categories, sets pointZ to the greater of water and
terrain height, and returns ALtrue; otherwise it leaves the point unchanged.
Classification uses the global scene while the height lookup uses the passed
scene. Only AL is the boolean result.

`733320/5AE500` sets entity water bit11 when wet samples **strictly exceed20%**
of all samples. A change calls entity slot43, which passes the bit to simulator
slot6. This identifies the previous pass's environment switch: water selects
`.2*T` damping and half gravity; the other mode selects`2*T` and full gravity.

Water impact requires vertical point speed below`-2.5*velocityUnit`. It emits
when the water bit changes, or when the bit is false and the repeat timer is due.
Remaining mostly submerged suppresses repeated emissions through this path.
The native COMISS/JBE gate also rejects unordered velocity comparisons.

Ground summary`7331B0/5AE380` uses the equivalent threshold for normal velocity.
Independently of whether impact fires, it copies contact presence to entity
ground bit1 and calls slot42 if that bit changed.

The event identities are now bound through RTTI, literal names and the definition
switch, rather than inferred from their offsets:

| Event | Type | Definition field |
|---|---|---|
| ground_hit | `11` | `C2` |
| grounded on / off | `12` | `C4` / `C6` |
| contact | `9` | `E4` |
| swim on / off | `20` | `106` / `108` |

Ordinary ground and water impacts put **positive** impact magnitude in event
context`F0`. Water supplies the token`water`; ground supplies the selected volume.
Both also publish a fresh point/source context. The water summary fires
`ground_hit`, not the separately registered `swim` event.

Ground-change slot42 updates support state, notifies simulator slot5 and dispatches
`grounded on/off`. Support predicate slot49 requires ground or water contact,
rejects entity flags`38&10200`, and checks parent mode when entity bit28 is set.
`71FB40/59AA30` propagates support/override bits through obstacle rows and placement
callbacks. Its precise writes are recorded in the manifest; the complete meanings
of alternate writers to bits17/18/31 remain open.

## Specialized movement

The grenade simulator resets a first-contact latch each ground phase. An incoming
contact above`3*velocityUnit` scales linear speed by`.15` and angular speed by`.1`.
An independently tested original speed below`.1*velocityUnit` zeros both and stops
simulation. It still submits a contact afterward, using fixed restitution`.1`.
Its tick traces backward along displacements longer than1; the first eligible
kind4 hit can correct body position. The full scene ray-query implementation
remains a lead.

The pillar simulator captures an activation origin and pins XY until falling.
It discards angular motion along the entity's third basis and clamps upward
linear motion before falling. For each opposite surface-point pair it adds the
midpoint and both quarter points. Ground contacts use friction1.5 and restitution.4.
When entity matrix element`70` drops below`.9`, it latches fallen, sets entity
bit12 and emits`fallen`. Reactivation does not reset the reviewed fallen latch.

Airborne activation enables a chassis flag, sets linear damping to`20*T` and
uses one selected-RNG draw for angular motion along the first entity basis,
scaled by30 angular units. While airborne, tick invokes chassis slot22 before
the base simulator tick. Ground contact restores`2*T` linear damping. Its contact
coefficients are friction`.2` and restitution`.001`.

Water activation measures spans using **lengths of projected surface points**.
These are not ordinary signed-coordinate widths; absent points give60/60. Binding
captures the number of active bit12 volumes and does not reset the counter on
rebinding. Each tick sets both damping times to`.4*T`.

If fewer volumes remain active than the captured count, water force is distributed
across remaining volumes by shape`28` weights. Otherwise it is applied at four
corners using the captured spans. For wet points below the waterline, force grows
with`1 + depth/(2*lengthUnit)`; no depth cap appears here. A water chassis can
supply the waterline offset at`920`. Corner XY uses body centre, while corner Z
uses entity origin. Force-at-point adds both current force and
`(point-bodyCentre) × force` torque; it does not add a pending impulse.

## Human throwing and recovery

Human throwing has its own two-phase movement controller. It does not use the
ordinary body integration/contact/sleep path; its ground-phase callback is empty.

Preparation notifies the active brain state with eventD and explicitly leaves
cover. StartThrowOff emits event25, temporarily clears volume flags`A6`, records
fatal intent, initializes motion, clears FSM requests and requests`throwoff`.
The admission routine also has a distinct flag-controlled knockdown/death route.

`93BDD0/7C35F0` tries at most20 normal trajectories. Every attempt consumes
**four RNG draws**, including attempt0 where perturbations are zero. The sign
deadband is±0.00001. Attempt index i controls horizontal scale`1±i/50`, one
rotation of`±3*i` degrees and two of`±i/3` degrees. XY is scaled; Z is not.

Acceptance uses the first hit, a placement-boundary test and either a qualifying
hit kind, water category or a human landing probe. An attachment-specific route
instead tries a fixed-speed ballistic solution toward a selected volume; it has
a45-degree acceptance limit and can fail without entering the20-attempt search.
Success writes the velocity and returns integer trajectory time; failure returns0.
The query also replaces shared path scratch, so it is not a pure observation API.

Initialization stores these fields:

| Human offset | Meaning |
|---|---|
| `1428` | Selected flight animation token |
| `1488` | Truncated animation duration / predicted flight duration |
| `148C` | Fatal intent |
| `1490` | Phase:0 inactive,1 flight,2 slide |
| `1494` | Phase start time |
| `1498` | Initial heading |
| `149C` | Remaining slide speed |
| `14A0..14A8` | Angular rates |
| `14AC..14B4` | Flight velocity |

Animation choice uses predicted duration and a clamped **unnormalized** horizontal
velocity/forward dot product. Short forward/backward flights use
`stand_fly_forward` / `stand_fly_back`. Longer backward flights choose among
`stand_fly_back_far`, `stand_fly_tumble_back` and `stand_fly_tumble_back_far`
with cuts`.3/.6`. Longer forward flights choose between`stand_fly_forward_far`
and`stand_fly_tumble_forward_far`. Long choices consume one selected-RNG draw.
Both binaries initialize the RNG fraction scale to exactly`1/16777216`.

Flight advances position using current velocity, then subtracts gravity from
vertical velocity. Going below terrain/water switches to sliding and records
speed. Landing requests the appropriate recovery state and publishes impact.
Here `ground_hit` receives **negative** speed, unlike ordinary ground contacts.
The editor's outlined`7C47D0` receives speed in XMM1; the game's ordinary landing
path is inline in`93D430`. The editor's alternate flag branch is documented
without inventing a matching game function address.

Sliding follows normalized stored velocity, aligns to terrain when dry and
decelerates by`(.2/T)*dt`. Zero speed or water ends the phase, stops simulation,
restores volume flags`A6`, emits`throw_off_end` or`throw_off_end_die` and clears
the actor context. GEM additionally asserts positive truncated animation duration
during initialization; the game does not contain that assertion.

## Remaining static work

This closes the named ground/water callback, owner binding and main throw/landing
questions from the preceding physics passes. Productive leads remain: scene
geometry and boundary queries; terrain/material producers; full landing/death
feasibility and pose-helper conventions; water-chassis inputs; support overrides;
simulation command28 reception; force/impulse writers; simulator and shared-scratch
lifetimes. The broader [frontier](static-frontier-25492057.md) remains active.
