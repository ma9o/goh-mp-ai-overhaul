# Sound owners, moving aliases and deferred effect sounds — build 25492057

This pass traces the direct owners of source-associated loop creation, their
stored handles, position updates and cleanup. It also binds the previously
numeric sound-manager events to native RTTI names. All addresses and member
offsets below are hexadecimal; virtual slots are decimal. Evidence is static,
from unchanged game/editor executables. No runtime experiment was performed.

Machine-readable contracts: [sound-owners-map.json](../artifacts/build-25492057/sound-owners-map.json).
This extends [sound definitions and channel lifetime](sound-definitions-map-25492057.md)
and [interaction/entity cleanup](interaction-lifecycle-map-25492057.md).

## Direct loop owners

The flags3 wrapper `4CDA10` / GEM `36F150` has six discovered direct callers in
each binary. The export's reference lists are not truncated. Its ABI at these
calls is output-handle address, path, position, source entity, then stack payload.
The source/payload are separate from the position's owner.

| Game / GEM caller | Owner | Source passed | Stored handle |
|---|---|---|---|
| `3713E0 / 265840` | Medic healing cue | null | order+60 |
| `755280 / 5D24E0` | Interaction alias | context owner+0 | alias+60 |
| `7B0220 / 6304A0` | `Entity::eSpeakerExtender` | null | extender+DC |
| `826EA0 / 6AA130` | Sniping aimer quant cue | null | aimer+1A0 |
| `827470 / 6AA700` | Sniping aimer explicit cue | null | aimer+1A0 |
| `A1C980 / 8A4340` | Deferred scene sound queue | queued row+38 | returned handle ignored |

Four callers therefore fail the sound manager's non-null AI-source gate, even
though they have an entity from which to obtain an audible position. They do not
publish source-associated AI signals through this sound creation path. This does
not describe other interaction events performed by those functions.

The queue supports a loop bit, but its **only discovered enqueue caller passes
false**, in both binaries. Consequently this investigation has not found a
reachable queued-loop repeat leak. The census covers recovered direct references;
it is not a proof that arbitrary indirect calls or externally modified code are
impossible.

## Interaction aliases move with their owner

`766F70 / 5E4D20` walks handler+28..30 in 88-byte alias rows. Every handle+60
other than FFFFFFFF is updated from handler owner+40 → entity position+74.
`4CDD50 / 36F480` checks the full generation/index handle, then
`4CEAE0 / 370300` copies the new position into channel+30..38.

The setter always copies those coordinates. For an active spatial device sample
it also sets the audio position and low-pass parameter; otherwise it marks
buffer flag3 for a deferred device update. It does not itself publish an AI
signal. A subsequent repeat uses channel+30, as established in the sound-definition
pass, so a moving alias can move the origin of later signals.

Entity virtual slot33, `71BB10 / 5968F0`, reaches the alias updater after linked
placement updates and before extender event7. It first calls the base position
callback and increments entity+A20. If entity+38 bit20 is clear and XY equals the
global sentinel pair, it returns before alias updates; otherwise it proceeds.
Full callback reachability remains open. GEM additionally asserts `simulator`
in the reviewed physics-update branch.

An alias play action can initially use context+F4..FC when its use-position byte
and context+188 bit18 allow it. Once the owner callback reaches the updater, the
stored channel position is replaced by owner+74. Do not treat the initial
context position as permanently fixed for a linked alias.

Play stops the first matching alias's old handle before replacing it. Stop also
stores FFFFFFFF. Destroy-event handler clear stops every alias before freeing
the alias rows. The preceding interaction pass connects scene entity retirement
to this destroy-event clear; arbitrary handler replacement/reentrancy remains
a separate lifetime question.

## Deferred effect sound queue

The enqueue function is `A1D110 / 8A49D0`. Scene+70/+78/+80 is a vector of
48-byte records:

| Offset | Content |
|---|---|
| 0..27 | owned path object |
| 28..33 | copied XYZ position |
| 38 | raw source entity pointer |
| 40 | loop byte |

Append deep-copies the path and value-copies the remaining fields. Growth
`A1EB10 / 8A63F0` and move `A1F940 / 8A7260` preserve the raw source without
retaining it. Destruction `A20090 / 8A7C90` destroys paths and storage; there is
no source release or invalidation in this vector implementation.

The sole recovered direct producer is `eFXEntity` virtual slot32,
`987770 / 810340`. Its reviewed sound slice walks FX+1A8..1B0, a vector of
20-byte strings, normalizes each path, copies **FX+74** for position and
**FX+198** for the AI source, and passes loop=false. The effect's position and
source entity can therefore be different objects. FX+198 writers, ownership and
initialization callers remain productive static leads.

`A1C980 / 8A4340` detaches the whole scene vector into a local batch, zeroing
the scene's three vector pointers before dispatch. It then selects the one-shot
or loop wrapper from row+40, with payload0. It ignores the returned handle and
destroys the batch afterward. Position is the queued copy, not the source's
position at drain time. A reentrant enqueue would enter the emptied scene vector.

The batch is drained **before** the same function calls entity retirement
`A1CC10 / 8A4510`. Scene destruction also destroys pending sound rows. That local
ordering is useful lifetime evidence, but does not prove that every raw source
was valid at enqueue or survives every other deletion route.

## Speaker extender

RTTI identifies the previously anonymous loop owner as
`Entity::eSpeakerExtender`, with primary table `10DA800 / F3D128`, resource
subobject+8 and eventable subobject+88. The E8-byte object's reviewed fields are:

| Offset | Meaning / constructor value |
|---|---|
| 18 | owner entity |
| 98 / 99 | enabled1 / loop0 |
| A0 | empty sound path |
| C8 | deadline0 |
| CC / D0 | cycle center−1 / spread0 |
| D4 / D8 | recovery center−1 / spread0 |
| DC | handle FFFFFFFF |
| E0 | signal mask0 |

The game factory `7B0A30` includes construction; GEM `631EB0` is the outlined
constructor, not the same allocation wrapper. The property UI `7AFB60 / 62FD50`
binds the names `cycle`, `recovery`, and `signals` to these fields.

Start `7B0220 / 6304A0` stops the old handle and invalidates DC, then plays only
when enabled and the path is nonempty. It samples owner+74 but passes null as
the AI source. In non-loop mode with a positive cycle center, it schedules a
new deadline: current clock plus converted center + (2×draw−1)×spread. A spread
whose absolute value is below the comparison epsilon skips the RNG draw.
The RNG accessor is `554B50 / 3DC040`, which reads the selected context pointer.
Asset selection uses `5549F0 / 3DBED0`, the default thread-storage accessor,
and bypasses that selected pointer. The clock/context follow-up establishes this
distinction; the earlier description of asset selection as current-context was
incorrect.

The entity callback `7AFF60 / 6301E0` handles these events:

| Event | Reviewed effect |
|---|---|
| 0 | register global1043; set owner+1C0 bit8 from mask!=0; start; synchronize owner+38 bit6 with global159AE7C and notify owner vslot13 if changed |
| 6 | stop and invalidate handle |
| A | test the incoming signal type against E0; if matched, stop, then schedule recovery when D4>0 |
| B | nonzero unsigned deadline<=clock: clear deadline and start |

The signal bit test is `BT RAX,RCX`, after a 32-bit mask load zero-extends RAX.
The index is modulo64; only its low32 positions can match. The separate global
event callback `7B0130 / 6303B0` repeats the bit6 synchronization for event1043.
That global's semantic name is not established here.

Stop only zeroes C8 when non-loop mode and cycle>0. In particular, do not assume
it unconditionally cancels every possible recovery deadline. Entity event6 is
the reviewed destruction stop route. The C++ destructors `7B1F00 / 632040` and
`7B0DC0 / 630F10` release path, event registration and base resources, with no
explicit sound stop. Their sound cleanup depends on the preceding entity-event
protocol. Event7 is not registered by this constructor, and the reviewed speaker
callback does not update a handle's position after starting it.

## Medic and sniping cues

Medic start `3713E0 / 265840` sets order+38, emits `start_healing`, and starts
`human/action/healing` at patient+74 with a null source. Stop and lifecycle
cleanup clear+38, emit `stop_healing`, and stop handle+60. The game Execute
contains inlined stop branches; GEM calls the outlined `265930`. The stop paths
do not themselves overwrite order+60. This corrects the earlier human-state
note's description of the start helper as creating a healing marker: it emits
an interaction event and starts an audible loop.

`Actor::Weapon::eSnipingAimer` quant `826EA0 / 6AA130` derives a boolean from
state+180!=sentinel and conversion of **member+180** >= float threshold+168.
The conversion helper takes an integer argument; assembly loads ECX from+180
before calling it. This is not a read of the current clock. Chassis helper
`83FB80 / 6C3350` gates the `human/calm` cue transition. False→true starts a
null-source loop and stores+1A0; true→false stops and invalidates it. The final
weapon+264 bit4 write occurs independently of that chassis gate.

Explicit helper `827470 / 6AA700` accepts the desired bit. Reset and both
destructor forms also stop the cue. The complete aim/accuracy meaning of bit4,
its other consumers and the chassis predicate remain open; an audible calm cue
alone is insufficient to assign an accuracy formula.

## Named sound-manager events

RTTI plus constructor literals bind all six previously anonymous events:

| ID | Native type under `Events::Scene` | Sound-manager effect |
|---|---|---|
| FB3 | `cmSceneDestroy` | clear channels |
| FB4 | `cmSceneInit` | reset gain-refresh deadline |
| FD2 | `cmQuant` | repeat/gain poll, then tick arbitration |
| FD3 | `cmNonPausedQuant` | non-tick arbitration when sound manager is paused |
| FD7 | `cmPause` | apply event+38 boolean |
| FE4 | `cmMuteVolume` | apply event+38 through the same helper |

FB3 is constructed in `A19FF0 / 8A1CB0`; FB4 in `A1A7D0 / 8A2490`.
FD2 producer `65F440 / 4D4790` increments object+48 by +4C before dispatch.
Normal executor `583250 / 40CAB0` gates that call on pause-count global==0
and an executor flag. It publishes FD3 **outside** the pause gate. Thus the
literal name `NonPausedQuant` does not establish that this event stops during
pause. Explicit step `65F3D0 / 4D4720` calls the FD2 producer without a local
pause-count gate; its callers remain to be traced.

Pause insertion `65EDB0 / 4D4270` adds an absent uintptr token to object+38's
tree and emits true only on the transition from zero tokens. Removal
`65EF40 / 4D43F0` removes a present token and emits false only when none remain.
Duplicate insert and absent remove are no-ops. FE4 has reviewed game-side
true/false producers `D0D8B0 / D0F620`; no matching GEM type was located.
Both pause event types write the same sound-state boolean. This does not prove
their full caller ordering or a combined logical-OR pause policy.

Full quant delta, slowdown, explicit stepping, pause and clock producers are
the next static investigation. Source-pointer validity through deferred effect
publication is also still open. The wider AI/entity frontier remains active.

## Verification scope

The frozen exports contain 69 game and 63 editor functions. The map records
43 contracts, including 39 game/editor pairs, 30 fields and 29 RTTI tables.
All requested candidates are exact entries and all exports decompile. Independent
LLVM disassembly checks every instruction start in the 82 reviewed method
bodies against Ghidra, allowing LLVM's separate LOCK-prefix formatting.
Binary fingerprints and the structural map are checked by `verify_fine_map.py`.
These checks protect provenance and instruction locations; they do not prove
the semantic interpretations or exhaust indirect reachability.
