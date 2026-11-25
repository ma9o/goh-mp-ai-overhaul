# Explicit Attack through soft cover and around obstructions · build 26734796

**0x903690 is the decision point.** An explicit Attack compares its scheduled firing trial with 3. A trajectory-derived 5 can be reconsidered here once the host has checked the intervening contacts. A blocked Attack can own a movement child and retry against the same target. The native firing and replication paths are identifiable. Penetration, friendly clearance and host/client agreement depend on the actual shot. **Runtime admission remains false.**

All addresses are game RVAs. Member offsets are hexadecimal and virtual slots are decimal. This pass covers firing feasibility (C-BATTLE/C-OUTPUTS), committed threat inputs (I-SUBJECT-POSITION/I-THREATENING/I-ATTACKING), and child ownership and replication (O-MOVE-TO-POSITION/O-MOVE-TO-COVER). [Manifest and fingerprint](../artifacts/build-26734796/lof-detour-soft-cover-26734796.json) · [Validation](../artifacts/build-26734796/lof-detour-soft-cover-26734796-validation.json) · [Baseline catalog](decision-catalog-25492057.md)

## Host and player admission

Require the exact manifest hash, emission role 0xC2B740 true, remote predicate 0xC2B7A0 false and worker predicate 0xA7A80 false. Scope the hook to the actor's serial simulation invocation after the worker join. Dispatcher 0x804AC0 calls sensor slot 13 at 0x804C0B when sensor+0x260 is nonnull, conditionally updates the brain at 0x804C57, then calls brain slot 24 at 0x804C96. Human slot 24 is 0x91DF50. Vehicle slot 24 is 0x9A76C0 and reaches the common order tick at 0x9A78A4. It takes the current order from brain+0xF0 and dispatches order slot 3, then slot 19 for Start/Tick. Sensor observations may predate this tick.

Check [actor+0xDC8] = brain and [brain+0x38] = actor. Require the concrete human or vehicle primary vtable (0x110FF88 or 0x101FD68). The client tables share their order-tick slots. Require the live Attack vtable 0x110C018, its owner subobject at actor+0xAF0 (order+8), and authorized order provenance. Honor control, hold and current user orders. Bail for (brain+0x39C & 0x3F) != 0, chassis+0x18 != 0, or weaponry+0x70 bit 1. Bit 28 at brain+0x39C permits servicing only that player's existing Attack within its fire and movement permissions. Reacquire pointers each trial and discard proposals on cancellation, target or weapon change. [Project authority rules](../../AGENTS.md)

## Soft-cover firing contract

| 25492057 → 26734796 | Interface and evidence |
|---|---|
| 0x81EEE0 → 0x833C30, 0x820A40 → **0x835790** | Attack calls the aimer wrapper at 0x90368B with target and four null optional arguments. Try takes RCX=context, RDX=target, R8=optional 0x48-byte hit output, returns EAX. Context +0/+8/+0x10/+0x18/+0x20/+0x28 holds time output, weapon, optional input, bone, owner and trial origin. Preserve bone-trial cleanup at 0x835DB5 and matrix restoration at 0x835DEA. |
| 0x820030 → **0x834D80**, 0x820150 → **0x834EA0** | The wrapper updates aimer+0x130 and can repeat the probe for crawl. Inner probe takes aimer, start, end and R9B option. Try passes false (mask 0x186E03). True uses 0x106E03 and can dispatch `break_window` at 0x835014 on one eligible window hit. A muzzle obstruction yields 5 before any trajectory exists. Preserve this veto. |
| 0x818770 → **0x82D4C0**, 0x83A850 → **0x84F5A0** | Bullet shooter slot 3 takes shooter, hits, optional input, path and flags. Try passes 0x80800, merged into 0x6003 defaults for **0x86803**. The step takes trajectory context, end XYZ, and start/end times in XMM2/XMM3. |
| 0x821AD0 → **0x836820** | Evaluator takes target, owner, hit vector, optional copied hit and a trajectory (XYZ stride 0xC). The first eligible allied kind-3/4 actor (type bit 1, Operatable bit 8) yields 4, or 3 if it is the requested entity. A target-entity hit or segment within half a spatial unit yields 3. Otherwise empty hits yield 2, nonempty hits 5 and an optional copy of the last hit. |

**The face and volume offsets depend on the record.** At 0xA4159F the kind-4 writer reads a face byte, at 0xA415AD it stores a 32-bit face, and at 0xA415FA it advances by 0x48. The scene query fills XYZ at 0xA40F2E–0xA40F38. Live contact handling separately constructs the armor input at 0x84B890.

| Field | Scene/preview hit (0x48) | Shape/live contact (0x28) | Armor input |
|---|---|---|---|
| Kind and distance | +0 u32 kind, +4 f32 cumulative distance in trajectory output | +0x20 f32 segment distance | Separate inputs |
| Hit XYZ | +8/+0xC/+0x10 | +0x14/+0x18/+0x1C (may be placeholders on shape hits) | +0/+4/+8 |
| Entity and volume | **+0x18 entity, +0x20 volume** | +0 volume, owner at volume+0x40 | Entity separate, **+0x28 volume** |
| Normal and direction | +0x28 and +0x34, each vec3f | +8 normal vec3f | +0xC and +0x18, each vec3f |
| Face and flags | **+0x40 i32 face**, terrain sentinel −1. +0x14/+0x44 unassigned. | +0x24 u8 face, +0x25 flags | **+0x24 i32 face**, +0x30 entry flag |

**0x400000 collects eligible hits.** At 0x84FB23 bit 22 allows travel beyond an accepted volume. Null-volume terrain/water kinds 1/2 still produce a terminal kind 1. Region exit, exhaustion, owner exclusion, the window filter and the 0x800 entity filter limit coverage. 0x84F31F selects a final sort by distance. Passing the extended path to the evaluator can yield 3 simply because a segment reaches the target behind a hard blocker.

**Proposed override.** Preserve the first-hit result and its source at the existing evaluator call 0x835D3B. Consider only a trajectory-derived 5. Capture the scheduled launch inputs at 0x82D588 and gravity before stepping. Replay once with separate context, hit/path vectors and effective flags **0x486803**. Repeating shooter slot 3 consumes RNG. Carry only the verdict through normal Try cleanup. Require target coverage, sufficient residual energy at every soft layer, and clearance through the possible flight beyond the target. Independently check friendly hulls over the live spread envelope. Terrain before the target, concrete/heavy armor, unknown material or effects, or incomplete coverage keep the native verdict. The kind-4 writer omits the shape entry/exit flag. Capture the underlying contact when the armor test needs that flag. [Geometry map](human-geometry-map-25492057.md) · [Ballistics map](ballistics-map-25492057.md)

### Weapon, obstruction and blast inputs

| Input | Read path on this build |
|---|---|
| Active weapon/ammo | 0x81B080([actor+0xDD8]) uses weaponry+0x4C to select a place and reads place+0x2B8, with first runtime weapon in [+0x30,+0x38) as fallback. Weapon +0x98/+0xA8/+0xB0/+0xB8 points to definition/aimer/charger/shooter. Bullet shooter+8 is the weapon. 0x820B50(charger) rejects dummy/absent ammo and returns charger slot 1's item+0x50 definition. 0x8514E0 checks eStuffBullet. |
| Caliber and remaining power | Definition+0x1628 is f32 Calibre (token 0xBB). 0x81EC40 selects a profile via [weapon+0xA0] and ammo+0xEC8 tags, with container+0x28 fallback. **0x841650 takes profile in RCX, float distance bits in EDX and returns XMM0.** It interpolates [+0x88,+0x90) knots or uses the +0x80 curve with +0x20 range, +0x198 scalar and +0x19C cap. At impact, distance = projectile+0xFC plus contact+0x20. 0x84BFB5 scales damage by projectile+0x10C remaining fraction. 0xA8BE80 converts typed damage to energy kind 6. |
| Armor/thickness | 0x72D200 resolves entity+0x580 or shared armor. 0x7FC090(&params, profile, incidence cosine in XMM2) sets angle factors. 0x7FC310(&query, entity, source, &armorHit, &params) binds the hit. 0x7FDDF0 chooses a 0xB8 part setting or the shared default. 0x7FC5B0 reads [volume+0x30] → definition+0x3B0+4×face, with setting+0xA0/A4/A8/AC overrides. Ray thickness uses shape slot 3 in detailed mode (second argument 0) or minimum box extent in estimate mode (1). 0x7FCA60 adjusts effective thickness. For admitted pierce inputs, 0x7FC3A0 sets flag 0x02 for sufficient energy and 0x01 for the separate Threshold/Pierce/Calibre (+0x10), energy-threshold and immunity checks. |
| Materials and breakage | Volume definition+0x80 is the tag set and +0x408 the material pointer. Material+0xE0 is SteelEquivalent (0x801FD0), 0.05 for stock wood and 0.15 for concrete. 0x8036B0 matches the material name tag. Entity tags come from [entity+0x548] → [+0x330,+0x338) and entity [+0x4E8,+0x4F0), stride 0x60. Fence and flora handlers vary. The stock fence interaction can detonate `shell fg` or `heat_shell` on contact. Breakage alone cannot authorize pass-through. |
| HE clearance | 0x7F5870(ammo) returns positive PossibleDamageRadius at +0x498 immediately. Otherwise it takes the largest effect slot 5 across [+0x480,+0x488). Inspect every effect independently. Blastwave vtable 0x1038380 stores world-distance r0/r1 at +0x10/+0x14, and slot 5 at 0x15F240 returns r1. Use the largest configured or actual bound at the earliest possible contact, including shooter hull and friendlies. Unknown fragment, fire or scripted effects keep the veto. |

The stock 105×764 HE shell inherits **PossibleDamageRadius 0.1**. Its three blastwave outer radii are **1.0, 3.12 and 12.039** configured spatial units. Its HE pattern also sets DetonationThickness to 0.5. Token 0x2E writes the typed value at ammo+0x12B8 (0x851F24). With armor flag 0x01 set, 0x84C143–0x84C175 compares consumed contact energy with that threshold in kind 6. A close tree can be within the blast even when the AI radius check passes. [Armor map](armor-events-map-25492057.md)

## Detours that preserve the Attack

| 25492057 → 26734796 | Decision and ownership |
|---|---|
| 0x8EDFD0 → **0x905290**, 0x8EC030 → **0x9032F0** | Attack slot 19 retains target and timers. Its due path calls 0x904930 at 0x90558E, then 0x9032F0 at 0x905155. A non-3 invokes 0x902ED0 at **0x903698**. A 3 still observes readiness and human timers. State 6 calls slot 20 at 0x9036F4. |
| 0x8EBC10 → **0x902ED0**, 0x93ED30 → **0x9575D0** | Infantry replacement point **0x9030A4** takes RCX=context, RDX=initialized result, XMM2=world radius and returns AL success. 0x957460 supplies actor+0, copied target+8, weapon+0x88, masks and bounds. Result holds kind+0 (1 pose, 2 point, 3 cover), pose+4, XY+8 and 0x88-byte cover descriptor+0x10. Kind 2 also passes +4 to the movement setter. Native initial pose is 0 and may refine to 1/2. Preserve stationary retry cutoffs (over 20 counted retries or 10 simulation seconds), growing radius capped at 12 units, rearm and cleanup. |
| 0x8D68E0 → **0x8EBD20** | This factory builds **eLookAt(target, option)**. Blocked nonhuman Attack uses it only in aimer state 7, otherwise returns running. For explicit vehicle Attack, use the 0x903698 obstruction branch and a parent-owned movement child. |
| 0x990870 → **0x9AEDA0** | Autonomous vehicle root reads root+0x30 → brain+0x3D8. Failed fire selection sets the deadline at 0x9A8647. The due test at 0x9AF4D3 consumes it (clear at 0x82BA23). The root tries four points with navigation, zone and positive nonempty-route checks, then installs Move as the current order via brain slot 13. That installation would displace an explicit Attack. |

**Candidate policy.** Try paired lateral positions perpendicular to the attacker–target XY line, then near corners of the blocking volume expanded by the unit footprint. Respect zone, terrain, weapon range and route feasibility. Use a private per-order PRNG for ties. Read the last committed enemy subjects via [actor+0xDD0] → [+0x280,+0x288). Copy subject+0x28 XYZ, require +0x10 bit 28 (known) and +0x14 bit 1 (enemy), and heed +0x6C suppression and observation age. Score route and endpoint exposure to threatening/attacking subjects (+0x14 bits 7/8), then route cost and displacement. If private evaluation cannot support a point, leave the scheduled native solver in control. Returning a synthetic failure can trigger the move-toward-target fallback.

**Speculative LOF.** Use copied muzzle/pose data and a candidate-specific launch solution. The geometry interfaces are 0x84F1B0(0x60-byte context, owner, hits, origin, velocity), 0x84F260(context, flags) and 0x84F280(context) with gravity+0xC and optional path+0x58. For one segment use 0xA409C0(query, flags, owner, −1), 0xA40A20(query, start, end), 0xA40AC0(query, hits). Bound the horizon, own and release vectors on all exits. These interfaces omit shooter sampling and window events, but native geometry can refresh volume transforms at 0x7A8410. Strict private scoring requires current copied geometry. Reject unsupported turret limits, stance changes or arcs. An ordinary Attack trial rechecks the selected point after movement.

The hypothetical wrapper 0x958A80 and route shortener 0x9590F0 are unsuitable for repeated scoring. The native infantry solver snapshots bones at 0x957814, restores transforms/pose at 0x9585AA/0x9585C4, and inner Try can notify event 0x2B. Its spread-disabled sampler still advances selected RNG at 0x82DC75/0x82DCCF and can write burst state. Vehicle queries 0x8C18A0 and 0x8DDED0 can reach another draw at 0x8C21B0. The circle sampler increments at 0x623DF0/0x623E3E. Reserve native movement admission for the chosen point. Replacing a stock solver can also remove vanilla draws. Exact frame-stream equivalence remains unverified. [Vehicle root map](vehicle-brain-map-25492057.md)

## Native output and validation

| Output | Observable boundary |
|---|---|
| Move | 0x90A8C0(&XY, 0) builds a point **eMoveTask**. A usable full or partial path in 0x90E3F0 produces actual eMove via 0x90A7B0. Move phase 0 sets **byte +0x3C** from 0xC2B740 and +0x3D from 0xC2B7A0. 0x90B71F gates ID 0x09 eOrderMoveCommand, sender adapter 0xBB5E0 → 0x151340. Let native progress/drop/End run. The task's own +0x3C is separate state. |
| MoveToCover | 0x8E2770 copies the descriptor to child+0x38. Exec 0x8E3AB0 uses native cover admission. Enter/leave 0x8AE410/0x8AE590 emit ID 0x22 eCoverSectorCommand, receiver 0xAA7B0. Infantry appends move/cover and WeaponWaitReady(target) via 0x919270(sequence, child, 0, 1). 0x91A180(parent, child) ends/deletes an existing child and binds the new one. Respect preemption. Running/success/failure are 0/1/2. Recheck arrival and LOF after a partial route. |
| WeaponFire | Slot 20 at 0x905240 attaches 0x8EB9C0(target, 4, 0). Exec 0x8ECBF0 sets mode 3 and shots −1 with 0x81F0C0, installs aiming and clears target on End. Commit 0x81FCB0 emits ID 0x0C eWeaponTargetCommand through 0xAB4E0, receiver 0xAB640. Weapon definition+0x15F4 (SyncedProjectiles) and session predicates gate separate ID 0x0E from bullet shooter slot 11. Receiver 0xABE90 calls projectile creation at 0xAC201. Live 0x84B890 handles impact events, residual power, penetration, stop or ricochet. |

The item passed static validation against the installed game, 16 RTTI tables and six read-only stock archive members. The general baseline audit exited 1 with 22 changed, 14 unresolved and 8 ambiguous contracts. This design requires zero resource edits. Live ABI, host/client role behavior including `-no_server`, RNG deltas, packet delivery, lobby admission and in-match checksums remain unverified. [Validation report](../artifacts/build-26734796/lof-detour-soft-cover-26734796-validation.json) · [Baseline engagement orders](engagement-orders-map-25492057.md)

Reproduce the structural checks from the repository root with the installed paths.

```sh
local-work/compat-venv/bin/python research/scripts/verify_fine_map.py \
  --map research/artifacts/build-26734796/lof-detour-soft-cover-26734796.json \
  --binaries-dir "$GOH_BINARIES" --resources-dir "$GOH_RESOURCES" \
  --exports-dir local-work/exports/build-26734796
```
