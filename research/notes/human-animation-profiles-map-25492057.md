# Human pose, locomotion and idle profiles — build 25492057

This pass connects equipment, movement, target and health inputs to the exact
animation names chosen by the human FSM handler. It is static evidence, not a
callable ABI or a claim that all animation dependencies are exhausted.

Addresses below are RVAs; member offsets are hexadecimal; virtual slots are
decimal. Fingerprints and instruction evidence are in
[the manifest](../artifacts/build-25492057/human-animation-profiles-map.json), with
[structural validation](../artifacts/build-25492057/human-animation-profiles-validation.json).
The [declaration catalog](../artifacts/build-25492057/human-animation-catalog.json)
records every base human animation declaration and the native name join.

## Evidence and scope

Game: 172 exported functions, 169 exact candidates. Editor: 148 functions, 147
exact candidates. All decompilations completed. Reviewed contracts/slices cover
30 game functions and 26 editor counterparts, 23 fields, 260 exact
token-constructor bindings and 8,417 instruction locations. LLVM independently
agrees on 5,134 instruction starts in 43 selected functions, after the existing
standalone LOCK-prefix normalization. Two short leaf initializers not assigned
Ghidra functions were inspected with LLVM and preserved as exact byte sequences.

The new resource parser passed six tests covering literal Windows paths, nested
events, include/default ordering, duplicate preservation, malformed declarations
and include cycles. Three archive-member hashes are checked. Structural checks
do not prove the semantic interpretations below or resource availability after
mods and mounted overrides.

Important counterparts:

| Contract | Game | Editor |
|---|---:|---:|
| Handler Quant | `95FF30` | `7E76A0` |
| Idle selection | `961DC0` | `7E9530` |
| Idle enable/disable | `9623C0` | `7E9AD0` |
| Pose dispatch | `96A4B0` | `7F2340` |
| Stand / squat / lie profiles | `96A730 / 96AE00 / 96B120` | `7F2570 / 7F2C10 / 7F2EE0` |
| Cache refresh / base apply | `96B410 / 96B450` | `7F3160 / 7F31A0` |
| Walking profile | `973010` | `7FAF50` |
| Walking handler | `971CA0` | `7F9B70` |
| Fire-while-walking tag predicate | `938910` | `7C00B0` |
| Queue request, scaled / plain | `968DE0 / 962B50` | `7F0A60 / 7EA410` |

## Inputs and mutable state

The equipment class at handler `424` and modifier at `42C` come from the tag
bindings in [human FSM actions](human-fsm-actions-map-25492057.md):
0 nothing; 1 bazooka; 2 machine gun; 3 short SMG; 4 SMG; 5 rifle; 6 pistol;
7 flamethrower; 8 small one-hand item; 9 one-hand item; 10 two-hand item;
11 carried human; 12 flag. Modifier 5 is binoculars.

`95FF30` refreshes handler flag `30` bit14 from actor `DC8` brain selected subject
`3B8 != null`. It clears bit16, then sets it when vitality's effective health
(`925010`) divided by maximum (`924F90`) is below float `0.3000000119`.
The decompiler's apparent second health argument is leftover RDX, not evidence
of a second meaningful argument. These flag writes do **not** themselves set the
profile-dirty bit15 in this function.

`96B410` rebuilds only when bit15 is set or pose selector `278` changes. It records
the selector, clears dirty and calls `96A4B0`. Selector values are 0 stand,
1 snipe, 2 squat, 3 lie. This numbering differs from the chassis/bone pose domains
already mapped. Other values leave the output unchanged.

Constructor `95F7F0` clears both flags `30` and variation counter `34` with a
single eight-byte write. Standing and squatting pistol profile recomputation
increments `34`; walking profiles read it. These selectors are not pure queries.
The tables below describe nonnegative counter values; native signed-remainder
branches can leave defaults/empty outputs after negative values or overflow.

Bit3 chooses strap/stowed poses. `9622C0` is one producer predicate: class must
be 0, an inventory item must match token `147B220`, the item must have a world
entity at `60`, and that entity's stuff extender `560` must have holder `80`
equal to the actor's inventory. It is an attachment relation, not just ownership
or the presence of any item in the inventory. Complete bit3 writers remain open.

## Six-token pose profiles

All profile branches start by clearing six `60`-byte tokens. `96B620` copies them
to handler `38` through `218`:

| Output offset | Handler offset | Role |
|---:|---:|---|
| `0` | `38` | base first |
| `60` | `98` | base second |
| `C0` | `F8` | aim first |
| `120` | `158` | aim second |
| `180` | `1B8` | recoil first |
| `1E0` | `218` | recoil second |

“First/second” is deliberate: standing often uses upper/lower names, but lying
often puts a whole-body animation in the second slot. Consumers select queues;
the layout is not universally two body halves.

Standing (`96A730`):

| Class | Base and aim selection | Recoil |
|---|---|---|
| 0, 8 | first `pose_stand_empty_1`, or `pose_stand_strap_1` with bit3; second `pose_stand_down` | empty |
| 1 | base second `pose_stand_bazooka`; aim remains empty | empty |
| 2 | `pose_stand_fire_mgun_up/down` | `stand_fire_mgun_up/down` |
| 3 | `pose_stand_fire_gun_short_up/down` | `stand_fire_gun_short_up/down` |
| 4 | `pose_stand_fire_gun_up/down` | `stand_fire_gun_up/down` |
| 5 | `pose_stand_fire_rifle_up/down` | `stand_fire_rifle_up/down` |
| 6 | pistol variation rules below | corresponding pistol recoil |
| 7 | same poses as class2 | empty |
| 9 / 10 / 11 / 12 | base second `pose_stand_1hand / pose_stand_2hand / pose_stand_corpse / pose_stand_flag`; aim empty | empty |

For classes0/8 with binocular modifier5, aim first becomes `pose_watch_binocular`
and aim second becomes empty; base slots keep their earlier selection.
After **every** standing class branch, `(flags30 & 14000) == 10000`—low health
and no selected subject—overwrites base second with `stand_after_body-hit`.

Pistol plain helper `96B790` writes `pose_stand_fire_pistol_up/down` to base and
aim, and `stand_fire_pistol_up/down` to recoil. Helper `96B720` uses the `_2`
versions of all four names. After incrementing counter34: with a subject,
remainder0 selects plain and 1/2 select `_2`; without a subject, 0 selects plain,
1 selects `_2`, and 2 retains plain aim/recoil but replaces base with
`pose_stand_empty_1` / `pose_stand_down`.

Squatting (`96AE00`) leaves first slots empty:

| Class | Base second and aim second | Recoil second |
|---|---|---|
| 0, 8 | `pose_squat_empty` | empty |
| 1 | `pose_squat_bazooka` | `squat_fire_bazooka` |
| 2, 4 | `pose_squat_fire_gun` | `squat_fire_gun` |
| 3 | `pose_squat_fire_gun_short` | `squat_fire_gun_short` |
| 5 | `pose_squat_fire_rifle` | `squat_fire_rifle` |
| 6 | `pose_squat_fire_pistol` or `_1` | corresponding `squat_fire_pistol` or `_1` |
| 7 | `pose_squat_fire_gun` | empty |
| 9–12 | empty | empty |

Binocular modifier5 changes only aim second to `pose_squat_watch_binocular`.
Pistol counter increments: without a subject odd chooses plain (`96B6E0`), even
chooses `_1` (`96B6A0`); with a subject remainder0 chooses plain, 1/2 choose `_1`.

Lying (`96B120`) also uses second slots. Classes0/8 use `pose_lie_empty`, with
binocular aim override `pose_lie_watch_binocular`. Classes1/2/3–4/5/6 use
`pose_lie_bazooka / pose_lie_mgun / pose_lie_gun / pose_lie_rifle / pose_lie_pistol`
for base and aim, and corresponding `lie_fire_*` recoil. Class7 uses gun poses
without recoil. Classes9–12 remain empty.

Snipe selector1 in `96A4B0`: classes0/8 put `pose_stand_empty_1` in base second
and aim second, or `pose_watch_binocular` in both for modifier5. Classes2–5/7 put
`pose_snipe_gun` in both second slots. Pistol puts `pose_snipe_pistol` in first
base/aim and `pose_stand_gun_down` in second base/aim. Other classes fall back to
the standing selector, including its health override. No snipe recoil is filled.

`96B450` refreshes the same cache. If base second is `stand_after_body-hit`, it
sets actor1420 bit3, starts that animation at scale1 with callback0 and variant−1,
then sets next initial-idle delay3A4 to0. Otherwise it applies both base tokens
through `961360`, reviewed in the prior handler pass.

## Walking selection and application

`973010` reads chassis virtual slot40 (`+140`) and class424. The numeric modes
0–7 below are recovered values; animation names alone are not an enum binding.
Output is two booleans at0/1 and name tokens at8/68. The caller initializes these
to false/empty. Unknown modes return without filling names in the game; editor
`7FAF50` contains unreachable-code diagnostics in those branches.

| Class group | Mode | First / second name |
|---|---|---|
| 0,8 | 0 | `run_slow_empty_up / run_slow_down`; bit3 substitutes `run_slow_strap_up / run_slow_down_1` |
| 0,8 | 1 | `run_empty_up / run_down`; bit3 substitutes first `run_strap_up` |
| 0,8 | 2 | `run_fast_empty_up / run_fast_down`; bit3 substitutes first `run_fast_strap_up` |
| 0,8 | 3,6,7 | `walk_empty_up / walk_down`; bit3 substitutes first `walk_strap_up` |
| 0,8 | 4,5 | `run_bow_tocover_up/down` |
| 1 | 0 | `run_slow_gun_up / run_slow_down_1` |
| 1 | 1 | `run_gun_up / run_down`, or with walking-fire tag `pose_stand_bazooka / walk_down` and first-is-pose |
| 1 | 2 | `run_fast_gun_rhand_up / run_fast_down` |
| 1 | 3 | `pose_stand_bazooka / walk_down`, first-is-pose |
| 1 | 4,5 | `run_bow_gun_tocover_up/down` |
| 1 | 6 / 7 | `pose_stand_bazooka / walk_spot_gun_down_1` or `walk_spot_gun_down`, first-is-pose |
| 2–5,7 | 0 | `run_slow_gun_up / run_slow_down_1` |
| 2–5,7 | 1 | `run_gun_up / run_down`, or tag-enabled `walk_normal_{mgun,gun_short,gun,rifle,gun}_up/down` respectively |
| 2–5,7 | 2,4 | `run_bow_gun_tocover_up/down` |
| 2–5,7 | 3 | `walk_gun_up / walk_down` |
| 2–5,7 | 5 | `run_bow_gun_up/down` |
| 2 | 6 | `walk_spot_mgun_up/down` |
| 3–5,7 | 6 | counter%3: 0 `walk_spot_gun_up/down`; 1 `walk_spot_gun_up_2/down_2`; 2 `walk_spot_gun_half-mast_up/down` |
| 2–5,7 | 7 | `walk_spot_gun_up_1/down_1`; other-than-class2 with chassis1C bit7 clear and odd counter uses `walk_spot_gun_belt_up/down` |
| 6 | 0 | `run_slow_empty_up / run_slow_down` |
| 6 | 1 | `run_empty_up / run_down`, or tag-enabled `walk_normal_pistol_up/down` |
| 6 | 2 | `run_fast_empty_up / run_fast_down` |
| 6 | 3 | `walk_empty_up / walk_down` |
| 6 | 4,5 | `run_bow_tocover_up/down` |
| 6 | 6,7 | `walk_spot_pistol_up/down` |
| 9 / 10 / 11 | any | first `walk_1hand / walk_2hand / walk_corpse`; single-name flag |
| 12 | any | `run_flag_up/down` |

`938910` checks tags from the equipped definition (`938830`): chassis18 nonzero
selects `shoot_walking_manual`; otherwise a nonnull brain whose398 low2 bits
equal1 selects `shoot_walking`; otherwise `shoot_walking_ai`. It returns whether
that exact token occurs in the definition's318..320 token vector. Helpers
`83FB70` and `7EF670` return **AL booleans**; their decompiled full-EAX expressions
contain unrelated upper bits.

For `walk_all` part0, `971CA0` compares booleans and both names against cache490,
491,498,4F8. An unchanged profile uses request102 (loop+resume), changed uses101
(loop+start). A first-is-pose name uses `962B50`, request1 and scale
`14776C8 * 0.2` (file-initial scale gives200 native duration units). Other names
use `968DE0` and movement scale27C. Single-name suppresses the second request.
The separate `walk` part0 branch resumes only the second name when applicable,
then clears the profile cache. `walk_start`/`walk_stop` construct suffixed names;
their absence from the base FSM graph was recorded in the previous pass.

`968DE0(queue,name,scale,flags,callback,multiplier,referenceName)` appends a68-byte
row with multiplier at60 and passes `scale*multiplier` to animation request.
If referenceName is nonempty and has an active controller, its slot15 variant
index is reused; otherwise variant−1 permits normal factory selection. Assembly
confirms the final stack argument is replaced with the recovered index. The
callback stack argument is preserved. `962B50` records row multiplier0, passes
the supplied scale unchanged and performs the same reference-variant lookup.

## Idle, repair and turning updates

`9623C0(true)` clears flags12/13 and chooses idle bits8..10: 1 normally, 2 when
health bit16 is set. If float3A4 is positive, it sets idle deadline3A0 to clock
plus converted3A4, then resets3A4 to1. Disabling clears idle bits and3A0; a
nonempty idle name is canceled, otherwise empty-idle deadline3AC is cleared.

The clock reader `65E700` reads1556E48. Converter `65E730` multiplies XMM0 by
**writable** float14776C8, then truncates with CVTTSS2SI. The file-initial value
is1000. Timings below are converter-input units; pause, time scale and full
counter production are not established by this pass.

When3A0 expires, Quant clears it and runs `961DC0`. That function clears name340
first. When global1483008 is clear it looks for actor interaction event
`select_idle` in definition30..38. A found event bypasses native selection:
context118 receives right-hand item definition from `7CCDE0`, context188 bit21 indicates
nonnull, and `7705E0` dispatches the event. Its concrete action list is still open.
Without the event, native selection only runs when chassis actual type2C is0.

For idle mode1, random float `U` comes from engine context `554B50`: update state8
with `state*7FF8A3ED+2AA01D31`, increment draw counterC, then convert the high24
bits using15885A4. Leaf initializer69980 sets that scale to1/2^24.

| Equipment class | Native idle selection |
|---|---|
| 2–5,7 | if U<0.1, a second draw chooses `stand_look_around_gun` or `_1` at0.5 |
| 8 | U<0.7 selects `stand_idle_empty` |
| 9 | U<0.1 selects `stand_idle_1hand` |
| 1,6,10–12 | remains empty |
| default, including0 | U<0.7: without bit3 `stand_idle_empty`; with bit3 the **same draw** chooses strap1 below0.5, strap2 below0.65, strap3 below0.695, else strap4 |

Mode2 selects `stand_after_body-hit`. The local “previous name” is copied **after**
the output is cleared, so the reviewed native loop does not demonstrate avoidance
of the actual preceding idle. Do not infer a previous-animation memory from its
comparison alone.

Nonempty idle sets actor1420 bit3 and requests start1, scale1, variant−1. Callback
is `(~(flags30 >> 14) & 4)`: **4 unless health bit16 is set, then0**. Bit14 is the
target flag, but this expression tests original bit16 after shifting; it is not
a target-presence gate. Empty selection schedules3AC at clock+convert(2+2U) for
mode1, otherwise clock+convert(3+5U). When due, it emits entity event6, subkind4,
null name. Completion4 (`9605C0`) reschedules3A0 with converted0 if idle mode is
still enabled. This is immediate clock-relative scheduling, not a recovered
fixed pause. Deadline0 remains the inactive sentinel.

Repair deadline3A8 similarly selects `squat_repair_1` or `_2` at U<0.5, writes
name280, and requests start1/scale1/callback5/variant−1. Completion5 reschedules
3A8 with converted0 only while handler flag11 remains set.

The remaining Quant branch requires chassis48 bit4 and reference bone558. It
computes the wrapped angular difference between chassis velocity20/24 and that
bone's world axisA8/AC (updating the dirty bone first). For each queued movement
name, it reads controller slot11 parameter0, converts degrees to radians, clamps
the step using `integerDelta1556E4C * 1547DB8 * 360`, wraps, then writes slot10
parameter0 in degrees. It has a separate near-opposite-angle correction. This
feeds animation interpolation; it is not itself a target-choice decision.
Initializer2FBC0 sets1547DDC=180/pi; leaf2FC60 sets1547DE0=1/1547DDC;
2FA60 and4C0C00 set1547DB8=1547DE0/14776C8. With initial scale1000 the clamp is
nominally360 degrees per1000 counter units. Full clock writers and bone558
binding remain open.

## Base resource join

`map_human_animations.py` expands `human_anm.ext` and its two includes in order,
preserving literal path separators and each source line. It records1339 sequence
and206 blend declarations:1545 distinct case-insensitive names, no duplicates.
There are224 definitions with events,790 event rows and34 distinct event names;
69 movement,177 resume,13 pinback and23 dynamicSpeed declarations. Properties are
recorded in source order; dynamicSpeed is kept as source intervals, not asserted
to equal the compiled runtime pair array.

The native selectors in this pass reference125 distinct animation names.123 have
base declarations. `stand_idle_empty` and `stand_idle_strap_4` do not occur in
these three text resources. That does not prove they are absent after automatic
binary loading, another definition source or mounted overrides. Resolve the
loader before treating this as a game bug.

Examples tying earlier contracts together: `pose_stand_bazooka` is a blend using
the beginning of `weapon\anim_bazooka_stand_shoot_all`; `stand_after_body-hit` is
a sequence with a dynamic-speed interval9..10 at0.12; the half-mast walking
lower sequence has left/right leg markers at10/40. Marker names are delivered
through event5 as mapped in [animation control](animation-control-map-25492057.md).
Their names alone do not establish damage, sound or inventory effects.

## Remaining static leads

- Resolve automatic animation registration/file lookup, binary timeline parsing
  and mounted overrides, particularly the two unmatched idle names.
- Trace `select_idle` action implementations and command2F synchronization.
- Bind movement mode enum names; finish speed27C, dirty/target/health flag writers,
  variation counter writers, and reference bone558 producer.
- Map death feasibility/selection, throwoff/landing and their animation choices.
- Continue concrete marker action lists, bone/root transforms and deferred mesh
  drain; full clock/RNG context and deterministic replay remain open.
