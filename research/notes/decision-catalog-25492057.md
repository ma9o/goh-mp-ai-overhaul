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

## Q1b partial checkpoint — root and restoration policy

The user shortened the remaining census while Q1b was in progress. This section
retains the reviewed work; **C-ROOT is partial, not fully enumerated**.
The [Q1b manifest](../artifacts/build-25492057/decision-catalog-q1b-map.json)
contains 83 input records and 28 output boundaries. The
[validation](../artifacts/build-25492057/decision-catalog-q1b-validation.json)
checks the fingerprint, 33 game entries, 3,226 instruction locations and six
immutable float operands. Local structural checks also confirmed every row's
five columns, instruction links, unique IDs and six unchanged source-map hashes.
There is no whole-scanner or complete instruction-census claim.

Location selection has a distinct tail body at0x9526D0;0x9526B0's assembly only
contains its child-present gate. Restoration helper0x956F20 reads **R8B** as the
position/cover-snapshot permission separately from DL's save-order permission;
0x956B20 forces DL=1 and forwards R8B. Soldier root0x957380 updates scanner
results, checks brain+0x3E0, calls0x9440E0(brain,1) when due, and clears that
deadline only on false. Q5 now has this concrete consumer.

The five hazard scanners remain query interfaces: their embedded brain pointer
is input; slot2 writes active/detected state before arbitration; slot1 returns
an order or performs a side effect. The preserved movement map supplies grenade
detection's visibility/link/tag/distance gates, obstacle point-query inputs, burn
bounding-region queries, crush moving-record inputs and out-of-bounds eligibility.
No scanner producer internals were expanded in this checkpoint. Nearby-burn
slot1's established normal-entry null result and world slot1's deletion plus null
return remain distinct. Retained entity/order ownership details are explicit
blanks, not runtime-only declarations.

The following records preserve the work already done. References to Q1g/Q10 in
these checkpoint rows describe the former full-census plan; the forthcoming Q1z
scope decides which blanks are active. Non-prototype blanks are deferred.

| Row | Read site | Access path / type / width | Meaning / domain | Freshness | Validity |
| --- | --- | --- | --- | --- | --- |
| I-STATE-BRAIN | 0x957420, 0x957680, 0x9526ee, 0x952702, 0x949f00, 0x9573ce, 0x9573ea, 0x9573f9, 0x955ad7, 0x955b56, 0x955c5b, 0x955cd3, 0x955cee, 0x956036, 0x9562cf, 0x9562f0, 0x9563e1, 0x9563fe, 0x956409, 0x95655d, 0x956667, 0x9566ea, 0x956732, 0x9567ab, 0x9567fd, 0x956908, 0x956b44, 0x956f4c, 0x956f6d, 0x956f85 | root/location +0x30, brain*, 8 bytes | Attached brain backpointer; extends Q1a. | Current synchronous policy read; mission-mutable. Exact writer/phase where not stated remains OPEN Q1g. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-OWNER | 0x957424, 0x957684, 0x949f04, 0x955b6d, 0x955c1e, 0x955c80, 0x955cde, 0x955d08, 0x955d19, 0x95610b, 0x9561af, 0x956260, 0x956372, 0x95640d, 0x956463, 0x9564f0, 0x95650a, 0x956541, 0x9565ee, 0x9566f7, 0x9567af, 0x9568c3, 0x95690c, 0x956959, 0x956bfc, 0x956cce, 0x956f89, 0x956fdd, 0x957073, 0x95708d, 0x9570c4 | state -> brain -> +0x38, actor*, 8 bytes | Owner actor; excludes stack slots and restore-target temporaries by operand form. | Current synchronous policy read; mission-mutable. Exact writer/phase where not stated remains OPEN Q1g. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-COVER | 0x957428, 0x957688, 0x949f08, 0x956411, 0x956467, 0x9567b3, 0x956916, 0x956f8d, 0x956fe1 | brain -> owner -> +0xDC0, cover*, 8 bytes | Owner cover object. Root does not retain or replace it. | Current synchronous policy read; mission-mutable. Exact writer/phase where not stated remains OPEN Q1g. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-COVER-GROUP | 0x95742f, 0x95768f, 0x949f0f, 0x956418, 0x9567ba, 0x95691d, 0x956f94 | brain -> owner -> cover -> +0x24, int32 | 1 vertical,2 horizontal,0/3/4 outdoors; nonzero with resident byte enables cover restoration. Q7 owns complete native names. | Read current value at selection/snapshot/expiry; snapshot root+0x4B8 tracks last selected group. Cover query 0x8988C0 can invalidate resident cover during this interrupt. Other producer phase OPEN Q1g. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-ROOT-LAST-GROUP | 0x956910 | root +0x4B8, int32 | Last selector cover group; mismatch invokes root slot5. | Written by soldier/civilian selector each time it runs. | Inline root field; attached-state lifetime Q1g. |
| I-ROOT-OUTDOORS | 0x957450, 0x957698 | root +0x3D8, state*, 8 bytes | Soldier named soldier_outdoors; civilian outdoors. Initialization/attachment evidence movement0x9570F0. | Set during root event1 initialization. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-ROOT-VERTICAL | 0x957469 | soldier root +0x580, state*, 8 bytes | Named soldier_cover_vertical; selected group1. | Event1 initialization via0x9570F0; attached child. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-ROOT-HORIZONTAL | 0x95745d | soldier root +0x588, state*, 8 bytes | Named soldier_cover_horizontal; selected group2. | Event1 initialization via0x9570F0; attached child. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-STATE-CHILD | 0x9526b0 | location +0x28, state*, 8 bytes | Non-null child bypasses location selection at entry0x9526B0. | Current synchronous policy read; mission-mutable. Exact writer/phase where not stated remains OPEN Q1g. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-LOCATION-IDLE | 0x9526e5, 0x952720, 0x952750 | location +0x3D8, state*, 8 bytes | Idle child; default and non-alert mode choice. | Initialized by location event1; retained through state attachment. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-LOCATION-BATTLE | 0x952719 | location +0x3E0, state*, 8 bytes | Battle child; alert mode1 with selected target. | Initialized by location event1. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-LOCATION-SEARCH | 0x9526da, 0x9526fb | location +0x3E8, optional state*, 8 bytes | Find-enemy candidate only when it exists and0x942CB0 returns true. Null checked. | Initialized for outdoors location; absent families retain null. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-LOCATION-PREVIOUS | 0x952727 | location +0x3F0, state*, 8 bytes | Previous selected child; leaving idle for a nonidle choice requests brain flag11. | Written by selection0x95275A on every reached selection. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-ALERT-MODE | 0x952706 | brain +0x1E0, int32 | Exactly1 permits battle/search; otherwise select idle. Q1a owns alert transition, native name OPEN Q1g. | Current synchronous policy read; mission-mutable. Exact writer/phase where not stated remains OPEN Q1g. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-SELECTED-TARGET | 0x95270f | brain +0x3B8, subject*, 8 bytes | Current selected subject; nonnull selects battle in alert mode1. Target refresh C-HELPERS. | Current synchronous policy read; mission-mutable. Exact writer/phase where not stated remains OPEN Q1g. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-REMEMBERED-SUBJECT | 0x95273e | brain +0x3C0, subject*, 8 bytes | Remembered subject; null checked, then auxiliary handle must be nonnegative to keep nonidle selection. | Current synchronous policy read; mission-mutable. Exact writer/phase where not stated remains OPEN Q1g. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-SUBJECT-RECON-HANDLE | 0x95274a | brain -> remembered subject -> +0x78, int32 | Auxiliary Recon registration handle; negative absent. Not the searched bit or zone completion. | Sensor commit/register/load lifecycle (perception); exact freshness/validity reconciliation C-SEARCH/Q1g. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-ADVANCE-ELIGIBLE | 0x9526f2 | 0x942CB0(brain) -> AL Boolean | True requires AI control, movement neither hold nor zone, flag6 clear, behavior>2 and0x905BE0 false. Full per-field helper census C-HELPERS. | On-demand, current brain fields; no write in wrapper. | Scalar result; borrowed brain. |
| I-SAVED-ORDER | 0x955ab9, 0x955b0a, 0x955b1c, 0x9562c2, 0x9563d4, 0x9566a4, 0x9566b6, 0x956f3e | root +0x568, order*, 8 bytes | Detached current order saved for post-avoidance restoration; distinct from live brain+0xF0. | Saved synchronously before replacement, cleared on restore or user-queue discard. | Root holds saved pointer after End; may be deleted on discard. Detailed order ownership Q1g/Q10. |
| I-RESTORE-MODE | 0x955acf, 0x9562b6, 0x9563c8, 0x95665f, 0x956b97, 0x956f2a | root +0x4C0, int32 | 0 no positional restoration;1 saved position/direction/type;2 saved resident-cover descriptor. | Set during first snapshot only while mode0 and no saved order; cancel/interface paths reset. Event0x10 completion Q10. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-IMMEDIATE-QUEUE-COUNT | 0x955adb, 0x955b95, 0x95666b, 0x956801 | brain +0x88, int32 | Positive pending immediate orders block restore and allow interface interruption. | Current synchronous policy read; mission-mutable. Exact writer/phase where not stated remains OPEN Q1g. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-USER-QUEUE-BEGIN | 0x955aea, 0x955ba5, 0x95667a, 0x956810 | brain +0x98, order** begin, 8 bytes | Compared with end to detect additional queued orders. Detailed queue ownership Q1g/Q10. | Current synchronous policy read; mission-mutable. Exact writer/phase where not stated remains OPEN Q1g. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-USER-QUEUE-END | 0x955ae3, 0x955b9e, 0x956673, 0x956809 | brain +0xA0, order** end, 8 bytes | User-order queue end; unlike immediate count, checked by pointer inequality. | Current synchronous policy read; mission-mutable. Exact writer/phase where not stated remains OPEN Q1g. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-CURRENT-ORDER | 0x955af3, 0x95603a, 0x9562d3, 0x9563e5, 0x956561, 0x956736, 0x956b48, 0x956f50 | state -> brain -> +0xF0, order*, 8 bytes | Current order; compared before replacement and detached into saved order. | Current synchronous policy read; mission-mutable. Exact writer/phase where not stated remains OPEN Q1g. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-CONTROL | 0x955b88, 0x95642b, 0x9566ee, 0x956fa6 | brain +0x398 bits0..1, 2-bit value | AI0/user1. User+queued orders suppress burning response; AI required to stand from shallow-water condition; user permits position snapshot. | Current synchronous policy read; mission-mutable. Exact writer/phase where not stated remains OPEN Q1g. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-MOVE-MODE | 0x95642b, 0x956fa6 | brain +0x398 bits12..13, 2-bit value | free0/hold1/zone2; hold permits restoration snapshot even without resident cover. | Current synchronous policy read; mission-mutable. Exact writer/phase where not stated remains OPEN Q1g. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-BURNING-TAG-INDEX | 0x955b50 | global0x1598D68, int32 | Index used to test owner tag bit in named burning branch. OPEN Q1g: initializer binding and valid index domain. | Current mutable global read; initialization/later writers OPEN Q1g. | V-global; valid index precondition. |
| I-ACTOR-BURNING-TAG | 0x955b71 | brain -> owner -> byte[+0xDAD + index/8], selected bit | Tag membership selected by I-BURNING-TAG-INDEX; gates burning policy. Native binding/producer OPEN Q1g. | Current synchronous policy read; mission-mutable. Exact writer/phase where not stated remains OPEN Q1g. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-CHASSIS | 0x955c9f, 0x956545, 0x9566fb, 0x9570c8 | brain -> owner -> +0xDE0, chassis*, 8 bytes | Owner chassis; used for movement mode, actual type and snapshot. | Current synchronous policy read; mission-mutable. Exact writer/phase where not stated remains OPEN Q1g. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-MOVEMENT-TYPE | 0x95654c, 0x956702, 0x9570cf | brain -> owner -> chassis -> +0x2C, int32 | Actual movement type;3 swim bound in movement diagnostic; saved for restoration. Remaining native enum names Q8. | Current synchronous policy read; mission-mutable. Exact writer/phase where not stated remains OPEN Q1g. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-ACTOR-WATER-CLASS | 0x956708 | brain -> owner -> +0xA38, uint8 | Current byte must equal3 to leave swim under named stand_from_water. OPEN Q1g: distinguish exact producer/binding from on-demand I-TERRAIN. | Current synchronous policy read; mission-mutable. Exact writer/phase where not stated remains OPEN Q1g. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-COVER-RESIDENT | 0x95641d, 0x956f9a | brain -> owner -> cover -> +0x50, byte | Resident-cover condition combined with nonzero group; permits descriptor restoration. Native field binding Q7. | Current synchronous policy read; mission-mutable. Exact writer/phase where not stated remains OPEN Q1g. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-COVER-DESCRIPTOR | 0x95646e, 0x956479, 0x956484, 0x95648f, 0x95649a, 0x9564a5, 0x9564b0, 0x9564be, 0x9564cc, 0x956fe8, 0x956ff3, 0x956ffe, 0x957009, 0x957014, 0x95701f, 0x95702a, 0x957038, 0x957046 | owner cover +0x20..+0xA7, opaque0x88-byte value | Pass-through descriptor copied for later MoveToCover; interpreted descriptor subfields belong to Q7, not inferred from block copy. | Copied synchronously on first eligible snapshot. | Value copy; any embedded borrowed identity/handle validity OPEN Q7/Q1g. |
| I-ACTOR-XY | 0x955d0c, 0x9564f4, 0x957077 | brain -> owner -> +0x74, float32 XY | Current XY for burning path start and restore snapshot. | Current synchronous policy read; mission-mutable. Exact writer/phase where not stated remains OPEN Q1g. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-ACTOR-Z | 0x956501, 0x957084 | brain -> owner -> +0x7C, float32 | Current Z for saved position. | Current synchronous policy read; mission-mutable. Exact writer/phase where not stated remains OPEN Q1g. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-FACING-X | 0x955d1d, 0x95650e, 0x957091 | brain -> owner -> +0x50, float32 | Facing vector X; snapshot multiplies by10, burning uses atan2(Y,X). | Current synchronous policy read; mission-mutable. Exact writer/phase where not stated remains OPEN Q1g. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-FACING-Y | 0x955d22, 0x956513, 0x957096 | brain -> owner -> +0x54, float32 | Facing vector Y; snapshot multiplies by10, burning uses atan2(Y,X). | Current synchronous policy read; mission-mutable. Exact writer/phase where not stated remains OPEN Q1g. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-FACING-Z | 0x956518, 0x95709b | brain -> owner -> +0x58, float32 | Facing vector Z; snapshot multiplies by10, burning uses atan2(Y,X). | Current synchronous policy read; mission-mutable. Exact writer/phase where not stated remains OPEN Q1g. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-SAVED-COVER | 0x956c59, 0x956c60 | root +0x4E0..+0x567, opaque0x88-byte descriptor | Saved descriptor passed to MoveToCover factory for mode2. | Written on snapshot, consumed after avoidance. | Inline copy; embedded handle validity Q7/Q1g. |
| I-SAVED-POSITION-X | 0x956d35, 0x956d6e | root +0x4C4, float32 | Snapshot position X; Move destination and base of Rotate point. | Written by eligible mode1 snapshot. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-SAVED-POSITION-Y | 0x956d7e | root +0x4C8, float32 | Snapshot position Y; Move destination and base of Rotate point. | Written by eligible mode1 snapshot. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-SAVED-POSITION-Z | 0x956d76 | root +0x4CC, float32 | Snapshot position Z; Move destination and base of Rotate point. | Written by eligible mode1 snapshot. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-SAVED-DIRECTION-X | 0x956d86 | root +0x4D0, float32 | Saved facing X times10; added to saved position for Rotate target. | Written by eligible mode1 snapshot. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-SAVED-DIRECTION-Y | 0x956d61 | root +0x4D4, float32 | Saved facing Y times10; added to saved position for Rotate target. | Written by eligible mode1 snapshot. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-SAVED-DIRECTION-Z | 0x956d52 | root +0x4D8, float32 | Saved facing Z times10; added to saved position for Rotate target. | Written by eligible mode1 snapshot. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-SAVED-MOVEMENT-TYPE | 0x956e43 | root +0x4DC, int32 | Saved actual movement type; passed to MovementChange(type,-1,wait=true). | Written by eligible mode1 snapshot. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-COVER-CHECK-DEADLINE | 0x9567cf, 0x9567dc | root +0x570, uint32 | Cover validity next-check deadline;0 initializes; when due, advance by converted1 from prior deadline, not always now. | Root interrupt updates when cover group nonzero; can change each reached due check. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-COVER-VALIDATION | 0x956837 | 0x8988C0(cover,diagnosticLabel) -> int32 | Cover Update result2 triggers Hide(5,false); returns2 if actor pointer+0xA8 absent or group0, else candidate validity result. Other result codes OPEN Q7. | On-demand when deadline due. Mutating query: result2 from candidate check unregisters/releases current cover via0x898C50. | Borrowed cover; query temporary resources released before return. |
| I-EQUIPMENT-DEADLINE | 0x9573d2, 0x9573db | brain +0x3E0, uint32 | Nonzero due deadline triggers0x9440E0(brain,1); false clears deadline, true leaves it for later decisions. Q5 owns meaning/writers. | Current synchronous policy read; mission-mutable. Exact writer/phase where not stated remains OPEN Q1g. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-EQUIPMENT-POLICY-RESULT | 0x9573f0 | 0x9440E0(brain,1) -> AL Boolean | Equipment policy helper: false on0x7CCEB0 owner gate, otherwise checks carried item/tags and can submit UseItem. Boolean is not an order-success status. C-HELPERS enumerates its policy reads. | Called after five scanner updates, before generic root arbitration; side effects may occur now. | Scalar return; helper temporary inventory handles released. |
| I-NOW | 0x9573e1, 0x9567d7, 0x95681d | 0x65E700() -> uint32 simulation counter | Same I-NOW clock as Q1; absolute unsigned deadline comparisons. | On-demand during root interrupt. | V-global. |
| I-DURATION-FACTOR | 0x9567c8 | 0x65E730(1.0) -> converted integer duration | Clock conversion for cover-check cadence; ordinary1000 counter ticks per simulation unit. | Current writable conversion factor; Q1 clock contract. | Value result. |
| I-CRUSH-SCANNER | 0x957394, 0x957397 | root embedded +0x3E0 -> vptr slot2(+0x10), pointer-width dispatch | Movement RTTI scanner CRUSH; slot2 updates detection before root arbitration. Object+8 holds brain; no separate allocation at this call. | Synchronous in soldier Interrupt, order crush/obstacle/grenade/burn/world. | Embedded root storage, initialized by0x9577C0; root lifetime applies. |
| I-CRUSH-ACTIVE | 0x95620f | root +0x3F0, byte (scanner+0x10) | Most recent CRUSH detection result, nonzero selects named gate. Query inputs/results described in scanner interface paragraph. | Written by scanner slot2 immediately before arbitration in soldier root; inherited generic/civilian call cadence not inferred. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-OBSTACLE-SCANNER | 0x9573a1, 0x9573a4 | root embedded +0x418 -> vptr slot2(+0x10), pointer-width dispatch | Movement RTTI scanner OBSTACLE; slot2 updates detection before root arbitration. Object+8 holds brain; no separate allocation at this call. | Synchronous in soldier Interrupt, order crush/obstacle/grenade/burn/world. | Embedded root storage, initialized by0x9577C0; root lifetime applies. |
| I-OBSTACLE-ACTIVE | 0x9560c0 | root +0x428, byte (scanner+0x10) | Most recent OBSTACLE detection result, nonzero selects named gate. Query inputs/results described in scanner interface paragraph. | Written by scanner slot2 immediately before arbitration in soldier root; inherited generic/civilian call cadence not inferred. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-GRENADE-SCANNER | 0x9573ae, 0x9573b1 | root embedded +0x468 -> vptr slot2(+0x10), pointer-width dispatch | Movement RTTI scanner GRENADE; slot2 updates detection before root arbitration. Object+8 holds brain; no separate allocation at this call. | Synchronous in soldier Interrupt, order crush/obstacle/grenade/burn/world. | Embedded root storage, initialized by0x9577C0; root lifetime applies. |
| I-GRENADE-ACTIVE | 0x956317 | root +0x478, byte (scanner+0x10) | Most recent GRENADE detection result, nonzero selects named gate. Query inputs/results described in scanner interface paragraph. | Written by scanner slot2 immediately before arbitration in soldier root; inherited generic/civilian call cadence not inferred. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-BURN-SCANNER | 0x9573bb, 0x9573be | root embedded +0x438 -> vptr slot2(+0x10), pointer-width dispatch | Movement RTTI scanner BURN; slot2 updates detection before root arbitration. Object+8 holds brain; no separate allocation at this call. | Synchronous in soldier Interrupt, order crush/obstacle/grenade/burn/world. | Embedded root storage, initialized by0x9577C0; root lifetime applies. |
| I-BURN-ACTIVE | 0x95616b | root +0x448, byte (scanner+0x10) | Most recent BURN detection result, nonzero selects named gate. Query inputs/results described in scanner interface paragraph. | Written by scanner slot2 immediately before arbitration in soldier root; inherited generic/civilian call cadence not inferred. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-WORLD-SCANNER | 0x9573c8, 0x9573cb | root embedded +0x498 -> vptr slot2(+0x10), pointer-width dispatch | Movement RTTI scanner WORLD; slot2 updates detection before root arbitration. Object+8 holds brain; no separate allocation at this call. | Synchronous in soldier Interrupt, order crush/obstacle/grenade/burn/world. | Embedded root storage, initialized by0x9577C0; root lifetime applies. |
| I-WORLD-ACTIVE | 0x956593 | root +0x4A8, byte (scanner+0x10) | Most recent WORLD detection result, nonzero selects named gate. Query inputs/results described in scanner interface paragraph. | Written by scanner slot2 immediately before arbitration in soldier root; inherited generic/civilian call cadence not inferred. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-HAZARD-ORDER-RESULT | 0x9562a7, 0x9563b9, 0x956635 | scanner slot1(+8) -> order* or null, 8 bytes | Build escape/action from current scanner state. Null obstacle/burn/crush/grenade result returns without replacement. World scanner requests entity deletion and returns null. | Queried only after active byte and named gate allow. Can have side effects even if null. | Non-null result installed/transferred to current order; detailed ownership Q1g/Q10. |
| I-SPATIAL-SCALE | 0x955cf2 | global0x14776C0, float32 | L: burning candidate radius interpolates L..5L; writable units scale. | Read per burning decision. | V-global. |
| I-RNG | 0x955e41, 0x955e70, 0x955e7a, 0x955ea7, 0x955eb9, 0x955ee6 | 0x554B50() -> selected RNG*, 8 bytes | RNG context for three draws per burning candidate; up to10 attempts. | On-demand context getter; stream shared with other users. | V-rng unresolved custom-context lifetime retained from Q1. |
| I-RNG-STATE | 0x955e4a, 0x955e82, 0x955ec1 | selected RNG +8, uint32 | Same LCG as Q1; state updated three times per attempted burning candidate. | Read/write during decision. | V-rng. |
| I-RNG-COUNT | 0x955e51, 0x955e89, 0x955ec8 | selected RNG +0xC, uint32 | Increment per draw; not a tactical threshold. | Read-modify-write per draw. | V-rng. |
| I-BURN-RNG-SCALE | 0x955e67, 0x955e9f, 0x955ede | global0x1587D5C, float32 | Multiplier for upper24 LCG bits in burning policy. Distinct from Q1 global0x1587C54. OPEN Q1g: initialization/value. | Writable non-file-backed global; writer/domain OPEN Q1g. | V-global. |
| I-BURN-LINE-COST | 0x955f56, 0x955f62 | 0x8AC950(out line,actor context,startXY,candidateXY);0x8ACA00(line,0) -> float32 cost | Hypothetical candidate feasibility; accept strictly below100000 then0x8AD130 materializes route. Query interface only (path-acceptance/path-lines evidence). | Evaluated anew for up to10 candidates. Mutates temporary context/cache; no movement issued by cost query. | Caller owns temporary line/route/context and releases them; no result pointer retained. |
| I-BURN-ROUTE-VALID | 0x955f9f | stack route +0x110 at RBP+0x160, byte | Route validity returned by materialization; must be nonzero. Independent query result, not excluded as ABI scratch. | Written by reached materialization after accepted cost. | Stack-owned route for this call. |
| I-BURN-ROUTE-BEGIN | 0x95600e | stack route +0 at RBP+0x50, point* (8 bytes) | Route point vector begin; count=(end-begin)/12 must be>=2. | Query materialization current result. | Owned route; factory transfer/copy details Q1g. |
| I-BURN-ROUTE-END | 0x956000 | stack route +8 at RBP+0x58, point* (8 bytes) | Route point vector end; used with begin. | Query materialization current result. | Owned route. |
| I-DEFAULT-TARGET-X | 0x956dd3 | global0x146E000, float32 | Default target coordinate X used during temporary Rotate target initialization before setting saved point. | Writable file-backed defaults; producer/value domain OPEN Q1g. | V-global. |
| I-DEFAULT-TARGET-Y | 0x956de0 | global0x146E004, float32 | Default target coordinate Y used during temporary Rotate target initialization before setting saved point. | Writable file-backed defaults; producer/value domain OPEN Q1g. | V-global. |
| I-DEFAULT-TARGET-Z | 0x956df3 | global0x146E008, float32 | Default target coordinate Z used during temporary Rotate target initialization before setting saved point. | Writable file-backed defaults; producer/value domain OPEN Q1g. | V-global. |
| I-SNAPSHOT-SAVE-REQUEST | 0x956f5c | 0x956F20 DL, byte Boolean | Controls whether current order is detached and saved. | Caller supplied;0x956B20 forces1. | Value argument. |
| I-SNAPSHOT-RESTORE-REQUEST | 0x956f31, 0x956f7c | 0x956F20 R8B, byte Boolean | Controls cover/position snapshot independently from saving current order.0x956B20 forwards caller R8B. | Caller supplied; root world branch passes1. | Value argument. |
| I-AVOIDANCE-ORDER-ARGUMENT | 0x956b20, 0x956b2f | 0x956B20 RDX, order* | Null makes wrapper do nothing; nonnull triggers snapshot, interrupt preparation, setter. | Caller scanner/action result. | Ownership transferred on install; null deletion-result can already have produced side effect. |
| I-ROOT-SELECT-DISPATCH | 0x956922, 0x956928 | root -> vptr -> +0x28(slot5), pointer-width | Location selector called if cover group differs from root cached group. | Current type dispatch. | Attached root. |
| I-ORDER-END-DISPATCH | 0x955b16, 0x9562ea, 0x9563f8, 0x9566b0, 0x956f67 | current/saved order -> vptr slot4(+0x20), 8-byte pointers | End called before detaching or deleting; complete contract Q1g/Q10. | Synchronous when root replaces/discards. | Owned current/saved order. |
| I-ORDER-DELETE-DISPATCH | 0x955b28, 0x9566c2 | saved order -> vptr slot0, 8-byte pointer | Deleting destructor invoked with1 after End on queued-user-order discard. | Synchronous on discard. | No use after clearing saved pointer. |
| I-ORDER-SET-DISPATCH | 0x955afc, 0x956043, 0x956046, 0x95656e, 0x95673f, 0x956b51 | brain -> vptr -> slot13(+0x68), 8-byte pointers | Current-order setter; snapshot path first detaches old pointer to avoid deleting saved order. | On root output issuance. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |
| I-CHASSIS-MODE-DISPATCH | 0x955cc1 | owner chassis -> vptr slot44(+0x160), 8-byte pointers | SetMovementMode boundary, arguments2,0 in burning branch. | Immediate output call. | Borrowed state/brain/owner path; Q1g must consolidate within-call lifetime. No local retain by this read. |

| Row / issuing sites | Effect | Result codes | Ownership | Cancellation | Side effects |
| --- | --- | --- | --- | --- | --- |
| O-ROOT-SELECT; 0x957432, 0x957457, 0x957464, 0x957470, 0x957692, 0x95769f, 0x956928 | Cache cover group; soldier selects outdoors for0/3/4,vertical1,horizontal2; civilian always outdoors.0x912460 handles switch. Unknown group leaves soldier child unchanged. | No status from selector. | Attached child pointers reused. | Switch deactivates previous child reason1. | Group mismatch can trigger selector during root interrupt. |
| O-LOCATION-SELECT; 0x9526b5, 0x95275a, 0x95276e | When no child, run separate0x9526D0 tail; choose idle/search/battle by catalog inputs, store previous child and switch. | No status. | Attached children reused. | 0x912460 deactivates previous child. | Body0x9526D0 is included independently because Ghidra assembly0x9526B0 contains only the gate. |
| O-LOCATION-VALIDITY; 0x949f1e, 0x949f21, 0x949180 | Outdoors AL true only group0/3/4; generic location always AL true. | Boolean AL, no broader RAX enum. | Borrowed cover/path. | Synchronous predicate. | No mutation. |
| O-LEAVE-IDLE-FLAG; 0x952737 | Call0x902F00(brain,1) when previous child idle and choice nonidle; sets flag11 unless user control. | No status. | Inline brain flag. | Later flag writer can clear it. | Setter compares requested byte to flag11; native name/writers Q5. |
| O-HAZARD-DETECT; 0x957397, 0x9573a4, 0x9573b1, 0x9573be, 0x9573cb | Update five embedded scanner results before generic root arbitration. Interfaces below give input/result/side-effect bounds. | No return consumed; active bytes/state are result channel. | Embedded scanners may retain detected entities; detailed retained handle contracts Q1g. | Next update replaces detection; per-type cleanup remains boundary contract. | Sequence crush,obstacle,grenade,burn,world. |
| O-EQUIPMENT-DEADLINE-POLICY; 0x9573f0, 0x9573fd | When nonzero deadline due call0x9440E0(brain,1); clear deadline only on false. Helper can submit UseItem via0x7CC810. | AL indicates policy gate, not task completion. | Temporary inventory query state released; UseItem boundary ownership C-HELPERS/Q1g. | True leaves deadline due, enabling retries on subsequent interrupts. | Per-variable helper census C-HELPERS; Q5 owns deadline producer meaning. |
| O-NAMED-GATE; 0x955aac, 0x955b43, 0x9560b0, 0x95615b, 0x9561ff, 0x956307, 0x956583, 0x956652, 0x9566dd, 0x95679e, 0x9567f0 | Q1a named-gate ordering: restore_order,burning,avoid_obstacle,avoid_burn,avoid_crush,avoid_grenade,retreat_out_of_world,restore_after_avoid,stand_from_water,cover_expired,interface. | AL false ends remaining root checks. | Borrowed image labels, pointer identity. | Flag22 resets next interrupt walk. | Priority is ordered, not independent Boolean voting. |
| O-PREPARE-INTERRUPT; 0x955bc7, 0x956558, 0x956688, 0x95671a, 0x956845, 0x95686b, 0x956b3f | Use Q1a preparation contract: conditional/unconditional flag21, end current order through slot13 if present, select root. | Conditional wrapper AL; callers branch on it. | Borrowed root/brain. | Can cancel live current order; saved order is detached first where required. | No order generation itself. |
| O-ACTION-LABEL; 0x955c19, 0x956106, 0x9561aa, 0x95625b, 0x95636d, 0x9565e9, 0x956780, 0x9568be, 0x956954, 0x956bf7, 0x956cc9 | Copy action text to state+0x58; nonempty text with third argument nonzero publishes qualified name via0x911FA0/0x9032F0. Root labels include burning_move,obstacle,burn,crush,grenade,retreat,stand_from_water,interface; restoration uses restore_cover/restore_position. | No result consumed. | Copies source string, caller releases temporary. | Empty label clears state label; does not notify through nonempty-only branch. | Detailed action notification contract OPEN Q1g. |
| O-RESTORE-OR-DISCARD-ORDER; 0x955aff, 0x955b04, 0x955b19, 0x955b30, 0x955b32, 0x95669e, 0x9566b3, 0x9566ca, 0x9566cc | If saved order and mode0, restore when both queues empty; otherwise End then deleting destructor, clear saved/mode. Pending user orders also discard before restore-after-avoid. | No success consumed. | Saved pointer transfers to current setter, or is deleted after End. | Queued user orders abandon saved restoration. | Setter avoided when saved already equals current. |
| O-SAVE-ORDER; 0x9562e3, 0x9562ed, 0x9562f4, 0x9563f1, 0x9563fb, 0x956402, 0x956f60, 0x956f6a, 0x956f71 | Only while restore-mode0 and saved null: copy current pointer to root+0x568, End(slot4), clear brain+0xF0 without destructor.0x956F20 additionally requires DL true. | No status. | Transfers live pointer into saved slot; End is not destruction. | Later restore/discard paths consume saved slot. | Allows new order setter to avoid deleting saved order. |
| O-SNAPSHOT-RESTORE; 0x955b04, 0x956459, 0x956472, 0x95647d, 0x956488, 0x956493, 0x95649e, 0x9564a9, 0x9564b7, 0x9564c5, 0x9564d4, 0x9564e6, 0x9564f9, 0x956504, 0x956525, 0x956531, 0x956539, 0x95654f, 0x95669e, 0x956fd3, 0x956fec, 0x956ff7, 0x957002, 0x95700d, 0x957018, 0x957023, 0x957031, 0x95703f, 0x95704e, 0x957069, 0x95707c, 0x957087, 0x9570a8, 0x9570b4, 0x9570bc, 0x9570d2 | Snapshot mode2 resident cover descriptor0x88 bytes, or mode1 positionXYZ/directionXYZ*10/actual type when hold or user;0x956F20 only if R8B true. Never overwrites existing mode/saved order. | No status. | Value copy; descriptor embedded references need Q7 lifetime contract. | Mode reset by queue cancellation or later event handling Q10. | Opaque descriptor copied, no geometry internals expanded. |
| O-SET-ORDER; 0x955aff, 0x95604d, 0x956574, 0x956745, 0x956b57 | Install restored, burning Move, hazard result, or stand-from-water MovementChange via brain slot13. | No setter result consumed. | Incoming order transfer; detailed ownership/cancellation Q1g/Q10. | Prior current order handled by native setter; snapshot can detach it first. | Type-specific result propagation remains output contract work. |
| O-HAZARD-BUILD; 0x9562a7, 0x9563b9, 0x956635, 0x956641 | Invoke selected scanner slot1; nonnull obstacle/burn/crush/grenade orders are installed. World slot1 deletes actor and returns null, so wrapper then does nothing. Nearby-burn normal entry returns null (preserved movement static evidence). | Pointer/null, not order execution result. | Non-null factory result consumed by setter; details Q1g. | Cancellation of installed escape order Q1g; no physical escape algorithm expanded. | Scanner may act before null result (world deletion); does not mean all null results are failures. |
| O-BURN-MODE; 0x955ccd | Set chassis movement mode2 with second extra argument0 via slot44. | No return consumed; requested/resolved distinction Q8. | Borrowed chassis. | Later mode request supersedes. | Foot setter recomputes movement; animation internals parked. |
| O-BURN-RNG; 0x955e51, 0x955e5a, 0x955e89, 0x955e92, 0x955ec8, 0x955ed1 | Three LCG draws per attempt. Candidate radius=L+(5L-L)*u2; angle=atan2(Y,X)+u1²*pi*(2*u3-1); up to10 attempts. | Derived candidateXY; normalized u-domain OPEN until scale binding. | Borrowed selected stream. | No rollback of failed candidate draws. | Consumes shared RNG state/count; sin/cos are pure numeric helpers. |
| O-BURN-PATH-QUERY; 0x955ce2, 0x955dc7, 0x955dd5, 0x955f56, 0x955f62, 0x955f90 | Construct owner pather context with flag2, temporary line/route; cost query candidate; first cost<100000 materializes route. | Cost float, route validity byte and point vector; only valid>=2 points passes Move factory. | Temporary context owns shared query handles; released on exit. | Temporary query state destroyed on all normal paths. | Hypothetical query can touch query caches; no actor movement until installed Move. Spatial/path internals parked. |
| O-BURN-MOVE; 0x956031 | 0x8F34F0(route,1) builds Move only with route-valid byte and>=2 points. | Order pointer; subsequent Move returns running0/success1/failure2 per movement map. | Route-copy/transfer exact factory contract Q1g. | Current order cancellation through setter Q10. | No candidate success after10 tries leaves no Move installed; movement type/mode request already occurred. |
| O-COVER-CHECK; 0x956831, 0x956837 | Advance cover deadline by convert1; run mutating cover Update. | Result2 drives conditional interrupt and Hide; other codes Q7. | Borrowed cover, temporary query objects released. | Next due check recomputes. | Invalid cover can be released before Hide. |
| O-HIDE-POLICY; 0x95685c | 0x950B60(root,5,false) after expired-cover result2; attempts cover movement within5*L, can release old cover and build order sequence. | AL policy success; root ignores result. | Temporary candidates released; chosen output order owned through sequence. | Native sequence cancellation Q1g/Q10. | Decision helper per-field reads and scoring queries assigned C-BATTLE, not treated as an opaque physics producer. |
| O-CREATE-SEQUENCE; 0x956b8f | Use Q1a0x912370 installed complex sequence for restore commands. | Sequence pointer or null; caller here does not test it. | Installed sequence returned borrowed. | Replacement/cancel Q10. | Allocation failure behavior remains Q10. |
| O-MOVEMENT-CHANGE; 0x95672d, 0x956c43, 0x956d1d, 0x956e51 | Factory0x8F3930(type,mode,wait): standing0/mode-1/wait1 for water;0/2/1 before restoration; savedType/-1/1 after Rotate. | Exec running0/success1/failure2; -1 argument means unchanged. | Factory result transferred to setter/sequence. | Order cancellation contract Q1g/Q10. | Requested/actual type comparison; enums Q8. |
| O-MOVE-TO-COVER; 0x956c60 | Factory0x8CD320(saved descriptor) appended after movement change. | Output order; result/acceptance contract Q1g. | Saved descriptor lifetime/copy Q7; order owned by sequence. | Sequence cancellation Q10. | Physical traversal internals parked. |
| O-MOVE-TO-POSITION; 0x956d3c | Factory0x8F3600(&savedXYZ,0) appended after movement change. | Output order; result propagation Q1g/Q10. | Factory coordinate copying boundary Q1g; sequence owns order. | Sequence cancellation Q10. | Path internals parked. |
| O-ROTATE; 0x956e14 | Factory0x8F3880(temporary Target(savedPosition+savedDirection),0.0) appended. | Output order; completion/failure Q10. | Temporary target destroyed after append; order target copy/retention Q10. | Sequence cancellation Q10. | Facing implementation parked. |
| O-APPEND-SEQUENCE; 0x956c54, 0x956c71, 0x956d2e, 0x956d4d, 0x956e25, 0x956e62, 0x956ea3 | 0x900A60(sequence,child,0,1), including tail JMP for final event order. | No return consumed; sequencing/failure flags Q10. | Child transfers to complex sequence; contract Q10. | Parent cancellation Q10. | Explicit order of movement change, restore movement/cover, optional Rotate/type, event0x10. |
| O-STATE-EVENT16-ORDER; 0x956e7a | 0x9125D0(root,0x10,1) factory appended at end of restoration. | Execution triggers state event0x10; result/cancellation contract Q10. | Order keeps state reference; exact ownership Q10. | OPEN Q10: whether sequence cancellation/failure skips or still issues the terminal event. | Exact event0x10 handler effects remain Q10. |
| O-RESTORE-TARGET; 0x956da5, 0x956db8, 0x956dc5, 0x956dce, 0x956e07, 0x956e34, 0x956e3e | Construct temporary eTarget, set saved look point via0x83E430, destroy after Rotate append. | No tactical result from ctor/dtor. | Caller owns temporary target and auxiliary handles. | Synchronous destruction after use. | Only factory-retained target validity remains O-ROTATE. |

Game-only reproduction uses call_to_arms.decision-catalog-q1b-pass.tsv, focused export into call_to_arms-decision-catalog-q1b, and verify_fine_map.py with the Q1b manifest. Neither executable was launched or modified. No new verifier was added.

## Q1z — effective prototype catalog

This is the active scope overlay; all earlier records are retained. The remainder
of C-ROOT and the full C-IDLE/C-BATTLE/C-SEARCH/C-HELPERS/C-OUTPUTS census are
deferred. Older cells naming Q1g do not authorize reopening them. Only the
prototype rows below and native accesses to those same fields are active.
Q2–Q5 follow; Q6–Q11 require a later prototype stage. This pass consolidates
existing evidence: no new disassembly, manifest, validation file or verifier.

Existing rows retained in the bounded set: I-THIS/I-OWNER/I-CHASSIS/I-COVER/
I-SENSOR; I-ACTOR-XY/I-ACTOR-Z; I-COVER-GROUP/I-COVER-DESCRIPTOR/I-COVER-RESIDENT;
I-S/I-RECOVERY-DEADLINE/I-A/I-B; I-BEHAVIOR/I-BEHAVIOR-DEADLINE;
I-CONTROL/I-MOVE-MODE/I-NO-ADVANCE/I-NO-RETREAT/I-ADVANCE-RATIO/I-RETREAT-RATIO;
the individual brain flags; subject/relationship rows below; and
I-NOW/I-DURATION-FACTOR/I-SPATIAL-SCALE. Native RNG rows remain evidence,
not an obligation to reproduce every native random consumer.

### Validity and freshness at the observation boundary

**P-call:** borrow the brain and its owner/components during the reached native
brain callback; copy scalar observations and reacquire components next time.
The native contract requires a live actor with an attached brain and appropriate
concrete components. Constructor-installed shared defaults alone do not establish
that precondition. Q2 resolves concrete brain selection/attachment. A future
runtime adapter must establish its callback/type/ABI and live-pointer admission
conditions; these static RVAs are not a tested callable SDK.

**P-subject:** the brain retains target references at subject+8, but a retained
subject can lose its actor/entity binding. Detach0x87A4D0 unregisters listeners
and nulls+0x18/+0x20; removal0x8754F0 marks/removes/releases. Position-only
observations legitimately have no actor. Copy remembered position separately;
check removal and null binding before dereferencing an actor/entity. Do not
retain a vector iterator across a native reset/mutation call. This is not a
thread-safety guarantee for an external worker.

Evidence: [perception](perception-map-25492057.md#information-sharing-and-lifecycle),
[ownership](ownership-map-25492057.md#what-reset-actually-clears) and
[ownership manifest](../artifacts/build-25492057/ownership-map.json), evidence
game_87a4d0/game_8754f0/game_946d40. Target refresh stores/retains at
0x9435C1/0x9435CD and0x94365F/0x94366B; release at zero uses subject+0xC
disposal flags. P-call/P-subject describe borrow/precondition limits, not
ownership transferred to a prototype.

F-sensor/F-score above remain authoritative. Sensor commit precedes reached
brain dispatch; A/B refresh only on a due, unskipped job. A retained observation
is not necessarily freshly visible. Actual callback cadence, mission resources,
values and branch outcomes require runtime observation; the conditional static
ordering is already established.

### Missing rows and refinements

| Row | Read site | Access path / type / width | Meaning / domain | Freshness / native writers | Validity |
| --- | --- | --- | --- | --- | --- |
| I-STANCE (same storage as I-MOVEMENT-TYPE) | 0x943076;0x956702 | brain→owner(+0x38)→chassis(+0xDE0)→+0x2C, int32 | Actual type:0 stand,1 crawl,2 squat,3 swim. Vision's crawl predicate groups1/3 and is a different domain. | Current actual type; native movement requests/Fsm application can change it during mission. Actual need not equal requested. | P-call; concrete chassis. |
| I-REQUESTED-STANCE | MovementChange0x8F7330 comparisons | chassis+0x30, int32 | Requested type; factory argument-1 means unchanged, not a stored stance. | SetMovementType0x848710 writes request and may reject; actual+0x2C changes through transition. Complete output enums deferred Q8. | P-call. |
| I-HEALTH-RATIO | chain0x951873/0x95187A; calls0x95188B/0x951896; divide0x9518A3 | owner→aggregate(+0x578)→vitality(+0x18); float32 result0x925010(v)/0x924F90(v) | Effective current/max health, not raw+0x12C/+0x128 division. Full health ordinarily≈1; no universal[0,1] clamp. Max accessor substitutes nonzero fallback below epsilon. | On demand via0x9291C0: actor parameters, Health ability, veterancy. Damage/heal setter0x925190 and update0x9263B0 change health; modifiers affect effective values. Stop at accessor contract. | Explicit owner/vitality null checks0x95186E/0x95187E; no ratio when absent. P-call. |
| I-SELECTED-TARGET | 0x941D25;0x95270F | brain+0x3B8, subject*,8 bytes | Native selected subject; selection does not establish a fireable target. | Refresh0x943560 called0x941D19 before selection; picker result store0x94365F, retain0x94366B. Reset0x9424F0/0x944070 can clear it. | Brain-held reference, nullable; P-subject. |
| I-PREFERRED-TARGET | 0x9437D4/0x943AA2 | brain+0x3D0, subject*,8 bytes | Preferred/explicit subject; distinct from selected+0x3B8. | Query0x903050 at0x943578; store0x9435C1/retain0x9435CD. Sources and query-return ownership deferred Q6. Reset does not necessarily clear it. | Brain-held reference; P-subject. |
| I-PREVIOUS-TARGET | 0x941D1E/0x941D25 | brain+0x3B0, subject*,8 bytes | Temporary previous-selected snapshot for change detection. | Reference assignment0x943607; caller releases and clears0x941D7F. | Retained across refresh/change handling only; P-subject. |
| I-MOVE-SPEED | 0x9031C0; fine/squad consumers | brain+0x398 bit14 | Named move_speed:fast0/slow1. | Setter store0x9031E3; actor_state call0x99FC03. Synchronous command/property update, mission-mutable. | Inline P-call. |
| I-FIRE-MODE | outdoors interrupt0x949AA0; attack-control policy | brain+0x398 bits10..11 | Named open0/hold1/return2; pattern3 unnamed. | Property/actor_state writes, store0x99F9C0. Full consumer/side-effect closure deferred Q8 at fire-control stage. | Inline P-call; preserve other bits. |
| I-SUBJECT-SET | 0x903BAC/0x903BB3; picker0x9437A0; score0x877170 | sensor+0x268+category·0x18, four begin/end/capacity triples of8-byte subject pointers | Category2 apparent allies;1 unknown/enemy;3 nonactor entities;0 remaining known actors. Collection interface, not reopened full native census. | Insert/recategorize0x8753C0, commit0x878AE0, reset/removal; F-sensor. | Borrow iteration, no iterator across mutations; P-subject. |
| I-SUBJECT-ENTITY | insertion0x8753C0; target/sensor consumers | subject+0x20, entity*,8 bytes | Observed entity or null; actor+0x18 is a separate optional view. | Insertion/load binds; detach0x87A4D0 nulls both. | P-subject; subject retain does not preserve binding. |
| I-SUBJECT-POSITION (refined) | 0x876F83;0x903D6B; target policy | subject+0x28..+0x30,float32 XYZ | Last-known position in engine spatial units; can exist without live entity. | Visible commit stores0x878F27/0x878F30, time0x878F39; signal/shared-info/load also write. | P-subject; copy value and heed known-position flag. |
| I-SUBJECT-UNCERTAINTY | eviction0x878320; Recon/search consumers | subject+0x34,float32 | Position uncertainty/radius; not a probability. | Visible refresh writes0 at0x878F43; signal/share/load may change. | P-subject. |
| I-SUBJECT-ATTACK-HUMAN | 0x877029 | subject+0x48,float32 | Human channel of AttackRate coefficient, not measured DPS. | Refresh0x878740, pair store0x8787CE or lookup0x8787D9; ally/self uses cached component-wise maxima. | Cached P-subject. |
| I-SUBJECT-ATTACK-TANK | 0x877036 | subject+0x4C,float32 | Tank AttackRate channel; maximum may come from a different weapon. | Same pair refresh. | P-subject. |
| I-SUBJECT-RANGE | 0x876F92 | subject+0x50,float32 | Remembered equipment range/scaled fallback; denominator of distance weight. No universal positive bound proved. | Refresh store0x8787AF. | P-subject. |
| I-SUBJECT-HP-HUMAN | score0x877170; refresh0x8787E5 | subject+0x54,float32 | Human HitPoints comparison channel; human special case uses Health parameter. | Refresh0x878740→0x8136E0; mission-mutable cache. | P-subject. |
| I-SUBJECT-HP-TANK | same pair consumers/refresh | subject+0x58,float32 | Tank HitPoints channel; human special case0. | Same refresh. | P-subject. |
| I-SUBJECT-WEAPON | eligibility0x912AE0; term0x91A0B0 | subject+0x40, remembered equipment*,8 bytes | Optional observed weapon identity. | Equipment refresh, signals/shared-info/load. Item lifetime details deferred Q9 at target stage. | Nullable; do not retain/dereference raw item beyond native lifetime. |
| I-SUBJECT-EQUIPMENT-TIME | eligibility0x912AE0(age<3) | subject+0x5C,uint32 | Equipment/combat observation counter. | Equipment refresh, selected signals, load. | Inline P-subject; simulation time. |
| I-SUBJECT-VISIBILITY-TIME | 0x878FE8 | subject+0x60,uint32 | Last visibility transition counter. | Transition store0x878E50. | Inline P-subject. |
| I-SUBJECT-OBSERVATION-TIME | 0x912AEF;0x876F60 | subject+0x64,uint32 | Last position/observation update; score weighting decays over60 units. | Visible store0x878F39; signals/share/load can replace. | Inline P-subject. |
| I-SUBJECT-SIGNAL-TIME | 0x87921B | subject+0x68,uint32 | Threat/injury transient-flag timestamp. | Signal0x877D20; commit expires injury>0.4,attacking>1,threatening>2 while threatening set. | Inline P-subject. |
| I-SUBJECT-ELIGIBILITY-DEADLINE | 0x912B2C/0x912B36 | subject+0x70,uint32 | Eligibility/recognition gate: nonzero and<=now. Exact name/writers deferred Q9 at target stage. | Cached recognition/signal deadline; unresolved static producer contract, not runtime-only. | Inline P-subject. |
| I-SUBJECT-ACCUMULATOR | 0x87909E/0x8790D0 | subject+0x38,float32 | Threshold100 accumulator, contribution0x891C00, decay3·elapsed. Semantic name deferred Q9 perception stage. | Commit updates/decays. No prototype rule inferred from unknown meaning. | Inline P-subject. |
| I-SUBJECT-RANK | ranker0x9136E0 | subject+0x11,uint8(overlaps observation bits8..15) | Descending rank byte for current candidate list, not global priority. | Mutated by each ranking query, including hypothetical ranking on the same subjects. | P-subject; ranking is not read-only. |
| I-RELATIONSHIP | classifier0x878560→0xA467B0;0xA46830 | observer actor/player,target actor,ignoreMask→int | 0 neutral,1 enemy,2 ally. Same actual player→2; mask equality→2 unless ignored; missing/non-Operatable/invalid actual IDs→0; otherwise actual-player matrix. | Current Player/MaskPlayer/Operatable/diplomacy. Player0x7EEEC0, mask setter, pair0xA46B00/load0xA46ED0 can change in mission. Subject cache refreshes later. | Live borrowed actors, scalar result. Matrix values for a mission require runtime observation. |
| I-APPARENT-ALLY | classifier0x878560;score0x877170 | subject+0x14 bit2 | Relationship with stored ignoreMask returns2; category2 input. | Relation refresh/events/load. | P-subject. |
| I-ACTUAL-ALLY | classifier0x878560;inform0x879C50 | subject+0x14 bit3 | Relationship with ignoreMask=true returns2. | Refresh; ownership event can clear before reclassification. | P-subject. |
| I-IGNORE-MASK | classifier0x878560 | subject+0x14 bit5 | Learned unmasking selector. | Recognition/shared information/load. | P-subject. |
| I-KNOWN-COMBAT | eligibility0x912AE0;cover score0x8A0ED0 | subject+0x14 bit10 | Known combat evidence; unlike7/8/9, not expired by their timer branch. | Threat/injury0x877D20, shared information/load. | P-subject. |
| I-SUBJECT-KNOWN-POSITION | 0x912BEC | subject+0x10 bit28 | Remembered position exists, independent of visible29. | Visible/position-only/signal/load writers. | P-subject. |
| I-SUBJECT-REMOVED | removal0x8754F0 | subject+0x10 bit16 | Marked for removal; retained object can outlive binding. | Detach/removal before final reference disposal. | Retained storage is not eligibility. |
| I-SUBJECT-CATEGORY | 0x8753C0;0x878AE0 | subject+0x10 bits21..22 | Category0..3, not rank byte. | Insert/recategorize/load. | P-subject; can change with vector membership. |
| I-SUBJECT-RECON-REGISTERED | commit/search;handle0x95274A | subject+0x10 bit25 | Auxiliary Recon registration exists. | Register/unregister/detach/load. | P-subject; handle ends on unregister. |
| I-SUBJECT-RECON-WAITING | 0x87918A;search0x94C8A0 | subject+0x10 bit26 | Hidden/stale waiting contribution; age path requires searched27 clear. Not zone completion byte. | Commit/search/load. | P-subject; full search interface deferred. |
| I-TARGET-RADIUS | 0x913736;Advance0x8D1C50/0x8D2120 | brain+0x14C,float32 | Target-ranking/engagement radius; binding/writers Q5. | Current field; Q5. | P-call. |
| I-ADVANCE-TIMESTAMP | 0x8D25FF/0x8D2682 | brain+0x18,uint32 | Age compared with5 units by Advance; name/writers Q5. Do not infer last-shot label. | Q5. | P-call. |

Retain individual earlier rows for subject known0/enemy1/unusable4/
threatening7/attacking8/injury9, inform-pending(+0x10 bit17), visible29,
searched27, suppress+0x6C and Recon handle+0x78. Targetable, Operatable and
linked dependencies retain J3/J8/J4; linked-type/environment gaps remain Q9.
Previous/propagated visibility bits30/31 are not additional live-visible values.
Full serializer/flag census is deferred.

These rows cite [human-states](human-states-map-25492057.md#healing-and-readable-actor-state)
and [manifest](../artifacts/build-25492057/human-states-map.json)
(game_951860/game_924f90/game_925010/game_942fc0);
[movement](movement-map-25492057.md#movement-orders-and-foot-chassis) and
[manifest](../artifacts/build-25492057/movement-map.json);
[fine target bookkeeping](fine-map-25492057.md#target-selection-and-engine-bookkeeping)
and [manifest](../artifacts/build-25492057/fine-map.json)(target_refresh_game);
[combat](combat-map-25492057.md#target-eligibility-and-ordering) and
[manifest](../artifacts/build-25492057/combat-map.json);
[perception](perception-map-25492057.md#persistent-subject-fields-and-memory) and
[manifest](../artifacts/build-25492057/perception-map.json)(observation_commit);
[tactics](tactics-map-25492057.md#the-score-pair-is-human--tank) and
[manifest](../artifacts/build-25492057/tactics-map.json);
[flow manifest](../artifacts/build-25492057/flow-map.json)(subject_weight);
and the ownership evidence above. Instruction RVAs are from retained exports.

### Existing-row closures for this prototype stage

| Rows | Native contract / closure | Remaining blank |
| --- | --- | --- |
| I-ACTOR-XY/I-ACTOR-Z | Current owner+0x74/+0x7C, read0x903E07/0x9564F4/0x956501. Motion/teleport can change during mission; copy at callback time. | Actual values and adapter admission: runtime. No motion internals. |
| I-COVER-GROUP/I-COVER-DESCRIPTOR/I-COVER-RESIDENT | Descriptor0x88 bytes at cover+0x20, group at descriptor+4. Enter0x898AD0 copies/claims and emits event0x21; exit0x898C50 releases/resets and emits0x21. MoveToCover success installs; cancellation releases temporary claim. Mission-mutable on native completion or synchronous invalidation. Group1 vertical,2 horizontal,3 bush,4 terrain; kind table0..12 in perception. | Descriptor+8/+0xC, direction masks and embedded-handle validity Q7 at cover stage. Until then observe an opaque copied descriptor, do not manufacture/edit one. |
| I-S/I-RECOVERY-DEADLINE | Morale-associated float0..100; clamp/delta/recovery ordering known. | Events/deltas/recipient/phase Q4. |
| I-A/I-B | Cached Human/Tank relative pressure/capacity; F-score conditional refresh. | Concrete implementation Q2; actual resource coefficients/values runtime. |
| I-BEHAVIOR/I-BEHAVIOR-DEADLINE | Functional0..4; absolute counter deadline,0 off. | Competing writers/load/factory Q2. |
| I-NO-ADVANCE/I-NO-RETREAT/I-ADVANCE-RATIO/I-RETREAT-RATIO | Constructor/property0x9426E0/actor_state0x99F940; stores0x99FE3B/0x99FE24/0x99FE72/0x99FEAB. Synchronous when property/command applies, reread at selection, mission-mutable. Ratios default2; Boolean nonzero; no full accepted numeric bounds established. | Scheduling relative to a particular external hook is runtime integration; Q2/Q5 cover competing native writers where required. |
| I-NOW/I-DURATION-FACTOR/I-SPATIAL-SCALE | Simulation counter/conversion, ordinary1000 ticks/unit; pause/step/reset in clock map. Distances use L(file-initial20), not raw meters. | Actual resets/scales/mods/cadence runtime; no clock-internal expansion. |
| I-SENSOR-INFORM-DISPATCH/O-SENSOR-INFORM | Q1a gap closed by perception: slot14=0x873CB0 changes mode+0x1A, reschedules skip tasks0x875580, argument0 sets now+convert3 at+0x358; expiry restores mode. Stores0x873CC1/0x873CF6/0x873D07. | Public mode name is deferred native-only metadata; prototype uses established freshness rule. |

Cover mutability cites perception manifest cover_enter at0x898ADD/0x898AF1/
0x898B0D and cover_exit at0x898C59/0x898C6A/0x898C88. These are the first
producers sufficient to close "can it change", not an invitation to map cover
execution internals.

Brain flags for native coexistence remain individual rows. Earlier rows cover
0..5/20(dispatch inhibitors),6(task),9(follow),13..18(aggregates),21(interrupt),
22(label match). The following additions are all one bit of uint32 brain+0x39C,
P-call, current mission-mutable flags. Functional labels remain inferred.

| Row | Bit | Native site / meaning | Freshness blank |
| --- | --- | --- | --- |
| I-BFLAG-7 | 7 | Squad goal marker; direct-goal clears at0x902FB0, squad goal policy in squad map | Q3 lifecycle; Q5 writer/name reconciliation |
| I-BFLAG-8 | 8 | New task marker, direct-goal0x902FB0 sets | Q3/Q5 |
| I-BFLAG-10 | 10 | Following activation marker,0x902B20 sets | Q3/Q5 |
| I-BFLAG-11 | 11 | Leave-idle/task policy,0x902F00 setter and direct-goal clear | Q3/Q5 |
| I-BFLAG-25 | 25 | Reconsider request,0x903FBB and direct-goal/follow paths set | Q3/Q5 |
| I-BFLAG-27 | 27 | Named follow_commander, actor_state0x99FED7 | Property/command mutable; Q5 existing writer consolidation |
| I-BFLAG-28 | 28 | Squad/user task gate; ownership/squad reset clears | Q3/Q5 |
| I-BFLAG-29 | 29 | Task-related marker, direct-goal clears | Q3/Q5 |

Read sites and native writes are retained in Q1/Q1a/Q1b, the
[squad map](squad-map-25492057.md), [squad manifest](../artifacts/build-25492057/squad-map.json)
and fine command evidence. Q5 must finish exact writers/names for this bounded
set. Other bits are preserved on writes; their complete census stays parked.

### Prototype outputs

O-BEHAVIOR/O-BEHAVIOR-DEADLINE remain Q2; O-DIRECT-GOAL remains Q3.

| Row / boundary | Effect | Result codes | Ownership | Cancellation | Side effects / remaining blank |
| --- | --- | --- | --- | --- | --- |
| O-MOVE-MODE,0x903140;stores0x903170/0x903184 | Set bits12..13; zone2 copies ownerXY to+0x128; changed mode deactivates root child via0x912520 | No status; compares unmasked argument; named0..2,3 unnamed | Scalar/XY copies, attached state remains owned | Immediate mode change; events4/3 select parent. Direct-goal consequences Q3 | Use setter to reproduce transition effects |
| O-MOVE-SPEED,0x9031C0;store0x9031E3 | Replace bit14 with masked value if extracted old differs from unmasked input | No status;fast0/slow1 | Inline scalar | Later write supersedes | Leaf has no calls; other bits preserved |
| O-FIRE-MODE,store0x99F9C0 | Actor_state replaces bits10..11 unless command=-1 unchanged | No setter status;open0/hold1/return2 | Inline scalar | Running fire-order cancellation not proved by store | Mask/store branch has no call; complete write-equivalence/consumer semantics deferred Q8 |
| O-NO-ADVANCE,0x99FE3B | Store+0x520 byte; normal nonzero-state selection gate | No status | Inline byte | Later property/command write supersedes | Does not block state0 timed branch or cancel existing movement |
| O-NO-RETREAT,0x99FE24 | Store+0x521 byte; normal retreat override gate | No status | Inline byte | Later write supersedes | No immediate order cancellation/state0 recovery change |
| O-ADVANCE-RATIO,0x99FE72 | Store float+0x524 for next threshold selection | No status/range check in store | Inline float | Later write supersedes | No immediate state/order transition |
| O-RETREAT-RATIO,0x99FEAB | Store float+0x528 for retreat/state0 recovery comparison | No status/range check in store | Inline float | Later write supersedes | No immediate state/order transition |
| O-PREFERRED-SUBJECT-QUERY,0x903050/call0x943578 | Obtain preferred subject for+0x3D0; later-stage interface | Pointer/null; source precedence Q6 | Refresh retains at0x9435CD; return ownership Q6 | Old reference released on change; reset need not clear slot | Raw target-slot assignment does not reproduce target-change/Recon bookkeeping |

Bindings/stores cite fine manifest human_property_binding/command_human_fields/
move_mode_setter, Q1 direct-goal evidence and Q1a deactivation evidence.
The four policy byte/float stores are synchronous, with no asynchronous
completion to poll.

### Remaining work under the shortened plan

| Status | Rows / blank |
| --- | --- |
| Q1z closed (bounded inventory/consolidation) | Stance, effective health, distinct target references, subject snapshot, relationship and cover mutability now explicit. No claim of complete native census. |
| Q2 active | I-THIS/I-OWNER factory/attachment, slot23/35; I-A/I-B implementation; I-BEHAVIOR/I-BEHAVIOR-DEADLINE writers/load/phase. |
| Q3 active | Direct-goal movement/arrival/failure/cancellation in idle/battle/cover, task flags and move-mode interaction. |
| Q4 active | I-S/I-RECOVERY-DEADLINE/O-MORALE-DELTA events/deltas/recipients/phase. |
| Q5 active | I-TARGET-RADIUS(+0x14C), I-ADVANCE-TIMESTAMP(+0x18), I-EQUIPMENT-DEADLINE(+0x3E0), listed brain flags: binding or explicit writers. |
| Deferred target/perception stage | Q6 preferred sources/return ownership; Q9 linked type/environment factor, remembered equipment lifetime, eligibility-deadline/unnamed accumulator semantics. These remain static blanks. |
| Deferred cover/fire/stance stage | Q7 descriptor roles/masks/handle validity; Q8 full fire-mode effects/movement-output enums. |
| Deferred richer outputs | Q10 sequences/events/secondary orders; Q11 native go-to commands. |
| Runtime integration | Callback/type/ABI/live-pointer admission; actual mission/mod values, cadence, executed branches and accepted movement outcomes. Not substitutes for active Q2–Q5 static work. |

No physical, animation, pathfinder or unrelated producer computation was reopened.

## Q2 — behavior writers and concrete brain attachment

This closes the Q2 freshness/type blanks in the Q1z overlay. Evidence:
[Q2 map](../artifacts/build-25492057/decision-q2-map.json),
[validation](../artifacts/build-25492057/decision-q2-validation.json), and
[literal-member leads](../artifacts/build-25492057/call_to_arms.decision-q2-member-leads.json).
The pass does new game-only disassembly. The existing fine-map checker validates
35 entries, 2,878 instruction locations, seven RTTI tables and five byte ranges;
those checks establish locations/data, not completeness of arbitrary aliases.

| Row / writer | Trigger and phase | Effect / instruction evidence |
|---|---|---|
| I-BEHAVIOR / I-BEHAVIOR-DEADLINE; constructor0x9417E0 | Creation through human or human_client factory, including clone construction | Qword store0x941958 sets int32 state2 and uint32 deadline0 together. Both begin before attachment/first decision. |
| Same; inline selector0x941D00 | Actor slot35, after target snapshot/refresh and before base reconsideration | Nonzero deadline: unsigned now>=deadline clears both fields at0x941E07, then leaves selection; before expiry preserves both. A zero deadline enables the normal branches below. |
| Same; zero-state recovery | Within the same decision, state0 and B·Rr>=A | Calls morale slot29 with30, then writes state2 at0x941E3D. Otherwise S>50 writes state3 at0x941EA9 and now+convert(1+sample) at0x941EB3. No disable-byte check in this branch. |
| Same; ordinary selection | Same decision, prior state nonzero and deadline0 | State2 at0x941ECD; advance3/4 at0x941EFB; retreat override1 at0x941F36 or0 at0x941F42. Predicates/order remain the Q1/J6 and fine-map contract. These stores do not refresh the deadline. |
| Same; outlined0x944E10 | Callable duplicate of selection, but no executable caller or AI vtable slot found | Corresponding stores0x944E8B,0x944EC4,0x944F43/49,0x944F66,0x944F9B,0x944FD5,0x944FEA. Keep as a possible entry; do not assert it runs between decisions. Its three Ghidra references are .pdata/chained-unwind data:0x1640428,0x13B1AB8,0x13B1AC8. |

The displacement scan covered 3,851,164 existing instructions and returned202
leads for0x52C/0x530 and adjusted-subobject offsets0x51C/0x520. Reviewed
non-brain write/exposure classes are:

- 0xCE4D8 writes eCampaignOptions (constructor0xCE2F0 installs RTTI-backed
  vtable0x1004898); 0xB03229/33/59 belong to eManualControl
  (constructor0xB03060, vtable0x1111560). The float4 at0xB03229 is not behavior4.
- 0x27DD00 belongs to GUI2::eGUI_PageStoreItem;0x3C2D5A addresses storage in
  GUI2::eSoldierLibrary (vtable0x1063C18 slot11).0xBC217F/0xBC469E belong to
  eViewEntityDialog (vtable0x112DAB8, slot2 and constructor).
- 0x847573 and the0x84A584/0x84EAE7/0x84EBBD family address chassis-foot
  storage;0x96F42C follows an explicit eChassis→eChassisFoot cast. They do not
  take a brain receiver.0x9564A9/0x957023 write the soldier-root restoration
  snapshot, not root+0x30's brain (Q1b restoration evidence).
- RSP-relative hits are stack storage.0x6685CD and0x95399F/0x953D1A/0x953D3C
  are also stack storage: their prologues establish RBP from RSP/RAX, rather
  than from the brain receiver (0x668188,0x9538B2). The tactical function's
  actual behavior reads follow state+0x30.

Copy and serialization are separate checks, not inferred from the literal scan.
Human slot10 at0xA7430 calls base copy0x909150 and human policy copy0x9451F0;
they copy configuration, including no_advance/no_retreat and ratios, but neither
behavior nor deadline. Human/client slot9 constructs a new instance first.
The shared slot7 load dispatcher0x9092A0→0x909380 and slot5 save
0x909730→0x9097B0 use the secondary DDA visitor0x9426E0 (this=brain+0x10)
and explicit base tokens. Neither those token cases nor the human property
visitor includes behavior/deadline or A/B. A freshly loaded/cloned instance
therefore starts with the constructor's transient state; a load into an existing
instance has no reviewed token that overwrites those fields. There is no newly
identified asynchronous writer to the pair.

| Row | Concrete type / phase answer |
|---|---|
| I-THIS / I-OWNER | Actor load token0x5B at0x7F5B7B..0x7F5BA5 uses the requested brain name through0xC19A0 and stores actor+0xDC8. Registry lookup0x903400 is case-insensitive. Table0x1482D88 binds human→0x945D30→0x9417E0; table0x146E050 binds human_client→0xA7AC0, which then replaces both vptrs and selects root name state.client. Clone0x7F56B8..0x7F56E2 stores the new brain, writes brain+0x38=owner, and invokes owner-changed slot14. Component destruction/borrowed within-call validity remains the entity-health/Q1z contract. |
| I-THIS / I-A / I-B |0xC19A0 tries name+_client only when0xC0B630 returns true, and falls back to the ordinary name if that lookup fails.0xC0B630 requires an active Multiplayer::eGameClient service from0xC65C20, multiplayer core0x15A4A80, mode2 (or mode4 with no -no_server command-line substring), and a false session slot25 result. Other modes, absent core/service, and a true slot25 select the ordinary factory.0xC0B190 names the core startup Multiplayer::Start(); this is a multiplayer gate, not a brain-score flag. |
| I-A / I-B / I-MORALE-DISPATCH | RTTI tables human0x10EC7C8 and client0xFFFEE8 both resolve slot35→0x941D00 and slot29→0x945010. Human slot23→0x941BC0 queues0x943C70/0x943EA0 and, only when due/unskipped,0x944DE0 for A/B. Client slot23→0xA7720 is empty. Thus ordinary human instances use the conditional same-quant refresh from J7/Q1; the client must not be assigned that freshness promise. |

**Static closure and runtime boundary:** for normal human-name construction
without the eligible multiplayer-client gate, single-player infantry get the
human implementation; the exact gate and both type outcomes are now mapped.
The actual configured name, loaded vptr, scheduling flags and intervening calls
in a particular mission require observation of that mission. No game was run.
The scan is not a proof against arbitrary pointer arithmetic, unrecognized code,
mods or an external writer. A prototype must check the live type/attachment and
choose its write phase: a state-only write with deadline0 is recomputed by the
next selector, while a nonzero deadline suppresses selection until expiry, when
both fields are zeroed. Static analysis does not establish a safe concurrent
injection point or make the two separate prototype stores atomic.

## Q3 — direct-goal output contract

**O-DIRECT-GOAL is closed to the prototype boundary.** It is a replaceable
movement request, not an order handle or a promise of arrival. The decision
join below uses preserved [human-states](human-states-map-25492057.md) /
[manifest](../artifacts/build-25492057/human-states-map.json),
[squad](squad-map-25492057.md) /
[manifest](../artifacts/build-25492057/squad-map.json),
[movement](movement-map-25492057.md) /
[manifest](../artifacts/build-25492057/movement-map.json), and
[flow](flow-map-25492057.md) /
[manifest](../artifacts/build-25492057/flow-map.json).
New disassembly fills the order-ownership, point-factory and leaf-result blanks:
[Q3 map](../artifacts/build-25492057/decision-q3-map.json),
[validation](../artifacts/build-25492057/decision-q3-validation.json)
(five entries,598 instruction locations, two floats, one RTTI table).

| Contract part | Observable behavior and evidence |
|---|---|
| Call and ownership |0x902FB0(RCX=live attached brain,RDX=readable XY float32[2]) copies eight bytes at0x903026/2A. It does not retain RDX. The caller must keep the argument valid through the preceding order-End callback; passing an owned independent coordinate value avoids an alias invalidated by cancellation. No explicit result, goal ID or completion callback is returned. |
| Immediate effects |If brain+0xF0 is nonnull, call slot13(null) at0x902FCF. Clear +0x39C bits7/9/11/29, set6/8/25; copy goal to+0x130; reset +0x140/+0x144/+0x148 from writable globals0x146E000/4/8. Bit6=request pending,8=new task,25=reconsider. No leaf allocation or immediate chassis movement in this setter. |
| Current-order replacement |Both human types use0x9052F0. Outside order-update flag24, it calls old order slot4 End at0x90532B and deleting slot0 at0x905342. Inside that callback phase it instead marks old order+0x18 bit22 at0x90531F for deferred cleanup. In both cases it clears brain+0xF0 and+0x108; a null replacement sets bit12. The setter owns installed orders; no retained order pointer is returned to a goal caller. Existing slot24 evidence0x905710 describes deferred cleanup after the callback. |
| Cancellation / replacement |Another0x902FB0 cancels the current order and replaces XY.0x902F50 clears6/7/9/11/29 and the look-point defaults, preserving stored goal/follower XY. It does **not** itself call End or clear+0xF0: clearing the request alone is not an immediate stop of an already installed movement order. Ending that order uses slot13(null). Squad updates and saved-policy restoration can replace requests later; the request has no exclusive ownership lease. |

| Decision context | How the goal is consumed, ended or deferred |
|---|---|
| Outdoors idle0x94EF90 |Bit6 is checked before other idle work. Bit8 forces the first attempt and is cleared. State+0x3D8 snapshots current actor XY; the sequence appends point MoveTask0x8F3600(brain+0x130,0), then state event0x10. If bit7 was set by squad policy, it first appends MovementChange using current configured movement type/mode. Sites0x94F039..0x94F0EF. On a later Execute, it compares the snapshot with current XY through0x86F8D0: both component differences <= float32(1e-5) means no progress, so0x94F118 clears the task. Otherwise it snapshots again and retries. This is a progress/stall test, not a distance-to-goal success test. Idle event0x10 sets reconsider25; event0xC only refreshes anchor (0x94EE50), so neither is an arrival receipt. |
| Outdoors battle0x94A790 |Tactical movement0x9538A0 is attempted through its due timer and0x950AB0 gate (0x94AC6E/0x94AC88), after higher-priority healing/medic and other policy. With a task, behavior>=3 permits movement along goal direction. Below3, it compares the first component of the threat query at self and ten scaled units toward the goal; only a non-increase permits the task direction (0x953A42..0x953AE1). A rejected task direction falls back to ordinary behavior motion; behavior2 then returns false. Thus a goal can remain pending while native policy chooses another action. |
| Tactical arrival / abandonment |On an accepted task direction, current XY distance times inverse spatial scale0x1547D90 < float32(0.1) clears the request at0x953B25. This is the explicit local near-goal test; equality does not clear. Otherwise action4(task) chooses a cover candidate or a bounded point-path step. If actor XYZ still equals the last successful-attempt snapshot, the function delays and clears a pending task. A rejected movement-bound query returns false without clearing. Failed/nonpositive paths increment state+0x3D8; >5 with task6 set clears at0x954503. Successful issue resets the failure counter and snapshots position through0x9545F0 (0x954778/0x954789). A false return is therefore not one universal failure reason. |
| Cover idle0x947200 |After motion eligibility0x942C10 and hide gate0x950A50, task6 or follow9 triggers named motion_required: cancel current action via0x9122C0, leave the cover through0x898C50, deactivate the cover-idle state. Root/location selection then uses the outdoors path. A selected enemy first switches away from idle. These gates can defer a request; setter0x902FB0 does not bypass them. |
| Cover battle0x9477A0 |The same motion gate, tactical timer,0x950AB0 and hide gate precede0x9538A0 (0x9478E0/0x9478FA), so task-direction, near-goal and failure rules are shared with outdoors battle. Following9 separately leaves cover. Task movement may select MoveToCover or a Move step; it need not leave cover immediately or move straight to the final XY. |
| Behavior/mode gates |0x950AB0 rejects behavior2 only when task6 is clear; with direct-user control it additionally requires self-as-commander and a task.0x942C10 rejects linked actors, blocks hold mode except its permitted commander-follow branch, and rejects control1 with flag28.0x950A50 supplies a behavior0/current-cover hide gate. These are conditional native movement eligibility rules, not a universal authorization gained by setting bit6. |
| Squad waypoint0x9309C0 |Commander path waits only while task6 is set AND squared XYZ distance >= waypoint-radius squared (0x9312B5..0x93130E). Thus either strict radius entry OR task-clear permits context/advance; it does not distinguish arrival from abandonment. Without commander, members count as done when task-clear or near; all-done clears each task, with a separate empty-action-list/any-near shortcut. These waypoint rules are not a result code for an independent0x902FB0 call. |

The idle point factory0x8F3600 creates an owned eMoveTask plus ePoint task,
copies XY into task+0x10, and stores the task pointer at order+0x40. Near-zero
coordinates invoke the "Moving to zero bug!" diagnostic; there is no ordinary
false/invalid-coordinate return path. Do not treat arbitrary XY as prevalidated.
MoveTask Exec0x8F6230 returns0 while waiting for route work/child progress,
2 when the chassis start gate fails or bounded retries fail, and otherwise
propagates child completion. It may replan after progress and alter the local
point within actor radius on failed path requests. Cleanup phase2 removes its
chassis request and child.0x8F6970 is the start/replan boundary; path generation
and chassis execution stop at their result interfaces here.

For tactical Move, the preserved movement map establishes0 running,1 success,
2 failure; its internal route-end code6 maps to1. MoveToCover0x8CE670 returns2
when its initial cover destination gate rejects, propagates its child result,
and on success installs the cover descriptor through0x898AD0; cleanup releases
its reservation. Neither leaf result alone guarantees final direct-goal XY was
reached: tactical movement uses intermediate steps, and MoveTask can adjust its
local destination. Sequence-wide event/failure propagation beyond this request
boundary remains Q10, as requested.

**Prototype observation contract:** inspect the still-current request, task6,
actual self position and the intended tolerance separately. A runtime scenario
must confirm accepted orders, achieved distance and interruption timing; static
code cannot report those outcomes for a mission. This closes the contract
without entering pathfinding, animation or physical-movement internals.

## Q4 — morale events and freshness

This closes the Q4 blanks in **I-S**, **I-RECOVERY-DEADLINE** and
**O-MORALE-DELTA**. Earlier entries remain as historical evidence.
S is the human brain's morale-associated float at +0x474, initialized to100
with deadline+0x478=0 (constructor0x94190B). It is not the sensor's threat or
suppression accumulator. The named binding is
Scene::Stats::eMoraleProcessor, RTTI table0x10FF320, slot4=0xA5A490.

Human/client slot29=0x945010 takes the brain in RCX and float delta in XMM1.
It writes clamp(S+delta,0,100) at0x945067. A strictly negative delta also writes
now+convert(10) to+0x478 at0x94508F, even if S was already0. Positive/zero deltas
do not shorten that hold. It has no completion/result handle, allocation,
retention or cancellation; the write is immediate. P-call validity applies.

| Trigger / name | Delta and recipients | Phase and exact evidence |
|---|---|---|
| Construction | Self S=100; recovery deadline0. | Human constructor before attachment; Q2 map0x94190B. Copy/save/load visitors reviewed in Q2 do not copy/serialize S or its deadline. |
| Behavior0 recovery to2 | Self +30 when B·retreatRatio>=A. | During behavior selection, before state2 store.0x941E37; outlined duplicate0x944EBE (Q2: no executable caller established). Fine/flow/Q2 maps. |
| Timed recovery | Self +0.5·100/120, then deadline=now+convert(0.5). | Slot35 after base reconsideration0x903B10. Zero or expired deadline permits one increment; future deadline skips.0x94225B..0x9422B2. This is one step per eligible invocation, not catch-up for elapsed time or guaranteed two updates per unit. |
| Stats type5: health change | Affected actor's brain gets20·d, where d=(new raw health−old raw health)/raw maximum. | Vitality health setter0x925190 writes raw health first, then builds event5 via0x4E05D0 and synchronously publishes at0x925231, unless vitality+0x174 bit0 suppresses the event. Effective-to-raw factor0x9291C0 is the existing health boundary; damage internals stop there. |
| Type5 with d<0: health cap | After20·d, affected human slot31=0x945150 lowers S, if needed, to max(0,100·effectiveCurrentHealth/effectiveMaxHealth) via a second slot29 delta. It never raises S. | Processor calls29 at0xA5A4E7 then31 at0xA5A4FB. Health getters0x925010/0x924F90; cap subtraction0x9451BF and call0x9451D2. Healing/zero delta takes0xA5A541 directly and skips both the cap and source reward. |
| Type5 with d<0: enemy damage reward | Nonnull source actor gets−20·d only when relationship(affected,source,ignoreMask=false)==enemy1. No radius or ally distribution. |0xA5A501..0xA5A541, after affected actor delta/cap. Event source actor is the optional source snapshot at event+0x28; affected actor is+0x10 and d is float+0x128. |
| Stats type3: destruction/casualty | Distribute a total−60 around affected actor through its cached apparent allies, radius20·L. The affected actor is not separately given−60. |0xA5A554..0xA5A578. Vitality Die0x924BA0 calls publisher0x926DD0 on dying/death branches (0x924D5B/0x924EE5). Publisher requires owner lifecycle+0x38 bit13, constructs event3 via0x4E0490/0x4E0540, delivers0x926E2C. |
| Type3: enemy casualty reward | If source is nonnull and relationship(affected,source,false)==enemy1: source brain gets+10, followed by a separate total+10 distribution around source, radius20·L. |0xA5A57D..0xA5A5D7. No ally check on affected victim is needed for its penalty distribution. |
| Other type3 publishers | Same processor rules, rather than a different morale formula. | Interaction::eKillCrew0x7432A0 and Interaction::eAction_StatNotify0x74E510 also call0x4E0490. RTTI tables0x10D2A10/0x10D45E8, slot9. StatNotify's configured event-kind3 branch emits it. Thus type3 is a stats notification, not proof that this call physically killed an actor. |
| Newly near apparent ally | Human slot30=0x9450B0 casts the other actor's brain to eHumanBrain. Let d=(other.S−self.S)·0.25 from the pre-write values; call self29 with+d then other29 with−d. Both use ordinary clamp/negative-hold rules; the gap halves for finite in-range values. | Relationship refresh0x878560 sets subject+0x10 bit20 when apparent ally2 and XY distance<8·L, clears beyond16·L or when no longer ally/bound, and preserves the bit between thresholds (including equality). A0→1 edge with unusable4 clear calls observer brain30 at0x878723. Calls0x945119/0x94512B; no periodic exchange while bit remains set. |

For casualty distributions, **0xA5A5F0** uses origin actor+0xDD0→sensor,
category2 begin/end at+0x298/+0x2A0. It accepts subjects with relationship+0x14
bit2 and nonnull actor+0x18. For each, using current actor XYZ, define
w=1−clamp(distance/(20·L),0,1). When W=sum(w)>0 it calls each accepted actor's
brain29 with total·w/W (0xA5A7E3). At/outside the radius the contribution is0;
there is no effect if every weight is0. The total is distributed, not awarded
individually to every ally; clamping can reduce the net change. This helper
does not enumerate the whole scene or insert the origin into its own list.
Cached category/relation controls membership, current XYZ controls weight.
The default spatial L is20, not a claim that engine coordinates are meters.

**Delivery and validity.** Collector initialization0xA57DD0 appends the morale
processor unconditionally after per-player stats processors and the optional
event logger (0xA57FE6/0xA57FF8; append0xA58230). Publication0xA588F0 iterates
scene+0x1F0 collector's processor vector immediately, stopping if a prior
processor returns false, then notifies listeners. Morale processor itself
returns true and skips all changes under Q2's multiplayer-client predicate
0xC0B630. Type3 publication can also return before local delivery under that
predicate unless forced. Network routing beyond this gate is parked.

Events therefore change S synchronously during health/death or authored stats
publication, independently of the scheduled brain selector. Near-ally exchange
occurs during relationship refresh (perception commit and other refresh
callers); timed recovery occurs at the slot35 tail. Sensor membership can
predate a stats event. There is no atomic multi-recipient snapshot or fixed
cross-event ordering promised by these methods. Actor/brain/sensor and subject
bindings must be live for the synchronous call; distribution neither retains
subjects nor null-checks every brain. Event payload/source context is borrowed
for delivery and cleaned up by publishers; do not keep its pointers. Actual
mission type, callback ordering and changing cached membership remain runtime
integration checks, not unknown event meanings.

**Evidence and coverage.** New [Q4 map](../artifacts/build-25492057/decision-q4-map.json)
and [validation](../artifacts/build-25492057/decision-q4-validation.json):
18 game entries,1,168 instruction locations,eight floats,three RTTI tables.
The [slot29 audit](../artifacts/build-25492057/decision-q4-slot29-audit.json)
records51 literal memory CALL/JMP sites at+0xE8: nine typed brain sites and42
foreign chassis/UI/render/Steam/entity/network sites, plus the separately
recognized register-call0x945119. Receiver paths, first type/ABI bindings and
discovery-export fingerprints are retained. A displacement scan is not an
arbitrary-alias or undisassembled-code completeness proof.

Consolidation sources: [flow](flow-map-25492057.md) and
[manifest](../artifacts/build-25492057/flow-map.json) for native recovery;
[perception](perception-map-25492057.md) and
[manifest](../artifacts/build-25492057/perception-map.json) for0x878560
(0x8786CA/0x8786DB radius constants,0x8786EC/0x8786F2 bit20,0x878723 notification);
[ownership](ownership-map-25492057.md) and
[manifest](../artifacts/build-25492057/ownership-map.json) for relationship and
unusable bits; Q2 for shared human/client slots and client-mode gate. No new
verifier, game/editor execution or editor analysis.

## Q5 — prototype field meanings and native writers

This additive section closes the Q5 blanks and corrects the provisional
descriptions of +0x14C and flags0..5. All offsets below refer to the **brain**,
unless another receiver is stated. P-call validity applies: copy these values
during an admitted live brain callback; none grants ownership of its owner,
inventory, squad, state or subject pointers.

### Three fields

| Row / access and reads | Meaning and domain | Native writers and freshness |
|---|---|---|
| I-TARGET-RADIUS; float32+0x14C; target ranking0x913736 and Advance0x8D1C50/0x8D2120 | **Cached weapon range**, rather than an independently configured radius. Binding: embedded task+0x150 is named weaponRangeQuantor (constructor0x902554, string0x10EA228). Worker0x904110 calls brain slot21 and stores its XMM0 result at0x90412A. Human/client slot21=0x9442E0 returns the maximum usable range found by its inventory weapon/missile query, starting at0; it is neither the chosen target's distance nor proof that the current weapon can fire. Range is in engine spatial units. | Constructor0x902678 writes0. Ordinary human slot23 first calls base0x9035E0: task+0x150 due check, reschedule bounds(1,1), positive skip count+0x1BC decremented, otherwise queue0x904110 through0xA19450 (lambda pointer0x10EA2B8). The scene's queued-work phase precedes decision dispatch, but this cache updates only when due/unskipped. Inventory/ammunition changes can make it stale until then. Q2 client slot23 is empty, so it does not inherit this scheduled freshness promise. |
| I-ADVANCE-TIMESTAMP; uint32+0x18; Advance0x8D25FF/0x8D2682 | **Last successful native attack construction/issue time**, an inferred functional name supported by the diagnostic eHumanBrain::TryAttackEnemy. It is not a last-shot or last-hit receipt.0x9449E0 first constructs a firing target through0x912D60; only a nonempty result updates the counter. | Base constructor0x9024ED initializes0. Human0x944BCE writes now immediately after successful target construction; geometry rejection, cooldown and empty-result paths do not. Shared-field vehicle code0x989A80 writes now at0x98A09E after its accepted attack path. These writes occur during native attack policy, independently of Advance's subsequent reads. No special no-attack sentinel check is made by the reviewed age consumer. |
| I-EQUIPMENT-DEADLINE; uint32+0x3E0; root0x9573D2/0x9573DB | **Deferred equipment/weapon preparation policy deadline**, functional name;0 is unarmed, otherwise an absolute simulation counter. It is not an equipment-ready result. | Human constructor0x941867 writes0.0x9442A0(brain,float duration in XMM1) arms now+convert(duration) only while field is0 (store0x9442CD); another arm request leaves an existing deadline unchanged. WeaponPrepare0x8D8310 arms0.1 at0x8D8388 in its delayed human branch. Attack crew-exit finish0x98EAE0 arms0.4+0.5·sample at0x98EB70. Soldier root's Interrupt, after scanner updates, checks nonzero/due, calls0x9440E0(brain,true), and clears at0x9573FD only when that helper returns false. |

The human range query0x9442E0 visits inventory entries: weapon-tagged entries
need resolved ammunition and a weapon-range query; missile-tagged entries use
their available range record. It keeps the maximum and releases its temporary
inventory view. The first meaningful producer is sufficient here; ammunition
selection, inventory internals and ballistics remain parked. The old field ID
I-TARGET-RADIUS is retained for cross-references.

The exact Advance age comparisons refine the earlier single-threshold row:
0x8D263C/0x8D26B2 use **10**;0x8D26FF uses **5** on its other branch. That
branch first requires the separate order+0x58 age to exceed **3**
(0x8D26F6). Order modes1/2 instead use the periodic engagement-query branch.
All use the existing clock conversion0x65E710; there is no wall-clock promise.

For +0x3E0,0x9440E0 first returns false when the actor's relevant inventory
operation list is nonempty (0x7CCEB0). With restore=true, an existing right-hand
weapon/missile is sufficient to return true; otherwise it can submit UseItem
for a queried candidate, and still returns true if no candidate was found.
Thus true leaves the deadline armed for later root interrupts, including when
nothing was issued. The restore=false path can submit PutFromRight and is
separate from the due root call. The producer and consumer are now joined;
operation/FSM execution and general order completion remain deferred Q10.

**Shared-constant correction to Q2/Q4:** file float0x10124B4 is **5.0**.
Both0x941E2F→0x941E37 and0x944EB6→0x944EBE pass it to morale slot29.
Consequently the behavior0→2 recovery bonus is **+5**, not the +30 written
in the earlier Q2/Q4 tables. Those entries are retained; this correction is
authoritative. Q5 validates the constant directly. This does not change the
S<30 withdrawal threshold, casualty−60, damage multiplier20 or timed recovery.

### Low bits are one counter

Add **I-BRAIN-INHIBITION-COUNT**: uint32+0x39C bits0..5 form an unsigned
six-bit count0..63. Base dispatch reads it as nonzero (0x903DBD and following
mask); the individual I-BFLAG-0..5 rows are bit positions of this count, not
six independent booleans.0x902A10(brain,DL) increments for nonzero and decrements
for zero, replacing only the low six bits (0x902A3D). Its arithmetic wraps
modulo64; it does not saturate or protect an unmatched release. On0→nonzero
it also sets flag12 (0x902A5B).

| Preserved row | Correct interpretation |
|---|---|
| I-BFLAG-0 | Count bit0, weight1. |
| I-BFLAG-1 | Count bit1, weight2. |
| I-BFLAG-2 | Count bit2, weight4. |
| I-BFLAG-3 | Count bit3, weight8. |
| I-BFLAG-4 | Count bit4, weight16. |
| I-BFLAG-5 | Count bit5, weight32. |

The constructor's0x08001000 at0x9026B4 initializes the count0, flag12 and
follow_commander27. Brain slot15=0x902350 is the other count writer: it increments
when flag19 is set, decrements when clear (0x90239B/0x9023B0), and sets12 on
entry into inhibition (0x9023CF). Locking can also clear the owner's selected
control bit and notify its slot59. The callback is invoked on flag19
transitions, not once every decision.

Its concrete causes are already in the preserved maps: incapacitation setter
0x9259B0 mirrors vitality+0x174 bit1 into brain19 (0x925AC5) then calls15;
human boarding/placement events0x1E/0x23 set19, and exit events0x1F/0x24
clear it (0x93744F/0x93753B/0x93763E/0x93768D), with guarded callbacks.
External control also balances0x902A10: inventory exchange gc.exam.begin
0xAF84D0 adds, gc.exam.end0xAF8720 removes;0xAF96F0 forwards its Boolean;
0xAF8310 removes on control teardown; manual-control change0xB7EDC0 removes
from the previous brain and adds to the new one. These are synchronous native
ownership/control transitions. Preserve the counter as a unit.

### Other selected brain flags

These rows name explicit native writer sets at the prototype boundary.
Functional labels are inferences except the command binding follow_commander.
All are mission-mutable and inline in the same live-brain uint32. Constructor
initialization above applies to every row; masked stores preserve the others.

| Row(s) | Meaning / read contract | Native writers and phase (instruction RVAs where newly joined) |
|---|---|---|
| I-BFLAG-6 | Direct-goal request active. |0x902FB0 sets;0x902F50 clears (Q3 policy callers and outcomes). Load token71 sets at0x909641. No arrival inference from a clear. |
| I-BFLAG-7 | Squad task requests configured movement type/mode before moving. | Squad destination0x932970 first calls direct-goal, then replaces7 with(squad+0x1C8!=null) at0x932A2D/0x932D21. Direct-goal/clear reset7. |
| I-BFLAG-8 | Fresh task / bypass first progress comparison. | Direct-goal sets8 at0x903020; idle event path sets0x94EEF8. Human task paths consume it by clearing0x94F04F/0x94F8AF; shared vehicle task0x9924EB does likewise. |
| I-BFLAG-9 | Follow request active. |0x902B20 enables only when27 is set, simultaneously setting10/25; disable always clears9.0x902D80 clears on follow-completion/cover-user-task gates. Direct-goal/clear also clear9. |
| I-BFLAG-10 | Fresh follow / bypass first progress comparison. |0x902B20 sets; consumed by human follow paths0x94B366/0x94F26F/0x94F9CF/0x951684 and shared vehicle0x992680. Clearing9 alone need not clear10. |
| I-BFLAG-11 | Leave-idle policy latch, consumed by native state policy. | Setter0x902F00 replaces11 at0x902F35, rejecting an enable request under user control. Location selection0x952737 invokes it on leaving idle. Direct-goal/clear reset11. |
| I-BFLAG-12 (added for count/order coexistence) | Default order stream enabled while no current overriding order; also set on inhibition entry. | Count callbacks above set12. Current-order setter0x9052F0 clears at0x905380 when installing an override, sets at0x9053BF for null; callback0x905710 checks it before default-stream execution. Native effect follows setter/counter transitions, not a periodic recomputation. |
| I-BFLAG-13 | Eligible-enemy aggregate. | Base0x903B10 clears/rebuilds from subject list before interrupts;0x902DE0 also clears at0x902E61/0x902EA8 when marking/suppressing targets for Advance. |
| I-BFLAG-14 | Attacking aggregate. | Base0x903B10, exact subject-derived stores in Q1a O-BASE-AGGREGATES. |
| I-BFLAG-15 | Injury aggregate. | Same base clear/rebuild. |
| I-BFLAG-16 | Threatening aggregate. | Same base clear/rebuild. |
| I-BFLAG-17 | Own recent-threat age<10 aggregate. | Base0x903B9F/0x903DEE, sensor threat-age query; rebuilt per reached base pass. |
| I-BFLAG-18 | Squad flag1 when attached, otherwise own17. | Base0x903DEE. Squad+0x10 is itself rebuilt by0x9309C0 from member metadata+0xB88; this is a cached squad summary, not a new live all-member query. |
| I-BFLAG-19 (added as count producer input) | Incapacitation/boarding inhibition request; not synonymous with death. |0x925AC5 and four human event stores above, followed by15 only on a change. |
| I-BFLAG-20 | Separate dispatch block; also forces current-order removal in0x905710. | Actor event0xD replaces20 from event+8!=0 at0x7F1D9F and notifies brain slot11. Final-death handler0x926B80 sets20 at0x926D97 and also notifies11. These synchronous event writers explain freshness without following their downstream chassis work. |
| I-BFLAG-21 | Interrupt/state-change latch. | Base clears at0x903F70; state helpers0x912211/0x91226D/0x91258F set during the synchronous interrupt/deactivation walk. |
| I-BFLAG-22 | Current candidate action label matched. | Base0x903F70 clears; label helper0x9121D8 sets when labels match. |
| I-BFLAG-24 (added for output cancellation) | Inside current-order callback. |0x90578A sets immediately before the callback;0x90579D clears immediately after. Current-order setter uses it for deferred destruction (Q3), so externally replacing the whole flag word can break ownership. |
| I-BFLAG-25 | Reconsider/update request. | Direct-goal0x902FB0, follow0x902B20, interrupt0x903FBB and explicit request0x903AC0 set;0x9035C3 clears before dispatching virtual35. Other retained event/action set sites:0x8CEDDE,0x8D173C,0x8D1BB6,0x94A6E2,0x94C469,0x94EE92/AB/DF,0x9529DD,0x980E2B,0x988D79,0x9891FD,0x98EF2A/6D. It can be set again during the same decision. |
| I-BFLAG-27 | follow_commander command flag. | Default1; actor_state store0x99FED7 replaces. Squad creation/member/commander paths set0x92F33A/0x93015E/0x9302DE/0x9306EF; control follow path sets0xB669DD. Clear sites0x8CEB7D, load token76 at0x90957B,0xB66B58,0xB67784,0xB6C05E,0xB6DBDE. These command/formation changes are independent of per-decision follow9. |
| I-BFLAG-28 | User/squad task marker which gates human movement under user control. | Control issue paths set0xB6DCA4/0xB6F23C; load token75 sets0x909566. Squad destination/reset clears0x9322CE/0x9329AE; ownership/control path clears0xB66589. No global task-completion meaning is assigned. |
| I-BFLAG-29 | Explicit look-at point valid at+0x140..+0x148. | Setter0x903270 copies XYZ and sets29 at0x90327F. Clear0x9032A0 resets the point from globals and clears29 at0x9032D0. Direct-goal/clear do the same. Look/aim execution remains Q10. |

The 39C offset alone is not sufficient typing:0x8E8D90 writes a
FortifySequence retry count (RTTI0x10E8A38, slot19),0xB21C30 parses minimap
configuration (eGC_MiniMap.cpp and /interface/scene/minimap/minimap.set), and
0xB5E9E0 writes an InterfaceActionCustom volume-ability mask. These are excluded,
as are same-offset state, chassis, GUI and stack storage. Remaining unrelated
literal leads were not promoted into catalog work. The scan of39D/39E/39F
finds only two byte reads, no extra literal byte writer.

The copied brain configuration at0x909150 includes398, not39C; it does not
clone the transient count/flags, cached range, attack time or equipment deadline.
Reviewed load cases explicitly restore task6/user-task28/disabled-follow27 as
listed, rather than deserialize this flag word wholesale (Q2 map). A
prototype must preserve native flags it is not changing and treat runtime hook
ordering separately from these static writer contracts. Alias/bulk-copy and
unrecognized-code completeness is not asserted.

### Evidence and closure

New [Q5 map](../artifacts/build-25492057/decision-q5-map.json) and
[validation](../artifacts/build-25492057/decision-q5-validation.json) check27 game
entries,1,136 selected instruction locations,seven floats,three RTTI tables and
two byte ranges. The selected sites validate boundaries and constants; this is
not a new comprehensive instruction/read census. The existing verifier was used.

Consolidation, with instruction RVAs given above:

- [Systems note](systems-map-25492057.md) and [map](../artifacts/build-25492057/systems-map.json): scene phases and slot23 task scheduling.
- [Combat note](combat-map-25492057.md), [map](../artifacts/build-25492057/combat-map.json), [human states](human-states-map-25492057.md) and [map](../artifacts/build-25492057/human-states-map.json): inventory/tag query boundaries, TryAttackEnemy and human task/follow consumers.
- [Engagement orders](engagement-orders-map-25492057.md) and [map](../artifacts/build-25492057/engagement-orders-map.json): Advance read paths and age branches.
- [Crew actions](crew-actions-map-25492057.md) and [map](../artifacts/build-25492057/crew-actions-map.json), Q1b map0x9440E0/0x957380: deadline arm/consume join. The AI type catalog binds0x8D8310 to Order::eWeaponPrepare.
- [Entity health](entity-health-map-25492057.md) and [map](../artifacts/build-25492057/entity-health-map.json), [ownership](ownership-map-25492057.md) and [map](../artifacts/build-25492057/ownership-map.json): incapacitation, boarding and actor/death flags.
- [Squad](squad-map-25492057.md) and [map](../artifacts/build-25492057/squad-map.json), [vehicle brain](vehicle-brain-map-25492057.md) and [map](../artifacts/build-25492057/vehicle-brain-map.json): shared flag/timestamp writers and squad metadata.
- Q1a, Q2 and Q3 maps: aggregate/latch stores, constructor/copy/load and current-order ownership. [Fine map](../artifacts/build-25492057/fine-map.json): command bindings and the shared5.0 constant.

Q1z and Q2–Q5 are now closed at their stated static depth. No active prototype
row blank remains assigned to them. Q6–Q11 and the expressly deferred target/
cover/fire/stance/output details remain static work for a later prototype
stage; the full native-read census stays parked. Runtime still must establish
the admitted concrete object/ABI/hook timing, actual values/cadence, and observed
movement outcomes. No game/editor execution or modification was performed.
