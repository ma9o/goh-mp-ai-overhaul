# Human FSM action handlers — build25492057

Static continuation of [FSM routing](fsm-routing-map-25492057.md) and the
[expanded base graph](human-fsm-graph-25492057.md). Addresses/offsets are hex;
virtual slots are decimal. No runtime validation. The manifest distinguishes
reviewed helper contracts from partial reviews of large handler families.

## Dispatch and graph coverage

Human handler slot3, game960970/editor7E8100, passes `(handler, part, name,
entering)` through12 families in this order and stops at the first true result.
Assembly preserves all four arguments, including the first call that Ghidra
displays with an empty prototype. A recognized branch commonly returns true even
when its equipment switch performs no action. This return means the callback
name was handled, not that an order has succeeded.

| Order | Family | Game | Editor | Selectors and part gate |
|---:|---|---|---|---|
|0|Standing|96F1A0|7F7020|27; part<0|
|1|Walking|971CA0|7F9B70|5 whole names;5 part0 names|
|2|Squatting/repair/healing|96D390|7F5170|34; part<0|
|3|Lying/crawling/healing|966CA0|7EE820|32; part<0|
|4|Upper split part|96C0E0|7F3E40|16; part==1|
|5|Looking/peeping from cover|968ED0|7F0B50|26; part<0|
|6|Snipe/cover pose|96B800|7F3550|12; part<0|
|7|Swimming|971380|7F9250|6; part<0|
|8|Boarding|9632B0|7EAB80|4; part<0|
|9|Death/knockdown/throwoff|963500|7EADD0|14; part<0|
|10|Pickup/drop|966640|7EE170|8; part<0|
|11|Standing bazooka|962DA0|7EA660|6; part<0|

All195 `(name,part gate)` selectors match between paired game/editor families.
Internal branch ordering is not claimed identical. The manifest records each
token/literal, exact constructor-call argument instruction sites, and a handler
join for all220 base states. Of174 whole-name selectors,170 occur in the graph.
`walk_start`, `walk_stop`, `unboarding_to_ground` and `unboarding_to_water` are
native selectors absent from these base declarations; other resources/callers
remain leads.

Fifty base names have no whole-name handler. Many use split handlers:
`stand_reload`, for example, uses part1=`reload`. Eight split combinations have
no selector: part0=`linked`, `snipe_aim`, `snipe_bolt`; part1=`linked`, `aim`,
`fire`, `move_fire`, `switch_begin`. This is not a missing-function verdict.
Graph tags, persistent controllers and other parts can give a state behavior
without an entry callback for every name. Recognizing `fire` is distinct from
creating a shot.

`lie_revive` is both a native whole-state selector and a base state, but its tags
are `revive` and `down`. The default subset routes request `revive` from `lie`
directly to `lie_revive`; the separate request named `lie_revive` has no matching
tag. Entry starts `lie_medic_revive`, sets handler30 bit0 and actor1420 bit3;
exit cleans the primary animation queue. Special request-name callers remain open.

## Completion and cleanup boundaries

961280 adds its animation name to the FSM completion context, then submits to
queue430 with mode1, scale in XMM2, completion subkind3 and no explicit slot name.
9612F0 does the same for queue470. Editor counterparts are7E89D0/7E8A40. The
omitted second argument to7FCAC0 in decompilation is present in RDX. Completion
uses entity event6/subkind3. This differs from961360/961480 blending, which uses
activeBlends and subkind6.

961540 clears actor1420 bit3, then cancels queue430/450/470 for group0/1/2.
962C30 sends cancellation for every68-byte row name, destroys names and clears
vector length.5CDD60 translates cancellation to manager5CD380 mode8, scale1,
callback0, slot-1; it does not directly call a controller destructor. Mode8
suppresses creation if no controller exists. Existing controllers receive slot5;
if it returns0, the manager emits any old nonzero completion callback before
clearing it. Concrete cancellation and reentrant queue mutation remain open.

961240 forwards to5CDD30, which adds bit2 (`flags|4`) and calls the same manager.
Values such as200/400/1400/4400 accompanying a name are integer mode flags here,
not durations. Their complete meanings require concrete animator review.

Whole `walk` and `throwoff` exits inline queue cancellation/clear instead of
961540; they do not clear actor1420 bit3 in that branch. Swimming idle exit
cancels/removes only its selected idle name. Repair exit clears handler30 bit11
and cancels the name at280. Exit paths are not interchangeable.

## Equipment and hand animation inputs

939C10 obtains equipment tags through938830 and supplies them to95FAD0.938830
returns the equipped definition's EC8 container, or a shared empty container.
When tags contain `weapon`,939C10 updates actor13C8 with that definition; its
nonweapon branch does not clear this cache. Callback replacement/lifetime is
not settled by this function alone.

Handler event2E derives428 from incoming tags; event2F derives420. GEM names424
**currentStuff** and428 **mountingStuff**.420 chooses the impending pickup's box
variant. These are tag-derived class IDs, distinct from entity IDs. The13
classes/six modifiers were bound in the previous pass. The main setter clears
420/428 and marks pose-cache dirty bit15 in30.

961710 specifically updates finger animations:

| Classes | Names requested |
|---|---|
|0,1,11,12|fingers_bare_l / fingers_bare_r|
|6|fingers_pistol_l / fingers_pistol_r|
|7,8,9|fingers_bare_l / fingers_item_r|
|10|fingers_box_l / fingers_box_r|
|2–5|No request in this function|

Each request adds a completion barrier and uses primary-group mode1/callback3.
The body-pose cache is separate:96B410 calls96A4B0 only when bit15 is set or
selector278 changes. Selectors0/1/2/3 are standing/snipe/squat/lying. Cached names
38/98, F8/158,1B8/218 feed base, aim and recoil layers. The complete96A4B0
equipment/pose matrix remains open.

961590 forwards null through7CD980 to inventory helper7CAE50, then sets actor1420
bit3. The forwarded pointer's full meaning remains open; do not name this a
weapon switch from its callers alone. It occurs on inventory-out, mounting
belt/pistol-out and bazooka pickup paths.

## Reload, bolt and throw decisions

Split `reload`, `squat_reload` and `lie_reload` derive an integer interval from
selected weapon: chargerB0→10 minus current clock65E700, minus definition1538.
The latter is `ReloadAnimBonus`: combat-pass parser8299E0 caseEE converts the
authored value through65E730 and writes1538; token initializer5B090 binds that ID.
A zero charger10 substitutes sentinel1556DE8 for the clock subtraction; no
weapon initially leaves the sentinel. Interval is compared with writable147D69C,
file-initial1; no direct WRITE xrefs were found. With a selected animation and
interval at least the threshold, scale is
`animation_definition.slot3(1.0) / float(interval)`, otherwise1. Assembly confirms
XMM0 float return and signed interval-to-float conversion.

Split reload chooses stand_reload_mgun for classes2/7, stand_reload_gun for3/4/5,
stand_reload_pistol for6; `piatrocket` replaces the gun name with stand_reload_piat.
Squat adds class1 bazooka; lying separates machine gun/bazooka names. Unload uses
reload names at scale-1. The standing bazooka-specific family uses fixed scales
1/-1 without the interval calculation in that branch.

Bolt requires class5 and selected-weapon definition tag `bolt_action`. Weapon
slot3 and7C9270 distinguish open versus ordinary bolt names. Split uses
stand_bolt_rifle_open_up for a zero result, otherwise stand_bolt_rifle_up; squat
and lying have their own pairs. The detailed amount-helper contract remains open.

970BE0 gets equipped definition, selected weapon and target via83DAA0 at weapon+C8.
It computes Euclidean distance from actor74/78/7C and multiplies by1547D90.
Initializer2FA60 and refresh4C0C00 set that multiplier to1/14776C0, whose
file-initial value is20. This proves conversion, not a current value/meters label.

For `m24`, `f1`, `mk1`, `m39`, `m61`, ordered checks return variant0 for0≤d≤5,
variant1 for5≤d≤10, variant3 for d<10 or d>20 after those checks, otherwise an
RNG-selected1/2. Boundary5 therefore selects0;10 selects1. `rpg40`, `rpg43`,
`pwm1` choose1 through10 else3; `m24x5` uses5. Remaining cases, including the
`molotov_coctail` tag scan, return1. Finite-distance assumptions are not a guard.

Throw-begin stores the variant in handler30 bits5–7; throw-end reuses it.
Standing maps0/1/2/3 to grenade_1/throw-side/grenade_2/grenade name pairs. Squat
and lying group0/1 versus2/3. Knife/axe standing variants use an RNG choice in
the same bits; chicken uses a fixed grenade-like animation. The helper may call
weapon Ready/Valid routines; GEM asserts `weapon && weapon->Ready() &&
weapon->Valid()`. It is not established as a passive query for arbitrary actors.

## Pose, cover and carried-entity effects

95FC00 forwards requested movement type/reason to chassis slot49 with fourth
argument0, then calls slot50. Stand/snipe/squat/lie entry supplies0/0/2/1;
swim_idle supplies3. Bone pose937FE0 is a separate domain: stand/snipe0, squat1,
lie2, swim3; transient operations often use4. These domains also differ in the
temporary firing-trial map.

Look/peep entry calls898D70 on Cover and selects an animation; stable states
call899460 with the corresponding cover-stance name. Native look_left uses
`rlook_*`, look_right uses `llook_*`; preserve that reversal. Exit uses scale-1.
Generic look supports0/8 item,2/3/4/5/7 gun,6 pistol, diagnosing others; peep uses
fixed names. This callback does not select or score a cover location.

With currentStuff11, stand_drop resolves the held human, requests its `dead` tag,
updates it and ticks its FSM before the carrier's animation. stand_pickup_end
requests its `corpse` tag; GEM asserts a held `deadHuman`. Pickup-begin expects
currentStuff0 in GEM and420 selects box versus small animation. Physical item
transfer remains in the operation/event callbacks of equipment-actions.

Before swimming,971C10 handles classes1/9/10/11/12. It reads the right-hand item
and definition hand-only predicate7E4F60. Non-hand-only goes through7C98D0 into
its existing box, detaching its holder and refreshing the item; hand-only uses
93AA10's drop/synchronization path. Water entry therefore has inventory effects.

Death-family branches select/store actor12D0, including a swamp-specific lying
death.938C60 falls back to stand_die_back_2 if that name is empty. Throwoff uses
actor1428/1488, then derives an `_end` name. Dead applies stored death-animation
flags, optionally blends pose_die_water for movement type3, then requests type1.
Feasibility, directional selection and synchronized application helpers remain open.

## Editor evidence and validation

GEM adds invariants: walking expects MT_NORMAL=0; crawling expects MT_CRAWL=1;
reload's animation must exist; pistol-out under owner `mount` expects
mountingStuff==WEAPON_PISTOL; pickup-begin expects currentStuff==NOTHING; corpse
handling expects a human; several throw branches require nonempty equipment or
animation. These describe intended conditions, not game-side fallback safety.

Assembly rejects decompiler artifacts: walking exit's apparent logging-string
writes are absent; an empty queue branches to the shared success epilogue.
Undefined high halves of stack values do not establish undefined int32 callback
arguments. The reload definition's duration is returned in XMM0.

Frozen exports contain380 game/49 editor functions; all candidates are exact and
all selected functions decompile. The manifest has40 game contracts/slices,
26 editor counterparts,33 fields,634 exact token constructor bindings and23612
instruction sites. Independent LLVM decoding of43 functions matches21753
instruction starts after standalone LOCK normalization. Structural validation
passed: binary hashes, entries, instruction sites, two RTTI tables,637 byte
sequences, seven float constants and16 resource hashes. It is not semantic,
ABI or completeness proof.

Continue full pose/walking matrices96A4B0/973010, idle/repair animation updates,
death selection938410/9648E0/9645D0, concrete animator Quant/cancel/destructors,
actor1420 bit3 and handler-flag consumers, callback/entity lifetime and the
remaining frontier. Large family contracts retain these helper boundaries.
