# Infantry decision input/output catalog — build 25492057

**Q1 is split, not complete.** This working ledger seeds J1–J8 and closes the
stored-input/inline-output **inventory** for the slot-35 selection span
`0x941D86 <= RVA < 0x941F49`. The input and output contracts themselves still
have the explicit blanks below. Base reconsideration and state policy remain
under the seven coverage rows; their variables must not be assumed absent.
The [decision frontier](decision-frontier-25492057.md) governs scope.

The six Q1 source manifests contain 340 distinct game method entries and 292
field records before consolidation. Those counts include producers and execution
internals; they are not a count of decision variables or a claim of coverage.
The source field lists do not certify all reads in the scoped methods. This
pass establishes an instruction-level inventory for selection; the remaining
census is split by policy family.

The [Q1 manifest](../artifacts/build-25492057/decision-catalog-q1-map.json) freezes
these rows and instruction evidence. This Markdown file is the working catalog:
later items can fill its rows and add rows, while retaining earlier evidence
manifests. IDs are stable; do not merge distinct bits merely because they share
a dword. `OPEN` is an unresolved static blank unless explicitly described as
runtime-only. Row IDs and functional labels are inferred; only names explicitly
identified as property, enum, tag or RTTI bindings are recovered native names.

## Reading the ledger

An input row represents an independently stored value or query result, not a
register temporary derived entirely by the decision equations. Pointer edges
and the indirect setter dispatch are included in the bounded selection inventory.
Read-only floating-point operands have their own K rows. Stack ABI save slots
are explicitly excluded. Instruction RVAs identify individual reads/calls, not
just containing functions. All RVAs are **game**, and all offsets are hexadecimal.

Each input has read site, access path/type/width, meaning/domain, freshness and
validity. Each output has effect, results, ownership, cancellation and side
effects; its issue/boundary sites are separate. The O rows include inline policy
bookkeeping so that it cannot be mistaken for an issued movement order. Output
seeds marked `seed-boundary` still need their issuing-method census.

The manifest's `fields[].evidence_ids` and `outputs[].evidence_ids` link each row
to exact instruction lists. Inherited entries retain source-map filenames,
evidence IDs and source hashes. Supplemental semantic sources are linked below.
The verification checks these links and addresses; it does not prove the prose.

## Freshness joins (J7)

**F-score — conditional refresh.** Human slot23 `0x941BC0` takes task address
`brain+0x488` at `0x941C56`, tests its slot2 at `0x941C63`, and resets a due task
with requested delay bounds **1,1** at `0x941C74/0x941C7B/0x941C89`. A positive
skip count at task `+0x6C` is decremented and skips submission
(`0x941C8C`–`0x941C98`). Only a due, unskipped task reaches the score callback
`0x944DE0` and submission (`0x941C9F`, `0x941CD3`). The existing systems map
establishes that bucket allocation determines the actual period; equal requested
endpoints do not guarantee a one-second cadence.

The bridge writes A/B via `0x877170`. The scene preparation/work/wait ordering
puts a submitted score job before later decision calls in that quant. **When
the task does not submit, decisions retain earlier cached A/B.** Thus J7's
same-quant statement is conditional on a score job having run; it does not
establish a new score for every decision, nor that a score used the sensor
commit which follows worker completion. Which brain slot23 implementation
single-player infantry instantiate is still Q2. See `score_task_gate`, inherited
`flow.producer_callback`, `systems.scene_ordering`, `systems.worker_wait` in
the Q1 manifest; the preserved [flow](flow-map-25492057.md) and
[systems](systems-map-25492057.md) maps supply the producer/scheduler contracts.

**F-sensor — commit before reconsideration.** Actor dispatcher `0x7EFD10`
calls sensor slot13 at `0x7EFE5B`, then invokes brain reconsider dispatch at
`0x7EFEA7`; commit through `0x873B00`/`0x878AE0` is the J7 path. Look-around
requested bounds are 0.5–1, inform 0.25–0.75, brain task 0.5–1.5, all subject
to the scheduler's bucket/eligibility gates. A commit can age/expire an old
observation without newly observing its actor. The catalog claims ordering of
the reached callbacks, not unconditional fresh perception each frame.

## Validity contracts used by the input rows

Non-null use is a precondition, not a lifetime proof. No current OPEN validity
blank is prematurely classified as runtime-only.

- **V-brain:** OPEN: incoming this is a valid live brain precondition. Inline fields need no extra allocation and are not retained, but the end-to-end actor/brain attachment and callback call-lifetime proof has not been consolidated; C-OUTPUTS must triage it. Do not label this runtime-only yet.
- **V-owner:** OPEN: owner and cover are dereferenced without null checks in selection. Native call requires live attached objects. Their lifetime/replacement relative to that call has not been proved here; C-OUTPUTS must triage it.
- **V-subject:** OPEN: sensor subjects have refcounts and entity event subscriptions (systems/ownership maps), but that alone is not a complete within-call lifetime proof for every actor/entity/brain chain. Guards and retains must be recorded per caller by the remaining census.
- **V-rng:** OPEN: getter supplies selected TLS pointer or default; this span neither frees nor replaces it. Custom-context pointer lifetime is still open in clock map. No blanket claim of worker/thread safety.
- **V-global:** Image-resident storage, available for the loaded module lifetime. Writable data is not assumed immutable.
- **V-query:** OPEN: terrain result is copied by value, so no cell pointer escapes to the brain. Level/cell lifetime for the native query during the call still needs its interface precondition consolidated.

## Input rows

The selection rows stop at the output setter/query interface. The seed rows
are representative read paths for J1–J8, not a claim to have enumerated every
consumer. In particular, I-RECOVERY-DEADLINE is an output-contract dependency
outside the selection span.

| Row | Read site | Access path / type / width | Meaning / domain | Freshness | Validity |
| --- | --- | --- | --- | --- | --- |
| I-THIS | 0x941d10 (RCX copied to RSI); all selection accesses | Incoming complete human-brain pointer; 8 bytes | Actor::eHumanBrain context. Selection implementation shared with the client type; concrete single-player factory choice remains Q2. | Call argument, stable local pointer in the reviewed span; no object replacement in this span. | V-brain |
| I-OWNER | 0x941d86 | brain +0x38; actor pointer, 8 bytes | Owner actor (inferred field name). | Read once before threshold scaling. OPEN: assignment/attachment lifetime has not been consolidated. | V-owner |
| I-COVER | 0x941da8 | brain -> owner(+0x38) -> +0xDC0; cover-component pointer, 8 bytes | Cover component, by the existing movement map; the same component supplies root location selection. | Current component at decision entry. OPEN: replacement and phase relative to selection. | V-owner |
| I-COVER-GROUP | 0x941dbb; root corroboration 0x95742f | brain -> owner(+0x38) -> cover(+0xDC0) -> +0x24; int32, 4 bytes | Cover group (inferred field name): 1 selects vertical cover, 2 horizontal, 0/3/4 outdoors. Selection multiplies both thresholds by 1.5 for exactly 1 or 2; no other value is scaled. Complete native enum names OPEN. | Current read; OPEN: first producer establishing when this group changes. | V-owner |
| I-A | 0x941d8a, 0x941f0a | brain +0x518; float32, 4 bytes | Relative incoming pressure (inferred); sum of position/age-weighted Human/Tank attack-to-comparison ratios, apportioned over self/support positions; not an enemy count. | F-score: for slot23 implementation 0x941BC0, cached and updated only on a due, unskipped score task; a completed job precedes later decisions in that quant. Can change during mission; no fixed per-decision refresh guarantee. Concrete brain selection remains Q2. | V-brain |
| I-B | 0x941d92 | brain +0x51C; float32, 4 bytes | Relative self/support capacity (inferred); ratio of self plus eligible support AttackRate pair to opposing HitPoints pair; not a friendly count. | F-score: for slot23 implementation 0x941BC0, cached and updated only on a due, unskipped score task; a completed job precedes later decisions in that quant. Can change during mission; no fixed per-decision refresh guarantee. Concrete brain selection remains Q2. | V-brain |
| I-S | 0x941e4c, 0x941ebe | brain +0x474; float32, 4 bytes | Morale-associated S, 0..100 for ordinary finite values; association is RTTI Scene::Stats::eMoraleProcessor, not an independently recovered property name. | Constructor 100; slot29 clamps deltas; this span can add 5; recovery follows base reconsideration. OPEN Q4: every external delta event, recipient and phase. | V-brain |
| I-ADVANCE-RATIO | 0x941daf | brain +0x524; float32, 4 bytes | Bound property advance_ratio; constructor 2.0. Used as a multiplier; no full accepted-value range is established. | Constructor/property visitor and actor_state command writers known; mission commands can change it. OPEN: their timing relative to a brain call and complete writer set. | V-brain |
| I-RETREAT-RATIO | 0x941d9d | brain +0x528; float32, 4 bytes | Bound property retreat_ratio; constructor 2.0. Used as a multiplier; no full accepted-value range is established. | Constructor/property visitor and actor_state command writers known; mission commands can change it. OPEN: their timing relative to a brain call and complete writer set. | V-brain |
| I-NO-ADVANCE | 0x941ed9 | brain +0x520; byte, 1 byte | Bound Boolean no_advance; any nonzero byte blocks its named normal-state branch. The state-zero branch does not read it. | Constructor/property visitor and actor_state command writers known; mission commands can change it. OPEN: phase and complete writer set. | V-brain |
| I-NO-RETREAT | 0x941f01 | brain +0x521; byte, 1 byte | Bound Boolean no_retreat; any nonzero byte blocks its named normal-state branch. The state-zero branch does not read it. | Constructor/property visitor and actor_state command writers known; mission commands can change it. OPEN: phase and complete writer set. | V-brain |
| I-BEHAVIOR | 0x941e13 | brain +0x52C; int32, 4 bytes | 0/1 withdrawal, 2 no tactical movement, 3/4 advance are functional/inferred meanings (J6). Unreferenced flee/retreat/none/advance/charge table is not a binding. | Constructor 2; selection writes 0..4. OPEN Q2: all writers, save/load, outlined copy, and which slot35 runs. | V-brain |
| I-BEHAVIOR-DEADLINE | 0x941deb, 0x941df4 | brain +0x530; uint32, 4 bytes | Absolute quant-counter deadline (inferred name); 0 disables the gate. Nonzero bypasses normal selection; unsigned now>=deadline clears state AND deadline with one 8-byte store. | Timed state-zero branch writes now+convert(1+sample). OPEN Q2: other writes and serialization. This compare is not a demonstrated wrap-safe timer. | V-brain |
| I-NOW | 0x941dfa, 0x941ea2 call 0x65e700; load 0x65e700 | 0x65e700() -> global RVA 0x1556E48; uint32, 4 bytes | Simulation counter, not wall time. Normal units are 1000 counter ticks per converted time unit. | Clock core increments per reached quant; ordinary executor gates pause, explicit step wrappers can advance while paused; reset/load can change it (clock map). | V-global |
| I-DURATION-FACTOR | 0x941e9b -> 0x65e730 | global RVA 0x14776C8; float32, 4 bytes | Duration-to-counter multiplier, file-initial 1000; conversion uses CVTTSS2SI. Ordinary units are simulation seconds, not elapsed wall seconds. | Writable image data. No direct write references located in earlier systems/clock work; alias/configuration writes not excluded. | V-global |
| I-RNG | 0x941e61, 0x941e8e call 0x554b50 | 0x554b50() selected TLS RNG pointer, default per-thread fallback; 8-byte result | Selected RNG context, not necessarily the default or a brain-owned generator. Fallback can lazily obtain the default per-thread RNG (clock map). Second getter return is unused in this span. | Selected context can change through quant/context or explicit RNG scopes (clock map); use the pointer returned for this draw. | V-rng |
| I-RNG-STATE | 0x941e69 | I-RNG +8; uint32, 4 bytes | Generator state; new = old*0x7FF8A3ED+0x2AA01D31 modulo 2^32, upper 24 bits form sample numerator. | Read and written by this draw (0x941e79); other users of the selected stream can change it between decisions. | V-rng |
| I-RNG-COUNT | 0x941e70 (read-modify-write) | I-RNG +0xC; uint32, 4 bytes | Draw counter (inferred name). Incremented but not used for this branch result. | Incremented once by this draw; other stream users also draw. Full stream history requires runtime. | V-rng |
| I-RNG-SCALE | 0x941e86 | global RVA 0x1587C54; float32, 4 bytes | Multiplier of this generator draw upper 24 bits. OPEN: initialization/value domain of this particular global; do not import 0x1584DAC from the different cover selector. | Writable, non-file-backed data in the fine map. OPEN: initialization and later writers. | V-global |
| I-MORALE-DISPATCH | 0x941e29, 0x941e37 | brain -> vptr at +0 -> byte offset +0xE8 (slot29); two 8-byte pointer loads | Human implementation 0x945010 adds/clamps S. Virtual dispatch, not a hard-coded direct call; concrete brain factory is Q2. | Read at invocation. Type table links are static; actual instantiated type is not determined by this read. | V-brain |
| I-RECOVERY-DEADLINE | 0x94225b and recovery evidence (outside selection span) | brain +0x478; uint32, 4 bytes | Morale recovery deadline (inferred). Separate from +0x530; 0 allows recovery. | Negative slot29 delta sets now+convert(10); slot35 recovery adds (0.5*100)/120 then schedules convert(0.5), after base reconsideration. OPEN Q4: other S-changing paths. | V-brain |
| I-TERRAIN | 0x8a0b8f/0x8a0b94; accessor 0xa43b4d/0xa43b59 | Brain cover/stance query XY -> 0xA43AB0 -> level-0 cell (4-byte stride), byte +2 low nibble; uint4 in byte. No brain-owned cell pointer. | J1: 0 undef,1 ground,2 no_pass,3 shallow_water,4 water,5 deep_water,6 swamp,7 road,8 no_pass_human,9 snow,A forest,B slope. Cover rejects 4/5 and kind1 in 3; pose/detour reject water 3..5. Same-cell lookup plus 0xA44070 names the IDs (path-profiles/ground-state). | Returned on demand, with water-height correction. OPEN: whether packed cells can be rewritten during a mission; no producer internals needed. | V-query |
| I-THREATENING | 0x903bf3 | brain -> owner(+0x38) -> sensor(+0xDD0) -> category1 pointers [+0x280,+0x288) -> subject +0x14 bit7; uint32 bit | J2: threatening; eligible nonally signal source. Hit3/explosion5 use ThreatZone; injury4 bypasses distance; shot2 does not set it here. Aggregates into brain +0x39C bit16 unless subject +0x6C suppresses it. | F-sensor: event writer 0x877D20; commit expiry clears above 2 converted units since subject +0x68 while threatening is set. Subject load is another producer; phase/cadence from J7 is conditional, not a per-frame observation guarantee. | V-subject |
| I-ATTACKING | 0x903bd3 | brain -> owner(+0x38) -> sensor(+0xDD0) -> category1 pointers [+0x280,+0x288) -> subject +0x14 bit8; uint32 bit | J2: attacking; eligible nonally signal source. Hit3/explosion5 use AttackZone; injury4 bypasses distance; shot2 does not set it here. Aggregates into brain +0x39C bit14 unless subject +0x6C suppresses it. | F-sensor: event writer 0x877D20; commit expiry clears above 1 converted units since subject +0x68 while threatening is set. Subject load is another producer; phase/cadence from J7 is conditional, not a per-frame observation guarantee. | V-subject |
| I-TARGETABLE | 0x8785ac, 0x8785b3, 0x8785b9 (classification dependency) | brain sensor subject -> actor(+0x18) -> target brain(+0xDC8) -> +0x398 bit15; uint32 bit | J3: targetable, from TurnOff.Targetable. Subject enemy bit1 requires this bit as well as enemy relationship. | Place Include 0x77C140 clears; Exclude 0x77B950 restores; constructor initializes. Can change in mission; relationship refresh/commit propagates it. Not an exhaustive alias-write proof. | V-subject |
| I-LINKED | 0x945d0d, 0x945d13 (pickup eligibility example) | Brain pickup candidate/entity argument -> entity +0x1C0 bit28; uint32 bit. Other subject paths use subject +0x20 entity. | J4: linked to parent (inferred, not a recovered native flag binding); excludes this pickup candidate. Other decision consumers remain in the census. | Current candidate read. OPEN: first link/unlink writer and phase that establish mission mutability. | V-subject |
| I-SEARCHED | 0x87918a (Recon waiting dependency); write 0x94c94c | brain +0x3C0 remembered subject (completion); sensor subject -> +0x10 bit27; uint32 bit | J5: searched (inferred). Set at find-enemy phase9; prevents becoming Recon-waiting bit26 on the age path. It is not the Recon zone completion byte. | Phase9 writes; sensor commit reads; load may restore flags. OPEN: complete set/clear writer list, including the other 0x878AE0 branches. | V-subject |
| I-OPERATABLE | 0x878619 (classification dependency) | brain sensor subject -> actor(+0x18) -> +0xDB4 bit8; uint32 bit | J8: Operatable, token6D. Derived from brain presence, crew/noncrew operability and vitality low bits. False contributes to unusable subject bit4, not simply dead. | 0x7F0130 reconciles slot67 0x7F1A30 and emits event0x1A; blinding temporarily clears/restores for that event; property can force it. Mission-mutable. OPEN: exact phase of reconciliation relative to a decision. | V-subject |

## Fixed selection operands

Each is a 4-byte float in read-only PE data, valid for the loaded module's
lifetime. Those storage/domain/freshness/validity facts are closed for this
fingerprinted file; instruction and value checks are in the Q1 manifest.
Immediate integer operands are part of the decision equations, not mutable
input fields.

| Row | Data RVA | Value | Read instruction RVAs | Meaning |
| --- | --- | --- | --- | --- |
| K-SCALE | 0x10220d4 | 1.5 | 0x941dc5, 0x941dcd | Cover group1/2 threshold multiplier |
| K-ONE | 0x10004d0 | 1.0 | 0x941dd5 | Base of randomized duration |
| K-DELTA | 0x10124b4 | 5.0 | 0x941e2f | Morale delta on leaving state0 |
| K-FIFTY | 0x10099b0 | 50.0 | 0x941e54, 0x941f17 | State0 morale and normal retreat threshold |
| K-SEVENTY | 0x10d205c | 70.0 | 0x941ec6 | Advance morale threshold |
| K-DOUBLE | 0x10004d4 | 2.0 | 0x941ee7 | Multiplier distinguishing states3/4 |
| K-THIRTY | 0x105ffbc | 30.0 | 0x941f29 | Normal retreat/flee threshold |

## Output rows

Only O-BEHAVIOR, O-BEHAVIOR-DEADLINE, O-MORALE-DELTA and O-RNG-DRAW are outputs
of the audited selection span. The last three rows are existing boundary
contracts seeded for the remaining census; no call from this span is asserted.

| Row / sites | Effect | Result codes | Ownership | Cancellation | Side effects |
| --- | --- | --- | --- | --- | --- |
| O-BEHAVIOR; 0x941e07/0x941e3d/0x941ea9/0x941ecd/0x941efb/0x941f36/0x941f42 | Immediate write of I-BEHAVIOR. Downstream movement state consumer gives J6 functional meanings; this store does not itself create an order. | No result code: inline stores. | Brain-owned scalar; no transferred pointer. | Superseded by subsequent writers (writer census Q2); no asynchronous operation to cancel. | Expiry store also zeroes I-BEHAVIOR-DEADLINE; state-zero recovery calls O-MORALE-DELTA before writing 2. |
| O-BEHAVIOR-DEADLINE; 0x941e07, 0x941eb3 | Clear gate on expiry, or set now+convert(1+sample) with state3. | No result code: inline stores. Integer conversion/overflow behavior is not an accepted-delay API. | Brain-owned scalar. | Later writes replace/clear it; complete writer set Q2. | Random path consumes one draw and advances stream count; branch skips normal no_advance/no_retreat checks. |
| O-MORALE-DELTA; 0x941e37 -> brain slot29 / 0x945010 | Add delta to I-S and clamp to [0,100] for ordinary finite inputs; this call passes +5. | No policy result consumed at this call; no success/failure enum established. | Borrowed brain; immediate mutation, no object ownership transfer. | Synchronous setter; a later delta/recovery supersedes state. No pending action created. | Negative deltas write recovery deadline now+convert(10); this positive call leaves that deadline unchanged. Q4 owns the other callers/events. |
| O-RNG-DRAW; 0x941e69..0x941e86 (actual writes 0x941e70, 0x941e79) | Advance selected RNG state and increment its draw count; derive float sample for the duration. | Computed float, no order status. Sample bounds OPEN until I-RNG-SCALE is bound. | Borrowed selected RNG; no transfer or local free. | Draw is immediate and not rolled back by this span. | Affects later users of the same stream even if they are outside this brain. |
| O-DIRECT-GOAL; boundary 0x902FB0; effect sites 0x902fcf, 0x903020, 0x90302a | Copy goal XY (8 bytes from RDX) into brain +0x130; clear flags7/9/11/29, set6/8/25. Complete movement/arrival/abandonment contract OPEN Q3. | No explicit success/failure value in setter body; subsequent state/order outcomes OPEN Q3. | Copies XY, does not retain the supplied coordinate pointer. Current-order end/ownership through slot13 remains Q3/Q10. | Calls brain slot13 with null if current order exists; later task clear, arrival and failure paths OPEN Q3. | Resets +0x140/+0x144/+0x148 from globals 0x146E000/4/8. Those are data loads, not proved readonly constants. |
| O-MOVE-MODE; boundary 0x903140; writes 0x903170, 0x903184; cleanup call 0x9031a3 | Set brain +0x398 bits12..13 to masked input; free0/hold1/zone2 named, pattern3 unnamed. Compare uses unmasked input. | Synchronous setter; no success/failure enum exposed by reviewed body. | Borrowed brain/root/active-child pointers; deactivation keeps state attached, does not destroy it. | A changed mode deactivates active child with reason0. This is a state transition, not a complete order-cancellation proof. | Zone copies owner XY to +0x128. 0x912520 sends event4, deactivates descendants reason2, sets brain flag21, selects parent and sends event3; root child pointer is cleared. |
| O-MOVE-SPEED; boundary 0x9031C0; store 0x9031e3 | Set brain +0x398 bit14 from masked input when current extracted value differs from unmasked input; fast0/slow1. | No status enum; leaf setter. | Borrowed brain; no ownership transfer. | Immediate scalar policy change; no pending object to cancel. | No calls in this leaf; other flag bits preserved. |

## Inventory coverage and remaining Q1 work

These **coverage rows are not variable rows**. Their explicit blank is the
remaining per-variable/read-site/output-call census; they prevent a seeded
catalog from masquerading as complete. Q1a–Q1g fill that inventory and leave
each semantic/phase/lifetime/contract blank explicit for a row-specific item.
They do not authorize reopening every producer or every order implementation.

| Coverage row | Status / item | Bounded scope | Blank |
| --- | --- | --- | --- |
| C-SELECTION | enumerated / Q1 | 0x941D86 <= RVA < 0x941F49, with incoming RCX/RSI at 0x941D10 and zero R15 at 0x941D49 | No missing stored-input or inline-output rows within this span; semantic/freshness/validity blanks remain explicitly in those rows. Not the whole slot35 body. |
| C-BASE | open / Q1a | 0x903B10 and 0x912110/170/1C0/200/260/370/3D0/460/4D0/520 dispatch/policy boundaries | Per-variable read-site/access-path inventory and issued-output inventory, including individual +0x39C bits and external call results; reconcile I-THREATENING/I-ATTACKING. |
| C-ROOT | open / Q1b | Soldier/civilian root, location selection, root interrupt 0x955A90 and hazard scanner query interfaces in movement manifest | Per-variable input and issued-output inventory; saved-order/restore fields; reconcile I-COVER-GROUP. Do not reopen avoidance physics/path internals. |
| C-IDLE | open / Q1c | Idle Interrupt/Execute, following, healing, medic and explicit boarding policy helpers in human-states manifest | Per-variable input and issued-output inventory; mode/task/follow/timer/health/patient fields; factory contracts only below output boundary. |
| C-BATTLE | open / Q1d | Outdoors/cover battle Interrupt/Execute and tactical movement/cover policy helpers in human-states and tactics manifests | Per-variable input and issued-output inventory, cover descriptor/masks and scoring-query interfaces; reconcile behavior and direct-goal rows. |
| C-SEARCH | open / Q1e | Find-enemy 0x94C410/750/8A0, look-point policy 0x94DD50, Recon query contracts | Per-variable input and issued-output inventory, current/pending phases, zone handles/assignment and search results; reconcile I-SEARCHED. |
| C-HELPERS | open / Q1f | Target refresh/picking/ranking/eligibility, attack/stance, weapon/pickup policy and hypothetical query interfaces named by the six Q1 manifests | Remaining per-variable input and issued-output inventory at policy/query boundary; reconcile terrain/linked/targetable/Operatable rows; attach row IDs for Q5-Q9. |
| C-OUTPUTS | open / Q1g | Setters/direct goal and orders issued by the above methods; engagement-orders manifest only to contract depth | Cross-check every issued call against one output row and all five contract columns, including query side effects; reconcile all family inventories, assign exact row/blank follow-ups and Q2-Q11 links. Factory internals/physics/animation remain parked. |

The existing downstream items retain their order after the split census:

| Item | Current row link / remaining inventory dependency |
| --- | --- |
| Q2 | I-BEHAVIOR, I-BEHAVIOR-DEADLINE, I-THIS, I-MORALE-DISPATCH, I-A/I-B implementation choice |
| Q3 | O-DIRECT-GOAL; C-IDLE/C-BATTLE still enumerate its consumer fields |
| Q4 | I-S, I-RECOVERY-DEADLINE, O-MORALE-DELTA |
| Q5 | C-BASE/C-HELPERS must create separate rows for +0x14C, +0x18, +0x3E0 and each read +0x39C bit; no catch-all flag row is claimed complete |
| Q6 | C-HELPERS: preferred-subject source and borrowed/retained reference rows not yet enumerated |
| Q7 | I-COVER-GROUP; C-BATTLE must add descriptor +8/+0xC, direction-mask and healing-cover query rows |
| Q8 | O-MOVE-MODE/O-MOVE-SPEED are brain policy setters, distinct from MovementChange type/mode outputs; C-BATTLE/C-OUTPUTS must enumerate those and fire_mode readers |
| Q9 | I-LINKED is the entity bit, not the still-open linked-actor type test; C-HELPERS must add that test and the environment-query input |
| Q10 | C-OUTPUTS must enumerate complex sequencing, state-event outputs and Rotate/LookAt/Aim/WeaponWaitReady/Take contracts |
| Q11 | C-OUTPUTS must add separate native actor/squad-to-waypoint command boundary rows; do not conflate them with O-DIRECT-GOAL |

Q1g must also assign concrete row/blank questions for the remaining OPEN
freshness/validity/domain entries above. Recording an OPEN cell is not static
closure of that input. Only actual run-time values, stream history, observed
cadences and acceptance of native calls are identified as runtime checks here;
missing static writers or lifetime proofs remain static work.

## Seed reconciliation and source pointers

| Join | Catalog destination | Preserved evidence |
| --- | --- | --- |
| J1 | I-TERRAIN | [ground-state](ground-state-map-25492057.md), [path-profiles](path-profiles-map-25492057.md), [path-acceptance](path-acceptance-map-25492057.md), [systems](systems-map-25492057.md); same-cell reads 0xA43B4D/0xA43B59 and 0x8AED1D/0x8AED28, native ID binding in 0xA44070 |
| J2 | I-THREATENING / I-ATTACKING | [ownership](ownership-map-25492057.md#threatening-attacking-and-injury-observations), [signals-damage](signals-damage-map-25492057.md); event and expiry evidence in Q1 manifest |
| J3 | I-TARGETABLE | [ownership](ownership-map-25492057.md#targetability-and-collision-identity), [tactics](tactics-map-25492057.md#how-relationship-flags-are-produced); read 0x8785B3 |
| J4 | I-LINKED | [marker-actions](marker-actions-map-25492057.md), [entity-contact](entity-contact-map-25492057.md), [human-states](human-states-map-25492057.md); pickup bit read 0x945D0D |
| J5 | I-SEARCHED | [human-states](human-states-map-25492057.md#searching-remembered-areas), [perception](perception-map-25492057.md); completion write 0x94C94C, waiting test 0x87918A |
| J6 | I-BEHAVIOR / O-BEHAVIOR | [fine](fine-map-25492057.md), [flow](flow-map-25492057.md#from-a-state-value-to-actual-orders), [tactics](tactics-map-25492057.md#retreat-and-advance-actually-change-the-weights), [human-states](human-states-map-25492057.md); functional names stay inferred |
| J7 | F-score, F-sensor, I-A / I-B | [systems](systems-map-25492057.md), [perception](perception-map-25492057.md), [clock](clock-map-25492057.md); conditional-refresh qualification above |
| J8 | I-OPERATABLE | [ownership](ownership-map-25492057.md#operatable-is-also-derived), [modifier](modifier-map-25492057.md), [engagement-orders](engagement-orders-map-25492057.md); older “still open” naming remark is superseded by the named property evidence |

The input multiplier's formerly unnamed component field is I-COVER-GROUP:
`0x941DA8/0x941DBB` reaches the **same** owner component/offset as root selector
`0x957428/0x95742F`. State attachments/names are inherited from the movement
map. This closes the functional classification for threshold scaling, not
its producer/phase, full enum naming or component lifetime.

A/B formulas and Human/Tank/HitPoints/AttackRate meanings remain in
[flow](flow-map-25492057.md#what-a-and-b-actually-calculate) and
[tactics](tactics-map-25492057.md#the-score-pair-is-human--tank); the catalog
does not remap their producers. No observation count, fixed wall-clock interval,
normalized range for 0x1587C54, or new ownership guarantee is inferred.

## Reproduction and checks

Existing notes, manifests and local exports were consulted first. The new
focused export contains only the game. No editor assertion was needed, and
neither executable was launched or modified. Earlier notes/manifests/exports
are preserved; this pass adds evidence alongside them.

```sh
python3 research/scripts/run_headless.py --build-id 25492057 export --program call_to_arms.exe --candidates research/artifacts/build-25492057/call_to_arms.decision-catalog-q1-pass.tsv --focused --output local-work/exports/build-25492057/call_to_arms-decision-catalog-q1
python3 research/scripts/verify_fine_map.py --map research/artifacts/build-25492057/decision-catalog-q1-map.json --binaries-dir "$GOH_BINARIES" --exports-dir local-work/exports/build-25492057
python3 research/scripts/verify_decision_catalog.py --map research/artifacts/build-25492057/decision-catalog-q1-map.json --binaries-dir "$GOH_BINARIES" --exports-dir local-work/exports/build-25492057 --output research/artifacts/build-25492057/decision-catalog-q1-validation.json
```

The [validation](../artifacts/build-25492057/decision-catalog-q1-validation.json)
checks 34 exact function entries, 2,076 instruction locations, seven read-only
float values, two mode enum tables and the human/client RTTI slots23/29/35.
The wrapper reuses `verify_fine_map.py`, then checks five nonempty contract
columns per row, source hashes, inherited instruction lists and row links.
It compares the selection assembly's memory-operand/CALL sites against the
manifest inventory: **90 instructions, 45 classified sites, two stack-save
exclusions**. The four selection output rows cover the policy scalar writes
and slot29 call in that span. This is an instruction-site census, not a proof that
the row meanings or pointer lifetime assumptions are correct.

Negative probes rejected a missing access record, an unknown row ID, an empty
freshness cell, a changed source-map hash and a mid-instruction evidence address.
Their local fixtures/results are under `local-work/codex-decision/Q1/`.

There is no claim of complete base/state/helper coverage, no runtime validation,
and no completion marker for the overall decision frontier.

## Q1a extension — C-BASE enumerated

This additive section updates the earlier C-BASE open snapshot. Other coverage rows remain open. Repeated stable row IDs extend their read sites; Q1 contracts remain preserved. No state body assigned to Q1b–Q1f is claimed covered here.

The [Q1a manifest](../artifacts/build-25492057/decision-catalog-q1a-map.json) records 75 individual input records (including reused rows), 17 outputs and three immutable operands. [Validation](../artifacts/build-25492057/decision-catalog-q1a-validation.json) checks 15 game entries, 703 instruction locations, three float values and the event8 payload bytes. The eleven C-BASE methods have 252 memory-spelled/CALL sites: 162 linked to rows, 90 explicitly excluded as ABI, local scratch, derived storage or address-only operations. This is a bounded inventory, not a proof of semantic completeness outside those entries.

The threat-age query 0x878280 returns converted now−sensor+0x2F0, or a read-only ~1e38 sentinel for timestamp0; base flag17 is age<10. The event6 branch publishes named mandie at remembered XYZ, radius20·L, once per observer-player bit on the corpse brain. Event8 uses payloads(0,1); the generic one-state dispatcher instead zeroes both. Mode transition clears the entire gate byte+0x1E4 before writing mode1. These distinctions are instruction/data checked.

| Coverage row | Effective status | Remaining blanks |
| --- | --- | --- |
| C-BASE | enumerated / Q1a | No unclassified stored input or issued output in the eleven declared entries. Semantic/freshness/validity blanks below remain static work for Q1g and named later items. |

| Row | Read site | Access path / type / width | Meaning / domain | Freshness | Validity |
| --- | --- | --- | --- | --- | --- |
| I-OWNER | 0x903b24 0x903b6a 0x903b9b 0x903cc2 0x903daa 0x903e03 | brain +0x38, actor*, 8 bytes | Owner actor; extends Q1 read-site census. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Borrowed path, no retain in this reader. OPEN Q1g: consolidate within-call validity/attachment guarantees and caller guards. |
| I-SENSOR | 0x903b6e 0x903ba5 0x903cc6 0x903db0 | brain -> owner(+0x38) -> +0xDD0, sensor*, 8 bytes | Owner sensor used for age, category vectors and slot14. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Borrowed path, no retain in this reader. OPEN Q1g: consolidate within-call validity/attachment guarantees and caller guards. |
| I-OWNER-PLAYER | 0x903b44 | brain -> owner(+0x38) -> +0xDA8, uint8 | Player/owner index. Corpse notification requires index <18, same candidate owner; shift uses index modulo32. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Borrowed path, no retain in this reader. OPEN Q1g: consolidate within-call validity/attachment guarantees and caller guards. |
| I-CONTROL | 0x903b54 | brain +0x398 bits0..1, 2-bit value in dword | Property control: AI=0, user=1; here exactly0 permits corpse-discovery signal. Patterns2/3 fail this gate. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Inline live-brain storage; V-brain remains an explicit Q1g validity dependency. |
| I-CORPSE-SCENE-GATE | 0x903b35 0x903b4b | 0xA3BB20() -> AL Boolean; global scene getter 0xA6CF30 -> nonnull and byte+0x908 nonzero | Scene predicate; no arguments consumed by its body despite caller RCX. OPEN Q1g: native meaning of scene+0x908. | Current synchronous query; no writes in predicate. Scene state may change; phase/name OPEN Q1g. | Value return; no pointer retained. Query requires valid scene object if getter returns nonnull. |
| I-THREAT-AGE | 0x903b75 | owner sensor -> 0x878280(), float32 XMM0 result | Age of sensor+0x2F0 threat timestamp in converted simulation units; timestamp0 yields read-only ~1e38 sentinel. Flag17 is age<10. | On-demand now minus recorded timestamp; first binding writer 0x877D20 records a threat-producing signal (ownership map). Can change during mission. | Value return; no escaping pointer. Sensor lifetime is I-SENSOR. |
| I-CAT1-BEGIN | 0x903bac | brain -> owner -> sensor +0x280, subject** iterator, 8 bytes | Category vector 1 begin; 8-byte subject pointers. Read each bound once before this loop. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Borrowed path, no retain in this reader. OPEN Q1g: consolidate within-call validity/attachment guarantees and caller guards. |
| I-CAT1-END | 0x903bb3 | brain -> owner -> sensor +0x288, subject** iterator, 8 bytes | Category vector 1 end; 8-byte subject pointers. Read each bound once before this loop. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Borrowed path, no retain in this reader. OPEN Q1g: consolidate within-call validity/attachment guarantees and caller guards. |
| I-CAT0-BEGIN | 0x903ccd | brain -> owner -> sensor +0x268, subject** iterator, 8 bytes | Category vector 0 begin; 8-byte subject pointers. Read each bound once before this loop. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Borrowed path, no retain in this reader. OPEN Q1g: consolidate within-call validity/attachment guarantees and caller guards. |
| I-CAT0-END | 0x903cd4 | brain -> owner -> sensor +0x270, subject** iterator, 8 bytes | Category vector 0 end; 8-byte subject pointers. Read each bound once before this loop. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Borrowed path, no retain in this reader. OPEN Q1g: consolidate within-call validity/attachment guarantees and caller guards. |
| I-CAT1-SUBJECT | 0x903bc3 | I-CAT1-BEGIN..END -> [iterator], subject*, 8 bytes | Current category1 subject; used only until next iteration. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Borrowed path, no retain in this reader. OPEN Q1g: consolidate within-call validity/attachment guarantees and caller guards. |
| I-CAT0-SUBJECT | 0x903ce4 | I-CAT0-BEGIN..END -> [iterator], subject*, 8 bytes | Current category0 subject; used until next iteration or first publication. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Borrowed path, no retain in this reader. OPEN Q1g: consolidate within-call validity/attachment guarantees and caller guards. |
| I-SUBJECT-SUPPRESS | 0x903bc6 | category1 subject +0x6C, byte | Nonzero suppresses aggregate contribution. Threat event clears it; native name and other writers OPEN Q1g. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Borrowed path, no retain in this reader. OPEN Q1g: consolidate within-call validity/attachment guarantees and caller guards. |
| I-ATTACKING | 0x903bd3 | category1 subject +0x14 bit8, dword bit | J2 attacking; OR into brain flag14 when suppression byte is zero. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | V-subject; see category iterator lifetime dependency. |
| I-THREATENING | 0x903bf3 | category1 subject +0x14 bit7, dword bit | J2 threatening; OR into brain flag16 when suppression byte is zero. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | V-subject; see category iterator lifetime dependency. |
| I-INJURY-OBSERVATION | 0x903c11 | category1 subject +0x14 bit9, dword bit | Injury observation; OR into brain flag15. 0x877D20 injury signal sets it; commit expires strictly above0.4 while threatening set (ownership map). | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Borrowed path, no retain in this reader. OPEN Q1g: consolidate within-call validity/attachment guarantees and caller guards. |
| I-ENEMY | 0x903c31 | category1 subject +0x14 bit1, dword bit | Enemy AND targetable relationship result (J3); contributes flag13 only with unusable bit4 clear. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Borrowed path, no retain in this reader. OPEN Q1g: consolidate within-call validity/attachment guarantees and caller guards. |
| I-UNUSABLE | 0x903c31 | category1 subject +0x14 bit4, dword bit | Unusable relationship marker (includes non-Operatable); flag13 requires clear. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Borrowed path, no retain in this reader. OPEN Q1g: consolidate within-call validity/attachment guarantees and caller guards. |
| I-ALERT-MODE | 0x903c5c | brain +0x1E0, int32 | Alert-like mode; this branch changes0 to1. Native enum name/domain beyond this branch OPEN Q1g. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Inline live-brain storage; V-brain remains an explicit Q1g validity dependency. |
| I-ALERT-INJURY-GATE | 0x903c6b | brain +0x1E4 bit0, byte bit | Enables mode0->1 when aggregate injury flag15 set; native binding OPEN Q1g. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Inline live-brain storage; V-brain remains an explicit Q1g validity dependency. |
| I-ALERT-ENEMY-GATE | 0x903c7a | brain +0x1E4 bit1, byte bit | Enables mode0->1 when aggregate eligible-enemy flag13 set; native binding OPEN Q1g. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Inline live-brain storage; V-brain remains an explicit Q1g validity dependency. |
| I-SUBJECT-ACTOR | 0x903ce7 0x903d86 | category0 subject +0x18, actor*, 8 bytes | Observed actor; explicitly checked nonnull before the corpse gate. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Borrowed path, no retain in this reader. OPEN Q1g: consolidate within-call validity/attachment guarantees and caller guards. |
| I-INFORM-PENDING | 0x903cf0 | category0 subject +0x10 bit17, dword bit | Pending information eligible for informing (perception map); corpse branch requires set. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Borrowed path, no retain in this reader. OPEN Q1g: consolidate within-call validity/attachment guarantees and caller guards. |
| I-VISIBLE | 0x903cf0 | category0 subject +0x10 bit29, dword bit | Current visibility; corpse branch requires set, independently of pending information. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Borrowed path, no retain in this reader. OPEN Q1g: consolidate within-call validity/attachment guarantees and caller guards. |
| I-SUBJECT-KNOWN | 0x903d02 | category0 subject +0x14 bit0, byte bit | Known classification (ownership map), required before corpse notification. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Borrowed path, no retain in this reader. OPEN Q1g: consolidate within-call validity/attachment guarantees and caller guards. |
| I-SUBJECT-PLAYER | 0x903d0e | subject -> actor(+0x18) -> +0xDA8, uint8 | Observed actor player index; must equal owner player. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Borrowed path, no retain in this reader. OPEN Q1g: consolidate within-call validity/attachment guarantees and caller guards. |
| I-SUBJECT-COMPONENTS | 0x903d17 | subject -> actor -> +0x578, component container*, 8 bytes | Actor component aggregate; supplies vitality. Container itself not null-checked here. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Borrowed path, no retain in this reader. OPEN Q1g: consolidate within-call validity/attachment guarantees and caller guards. |
| I-SUBJECT-VITALITY | 0x903d1e | subject -> actor -> components(+0x578) -> +0x18, vitality*, 8 bytes | Vitality component; null explicitly skips corpse branch. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Borrowed path, no retain in this reader. OPEN Q1g: consolidate within-call validity/attachment guarantees and caller guards. |
| I-SUBJECT-DEAD | 0x903d27 | subject -> actor -> components -> vitality -> +0x174 bit0, byte bit | Final death/destruction, entity-health binding; not merely incapacitated. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Borrowed path, no retain in this reader. OPEN Q1g: consolidate within-call validity/attachment guarantees and caller guards. |
| I-SUBJECT-BRAIN | 0x903d31 | subject -> actor -> +0xDC8, brain*, 8 bytes | Observed actor brain holding per-player corpse notification mask. No local null check. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Borrowed path, no retain in this reader. OPEN Q1g: consolidate within-call validity/attachment guarantees and caller guards. |
| I-CORPSE-NOTIFIED | 0x903d43 | subject -> actor -> brain -> +0x11C, uint32 bitset | Already notified players for corpse discovery; current owner-index bit blocks a repeat. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Borrowed path, no retain in this reader. OPEN Q1g: consolidate within-call validity/attachment guarantees and caller guards. |
| I-SUBJECT-POSITION | 0x903d6b | category0 subject +0x28, float32 XYZ, 12 bytes passed by address | Remembered subject position supplied to mandie publisher; uses observation position, not a new actor-position sample. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Borrowed path, no retain in this reader. OPEN Q1g: consolidate within-call validity/attachment guarantees and caller guards. |
| I-SPATIAL-SCALE | 0x903d6f | global RVA0x14776C0, float32 | Writable engine distance scale L; file-initial20. Signal radius is20*L. | Current read. Definition parsing/init establishes units (signals-damage); full later writer/phase reconciliation Q1g. | V-global. |
| I-SQUAD | 0x903dc3 | brain +0x100, squad*, 8 bytes | Optional squad. If null, flag18 derives from own flag17. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Borrowed path, no retain in this reader. OPEN Q1g: consolidate within-call validity/attachment guarantees and caller guards. |
| I-SQUAD-FLAG1 | 0x903dcf | brain -> squad(+0x100) -> +0x10 bit1, dword bit | Squad flag copied to brain flag18 instead of own recent-threat flag17. Native meaning/writers OPEN Q1g. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Borrowed path, no retain in this reader. OPEN Q1g: consolidate within-call validity/attachment guarantees and caller guards. |
| I-ACTOR-XY | 0x903e07 | brain -> owner -> +0x74, float32 XY, 8 bytes | Current actor position copied to brain+0x128 when goal/follow flag6 or9 set. | Current position at reached reconsideration; motion can change it. Only the query/copy boundary is scoped. | Borrowed path, no retain in this reader. OPEN Q1g: consolidate within-call validity/attachment guarantees and caller guards. |
| I-ROOT-STATE | 0x903c8f 0x903e29 | brain +0xE0, state*, 8 bytes | Optional root; checked before event8 traversal and before interrupt block. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Borrowed path, no retain in this reader. OPEN Q1g: consolidate within-call validity/attachment guarantees and caller guards. |
| I-ACTIVE-STATE | 0x903ee0 0x903fe5 | brain +0xE8, state*, 8 bytes | Current active state, ancestor walk source and Execute(slot3) recipient. Execute path assumes nonnull after root gate. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Borrowed path, no retain in this reader. OPEN Q1g: consolidate within-call validity/attachment guarantees and caller guards. |
| I-STATE-PARENT | 0x903f3d 0x912564 0x91259d 0x9125a8 | state +0x20, parent state*, 8 bytes | Ancestor chain; null terminates base walk. Deactivation requires nonnull parent. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Attached state storage; deactivation preserves attachment. OPEN Q1g: consolidate callback lifetime. |
| I-STATE-CHILD | 0x912186 0x91219c 0x912470 0x9124a1 0x9124d8 0x9124f9 0x912551 | state +0x28, active child*, 8 bytes | Event propagation/deactivation follows active child; reenter assumes one exists. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Attached state storage; deactivation preserves attachment. OPEN Q1g: consolidate callback lifetime. |
| I-STATE-BRAIN | 0x9121c0 0x9121cb 0x9121e2 0x91220d 0x91221b 0x912231 0x912266 0x912277 0x91228d 0x91239b 0x9123df 0x912483 0x9124d4 0x91258b 0x912599 | state +0x30, brain*, 8 bytes | Set from parent at attach; borrowed backpointer. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Attached state storage; deactivation preserves attachment. OPEN Q1g: consolidate callback lifetime. |
| I-CURRENT-ORDER | 0x903fd2 0x91221f 0x91227b 0x91239f | brain +0xF0, order*, 8 bytes | Current order blocks Execute; interrupt clears through slot13. Create-sequence replaces it if distinct. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Inline live-brain storage; V-brain remains an explicit Q1g validity dependency. |
| I-IMMEDIATE-QUEUE-COUNT | 0x903fdc | brain +0x88, int32 | Positive immediate queue count blocks state Execute. Native queue binding/writers OPEN Q1g. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Inline live-brain storage; V-brain remains an explicit Q1g validity dependency. |
| I-ACTION-LABEL | 0x9121cf | state -> brain -> +0x108, label pointer, 8 bytes | Current action label; compared by pointer identity, not bytes. Matching candidate latches flag22. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Only compared, not dereferenced. Label storage identity/producer OPEN Q1g. |
| I-OWNED-CHILD-END | 0x912410 0x912424 | state +0x10, child** end, 8 bytes | Owned-child vector next slot; increment8 on append. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Attached state storage; deactivation preserves attachment. OPEN Q1g: consolidate callback lifetime. |
| I-OWNED-CHILD-CAPACITY | 0x912418 | state +0x18, child** capacity end, 8 bytes | Compared with end before append/grow; address formed through state+8 vector. | Read in reached base reconsideration after the sensor commit (F-sensor); mission-mutable. Full writer/phase reconciliation: Q1g. | Attached state storage; deactivation preserves attachment. OPEN Q1g: consolidate callback lifetime. |
| I-BFLAG-0 | 0x903dbd | brain +0x39C bit0, one bit of uint32 | dispatch inhibitor0; native name OPEN Q5 | Current policy flag. Complete named writer set OPEN Q5. | Inline live-brain storage; V-brain remains an explicit Q1g validity dependency. |
| I-BFLAG-1 | 0x903dbd | brain +0x39C bit1, one bit of uint32 | dispatch inhibitor1; native name OPEN Q5 | Current policy flag. Complete named writer set OPEN Q5. | Inline live-brain storage; V-brain remains an explicit Q1g validity dependency. |
| I-BFLAG-2 | 0x903dbd | brain +0x39C bit2, one bit of uint32 | dispatch inhibitor2; native name OPEN Q5 | Current policy flag. Complete named writer set OPEN Q5. | Inline live-brain storage; V-brain remains an explicit Q1g validity dependency. |
| I-BFLAG-3 | 0x903dbd | brain +0x39C bit3, one bit of uint32 | dispatch inhibitor3; native name OPEN Q5 | Current policy flag. Complete named writer set OPEN Q5. | Inline live-brain storage; V-brain remains an explicit Q1g validity dependency. |
| I-BFLAG-4 | 0x903dbd | brain +0x39C bit4, one bit of uint32 | dispatch inhibitor4; native name OPEN Q5 | Current policy flag. Complete named writer set OPEN Q5. | Inline live-brain storage; V-brain remains an explicit Q1g validity dependency. |
| I-BFLAG-5 | 0x903dbd | brain +0x39C bit5, one bit of uint32 | dispatch inhibitor5; native name OPEN Q5 | Current policy flag. Complete named writer set OPEN Q5. | Inline live-brain storage; V-brain remains an explicit Q1g validity dependency. |
| I-BFLAG-6 | 0x903dbd | brain +0x39C bit6, one bit of uint32 | direct goal/task active; O-DIRECT-GOAL sets, Q3 completes contract | Current policy flag. Complete named writer set OPEN Q5. | Inline live-brain storage; V-brain remains an explicit Q1g validity dependency. |
| I-BFLAG-9 | 0x903dbd | brain +0x39C bit9, one bit of uint32 | following active; squad following producer 0x902B20 sets, fine-map predicate consumes | Current policy flag. Complete named writer set OPEN Q5. | Inline live-brain storage; V-brain remains an explicit Q1g validity dependency. |
| I-BFLAG-13 | 0x903bcd 0x903c56 0x903d94 | brain +0x39C bit13, one bit of uint32 | eligible enemy aggregate | Rebuilt synchronously in 0x903B10 before interrupt traversal; mission-mutable. | Inline live-brain storage; V-brain remains an explicit Q1g validity dependency. |
| I-BFLAG-14 | 0x903bcd 0x903c56 0x903d94 | brain +0x39C bit14, one bit of uint32 | attacking aggregate | Rebuilt synchronously in 0x903B10 before interrupt traversal; mission-mutable. | Inline live-brain storage; V-brain remains an explicit Q1g validity dependency. |
| I-BFLAG-15 | 0x903bcd 0x903c56 0x903d94 | brain +0x39C bit15, one bit of uint32 | injury aggregate | Rebuilt synchronously in 0x903B10 before interrupt traversal; mission-mutable. | Inline live-brain storage; V-brain remains an explicit Q1g validity dependency. |
| I-BFLAG-16 | 0x903bcd 0x903c56 0x903d94 | brain +0x39C bit16, one bit of uint32 | threatening aggregate | Rebuilt synchronously in 0x903B10 before interrupt traversal; mission-mutable. | Inline live-brain storage; V-brain remains an explicit Q1g validity dependency. |
| I-BFLAG-17 | 0x903b8f 0x903d94 | brain +0x39C bit17, one bit of uint32 | own recent threat: age<10 | Rebuilt synchronously in 0x903B10 before interrupt traversal; mission-mutable. | Inline live-brain storage; V-brain remains an explicit Q1g validity dependency. |
| I-BFLAG-18 | 0x903dbd | brain +0x39C bit18, one bit of uint32 | squad flag1 if attached, otherwise own flag17 | Rebuilt synchronously in 0x903B10 before interrupt traversal; mission-mutable. | Inline live-brain storage; V-brain remains an explicit Q1g validity dependency. |
| I-BFLAG-20 | 0x903dbd | brain +0x39C bit20, one bit of uint32 | dispatch inhibitor20; native name OPEN Q5 | Current policy flag. Complete named writer set OPEN Q5. | Inline live-brain storage; V-brain remains an explicit Q1g validity dependency. |
| I-BFLAG-21 | 0x903f5a 0x903f8e | brain +0x39C bit21, one bit of uint32 | interrupt taken/state changed latch | Cleared at interrupt-walk start; set by dispatch helpers during that walk. | Inline live-brain storage; V-brain remains an explicit Q1g validity dependency. |
| I-BFLAG-22 | 0x9121e6 | brain +0x39C bit22, one bit of uint32 | candidate label matched current action latch | Cleared at interrupt-walk start; set by dispatch helpers during that walk. | Inline live-brain storage; V-brain remains an explicit Q1g validity dependency. |
| I-STATE-EVENT-DISPATCH | 0x912114 0x91217a 0x912190 0x9123f4 0x9124af 0x912506 0x91252a 0x9125ac | state -> vptr -> +0x10 (slot2), 8-byte pointers | Event receiver for concrete state; indirect call effects depend on event/state family (Q1b-Q1e,Q10). | Read on each synchronous invocation. | Attached state storage; deactivation preserves attachment. OPEN Q1g: consolidate callback lifetime. |
| I-STATE-INTERRUPT-DISPATCH | 0x903f88 | state -> vptr -> +0x20 (slot4), 8-byte pointers | Interrupt method; root first, stop on flag21. State bodies assigned other coverage rows. | Read on each invocation; per-type vtable in read-only image. | Attached state storage; deactivation preserves attachment. OPEN Q1g: consolidate callback lifetime. |
| I-STATE-EXECUTE-DISPATCH | 0x903fec | brain -> active state -> vptr -> +0x18 (slot3), 8-byte pointers | Execute only after no interrupt/current order/immediate queued order. | Read on invocation. | Attached state storage; deactivation preserves attachment. OPEN Q1g: consolidate callback lifetime. |
| I-SENSOR-INFORM-DISPATCH | 0x903db7 | owner sensor -> vptr -> +0x70(slot14), 8-byte pointers | Called with argument0 when flag13 or16 is true. OPEN Q1g: concrete dispatch/effect consolidation. | Read on invocation. | Borrowed path, no retain in this reader. OPEN Q1g: consolidate within-call validity/attachment guarantees and caller guards. |
| I-ORDER-SET-DISPATCH | 0x912229 0x912285 0x9123a8 | brain -> vptr -> +0x68(slot13), 8-byte pointers | Current-order setter/end boundary; null for interrupts, allocated complex sequence for 0x912370. | Read on invocation. | Inline live-brain storage; V-brain remains an explicit Q1g validity dependency. |
| I-STATE-CONTEXT | 0x912180 0x912206 0x91226a 0x91237a 0x9123ea 0x91246a 0x91252d | Incoming RCX, state*, 8 bytes | Current state/parent context supplied by caller; live state is required. | Per invocation, caller supplies the value; policy callers belong to their own coverage rows. | Borrowed call argument; no allocation by reader. Pointer arguments require live state/event; label is compared only. Handler retention and ownership reconciliation Q1g. |
| I-EVENT-CODE | 0x91211a | 0x912110 EDX, int32 | Caller event number; dispatcher builds payload(0,0). | Per invocation, caller supplies the value; policy callers belong to their own coverage rows. | Borrowed call argument; no allocation by reader. Pointer arguments require live state/event; label is compared only. Handler retention and ownership reconciliation Q1g. |
| I-EVENT-POINTER | 0x91217d | 0x912170 RDX, event*, 8 bytes | Caller event record, type int32+0 and two payload qwords+8/+0x10, borrowed for traversal. | Per invocation, caller supplies the value; policy callers belong to their own coverage rows. | Borrowed call argument; no allocation by reader. Pointer arguments require live state/event; label is compared only. Handler retention and ownership reconciliation Q1g. |
| I-CANDIDATE-LABEL | 0x9121c4 | 0x9121C0 RDX, label*, 8 bytes | Pointer identity for named interrupt ordering; stored at brain+0x110. | Per invocation, caller supplies the value; policy callers belong to their own coverage rows. | Borrowed call argument; no allocation by reader. Pointer arguments require live state/event; label is compared only. Handler retention and ownership reconciliation Q1g. |
| I-INTERRUPT-CONDITION | 0x912209 | 0x912200 DL, byte Boolean | Zero leaves state/order unchanged; nonzero takes interrupt. | Per invocation, caller supplies the value; policy callers belong to their own coverage rows. | Borrowed call argument; no allocation by reader. Pointer arguments require live state/event; label is compared only. Handler retention and ownership reconciliation Q1g. |
| I-CHILD-ARGUMENT | 0x9123e3 0x91246d | 0x9123D0/0x912460 RDX, child state*, 8 bytes | Valid child to attach/select. Attach copies parent/brain; selection assumes attached ownership. | Per invocation, caller supplies the value; policy callers belong to their own coverage rows. | Borrowed call argument; no allocation by reader. Pointer arguments require live state/event; label is compared only. Handler retention and ownership reconciliation Q1g. |
| I-DEACTIVATE-REASON | 0x912530 | 0x912520 EDX, int32 | Sign-extended event payload; caller reasons0/1, recursive descendant reason2. Full event interpretation Q10. | Per invocation, caller supplies the value; policy callers belong to their own coverage rows. | Borrowed call argument; no allocation by reader. Pointer arguments require live state/event; label is compared only. Handler retention and ownership reconciliation Q1g. |

| Row / issuing sites | Effect | Result codes | Ownership | Cancellation | Side effects |
| --- | --- | --- | --- | --- | --- |
| O-BASE-AGGREGATES; 0x903b2b, 0x903b9f, 0x903bec, 0x903c0b, 0x903c2a, 0x903c42, 0x903dee | Clear brain flags13..16 then OR eligible subject contributions; replace17 from threat age and18 from squad flag1/17. | No status. | Brain-owned scalar. | Next base pass recomputes. | Other bits preserved. Individual input flags have separate I rows. |
| O-ALERT-TRANSITION; 0x903c8a, 0x903c96 | 0x908710 clears +0x1E0 to -1 and byte+0x1E4 to0; caller sets mode1. | No status. | Inline brain. | Later mode/config writers supersede. | Clears BOTH automatic alert gate bits; then emits event8 if root exists. |
| O-STATE-EVENT8; 0x903ca5, 0x903cbd | Root then active descendants receive event8, payload qwords0 and1. | No status consumed. | Stack event borrowed synchronously. | No pending event object in this dispatch. | Receiver-specific effects remain Q1b-Q1e/Q10. |
| O-CORPSE-SIGNAL; 0x903d64, 0x903d8f | Set corpse brain+0x11C owner bit; publish mandie6 radius20*L, corpse actor source, remembered XYZ, null payload; break after first qualifying corpse. | No success value consumed. | Descriptor/position copied to publisher stack; no ownership transfer of actor. | Synchronous; mask prevents repeat for that observer player. | Publisher invokes qualifying recipient slot51 and scene signal-condition manager (signals-damage). Callback effects can persist. |
| O-SENSOR-INFORM; 0x903dba | Call owner sensor slot14(0) when eligible enemy or threatening aggregate set. OPEN Q1g: concrete method/effect. | No return consumed. | Borrowed sensor. | OPEN Q1g: any pending task scheduling. | OPEN Q1g: callback effects. |
| O-ZONE-ORIGIN; 0x903e0c | Copy owner XY to brain+0x128 when flag6 or9 set. | No status. | 8-byte value copied. | Later writes replace origin. | Distinct from direct goal at+0x130. |
| O-INTERRUPT-WALK; 0x903f65, 0x903f70, 0x903f8b, 0x903fac, 0x903fbb, 0x903fc1 | Clear flags21/22 and candidate+0x110; call ancestor Interrupt root-first until flag21; on interruption event5 to that state, set flag25; clear candidate afterward. | No boolean return from base consumed; flag21 is the stop/result channel. | Temporary vector contains borrowed state pointers; freed at end. | Stops remaining interrupt calls once latch set. | State methods can replace current order; flag25 producer recorded for Q5. |
| O-STATE-EXECUTE; 0x903fef | Invoke active-state Execute(slot3) when low6 flags/flag20 clear, root exists, no interrupt/order, queue count<=0. | No result consumed at boundary. | Borrowed active state. | Not called when gate fails. | Policy-family outputs remain Q1b-Q1e. |
| O-SEND-STATE-EVENT; 0x912129, 0x912183, 0x912199 | 0x912110 sends event EDX to one state with both payloads0; 0x912170 sends caller event to state then current active descendants. | No return consumed. | Stack/caller event borrowed synchronously; no copying to owned queue here. | No async work at dispatcher boundary. | Traverses child pointer AFTER each callback; concrete handlers Q10. |
| O-NAMED-GATE; 0x9121c4, 0x9121d8 | Store candidate label at brain+0x110; equal to+0x108 latches flag22. | AL = NOT flag22 (Boolean only). | Borrowed label pointer, not copied string. | Flag22 reset on next interrupt walk; candidate cleared by base. | Earlier match prevents later candidates, even if later label differs. |
| O-PREPARE-INTERRUPT; 0x912211, 0x91222e, 0x912235, 0x91226d, 0x91228a, 0x912291, 0x91229e | Set flag21; if current order nonnull call brain slot13(null); select this state at+0xE8. Conditional wrapper does nothing for false DL. | 0x912200 AL equals condition; 0x912260 no status. | Borrowed state; order ownership via O-SET-ORDER. | Current order cleared through its normal setter. | Does not change parent active-child by itself. |
| O-SET-ORDER; 0x91222e, 0x91228a, 0x9123ae | Brain slot13 installs supplied order/null; Q1g/Q10 complete end, ownership, cancellation and failure contract. | No result used by these callers. | OPEN Q1g/Q10: accepted order ownership and rejected factory result. | Current order ends/replaced via setter, details Q10. | Native setter boundary only; no executor internals. |
| O-CREATE-SEQUENCE; 0x912382, 0x91238f, 0x9123ae | Allocate0x58 and construct complex-order sequence(0x8FFD50), install through slot13 if distinct from current. | Returns allocated pointer or null; null may replace current order on allocation failure. | Installed pointer returned as borrowed sequence for appends; complete ownership Q1g/Q10. | Later replacement/cancel through order setter Q10. | Sequence execution/failure propagation Q10; allocator internals parked. |
| O-ATTACH-STATE; 0x9123e6, 0x9123ed, 0x91240d, 0x91241e, 0x912424, 0x912439 | Copy parent brain and parent pointer into child; send event1 then append child to owned vector. | Returns child pointer. | Parent owns attached-child list; callback lifetime/destruction reconciliation Q1g. | Detach/destruction not part of this routine. | Event1 can initialize descendants; vector growth only when full. |
| O-SWITCH-STATE; 0x91247e, 0x91248c, 0x912498, 0x9124b2 | Deactivate old child reason1; install child in parent+0x28 and brain+0xE8; send event2 payload0. | No status. | Attached child reused; no destruction. | Deactivation recurses and sends events3/4. | Event2 receiver effects are policy-family boundaries. |
| O-REENTER-STATE; 0x9124ed, 0x912509 | Select existing child at brain+0xE8; event2 with payload1, other payload0. | No status. | Attached child, assumed nonnull. | Immediate selection. | Does not call Execute directly. |
| O-DEACTIVATE-STATE; 0x91254e, 0x91255f, 0x912583, 0x91258f, 0x9125a1, 0x9125af | Event4(original reason); deactivate descendants reason2; clear parent child; flag21; select parent; parent event3(original reason). | No status. | Retains attached state; does not free it. | Deactivates descendants but leaves owned vector intact. | Parent required. Event callbacks may mutate state; event-specific outputs Q10. |

Q1a fixed operands: K-THREAT-AGE at0x1004044 =10; K-CORPSE-RADIUS at0x1010EB8 =20; K-NO-THREAT at0x10004DC ≈1e38. All are read-only float32, valid for module lifetime. Event8 qwords at0x1004260 are0,1. Spatial scale0x14776C0 remains a separate writable input.

Reproduction: use the Q1a candidate TSV with read-only focused export into call_to_arms-decision-catalog-q1a, then run research/scripts/verify_decision_pass.py with --map research/artifacts/build-25492057/decision-catalog-q1a-map.json, --binaries-dir "$GOH_BINARIES" and --exports-dir local-work/exports/build-25492057. Three retained local negative probes reject a missing census site, unknown row and empty freshness column. No game/editor execution or modification; no editor analysis.
