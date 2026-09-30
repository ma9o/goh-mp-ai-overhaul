# Sound definitions, inherited AI signals and channel lifetime

Build25492057, static file evidence. Addresses/offsets are hexadecimal; virtual
slots are decimal. [Native manifest](../artifacts/build-25492057/sound-definitions-map.json)
and [base folder audit](../artifacts/build-25492057/base-sound-folders.json).
No mounted override, game playback or callable ABI validation.

## The pointer chain and inherited fields

Channel48 points to a48-byte **asset row**. Asset0 points to its shared folder
definition. Folder14/18 is an inline integer signal type and float radius.
The earlier marker note incorrectly called14 a signal-object pointer; that note
and manifest are corrected. The nonzero publication check tests the type.

`4CF430/370D80` initializes the folder. `4CF490/370DF0` copies the first20hex
bytes from its parent before applying its immediate `folder.set`:

| Folder offset | Field | Constructor value |
|---|---|---|
| 0 | playback volume group | other4 |
| 4/8 | playback min/max distance, floats | native0/100 |
| C | signal repeat period | -1 |
| 10 | audio priority, integer | -1 |
| 14/18 | AI signal type/radius | 0/0 |
| 1C | playback volume | 1 |
| 20 | normalized directory path | built with `/sound/` prefix |
| 48/50/58 | immediate asset vector | empty, stride48 |
| 60 | remaining random choices | 0 |

Every scalar in that table survives into a child unless overridden. A child
that changes loudness, tag or volume does **not** clear its inherited AI signal.
The third parser argument is `parent != null`: game assembly4CF6F6..4CF71A and
editor37109A..3710A7 establish this even though game C omits the argument.
For a root without parent, the parser first reads default loudness min/max;
that preliminary reset does not copy priority.

`4CFC50/3715F0` applies properties in declaration order. Distance inputs use
the spatial scale14776C0, file initial20. Assembly stores float multiplication
results directly; apparent integer casts in decompiled C are misleading.
GEM additionally asserts `minDistance > 0 && maxDistance > minDistance` for
the two-value Distance property. Signal radius uses the previously mapped
`A5C9C0/8E5250`, independently of audio min/max and volume.

`4D0B40/372580` reads `/sound/sound.set`. Loudness rows have name0, float
min/max20/24, optional integer priority28, stride30. A duplicate name updates
the existing row case-insensitively; it does not append a competing row.
New priority defaults to-1 and is retained if a later declaration omits it.
`4D3D70/375850` returns the first matching native row, or manager108 default
when fallback is requested. Thus the last complete declaration for a name
determines that row's values. Default-pointer relocation during vector growth
and the wider reload lifetime remain outside this pass.

## What the installed base archives declare

The bounded extractor covers all seven archives under resource/sound:
548 folder declarations,244 loudness declarations/238 unique names, and
508 macro expansions across209 used macros.551 text members are hashed.
It supports the observed literal includes and zero-argument macros, including
three nested interior-gun signal presets, and rejects unresolved or conflicting
syntax. Ordered overrides and their original source lines are retained.

The archive inventory has20,883 WAV members in2,943 immediate directories;
each directory records the nearest inherited declaration and a digest of its
ZIP name/size/CRC metadata. WAV payloads are not decoded or copied. This digest
is not a content hash or a prediction of the native iterator's order.
These seven archives have no conflicting member names in the audited namespace.
DLC, mods, mounted precedence and cache history are not represented.

Human sounds comprise29 declarations,87 asset directories and662 WAV members.
Selected effective results, in authored radius inputs before spatial scaling:

| Folder | Signal | Period | Playback volume |
|---|---|---|---|
| human/anims and its reviewed children | sound3 | -1 | 0.75 |
| human/move | movement8 | -1 | 1 |
| human/move/lying | movement1 | -1 | 0.35 |
| human/move/normal | movement5 | 0.5 | 0.35 |
| human/move/fast | movement15 | 0.5 | 0.35 |
| human/move/gear_rattle/normal or fast | **movement8 inherited** | -1 | 0.35 |
| human/move/normal/gear_rattle | movement5 inherited | 0.5 | 0.35 |
| human/move/fast/gear_rattle | movement15 inherited | 0.5 | 0.35 |
| human/move/swim | movement8 | -1 | 0.3 |

The order of directory components matters. The gear-first directories inherit
from human/move because human/move/gear_rattle has no declaration. The movement-
first directories inherit from normal or fast. The base marker macro contains
`human/move/gear_rattle/%0/`; expanding its positional arguments and joining all
branch-specific requests remains a concrete follow-up, not an assumed runtime
choice. No immediate asset directory for gear_rattle/lying appears in this scope.

The all-folder audit finds145 declarations with no signal and403 with a signal:
movement225, shot56, explosion50, hit36, sound36. A nonzero descriptor alone does
not prove publication or recipient detection. The two unresolved loudness names
are `mission_voice` and `vehicle_internal`; native lookup requests default
fallback for both. Six repeated loudness names are updated in place.

## Selecting a sound consumes the current engine RNG

`4D3900/3753A0` caches directories, recursively resolving the parent first.
`4CF490` enumerates mounted resources but accepts only immediate children of
the requested directory. It recognizes `folder.set` and the literal `.wav`
extension. Descendant WAV files do not form a recursive random pool.

`4D3BC0/375680` distinguishes a directory request from a specific filename.
`4CF9F0/371390` then behaves as follows:

1. No assets: return null. A specific stem: return its first exact string match,
   with no random draw and no cycle-counter change.
2. A directory containing one asset: return that asset without a draw.
3. A directory containing multiple assets: refill remaining60 with N only when
   zero; fetch the current engine RNG; increment its counterC; update state8
   with `state*7FF8A3ED+2AA01D31` modulo2^32; decrement remaining; swap rows at
   remaining and `(state >> 8) % old_remaining`; return the remaining row.

`4D0620/372030` swaps the entire asset records, including their strings and audio
bookkeeping. This produces sampling without replacement within a complete
random cycle, but the boundary between cycles can repeat. Even the last choice
of a multi-asset cycle consumes a draw. Explicit file requests bypass that cycle.
No unbiased distribution or native enumeration-order claim is made.

This is the **current** RNG context, so sound selection can consume the context
selected by the interaction/event executor. It is not established as a separate
audio-only random stream. Retained channels point to asset slots whose contents
can be swapped; folder signal properties are unchanged because those rows share
the same folder. Full per-asset audio bookkeeping consequences remain open.

## Publishing and hearing are separate from playback gain

`4D2810/3742D0` obtains a cached buffer by full WAV path, allocates/recycles a
channel, assigns asset48 and position30, and sets group-derived gain54.
Initial signal publication requires creation bit0, nonzero folder14 type,
channel40 bit0 clear, and a source entity with30 bit0 set and3A bit0 clear.
The call is `A5CA90(folder+14, source, channel+30, payload)`.

There is no local volume, attenuation or listener-distance test in that gate.
`4D00B0/371A90` and `4D0120/371B00` compute audible gain from folder1C,
channel54 and a spatial audio helper. They do not scale the AI descriptor.
That helper and the full audio priority sort are outside this semantic review.

The global1478560, GEM12F0330, is the registered `snd_disable` value. Its direct
writers include zero initialization and audio startup/driver failure paths
that set1. Generic configuration writes through the registered descriptor are
additional possible aliases. Disabled audio bypasses buffer loading/playback,
while the reviewed initial publication remains outside that branch. The manager
registers its simulation events before attempting audio startup. This local
separation does not establish complete recipient hearing or mission behavior.

After initial publication, loop bit1 and positive folderC append an18-byte repeat
record: full handle0, **raw entity source8**, unsigned deadline10. The repeat
poll reads the current channel position and folder descriptor, reschedules as
`now + convert(period)`, and publishes with payload0. It does not fetch source74
as a new position. Loop owners' position-update paths therefore still matter.

## Cleanup, pauses and the GEM invariants

Handles contain generation high16 and index low16. The getter multiplies the
index by60 then checks full-handle equality; no index bound check is present in
that reviewed getter. GEM's outlined375BE0 makes allocation/recycling and the
16-bit generation increment particularly clear. These are channel handles,
not entity ownership references.

`4CDAD0/36F210` explicitly stops a handle: resolve it, erase its first repeat row
through `4D2FA0/374910`, then `4CFFD0/3719B0` ends playback, clears58 low3 bits
and zeros gain. Stop leaves the allocation/handle intact.

`4D2E00/374770` retires an allocated channel: erase its first matching repeat,
queue its low16 index for reuse, clear allocated bit4, then invalidate/release
through `4D0070/371A50`. That helper sets handleFFFFFFFF and releases audio
resource18 through `4CE1A0/36F900`. This reference-counted audio resource is
distinct from the raw entity stored in a repeat row.

`4D26B0/374170` retires each channel before destroying channel buffers and
resetting the free queue. Repeat cleanup follows that per-channel invariant;
there is no unconditional repeat-vector clear in this routine.

`4D1C90/373760` marks audio paused, stops playing buffers and marks them in58
bit3. Resume restarts marked loops and clears the bit. It neither removes repeat
records nor adjusts their deadlines. This does **not** prove that repeats run
during a globally paused game: event delivery and simulation-clock progression
still have to be traced.

The reviewed retirement branches of `4D1E70/373960` remove inactive/completed
channels when pending/paused bits are clear and the channel is not an enabled
loop. Non-tick calls preserve signal-bearing channels in that branch. An active
spatial channel with near-zero minDistance also reaches retirement. Other quota
branches can stop/defer audible buffers without removing repeat rows.

Sound manager vslot1 is `4D3F10/375A40`:

| Event code | Local effect |
|---|---|
| FB3 | clear channels |
| FB4 | gain refresh deadline=0 |
| FD2 | repeat/gain poll, then audio arbitration(true) |
| FD3 | if paused byteC4, arbitration(false) |
| FD7/FE4 | pause/resume from event38 byte |

GEM outlines the repeat poll3737F0, while game4D3F10 inlines the reviewed
4D1D20 logic. GEM asserts that each repeat handle resolves to a channel;
the game lacks that local assertion before dereferencing. GEM also checks
buffer length during audio arbitration and positive audio-resource refcounts.
These diagnostics illuminate invariants but do not prove all callers satisfy
them. Event names/producers and global pause ordering remain leads.

## Verification and continuation

36 contracts/slices include35 selected game/editor pairs and43 fields. All71
described functions were independently disassembled with LLVM:6,792 instruction
starts agree after LOCK-prefix normalization. Structural validation checks both
PE hashes, exact entries, four RTTI tables, constants/bytes and551 text-resource
hashes. Five extractor tests cover inherited signals, nested override order,
duplicate loudness updates, missing/cyclic syntax and ambiguous archives.

The exports also contain discovery functions; inventory is not semantic coverage.
Remaining productive static work includes loop-owner cancellation/position updates,
raw source lifetime, mounted filesystem/caches, default-row pointer relocation,
event/clock producers, positional marker macros and receiver threshold/area
registration. The earlier interaction-lifecycle pass already establishes
linked-alias cancellation on entity destroy-event cleanup.

Reproduce the resource audit with:

```sh
python3 research/scripts/map_sound_folders.py \
  --resources-dir "$GAME_ROOT/resource" \
  --output research/artifacts/build-25492057/base-sound-folders.json
python3 -m unittest discover -s research/tests -p 'test_map_sound_folders.py'
```
