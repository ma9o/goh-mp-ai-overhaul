# Scripted idle policy and native entity predicates — build 25492057

The installed human interaction definition supplies a `select_idle` handler, so
the native fallback described in the previous pass is not the complete base idle
policy. This pass expands that script and maps every condition/action it uses to
native reads and writes. It also resolves the concrete action-list implementation
behind the previously unresolved `761B50` dispatch.

RVA/member offsets are hexadecimal; virtual slots are decimal. See the
[manifest](../artifacts/build-25492057/idle-actions-map.json),
[validation](../artifacts/build-25492057/idle-actions-validation.json), and
[expanded decision tree](../artifacts/build-25492057/human-idle-policy.json).
No game code was executed and no binaries were modified.

## Evidence

Game exports:79 functions/71 exact candidates. Editor:65 functions/57 exact
candidates. All decompilations completed. The manifest records39 game contracts
or slices,28 editor counterparts,17 fields,28 RTTI/vtable checks,24 name/factory
registrations,49 byte checks, and2,573 instruction locations. Independent LLVM
disassembly agrees with1,620 instruction starts in45 selected functions.

Five extractor tests cover empty-branch ordering, named macro arguments,
negation/multiple descriptors, missing arguments and unknown conditions. The
base script expands two macros/eight invocations into70 conditional branches
and13 unconditional else branches. It names21 distinct animations,20 matching
the base animation declaration catalog. Archive hashes cover the include chain
`set/interaction_entity/entity.set` → `human.inc` → `human_idle.inc`.
Mounted overrides and assignment of an interaction definition to every actor
subtype remain separate questions.

## Exact condition inputs

Conditions execute through virtual slot1; slot2 parses their arguments. Actor
condition wrapper `757450` checks context entity0 nonnull and entity30 bit1,
then passes the entity in **R8** to slot3. Human wrapper `757500` uses bit2.
Both return AL=false if the guard fails. Editor counterparts `5D5ED0/5D5FD0`
also assert that the context entity is nonnull. Return upper-register garbage
in some decompilations must not be treated as a boolean value.

| Condition | Game / editor implementation | Actual read |
|---|---|---|
| `standing` | `757BD0 / 5D6740` | actorDE0 chassis2C equals0 |
| `lying` | `757B90 / 5D6700` | chassis2C equals1 |
| `squatting` | `757C10 / 5D6780` | chassis2C equals2 |
| `swimming` | `757B50 / 5D66C0` | chassis2C equals3 |
| `in_cover` | `757C50 / 5D67C0` | actorDC0 cover24 is nonzero |
| `cover_type` | `757D50 / 5D68C0` | cover24 must be nonzero, then cover20 must occur in the condition's int32 vector8..10 |
| `idle_low_health` | `757E20 / 5D6990` | human FSM handler30 idle mode bits8..10 equal2 |
| `hold_strap` | `757E80 / 5D69F0` | handler30 bit3 |
| `no_stuff` | `7563B0 / 5D4A30` | context188 bit21 is clear |
| `stuff` | `75A4E0 / 5D4880` | bit21 present, then context118 definition'sEC8 tags satisfy a descriptor |
| `rand` | `756410 / 5D4A90` | a new RNG float is strictly below condition float8 |

`idle_low_health` does **not** call vitality or test health flag16. It tests the
idle mode previously chosen by `9623C0`; that mode can lag the next health
refresh. `hold_strap` likewise consumes the existing handler bit rather than
rescanning inventory. `938A20` gets the handler through actorDB8 FSM+E88.

`757C90` parses one or more quoted cover names; `8A9720` uses case-insensitive
lookup in the thirteen-entry table at1482170. Unknown names map to0 (`none`).
It does not reject an unknown spelling. `cover_type` still requires cover24
nonzero even when the requested kind is0.

`7562A0` parses quoted `stuff` descriptors into330-byte records using space as
the tag separator. Descriptors are alternatives: any descriptor may match.
Within one descriptor, `556F20` requires **all** tags to occur in the definition's
318..320 vector. Thus `"pistol weapon"` requires both tags. These script tests
operate on tags directly, not on the handler's priority-derived equipment class.

`961DC0` puts the right-hand **item definition** in context118, not an inventory
item object. `7CCDE0` follows the right-hand location and returns occupied
item40 when item48 count is nonzero. It returns null if unavailable. The
context's bit21 tracks this pointer. This sharpens the previous pass's shorthand
“right-hand item”: consumers addEC8 to a definition pointer.

## Action execution and RNG side effects

`set_idle_animation` registration is the name/factory pair at147FC70 → `747E10`
(editor12F70D0 → `5C3B10`). `Interaction::eAction_SetIdleAnimation` owns a vector
of60-byte name tokens at28/30/38. Parser `745210/5C0AE0` appends every quoted name.

Its action gate `743C30/5BF4F0` requires a nonnull context entity with entity30
bit2, then tail-calls slot9 with the entity in R8. Slot9 `745290/5C0B60`:

1. Empty vector: return without changing the handler.
2. Nonempty vector ofN entries: advance current RNG state8 with
   `state*7FF8A3ED+2AA01D31`, increment draw counterC, and choose
   `(state >> 8) % N` using unsigned division.
3. Copy that name into handler340 through `938A20` → `5564F0`.

Even N=1 consumes a draw. The action neither checks whether the animation exists
nor starts it. Handler Quant `95FF30` performs the later request and completion
logic mapped in [human animation profiles](human-animation-profiles-map-25492057.md).
Lists with multiple entries are not proven statistically independent of earlier
`rand` tests; both consume the same deterministic generator.

The random condition parser `756470` stores a float without clamping. Evaluation
always advances the generator, converts its high24 bits, and compares U<p.
Leaf initializer4CA40 sets scale1571AC8=1/2^24. Hence `rand 1` succeeds for the
normal initialized scale but still consumes a draw; it is not a no-op condition.

## Ordered branches and the interaction event chain

`Interaction::eAction_If` has a vector at28/30/38 of18-byte rows:
condition pointer0, inversion byte8, complex-action pointer10. Parser
`7595E0/5D3870` recognizes optional `not`, resolves condition factories by
case-insensitive name, calls condition slot2, then parses an owned complex action.
Quoted flag conditions exist in the parser but are not used by this idle script
and their concrete implementation is not reviewed here.

Executor `759560/5D37F0` considers a null condition true and otherwise calls its
slot1. The **first** row where truth differs from inversion executes its complex
action and returns. A matching empty action therefore stops the entire else
chain. This is why the extractor preserves no-op branches.

Complex action parser `7524F0/5CF210` resolves action factories, records their
names, appends them to the global action-ID registry1571B18, calls slot5 to parse,
and stores action pointers in vector28/30/38. Executor `31C730/21C250` walks that
vector in source order and calls each child's slot2.

The previously unresolved `761B50` is an event-object adapter to an embedded
`Interaction::eComplexAction` at offset10. Constructor `7618A0/5DFFF0` installs
that vtable; event byte0 defaults to1 (`inherit`), while `overload` sets it0.
`76AE80` selects the most-derived available event layer, invokes the preceding
layer first when inheritance is enabled, writes context188/189 layer/event
bytes, and calls the embedded action.

Around that call, `76AE80` changes RNG context with `554CE0`. Entity3A bit0 clear
selects `6FBD60`, whose literal name is `synced_global`; set selects `6FBE90`,
named `entity_local`. The previous context is restored if nonnull. The names are
recovered facts; seed origin, replay guarantees and that entity flag's producers
remain open. This event-scoped RNG may differ from the context used by the native
idle fallback outside an interaction event.

Common action wrapper `1AFA20` contains an `eIASyncCommand`4C branch controlled by
virtual slots6/7/8. For the concrete idle action, if action and complex action
reviewed here, slots7/8 both return false (`31C790/31C7A0`), so that branch does
not submit a command for these types. This is not a claim that all interaction
actions or the surrounding event are unsynchronized. Dynamic receiver followup
`766ED0`, registry teardown and other network paths remain separate.

## Base script policy

The ordered tree in the JSON artifact is authoritative for exact nested branches;
the following describes its outcomes. Equipment descriptions below mean tag
tests on context118, not the numeric equipment class.

In cover, `look_left`, `look_right` and `look_2way` stop with no selection before
any posture test. This avoids an idle replacing the corner-peeking pose.

| In-cover posture | Selection |
|---|---|
| Standing, no stuff / small one-hand item / one-hand item / pistol+weapon | `hide_stand` → `idle_cover_stand_hide`; otherwise `idle_cover_stand_empty` |
| Standing, rocketlauncher+weapon | empty |
| Standing, remaining tags | `hide_stand` → `stand_look_around_gun`; otherwise `idle_cover_stand` |
| Squatting, `hide_squat` | `idle_crouch_gun`, before all equipment tests |
| Squatting, small one-hand / one-hand / pistol+weapon | `idle_crouch_empty` |
| Squatting, rocketlauncher+weapon | empty |
| Squatting, remaining cases | `idle_cover_crouch`; notably there is no explicit no-stuff branch here |
| Lying | random gate0.5, then shared empty/gun choice below |
| Other posture, including swimming | empty; does not fall through to the out-of-cover branches |

Outside cover, standing checks `idle_low_health` first and does nothing if true.
The apparent low-health animation line is commented out in the installed script.
Next, `mgun weapon`, `smg weapon`, `rifle weapon` or `flame_thrower weapon` uses
`rand 1`, followed by an RNG choice between `stand_look_around_gun` and `_1`.
This differs substantially from the native fallback's0.1 gate.

Small one-hand items use gate0.7 then `stand_idle_1hand`; one-hand items use0.1
and that same name. Rocketlauncher, pistol, two-hand item, human and flag branches
select nothing. The final branch uses gate0.7: `hold_strap` chooses from
`stand_idle_strap_1..4`, otherwise from `stand_idle_empty_1..4`.

Outside-cover squatting and lying use gate0.5; swimming uses gate1. Their shared
macro chooses the “empty” animation for no stuff, small/normal one-hand items
or pistol+weapon, no animation for rocketlauncher+weapon, and the “gun” animation
otherwise. The respective pairs are `idle_crouch_empty/gun`, `idle_lie_empty/gun`,
and `swim_on_the_spot_empty / swim_gun`.

`stand_idle_strap_4` is the only one of these21 names without a declaration in
the three base human animation text resources. The script uses numbered empty
idles, avoiding the native fallback's unmatched bare `stand_idle_empty` name.
Neither absence proves the final mounted animation system lacks that name.

## Remaining static leads

- `1483008` writer/meaning: it bypasses idle selection before either native or
  scripted branches. Finish synchronization and the action registry lifetime.
- Trace all context118/188 producers for other events; idle deliberately uses
  the right-hand definition, but other actions may populate different contexts.
- Map the other concrete interaction actions used by frame markers, wounds,
  repairs, weapon events and death; this pass resolves the common list/if layer.
- Finish event-layer construction/dynamic receivers, entity flag3A writers and
  RNG context initialization/replay.
- Continue animation file resolution and EANM timelines in the ongoing resource
  pass, including unmatched names and declared-versus-binary marker coordinates.
