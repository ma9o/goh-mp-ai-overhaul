# Scene spatial state, automatic obstacles and contact dispatch

Build `25492057`; static analysis only. RVAs and offsets are hexadecimal;
virtual slots are decimal. This pass reviews **56 paired contracts plus one
editor-only cleanup function**, **38 field records**, four RTTI tables and
**12,666 instruction starts** independently checked with LLVM. Two raw exit
thunks were checked separately. Large entity, serialization and scene functions
have explicitly limited scopes in
[area-scene-map.json](../artifacts/build-25492057/area-scene-map.json).

## Placement counters and refresh timing

This corrects the field names in the earlier volume-producers map:
**`A20` is the current placement counter; `A18` is the last spatial refresh
counter.** `72A160 / 5A4EE0` supplies the diagnostic name
`placementFrame.Counter()` for `A20`. Entity virtual slot33
`71BB10 / 5968F0` increments that 16-bit counter after its base callback, even
when a later branch skips geometry updates. It wraps modulo65,536.

The callback sets pending spatial refresh bit31 in entity `1C0`, except for
change codes3/5. Immediate refresh depends on bit17, entity `38` bit14 and a
timer at `A50`. `71FE30 / 59AD20` clears the pending bit, optionally rebuilds
procedural obstacles, then refreshes zones. `71F450 / 59A2D0` walks the zone
vector `A00`; its Boolean argument can skip obstacle-category zones. It always
copies `A20` into `A18` afterward, including an empty or entirely skipped list.
Equality of these counters therefore does not prove every zone was updated.

`71D020 / 597D50` separately compares `A24` against `A20`. An unchanged counter
accumulates simulation delta at `A28`; after the converted0.1 threshold it sets
stationary bit2, invokes entity slot41 and updates the pather. A changed counter
resets that elapsed state. It also handles the pending refresh deadline.
Other component quant and periodic bone/volume refresh branches remain open.

Actor slot33 `7F1A90 / 673B70` calls the entity implementation, then forwards
changes other than code2 to chassis slot51 when the chassis has its required
object. The entity implementation also updates pathing, simulator placement,
linked entities, interactions and event receivers. Their call order is recorded;
transitive component behavior is not claimed complete.

The all-obstacle and named-obstacle enable functions `71FB40 / 71FC90` update
entity suppression flags, refresh pending placement and feed changed zones into
the spatial update path. These are obstacle controls, not generic AI enable flags.

## Zone copying and automatic shape production

`72FE50 / 5AAF70` copies zone configuration and binds a new entity. It clones
the source geometry twice for the working and source shapes, resets the area ID
and does not copy the separate pather shape/ID. It does not first delete existing
destination shapes; initialization or cleanup is a caller precondition.
`71F810 / 59A6F0` releases existing destination zones except the embedded body,
copies or preserves the body according to its flag, and clones eligible owned
zones. Its refresh also copies `A20` to `A18`.

`7209B0 / 59B8D0` creates automatic obstacle zones for nonlocal entities using
`720B40 / 59BA50`. The entity property token `93`, `CompoundObstacles`, sets
`1C0` bit25. Clear means one combined point cloud; set means a separate cloud
for each eligible entity volume. This produces multiple circles/boxes, rather
than the native compound-area shape kind15.

`723C80 / 59EBD0` reads entity volumes `510..518`. A volume contributes only
when `28` bit5 is clear and `C` bit4 is set. After refreshing its transform, it
uses shape slot5 to enumerate transformed XYZ vertices. Normally the whole
volume cloud is accepted when its minimum terrain-relative height is at most
`1.2 * 14776C0` and its maximum is at least `0.4 * 14776C0`. The definition tag
`obstacle_ignore_height` bypasses this gate. This is a min/max eligibility test;
it does not clip individual vertices to the band.

For each nonempty accepted cloud:

- `5E9510 / 45B020` uses the XY arithmetic centroid and the farthest point's
  distance as a containing-circle radius. Its score is `pi * radius²`.
- `5E9290 / 45ADA0` tests90 box orientations over a quarter-turn, excluding the
  endpoint. It keeps the strictly smallest projected width×height, retaining
  the earliest orientation on a tie.
- The circle wins when its score is no greater than the box's. Otherwise the
  generated shape is the fitted oriented box. Z no longer affects fitting.

The circle is not a minimum-enclosing-circle algorithm, and the box search is
sampled. Exact math-library rounding remains outside this contract.

[entity-obstacle-resources.json](../artifacts/build-25492057/entity-obstacle-resources.json)
records a bounded census of13 base archives,11 text extensions and71,888 members.
There are41 literal-matching members:36 full-file parsed `CompoundObstacles`
heads, one recovered complete declaration in `airborne.ext`, three
`obstacle_ignore_height` occurrences in parsed declarations (two recovered
from animal/vehicle property files and one in the interaction token catalog), and a comment-only occurrence in `cliff.inc`. Three files fail
the full parser; recovered lines retain unknown parent context. Archive digests
match the earlier bounding-shape census. Includes, mounts and live attachment
are not inferred from these declarations.

## Storage initialization and IDs

`775DA0 / 5F4700` initializes world bounds, both interval axes,400 pair buckets,
a seven-level pyramid and a50×50 seed grid. `776890 / 5F51D0` makes2,500 seed
spheres with radius5, XY cell-center positions and Z0. Their areas are enabled
with zero category/filter masks. A construction flag suppresses pyramid insertion
and negative-seed cell lookup until seed setup finishes.

Requested-ID allocation exists separately from the ordinary allocator:
`772EB0 / 5F1950` chooses a bank from the requested ID; `773310 / 5F1DE0` extends
slots or replaces one, retaining the new snapshot and returning that ID. It
does not clean up spatial endpoints/pairs itself. `776E80 / 5F57C0` forwards
the requested ID unchanged in `R8`, which the decompiler's prototype hides.

`775F60 / 5F48C0` recreates the destination grid, then copies nonnull source
snapshots from index2,500 onward. Its source vector concatenates both registry
banks without padding to the second bank's base`80000`. Destination IDs are
the loop indices. Thus ordinary first-bank IDs preserve their indices; if the
second bank contains entries, their copied IDs follow the concatenated indices.
Whether that second-bank condition occurs during this scene initialization is
still open; this observation alone is not a demonstrated game bug.

`8B2B70 / 737BA0` initializes primary/worker pathing contexts, copies the scene
heightmap and obstacle storage into worker state when asynchronous mode is used,
then marks pathing active and creates pathers. Heightmap copying and the mode
predicate remain static leads.

## Overlap consumers and contact boundary

`7721E0 / 5F0BB0` returns a borrowed overlap vector from the registered snapshot
when its ID resolves, otherwise from the original area. It releases its temporary
shared handle before returning; it does not retain a lifetime for the caller.

Near the tail of `71C640 / 597370`, entity quant copies body-area overlap rows,
sorts them by unsigned cached owner ID and dispatches each through
`727520 / 5A23B0`. Invalid owner metadata sorts as`FFFFFFFF`, after valid IDs.
The temporary rows still contain raw area pointers. Earlier network motion
interpolation in the same function is not reviewed here.

Dispatch requires appropriate side/entity flags, at least one nonstationary
eligible direction and a negative pair-exclusion predicate. Eligibility
`727370 / 5A2200` reads both entities' contact masks/categories, locality,
simulation state and parent-link kind. It rejects entity flag mask`820` and
has no internal null guard on its second entity. The contact constructor
`727BD0 / 5A2A00` puts both raw entities, two spheres, vectors and direction/
chassis flags into a stack context, then calls `727D60 / 5A2BE0` for resolution.
That lower resolver is the next investigation target.

`7214B0 / 59C230` provides a named-zone membership check by raw entity-pointer
equality in overlap rows, without an owner-ID fallback.

The reviewed prologue of scene tick `A1B320 / 8A2FB0` runs synchronization,
trigger and animation work before ticking the general area manager, then
asynchronous path quant. This is a phase slice, not a complete scene schedule.

## Scale default, task threads and queue capture

Initializer `45540 / 36420` registers `g_maxEntityScale` and sets its mutable
cap to10.0. This closes the earlier uninitialized-file-bytes question. The
area-lifecycle erratum in commit`a931893` also corrects NaN handling: the clamp
forwards a NaN input when floating-point exceptions are masked; it has no finite
value check. The registered variable's full setter mechanism remains open.

The generic task thread uses a manual-reset kick event and completion event.
Its entry `A5AA0 / 93F80` waits, resets the kick, checks stop, invokes task slot1
and signals completion. The stop byte cannot interrupt an executing callback.
The pather worker loops inside that callback until the global application stop
event is signaled. Its lazy initializer registers an exit thunk to the worker
destructor. The outer wrapper creates/signals/closes the stop event; its100ms
sleep is not proof that every worker has joined. Inner application teardown and
quit-handler ordering remain open.

Pather wrappers now establish capture ownership: register clones the input
shape and captures context, removal captures the numeric ID and ignores its
incoming shape pointer, and box-change captures the box by value. Their concrete
queued invocations and per-pather algorithms still need review.

Validation checks binary fingerprints, exact function entries, four RTTI tables,
literal token/parser/thunk bytes, four selected resource members and instruction
locations. LLVM starts match Ghidra after standalone LOCK normalization. These
checks validate evidence identity and structure, not the semantic conclusions or
runtime behavior. Open work is tracked in the
[continuing frontier](static-frontier-25492057.md).
