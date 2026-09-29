# Engagement orders, readiness and melee — build 25492057

Continues the [human geometry pass](human-geometry-map-25492057.md). Addresses
are game RVAs unless marked editor. This is a static contract map, not a callable
SDK. The [investigation frontier](static-frontier-25492057.md) remains active.

## Ownership and dispatch

The order owner at `+8` is the actor's `+AF0` interface. Subtract `AF0` to
recover the complete actor; interface-relative `+2C8/+2D0/+2D8/+2E0/+2E8/+2F0`
reach Fsm/Cover/Brain/Sensor/Weaponry/Chassis respectively. The owned child is
`+30`. Completed children receive slot4 (end), then slot0 (deleting destructor),
and the parent clears the pointer. This is an ownership boundary, not a borrowed
command reference. The previously mapped base dispatcher calls slot19 with
phase0=start, 1=tick, 2=end. Results are 0=continue, 1=complete, 2=fail.

| Concrete order | Vtable | Slot19 | Selected additional slots |
|---|---:|---:|---|
| Advance | 10E6FE8 | 8D02B0 | 13=8D0F50 replacement test, 14=8D0FA0 replacement apply |
| WeaponAiming | 10E7508 | 8D6F30 | Maintains cover/body orientation under a parent |
| WeaponFire | 10E7658 | 8D77B0 | Factory8D6580, allocationC8 |
| Fight | 10E88F8 | 8E69E0 | Factory8E7550, allocationE0 |
| Attack | 10E8F78 | 8EDFD0 | Slot20=8EDF80, installs WeaponFire child |

## Named weapon inputs and readiness

Weapon parser `8299E0`, registry `1580E40`, now closes two geometry/pickup
questions. TokenDC `AimingTolerance` stores the parsed float times angular
scale `1547DE0` at weapon definition `+15E8`. TokenF0 `AmmoLocked` sets
definition `+15F8` bit0. The pickup rejection in `958610` therefore combines
**AmmoLocked and an empty secondary count**, rather than an unnamed weapon
quality flag. `809F40` tests definition `+808` against `weapon_throwing`
(initializer59CA0); this is a definition-kind name, distinct from the broader
`throwable` equipment tag. Nearby bindings are Automatic=bit1, Automatic_AI=bit2,
Reactive=bit4 and AutoReload=bit5. The latter two parse booleans; the first
three named flag tokens set their bit by presence.

`80A070` requires a nonnull weapon definition, place-definition `+D30` bit0,
a false owner-controller predicate `77B8A0(controller,weapon)`, and both aimer
bones `+20/+28` (`81DA10`). It does not check every ammunition or trajectory
condition. The meaning of the controller predicate remains a follow-up.

Charger helper `80BF90` is true if the runtime weapon/definition is absent or
the charger slot1 object has `+50` definition tagged `dummy`. Initializer
`53EC0` binds that tag. `80BF00` is true for that special case; otherwise it
is true only when the slot1 object's integer `+DC` is zero and charger `+14`
bit4 is clear. Getter `7C9270` establishes that offset but not its producer.
Thus neither helper should be renamed simply “can fire.” Charger `+10` is a
deadline and `+14` bits2/4 appear as separate blockers in the order paths.

`8174D0` resolves target XYZ from weapon `+154` in mode1, otherwise from its
`eTarget` at `+C8`, then returns shooter slot12. The base implementation
`817C40` returns true. Bullet/flame implementation `819DA0` measures planar
distance from aimer slot6's position, requires distance >= weapon-state `+28`,
and normally <= weapon-state `+24`. If chassis `83FB80` and state `+7C` are
both true, it omits the upper limit. Exact chassis flag names remain open.

`8107D0` combines a place update with a readiness query: when `+C88` is zero
or due it sets `+D31` bit1, clears the timer and notifies controller `781410`
with place `+2C8`. It then **tail-jumps to `781D50` and preserves its return**.
The shooter caller `817F70` tests AL from this call. The void decompiler
signature hides that result; it must not be taken as evidence that the helper
only updates state. The controller predicate itself is a follow-up.

## Obstruction checks can send events

`820030` clears aimer `+130`, obtains two bone-derived endpoints through
`81DEF0`, and stores `820150`'s result. If that is clear and the owner is human
in stance1 (crawl), it makes a second probe from actor XYZ with Z raised by
0.2 spatial units to the updated aimer `+38` bone position. True means blocked.

`81DEF0` normally uses aimer `+20/+38`; an alternate flag at `+78` resolves
bones by name from `+80`. It refreshes dirty bone transforms. With no auxiliary
bone it uses the primary bone's `+138` related bone and primary position. With an
auxiliary bone it projects their displacement onto the primary bone axis; a
weapon-kind branch uses related bone `+140` XY at the primary bone's height.

`820150` traces with flags `0x106E03 + ((option XOR 1)*0x80000)`, excluding
the weapon owner. Ordinarily a nonempty collision vector means blocked. With
option=true and **exactly one** 0x48-byte hit, kind4, a nonnull volume at
record `+20`, volume `+28` bit5 clear and volume `+C` bit13 set, it creates
an entity/volume event context, emits **`break_window`**, and returns false.
This path mutates the world. It is not safe to treat this whole helper as a
pure line-of-fire read. Assembly `820257..8202D5` confirms the count and event
branch; an empty vector cannot take it because of the unsigned length test.

`81F030` is the time-to-aim trial consumer. It initializes the output to the
global sentinel, builds/evaluates the bone trial, and returns7 when place
predicate `810780` succeeds and the optional `+40` bone's double values
`+218/+220` differ by more than epsilon. Otherwise trial success with its
secondary flag clear writes the estimated time, returning3 only when measured
error is strictly below **AimingTolerance**; all other paths return6. The
estimate can therefore be written even when the final error check fails.

## WeaponFire and its aiming child

`8D77B0` acquires the selected runtime weapon every invocation. With no weapon,
it fails unless remembered byte `+BD` identifies `weapon_throwing`, permitting completion. An initial failed
`80A070` gate returns2, including during cleanup. Start clears weaponry targets
in mode2, assigns order target `+38` with option `+B8`, mode3 and unlimited
shot argument -1, copies order `+BC` bit0 into weapon `+264` bit3, remembers
`809F40` at `+BD`, and installs `WeaponAiming(0)`. It stores initial target
invalidity at `+C0`, zeros accumulator `+C4`, and enables base cleanup.

Tick executes/removes the child, then completes if a second readiness check
fails or the weapon target type is zero. Its timeout is precise:

- Accumulate quant delta into `+C4` if actor brain control bits0..1 equal1,
  `80BF00` is true, charger bits2/4 clear, charger deadline zero and `80BF90`
  false; **or** if the weapon has the `visor` tag.
- Otherwise preserve the accumulator. A successful intervening tick does not
  reset it. Fail when accumulator > `0.1 * time_scale`.
- Aimer state3/4 completes when a target initially valid becomes invalid.
  States5/7/8 keep running; other values fail.

Actor helper `7EF670` tests those brain control bits; it is not an inventory
busy predicate. Target predicate `83DB40` returns false for point type2 and
true for unsupported/empty types. Types1/3 require actor `+DB4` bit8; if a
named target volume resolves, its component `+24` can invalidate the target.
The same actor bit participates in ally-collision filtering in the prior pass.
Its producer/native flag name is still open.

End calls `80A730(weapon,1)`. This clears the target and stops the shooter via
the previously mapped weapon lifecycle. Merely installing WeaponFire is not
the same operation as creating a projectile; the weapon's scheduled update
performs that downstream work.

`WeaponAiming` (`8D6F30`, allocation48 from `8D6350`) remembers the selected
weapon at `+40`. A weapon change or active vehicle movement cancels its child
and resets rotation timer `+3C`. With no child it can request a cover-facing
transition before aiming. Aimer state7 can lead to direct human transform
rotation or a Rotate child, conditional on chassis capabilities/flags. It
sets human `+1420` bit2 on those paths. Rotate targets can be refreshed after
the timer threshold. Cleanup clears its chassis mode request and can restore
a cover state when both bytes `+39/+3A` require it. These are bounded lifecycle
and branch contracts; chassis flag definitions and all body-turn limits remain
open. This order generally continues until its parent cancels it.

## Advance: movement and engagement levels

Constructor `8D08E0` sets byte `+38` true, state `+3C`=1, level/timer fields
zero, destination/radius and dirty flag zero, and constructs a target at `+78`.
Start explicitly clears byte `+39`, zeros current/previous levels `+40/+44`,
initializes deadline `+6C`, calls `8D2980(1)`, then starts movement.

`8D1000` queries the actor's named **cannon** extender (`92D0E0`, key bound
by `67100`). If present and extender `+C30` !=1, it installs `92D220(1)` and
sets state0. Otherwise state1 gets a point Move when radius `+64`<=0 or a
circle MoveArea around XY `+5C/+60`. It executes that new child immediately.
The cannon order (`92D310`) waits for model animation, requests extender
slot9 while current `+C30` differs from requested order `+38`, and fails on
its disable/no-crew/actor gates. Native names of cannon states0/1/2 remain open.

Replacement slot13 requires **exact runtime type-info equality**. Slot14
casts to Advance, copies only destination/radius and sets dirty `+68`.
On the next tick an existing MoveTask child gets replacement-test/apply;
otherwise the parent returns to state0. Child commands are explicitly ended
and destroyed when displaced or completed.

`8D2980` has a sensor side effect. With start and byte `+38` true, `902DE0`
sets subject `+6C` for sensor subjects whose `+14` bit1 is set, clears selected
targets if they were marked, and clears brain `+39C` bit13. End uses `902EC0`
to clear **every** subject `+6C`. Group leaders apply these operations to the
member vector at brain `+100 -> +80/+88`; unaffiliated actors apply them to
themselves. These flags feed the already mapped target eligibility predicate.

`8D1C50` is the subject gate: human brains require `+3B8` selected subject,
and, if brain `+14C`>0, strict squared 3D distance below that radius. Other
brains use low-bit result of `+39C` bit13.

`8D2120` tests whether to raise engagement. For cannons it reads the first
vehicle-brain subject and compares distance with 0.75*brain slot21's range.
For humans it requires a nonzero configured `+14C`, a weapon, no veto from
`944D90`, attack-control permission, and no negative-priority ammunition rule.
It resolves a target and makes the temporary pose feasibility trial `940560`
with delivery pose1 if crawling, otherwise3. It is therefore a geometric and
policy test, with the temporary-state caveats from the geometry pass.

`8D25B0` writes engagement level `+40`:

- No relevant subject normally gives0. A new qualifying subject promotes0 to1.
- Non-vehicle actors in states1/2 reevaluate `8D2120` on a half-unit timer,
  selecting level1 or2. In other states they wait more than three converted
  units since `+58`, then compare age of brain `+18` against five units.
- Actor type bit3 uses ten units instead. Cannon handling also combines a
  `+6C` deadline, the geometric gate and a ten-unit brain timestamp age.

The timestamp's producer/native meaning is not established by this consumer.
When levels change, level2 can stop the movement child, clear group subject
marks, and enter state3; level1 can schedule recovery/equipment/cannon work
and enter state4. Dropping engagement from states2/3 resumes movement.

| Advance state | Observed work while level is stable |
|---:|---|
| 0 | Wait for cannon transition child, then start Move |
| 1 | Return movement child's status |
| 2 | Maintain chassis `+3C0` bit9, resume Move when weapon/ammo or aimer conditions require it, and use direct target assignment `8D1910` |
| 3 | With no child, try stance helper `8D1DE0`, then gated equipment/aim/fire helper `8D14B0` |
| 4 | After auxiliary child, enter3 for cannon or resume Move |

State2 is selected only when byte `+39` is nonzero. Start clears that byte;
this pass has not found a set-side. Presence of the branch is **not evidence
that ordinary infantry reaches it**. `8D1910` uses attack decision `9449E0`:
on success clear mode2 targets, assign target in mode2 and write chassis facing
XY `+6C/+70`; non-wait failures delay the subject and set brain `+39C` bit25.

`8D14B0` prioritizes equipment recovery (`944520`) unless policy disallows it,
then may rotate toward the subject, initiate charger work, create WeaponFire
on decision0, or WeaponAimTarget with a bounded wait on other decisions. It
also updates subject/brain retry flags. `8D1FE0` gates this by brain timers
`+3A0/+3A8`, byte `+3DC`, and remaining charger deadline. `8D1DE0` requests
a stance change selected by `943150`, then renews its timer. End restores
subject marks and the conditional chassis/weapon mode state.

## Attack: retries and action selection

The outer `8EDFD0` saves selected weapon index `+100`, records whether the
original target is an entity (`+F0`), initializes selection/visibility timers,
and sets cleanup. End clears weaponry mode3 targets and restores the saved
weapon index. Missing target/type, or an invalid entity-controller flag,
completes the order. Tick dispatches `8ED670` when its timer is due and no
child remains, with state5 permitted to reevaluate during a child.

`8EC470` resets state and retry counters and invokes `8EC510`, which is a
**target repair helper**, not a child destructor. For ordinary human targets
it resolves an eligible ground/volume target via `912EB0`. A linked/dead-style
entity-flag branch instead searches eligible geometry volumes near its bounds
center and can replace the target with a point. Its subtype/flag semantics
remain open.

`8ED670` can replace a broken target volume with a randomly selected eligible
unbroken volume. `visor` equipment enters state7, sets the weapon target and
keeps a WeaponAiming child. Other weapons can take a nearby-human melee path:
configured fight distance, target height <2 spatial units and a successful
point-path probe lead to Fight. Otherwise `8ED010` chooses equipment/ammo.

`8ED010` constructs a temporary sensor-subject-shaped record with target entity,
position and flag masks. Vehicle brains can select a runtime place and ammo
with `98ABA0`; humans run the weapon evaluator. A required item change creates
a weapon-taking child (state1); a required ammo change sets state2. With no
weapon, the human fallback permits one recovery attempt (`+C4`), then fails.
Return3 is this helper's **continue-selection sentinel**, consumed internally;
it is not the base order completion code.

`8EC030` then performs these actions:

- A qualifying human definition tag creates AttackProximityCharge (state8).
- The charger-empty condition permits one reload attempt (`+C8`), state2.
- Outside planar weapon range, increment `+CC`; fewer than four attempts
  create a MoveArea (state3), then fail on exhaustion.
- Inside range or with an existing child, a one-shot `+C0` gate can install
  WeaponWaitReady (`8D6470`, target at child+38).
- In one simulation-role path, a target not satisfying player observation
  `7F4DF0` for more than six converted units fails the order.
- Otherwise set state4 and call TargetAccessor. Result3 plus expired human
  brain attack timers enters6 and calls slot20. That sets `+F1`, clears retry
  fields and installs WeaponFire(target,4,0).

Obstruction helper `8EBC10` remembers actor XYZ at `+DC`. With an unchanged
position, it fails after more than20 attempts when option `+C1` is set, or
after ten converted units since `+E8`. Human detour radius grows as **1.4^n**,
capped at12 spatial units; exceeding the cap resets the exponent counter.
The geometry solver's pose/point/cover result becomes a stance change, Move,
or MoveToCover followed by WeaponWaitReady. Successful detour resets the
counter and rearms the one-shot readiness gate. Failure can enter state5 and
move toward the target if beyond the ammunition safety radius. Nonhumans and
some option/weapon-kind paths instead use the target-oriented helper8D68E0.
Its full body-turn/aim contract remains a follow-up.

The visor state refreshes its timer only on suitable sensor visibility (or
point-target mode), and fails after more than five units without refresh.
These order-level deadlines are separate from sensor memory expiry.

## Fight and data-driven blows

Fight target is `eTarget +40`, actor at `+58`, selected blow at `+C0`, selected
opponent variant at `+C8`, hit deadline at `+D0`, optional area radius at
`+D4`, and melee agility at `+D8`. `8E70A0` can resolve an area to a random
eligible sensor subject using `8D6CD0`; the latter uses target eligibility and
strict squared 3D radius, not an unconditional nearest-enemy search.

`8E69E0` rejects invalid/self targets, reads melee parameters and approaches
the target. State1 tries `95E410` to select a blow; state2 ensures standing
stance, then starts its named animation at agility speed. The hit deadline
is `convert(variant+C / agility)`. State3 waits while the animation runs and
applies the blow when due; if the child finishes first it can still apply the
pending blow. It then clears the Fsm request and selects/approaches again.
The `kick` Fsm tag or wrong stance fails during this timing path. The ordinary
`animation` tag controls the pending-hit timer; before that tag is present the
deadline is renewed. Cleanup `8E6EE0` sends `stop-move` and optionally replicates
the request. Initializers580B0/58530/58C50 establish these names.

`95E410` first requires separation >=0.1 spatial units and named-volume ray
contact whose first hit entity is the opponent. It filters loaded blows by
melee stroke tags, equipped-item enable/disable tags, facing within45 angular
degrees (through the shared scale), and a compatible opponent variant. It
collects candidates and chooses randomly; an alternate option requires the
`moving` tag. A debug path admits a `test` tag. The decompiler invents writes
to readonly angle constants in this large function; assembly only loads them.
Only the real writable candidate-vector state belongs in the entity contract.

`95DC00` scans opponent records of stride1C. It transforms the attacker's
position with target-derived transform data, requires orientation1 when local
X>=0 or2 when X<0, distance<=25+35 engine length units, and a bit matching
the target's actual chassis stance. The first matching record wins. The
transform helper and global scratch-matrix lifetime remain open. `95D6F0`
rechecks item filters, facing and variant compatibility at the hit moment.

Loader `95ED00` owns a global vector of **A78-byte blow definitions** from
`/set/blow/*.set`; it destroys old records before rebuilding. Parser `95D230`
uses registry1587F90 with these bindings:

| Blow field | Configuration |
|---|---|
| +60 tag set | tags |
| +390 integer | distance: near0, medium1, far2 |
| +398 name object | self/Animation; string storage is at+3D8 |
| +3F8 / +728 tag sets | self/enable_items and disable_items |
| +A58 float | self/Fatigue |
| +A60/+A68/+A70 | opponent vector begin/end/capacity, stride1C |
| variant+0 integer | Orientation: Front1, Back2 |
| variant+4 byte | Movement mask: Stand bit0, Lie bit1, Squat bit2 |
| variant+8 float | Damage first value |
| variant+C float | Optional second Damage value; used as hit-time numerator |
| variant+10 float | knock_down_chance |
| variant+14/+18 floats | knock_down_time pair |

Registered Health and delay tokens do not have independent cases in this
parser. Registration alone does not establish a supported property path.
Approach-distance table `10EDC80` is [17,29,35] for near/medium/far. The units
are engine lengths; the table is not multiplied by spatial scale here.

Melee parameter parser `976EF0` binds `strokes +8`, `agility +C`, `strength
+10`, and chance fields `defence_stroke +14`, `counter_attack +18`,
`knockdown_blow +1C`. Defaults (`977710`) are strokes0x40, agility0.5,
strength1 and chances0. `976CB0` stores levels in an 80-byte vector at ability
`+68/+70/+78`; each level has a name at0 and params at60, with current parse
record at `+80`. Slot12 delegates parsing into those params. Global15984D8
is the dynamically allocated ability key returned by Melee slot9 (`977070`).

`95D820` calculates knockdown probability, not hit accuracy. Let `b` be the
variant's knock_down_chance, `k` the attacker's knockdown_blow and `r` the
opponent's resistance knockdown_melee (`+C`, established in the resistance
pass). For b<0.99 it uses **(b+(1-b)*k)*(1-r)**; otherwise it leaves b unchanged.
Success is a strict random draw < that value. On success, duration is the
configured center plus a symmetric random perturbation if spread is nonzero,
then converted to ticks; otherwise it uses the no-duration sentinel. Damage
passed onward is **variant damage * melee strength**, independent of that
knockdown draw. Assembly confirms float loads; integer-looking casts in the
decompiler are not integer conversions.

`95DF20` forwards the damage context through opponent slot46, publishes a
type3 injury/damage event, drops human cover, writes human `+13D0` reaction
flags and calls the previously mapped KnockDown path. It emits `blow_effect`
on the attacker and `blow_opponent_effect` on the opponent, with scaled damage
in the latter event context. Named property events `+E0/+E2` are also invoked
when configured. Full scripted health-effect execution remains open; forwarding
this scalar is not by itself proof of the final vitality decrement.

The installed archive has13 blow definitions and8 melee levels. Every reviewed
blow defines zero base knockdown chance/time; the higher melee levels supply
nonzero knockdown_blow. Thus zero in the blow file alone does not disable the
probability branch. Archive-member hashes accompany these observations.

## Cross-binary and verification limits

Selected editor counterparts preserve the reviewed field contracts: Advance
756400, replacement7572E0/757330, Fight76CF10, WeaponFire75DD30, shooter range
69CD00, melee level/parser7FEDB0/7FEFD0. Editor replacement asserts
`CanUpdateFrom(_other)` and `other`; game still calls the predicate but lacks
those assertions. Fight adds an unreachable-phase diagnostic. Advance contains
different helper outlining/inlining. These are selected comparisons, not a
claim of transitive binary equivalence.

The evidence manifest checks function entries, instruction locations, RTTI,
token registration bytes, constants and resource fingerprints. LLVM disassembly
independently checks selected decision routines. Neither check proves native
ABI, object lifetime, runtime reachability or completeness.

Still productive: controller/readiness predicates and charger-field producers;
actorDB4bit8; Advance byte39 reachability; brain timestamp producer; complete
vehicle/cannon and squad behavior; Attack option fields and target-volume
selection; body/cover-turn helpers; melee defence/counterattack consumers,
strokes bit decoding, animation completion and scripted blow effects; actual
shooter sampling, projectile construction, collision and damage execution.
