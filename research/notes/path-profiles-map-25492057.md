# Path profiles, obstacle names and vaulting — build 25492057

This pass connects the numeric inputs in the [path acceptance map](path-acceptance-map-25492057.md)
to native parsers, installed declarations and the soldier's animation order.
All addresses are RVAs; offsets are hexadecimal; virtual slots are decimal.
This is static evidence, with no game or editor execution.

The [manifest](../artifacts/build-25492057/path-profiles-map.json) records 33
game/editor method pairs, 33 fields, native literals, property registration bytes,
RTTI and the direct-reference census. The [resource audit](../artifacts/build-25492057/path-profile-resources.json)
expands 116 profiles and 21 obstacle descriptions into 2,436 profile/obstacle
costs. Its source members are hashed; mounted overrides are not assumed.

## Profile layout and loading

`8CA430 / 750470` opens `/set/pather.set`, section `pathers`, with property
registry `15852F8 / 13EC9C8`. The installed member is `set/PATHER.SET` in
`gamelogic.pak`. Each profile occupies F8 bytes in the vector at
`1585310 / 13EC9E0`.

| Offset | Meaning | Initialization |
|---|---|---|
| 0 | Owned name string | Empty |
| 20 | Query radius | 30 native units |
| 24..5C | 15 Height cost floats | All 0 |
| 60..9C | 16 terrain cost floats | Index 0 = -1; others 0 |
| A0..DC | 16 terrain ratio floats | Index 0 = -1; others 1 |
| E0..EF | 16 terrain permission bytes | All 1 |
| F0 | Flags | Low five bits cleared |
| F4 | Priority integer | 0 |

The registration stubs bind these exact tokens:

| Token ID | Name | Effect |
|---|---|---|
| B2 | Profile | Append named row |
| B3 | Network | F0 mask 1 |
| B4 | LinearPath | F0 mask 2 |
| B5 | Human | F0 mask 4 |
| B6 | BattleCrusher | F0 mask 8 |
| B7 | SkipImpregnable | F0 mask 10 |
| B8 | Priority | F4 integer |
| B9 | Height | Read exactly 15 floats into 24..5C |
| BA | Terrain | Named ratio and optional integer permission |
| BB | Radius | Authored float × spatial scale |

The game scale at `14776C0` has image value 20. Thus base authored radii .3
and .4 correspond to 6 and 8 native units with that scale. This does not establish
that the global is immutable.

For a Terrain declaration, resolve its name to a byte ID, clamp ratio K to
[0,1], then:

```
if abs(K) < binary32(0.00001):
    terrain_cost[id] = -1
    terrain_ratio[id] = -1
else:
    terrain_cost[id] = 1 / powf(K, 0.5) - 1
    terrain_ratio[id] = K
```

The consumer adds a baseline 1 to terrain and Height costs. With Height cost 0,
this makes its terrain multiplier `1/sqrt(K)`. The file's introductory comment
says `1/K`; the executable is the evidence for the implemented formula.
Assembly passes .5 in XMM1 to the power helper and uses DIVSS/SUBSS afterward
(`8CA94B..8CA960`, editor `7509BB..7509CD`).

`66D590 / 4E2BA0` defaults the optional permission to true when no numeric token
is next. A present numeric argument must be an integer; nonzero means true.
Token probe `66CB00 / 4E20C0` treats token classes 10 and 11 as numeric. Permission
only affects the earlier cost consumer when context E8 mask 1 is set.

The previous map's 16-element *read window* at 24 now has an authoring count:
there are exactly 15 Height entries. Packed slope index 15 aliases terrain
entry 0 at offset 60. Whether every cell producer excludes index 15 still needs
mapping; the resource's angle comments are not proof of that producer.

Name lookup `8C9B60 / 74FB30` compares exact bytes and returns the first matching
index as a byte, or FF. By-ID lookup `8C9B00 / 74FA70` uses `index*F8`.
GEM asserts bounds; the game body does not. No native <=255 profile-count guard
has been established.

Context bind `8C9D50 / 74FD20` writes C8, stores the raw profile pointer at D0,
clears E8 low five bits, then replaces E8 bit 5 with profile SkipImpregnable.
Higher bits survive. It also assigns a shared grid handle at D8/E0. The profile
loader destroys old names and resets/reallocates the vector, so D0 is not a
stable owned pointer across reload. Scene creation `A19CD0 / 8A1960` calls the
profile loader immediately before the obstacle loader; full reload lifetime is
not proved by this ordering.

## Native terrain IDs

The initial slice of `A44070 / 8CC950` explicitly constructs these rows:

| ID | Name | ID | Name |
|---|---|---|---|
| 0 | undef | 6 | swamp |
| 1 | ground | 7 | road |
| 2 | no_pass | 8 | no_pass_human |
| 3 | shallow_water | 9 | snow |
| 4 | water | A | forest |
| 5 | deep_water | B | slope |

Rows are 68 bytes: integer ID at 0 and a 60-byte tag at 8. The vector is
`159B9A0 / 14034C0`. `A46420 / 8CF100` scans tags; `A439B0 / 8CC2A0` returns
the matching byte, otherwise diagnoses an unknown name and returns AL=0.
`A435A0 / 8CBE70` reverses ID to tag, with a global empty fallback.
`A45560 / 8CDF20` clears both the larger FX table and these pathing tags.
The rest of terrain initialization and all packed-cell writers remain separate
work. This pass does not conflate pathing IDs with visual/physics terrain IDs.

## Obstacle costs and flags

`8CAB00 / 750B70` reads registry key `obstacles`; the installed declaration is
`set/registry/obstacles.reg`. It appends 40-byte rows at `1585418 / 13ECAE8`:
name at 0, flags at 20, and a float cost vector at 28/30/38.
`8CA220 / 750210` initializes every cost to **100000**, sized to the current
profile count. A nonnegative integer declaration overwrites its profile's cost;
a negative declaration leaves the previous value unchanged. In particular,
`-1` does not store a negative float and would not undo an earlier positive
declaration for the same key.

| Mask | Native obstacle flag |
|---|---|
| 1 | flag_damage |
| 2 | flag_human |
| 4 | flag_crush_in_battle |
| 8 | flag_skip_impregnable |
| 10 | flag_vaultable |

This names the policy in `8AE060`: context mask 2 skips Damage; mask 4 blocks
Human obstacles; mask 10 skips CrushInBattle. Profile Human + obstacle Vaultable
selects the special angle/shape path. SkipImpregnable participates in the
ordinary passability branch. The actor identity, relation and mask checks
before these branches remain as described in the preceding map.

The base `human`, `human_tpc` and `human_prone` profiles all have Network+Human
flags; tpc overrides radius .3 to .4. `human_flying` has Human but no Network
flag and its own Height table. These are data profile names; their names alone
do not prove all call sites that select them.

All four base vaultable obstacle descriptions (`unbreakable_vaultable`,
`building_vaultable`, `small_building_vaultable`, `construction_vaultable`) cost
5 for `human` and `human_flying`, and retain 100000 for `human_prone` and
`human_tpc`. The source's `flag_crush_in_battle` occurrences are commented out.

Obstacle name lookup `8CA2B0 / 7502B0` returns a byte index or FF. By-ID lookup
`8CA360 / 750360` indexes 40-byte rows; GEM asserts range. Registry iteration
order has not been bound to file declaration order: the audit records
`source_order`, not a claimed native obstacle ID.

## Network geometry completed from the previous pass

Edge updater `8B9670 / 73E830` writes 100000 if either endpoint's multiplier
34 reaches the sentinel. Otherwise, with node positions A/B, portal origin O
and portal delta V:

```
D = B - A
den = D.y*V.x - D.x*V.y
t = ((O.y-A.y)*D.x - (O.x-A.x)*D.y) / den   if abs(den) >= epsilon
t = 0                                                otherwise
t = clamp(t, 0.05, 0.95)
P = O + t*V
edge.cost = distance(A,P)*A.multiplier + distance(B,P)*B.multiplier
```

These are float fields/stores, despite integer-looking decompiler casts. The
chosen intersection is not a minimization over every possible portal crossing.

Bound inclusion `8BD570 / 742E60` tests every directed edge, including the
closing edge, and rejects `cross(P-prev,current-prev)>0`. Boundary points and
an empty point vector pass. This is a convex halfspace/winding test; general
nonconvex point-in-polygon behavior has not been established.

The configuration initializers name earlier smoothing gates:
`dbg_pathOptimize=1`, `dbg_pathOptimizeFrontToBack=0`, `dbg_pathOptimizeOld=0`,
and `g_pnetDirectionHeuristicsScale=0`. These are initialization values.

## From a vaultable obstacle to an animation order

RTTI ties `eBoundingObb2` vslot 7 to `5E9DA0 / 45B8D0`, which returns 13.
This identifies the previously numeric shape check. Its fields consumed here
are center 8/C, axis 10/14 and extents 18/1C.

`8F5E70 / 77BFD0` receives Move and the selected obstacle. Null clears Move
1AD. Otherwise it asks `938410 / 7BFBA0` for the `getover_middle` displacement.
A false feasibility result returns without changing 1AD. For a true result:

1. Choose N=axis if extentY>extentX, otherwise N=(-axis.y,axis.x).
2. Compute `d=abs(dot(N,obstacleCenter-actorXY)/dot(N,actorForwardXY))`.
   There is no local zero-denominator guard.
3. Sample the current compiled segment's endpoint through vslot 6 at fraction 1.
4. Require `d <= length3D(animationDisplacement)/2` and
   `d < distanceXY(actor,segmentEndpoint)`.
5. Clear 1AD, set 1AF and attach `Order::ePlayAnimation` with name
   `getover_middle`, scale 1, flags 80001, argument 4 = 0 and argument 5 = 1.

If feasibility passed but the geometry has not reached that trigger, set 1AD=1.
The displacement helper returns true without writing the output if the named
animation is missing or its vslot 4 cannot supply the data. The caller's
prefilled displacement survives that case; true is not proof of a lookup.

When data is available, the helper applies actor vslot 25 and calls
`938520 / 7BFCB0`. That helper writes end-start to the optional output **before**
checking feasibility. It requires abs(deltaZ) <= `g_humanAnimationDelta`, or
`g_humanAnimationDeltaManual` when chassis18 is nonzero. Both initialize to
85 native units. It runs `8AC3F0` with actor context, then a separate line cost
probe with the **human_flying** profile and context mask 2 (skip Damage).
If the first predicate succeeds and the returned object has a nonnull98 pointer,
it calls `71FAE0(pointer,false)`, probes again, then calls the helper with true.
There is no saved previous enable state in this wrapper. Final success is cost
<100000; lower query and clearance predicates remain open.

`71FAE0 / 59A9D0` walks entity A00/A08 pointer entries, selects entry38 mask40,
and dispatches `771880(entry+8,boolean)`. This reveals the scope of the temporary
change; the lower enable/registration lifecycle needs its own pass.

`8D2E80 / 759240` constructs a 70-byte PlayAnimation order. Name48 is owned,
scale38 is a float, flags3C are copied, and flag80000 sets base18 mask800000.
`8D3070 / 759440` is its vslot19 executor:

- Phase0 clears started68, prepares the human handler through95FC40 and may
  send synchronization command53 when69 is clear. Flag20000 also clears FSM.
- Phase0/1 polls FSM request mode2 with token157EBD0, owner
  `ePlayAnimation::Exec`, flags2 and parent `none`. If unready it returns started68.
  With flag20000 it finishes after readiness. Otherwise the first ready call
  sets started68 and requests the animation through5CD380.
- It returns false while the named active animation exists and vslot7 is true;
  a missing/inactive animation returns true.
- Phase2 cancels a started animation by name through5CDD60, may send the Drop
  synchronization command, then returns true.

Animation requests, active-object lookup and cancellation connect to the earlier
[animation control map](animation-control-map-25492057.md). FSM request readiness
connects to the [equipment/FSM boundary](equipment-actions-map-25492057.md).
Handler95FC40, request-token initialization, command reception and the reset of
Move1AF remain concrete follow-ups.

## Verification and remaining work

Read-only Ghidra exports contain 50 game and 42 editor functions. All 21/17
explicit candidates are exact entries; every export decompiled. Independent
LLVM disassembly agrees on all 7,985 instruction starts in the 66 paired method
bodies after normalizing standalone LOCK prefixes. The
[validator report](../artifacts/build-25492057/path-profiles-validation.json)
checks both executable fingerprints, entries, instruction locations, four RTTI
tables, 40 literal/registration byte sequences, eight float constants and both
resource hashes. Structural validation does not prove interpretations.

The resource extractor has three focused tests covering nested macro expansion,
permission/default handling, negative-cost overwrite behavior and rejection of
unreviewed forms. It models input floats as binary32; derived square-root costs
are estimates, not bit-exact native powf emulation.

Immediate leads: grid solver8AFA00; line/shortcut8AC950/8ACA00; clearance8AC3F0;
portal/smoothing helpers; packed slope producers; terrain ratio A0 speed
consumers and Priority F4 ordering; obstacle registration and entity flags;
profile/context reload lifetime; and the remaining PlayAnimation handoff.
Static mapping opportunities are not exhausted.

The [line/spatial follow-up](path-lines-map-25492057.md) now establishes that
`8AC3F0` returns true for blocked, and maps the line query, portal helpers,
spatial owner snapshots and enabled-mask refresh. It also identifies a game/GEM
difference in the player boundary gate. The vault manifest now names the blocked
predicate explicitly; the related acceptance-manifest hash and validation were
refreshed after correcting that map's spatial query argument.
