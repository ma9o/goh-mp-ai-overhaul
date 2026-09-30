# Infantry geometry, firing trials and equipment choice — build 25492057

Reviewed static pass with open dependencies below. All addresses are game
RVAs unless marked editor. This follows the infantry policy and armor/events
passes. Descriptive names are analysis labels unless supported by native text or
RTTI. A reviewed caller does not establish every transitive dependency.

## Firing feasibility is a trial with temporary state

`0x940130(context, XY)` forms `(X,Y,0)` and calls `0x81EEE0` on the context's
weapon `+88 -> aimer+A8`, passing its target at context `+8`. Only result3 counts
as feasible. `0x81EEE0` builds a `0x30`-byte TargetAccessor context:

| Offset | Source |
|---|---|
| `+0` | optional result-time pointer |
| `+8` | weapon, aimer `+10` |
| `+10` | optional trajectory/query argument |
| `+18` | bone selected from aimer `+20` or `+28` by global `0x1480ED0` |
| `+20` | owning entity, weapon `+90` |
| `+28` | optional trial origin |

The diagnostic in `0x820A40` names `TargetAccessor::Try` and `shooterBone`.
Without the required weapon/bone state it returns0, sometimes issuing that
diagnostic. The ordinary path saves the owner's root-bone matrix, creates a
temporary target, starts a bone trial, and uses entity slot25 to construct a
transform at the proposed origin facing the target. It applies that transform,
evaluates aiming and finally restores the saved matrix. Without a proposed
origin, additional target/orientation gates may perform a second trial.

Trial evaluator `0x821490` calls aimer slots16/17 around bone solve helpers
`0x5C5760/0x5C5400/0x5C6090`. A second bone can be refined up to three times,
stopping when error is negligible or no longer improving. An alternative branch
notifies the owner with event2B. Exact bone-limit/error semantics remain open.

`0x820A40` initially returns1 after the trial. With its success byte set it
returns2 unless measured error is strictly below weapon-definition `+15E8`.
Below that threshold it checks `0x820030`; when that returns false,
it asks the aimer and shooter to construct trajectory/collision results,
then delegates to `0x821AD0`. The complete result names are not recovered; these
are branch contracts, not a fabricated public enum.

`0x821AD0` consumes collision records of stride `0x48` and XYZ trajectory points
of stride12. Its order of decisions is significant:

1. Scan collision kinds3/4 with a nonnull actor (type bit1), actor `+DB4` bit8,
   and relation2, already established as ally. Return3 if it is the requested
   target entity; otherwise return4. Copy the record to optional output.
2. Scan for the requested target entity in kinds3/4 and return3 on a match.
3. Measure point-to-segment 3D distance from the target position to every
   trajectory segment. Strict distance below `0.5*spatial_scale` returns3.
4. Return2 for an empty collision vector, otherwise copy the last record and
   return5.

This identifies both an ally rejection and a position-tolerance success path.
It does not mean all allies are rejected: the explicit actor flag and collision
kind gates remain essential. Shooter-specific collision construction and aimer
implementations remain static leads. Calling this code as if it were a pure
position query would overlook its temporary bone/world state changes.

## Detours try poses, cover and reachable points

`0x93ED30` requires a weapon at context `+88`. It obtains target XY and weapon
range, builds bounds at context `+D8/+F0`, and intersects them with optional
context `+C0` when its radius `+D0` is nonzero. The resulting sampling circle
lives at `+A8` (center `+B0/+B4`, radius `+B8`). Empty intersections fail early.
Native bounding-circle helper `0x6144E0` and exact caller-supplied bound meanings
were dependencies in this pass. The later
[path-acceptance pass](path-acceptance-map-25492057.md) maps the circle formula,
sampler, input bounds and graph reachability marks, plus route distance sampling.

It snapshots human pose and the bone hierarchy, then tries alternatives in this
order, subject to stance and eligibility bytes at context `+91..+94`:

- An in-place lower-cost pose change can return result-kind1 and selected pose.
- Query nearby cover within `min(result radius, 10*spatial_scale)`. Only cover
  inside all active bounds reaches the cover/pose trial. Up to five such
  candidates are attempted; out-of-bounds candidates do not consume this budget.
  Cover-kind mask bytes begin at context `+95`. Success copies the `0x88`-byte
  cover descriptor to output `+10` and writes result-kind3.
- Otherwise build a pather network around the actor, prepare a firing pose and
  sample up to ten points from the intersected circle. Reject outside bounds,
  environmental classes3/4/5, missing network nodes, `node+20 >>22` unequal to
  network `+28`, or nodes without bit21. Up to five eligible failed firing trials consume a
  second budget. Success writes result-kind2 and XY at output `+8`.

The decompiler duplicated the random-point virtual call. Assembly has one call
to bounding-object slot14 at `0x93F99B` per loop iteration; the next indirect call
is the containment test. Candidate budgets above are separate from RNG draw
counts inside the sampler itself.

Vertical cover kinds1/2/3 try direction-mask bit8 at one lateral offset, then bit7
at the opposite offset, then mask20 at the center. Kind4 requires mask40 and pose
eligibility `+94`; kinds5/7/10 use squat;6/9 use standing;8/11 try lying. These
checks make cover geometry and allowed firing pose part of feasibility, beyond
the general cover score.

If the point candidate has a successful route of more than one waypoint,
`0x940780` shortens the move toward an earlier firing location. It performs seven
unrolled midpoint trials along route length, maintaining a feasible upper bound,
and chooses a point no closer than one spatial unit along the route. This is a
bounded search; it does not prove the first feasible point on a nonmonotonic
line-of-fire path. The detour can then retry a preferred lower pose at that point.
Normal exits restore the bone snapshot and previous human pose.

## Pose and animation domains differ

`0x940560` maps actual chassis stance0/1/2 to delivery pose3/1/2. If that requested
pose is already active and the current FSM contains the weapon's `+1438` tag,
it directly runs the firing trial. Otherwise it snapshots, temporarily changes
pose, tests, and restores. With no weapon or target, this wrapper returns true.

`0x940040` requires a human and the corresponding eligibility byte at
`context+90+pose`. Its recognized mappings are:

| Delivery pose | Human pose `+13E8` | Animation selector |
|---:|---:|---:|
| 1 (lying/crawl) | 2 | 3 |
| 2 (squat) | 1 | 2 |
| 3 (standing) | 0 | 0 |
| 4 (cover firing pose) | 0 | 1 |

It sets handler `+30` bit1 while selecting/applying the temporary animation,
then clears it. `0x961360` uses that flag to bypass its ordinary queued/blended
animation path and instead create, apply and destroy a direct animation object.
The ordinary path increments handler `+560` and uses `0x962B50`.

Snapshot visitor `0x941270` recursively traverses bone `+158/+160` children and
stores records of size `0x38`: bone pointer followed by its `0x30`-byte transform
from bone `+14`. Cleanup restores each transform with `0x5C0F10`, then restores
the human pose via `0x937FE0`.

`0x937FE0` changes constraints only when pose changes and all five required bone
pointers `+13F0/+13F8/+1400/+1408/+1410` exist. For poses0/1, `+13F0` limits are
[-30,30]; for pose2 [-10,10]; for pose3 [0,0]. It also switches bone modes and
limits on `aim_ref_h`. These are stored angular values; conversion is a bone
helper dependency. Separate `+1418` limits are [-5,5] only for pose0, handler
state8, no cover and `throwable` equipment; otherwise [-20,20].

Animation selector `0x96A4B0` produces six tags using selectors0/1/2/3.
The standing/squat/lying builders `0x96A730/0x96AE00/0x96B120` branch on handler
weapon/animation state `+424`, modifier `+42C`, flags `+30`, and a variant counter
`+34`. Some selector paths increment that counter even during a pose trial.
Restoring transforms is therefore not evidence that every handler field is
unchanged. Native tag names and full handler-state producers remain open.

## Cover heights and edge boundaries

Let S be the spatial conversion value read during the small initialization
sequences. LLVM disassembly establishes these writes; the writable destination
globals are not immutable constants:

| Global | Initial expression | Code sequence |
|---|---|---|
| `1584FE4` | `0.3*S` minimum accepted height | `61750` |
| `1584FF8` | `0.4*S` | `61C70` |
| `1584FE0` | `0.7*S` | `61730` |
| `1584FFC` | `0.85*S` | `61CB0` |
| `1584FDC` | `1.26*S` | `61710` |
| `1585000` | `1.35*S` | `61C90` |
| `1584FD4` | `1.64*S` tall threshold | `61790` |
| `1584FCC` | `0.35*S` edge outward offset | `61910` |
| `1585008` | `1.5*S` cross-edge probe length | `61BD0` |
| `158500C` | `1.05*S` lateral tall-cover test | `61930` |
| `1585038` | `0.1*S` boundary refinement tolerance | `617D0` |

`0x8A6EF0` snapshots seven ordered heights into `0x1585040..58` on its first
thread-safe initialization. It casts horizontal probes at terrain height plus
each value using `0xA23D60`. The first three probes have special logic: when all
are clear it returns0; when the first is blocked but the next two clear it
returns the minimum height. Later first-clear probes return the preceding test
height, and an entirely blocked sequence returns the top height. The full branch
behavior is retained in the evidence; this is quantized geometry, not a smooth
height measurement. Configuration changes after first initialization would not
automatically rebuild that cached array.

`0x8A65B0` converts the sampled height to a nominal kind: below0.4*S→8 open_lie;
below0.85*S→5 squat_lie; below1.35*S→4 snipe_cover; otherwise height class2→7
hide_squat and class3→6 hide_stand. An unmatched class returns0.
`0x8A8040` additionally rejects below0.3*S and applies common location and
duplicate gates. For tall class3 it probes two lateral positions, requiring
both a route/clearance predicate and an unblocked cross-edge ray. Left-only,
right-only, both and neither select kinds1,2,3,6 respectively. The names are
from the previously verified kind table: look_left/look_right/look_2way/hide_stand.

For an edge with start P and direction U, `0x8A7C10` samples at
`P+t*U+0.35*S*(Uy,-Ux)` and probes inward by `1.5*S*(-Uy,Ux)`.
`0x8A7CD0/0x8A7E40` bisect the left/right boundary of a same-kind interval until
the remaining gap is at most0.1*S. Record `+0/+4` are interval endpoints and
`+8` is an **integer kind**; decompiler float comparisons here are wrong.

Edge generation `0x8A73B0` rejects edges shorter than0.4*S, samples interior
intervals, refines kind boundaries and generates cover points. Generated
candidates set byte `+30` and preserve edge endpoints at `+38/+40`.
Spacing and alternating inward/outward placement arithmetic is still partially
reviewed. Its decompiler falsely displays writes to read-only constant3; assembly
only reads that constant, so those displayed global assignments are discarded.

## Ground-weapon preference includes proficiency

`0x958610(human,itemHandle)` returns a float in XMM0; its decompilation omits
most return calculations. Weapon cast `0x82A490` identifies `eStuffWeapon`.
If that definition's `+15F8` bit0 is set and item-handle secondary count `+18`
is zero, score is0. The exact definition flag name remains open.

Otherwise it scans human breed `+528` ability-manager entries (`+548/+550` in
the breed), stopping at the first entry whose ability slot6 matches the item
definition and whose info slot5 supplies a level pair. Fallback is0.5. For a
successful pair `(level,total)`, level<=1 also gives0.5; level>1 gives
`float(level)/float(total)` when total is nonzero, otherwise0. No final clamp is
performed here.

`Human::eAbility_Weapon` slot6 `0x97AB80` compares the definition's `+EC8` tags
against ability `+88` via `0x556F20`. `eInfo_Leveled<weapon>` slot5 `0x97B5D0`
counts records of stride `0x100` in ability `+68/+70`, determines the one-based
position of info `+10`, adds an integer Veterancy bonus from params `+28`, and
caps at the total level count. An unmatched current pointer reaches total count;
an empty vector starts at0. The exact Veterancy property name and tag-match
semantics remain dependencies. Selected editor methods `0x802FB0/0x803A60`
have the same reviewed operations and offsets.

Ground-pickup builder `0x945520` stores records of stride `0x18`:
score `+0`, 3D distance `+4`, entity pointer `+8`, primary definition pointer `+10`.
It excludes zero scores and sorts with `0x9469C0`. The ordering is preferred
definition (compared with human `+12A0`) first, then descending score, then
ascending distance. The comparator appears consistently in insertion, median,
partition and heap paths (`0x946240/0x946350/0x946060/0x946820`). The winner's
entity at `+8` becomes the pickup order target; `+10` is not that entity.
The preference is remembered from dropping equipment. `0x93AA10` sees a nonempty
holder and a definition tagged `weapon`, saves its definition to human `+12A0`
and item entity ID (`entity+C0`, or-1) to `+12A8`, then performs `drop_from_hand`
and optional replication. `0x936370` initializes definition0 and ID-1.
`0x944520` tries the saved entity first when the actor has no current weapon,
its caller option permits recovery, validation succeeds and distance is strictly
below10*S. This builds `take_old_weapon_from_ground`. Regaining a weapon clears
both fields; failure of the saved-entity validator clears the ID. The definition
can remain as a preference for other ground pickups. The rest of this method's
mount/change policy was mapped in the preceding infantry pass; the
weapon-definition/ammunition flag remains open.

## Idle requests and knockdown movement

`0x95FCC0` is diagnosed `eFsmHandler::RequestIdle`. It returns immediately when
the current FSM tag vector already contains `idle`. With no `idle-prepare`, it
requests `idle-prepare`; while prepare is present it requests `idle` only when
handler `+560` is0. It can also emit `eFsmRequestIdleCommand`. Initializers
`0x584A0/0x584D0` prove the two tag names. The counter increment in `0x961360`
links the gate to pending animation work; all decrements remain to be traced.

`0x95FC00`, reached by active knockdown, forwards the requested movement type
to chassis slot49 and then calls slot50. For foot chassis those are
`0x84D4D0` (`eChassisFoot::OnMovementTypeChanged`) and thunk `0x84D920` to
`0x84DBA0`. The first writes actual/requested stance `+2C/+30`, sends entity
notifications17 and18(kind6,newstance), and refreshes placement; the second
re-evaluates movement mode and can replicate it. Knockdown passes type1 (crawl).
The full swim/placement and speed-mode helpers remain open.

Editor counterparts `0x6D0B40/0x6D0F20/0x6D11B0` agree on the reviewed movement
fields and refresh sequence. Editor mode refresh additionally asserts that the
chosen mode is not `MM_NONE`; the game omits that assertion. Installed FSM request
definitions give `idle` and `idle-prepare` priority0 and `keep-knock-down`
priority46 with duration2.

## Evidence and remaining work

[Machine-readable map](../artifacts/build-25492057/human-geometry-map.json) and
[validation](../artifacts/build-25492057/human-geometry-validation.json) preserve
47 reviewed game contracts/slices, five editor pairs,77 fields,13 constants,
six RTTI tables, the cover-kind name table,12 code/literal byte sequences and
one resource hash. Frozen local exports contain66 game functions and five
editor functions; exported but unreviewed helpers remain discovery evidence.

Independent LLVM disassembly agrees with2582 Ghidra instruction starts in ten
selected functions, after normalization of standalone LOCK prefixes. Specific
assembly checks resolve the missing float score, integer cover kinds, a duplicated
virtual call, and false writes to a read-only constant. The11 small height/offset
initialization sequences are separately reviewed in LLVM and byte-checked; they
are not asserted to be Ghidra-defined functions.

Static leads remain in shooter collision/ballistic production, bone constraints,
weapon readiness, detour-bound construction, pather search, full edge spacing,
animation tag/state producers and queue completion, equipment flag definitions,
Veterancy bindings and ability level mutation. Structural validation does not
prove semantic completeness, safe runtime ABI or object lifetime.
