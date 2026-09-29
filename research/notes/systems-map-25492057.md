# Scene scheduling, sensors and cover sources — build 25492057

This pass extends the [tactical map](tactics-map-25492057.md). All addresses
are RVAs in the fingerprinted **game**, unless an editor address is given.
These are reviewed static contracts, not runtime-tested calls. The
[frontier](static-frontier-25492057.md) remains active.

## Entity work is phased and grouped by owner

`0xA19450` accepts a callable, captures the current entity from global
`0x159A058`, and submits a **0x50-byte work record** to `0xA1D480` with required
phase **1**. It is not a timer-registration function. The record contains an
owner pointer at `+0`, callable storage from `+8`, callable pointer at `+0x40`,
and an optional completion pointer at `+0x48`. This entry initializes completion
to null. The callable is copied/moved and its input storage is destroyed.

`0x159A058` is the **current quant owner**, not a time counter. `0x159A060` is
the phase; `0x159A068` points to the scene. These names are analyst descriptions.
`0xA1D480` locks the scene critical section at `+0x418`, asserts that owner is
non-null and current phase matches the supplied phase, and groups records in
the scene tree at `+0x440`. The comparison key is owner dword `+0xC0`.
Tree node `+0x20` holds the owner; its embedded execution context starts at
`+0x28`. The record vector begins at node `+0x540`, ends at `+0x548`, and has
capacity at `+0x550`. These offsets follow assembly, including the container's
leading allocator/storage field at `+0x538`.

The reviewed main scene routine `0xA1B320` has this ordering:

1. An earlier entity loop invokes virtual slot 22 (`vtable+0xB0`).
2. Set phase 1; invoke entity slot 21 (`+0xA8`) with current-owner set.
3. Set phase 2; swap the accumulated owner tree into the processing tree.
4. Execute queued records serially or as worker jobs, then clear that tree.
5. Set phase 0; drain remaining queued records serially.
6. Invoke entity slots 23 and 24 (`+0xB8`, `+0xC0`) with current-owner set.

There are additional events and scene work around these regions; this is the
AI-relevant ordering, not a complete frame-loop specification. The selector
argument 0/1/2 in helper `0xA1D680` chooses slots 21/22/23+24 and must **not** be
confused with the global phase values.

The parallel path uses `eSceneQuantTask` (RTTI vtable `0x10FC658`). Its factory
is `0xA20D80`; virtual slot 1 is `0xA21070`. A job's `+8/+0xC` hold worker index
and worker count. It partitions a vector of owner/context pairs into contiguous
ranges and walks each context's work records in insertion order. Thus callbacks
for one owner stay in one worker job. Optional shuffling changes owner-group
order. An optional completion byte is set after each callback. The serial path
uses the same record stride and invocation slot.

Worker count comes from `0x1485F30`; negative configuration selects a derived
count capped at four, and zero selects serial execution. Submission is
`0x0A3E50`; `0x0A3EE0` repeatedly dispatches pending jobs, tests active-worker
completion, and calls `SwitchToThread` until the active list is empty. This wait
precedes the later entity decision/order callbacks. This supports a producer /
worker computation / consumer structure. It does not prove that arbitrary
engine reads or mutations are safe on workers.

## Actor and sensor preparation

Actor slot 21 is `0x7EFBF0` (editor `0x671CD0`). With actor flags `+0x38 & 0x220`
clear, it queues an actor callback, invokes the component at `+0xDD0` slot 12,
then the brain at `+0xDC8` slot 23. The latter was already mapped to the human
brain's score-producing callback registration. The `+0xDD0` component dispatch
matches **Actor::eSensor** RTTI and its constructor/uses.

The sensor constructor `0x8733F0` constructs four `eQuantScheduledTask` members:

| Sensor offset | Embedded task name | Constructor capacity argument |
|---|---|---:|
| `0x20` | `taskWeaponUpdate` | -1 → default 100 |
| `0xB0` | `taskInform` | -1 → default 100 |
| `0x140` | `taskLookAround` | 64 |
| `0x1D0` | `taskBrain` | -1 → default 100 |

The argument becomes task `+0x70`; its scheduler role is reviewed below, rather
than assumed to be a period. Owner is sensor `+0x260`; four subject pointer
vectors begin at `+0x268`, `+0x280`, `+0x298`, `+0x2B0`; visor pointers span
`+0x2D8..0x2E0`. The candidate vector spans `+0x338..0x340`.

Sensor slot 12 `0x873910` (editor `0x6F71B0`) is gated by byte `+0x18 == 0` and
either `+0x19 == 0` or owner flag `+0xDB4` bit 8. It refreshes eligible visor
equipment and sets `+0x2F8` to the largest visor-info `+0x70` range, plus five
spatial units. It clears scan candidates and, after an additional global gate:

- Schedules the weapon-update callable when task `+0x20` passes the interval
  gate with requested bounds **1..2**.
- Schedules look-around `0x8798D0` when forced by byte `+0x1C`, or when task
  `+0x140` passes bounds **0.5..1**. It writes bytes `+0x1B=1`, `+0x1C=0`.

`0x875BE0` checks task virtual slot 2, resets through slot 3 if due, and suppresses
that occurrence while positive task `+0x6C` is decremented. Naming all control
flags and the producers of this skip count remains open.

## Scheduled tasks use allocated intervals

`eQuantScheduledTask` slot 2 `0x1CEF40` compares unsigned current quant counter
with deadline `+0x84`. Slot 3 `0x1CEF70` checks that it is called during quant
and outside a quant worker, maintains requested delay bounds `+0x7C/+0x80`,
then writes `deadline = current_counter + duration_to_counter(period88)`.

`0x1CF600` allocates a scheduler bucket: it searches the requested interval for
eligible cells with the lowest recorded load, comparing load against task
slot 1's capacity result. If none qualify, it logs that no empty cells were
found and searches later cells. Equal delay endpoints widen the initial search
to nine cells. It writes selected bucket index `+0x74`, registration handle
`+0x78`, and the bucket-derived period `+0x88`. Requested endpoints therefore
are not proof of the actual interval, nor of a uniform random distribution.

`0x65E700` reads counter global `0x1556E48`. `0x65E730` multiplies duration by
file-initialized `0x14776C8 = 1000` and truncates to integer; `0x65E710` multiplies
counter by reciprocal global `0x1547D9C`. Initializers `0x2FA60` and `0x4C0C00`
set that reciprocal. Spatial conversion similarly starts with
`0x14776C0 = 20`. No direct Ghidra write references were found to those two
initialized factors; this is not proof against indirect writes. The counter's
advance/pause/time-scale producer still needs review before treating this as
a wall-clock timer.

GEM provides useful extra static diagnostics. Editor `0x14B5F0` has the same
reset fields and adds an explicit nonzero-period assertion. Editor subject
insertion `0x6F8D90` asserts `!Core::IsQuantWorkerThread()`. The corresponding
game routine retains the predicate call but lacks that assertion branch.
This identifies game `0x0A6740` as the worker-thread predicate and makes the
subject-mutation thread restriction explicit without running GEM.

## Spatial observations and subject ownership

`0x8798D0` resets selected observation flags across all four subject vectors,
then scans enabled visors. Visors backed by equipment provide a sphere, origin
and range; other visors contribute to a cached-bounds calculation. It also
scans a cached bounding box and conditionally runs an additional scan.

`0x87ABE0` queries spatial index `0x159A0A0` with mask 2, excludes owner ID
`actor+0x590`, follows result `+0x98` to an entity, rejects null/self and entity
flag `+0x3A` bit 0, and supplies an in-range predicate to `0x87AE10`. The distance
test is strict planar squared distance `< range²`. Candidates are 0x18-byte
records and are subsequently sorted. Full visibility and candidate acceptance
are the next perception pass, not inferred from this range test alone.

Sensor slot 19 `0x8753C0` allocates a **0x80-byte Actor::eSubject**. It stores
the entity at `+0x20`, and also at actor pointer `+0x18` only when entity
`+0x30` bit 1 is set. It packs the supplied category into flags `+0x10` bits
21..22 and appends to `sensor+0x268 + category*0x18`. Timestamp `+0x64` gets
the quant counter. It registers sensor's secondary subobject (`+0x10`) as an
entity event listener for ID 7, or immediately calls sensor slot 20 when entity
flag `+0x38` bit 5 is already set. Event name and removal semantics remain leads;
this establishes that these references have lifecycle handling.

## Cover candidates have distinct sources and common gates

`0x8A0B20(search, XY, stance_kind)` accepts immediately when search byte
`+0x11E` is clear. Otherwise it enforces:

1. Positive-radius search bound at `+0x150` via its point predicate.
2. Navigation-property query `0x8AC2B0` must not reject the point.
3. Environment low-byte types 4 and 5 reject; type 3 rejects stance 1.
4. With an owner, occupancy `0x8A5400` and a spatial mask-8 proximity query
   must both be clear. Search byte `+0x11F` enables the own-occupancy exception.

`0x8A5400` queries index `0x1482088` around XY. No records means unoccupied;
one 12-byte record is allowed only when its leading ID equals the supplied
owner's **16-bit `+0xDAA`**. Multiple records reject. This identifier differs
from the 8-bit relationship IDs at `+0xDA8/+0xDAC` and spatial ID `+0x590`.
The index's writers still need review before calling it an exclusively
cover-reservation index.

`0x8A0E60` detects duplicates against previously generated 0x88-byte candidates
using a strict squared-distance radius. `0x8A0D00` rejects nearby mask-0x40
spatial objects whose associated entity exists and whose spatial-object
`+0x1C0` bit 2 is set. It is a specific object-clearance test, not simply another
distance-to-candidate test.

`0xA43AB0` extracts two environment fields from a four-byte terrain cell:
low nibble and next seven bits; the low nibble is reset for certain 3..5 cells
above a world height threshold. The meanings of those environmental enum values
remain to be named. `0x8AC2B0` has a fast property/grid rejection and a fallback
query whose cost/result is rejected at **100000**; full path semantics remain open.

The previously unexplained search flags select separate producers:

| Producer | Observed behavior | Generated kinds |
|---|---|---|
| `0x8A84A0`, flag `+0x124` | Raster cells with value 2; obstacle/ground height difference; posture-height probes through `0x8A8E80` | 9, 10, 11 |
| `0x8A89A0`, flag `+0x125` | Terrain-grid height breaks, neighbor/directional tests, common gates and object clearance | 12 |
| `0x8A27B0` | Object geometry: OBB edges, circle boundary, or polygon segments | Determined in edge/circle helpers |

The object collector temporarily changes a spatial-object flag, builds geometry
contexts with `0x8A6E00/0x8A7120`, calls edge helper `0x8A73B0` or circle helper
`0x8A7140`, then restores the original flag. Candidate generation therefore
has temporary engine state changes even though this research only reads files.

`0x8A5880` sets candidate kind at `+0`, and derives fields `+4/+8/+0xC`:

| Kind | +4 | +8 | +0xC |
|---:|---:|---:|---:|
| 0 | 0 | 0 | 0 |
| 1 | 1 | 3 | 3 |
| 2 | 1 | 3 | 3 |
| 3 | 1 | 3 | 3 |
| 4 | 2 | 2 | 3 |
| 5 | 2 | 1 | 2 |
| 6 | 2 | 3 | 0 |
| 7 | 2 | 2 | 0 |
| 8 | 2 | 1 | 1 |
| 9 | 3 | 3 | 3 |
| 10 | 3 | 2 | 2 |
| 11 | 3 | 1 | 1 |
| 12 | 4 | 1 | 1 |

`+4` is a switch in `0x8A98B0`; the other fields use initialized integer tables
`0x14821E0` and `0x1482218`. Out-of-range kinds produce zero. Exact names for all
kinds and descriptor fields remain to be established through their parsers and
consumers; posture correlations alone are not enough.

## The detector returns a height measurement

The decompiler incorrectly displayed `0x87D6C0` as returning void. Assembly
shows a float in XMM0. Wrapper `0x87C780` substitutes the object at `+0x18` and
tail-calls it. Successful query `0x87DB80` computes a ray/grid height, then
`0x87D6C0` converts its scale and subtracts the sampled signed-16-bit terrain
height. A failed query returns approximately **1e38**. This is a vertical
measurement relative to terrain, not a probability or a boolean visibility flag.
The ray walker and angle gate remain productive static leads.

## Reproduction and checks

The manifest is `research/artifacts/build-25492057/systems-map.json`.
Using `GOH_BINARIES` as in the README, run exports sequentially:

```sh
python3 research/scripts/run_headless.py --build-id 25492057 export --program call_to_arms.exe --candidates research/artifacts/build-25492057/call_to_arms.systems-pass.tsv --anchors research/artifacts/build-25492057/call_to_arms.systems-anchors.tsv --focused --output local-work/exports/build-25492057/call_to_arms-systems
python3 research/scripts/run_headless.py --build-id 25492057 export --program call_to_arms_ed.exe --candidates research/artifacts/build-25492057/call_to_arms_ed.systems-pass.tsv --anchors research/artifacts/build-25492057/call_to_arms_ed.systems-anchors.tsv --focused --output local-work/exports/build-25492057/call_to_arms_ed-systems
python3 research/scripts/verify_fine_map.py --map research/artifacts/build-25492057/systems-map.json --binaries-dir "$GOH_BINARIES" --exports-dir local-work/exports/build-25492057 --output research/artifacts/build-25492057/systems-validation.json
```

The exporter now distinguishes external address-space targets from image RVAs
and records imported call names. The successful exports exercise critical-section,
thread-yield and performance-counter imports. `--focused` skips the original
survey anchors; an optional third TSV column `write` restricts data references.
This is a direct-reference filter, not an alias/write-completeness analysis.

Validation passed for 36 exact game candidates and five editor candidates,
69/11 completed function exports, 47 method entries, 210 instruction evidence
locations, six RTTI tables, three constants and five small byte sequences.
LLVM independently matched 305 Ghidra instruction starts across submission,
worker execution, deadline and detector-return routines. The six PE-scanner
tests passed. These checks establish reproducibility, not semantic completeness.
