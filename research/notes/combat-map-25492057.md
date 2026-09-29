# Target choice, weapon controls and accuracy — build 25492057

Reviewed pass following the [perception pass](perception-map-25492057.md).
All addresses are game RVAs unless marked editor. This is static evidence,
not a callable SDK. The [frontier](static-frontier-25492057.md) remains open.

## Target eligibility and ordering

`0x912AE0` requires position age ≤20 duration units, or ≤10 for control mode 1;
subject `+0x70` must be nonzero and due, `+0x6C` must be clear. The dynamically
registered `burning` actor flag rejects the target (initializer `0x705F0`). Human
owners also have a conditional rejection of targets with entity `+0x1C0` bit 28,
a linked actor and brain flag 17 clear. That linked-type test remains open.

For a recognized subject (`+0x14` bit 0), return bit 1. Otherwise require known
position, an actor pointer, and either bit 10 or a weapon observation less than
three duration units old. These tests do not themselves establish line of fire.

The human picker `0x9437A0` searches sensor category 1, rejects unsuitable and
`broken` component state, then requires relation enemy unless control is AI.
A nonnull explicit subject reference at brain `+0x3D0` takes priority. It ranks
candidates with `0x9136E0`, tries `Human::eWeaponEvaluator`, returns the first
candidate with a selected item, and falls back to the first ranked subject.
Thus selecting a subject does not guarantee a fireable target.

`0x9136E0` first checks whether any candidate is strictly within brain `+0x14C`
using 3D distance to last-known position. If so, it drops all farther candidates
before ranking. It selects a rule profile with `0x919630`, applies `0x918C70`,
and writes a descending rank byte at `subject+0x11` while rebuilding the list.
With no matching selector the output is empty; this is not an implicit nearest
fallback.

`0x913CB0` loads `/set/target/select.set`; `0x919630` selects the first matching
profile, using unit definition tags/current weapon tags or an alternate actor
name at `+0x1180`. The installed defaults include human bullet, AP, grenade and
fallback profiles, plus vehicle, cannon and airborne profiles. The human bullet
profile prioritizes specified nearby vehicles, close threats and attacking
subjects, followed by several human weapon classes and distance. Names in the
resource are not sufficient to name the underlying field tests; those are being
traced individually.

Ranking groups contain ignore filters, inclusion filters and a sort specification.
`0x915850` ANDs each filter's terms, applies per-term inversion, and caches results
in a 24-byte candidate record (`+8` true bits, `+C` evaluated bits). Terms have
stride `0x3A8` and use dynamic bit indices masked to 0..31. `0x915A70` removes
ignored subjects from the remaining input and transfers matching included
subjects; `0x915D80` sorts when configured. The insertion comparator `0x916B90`
orders configured predicate groups lexicographically, true before false, then
ascending numeric key. This is not a weighted sum. The outer sorter `0x9182E0`
also has partition/heap branches; equivalent tie handling is not yet reviewed.

`0x919C50` binds the predicate names to IDs, supports `not`, and deduplicates
terms globally. Boolean cache indices start at 2, leaving 0/1 for numeric keys.
The runtime evaluator `0x91A0B0` implements:

| Term | Concrete test |
|---|---|
| class | Recognized actor's +0xDE8 classification; otherwise known-position fallback named `unknown` by initializer 0x66320 |
| entity | Recognized actor's name at +0xF0 |
| weapon | Class weapon tags, or recognized current weapon tags, or remembered weapon +0x40 tags |
| attacking | Subject +0x14 bit 8 |
| threating | Subject +0x14 bit 7; spelling is the engine's |
| invisible | NOT subject +0x10 bit 29 |
| out_of_range | NOT subject +0x10 bit 18 |
| in_ik_range | **False in this build**: named ID 7 goes to the default false branch |
| in_range | Strict 3D distance to last-known position less than configured distance |

Numeric keys (`0x91A380/0x91A4B0`) are `time_to_aim` and `distance`. The former
asks brain slot20 for a weapon, then aimer `0x81F030` for an estimate, returning
FLT_MAX when no weapon exists; the latter is squared 3D distance. The returned
value travels in XMM0, despite a misleading integer decompiler signature.

An unusual cross-group behavior is present in `0x918C70/0x915A70`: the temporary
output vector survives between groups. An explicit inclusion filter appends to
it; an empty inclusion filter replaces it with the remaining candidates. Each
group then appends that entire temporary vector to the final output. Thus a later
explicit-inclusion group can re-emit earlier candidates. This is an observed
static path, not a claim about how often shipped configurations encounter it.

## Weapon/ammunition suitability is a separate rule engine

`0x9144F0` loads `/set/target/ammo.set`. Target-class entries have stride 0x80,
optional cover condition (`1=in_bush`, `2=in_cover`), and a vector of 0x398-byte
weapon/ammunition rules. The loader assigns decreasing priority by source order
at rule `+0x390`; it appends a priority -1 fallback. Rule flags at `+0x394`:

| Bit | Parser name |
|---:|---|
| 0 | ground |
| 1 | volume, with volume tag at +0x330 |
| 2 | enable_for_ai |
| 3 | enable_for_user |
| 4 | enable_for_human_user |
| 5 | check_grenade |
| 6 | ready_for_shot |
| 7 | charging |

Default flags are 0x1C; `disable` clears that mask. `0x914180` matches target
class, cover state and ammunition tags, then applies those policy/readiness
gates. `check_grenade` rejects targets nearer than `0x7E0AC0`'s result and requires
a cover-facing predicate when an actor exists. `volume` requires a matching
unbroken geometry volume. Caller and parser evidence establish these exact
checks; they are not a generic full safety/ballistics simulation.

`Human::eWeaponEvaluator` constructor `0x957B10`, setup `0x957C00`, evaluation
`0x957C70`, inventory scan `0x957F70`, item check `0x958010`:

- Context stores owner +0x20, inventory view +0x28, current item +0x30,
  human brain +0x38, subject +0x40. Selected item is +0x10; best priority +8.
- It tries current/inventory items in multiple passes, including a special
  rocket-launcher pass for actor type bit 3. Item tags are proven by initializers:
  `0x1575860=rocketlauncher`, `0x1575C80=weapon`, `0x1575380=missile`,
  `0x1576820=visor`.
- It checks applicable breed ability rules, ammunition availability, duplicate
  item definitions, policy priority and range. Higher priority replaces the
  prior item; ties keep the earlier result.
- The final fallback relaxes range/target checks. Resulting item choice must not
  be equated with immediate permission to fire.

`0x7CCD20` follows actor `+0x578`, object `+0x28`, then inventory `+0xA0`, with
an empty default fallback. `0x7CCDB0` gets the named hand/holder interface from
the same owner object. Precise inventory owner/holder types remain open.

`0x806850`, used for missile checks, **creates or mutates a cached synthetic
weapon place** at weaponry +0x78 and installs the proposed item. It is not a
read-only lookup. `0x9134C0` separately tests strict 3D range; `0x912D60` requires
an actor and visible or <3-old observation, then resolves ground/volume targets.

## Weaponry and accuracy inputs

The two weaponry vectors have different roles: +0x18..0x20 contains configured
place definitions; +0x30..0x38 contains active runtime places. Primary-place
getter `0x806340` uses selected definition index +0x4C and its +0x2B8 runtime
link, falling back to the first active place. Selection setter `0x8064D0` sends
actor events 0x1D and 0x1C after a change. `0x807A60` clears active targets only
on places whose +0x208 mode equals its argument.

Runtime places are `Actor::eWeapon` (constructor `0x809500`, size 0x268),
derived from `Inventory::eHolderGeneric`. Definitions are the separate
`Actor::Weapon::ePlace` (constructor `0x810480`, size 0xD38).

Runtime place inputs include owner +0x90, weapon +0x98, aimer +0xA8,
ammunition/charger +0xB0, shooter +0xB8, definition +0xC0. Target setup `0x80A370` changes
mode, burst/shot limits and target state; clear `0x80A730` releases target state,
resets aim/fire modes, stops the shooter and emits event 0x26.

Accuracy modifier `Helper::eModifierParamaterAccuracy` applies to active places
matching a definition-name pattern (`0xA0F040`); `0xA0F170` calls shooter slot 4
with its float +0x28. For `eBulletShooter`, slot 4 `0x8188A0` writes +0x3C4 and
slot 5 `0x8188C0` writes +0x3C0; both force profile refresh `0x81B510`. Editor
slots `0x69B800/0x69B820` preserve those offsets and SSE float arguments.

Refresh selects an ammunition-tag profile from weapon +0x1600, copies its spread
settings, then applies the product of those two accuracy scales. `0x817090` copies from
profile offsets +0x10..0x74 into shooter+0x28..0x8C. `0x817140` divides both
curve Y values, base spread radius and burst width by the scale, and divides
burst delta by `scale^0.3` (math helper `0xF50340`, reviewed power-function
special cases and logarithm/exponential path). With a nonempty normalized curve, scaling both its Y
values and base radius compounds to a squared scale factor. An additional
actor factor is applied for actor type bit 3, but its helper `0x863F10` returns
constant 1 in this build. The non-forced refresh can retain cached values when ammunition tags match.

`eBulletShooter` slot 8 `0x819AC0` (editor `0x69CA20`) computes spread radius:

1. Place flag +0x264 bit 3 gives zero.
2. Use either the normalized-distance `RadiusFunction` times base radius, or
   `RadiusTable` evaluated in world distance times spatial scale.
3. With place bit 4, divide by weapon state +0x38 (`accuracy`) when present.
4. With weaponry +0x70 bit 1, divide by `ManualAccuracy` (+0x50) or
   `ManualAccuracyZoom` (+0x54), selected by place bit 6.
5. Divide by `1 + (place_definition.motion_factor - 1) * weapon.AccuracyByMotionK`.

Table helper `0x3FB290` linearly interpolates adjacent points and holds the
first/last Y outside the table; empty tables return zero. Larger divisors reduce
this radius. No clamp should be invented for externally supplied zero/negative values.

Slot 7 `0x819C20` evaluates that radius at a target point, multiplies a zeroing
factor from `0x818970`, then divides by the current `BurstAccuracy` factor
clamped to at least 0.01. `0x818970` reads weapon +0x1630..0x1638, indexed by
validated shooter zeroing state at +0x3C8/+0x3E0; it is not an angle-spread helper.
After-shot handler `0x81AF10` resets this zeroing count if muzzle/target positions
moved outside the thresholds, increments it, and stores both positions. It also
consumes ammunition, dispatches firing effects and increments total/burst counts.

The firing controller `0x817F70` additionally gates by ammunition, aimer state,
place readiness and animation transitions. Slot11 `0x8184D0` generates a shot
through `0x818A10` and creates it through `0x81A470`; the latter allocates a
0x1C8-byte projectile with constructor `0x8321F0`, installs weapon/ammunition
references, muzzle position, sampled velocity and gravity input. Synchronized
projectiles have a separate multiplayer command path. Projectile collision and
damage are not established by this creation path.

Sampling `0x818A10` uses a per-context random stream (`0x554B50`), with integer
update `state = state*0x7FF8A3ED + 0x2AA01D31` and an incremented draw counter.
Its ordinary radial branch raises a uniform sample times the reciprocal burst
accuracy to `Power/2`, and draws a separate azimuth in 0..2π. Burst drift,
elliptical XY shaping, vertical adjustment and ballistic correction add further
branches. The whole sampler is only partially reviewed; radius alone is not the
complete hit-probability model.

## Property registration evidence

Some property initializer stubs are not recognized as functions by Ghidra and
have no unwind record. An exact reviewed 32-byte instruction shape passes
(name, ID, registry, entry) to registrar `0x66A940` (editor `0x4DFE80`). The
registrar stores name at entry+0, ID at +8, and appends the entry to a linked
registry using +0x10. `catalog_property_tokens.py` captures 1,229 game and 1,254
editor instances, including raw stub bytes. This is one shape, not all properties.

Selected registry/parser connections:

| Registry | Parser | Property/field evidence |
|---|---|---|
| 0x157FD10 | 0x807CF0 | Current place, ManualAccuracy +0x50, zoom +0x54, restore-IK time +0x58 |
| 0x157FF30 | 0x80F3C0 | SpreadRadius +0x40, XY ratio +0x44, Y adjustment +0x48, power +0x4C, burst accuracy +0x50 vector, recovery +0x68, delta +0x6C, count +0x70, width +0x74, range curves +0x10/+0x28 |
| 0x1580E48 | 0x828610 | Weapon state: accuracy +0x38, sway +0x3C, throw_off +0x40, throw_off_always +0x41 |
| 0x1580E40 | 0x8299E0 | Weapon: AccuracyByMotionK +0x15F0, SyncedProjectiles +0x15F4, automatic/ammo/reload flags +0x15F8, animation states and recoil/sway parameters |

Spread property offsets in the parser are relative to the profile object,
not the bullet shooter's outer object. Mapping the copy establishes that
relationship; the two domains must not be mixed. Distances/angles are converted
by engine scale factors. Burst accuracy inputs are divided by 100 on load.

`0x80F8C0` parses motion accuracy: maximum linear/angular speed, associated
accuracy values (asserted ≤1), disorder/order times, saved value, previous
position and direction. Runtime update `0x80AA10` passes definition+0xC90 and
the aimer's bone to `0x80FF40`. It uses bone position +0xCC and direction +0xA8,
refreshing a dirty transform first. These are not the actor's velocity fields.

The motion block is 0xA0 bytes: configuration +0..0x14, linear history +0x18,
angular history +0x48, last angular sample counter +0x78, current factor +0x7C,
previous position +0x80, previous direction +0x8C, initialized byte +0x98.
Linear speed is displacement length / quant-counter step. Angular samples use
acos(clamp(previous_direction dot direction, -1, 1)) divided by elapsed duration
times 1000, and are taken after at least 0.1 duration units. Both histories use
`0x1D7D30`: a trapezoidal time average followed by expiry of old samples.

For normal finite configuration values, let L/W be the two averages capped at
their configured maximum speeds, with tiny values treated as zero. The desired
factor is `min(1+(linear_accuracy-1)*L/maxL,
1+(angular_accuracy-1)*W/maxW)`; a zero maximum contributes ratio zero. Set
`a=min(linear_accuracy,angular_accuracy)`. Each update approaches that desired
factor by at most `(1-a)*step/order_time` upward or
`(1-a)*step/disorder_time` downward. Near rest and factor 1, it snaps to 1.
The initial sample sets factor 1. No general validation of negative settings is
implied. Aimer `0x81E160` also compares this factor with weapon+0x15EC before
choosing its ready-state value.

## Order lifecycle and follow-up perception findings

Base order update `0x901750` uses flags at +0x18: bit16 begun, bit17 finished,
bit20 retry delay. It invokes slot19 with phase0 once, then phase1. Nonzero
result calls end slot4; result2 with a remaining low-16-bit retry count retries
after `new_delay = 2*old_delay + random(0.2,0.8)*time_scale`. Delay decrements by
quant step global 0x1556E4C. Otherwise it marks finished. Result is mirrored to
an optional handle at order+0x10, handle+8.

End `0x9018D0` calls phase2 only when begun and bit18 requests cleanup; ends and
destroys child order +0x30, then clears begun/bit19. Runtime target/cover ownership
must follow this lifecycle. Attack execution `0x8EDFD0` saves current place,
updates child aiming/action orders and restores the selected place on cleanup;
additional internal attack states remain to be traced. WeaponAimTarget
`0x8D7AC0` sets aim mode 3 and checks a deadline and chassis activity before
reporting completion. AttackBasic slot19 is an immediate result1 default.

The perception accumulator at subject+0x38 is now identified as **unmasking**.
Actor flag index global 0x157DFD8 is registered as `unmask` by 0x57DA0. The
increment `0x891C00` sums matching conditional rules from `/set/unmasking.set`.
For each rule it gives 100 inside instant distance, zero beyond maximum range,
otherwise interpolates between `100*elapsed_counter/time_min` and
`100*elapsed_counter/time_max` over its distance interval. Rule inversion is
stored in the term object. Threshold action `0x878830` sets subject relationship
bit5 and adds the `unmasked` tag to the observed actor (`0x48FB0` initializer).
Rule loader `0x891D90` scales range/instant distances and time bounds.

Unmask predicates now have concrete reads:

- `item` (`0x892FD0`): scan inventory by tag, require held/active-item predicate
  `0x7C97E0` or a linked entity with bit28 at +0x1C0.
- `skin` (`0x893190`): human appearance reference +0x14C0, resolve through
  `0x704470`, then match the resolved entity tags at +0x1D0.
- `speed` (`0x8931E0`): strict velocity-vector magnitude > configured threshold,
  using entity velocity helper `0x727070`.
- `pose` (`0x893150`): active Fsm byte +0x670; match current state (+0x9F0)
  tags at state+0x90.
- `examine` (`0x892F70`): lookup an examined entity via manager `0x159E218` /
  `0xAF88A0`, optionally match its tags.
- `and`/`or` apply each nested term's inversion; empty AND is true, empty OR false.

Profiles must be attached to the sensor and the target must have the `unmask`
flag for this accumulator to run. This is not general-purpose suppression.

`0x875EF0` closes reveal table indices: 0 is `visible`, 1 is `attack`.
`0x9036A0` records brain +0x120=now only if its own sensor currently sees the
notifying observer, so the visibility notification is reciprocal, not an
unconditional alarm or forced target assignment.

The per-player observation manager is **eReconBase**, named by `0x88A9D0`'s
unknown-zone diagnostic. It stores 0x80-byte zones, deduplicates by entity or
exact position, retains observation timestamps/uncertainty and reference counts.
`0x88BE20` selects it by actor player +0xDA8; `0x88BCC0` allocates per-player
entries. Both sensors and a brain's chosen target hold registrations.

## Open work and verification

The [manifest](../artifacts/build-25492057/combat-map.json) identifies reviewed
contracts separately from exported leads. The companion validation checks
binary identity, entries, instruction locations, selected token bytes, RTTI,
tables and resource hashes. It does not prove ABI, lifetime or semantic completeness.

Validation passed for 145 game and eight editor exports, 122 paired/single method
entries, 449 instruction locations, five RTTI tables, two name tables, five
constants, 192 byte sequences and 24 resource members. The byte sequences include
95 matching property-token registrations in each binary. Independent LLVM
disassembly matched 593 instruction starts across motion update, target predicate,
order update and editor spread-radius functions. Three property scanner tests pass.

Open work includes full sorter branch equivalence, weapon placement lifetimes,
ballistic/sampler subbranches, detailed attack states, unmask predicate helper
lifetimes, entity classification producers and projectile collision/damage.
Exported functions alone are discovery; only the contracts described here have
semantic status. The global static investigation continues in the frontier.
