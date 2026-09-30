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
