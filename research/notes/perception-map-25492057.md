# Perception, observation memory and entity inputs — build 25492057

This continues the [systems pass](systems-map-25492057.md), using the same
fingerprinted binaries and read-only Ghidra project. Addresses below are game
RVAs unless marked editor. The [static frontier](static-frontier-25492057.md)
remains open. Decompiled bodies and extracted resources stay in `local-work`.

## Actor components now have constructor evidence

The actor constructor `0x7EF100` installs shared default objects. Static
initializers construct those objects with named RTTI constructors:

| Actor pointer | Type | Default object | Initializer → constructor |
|---|---|---|---|
| `+0xDB8` | `Actor::eFsm` | `0x157C230` | `0x574F0` → `0x7FB2B0` |
| `+0xDC0` | `Actor::eCover` | `0x157D860` | `0x574C0` → `0x8987E0` |
| `+0xDC8` | Brain | initially null | Already mapped in earlier passes |
| `+0xDD0` | `Actor::eSensor` | `0x157D4F0` | `0x57510` → `0x8733F0` |
| `+0xDD8` | `Actor::eWeaponry` | `0x157D910` | `0x57530` → `0x806040` |
| `+0xDE0` | `Actor::eChassis` | `0x157D0C0` | `0x574A0` → `0x83F140` |

These shared defaults are not evidence of a usable owned component on every
actor. Concrete attachment/replacement and lifetime still need review. Editor
actor constructor `0x671300` provides a separate comparison anchor.

The previously unnamed decision multiplier now has a concrete source:
`owner->cover + 0x24` is the active cover candidate's **group** (`candidate+4`).
The human A/B state rule uses `k=1.5` for groups 1 or 2, otherwise `k=1`.
Those groups correspond to cover kinds 1..8 below; bush/terrain kinds do not
receive this multiplier. This identifies a direct link from current cover
state to advance/retreat thresholds.

## Candidate collection and persistent observations are separate

The 0x18-byte transient candidate has diagnostic field names recovered from
sensor slot 16 `0x873D60` (editor `0x6F7790`):

| Offset | Field |
|---|---|
| `+0` | Entity pointer |
| `+8` | Conditional actor pointer |
| `+0x10` | `candidate.visible` |
| `+0x11` | `candidate.inRangeForAlly` |
| `+0x12` | `candidate.requestRecognize` |

`0x87AE10` first looks for an existing subject. For a new relation-2 actor,
the planar range predicate is required, `requestRecognize` is set, and the
candidate is appended with visible initially false. Other new candidates need
entity `+0x38` bit 6 and either an actor view or the range predicate. Helper
`0x8784E0` performs optional deduplication and append; it does **not** itself
perform a visibility test. Existing subjects receive scan/visibility bits
according to their relationship flags.

`0x879630` tests candidates through enabled visors. It first checks strict
3D distance against target-dependent effective range `0x88EA30`, then calls
detector `0x87C790`. Successful candidates get `visible=1`; existing eligible
subjects get flags `+0x10` bit 29. Global `0x1481439`, initialized to 1, chooses
whether these tests occur in scheduled look-around work or later in the main
commit path. No direct write references were found; indirect/configuration
writes remain possible.

Sensor slot 13 `0x873B00` (editor `0x6F73A0`) handles the main-side work:

- The inform task uses requested delay bounds **0.25..0.75**.
- If byte `+0x1B` marks finished look-around work, clear it and call commit
  routine `0x878AE0`.
- Brain task bounds are **0.5..1.5**. Return true if observations were committed
  or the brain task is due. The earlier actor/brain dispatch consumes this gate.

Byte `+0x1C` is named `forceUpdate` by serialization. Slot 14 `0x873CB0` changes
mode byte `+0x1A`, refreshes task schedules, and starts a three-unit deadline at
`+0x358` when the mode is false. Expiry sets the mode true. `0x875580` requests
skip count 3 for look-around, inform and brain tasks while this mode is true,
zero otherwise. `0x1CEC40` replaces an existing skip count only when it is below
one or explicitly forced. This is an observed reduced-update mechanism;
the exact public/state name of the mode is not recovered.

## Persistent subject fields and memory

`0x878AE0` turns candidates into subjects through `0x874570`, which searches
all four vectors and otherwise calls sensor slot 19 with initial category 1.
Commit then updates visibility, refreshes position and recognition, shares
information, recategorizes entries and removes stale observations.

The following meanings are supported by producers and consumers; they are
analyst field names unless quoted diagnostic names:

| Subject field | Observed role |
|---|---|
| `+0x28..0x30`, float3 | Last-known position, also usable without a current entity |
| `+0x34`, float | Position uncertainty/radius; zero on direct visible refresh |
| `+0x38`, float | Accumulator raised by `0x891C00`, threshold 100; decays by elapsed duration × 3 when no increment |
| `+0x5C`, uint32 | Equipment/combat observation timestamp; also refreshed by selected signals |
| `+0x60`, uint32 | Last visibility transition time |
| `+0x64`, uint32 | Last position/observation update time |
| `+0x68`, uint32 | Time of signal-related transient flags |
| `+0x78`, int32 | Registered auxiliary observation handle, -1 when absent |

The accumulator's producer remains a lead; calling it suppression, identification
or morale would be premature.

Flags in dword `subject+0x10`:

| Bit | Reviewed meaning / effect |
|---:|---|
| 16 | Marked for removal; excluded from later processing |
| 17 | Pending information eligible for inform processing |
| 18 | Marked by the spatial scan as a candidate for this update |
| 19 | Position-only subject, without a required entity pointer |
| 21..22 | Current category index |
| 23 | Eviction mark used by retention/inform processing |
| 25 | Auxiliary observation registration exists |
| 26 | Auxiliary stale/hidden-state flag submitted to its registration manager |
| 28 | Known position is present |
| 29 | Current visibility for this update |
| 30 | Previous/committed visibility, copied from bit 29 when it changes |
| 31 | Pending linked-actor visibility propagation, cleared after processing |

Bit 27 participates in recognition and auxiliary visibility logic; its exact
meaning is still deliberately unnamed. `0x87A320` can propagate visibility
through entity `+0x560` and that object's `+8` link. Naming that link as a
particular crew/parent relationship needs its own type/lifecycle evidence.

Visibility transitions write `+0x60`. A currently visible subject gets current
entity position, `+0x64=now`, zero uncertainty and bit 28. Delayed recognition
uses `max(distance / sensor.range2F8 * sensor.factor2FC, sensor.minimum300)`.
Signal flag `subject+0x14` bit 9 can bypass the delay. Three transient flags in
that dword clear after **0.4**, **1**, and **2** duration units respectively.

Unseen subjects expire against sensor `+0x304`, initialized to **120**, or can
be evicted earlier. The four category limits are **10, 20, 20, 5**, used for
eviction and admission of shared information; they are not instantaneous hard
caps on visible subjects. `0x878320` computes eviction priority:

`age / 60 + (3*uncertainty + distance3D) / (100*spatial_scale) + (recognized ? 0 : 0.5)`.

Larger values are selected for eviction among unseen observations when over
the category limit. Removal is staged, so do not assume one call immediately
shrinks a list to its nominal limit. Category reassignment is exactly:

| Category | Predicate using flags dword `+0x14` |
|---:|---|
| 2 | Bit 2 set (relation-2/support class) |
| 1 | Bit 0 clear, or bit 1 set |
| 3 | Otherwise, entity exists but actor pointer is null |
| 0 | Remaining recognized actor observations |

`0x8783F0` creates position-only observations in category 1, storing position,
uncertainty and time. It reuses the oldest such entry once sensor `+0x324`
(default **4**) is reached. This explains how the brain can react to a location
without retaining an identified live actor.

## Information sharing and lifecycle

`0x879C50` collects entries with known-position and inform bits set, no eviction
mark, a live entity, and relationship bit 3 clear. It makes at most five random
attempts to select a nearby eligible ally from category 2, then sends **one**
signal of type 8 through that ally's sensor slot 17. Attempts can repeat the
same recipient; this is not a broadcast to five allies.

Slot 17 `0x877D20` dispatches type 8 to `0x87A090`. The receiver uses category
capacity and eviction priority for admission, copies newer position/equipment
timestamps and observations, and rechecks relationships from its own owner.
Other signal types update uncertain positions, transient flags, reveal times
and recognition; their producer names remain to be traced. Do not collapse all
of those types into gunshots without publisher evidence.

`0x87A3B0` updates a per-player observer count in the target actor's array at
`+0xBF8`, indexed by owner player byte `+0xDA8`, stride **0x18**. Count is a
signed 16-bit member at per-player `+0xA`; a deadline at `+0x14` also keeps the
per-player visible state alive. This is a separate layer from one sensor's
subject visibility.

`0x8752D0` also notifies an active enemy brain through `0x9036A0` when the
observer sees it. The downstream behavior is a separate lead; the helper is
not itself a reveal timer.

Slot 20 `0x87A4D0` detaches a subject: clears the observer contribution, removes
any auxiliary registration, unregisters the entity listener, and nulls entity
and actor pointers. It does not itself free the subject. `0x8754F0` performs
further cleanup, invokes slot 20, marks removal, decrements the reference count,
and conditionally destroys the object. Editor `0x6FE300` explicitly asserts that
detachment is outside a quant worker thread.

## Vision inputs and rule evaluation

`0x88F2D0` loads `set/vision/settings.set` and `set/vision/vision.set`.
`0x88C810` parses named custom terms; `0x88EAC0` parses each `Actor::eVision`:

| Vision offset | Property |
|---|---|
| `+0x70` | Conservative maximum range derived from rules/modifiers |
| `+0x7C` | `uncover` distance, spatially scaled |
| `+0x80`, `+0x84` | `h_fov`, `v_fov`, converted from degrees |
| `+0x88` | Base radius, spatially scaled |
| `+0x90..0x98` | Compiled conditional-rule vector, stride 0x10 |
| `+0xA8..0xB0` | Multiplicative modifier vector, stride 8 |
| `+0xC0`, `+0xC4` | `wide` mode and curve power, constrained to (1,10] |

`0x88DB50` compiles nested rules into entries `(condition, value, false-next,
terminal)`; `0x88DFE0` compiles modifiers. Evaluator `0x88E4C0` follows the
conditional chain, retains the matching value, then multiplies every applicable
modifier and the base radius. Rule order matters; this is not a maximum across
all matching rules.

`0x88E5A0` gives the fine-grained field contract for conditions:

| Condition ID/name | Predicate |
|---|---|
| 0 `still` | Actor `+0x1C0` bit 2 |
| 1 `stand` | Chassis `+0x2C == 0` |
| 2 `squat` | Chassis `+0x2C == 2` |
| 3 `crawl` | Chassis `+0x2C` is 1 or 3 |
| 4 `stealth` | Human type bit and human `+0x1420` bit 5 |
| 5 `firing` | Elapsed time since Weaponry `+0x90` is less than 1 |
| 6 `cover` | Cover `+0x24 != 0` |
| 7 `visible` | Prior-visibility byte supplied in the condition context |
| ≥10, custom `firing` | Same recent-fire gate, last weapon `Weaponry+0x98`, and weapon property/tag lookup at `weapon+0x868` |
| ≥10, custom `actor` | Tag lookup in entity definition `actor+0x548`, vector `+0x330..0x338`, stride 0x60 |
| ≥10, custom `breed` | Human type, breed pointer `human+0x1390`, property lookup at breed `+0x128` |

Custom term records have stride **0x88**, condition ID at `+0x20`, property/tag
key at `+0x28`. Direct IDs 8/9 fall through in this evaluator; actor/breed require
a custom term carrying a property key. Although the parser can construct custom
aliases for other built-ins, this particular callback only handles custom types
5, 8 and 9. Do not assume all accepted aliases are evaluated identically.

`0x88EA30` additionally applies initialized factor **0.13** when two global
gates are clear and an environment/illumination query fails at the target
position. That query's exact environmental semantics still need review.

The installed `gamelogic.pak` supplies all five vision files and `set/detect.set`;
no duplicate of those members was found in the other top-level resource archives.
Hashes are preserved in the manifest. These are archive defaults, not a claim
about active mod/mission overrides. The files include separate spotting/hearing
presets and multiple human vision fields. Standard human field defaults include
150° horizontal, 180° vertical and uncover distance 20; peripheral vision uses
240°, 120°, uncover 10 and a smaller range multiplier.

`0x876610` chooses a reveal-duration table by target definition tag, converts
distance to world units, clamps at endpoints and linearly interpolates between
knots. `detect.set` documents meters/seconds and supplies separate `attack` and
`visible` rules. Exact mapping of its two native table indices to those names
still needs the loader. These duration comments alone do not prove the main
quant counter's wall-clock/pause behavior.

## Geometry used by actual visibility

`0x87C790` selects the grid detector when global `0x1481438` is nonzero
(file default **1**). In the alternate branch it builds a volume callback but
ultimately returns AL=0 in this build; assembly confirms that path. Its callback
side effects remain a separate lead, not a reason to assume useful visibility
from that return value.

Grid detector `0x87D1F0` traverses entity volume pointers `+0x510..0x518`.
It tries a cached `(volume_index, point_index)` pair of signed 16-bit values,
then eligible volume centers. It excludes volume `+0x28` bit 5, `+0xC` bit 14,
and requires at least one of `+0xC` bits 0, 5 or 6. Local geometry samples are
transformed with the volume matrix at `+0x58` and position at `+0x7C`.

`0x87D4C0` transforms world XY to grid coordinates and applies `0x87D7E0`:
strict radius, then ordinary wrapped-angle half-FOV or the wide-mode curve.
A point in the observer's own cell passes. Other points use `0x87E050`, an
integer grid-step ray walk. The ray must be above sampled occlusion height at
each step; equality blocks. Within `uncover` distance, grid type 2 uses terrain
height instead of that cell's obstacle height. Vertical FOV also bounds the
initial ray through `0x87E310`.

The height-query sibling `0x87DD70` instead raises the required endpoint height
to clear intervening cells. This links the previous pass's cover height
measurement to the same occlusion grid. Cell type 2 corresponds to the raster
used for named bush-cover candidates; tracing grid construction is still needed
to establish every material assigned that value.

Entity `+0x550` is passed to paired exclusion helpers `0x887CA0/0x887CC0`
around the query when its byte `+0x1A` is active. The lower grid mutation helpers
and their thread-local storage contract remain leads. A custom brain should
not assume this is a side-effect-free public query.

## Relationship values and cover occupancy

Relationship values are now named with both display and use evidence:
**0 neutral, 1 enemy, 2 ally**. `Helper::Selection::eSF_Relation` display method
`0x9E7800` names filter values 1/2/3; predicate `0x9E84D0` explicitly compares
native relation to `filter_value-1`. The different enum domains matter.

The native matrix supports 18 player IDs. `0xA46B50` initializes its default
18×18 values to 1. Same-player queries return 2 independently of diagonal
entries; invalid/disabled targets return 0. `0xA46B00` writes a pair symmetrically;
scene parser `0xA46ED0` can load individual diplomacy cells. Multiplayer setup
`0xD0ECF0` builds relations from player/team state. Exact active values are data,
not recoverable from the initialized file alone.

Cover-kind parser `0x8A9720` is a case-insensitive lookup in pointer table
`0x1482170`; missing/unrecognized names return kind 0:

| Kind | Name | Group (`candidate+4`) |
|---:|---|---:|
| 0 | none | 0 |
| 1 | look_left | 1 |
| 2 | look_right | 1 |
| 3 | look_2way | 1 |
| 4 | snipe_cover | 2 |
| 5 | squat_lie | 2 |
| 6 | hide_stand | 2 |
| 7 | hide_squat | 2 |
| 8 | open_lie | 2 |
| 9 | bush_stand | 3 |
| 10 | bush_squat | 3 |
| 11 | bush_lie | 3 |
| 12 | terrain_lie | 4 |

The table closes the names of the two raster/terrain sources from the systems
pass. Candidate `+8/+0xC` correlate with stance but their precise consumer roles
remain open; do not substitute chassis's different numeric stance domain.

`0x8A5C80` claims the candidate's XY in occupancy index `0x1482088` and records
actor UID (`+0xDAA`) at candidate `+0x24`. `0x8A5CB0` releases it and clears that
field. Cover component entry `0x898AD0` copies a 0x88-byte candidate to
`cover+0x20`, updates component state, claims it, and sends actor event 0x21.
Exit `0x898C50` releases and resets the candidate and sends the same event.

`Order::eMoveToCover` execution `0x8CE670` first checks occupancy, claims a
temporary destination, and starts its child motion order. Cancel/cleanup phase
2 releases an outstanding claim. Normal child completion releases the temporary
claim and installs nonzero cover through `0x898AD0`. Thus occupancy covers both
an in-progress destination and a resident cover position.

Object-edge source `0x8A73B0` subdivides boundary segments by sampled height
changes and spacing, then calls `0x8A8040`. Circle source `0x8A7140` samples around
the circumference. Both store boundary geometry in candidate `+0x38/+0x40`
and mark `+0x30`. Detailed kind selection inside `0x8A8040` and height helpers
remains a productive follow-up.

## Reproduce

Use `*.perception-pass.tsv` and `*.perception-anchors.tsv` with the same
`run_headless.py export --focused` commands as the systems pass, changing the
output suffix to `-perception`. Verify `perception-map.json` using
`verify_fine_map.py`, with `--resources-dir` pointing to the installed resource
directory. The manifest checks the six archive members as well as binary,
instruction, RTTI, table and constant evidence. Verification does not establish
semantic completeness or a runtime-safe API.

Verification passed for 74 method entries and 278 instruction locations across
91 game and 16 editor exports, seven RTTI tables, the cover-name table, five
constant values, five byte sequences, and six resource hashes. Independent LLVM
disassembly also matched all instruction starts in the vision predicate,
move-to-cover execution and observer-count methods.
