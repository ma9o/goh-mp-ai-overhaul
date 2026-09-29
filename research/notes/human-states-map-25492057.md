# Infantry policy states and inputs — build 25492057

Reviewed static pass with the dependencies listed below still open. Game RVAs
throughout unless explicitly marked editor.
The [movement pass](movement-map-25492057.md) establishes state hierarchy,
dispatch, hazards and chassis execution. This pass follows the policy inside
idle, battle, cover and search states. Exported functions remain discovery
until their branches and dependencies have been reviewed.

## Action state is separate from brain behavior state

The brain behavior selector at brain+0x52C is not the action stored in battle
state+0x3DC. `0x9545F0` writes the latter, snapshots actor position at state+0x3E0,
resets retry count+0x3D8 and emits a diagnostic name with `_move` or `_to_cover`.
Pointer-string table0x1482E40 names the action values:

| Value | Name |
|---:|---|
| 0 | std |
| 1 | detour |
| 2 | advance |
| 3 | retreat |
| 4 | task |
| 5 | fight |
| 6 | follow |
| 7 | rush_ahead |
| 8 | rush_back |

These names attach to the observed action field and table, not to every other
integer with the same value in the engine.

## Attack attempt and fallback

`0x9449E0`, named `eHumanBrain::TryAttackEnemy`, fills an output `eTarget` and
returns a signed status consumed by state helper `0x952AB0`:

| Result | Observed cause / policy consequence |
|---:|---|
| -2 | Current primary weapon fails `0x9134C0`; `wait_attack` label |
| 1 | Brain attack/alternate timer has not expired; `wait_attack` label |
| -1 | Selected ammunition policy has negative priority; `want_attack` |
| -3 | Constructed target remains empty; `want_attack` |
| 0 | Target exists; record last attempt counter, rocket-launcher tag state and target; build WeaponFire order |

The helper requests the weapon's Fsm state, selects an ammunition rule, creates
a delivery/stance context, and calls the combat target constructor `0x912D60`.
For success, `0x952AB0` uses factory `0x8D6580(target,4,0)` and appends it to a
state-owned sequence. RTTI identifies this as `Order::eWeaponFire`, allocation
0xC8, copied target+0x38, integer option+0xB8 and byte options+0xBC/+0xBD.
Tag0x1575860 is `rocketlauncher`, initialized by0x549C0; brain+0x3DC records
whether the chosen weapon carries that tag. Most of the wrapper is diagnostic-string assembly;
the actual policy is the small status switch above. Remaining dependencies:
target reachability/ground-volume selection and the attack order's inner states.

Outdoors battle Execute `0x94A790` tries healing and medic action helpers first,
then timed stance/special-action checks, tactical movement and cover updates,
commander following, attack attempt and aiming/turning fallbacks. It resets
several deadlines after a successful attack. A failed attempt can instead produce
`face_enemy` (Rotate toward remembered subject position) or `aim_target`
(Aim followed by state event0x12). This top-level sequence is reviewed; several
called helpers remain open, so it is not yet complete policy coverage.

Outdoors battle Interrupt `0x94A010` exits if the target disappears, then applies
ordered named gates: board cannon; stop advance; stop detour; stop fight;
commander movement/following; reload; healing stop/restore; injured by enemy.
`stop_advance` applies in action2 when a primary weapon exists and 2D target
distance is below0.8 times its `0x80A910` range. `stop_fight` in action5 checks
distance beyond twice configured fight distance or deadline expiry. Mode and
subject gates remain explicit dependencies rather than assumed labels.

## Tactical repositioning and cover

`0x9538A0` selects a movement action and direction from brain+0x52C, primary
weapon range, target position, task destination+0x130, task flag6, sensor threat
queries, direct-user/commander conditions and current stance. With no task,
behavior2 returns without moving; low states choose retreat/rush_back; high
states can choose advance/rush_ahead according to weapon range. Task movement
compares threat at current and prospective positions when behavior is below3.

It first tries direction-filtered cover when eligible. On success it records
the selected action and queues MovementChange(mode4), MoveToCover, event0x10.
Otherwise it picks a randomized step, validates the squad/zone boundary, and
requests a point path. A successful positive-length path queues
MovementChange(mode5), Move(route), event0x10. Failures increment state+0x3D8;
more than five failures can clear the task request. Exact path search budget
and direction/threat helper contracts remain open.

`0x9516E0` supplies a circular movement bound: a different commander uses the
group center/radius; otherwise zone mode or `0x905BB0` uses brain anchor+0x128
and radius+0x124. `0x9517A0` rejects a proposed point only if it is outside that
circle **and farther from its center than the actor currently is**. Thus an
already-outside actor may move toward the allowed area.

`0x954B40` (`update_cover`) searches within two scaled spatial units. If the
actor already has cover, it re-scores that descriptor and requires new score
strictly greater than old score+12. The current-cover re-score uses changed
query options, so this is a comparison of the two observed query results,
not a claim that all scoring inputs are identical. Success queues mode4,
MoveToCover, event0x10 and records action7.

`0x954F80`, reached by `injured_by_enemy`, may seek cover within four scaled
units when movement is not held, otherwise change stance. Its own entry gate
requires no resident cover and absent/distant target. The distance threshold is
writable global0x1482E30, registered as `coverLeaveEnemyDistance` by0x68B50
with startup default20. The stance fallback
is explicitly labeled `injured_crawl` for type1 and `injured_squat` for type2;
both request mode2. A recent-attack window of five converted time units adds
a stance/target-feasibility test. Type3 remains swim, as independently named
in the foot chassis.

## Cover battle

Shared Execute `0x9477A0` tries healing/medic helpers, handles movement binding,
timed tactical movement and cover improvement, then branches on target visibility.
The `enemy_detected` branch can execute an action from `0x9537C0`, invoke
horizontal/vertical slot6, or issue a LookAt point order. The `enemy_unseen`
branch only changes the diagnostic label in this method.

Shared Interrupt `0x947CF0` exits on missing target or cover. It evaluates
cover-direction mask0x1C0 and the candidate hide state, then ordered healing,
injury and `hide_check` gates. A hide interrupt queues a cover-state order and
event0x11. Horizontal slot6 `0x948B60` uses mask0x40 for exposed/open action and
mask0x20 for hide; vertical slot6 `0x9493D0` uses mask0x180 plus0x20. Current
cover-state masks, threat flags and deadlines determine fear/wait/open/hide
branches. `0x948160` first attempts the selected target; on a negative result,
it builds candidates from brain+0x3D0 and eligible sensor category1 subjects
whose cover-direction mask intersects the requested sector. `0x953320` tries
that list. A further negative result can cause a five-degree Rotate fallback,
depending on the caller flag. Nonnegative results, including a waiting attack,
count as handled. `0x953320` copies and ranks candidates with0x9136E0, moves
brain+0x3D0 to the front if present, then stops at the first nonnegative attack
result. If every attempt is negative it returns -3, losing the individual
failure codes. `0x9136E0` first checks whether any candidate is strictly inside
brain+0x14C's distance; if so it removes all candidates outside that distance.
It then uses the existing target-priority evaluator and writes descending rank
bytes at subject+0x11. The masks' complete named binding remains a follow-up.

## Healing and readable actor state

`0x951860` permits healing when the `0x925010/0x924F90` component ratio is below
0.7; `0x9518D0` requires an inventory item selected with tag0x1575AA0, explicitly
bound to `bandage` by0x53C80. The same ratio below0.3 reduces foot speed.

`0x925010` passes component+0x12C to `0x9291C0`; `0x924F90` passes+0x128 and
substitutes a nonzero fallback if the result is below an epsilon. `0x9291C0`
transforms these health values through actor slot62, a `Human::eParams_Health`
multiplier and a `Human::eParams_Veterancy` fractional bonus. The component
is reached through actor+0x578→+0x18. `0x936580` returns the human ability
collection at actor+0x1390→+0x528, or a thread-safe initialized global fallback.
The parameters query uses concrete `Human::eParams_*` types, not a single
untyped scalar getter.

`0x9744E0` iterates ability entries at collection+0x20..+0x28, obtains each
entry's implementation from+8, compares its slot9 ID, tests slot5 applicability,
then returns the **first applicable match's** slot4 parameter result. This
wrapper does not accumulate all matching entries.

`0x9519A0` sets state healing byte+0x3C0 and queues `Order::eHeal` followed by
event0xF, which clears the byte. Healing-stop predicate `0x951FA0` tests whether
sensor+0x2F4's elapsed time is below5, using `0x8782D0`; zero timestamp yields
FLT_MAX. This is an elapsed-event measurement, not distance or threat strength.
Sensor dispatch `0x877D20` writes that timestamp on signal type4, also setting
subject+0x14 bit9. The caller's `injured_by_enemy` name and its aggregate brain
bit15 connect that signal with injury; the publisher remains to be traced.

The same signal handler sets `threating` bit7 (and bit10) for signal types3/4/5
within sensor+0x308, with type4 bypassing distance. Within sensor+0x30C, or for
type4, it additionally sets `attacking` bit8 and invokes disguise detection.
New threat/attack flags clear subject suitability block+0x6C. These are concrete
producers for the predicates mapped in the combat pass; signal publishers and
the distance helper's units still require an end-to-end check.

`0x9520B0` (`healing_restore`) contains a suspicious address test, confirmed in
Ghidra and LLVM assembly: actor-derived pointer+0x12B8 must equal zero before
the saved healing item can be used. Ordinary valid actor pointers cannot satisfy
it. Editor0x7D9700 has the same assembly gate. The helper cannot enter its
restore branch with an ordinary valid actor pointer in either binary; this is
not a decompiler-only artifact. The normal healing order builder is separate.

## Commander following

`0x94B2B0` (`follow_commander`) records action6, actor XYZ and the requested
follow point, adjusts speed, then queues a movement order and event0x13. A
not-moved check can clear the follow request; brain flag10 bypasses that check
once and is cleared. `0x951530` compares change in follow destination against
the remaining actor-to-destination distance to decide when to revise a route.
Its first comparison is squared displacement against `spatial_scale*2` exactly
as compiled; do not silently square that threshold in a reimplementation.

`0x9515C0` attempts in-place replacement on the front child of a complex order
through order slots13/14, updates its cached endpoints and clears flag10.
It ends and destroys the temporary proposed order whether replacement succeeds
or fails. Passing an owned live order here would violate that observed contract.

`0x9514D0` requires both leader/follower chassis flag4 before stance matching.
`0x951450` copies leader stance except when one side alone is swimming.
`0x9512C0` matches a nearby human leader's movement mode, or another leader's
speed. For larger separation it computes leader speed plus distance/(1000*5),
then asks the follower foot chassis for a faster matching mode if necessary.
The distance is between cached route endpoints, not a new position read.

## Searching remembered areas

Find-enemy state `0x94C410` activation copies center/radius from the subject's
Recon zone, stores its zone handle at+0x3E4 and registers the actor as a searcher.
Deactivation unregisters it and clears any search aim. Interrupt `0x94C750`
exits for threat/selected target, changed/missing zone or lost search assignment;
in phase6 it periodically refreshes look points.

Execute `0x94C8A0` uses current phase+0x3D8 and pending phase+0x3DC. Event0x11
copies pending→current and requests reconsideration:

| Phase | Main operation |
|---:|---|
| 0 | Prepare inventory/weapon once, turn toward area, optionally aim and wait, set stance, then pending2 |
| 1 | No action in this method |
| 2 | Choose3 outside the area or4 inside |
| 3 | Aim sweeps around the area's edges, then pending5 |
| 4 | Timed local sweeps/waits, then pending8 |
| 5 | Move into the circular area; set current4 and an entry deadline |
| 6 | Aim at sampled look points while its timer runs, mark visited sector; then current8 |
| 7 | Move to next search position, then pending6 |
| 8 | Ask Recon for the next sector position, adjust movement/aiming and pending7; completion→9 |
| 9 | Mark subject flag27, refresh brain selection and deactivate |

`0x94DD50` rebuilds look-point vector+0x410..+0x418: nearby cover points with
terrain height+0.5 scaled units, then eligible human/stuff entities from the
spatial index. If empty, it adds three random points at radius6 scaled units.
A debug option creates markers; it is not part of normal decision output.
The search phase picks randomly from that vector and issues timed Aim orders.

Recon uses0x80-byte zone records. `0x88AEA0` adds/removes searchers in six
pointer slots+0x18..+0x40 and updates count+0xC; `0x88B110` checks membership.
Slot selector `0x889B40` scans slots5 down to0. An existing assignment wins;
an empty slot is usable only while count+0xC is below capacity+0x10. Otherwise
it replaces the farthest incumbent only if that incumbent is more than ten
scaled units farther from the center than the candidate, using 3D distance.
Zone+0x58
is center XYZ, +0x70 is ring index and +0x78 a64-bit visited-sector mask.
Global0x15840B0 supplies16-byte ring descriptors: inner/outer radius, sector
count and cumulative sector count.

Initializer0x88B790 constructs sector counts **1,4,7,11,16,21**, cumulative
counts1,5,12,23,39,60, followed by a zero-sector sentinel. For each ring,
outer radius=sqrt(inner radius² + sector count*scale²); the next inner radius
is the previous outer radius. Leaf0x5F930 writes scale0x1584094 as three times
the spatial conversion global. Thus equal angular sectors within each ring
have equal annular area across rings. The 64-bit mask uses only60 bits at the
largest regular radius. The sentinel preserves radius/cumulative count.

`0x88B170`, named `eReconBase::NextSearch`, returns **true for completed search**,
not for a newly chosen point. When the visited mask covers all sectors through
the current ring, it marks zone byte0 and returns true. Otherwise it searches
unvisited sectors, compares nominal positions' squared distance to the actor,
and samples the best candidate's sector/radius into the output XY point.
Assembly resolves the nominal angle as2π*(sector+0.5)/sector_count and nominal
radius as(inner+outer)/2. **Each new closest candidate consumes two RNG draws**,
even if a later sector replaces it. The chosen point uses angle
2π*(sector+u)/sector_count and radius inner+(outer-inner)*v. This is linear
radius sampling, not area-uniform sampling. Invalid zone indices also return
false after logging, without writing a point; callers must satisfy the valid
zone/remaining-sector precondition.

`0x88AC90` marks a sector for a visited position, but only in zones whose +4
integer is nonzero and equals+8. It locates the ring by radius and the sector
by normalized atan2, then sets its bit. Ghidra incorrectly types the two count
comparisons as floats; assembly performs integer TEST/CMP. Precise count
producers are now linked: zone+4 counts registered observations; +8 counts those
whose subject flags+0x10 bit26 is set. Sensor0x878AE0 sets that bit when the
observation is older than0.3 converted time units and subject bit27 is clear.
`0x878970` updates the same bit/count together;0x88AC40 starts search when all
references are waiting. `0x88ABC0` renews center/radius/timestamp for a newer
observation, clears both completion/start bytes and visited bits; an equal-time
observation can only reduce radius. `0x889AB0` starts search once, sets a five-unit deadline,
selects the first ring whose outer radius exceeds zone+0x64 (or the final
record), clears visited bits and sets capacity=min(ring index+1,6).

Status0x88AE10 returns3 for completed,2 while some registered observations are
not waiting,1 for invalid/unavailable assignment,0 for available search. A full
assignment can still return0 during its five-unit grace period even when the
candidate cannot replace anyone. Assignment0x88AEA0 can then fail to insert;
the find-enemy interrupt independently checks actual membership after phase1.

## Stance policy and close combat

`0x943150` blocks autonomous stance changes during direct control, cover kind4,
inventory work, actor terrain states4/5 and follow flag9. It measures 3D distance
to the subject's remembered position in converted spatial units. When behavior
is neither0 nor4 and distance>25, an allowed, feasible crawl pose takes priority.
Otherwise distance>10 can select squat; distance<7 can select stand. The squat
branch is suppressed by a health-based flag: health ratio<0.1 sets it always,
0.1..0.2 with probability0.7, 0.2..0.3 with probability0.3; brain flag16 also
sets it. This flag does **not** gate the earlier distance>25 crawl branch.
An already-selected actual pose returns false rather than queuing a redundant
change. The random branches advance the shared RNG counter and state.

`0x942FC0` additionally constrains pose changes under movement-hold: it ranks
stand0, squat1, crawl2 and refuses a requested rank below the current one.
Crawl is also excluded by weapon tag `cannot_lie`, bound by0x68410, and
environmental class3/4/5, whose named binding remains open.
`0x953600` translates the selected pose into
MovementChange(mode2), with a Rotate toward the enemy before crawl.
Feasibility0x940560 maps actual stance to delivery pose, temporarily evaluates
human pose/animation nodes, calls0x940130 and restores the saved state. With
no weapon or no target it returns true; it is not an unconditional line-of-fire
test for arbitrary inputs.

`0x94BBA0` (`hand_fight`) rejects targets more than two scaled units above the
terrain or outside the allowed movement bound. It probes a point path with
tolerance1.5*configured fight distance. Success records action5 and queues
MovementChange(mode2), `Order::eFight` factory0x8E7550(target), event0x11. The outer Execute
also requires the fight cooldown, distance below configured fight distance,
standing stance, a human target recently observed, allowed movement and the
attack-control predicate. The path here verifies reachability; the returned
route is not installed as a Move order by this helper.

`0x94B7E0` retries detours with a factor1.5^retry and resets the retry counter
after the factor exceeds10. Its solver0x93ED30 can return a pose-only result,
a point or cover. The wrapper emits `detour_pose`, mode5+Move, or
mode5+MoveToCover respectively, then event0x10 for movement. The solver remains
a geometry/target-feasibility follow-up; it includes temporary pose evaluation
and restores actor pose state before returning.

## Healing orders and medic patients

`0x8FA220` is the Heal order lifecycle. Start stores the human pointer at
order+0xA8 and sets human+0x12B8. End clears+0x12B8/+0x12B9, emits
`stop_healing` and a health event, and conditionally restores inventory state.
`0x8FA4B0` performs the work in phases0/1/2:

| Phase | Observed operation |
|---:|---|
| 0 | Finish if health ratio>0.99; select heal-head below0.3, heal-leg below0.6, otherwise heal-hand; require and expend one bandage; set budget30 |
| 1 | Wait for inventory operations, save the right-hand item at human+0x13C0/+0x12B0, issue PutFromRight if needed, then enter2 |
| 2 | Maintain the selected Fsm state and apply health increments; when budget expires, return to0 if ratio<=0.85, otherwise finish |

Those Fsm names are bound by0x58380/0x583B0/0x58350. Their selection follows
the health fraction; it does not prove a body-part wound model. `0x938A40`
applies min(requested,(maximum-current)/(maximum/100)) percentage points, calls the
health setter and emits component event0xD. Heal's per-step input is quant delta
times9.6/time-scale. Simulation-role helpers0xC0B630/0xC0B5D0 gate command
replication and the local work path; they test complementary manager slot25
results in manager modes2/4. Their named role binding remains open.

`0x952290` queries `Human::eParams_Medic`, checks0x9366A0 against its integer
requirement, then asks the medic-tent extender for a patient. **0x9366A0 counts
applicable entries in the veterancy collection; it is not a charge counter.**
`0x1BFBE0` searches a scaled sphere around the tent owner and chooses the nearest
eligible allied patient. Its radius also uses owner scale0x6FD580. Eligibility
`0x3712B0` requires a human, the named `personage` actor bit, health-component
flags1 and2, and entity flag28 clear. `0x371330` tests human+0x12BA, preventing
two medics from treating the same patient.

`Order::eMedic` Execute0x370760 tracks patient+0x48 and states1..6: wait for
inventory; approach with progressively relaxed positioning; empty hands and
rotate toward the patient; wait for that child; request `revive`; then count
down while the Fsm remains active. It sets patient+0x12BA when treatment starts
and clears it on interruption/end. `0x3713E0` emits start_healing and creates
the healing marker. Completion0x371960 updates the patient's health component,
transitions Fsm states, updates medic/patient statistics and emits an event.
Damage/death flags, exact restoration quantity, statistics and replication
semantics remain part of the health/entity investigation.

Battle healing0x951B60 rechecks every1..2 converted time units, requires ratio
below0.7, a bandage and no recent injury signal. Suitable resident cover can
skip the threat scan. Otherwise it inspects enemy subjects and their combat
ranges; within a threatening range a random draw<=0.7 postpones healing.
If the draw permits movement and movement is allowed, it queries cover within
seven scaled units with `Human::eCoversForHeal`, then queues mode4+MoveToCover
and event0xB, rather than immediately healing. If no threat blocks it, it queues
Heal. Idle healing0x951AA0 has the same item/health/injury gates plus a world-mode
predicate, without that battle cover search. Stop0x951FE0 can request a recovery
Fsm state, clears the state healing byte and delays the next check by3..5 units.

## Weapon changes and ground pickup

`0x944520` skips inventory-busy actors. A saved old weapon reference at
human+0x12A0 and entity ID+0x12A8 can trigger `take_old_weapon_from_ground`
within ten scaled units. Eligibility0x945C80 requires a stuff extender with
item+0x88, no holder+0x98, a `weapon` item tag, allowed terrain state and entity
flag28 clear. A held `missile` tag0x1575380 starts a three-unit stow deadline;
on expiry the non-direct-controlled actor issues PutFromRight.

When brain+0x47C is due, the inventory weapon evaluator can select a different
weapon, record a target reservation and queue a mount order. Successful swaps
delay reconsideration5..10 units. Without a current usable weapon, context
flag1 permits ground pickup0x945520, separately rate-limited by+0x480 to3..5
units. It searches a ten-scaled-unit circle for eligible stuff entities,
evaluates their item score, sorts the nonzero candidates, then queues
MovementChange(stand,mode2)+Take for the first candidate. Exact item scoring
and comparator semantics remain dependencies.

## Idle work, following and explicit boarding

Outdoors idle Interrupt0x94ECB0 requires alert state1 and leaves for a selected
enemy or eligible remembered subject. Execute0x94EF90 handles task destination,
medic/healing, following, subject interest, requested looking and leader facing.
Fallback0x9505A0 runs a queued user order, returns toward the zone/anchor, or
calls the general idle helper. Idle event0xC snapshots actor XY into brain
anchor+0x128 via0x903250; it is not by itself a general task-completion API.

The civilian override0x94FBA0 can enqueue walking or waiting before calling
the shared Execute anyway. Static ordering does not establish that its initial
action will remain installed. Cover idle0x947370 instead schedules lookout,
cover-state changes and waits; vertical slot6 can request0x9509F0 when the
current cover pose requires it. Horizontal idle slot6 simply returns false.

Follow factory0x8D2B20 constructs `Order::eAdvance` (0xF8 allocation), copying
the destination to+0x5C and its byte argument to+0x38. Following therefore uses
Advance's inner policy; it is not a bare Move. `0x902AA0/0x902AD0/0x902B00`
identify self-as-commander, a different commander, or return the commander
through brain+0x100→+0xA8. `0x902B20(false)` clears follow flag9; the true case
requires flag27 and also sets flags10/25. `0x902F50` clears task/follow/return
flags6/7/9/11 and look flag29 and invalidates the requested look point.

`0x9523D0` consumes human+0x12BC, an actor-ID request initialized to-1. It
resolves the ID, requires actor type-bit1, clears the request even on failure,
and on an accepted interrupt queues factory0x8E3B00 through0x7EFA10. This is
the explicitly named `board_cannon` interrupt; the boarding order remains open.

## Evidence and remaining work

The [manifest](../artifacts/build-25492057/human-states-map.json) records142
reviewed or partially reviewed game methods, three editor comparisons,82 field
records,21 read-only float constants, the nine action names and15 RTTI tables.
[Validation](../artifacts/build-25492057/human-states-validation.json) checks
5,456 instruction locations against301 game/16 editor exports and fingerprints.
Exports include discovery functions that are not claimed as reviewed policy.
Independent LLVM disassembly agrees on678 instruction starts across six selected
functions, including the healing-restore gate in both binaries. These checks
validate evidence locations and bytes, not semantic completeness or a callable ABI.

Still productive: complete0x93ED30 placement/feasibility dependencies;
WeaponFire/Advance/Fight/boarding execution; general idle helper0x95FCC0;
cover pose/mask bindings and the healing-cover validator; exact ground-weapon
scoring/comparator; signal publishers and health component attachment/lifetime;
medic health restoration/statistics; complete event and simulation-role binding.
The search phase+0x3E1 cleanup flag's set-side remains unlocated in this Execute.
