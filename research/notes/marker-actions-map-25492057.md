# Human animation markers, interaction actions and perception inputs

Build25492057; file evidence only. RVAs/field offsets are hexadecimal; virtual
slots are decimal. [Manifest](../artifacts/build-25492057/marker-actions-map.json),
[handler closure](../artifacts/build-25492057/human-marker-actions.json),
[selected sound declarations](../artifacts/build-25492057/human-marker-sound-declarations.json).
No game execution, binary patches, callable ABI or mounted override validation.

## Concrete scope

The base `set/interaction_entity/human.inc` has31 animation-event handlers.
Following literal calls and macro references reaches36 nodes:31 markers, two
macros and three named events. The extractor preserves ordered syntax, source
lines and unexpanded macro arguments; no literal dependency is unresolved.
Of34 marker names in the base animation catalog,30 have these handlers.
`close_bolt`, `open_bolt`, `death_breathe` and `splash_big` have no handler in
this file. Conversely, `drink_canteen` has a handler but no catalog marker row.
This is a base-file join, not proof about inherited or mounted event layers.

Many suggestive names are sound actions. `prone_to_stand`, `crouch`, pickup and
construction markers do not themselves change posture or perform construction
in these bodies. `arm_right` plays the `throw` alias. `row` and `row_off` use
dynamic forward/backward tags to gate paddle sound and a view pulse. Shovel
chooses snow/sand versus ground sound. Bagpipe stopping removes its playing tag
and replaces sound; piano first links a sound alias.

`leg_left/right` call `play_leg_sound`: velocity16 chooses fast movement sound
and breathing; otherwise lying chooses crawl, then normal movement is the
fallback. `play_move_sound` combines gear sound, terrain-dependent footsteps,
rain branches and short `on_ground` view pulses. The leg handlers also gate
foot-dust spawning on velocity, season, rain and terrain. Macro `%0` substitution
and preprocessing are deliberately not emulated by this extraction.

A later [marker sound-path join](marker-sound-paths-25492057.md) resolves the used
positional path expressions, preserving branch guards and unused forwarded
arguments. It joins147 potential sound rows to the base sound-folder audit and
records four missing paths; native preprocessing remains a separate open lead.

## Duplicate frame100 has a static answer

The base `board_plane_right` definition has nine marker rows, including
`metal_footstep` then `jump` at100. Sequence parsing `5D0B40` appends rows in
declaration order and invokes `5D5540` (GEM `44C9C0`). For arrays of at most32
rows, that function uses insertion sort with **strict less-than** movement;
equal keys retain their order. The larger partition/heap paths were not needed
for this instance and remain outside this contract.

The previously reviewed `5D6110` uses lower_bound and dispatches only the first
exact marker coordinate. Consequently, this base definition dispatches
`metal_footstep`, not its duplicate `jump`, at100 through this path. This closes
the tie-order question from the animation-resource audit. It does not prove
the complete boarding outcome: other callers, overrides and event paths can
still matter.

That distinction matters because `jump` calls `eject`. The latter adds
`can_die`; altitude1 selects `throw_off up1 forward0.01`, then a named zero-delay
parachute check at altitude4, tag change and attachment spawn. The spawned
attachment schedules a0.5-delay `_flight` check and deletion. The low-altitude
branch calls the synchronized `do_throw_on_ground` event.

## Sound aliases reach AI signals

`link_sound` (`754B80/5D1D70`, parser `754D60/5D1F80`) lazily creates entity540
and appends an88-byte row to handler28/30/38: alias token0, handle60 initially
FFFFFFFF, path string68. Nonempty paths are normalized/preloaded. No alias
deduplication or animation-marker registration is established by this action.

`play_sound` (`755280/5D24E0`) finds the **first** alias, stops its previous
handle, plays a one-shot or loop according to integer50, and saves the new
handle. Missing aliases do nothing. `stop_sound` (`7554B0/5D2780`) stops/reset
the first matching handle. Default position is entity74..7C; action28 plus
context188 bit18 selects contextF4..FC instead.

`start_sound` (`755120/5D2350`) uses its string as a raw path. Ambient byte58
selects the source-free ambient wrapper. Otherwise it uses the one-shot wrapper
with the entity and interaction context as signal payload. Its parser accepts
integer50, but this executor does not read it or save an alias handle. The
`use_position` optional bare-token slot and subsequent `ambient` slot are
separate parser positions; no general alternate syntax is inferred here.

Wrappers `4CD9C0/4CDA10/4CD8C0` supply creation flags1/3/5 respectively, then
`4D27A0` resolves a definition and calls `4D2810`. Signal publication requires:

- Creation bit0 set and folder-definition14 signal **type** nonzero.
- Channel40 bit0 clear.
- Source entity nonnull, source30 bit0 set, source3A bit0 clear.

Channel48 points to a48-byte asset row, whose first pointer points to the folder
definition. Definition14/18 stores the inline type/radius descriptor; it is not
a signal-object pointer. This layout is clarified by the later
[sound-definition pass](sound-definitions-map-25492057.md).
The publisher receives the signal, source and channel30 position. Alias play
uses payload0; raw `start_sound` uses the interaction context. Loop bit1 and a
positive definitionC period append an18-byte repeat record (handle0, raw
source8, deadline10). `4D1D20` uses unsigned deadlines, reschedules from now,
and publishes repeats with payload0. Explicit stop `4CDAD0` resolves the full
handle and `4D2FA0` removes its first repeat row before stopping the channel.
Source lifetime outside this explicit stop path remains open.

The selected archive declarations explicitly provide movement signals:
footsteps8, crawl1, walk5, run15, swim8. Walk/run declare period0.5.
Infantry-animation sounds declare signal `sound`3. Thus a sound-only marker
can still affect AI perception when publication gates and inherited definitions
allow it. Gear macros have no direct signal declaration; that does **not** prove
they are silent to AI. The subsequent sound-definition pass resolves inheritance:
`human/move/gear_rattle/normal` and `fast` inherit movement8 from `human/move`.
The differently ordered `human/move/normal/gear_rattle` and `fast/gear_rattle`
inherit movement5 and15 respectively. These are base-archive results.

## Exact condition inputs and the GEM difference

| Condition | Native input/decision |
|---|---|
| velocity | `756F40/5D5840`: length of all three velocity components, strictly greater than threshold8. `727070/5A1F00` selects physics when entity1C0 bit4 is set, otherwise entity vslot47. Parser multiplies by1547DAC, initialized as `(14776C4*14776C0)/(14776CC*14776C8)`. File defaults give1/180, so16 gives0.0888889 native units. No horizontal-only or unverified physical-unit claim. |
| tagged | `759BA0/5D3E70`: entity4E8..4F0 dynamic60-byte token vector. These are distinct from equipped-definition tags. |
| terrain_fx | `756E20/5D56F0`: entity position through `A43AB0`, then `A43550`. First terrain layer, clamped grid coordinates; cell has signed16 height and packed type/FX. Type is low4 bits, FX bits4..10. Surface types3..5 above world38 return zero pair. FX lookup uses458-byte registry rows with token8. |
| rain | `756F00/5D5800`: global rain object and nested18 object exist and nested118 integer equals0. The predicate does not test intensity. |
| season | `759420`: game requires session state, resolves the current descriptor and compares descriptor110 name. **GEM `5D81D0` returns false unconditionally.** The editor therefore cannot verify this game decision. |
| altitude | `7571B0/5D5B60`: entityZ strictly exceeds terrain/surface reference plus parsed input times14776C0 (file default20). |
| linked | `757870/5D6390`: entity1C0 bit28 required; optional requested tags are matched against the linked parent's definition. |

`A3E8D0/8C6FC0` performs bilinear interpolation of four signed16 cell heights.
Coordinates use origin34/38, integer cell size and truncated integer division;
neighbor indices clamp to layer bounds. The game decompiler emits an empty
void function, but the assembly computes and returns the height in **XMM0**.
`A42910` raises the reference to world38 surface height for types3/4/5.
This is an explicit example where assembly, not apparent C output, establishes
the input contract. Season descriptor production and physical velocity helper
`73DD70` remain follow-ups.

## Tags, calls, views and delayed actions

`Tags` (`74DEF0/74E040`) parses add/remove and a space-separated tag descriptor,
copies entity1D0, modifies it, then commits through `71A3F0`. The latter compares
ordered named tags, updates the descriptor and global tagged-entity membership,
and can invoke entity vslot56 when entity38 bit13 is set. Full invalidation and
entity lifetime are not established by these local branches.

`Call` parses an interned event descriptor (`753670→761B70`), locates that
pointer in the interaction definition's event vector and dispatches its16-bit
ID through `7705E0` with the original context. `Call_Synced` uses the same
execution path but sets virtual slot8 true (`7536D0`); `ThrowOnGround` also
sets that trait. The common `1AFA20` wrapper has session/authority gates and can
emit eIASyncCommand4C before local execution. This is a reviewed gate slice,
not a complete multiplayer protocol or permission model.

`ThrowOnGround` (`744690→937E20`) sets bit0 in the human FSM handler at
actorDB8→E88 and requests immediate entity work. It does not apply an inline
velocity impulse here. `ThrowOff` parses up/direction/forward base/spread,
turn, force, death and rotation options, then `749670` selects launch versus
failure/death paths after trajectory trial `93BDD0`. Full trajectory, landing,
handler bit0 consumption and raw entity lifetimes remain open. `Spawn`'s large
executor is exported as a discovery lead, not claimed as semantically mapped.

`view` (`75F490/75F5D0`) parses start/stop/pause/show/hide and optional
my/ally/enemy/neutral bits1/2/4/8. A relation mask requires an actor, applies a
show/hide decision for the viewer's relationship, then requests the chosen
mode through `982CC0`; entity3C bit1 adds a second target. The effect system
inside that callee remains outside this pass.

`Delay` has children28/30/38, name40, baseA0, spreadA4, `uniq`A8 and captureA9.
`DelayEx` sets captureA9 and copies190 bytes of interaction context, marking
the copy through `76DEE0`; that helper does not retain entity references.
With abs(spread)<1e-5 it consumes no random draw;
otherwise delay is `base+(2*U-1)*spread`. The LCG uses multiplier7FF8A3ED,
increment2AA01D31, increments its draw counter, and converts the top24 bits to
U with mutable1571A74. The leaf initializer at4C440 stores1/16777216 there;
its raw bytes and LLVM disassembly were checked independently.

`766970/5E4720` stores deadline=`clock+convert(delay)` in anA0-byte action row.
When both new and stored `uniq` flags are false, it replaces the first row with
the same **action pointer**, releasing any previous capture; the name is not
the key. `uniq` forces a new row. Even zero delay is queued, not executed
inline. `DelayEffect` instead coalesces its68-byte queue by name. `KillDelay`
removes the first matching name from each queue, not every matching row.
`768600` recomputes handler48 and reschedules its owner when active state changes.

Capture release is explicit in `7688D0`; the row value destructor `768860`
only destroys its token/string. Cancellation and queue compaction therefore
need to be read together with Quant/destruction before making ownership claims.
Immediate leads: `766ED0` (dynamic receiver dispatch, not queue Quant), `769910`
(handler destruction), the queue executor, captured context ownership and
reentrant enqueue/cancel during child execution.

## Verification and continuation

78 described function contracts/slices,55 selected GEM counterparts and42
field records are anchored to exact entries/instruction starts. The export
inventories also include discovery functions; inventory is not review coverage.
All133 described game/editor functions were independently disassembled with
LLVM; instruction starts agree after treating LLVM's separate LOCK prefix as
one Ghidra instruction. PE hashes,46 RTTI tables, readonly constants, initializer
bytes and12 archive-member hashes are checked by `verify_fine_map.py`.
Extractor tests cover call cycles, unexpanded macros, missing calls, comments,
sound links and ambiguous event rejection. These checks do not prove semantics.

Next static work: delayed-handler execution/lifetime, sound-folder inheritance
and asset selection, signal source cancellation, throw/landing and spawn
ownership. None is treated as requiring runtime merely because it crosses a
virtual call, uses a raw pointer or defeats the decompiler.
