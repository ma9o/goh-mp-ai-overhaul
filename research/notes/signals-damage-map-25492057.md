# Signals, damage, resistance and carried load — build 25492057

Static evidence only. Addresses are RVAs in `call_to_arms.exe` unless marked
editor. This pass extends [entity health](entity-health-map-25492057.md),
[perception](perception-map-25492057.md) and [weapon behavior](combat-map-25492057.md).
The manifest distinguishes reviewed contracts from partially reviewed functions.
Decompiler return types are unreliable for several floating-point methods below;
the contracts use the XMM instructions and native dispatch tables as evidence.
Installed configuration is evidence of supplied definitions, not proof that every
unit or mission uses those values after resource overlays and overrides.

## Named signals and their producers

`0xA5C9C0` parses an eight-byte descriptor: type at `+0`, radius at `+4`.
Its case-insensitive name table at `0x10FF770` is:

| Value | Native name |
|---|---|
| 1 | movement |
| 2 | shot |
| 3 | hit |
| 4 | injure |
| 5 | explosion |
| 6 | mandie |
| 7 | sound |
| 8 | ally_info |

Unrecognized names resolve to zero and hit an `Unknown signal` assertion.
The parser multiplies radius by the spatial scale at `0x14776C0`; its file-backed
initial value is 20. `0x2FA60` and `0x4C0C00` calculate the reciprocal at
`0x1547D90`. Thus raw world coordinates and configured distances must not be mixed.

`Interaction::eAction_Signal`, primary vtable `0x10D4830`, has parser slot5
`0x74F210` and Execute slot1 `0x74F220`. The parser targets action `+28`;
Execute publishes that descriptor at context entity position `+74` with null
payload through `0xA5CA90`. Factory `0x750230` allocates `0x30` bytes and clears
the descriptor. The named action registration at `0x147FE90` pairs `signal`
with that factory, registered by `0x4BE80`.

The world publisher's signal record is `0x28` bytes: type `+0`, radius `+4`,
XYZ `+8`, typed actor source `+18`, payload `+20`. It queries a bounding sphere,
excludes the source ID, tests recipient availability/flags and the definition
threshold at entity `+548 -> +3C8`, then invokes recipient slot51. The publisher
does not itself trace an occlusion ray; sensor handling has additional gates.
Actor slot51 forwarding and injury/corpse producers are covered in the preceding
pass. Source entity ID is read at `+590`; complete ID/lifetime semantics remain open.

`gamelogic.pak/set/interaction_entity/human.inc` supplies two explicit producers:
`bullet_hit` emits `hit` radius5 before its damage branches; `die_scream` emits
`mandie` radius5 after its death-cry action, guarded by `die_sound`. This supplements
the corpse-observation `mandie` producer previously found in `0x903B10`.
The fauna interaction definition also supplies `hit` radius5. The archive search
was bounded to the selected text member types; it is not a claim that these are
the only signal producers in all resources.

The publisher also calls `0x9D25D0`, which sets received-mask bits on registered
scene objects whose enabled mask `+38` contains `1<<type` and whose lazily resolved
area `+60` contains the signal position. `Helper::eTerm_signal` is the matching
mission condition: factory `0x9F9950`, registry `0x1485958`, initialization slot6
`0x9F76B0`, predicate slot9 `0x9F76E0`. Initialization resolves its named area and
ORs requested signals into that object's `+38`. The predicate intersects requested
mask `+78` with received mask `+3C`; if nonzero, it succeeds and, by default,
clears the **entire received mask**, not just the matched bits. Binder `0x9F7710`
names `signals` and `reset_signals`; reset flag `+7C` defaults true. Area lookup,
registration removal and interaction between multiple conditions remain open.

## Sound definitions also control perception

`0x4CFC50`, identified by `eDeviceSoundChannel.cpp` diagnostics, parses these
fields in a sound definition:

| Offset | Meaning | Binding |
|---|---|---|
| `+0` | volume group | effects0, emotions1, speech2, environment3, other4 |
| `+4/+8` | minimum/maximum distance | `Distance`, `MinDistance`, `MaxDistance`; spatial scale |
| `+C` | repeat interval | `SignalPeriod` |
| `+10` | additional loudness value | copied from loudness entry `+28` |
| `+14/+18` | signal type/radius | `Signal`, through `0xA5C9C0` |
| `+1C` | volume | `Volume` |

Registry `0x1548340` assigns E8..EF to Tag, Distance, Signal, Volume,
SignalPeriod, MinDistance, MaxDistance, Loudness. When the caller requests defaults,
the parser looks up `default` loudness through `0x4D3D70`; missing defaults assert.

`0x4D2810` creates sound-channel records of stride `0x60` in manager `+78/+80`.
Selected channel fields: signal position `+30`, state flags `+40`, asset-row
pointer `+48`, full handle `+50`, creation flags `+58`. Asset-row `+0` points
to the folder definition; its `+14/+18` is an inline type/radius descriptor.
The later [sound-definition pass](sound-definitions-map-25492057.md) establishes
that extra indirection, inheritance, selection and additional cleanup paths.
Publishing requires the
creation flag bit0, nonzero definition signal, channel state bit0 clear, and a
nonnull source with entity type bit0 and entity `+38` bit16 clear.
It calls `0xA5CA90(definition+14, source, channel+30, payload)`.

For repeating channels, creation flag bit1 and a positive period append a
`0x18`-byte record: handle `+0`, source pointer `+8`, next deadline `+10`.
`0x4D1D20` and the FD2 branch of `0x4D3F10` resolve handles using their low16
index plus full-handle equality, publish when due and advance the deadline to
current time plus converted period. Source pointer ownership, cancellation and
remaining channel cleanup branches are still open.

In installed `sound/sound.pak`, `defines.inc` supplies shot signals in small-arms,
launcher and cannon sound definitions. Small-arms close/third-person S1..S4
use radii120/130/140/150 and periods2/2/3/3. The reviewed launcher/cannon
definitions reach radius200 and period7. Explosion close definitions use radius5
for S1/S2/Auto and10 for S3..S6. `signal_presets.inc` supplies interior shot radii
160/160/180/200 for its four named gun groups. These are configured signal values;
exact definition selection and channel creation flags must also be satisfied.

## Damage action dispatch and common curves

The primary `Interaction::eHealthDamage` vtable is `0x10D24F0`.
Slot1 `0x741640` checks that the context entity is an actor then invokes slot9.
For ordinary health damage slot9 is outer executor `0x741710`; vehicle subclasses
use `0x745960`. Three subsequent virtuals split the calculation:

| Subclass | Slot10: health damage | Slot11: impact/effect magnitude | Slot12: location coefficient |
|---|---|---|---|
| base | pure virtual `0xF1BCB4` | zero `0x742390` | `0x7423A0` |
| Pierce | `0x7428B0` | `0x742870` | `0x7423A0` |
| Blast | `0x742AE0` | context `+F0`, `0x742AC0` | `0x742B80` |
| Count | action `+60`, `0x742BF0` | inherited zero | inherited |
| Blow | context `+F0`, `0x742C80` | inherited zero | inherited |

The normal coefficient calls vitality `0x925050` with blast=false; Blast passes
true. `0x742C00` parses Count's scalar before invoking the common parser.
Crush slot9 `0x742CD0` chooses between two configured event names according to
vitality dead bit0, then dispatches the event. It does not subtract health itself.
Crush's two tags start at `+28/+88`, parsed by `0x742D20`.

Common parser `0x7423D0` binds `effects` to a vector at action `+28/+30/+38`,
`0x60` bytes per event tag, padded to at least seven entries. `table` binds the
float-pair curve at `+40/+48/+50`; the literal misspelling `explosive_treshold`
binds float `+58`. Sampler `0x3FB290` returns0 for empty curves, clamps to endpoint
values outside the range and linearly interpolates between adjacent pairs.
An invalid/nonbracketing table reaches an assertion.

## Projectile damage, armor and Avoidance

`0x748B90` supplies the factor used by projectile health and impact calculations.
Context `+120` points to the projectile; projectile `+130` points to its definition.
The ordinary factor samples the curve referenced at definition `+80`, using
projectile `+FC / definition+20` as its input. Optional effect flags receive bit3
when projectile `+FC * reciprocalSpatialScale < 5`, and receive bit1 generally.

For the projectile `+AB` bit0 branch, the factor instead uses auxiliary data
at projectile `+128 -> +7D8`: mean `+30` plus uniform[-1,1] times deviation `+34`.
It skips that random draw when the absolute deviation is below epsilon. Optional
flags get bit2; bit0 is copied from auxiliary `+40`, clearing bit1 when set.
The ordinary branch can set bit0 from auxiliary `+41`.

If that special-branch victim is human, `Human::eParams_Avoidance` is populated
through ability ID storage `0x15984C8`. A draw below its `aimed_shot` parameter
multiplies the factor by a separate uniform[0.30,0.55] value and clears optional
effect bit0. This is damage reduction, not a trajectory miss. Actual skill-level
selection is a separate dependency. Impact and health invoke this helper separately,
so they can consume different draws; they must not be collapsed into one sampled
factor when modelling the native sequence.

The ability's parameter constructor `0x9762B0` initializes all five floats to0.
Binder `0x975AD0` establishes:

| Parameter offset | Name | Reviewed consumer |
|---|---|---|
| `+8` | drop_weapon | human event path `0x936C60`, partial |
| `+C` | knockdown_melee | melee probability path `0x95D820`, partial |
| `+10` | aimed_shot | projectile factor `0x748B90` |
| `+14` | explosive | clears throw/knockdown flag in `0x741710` |
| `+18` | fire | heat/fire application `0xA75CD0`, partial |

`0x975890` parses named `level` records, stride `0x80`: tag `+0`, parameter
object `+60`. Ability `+68/+70/+78` is the level vector, `+80` the parser's
current record. `0x975CD0` copies the selected record's five values into the
output parameters and returns success only with a selected record. Installed
`set/ability/avoidance.set` supplies levels1..8, including aimed_shot 0.15/0.20/
0.25/0.30/0.35 at levels4..8; earlier omitted fields retain defaults. Its commented
example is not an active level.

Pierce health `0x7428B0` starts with `definition+1A4 * projectile+10C`.
For positive base damage and context hit-part-valid flag `0x2000000`, it obtains
armor absorption from `0x7D1E00`, using the part tag at context `+10 -> +30 -> +398`.
Positive absorption is multiplied by the victim's parameter-kind1 transform.
The result before source modifiers is:

```
factor * max(baseDamage - transformedArmorAbsorption, 0)
```

Projectile source `+B0` applies actor parameter-kind5. It may additionally apply
kind0x15 when effect bit3 is clear and both source and victim satisfy `0x7EF6A0`.
The outer executor has another provenance-dependent kind0x15 path; these calls
are not interchangeable without resolving the different source/provenance gates.
Pierce impact `0x742870` is its separately sampled factor times projectile `+10C`
times definition `+198`.

`0x7D1E00` walks the inventory extender's holder vector `+88/+90` and returns
`+770` from the **first** eligible `Inventory::eHolderArmor`; otherwise0. Predicate
`0x7D1B70` requires holder slot6 to return false, a nonempty `+758/+760` volume-tag
vector (inside the `+440` tag set), hit-part membership in that set, and the first item's
definition tags `+11E0/+11E8` to contain `armor`. Initializer `0x48560` proves
that name for global tag `0x1571040`. `0x7D0FF0` returns the selected item handle
at slot3-result `+40`, or a shared empty handle. Armor is not summed over all
eligible holders in this lookup.

Installed `properties/human.ext` supplies helmet absorption20 and body/accessory
absorption25, along with their different volume masks. The numeric field binding
and holder lifecycle remain a follow-up; these configured values alone do not
prove an item is equipped or meets the `armor` tag predicate.

## Blast conversion and reaction events

For actor victims, Blast `0x742AE0` samples `0xA6DF30(context+F0)`, multiplies
by125 and invokes victim parameter-kind3. `0xA6DF30` samples the global curve
at `0x159C7B0`. Parser `0xA6D560` binds it to `blast_to_health_scale`.
Installed `set/damage.set` confirms that purpose and supplies knots from0/0 to
10000/0.9; for example blast1 gives0.2, hence25 before subsequent modifiers.

For non-actor targets the helper converts a tagged scalar from kind2 to kind1.
`0xA6D310` constructs it; `0xA6D380` multiplies by source energy factor and divides
by target energy factor. Kind0 is `energy_per_projective`, kind1
`energy_per_health`, kind2 `energy_per_blastwave`, kind6 uses factor1. Installed
values are1,0.025,5 respectively. The parser also loads `projective_health_scale`
and `brinell_hardness_multiplier`; reviewing their parser is not coverage of all
consumers. Blast impact `0x742AC0` returns context `+F0` and clears effect bit1.

Outer health executor `0x741710` separately obtains impact, coefficient and health
damage, performs further role/provenance/cover/distance gates, invokes actor injury
slot61, then subtracts effective health through `0x925190`. Even a zero permitted
damage result can reach injury notification. The earlier health pass covers that
signal-before-setter ordering; the remaining outer gates are still partial.

The common impulse curve can set context direction `+100` and scalar `+F0`, with
validity flags `0xA0000`, and enable throw/knockdown effect bit0. It requires a
nonempty table and excludes chassis mode1; it runs when impact exceeds the first
curve x or bit0 was already set. A human `explosive` Avoidance draw may then clear
bit0. Impact below `explosive_treshold` clears bit1.

The seven effect slots in the installed human interaction definition are:

| Index | Event |
|---|---|
| 0 | hit-scream |
| 1 | hit-light |
| 2 | hit-heavy |
| 3 | die |
| 4 | hit-explosion |
| 5 | throw-off |
| 6 | throw-off-and-die |

In the ordinary reaction path damage/maxHealth >0.2 selects heavy; a smaller
positive ratio above epsilon selects light. Crossing from positive health to
nonpositive adds die or throw-off-and-die according to bit0. Otherwise it adds
hit-scream and optionally throw-off. The explosion override selects slot4 when
the victim is not already finally dead, bit1 remains set and ratio>6; for an
already finally dead victim it instead uses newHealth < -12*maxHealth. Empty tags
are removed, then `0x76E0B0` dispatches the selected events. Event execution,
animation/knockdown consequences and replication details remain to be traced.

## Vehicle calculations: preserve floating-point returns

Vehicle Pierce `0x745DF0` and Blast `0x746970` decompile as `void`, but both return
floats in XMM0. Pierce constructs an armor interaction with incident-angle floor
0.01, obtains penetration from projectile definition/range and projectile `+10C`,
and compares it with resolved armor thickness. Its penetrating result is
`(penetration - thickness) * componentScale + definition+264`; componentScale
comes from `0x894720(victim+BB0, hit component)`. Below-thickness penetration
returns0: XMM6 is zeroed at `0x745EFF`, the comparison jumps to its return path
at `0x7467B5`, and `0x7468A8` copies it into XMM0. The nested armor helpers
`0x7E72E0`, `0x7E7560`, `0x7E7800` and `0x7E7CB0` remain open.

Vehicle Blast converts context `+F0` from kind2 to energy kind6 and subtracts
`0x7E8030`'s threshold. **A negative difference is returned unchanged**, not
clamped to0. Nonnegative differences are multiplied by victim `+BD0`. Outer
`0x745960` only applies positive results to component damage `0x893550` and injury,
then handles component thresholds and notifications. The distinction matters if
reusing this helper as an estimator rather than reproducing its caller's gate.

## Inventory load supplies stamina's weight input

`0x7CCD50` selects extender collection `+28`, with shared empty fallback;
`0x7CCD20` returns that inventory extender's `+A0` box, again with shared fallback.
Thus cache index2 is inventory, complementing index0 vitality from the prior pass.

`0x71DD90` recomputes entity `+A2C` via `0x7DA290`, sets entity `+1C0` bit10 if
the float changed, then follows entity `+560 -> +8` and repeats for linked parents.
The inventory total sums item pointers in box `+10/+18`; `0x7C90B0` supplies each
weight. When an item wraps an entity with an inventory, it returns entity `+A30`
plus that entity's accumulated `+A2C`, accounting for nested contents. Otherwise
it resolves definition weight through item handle `+40` and `0x7E2420`.
That `0x20`-byte handle has primary definition/count at `+0/+8` and secondary
definition/count at `+10/+18`; the latter contribution is optional. The formula
is `(primaryDefinition.weight + secondaryCount*secondaryDefinition.weight) *
primaryCount`, using definition float `+10`, and returns0 for absent/zero-count
primary entries. The meaning and lifecycle of secondary contents remain open.
Constructor zero and clone copy of `+A2C` were also found. The literal-displacement
scan is a lead inventory, not proof that aliased/bulk writes cannot exist.

The installed human vitality definition supplies weight knots0/25/50/75 kg.
Stay/walk/slow stamina rates are3/3/2.5/2; normal2/2/1.5/1; fast -3 at every knot.
Fast movement has lower/upper hysteresis1/10, Normal0/0 and Slow-1/-1. These
configure the stamina/cap algorithm already mapped in `0x9253B0/0x9263B0`.
In the rate parser, `stay` reaches the stationary default branch; the enum's
named stationary sentinel is `none`, so do not infer a new enum value from this
resource spelling.

The same definition overrides raw health regeneration to1 (constructor default0.5),
ordinary head/face damage to1.3 and limbs to0.8. `DamageTPC` labels third-person/
direct control and raises head/face to1.75. `0x83FB90` is the chassis `+18 !=0`
predicate used in coefficient selection. Hardcore overlays differ; active resource
resolution and chassis control-mode producers remain open.

## Comparison and verification

Eight selected game/editor pairs agree on the reviewed descriptor, formula or
parameter-binding contracts: signal parse `A5C9C0/8E5250`, action parse
`74F210/5CBF40`, action execute `74F220/5CBF50`, Pierce impact `742870/5BDE90`,
Pierce health `7428B0/5BDF70`, Blast health `742AE0/5BE290`, projectile factor
`748B90/5C4C60`, Avoidance binding `975AD0/7FDAA0`. The editor retains additional
context/entity assertions; these are bounded comparisons, not byte equivalence
or proof that all downstream behavior matches.

The [manifest](../artifacts/build-25492057/signals-damage-map.json) records 61
reviewed/partially reviewed game entries, eight editor comparisons, 76 field
contracts, signal enums in both binaries and seven hashed archive members.
Structural [validation](../artifacts/build-25492057/signals-damage-validation.json)
passes against both pinned executable hashes, 6,678 instruction locations, 24 RTTI
tables, 11 constants and 13 byte sequences. Independent LLVM disassembly matches
Ghidra instruction starts for eight selected functions (1,430 instructions).
These checks protect evidence identity and instruction boundaries; they do not
automatically prove the interpretation or exhaust the exports' discovery leads.

## Remaining work from this pass

- Complete signal area/registration lifetime and sound source lifetime/cancellation.
- Trace armor holder field parsers, material/penetration helpers and vehicle components.
- Resolve projectile field producers, source-parameter labels and outer cover/distance gates.
- Trace configured hit/throw/death events into native action effects and animation states.
- Complete ability level selection, fire/melee/weapon-drop consumers and inventory lifetime.
- Extend game/editor comparisons as newly reviewed downstream contracts warrant.

The static frontier remains productive; this pass is not an exhaustion claim.
