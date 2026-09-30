# Native actor controls and squad-status inputs — build 25492057

This pass maps eight native commands: `actor_fire`, `actor_recon`,
`actor_to_cover`, `wait`, `actor_state`, `ables`, `player` and
`actor_component`. It also follows their order constructors, text parsers,
weapon/ammunition tests and the producer of the actor status word consumed by
squad coordination. It does not exhaust the native command registry or the
broader AI/entity investigation.

The manifest contains **105 paired contracts and one game-only binding helper**
(211 entries), **116 fields**, **26 RTTI tables**, and **13 named enum tables per
binary**. Independent LLVM decoding agrees with **19,167 Ghidra instruction
starts**, after normalizing standalone LOCK prefixes. Another58 raw regions
establish named globals, tag-set construction and RNG normalization. Structural
validation passed for both frozen binaries; no runtime experiment was performed.

Artifacts: [manifest](../artifacts/build-25492057/actor-controls-map.json),
[validation](../artifacts/build-25492057/actor-controls-validation.json).
All RVAs, sizes and offsets below are hexadecimal unless marked otherwise;
bit indices and virtual slots are decimal. These are evidence-backed contracts,
not callable ABI declarations.

## Entry points and defaults

| Command | Game / GEM execute | Allocation | Main defaults |
|---|---|---|---|
| actor_fire | 199F60 / 121DB0 | 148 | attack=-1; burst=smart; empty waypoint/place; zero time/interval; flagsfalse |
| actor_recon | 19A2D0 / 122120 | A8 | empty waypoint, radius0 |
| actor_to_cover | 19D0E0 / 120C30 | 98 | empty names, best mode, density50% |
| wait | 1997D0 / 121610 | 48 | time mean/spread0 |
| actor_state | 99F940 / 829320 | 170 | enum-1/emptytext means unchanged; speed integers0 mean unchanged |
| ables | 9A0460 / 829E00 | 48 | add/remove masks0 |
| player | 9A0DE0 / 82A6E0 | 48 | playerFF unset; operation=set0 |
| actor_component | 9A1480 / 82ACA0 | A8 | empty component, state-1 |

All inherit selector10 and drop38, initiallyzero. Drop bits0/1/2 are named
`orders`, `sensor`, `senseless`; their execution gate is in the
[modifier pass](modifier-map-25492057.md). `actor_to_cover` additionally overrides
batch slot7 at19CE70/1209E0. Its per-batch scratch state must be distinguished from
per-actor execution slot16.

## Firing: an order or a direct weapon operation

`actor_fire.attack` is position0/enemies1; the default-1 tries a waypoint first
and only falls back to entities if the location helper returnsfalse. A found
waypoint returns **true even if only_weapon cannot resolve a weapon place**.
That case does not fall back to entity targeting.

Location helper1A3610/12C480 uses waypoint XYZ20 and height offset11C. When
`deviate`140 istrue, its radius is `(1-U²) * waypoint118`; otherwise radiuszero.
It always draws an azimuth, even with radiuszero. The trigonometric helpers are
sine and cosine, not interchangeable: with polar angleπ/2 and azimuth2πU,

```
X = waypointX + sin(polar) * radius * cos(azimuth)
Y = waypointY + sin(polar) * radius * sin(azimuth)
Z = waypointZ + waypoint11C + cos(polar) * radius
```

Normal execution constructs WeaponFire and submits it with mode1. With
`only_weapon`141, it resolves a named definition and follows2B8 to its runtime
place, or uses the primary-place accessor806340/6889B0 for an empty name. That
accessor tries weaponry4C's definition index and then the first runtime place.
It has no local upper-index guard. Direct fire uses usage3 (`USAGE_ORDER`, named
by the GEM assertion), automatic count-1, and sets place264bit3 from `cheating`142.

UI burst values smart0/once1/short2/long3 become native4/1/2/3. Direct fire
80A370/68CC80 requires a weapon definition, place-definitionD30bit0, a false
placer weapon-block predicate and valid aiming bones. The placer predicate's
slotbit20 is **Sealed**, as established in the placement pass. Burst1 fires one;
2/3 use short/long profiles;4 selects long, short or one from target distance and
weapon range/ratio;5 gives its special zero endpoint. Automatic/Automatic_AI are
weapon15F8bits1/2. Sampled counts use owner parameter6 and a minimumone before
conversion when those flags apply. The endpoint is relative to shooterB8+10
unless an explicit nonnegative count was supplied.

Entity helper1A3250/12C0D0 creates an **AttackBySelector** order in normal mode.
Direct mode instead collects enemies selectorA8 immediately, with nullcontext,
retains only entity38bit6 entries in shared scratch, and chooses high24 RNG
modulo candidatecount. A single candidate still consumes a draw. Scratch end is
cleared before target resolution; no local save/restore protects reentrancy.

AttackBySelector's selector38 is an owned clone. Interval40/44 and deadline48/4C
control repetition; burst50 and cheating54 are forwarded to child WeaponFire.
A zero interval pair defaults to the currently selected weapon profile's
`recharge_time`18 at phase0. Each valid selection creates a child and schedules
the next deadline. Empty selection or an invalid resolved target retries after
0.5seconds. The duration deadline is checked **after an active child finishes**.

Execute8D8560/75EAD0 resolves an active weapon before phase handling. Missing
weapon returns2, including phase2; therefore the later phase2 clearing of
brain39Cbit26 is not reached in that local path. Valid target creation sets
that bit and order18bit18. Load/save include selector, intervals, deadlines and
burst, but omit cheating54; the standalone factory also does not write54.
Outer allocation/load invariants remain a static lead; this is not a demonstrated
runtime uninitialized-value defect.

## Recon, cover and waiting

`actor_recon` calls SensorDD0 slot18, resolved through RTTI to8783F0/6FC090.
A valid waypoint supplies its position20/radius118. Otherwise a positive command
radiusA0 uses actor position74 and multiplies radius by the spatial unit.
The sensor adds or reuses a position-only subject, selecting the oldest when
its configured count limit requires reuse; it writes XYZ, radius, timestamp
and flags. This joins the [perception map](perception-map-25492057.md).

Cover batch execution clears claimed XY positions, computes bounds around all
matching named-cover records and expands them by cover spacing. The actor path
has two distinct searches:

- Empty names require a human. Center is context14; radius is context1C when
  at least1, otherwise `5 * spatialunit`. Search119 istrue and the first result
  is taken.
- Named search uses the box center and half diagonal, capped at **300 internal
  units**, with search124 flags0x1010000. `best`0 takes the first result,
  `random`1 draws modulo count, and `density`2 uses successive geometric masses
  p,(1-p)p,… with p=clamp(density,1,100)/100, clamping the resulting rank.

Named candidates must have a matching associated named-cover record and be at
least the cover-spacing distance from previously claimed positions in this batch.
Rejected candidates are erased and selection repeats. Success copies the88-byte
cover descriptor into MoveToCover and submitsmode1. Pointer lifetime within that
copy joins the earlier named-cover and resident-cover maps. Invalid modes outside
the authored0..2 table are not locally guarded.

`wait` samples `mean + (2U-1)*spread`; absolute spread below1e-5 skips RNG.
It writes an integer tick duration to Wait38 and clears3C. No nonnegative clamp
is present. Four normalization globals per binary are statically initialized to
1/2²⁴; the manifest preserves their code, constants and initializer-table references.

## State and ownership writes

`actor_state` requires both actor and brain. Control, fire/movement modes,
movement type and speed occupy brain398 bit groups. Speed mode resets override118;
`speed_kph` then writes a converted integer speed there. Positive `max_speed_kph`
changes the chassis maximum; a negative value restores it from the breed/template
chassis. Conversion1547DAC/13B11EC is produced by mutable spatial/length/time
units, not an assumed literal metres-per-tick ratio. Forward/backward set chassis
3C0 disablebits6/7 when a configured value is not1.

AI-move copies mode5C and enable_on60 to brain1E0/1E4 and publishes contextmessage8
with boolean(mode==1). Human-only policy writes no_advance520, no_retreat521,
advance/retreat ratios524/528, and follow_commander39Cbit27. An unmasking name uses
an exact case-sensitive lookup and writes its borrowed profile pointer ornull to
sensor328. `make_enemies_forget` invokes entity delivery at198 with mask3.

Weapon preparation and cannon state append to a Complex order when the relevant
game session gate permits. GEM's paired gates B26000/B26010 are constantfalse;
game gates require a session and selected global modes, then test opposite values
of sessionvirtual25. Their client/server role names remain unresolved. GEM also
asserts that a prepared human is not tagged `board`; game retains the scan without
an assertion or a branch blocking preparation. Replication uses command67 kind4.

Health/stamina text changes **both raw maximum and current** values. A relative
operation uses the old maximum, not the old current. The full signed text is
passed to the base0 integer parser: `+10` adds10, while `-10` subtracts parsed-10
and therefore also adds10. Bare text assigns its parsed result. The wrapper
accepts a numeric prefix, including octal/hex, and defaults tozero on no digits
or error0x22; trailing text is not rejected. Health increments actorBD8 and sends
BC9; stamina sendsBCE. No local clamp or clearing of death/revival state is shown.

Veterancy text accepts1..8 or exactliteral`0`, then the human setter checks the
available table length. It writes floatC in actor1178's record using the selected
Veterancy parameter, publishes events and refreshes actor summary with equipment
refreshfalse. The human breed pointer is1390 in both binaries, but **ability and
veterancy table offsets differ**: game528/560, GEM530/568. The named meaning of
record1178+C remains a lead rather than being assumed to be experience.

`ables` applies add40 then remove44 through the dynamic actor flag catalog;
remove wins a conflict. `player` exposes set0 and mask_set1. Actual-player set
calls actorvirtual58 only on a change, while mask_set writesDAC. These join the
[ownership map](ownership-map-25492057.md); masked and actual ownership differ.

`actor_component` finds the first matching component definition tag328 in actorB90
rows. States are work0/broken1/destroyed2. The setter clears row25bit0 and resets
its timer even for an unchanged state. A work→destroyed request passes through
broken first. Damage/events join the [armor map](armor-events-map-25492057.md).

## ActorB88: inputs available to squad logic

Producer7F3130/6751B0 zeroes the whole word when there is no brain. Otherwise it
updates the following fields and preserves unrelated bits. Equipment bits are
only recomputed when its second argument istrue.

| Bit(s), decimal | Source |
|---|---|
| 0,1 | brain39Cbits6,17 |
| 24 | Nonzero resident-cover kind |
| 25,26 | Fire mode other/open |
| 27,28 | Move mode other/free |
| 7 | Medic ability present, sufficient veterancy and positive parameter |
| 8 | Fortification ability with positive parameter |
| 29,30,31,32 | Vehicle BA9,BAA,chassisvirtual28,inverse Operatablebit8 |
| 35–38 | Human applicable veterancy; vehicle maximum occupied-actor value |
| 39,40 | Usable SupplyZoneExtender predicate / its inverse |
| 33 | chassis18 pointer nonnull |
| 34 | AI control mode0 |
| 9,10, equipment refresh only | morphine/flame_thrower inventory presence |
| 3,5 | Eligible rocketlauncher/at_rifle |
| 11–23 | Eligible assault rifle, shotgun, machine gun variants, SMG, auto/battle rifle, rifle, pistol and silenced variants; exact order in manifest |
| 6 | Eligible sniper weapon and SpecialAttack ability |
| 2,4 | Eligible flaregun/grenadelauncher |

Weapon categories use the definition's11E0 tag vector. They require more than a
tag match:80D780/68FDE0 computes eligibility from ammunition. For an AmmoLocked
(bit0, **not Automatic**) rocketlauncher with quantity unit1, it counts matching
loaded weapon items. Otherwise it requires a filling definition, gathers compatible
ammunition, and conditionally adds its own DC count. Hand/right-position weapons
omit that own-count addition. The gathering helper sums compatible filling58 from
**weapon-tagged filled items**, then loose item48 quantities. The single-tag set's
constructor proves `weapon`; it is not an assumed generic ammo tag. Full linked
inventory traversal through7D0610/7D0990 remains to be followed.

## Remaining static leads from this pass

The other62 native commands and concrete native conditions remain open. Within
these command paths, follow actor-flag catalog initialization/parsing, the named
veterancy-record binding, AttackBySelector allocation/load and scratch lifetime,
linked inventory enumeration, command67 receivers and remaining B88 producers.
The broader [frontier](static-frontier-25492057.md) also retains geometry,
component replacement, goal consumers, morale/suppression and worker/lifetime
questions. A verified entry or a named enum is not a claim that those dependencies
have been exhausted.
