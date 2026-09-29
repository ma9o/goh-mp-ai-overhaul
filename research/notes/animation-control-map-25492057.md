# Animation control, completion and entity effects — build 25492057

This pass connects human FSM animation requests to concrete native controllers,
frame markers, bone changes and scene scheduling. It is static evidence from the
installed game and editor. RVAs and offsets are hexadecimal; virtual slots are
decimal. Names such as “working” describe reviewed behavior unless a native
diagnostic or property supplies the name. This is not a callable ABI.

## Request lifetime and completion

`5CD280` looks up a definition by its name at `+8` in the system's shared
`eAnimations` collection (`system+28`, vector `+10/+18/+20`). `5CD300` searches
the system's owned controller pointers (`+10/+18/+20`) by the same definition
name. These are linear, first-match lookups. The parser diagnoses duplicate
definition names.

`5CD380` is the request boundary described in the preceding FSM passes. The
concrete types now clarify its replacement rule: when `(flags & B) == 1`,
definition slot5 says it has variants, and controller slot15 differs from the
requested variant, it removes and destroys the old controller, then creates
another through definition slot1. State definition slot5 (`5D5AE0`) means
**more than one binary animation**. Thus a default variant argument `-1` can
replace an existing multi-variant controller rather than preserve its choice.

State factory `5D09F0` returns null for an empty binary list. With `-1` and more
than one variant it temporarily selects the owner's RNG context (or the global
fallback), consumes a draw and uses the result modulo the variant count.
Explicit indices are unsigned-clamped to the last available variant. With one
variant, `-1` resolves to index0. This is another source of RNG consumption;
requesting an animation is not a passive query.

For a controller, requests with either low flag bit set install the supplied
completion kind at controller `+8` **before** calling slot5. When that control
call returns zero, the manager sends entity event6 with the saved completion
kind at event `+8` and the definition name at event `+10`, then clears controller
`+8`. The owner is `system+30 -> 5C0BE0 -> root+1F0`; delivery is conditional on
the required pointers and a nonzero completion kind. A missing definition with
a start/resume request takes the fallback completion path `5CDFE0`.

Flag8 does not create a missing controller. On an existing state/blend
controller it stops work, and that zero return can deliver the **previous**
pending callback. This explains the human queue cancellation path
`5CDD60 -> 5CD380(flags8)`. Cancellation is not success at the gameplay level,
even though the shared completion channel can release a wait.

Destruction is different. The reviewed base, state, blend and blendspace
destructors free owned storage/children without emitting entity event6. System
destructor `5CD160` destroys controllers first, releases the shared definition
collection, clears its raw skeleton pointer and unregisters from scheduling.
Replacing a variant therefore does not complete the old callback through these
destructors. Reentrant callbacks can modify the system; a safe external interface
still needs a lifetime policy rather than retaining returned raw pointers.

## The manager's update protocol

`5CE040` (`445130` in GEM) snapshots the initial controller count and runs two
passes: definitions without flag68 bit3 first, then definitions with it. The
sequence parser names that bit **additive**.

| Controller slot6 result | Manager behavior |
|---|---|
| 0 | Deliver saved completion callback; request cleanup/reschedule scan |
| 1 | Continue, without delivering completion |
| 2 | Deliver saved callback at a loop boundary; request cleanup/reschedule scan |
| 3 | No completion; request cleanup scan when the definition is not resumable |

The manager tests slot6's full EAX. Its slot7 working query uses AL. The slot5
control result is also tested in EAX, despite several decompiler `bool`/byte
prototypes. **Quant delivery does not clear controller+8**. Loop notifications
can repeat; the synchronous control path clears the field after delivery.

Cleanup walks the current controller vector backwards. A nonworking controller
is deleted unless definition68 bit0 is set; the parser calls this **resume**.
Resumable controllers can remain resident while the system is unscheduled.
Definition flag **store** (bit2) is a separate property: state slot8 (`5D5B30`)
returns true when working, or when store is set and absolute time is at least
the epsilon. It is not the manager's keep-alive test.

## State animation: time, flags and frame events

`Bone::eStateAnimator` has loop byte20, mesh-action suppression21, output/event
suppression22, root-motion restriction23, float time24, float scale28, current
frame pointer38, last-dispatched frame40, bound bone vector48 and variant index60.
Definitions supply movement70, pinback74, speed78, smooth7C, binary variants80,
dynamic speed pairs108 and frame/name marker rows120.

`5D2460` (`449860`) interprets these request flags:

| Mask | Reviewed effect |
|---|---|
| 1 | Start; usually reset time and call Quant immediately |
| 2 | `ANM_RESUME`; clamp existing time and choose its neighboring frame |
| 4 | Type-specific pose application, described below |
| 8 | Set speed to zero |
| 10 | Update speed if already working; zero triggers a diagnostic |
| 20 | Set time to the supplied float clamped into `[0,binary end]` |
| 100 | Loop |
| 800 | Suppress mesh actions |
| 1000 | Suppress frame processing and pose application |
| 2000 | Suppress the ordinary initial smooth-capture path |
| 4000 | Restrict movement/pinback handling; use alternate root correction |
| 8000 | Use the existing-time/clamp path on start |

Bytes21–23 are rewritten from the incoming flags on **every** control call.
These are not accumulated flags. The stop called inside Quant therefore also
rewrites them. Flag20 changes time without repositioning the frame pointer in
that final branch; later frame traversal must be considered. Assembly at
`5D2899` supplies the upper clamp value that Ghidra incorrectly shows as an
uninitialized local.

Flag4 is not a universal instantaneous stop. For binary byteEC clear, the
effective flags become2009 with scale+1 when200 is set, otherwise-1. For byteEC
set they become2001 with scale-1000 when200 is set, otherwise+1000. The meaning
of the binary EC producer is still open. The byte21–23 values were taken from
the original flags before this rewrite.

`5D2900` returns3 immediately for zero speed. Otherwise it processes the current
frame, traverses every crossed frame in the playback direction, applies the
interpolated pose, then checks the **pre-advance** time against `[0,end]`.
Without a dynamic speed curve it advances by:

`definition.speed * controller.scale * signed_integer_global_1556E4C`

With a curve, it consumes the step across successive time/rate segments. A
crossing can therefore be reported complete on the following Quant rather than
the same one that advances time past the endpoint. When stopped, a nonlooping
controller returns0; a looping controller restarts through slot5 and returns2.
The native loop restart uses2101, or2901 according to byte21 at that point.
Immediate Quant on start is a real update, including frame effects and advancing
the current integer delta; it is not just pose initialization.

State duration slot3 (`5D0950`) returns a float in XMM0:

`(first_binary.end + 1) / (requested_scale * definition.speed)`

If the absolute denominator is below about1e-5, it returns approximately1e38.
This query uses the first variant and does not integrate the dynamic speed curve.
It is a useful bound/input for the previously mapped reload scaling, not a
universal measurement of the remaining completion time. Engine clock conversion
and binary timeline units still need their producers traced.

`5D6110` suppresses duplicate processing by comparing the frame pointer with
controller40, unless byte22 suppresses the entire path. Binary frames are16
bytes: coordinate0, pose sample offset4, mesh/visibility action offset8 and
countC. For each applicable action it can change the bone visibility bit and
queue a mesh action. Byte21 suppresses those mesh actions, while named frame
markers remain eligible.

The definition's `events` parser stores sorted68-byte rows: frame integer0 and
name token8. `5D6110` lower-bounds the current frame coordinate and sends the
first exact matching row as **entity event5**: animation name pointer at+8,
marker name pointer at+10. It does not iterate every duplicate marker at that
coordinate. This is distinct from event6 completion and from FSM callback IDs.

Game-entity notification `71A7C0`, case5, resolves the marker in the interaction
definition's `+48` map using `71F160` (68-byte name/short-ID rows), then invokes
`7705E0 -> 76ABE0` with an interaction context. Event6/subkind2 uses the separate
`+60` animation-name map. The existing interaction/event pass covers the next
dispatcher; concrete action lists are still needed before claiming a particular
marker causes damage, transfer or a shot.

The installed `human_anm.ext` sets default smooth0.5 and includes the movement
and crew registries. Reviewed movement examples include run_down markers11
leg_right/31 leg_left, posture-change markers, and lie_bolt_rifle markers7
open_bolt/22 close_bolt. These are source examples with hashes, not a full active
resource expansion or a statement that every configured frame exists in each
binary variant.

## Pose application can move the entity

`5D39A0` builds the controller's raw bone-pointer vector from the binary's
70-byte bone records. `5D8110` separately binds native names **placement** atC8
and **body** atD0. Initial smoothing (`5D3C10`) captures current poses and uses
`int(binary_end * smooth + 1)` as its span. `5D2F30` interpolates frame poses,
optionally smooths from that capture, and applies normal or additive transforms.
The additive path also reads a mutable `dbg_additiveAnmK` value and per-bone
cache stamps. The exact quaternion helper internals remain a geometry lead.

With movement enabled, a placement bone present, a nonnegative movement-track
index and byte23 clear, `5D7B60` computes movement through entity slot25
(`+C8`), writes the owner's transform at50..7C, and calls `6FF220` with flag3.
Thus these animations can change the world entity; they are not purely visual.
`5D7710` prepares forward/reverse movement references or pinback transforms.
`5D7E40` corrects root bones afterward. Its byte23-set branch still constrains
root coordinates; “skip all movement effects” would be an overstatement.
Full transforms, root ancestry and the virtual slot25 implementations remain
open static work.

## Blend, blendspace, aimset and UV contracts

Blend controller `5CF680` treats the float request argument as an integer
duration by truncation. A duration below1 is invalid. Its Quant (`5CF7F0`)
increments elapsed68 by1556E4C, applies a clamped elapsed/duration pose blend,
stops at the endpoint and returns0; an inactive blend returns3. Flag4 captures
and applies the endpoint immediately and clears duration. Fresh start captures
then calls Quant immediately; resume/8000 preserves and clamps elapsed.

`5CFBB0` captures source bone poses and picks the target binary's first or last
frame according to the definition's file begin/end setting. If all examined
translation differences are below0.5 and quaternion `acos(abs(dot))` differences
below about0.1, it reduces duration to1. Empty/no-applicable-bone cases also take
that shortcut. `5CF850` interpolates and writes bone transforms. These findings
explain why the human activeBlends counter must tolerate synchronous completion.

Aimset control (`C5AA0`) applies Quant for either low flag bit, returns0, and
always reports not working (`C5A40`). It is a retained pose sampler when its
definition has resume set. Parameters0/1 are floats at38/3C, set/get through
slots10/11 (`C44C0/C44E0`); `C4500` samples neighboring poses in two dimensions.
Its helper `C4A20` returns a float interpolation fraction and an **integer** index
at+4. Ghidra's float-looking index assignments are incorrect; assembly stores
R8D/EAX there. Parameter producers and complete table construction remain open.

Blendspace control (`C6ED0`) propagates requests to its child controllers.
It adds800 to suppress their mesh actions, plus1000 for unselected children or
when output suppression is requested. `C6520` selects the last sample whose
threshold does not exceed parameter0, optionally the next sample, and a fraction
between them; parameter1 is propagated as parameter0 of nested blendspaces by
`C6720`. Selection changes rebind bone sources and update child suppression.
`C73E0` blends the selected bones into the output bones.

Blendspace Quant updates all children and uses their slot7 working results to
return0/1; it does not propagate child loop code2 as a parent completion.
Positive speed20 means working. A negative start scale passes the absolute-speed
guard but does not satisfy that working predicate. Destruction releases the
retained child objects and destroys child animators, without event6.

UV Quant (`5D8870`) updates two texture-coordinate axes, patches applicable mesh
objects and always returns1. Its inherited control and working methods also
always report running. This is continuous visual work, not a useful finite AI
action-completion source. Its constructor enables autostart. The registered
`texture` definition factory `5D8360` returns null in this build; token
registration alone is not evidence of a usable texture-animation parser path.

## Scheduling and deferred mesh lifetime

The system scheduler (`5C4F50`, storage154D448) has an active pointer vector at0
and a pending vector at18. `5C48A0` toggles system8 between-1 unscheduled,-2
pending and active. `A20920` compacts null active entries, appends pending
systems, writes status1 and clears pending. Status1 is not an array index.
Unregistering an active system nulls its first active entry; removing a pending
system removes its pending occurrences.

The `scene:quant.anim` section of `A1B320` merges pending systems, iterates the
active vector calling slot2, then merges again. It follows trigger processing
and precedes the collision and entity-before sections. An animation system
scheduled during that iteration joins at the second merge and normally waits
for the next scene animation phase. Explicit request-side Quant is a separate
path. Scene destruction `A19FF0` clears pending/active vectors after its earlier
cleanup phases.

Binary mesh actions use a different mechanism from gameplay event5. A queued
70-byte record retains a model pointer at60, carries the animation name at0 and
mesh payload at68. Queueing increments the model reference count. The
`Bone::eDeferredUpdates` object at147C520 holds the two vectors at10 and28;
its event slot1 (`5D5DE0`) swaps them on event-object kind26. `5D6A00` releases
models in both vectors for scene destruction. `5D6AF0` releases matching models
in the first vector; a null argument also clears references in the second.
These functions null model pointers but do not erase every record.

`5D7180` applies a record through `5D6700`, which patches a mesh buffer and can
update validated bounds. Failure produces an “Invalid mesh animation” diagnostic.
This is a visual-buffer path, not the named gameplay action dispatcher. The
remaining deferred drain/registration and render-thread scheduling are still
static leads; no queue-lifetime completeness is claimed here.

## Evidence and remaining work

The [machine-readable map](../artifacts/build-25492057/animation-control-map.json)
records the method counterparts, fields, flag effects, RTTI slots, constants,
property registrations and resource hashes. The
[validation](../artifacts/build-25492057/animation-control-validation.json)
checks those addresses against the preserved binaries and frozen Ghidra exports.
Independent LLVM disassembly checks instruction starts for the core controls,
updates and consumers, including the misleading float/int and callback ABI cases.
This pass records73 game contracts/slices,54 editor counterparts,62 fields,
30 RTTI tables and17,807 evidence instruction starts. All130 game and114 editor
candidates are exact entries and all250/138 exported functions decompiled.
The58 independent disassembly checks cover6,939 instruction starts and match
after normalizing LLVM's standalone LOCK prefixes. These counts measure
evidence coverage, not semantic completeness.

GEM preserves the reviewed manager completion protocol and the state/blend
control branches, and adds the pinback1-or2 assertion in its parser and
reference-count checks at definition destruction. Paired RVAs do not assert
whole-function equivalence. Neither binary has been run during this work.

Next static leads are the animation resource-to-handler join, walking/pose
selection and idle/repair repetition, binary timeline normalization, concrete
event actions, root/bone constraints, serialization/rebind, callback reentrancy
and clock producers. These supplement the broader
[active frontier](static-frontier-25492057.md); static AI mapping is not exhausted.
