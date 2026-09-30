# Ownership, relationship and observation flags — build 25492057

This pass follows player changes into brain, squad, sensor and weapon state,
connects target-class and threat flags to their producers, and checks the same
contracts in the game and editor. It adds **71 paired contracts, 65 fields,
20 RTTI tables and 30 property registrations**. Static AI investigation remains
active; full squad coordination and the other frontier items are not exhausted.

Machine evidence: [ownership-map.json](../artifacts/build-25492057/ownership-map.json)
and [validation](../artifacts/build-25492057/ownership-validation.json).
Offsets/RVAs below are hexadecimal; vtable slots and bit indices are decimal.
No game execution, binary patch, hook or runtime ABI test was performed.

## Changing Player is a state transition

`7EEEC0 / 6710C0`, actor slot58, accepts a pointer to the new player byte.
It first ensures a Recon object for a valid new player. That helper returns for
player>=18; it does **not** reject the setter or prevent the byte being stored.

When the **old** player at actor`DA8` is below18, the setter:

1. Calls brain slot19 with mask40, or clears the sensor when no brain exists.
2. Removes the actor from its current squad, if any.
3. Writes the new actual player, sends entity Delivery mask1 and actor event19.
4. Resets every occupied weapon place.

When the old player is invalid, those cleanup/broadcast operations are skipped.
Both paths set `DAC` to the new player, invoke actor slots19 and56, and may send
extender notification15. There is **no equality check** in this API: setting the
same player still resets and removes a normally owned actor. The `Player`
property parser explicitly checks for a changed byte before invoking it.

Only `eActor` and `eHuman` were found with an `eActor` base at primary offset0 in
the validated COL census (5,605 game and4,927 editor primary locator candidates).
Both use this setter and the same slots19/56. Human slot35 calls actor slot35
first, preserving the player-change event branch.

### What reset actually clears

Base brain reset follows **brain`38` → actor`DD0`**, then clears all four sensor
category vectors and pending candidates. Brain`10` is a secondary interface,
not the owner field. Subjects are detached, marked removed and released; a
retained subject object may survive with its raw entity/actor binding nulled.
The editor asserts that removal is outside a Quant worker.

Human reset releases/nulls `3C0`, reference-assigns null through `3B8` and `3C8`,
zeros `3DA` and reconciles Recon handle`3E4`. It does not explicitly clear `3D0`
in this body. Vehicle reset empties its `3A0..3A8` observation vector.

Weapon-place reset requires an occupied definition at`98`. It releases
reservation`214`, clears both target accessors and their position state,
notifies the aimer, and in mode1 clears shooter state and sends actor event26.
This connects ownership to actual firing state; it does not yet close every
place/profile/shooter replacement lifetime.

Actor event19 refreshes operability and two entity slot20 flags when its
initialization gates permit, then publishes `cmAEC_Props` (`BC8`). Slot56 runs
the global modifier manager and previously mapped crew/weapon modifiers.
The manager's concrete filters and modifier implementations remain static leads.

## Player, MaskPlayer and relationship flags

| Actor field | Named producer | Contract |
|---|---|---|
| `DA8` byte | `Player`, token62 | Actual ownership; setter58 has the side effects above |
| `DAA` ushort | `MID`, token5A | Numeric actor ID; see identity pass |
| `DAC` byte | `MaskPlayer`, token63 | Separate setter only stores a byte |
| `DE8` tag | `TargetClass`, token6B | Parsed class consumed by target policy |
| `E48` tag list | Optional second TargetClass value | Separate classification list |
| `DB4` bit8 | `Operatable`, token6D; native refresh | Gate used by relationships and sensor signal handling |

`A467B0 / 8CF360` compares two actors; `A46830 / 8CF3E0` accepts a viewer player
byte instead. Their essential target-side rule is:

```
missing or non-operatable target          -> neutral (0)
!ignoreMask && viewer == target.MaskPlayer -> ally (2)
viewer == target.Player                   -> ally (2)
either actual player outside 0..17         -> neutral (0)
otherwise                                 -> matrix[target.Player][viewer]
```

Thus a mask supplies an own-player equality shortcut. It does **not** replace
the target's actual player in the diplomacy matrix. The viewer's mask is not
used. Subject`14` bit5 supplies `ignoreMask`; learned unmasking sets it, and
ally information may propagate it.

Relationship refresh sets subject known0, apparent ally2, actual ally3 and
unusable4. Enemy1 additionally requires target brain`398` bit15. Nearby apparent
allies use a separate hysteresis marker at subject`10` bit20 and may notify the
observer brain. Current visibility is `10` bit29; auxiliary Recon registration
is `10` bit25. These are distinct states.

On entity Delivery mask1, the sensor first unregisters auxiliary Recon. If the
subject had been an actual ally, it clears that classification and visibility.
It refreshes the relationship only when the subject remains visible or the
observer is user-controlled. Death/retirement mask6 instead removes the subject.
This explains why merely writing `DAC` does not reproduce a complete ownership
transition.

### Operatable is also derived

Actor slot67 (`7F1A30 / 673B10`) requires a brain, an operability source, and no
vitality`174` low-two-bit state. The source is either a nonempty weaponry vector
when weaponry`49` selects it, or actor`DB4` bit12 otherwise.

`7F0130 / 672230` reconciles this result with `DB4` bit8 and sends event1A when
needed. It has a special blinding branch: it temporarily clears bit8 during the
event and restores it afterwards. The blinding predicate reads vitality`98`
and requires a nonzero deadline strictly later than the current time. The
`Operatable` property can force bit8 and queue the initial notification via bit20.
These paths should not be collapsed into a generic “alive” boolean.

## Threatening, attacking and injury observations

`877D20 / 6FB980`, sensor slot17, is the concrete event producer. It requires an
owner and owner Operatable bit8. For an eligible nonally subject:

| Signal | Subject flag effect |
|---|---|
| Shot2 | Updates observation; does not directly set threatening/attacking here |
| Hit3 or explosion5 | Sets threatening7/known-combat10 inside ThreatZone; adds attacking8 inside AttackZone |
| Injury4 | Sets7/8/9/10, bypassing both distance checks after earlier eligibility gates |
| Ally information8 | May propagate5/10 and newer observations; does not copy7/8/9 or their timestamp |

Injury creates a subject if necessary; other source-actor signal paths normally
look up an existing observation. Missing-source signals and death6 use the
separate position-only path for selected signal kinds. Direct injury publication
comes from the victim's damage path, with the attacker as source and the victim's
position as the event point.

The thresholds have exact named property bindings:

| Property | Token | Field | Constructor default |
|---|---|---|---|
| ThreatZone | 7F | sensor`308` | `15 * L` |
| AttackZone | 80 | sensor`30C` | `5 * L` |
| InformRange | 81 | sensor`350/354` | `20 * L` for each float |

`L` is the writable spatial scale, whose file-initial value is20. Parser inputs
are multiplied by it. The hit/explosion comparisons are strict `<`; neither the
numbers nor the constructor values imply a universal fixed distance in every mod.

The threat-producing event records subject`68` and sensor`2F0`. Newly setting
bits7/8/9 also clears decision marker`6C`. The sensor commit pass expires bits
using converted elapsed time since`68`: **strictly above0.4 clears injury9,
above1 clears attacking8, above2 clears threatening7**. These checks run while
threatening7 is set; known-combat10 is not cleared there. Expiry takes effect on
the next commit, rather than at an independently scheduled instant.

### Serialization is another flag producer

Subject loading restores both flag dwords, then clears observer-count6,
auxiliary-state25/26 and the low observation byte. It remaps both saved actor
and entity IDs but resolves the pointer through the **entity ID** lookup. The
editor additionally asserts that the resolved actor ID matches the remapped
saved actor ID; the game omits that check.

The loader restores position, uncertainty, four time fields and observed weapon
identity, subscribes to Delivery mask5, recategorizes and appends the subject.
Therefore event writers alone do not enumerate every way flags7/8 can become set.
Saving itself is selective: a scene option gates category1 subjects with
observation bit28 and enemy bit1, rather than persisting every sensor category.

## Targetability and collision identity

The located literal brain`398` bit15 writers are the base constructor (`80C0`),
place Include (`77C140 / 5FAD60`) clearing it under **TurnOff.Targetable**, and
place Exclude (`77B950 / 5FA5C0`) restoring it. The vehicle constructor preserves
this bit. Selected mode/network/action stores also preserve it.

The member-displacement scan is not a typed write proof: it includes unrelated
GUI/runtime/geometry fields and misses adjusted aliases and whole-object copies.
Those limits are recorded in the manifest; they are not grounds to claim that
all possible writers have been exhausted.

Actor ID changes send event2C **before** writing `DAA`. Chassis slot59 dispatches
registered actors to slot70, which calls `8AA220 / 72F270`. Its diagnostic identifies
the path collision resolver. It erases the old ushort-ID key and checks that the
removed count matches the old registered bit. Cleanup destroys track geometry
storage, not the actor; the chassis then clears its registered bit. The vehicle
variant also refreshes `83C`. This supplies the missing meaning of the ordering
observed in the identity pass.

## Squad departure and queued orders

Brain`100` is the squad pointer. Brain slot17 with null clears it; nonnull first
removes the actor from its existing squad, even when the supplied squad is the
same. It does not itself insert an actor into a member vector.

`9304A0 / 7B7AE0` removes membership, unsubscribes the adjusted Delivery receiver,
and clears that pointer. Leader removal backs up a **complex order** when its slot8 returns true,
marks leader replacement pending, clears the squad target and notifies remaining
members. A departing nonleader with no own sequence can instead receive a clone
of the leader's or backup sequence through actor order context5.

`9056F0 / 78BE00` returns the inline `eComplexOrder` at brain`40` only when count`88`
is positive. It is a borrowed container, not one leaf order. `9324D0 / 7B9AB0`
clones this container into squad`1A8` after that predicate, cancelling/deleting
any old backup. The full meaning of slot8 across order subclasses remains a lead.
Actor order acceptance rejects inactive or unsuitable linked actors and deletes
rejected orders, so the clone path also has concrete ownership consequences.

The squad Delivery callback is more selective than “remove on every message”:
retirement mask4 or dying vitality state causes removal in the ordinary branch;
an entity-role/operability branch can also clear actor`DB4` bit17 and remove it.
Full leader election, formation, quant work, squad target assignment and registry
compaction are the next separate investigation.

## Verification and remaining work

Frozen focused exports contain84 game and83 editor functions, with71 selected
entries in each. Independent LLVM disassembly agrees with **16,139 Ghidra
instruction starts**, normalizing LLVM's separate LOCK-prefix display. PE hashes,
selected RTTI/vslots, constants, property-stub/name bytes and instruction locations
pass structural validation. These checks establish evidence integrity, not runtime
ABI safety or complete semantic equivalence.

The [static frontier](static-frontier-25492057.md) remains active. In addition to
squad coordination, live leads include modifier filters/effects, alias-aware mode
writers, component replacement and scheduler lifetimes, other target pickers,
morale/suppression, material/projectile outcomes and native script API alignment.
