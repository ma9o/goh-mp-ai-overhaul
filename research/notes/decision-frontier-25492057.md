# Decision frontier — build 25492057

This file replaces the [static frontier](static-frontier-25492057.md) as the
working plan for static research. That file remains as a historical log; do not
take leads from it directly. Addresses are **game** RVAs and offsets are
hexadecimal. Everything here is static evidence, not runtime verification.

## Objective and scope rule

Map the prototype-facing infantry-brain fields and interfaces listed in Q1z,
their inputs to the depth of meaning and outputs to the depth of contract.
The full native decision-read census is deferred; Q1/Q1a and partial Q1b work
remain preserved evidence.

An input is closed when these are known from static evidence:

1. **Read site:** the decision code that reads it.
2. **Access path:** brain → object chain → offset, type, width.
3. **Meaning:** name and value domain (enum names, units, range), from the
   nearest binding (table, parser key, config file, RTTI, diagnostic string).
4. **Freshness:** which phase or cadence writes it relative to the brain
   update, and whether it can change during a mission.
5. **Validity:** lifetime of the pointers on the access path.

Follow a producer only to fill 3 or 4, and stop at the first step that does.
Map a computation in full only if the brain performs it (it is decision logic),
or if the brain must evaluate it for hypothetical inputs; in that case map its
query interface (inputs, outputs, side effects), not its internals.

An output (order, command, setter) is closed when its observable effect, result
codes, ownership, cancellation and side effects are known, not how chassis,
animation or physics carry it out.

Inside decision logic, indirect calls and helpers are leads to follow. Outside
it, a lead that fills none of the blanks above is parked as one line.

## Working rules

- Re-read this file at the start of every item. It takes precedence over the
  old static frontier and over the README's game/editor pairing rule.
- Take work only from Open: next, one item per pass, in order. Open: later
  remains deferred until a prototype stage explicitly needs an item.
- Evidence rule: consolidation cites existing notes, manifests and instruction
  RVAs in the catalog Markdown. Create a new manifest, validation file or
  verifier only when a pass does new disassembly.
- A new lead must name the catalog row and blank it fills (see Q1). If it names
  none, add one line under Parked and do not investigate it.
- Map the game binary. Use the editor only when one of its assertions answers
  an open item.
- Start each commit message with the item ID (`Q3: …`).
- When an item closes, record the answer in its catalog row and move it to the
  Closed section with its evidence locations. A blank that only runtime can
  settle is recorded as such; that closes it for static purposes.
- If an item outgrows one pass, close what you can and split the rest into
  narrower items at the top of "Open: next".
- Keep all existing work. Earlier maps, manifests, TSVs, scripts and the old
  frontier stay as they are, parked or not; reuse them as evidence.
- Current stop condition: after Q1z and Q2–Q5, add a status summary recording
  closed items, blanks needing runtime verification and deferred items; commit
  it and stop. Do not activate Q6–Q11 merely to empty Open: later.

## Closed

J1–J8 were closed by joining existing evidence, with no new analysis beyond
the assembly checks listed. Each is a seed row for the catalog (Q1). Items
closed later are appended after them.

### J1. Terrain class at a point

- **Read sites:** cover-candidate gate `0x8A0B20` (classes 4 and 5 reject; 3
  rejects stance kind 1), pose limiter `0x942FC0` (no crawl in 3–5), detour
  point sampling in `0x93ED30` (rejects 3–5).
- **Access:** `0xA43AB0(xy)` → level-0 heightmap cell, `byte[+2] & 0xF` (second
  field `(word[+2] >> 4) & 0x7F`).
- **Meaning:** path-cost lookup `0x8AEBC0` reads the same byte and mask from the
  same level layout (cell size `+0`, cells `+8`, dimensions `+0x18/+0x1C`) and
  indexes the terrain table whose IDs `0xA44070` names: 0 undef, 1 ground,
  2 no_pass, **3 shallow_water, 4 water, 5 deep_water**, 6 swamp, 7 road,
  8 no_pass_human, 9 snow, A forest, B slope. Independently, `0xA43AB0` zeroes
  classes 3–5 when the cell's height is above the scene water level (`+0x38`),
  and water-surface query `0xA42910` accepts exactly those classes.
- **Brain effect:** no cover in water or deep water; no stance-kind-1 cover in
  shallow water; no crawling or detour points in any water.
- **Evidence:** `0xA43B4D`/`0xA43B59` and `0x8AED1D`/`0x8AED28`; path-profiles
  (native terrain IDs), path-acceptance (terrain lookup), ground-state
  (`0xA43AB0`, `0xA42910`), systems, human-states and human-geometry maps.
- **Open blank:** freshness — whether packed cells are rewritten during a
  mission. Only "can it change" is needed, not the writer's mechanism.

### J2. Threatening and attacking (`subject+0x14` bits 7 and 8)

Sensor slot 17 `0x877D20` sets these on an eligible non-ally subject that is
the signal's source. Hit (3) or explosion (5) inside ThreatZone (`sensor+0x308`,
default 15·L) sets threatening (7) and known-combat (10); inside AttackZone
(`sensor+0x30C`, 5·L) it adds attacking (8). Injure (4) sets bits 7–10
regardless of distance, with the attacker as source. Shot (2) only updates the
observation. Evidence: ownership map ("Threatening, attacking and injury
observations"), signals-damage map (signal names), human-states map.

### J3. Targetable (`brain+0x398` bit 15)

Place Include `0x77C140` clears it under `TurnOff.Targetable`; Exclude
`0x77B950` restores it. Subject bit 1 (enemy) requires the target's bit, so
bit 1 means "enemy and targetable". Evidence: ownership map ("Targetability and
collision identity"), tactics map (relationship flags).

### J4. Linked to a parent (`entity+0x1C0` bit 28)

The marker condition `linked` (`0x757870`) requires it and matches tags on the
linked parent's definition; contact code makes such an entity follow its
parent's velocity. Decision code excludes these entities from weapon pickup,
medic patients and grenade threats, and conditionally from targets. The name is
inferred from those uses; no binding string was located. Evidence:
marker-actions, entity-contact, combat, human-states and movement maps.

### J5. Searched (`subject+0x10` bit 27)

Set when find-enemy search completes (phase 9). While it is set, the
observation cannot become "waiting" for Recon (bit 26 requires bit 27 clear).
The name is inferred; no binding string was located. Evidence: human-states
map (search phases, Recon counts), perception map.

### J6. Behavior state values (`brain+0x52C`)

Consumers treat 0 and 1 as withdrawal (retreat and rush_back actions; cover
weights favour hiding and leaving threat range; the out-of-bounds scanner acts
only below 2), 2 as no tactical movement, and 3 and 4 as advance (advance and
rush_ahead; cover weights favour target screening and own weapon range). The
stance policy skips its long-range crawl rule in 0 and 4.

Selection (fine map): from a nonzero state, 3 or 4 when S > 70 and B > Ta
(4 when B > 2·Ta) unless `no_advance`; then, unless `no_retreat`, overridden
to 0 when A > Tr or S < 30, or to 1 when S < 50. From 0: back to 2 once
B·Rr ≥ A; otherwise a timed 3 when S > 50.
This matches the unreferenced table `0x10EC720`: flee, retreat, none, advance,
charge. Evidence: fine, flow, tactics, movement and human-states maps. Open: no
reference to the table has been found; the names are functional.

### J7. Freshness of perception and combat estimates

Within one scene quant (`0xA1B320`), actor slot 21 `0x7EFBF0` runs sensor
preparation (slot 12) and queues the A/B score job (human brain slot 23
`0x941BC0`, when it registers) in phase 1. Queued jobs run in phase 2, possibly
on workers. The actor dispatcher `0x7EFD10` then calls sensor slot 13
`0x873B00` (commit through `0x878AE0`) at `0x7EFE5B`, immediately before
checking the brain's pending flag and dispatching slot 35. Sensor-derived
inputs and A/B therefore come from the same quant as the decision. Look-around
uses 0.5–1 unit bounds, inform 0.25–0.75 and brain task 0.5–1.5
(bucket-allocated). Evidence: systems, perception and flow maps.

### J8. Operatable (`actor+0xDB4` bit 8)

Property `Operatable` (token 6D). Derived by actor slot 67 `0x7F1A30`: it
needs a brain, an operability source (occupied crew slots for crewed placers,
otherwise `DB4` bit 12) and clear low two bits of `vitality+0x174`. `0x7F0130`
reconciles the bit with that result (event 0x1A); its blinding branch clears
the bit only for the duration of that event. Consumers:
relationship (a non-operatable target is neutral), sensor signal handling,
relationship flag bit 4, WeaponFire target predicate `0x83DB40`, boarding
access `0x7FA9D0`. Evidence: ownership map ("Operatable is also derived"),
modifier map (filter term `operatable`). The engagement-orders map's "still
open" remark predates this.

### Q1. Input and output catalog — split

The ledger that makes the scope rule checkable: one row per variable read by
the human decision logic and per output it issues, with the five blanks, read
sites and evidence. Decision logic means the slot-35 selection region, base
reconsideration `0x903B10`, the state Interrupt/Execute methods and their
policy helpers, as listed in the fine, flow, tactics, movement, human-states
and engagement-orders manifests. Outputs are the setters, `0x902FB0` and the
order factories those methods call.

This is mostly consolidation. Several answers already exist but are scattered,
and older notes still call some of them open (J5, J6, J8). Seed it with J1–J8.
Done when every variable those methods read has a row and every empty blank is
explicit.

**Result (partial inventory; Q1a–Q1g below finish it):** created the
[decision catalog](decision-catalog-25492057.md) with 28 input rows, seven
output rows, seven fixed operands and explicit coverage rows. J1–J8 are seeded;
all memory-access/call sites in the slot-35 selection span
`0x941D86 <= RVA < 0x941F49` are classified (45 sites, including two ABI stack
saves). The full base/state/helper inventory is not claimed complete.

Two joins sharpen the seed evidence: the threshold multiplier reads cover
group `owner+0xDC0 -> +0x24` (`0x941DA8/0x941DBB`, same path as root selector
`0x957428/0x95742F`); A/B refresh is conditional on the scheduled task being due
and unskipped (`0x941C56`–`0x941CD3`). J7's same-quant score statement applies
when that job runs, not to every decision. Concrete brain factory selection
remains Q2. Unknown names, freshness, validity and output-contract details are
explicit catalog blanks, not runtime-only conclusions.

Evidence: [Q1 manifest](../artifacts/build-25492057/decision-catalog-q1-map.json),
[validation](../artifacts/build-25492057/decision-catalog-q1-validation.json),
game-only `call_to_arms.decision-catalog-q1-pass.tsv`, and the catalog's seed
crosswalk to the preserved maps. The new catalog checker reuses
`verify_fine_map.py` and additionally checks row links and the bounded access
census. No game/editor execution; no editor analysis was needed.

### Q1a. Base reconsideration and state dispatch catalog

Catalog row **C-BASE**, blank: per-variable inputs and issued outputs of
`0x903B10` and the state dispatch/interrupt boundaries
`0x912110/170/1C0/200/260/370/3D0/460/4D0/520` in the movement manifest.
Enumerate individual `+0x39C` bits, queue/state pointers, aggregate inputs,
external query results and emitted events. Reuse I-THREATENING/I-ATTACKING.
Stop at query/output contracts; do not expand state bodies assigned below.

**Closed (inventory):** C-BASE is enumerated in the additive Q1a catalog
section: 75 input records, 17 output records, three immutable float operands.
All 252 memory-spelled/CALL sites in the eleven entries are linked or explicitly
excluded as ABI/local/derived storage. Threat age, mandie publication gates,
event8 payload(0,1), and alert-gate clearing are checked; semantic/phase/lifetime
blanks remain explicit for Q1g/Q5/Q10, not declared runtime-only.

Evidence: [catalog extension](decision-catalog-25492057.md#q1a-extension--c-base-enumerated),
[manifest](../artifacts/build-25492057/decision-catalog-q1a-map.json),
[validation](../artifacts/build-25492057/decision-catalog-q1a-validation.json).
Game-only focused export; 15 entries, 703 instruction locations, three floats
and event payload bytes validated. No game/editor execution or modification.

### Q1b. Root, location and hazard policy catalog

Catalog row **C-ROOT**, blank: per-variable inputs and issued outputs of
soldier/civilian root and location selection, root interrupt `0x955A90`, and
hazard-scanner query interfaces in the movement manifest. Include saved-order
and restoration fields; reuse I-COVER-GROUP. Order execution, physical escape
and path internals are outside this census.

**Partial checkpoint committed at user direction:** retained 83 input records
and 28 output boundaries for root/location selection, hazards and restoration;
no full C-ROOT census is claimed. The separate selector tail0x9526D0, snapshot
R8B permission, and brain+0x3E0 deadline consumer are now explicit.

Evidence: [Q1b catalog checkpoint](decision-catalog-25492057.md#q1b-partial-checkpoint--root-and-restoration-policy),
[manifest](../artifacts/build-25492057/decision-catalog-q1b-map.json),
[validation](../artifacts/build-25492057/decision-catalog-q1b-validation.json).
33 game entries, 3,226 instruction locations and six constants checked.
No game/editor execution or modification. Remaining census deferred by the
user's revised plan; Q1z will select prototype-facing blanks.

### Q1z. Prototype-facing rows

The active catalog covers only:

- **Reads:** self position, stance, cover group and descriptor, health ratio,
  morale, A/B estimates, behavior state/deadline, modes and flags, sensor subject
  fields, relationship and clock.
- **Writes/calls:** brain+0x52C/+0x530, direct goal0x902FB0, move-mode/speed
  setters, fire-mode bits, no_advance/no_retreat and advance/retreat ratios;
  preferred-subject query0x903050 is later-stage work.
- **Shared-field contracts:** native readers and writers of those same fields,
  to meaning/freshness/validity or output-contract depth only.

Catalog rows I-STANCE, I-HEALTH-RATIO, I-SELECTED-TARGET(+0x3B8),
I-PREFERRED-TARGET(+0x3D0), and any other missing rows in the set above fill
the prototype read/access/meaning/freshness/validity blanks. Reuse existing
rows and evidence; do not finish C-ROOT/C-IDLE/C-BATTLE/C-SEARCH/C-HELPERS/
C-OUTPUTS as comprehensive native-read inventories. Q2–Q5 retain their named
blanks. Q6–Q11 stay in Open: later until a prototype stage needs them.

Done when this bounded row set exists and each blank is either filled, linked
to Q2–Q5, explicitly deferred with its later prototype stage, or marked runtime
only with a reason. After Q1z, work Q2–Q5, then append and commit a status
summary (closed items, runtime blanks, deferred items) and stop.

**Closed (bounded inventory/consolidation):** the additive
[Q1z catalog overlay](decision-catalog-25492057.md#q1z--effective-prototype-catalog)
selects the prototype-facing rows, adds stance/effective-health and separate
selected/preferred/previous target references, and consolidates subject,
relationship, cover-mutability, clock and setter contracts. It names exact
Q2–Q5 blanks and later-stage static deferrals. Subject reference ownership is
separate from actor/entity binding validity. No full native census is claimed.

Existing notes/manifests/instruction RVAs are cited in the catalog. No new
disassembly, manifest, validation file or verifier was required. Earlier Q1/Q1a
and partial Q1b artifacts are retained. Q2 is the next active item.

### Q2. Everything that can change the behavior state between decisions

Catalog rows: I-BEHAVIOR/I-BEHAVIOR-DEADLINE (writer/phase), I-THIS/I-OWNER
(concrete brain attachment/dispatch), I-A/I-B (which refresh implementation).

Blank: freshness of `+0x52C` and its deadline `+0x530`; also which slot-35
implementation single-player infantry run.

Known literal writers: constructor `0x9417E0`; selection region of `0x941D00`
(`0x941DEB`–`0x941F42`); outlined copy `0x944E10`, which honours the deadline
but has no caller in current exports and is not a vtable slot in the AI type
catalog. Literal-displacement hits whose object type is unverified: `0xCE2F0`
(16-byte store at `+0x52C`) and `0xB03060` (float store). Also check save/load
and the brain factory (`eHumanBrain` and `eHumanBrainClient` share slot 35 but
differ in slot 23).

Done when every path that writes these fields is listed with its trigger and
phase, or excluded with object-type evidence.

**Closed for static purposes:** catalog Q2 records the constructor, inline
selection and uncalled outlined selection writer sets; excludes same-offset
campaign/manual/UI/chassis/stack/root storage; checks copy and save/load;
and maps human versus human_client factory selection and slot23/29/35.
Normal human-name construction outside the eligible multiplayer-client gate
uses the human refresh. The actual mission type and injection timing need
runtime verification; no arbitrary-alias completeness claim is made.

Evidence: [catalog Q2](decision-catalog-25492057.md#q2--behavior-writers-and-concrete-brain-attachment),
[map](../artifacts/build-25492057/decision-q2-map.json),
[validation](../artifacts/build-25492057/decision-q2-validation.json).
35 game entries, 2,878 instruction locations, seven RTTI tables and five data
ranges checked with the existing verifier. No game/editor execution.

### Q3. Direct-goal contract (`0x902FB0`)

Blank: output contract (effect, completion, failure, cancellation).

Known: `RCX` = brain, `RDX` = pointer to goal XY. It ends the current order
through brain slot 13, clears flags 7/9/11/29, sets 6/8/25, writes the goal to
`+0x130` and resets `+0x140..+0x148` from constants. Start from idle Execute
`0x94EF90`, tactical movement `0x9538A0` (task destination `+0x130`, flag 6;
more than five failures counted at `state+0x3D8` can clear the task), squad Quant
`0x9309C0` (completion from waypoint radius and flag 6) and clear helper
`0x902F50`.

Done when, for idle, battle and cover states, it is known how a goal becomes
movement, what marks arrival, what abandons it, and how the behavior state
gates it.

**Closed:** O-DIRECT-GOAL now distinguishes request storage, current-order
ownership/cancellation, idle progress retries, tactical near-goal and failure
clears, cover-state gates and squad waypoint completion. A cleared task bit is
not proof of arrival; the clear helper does not itself end the current order.
Leaf movement results stop at their output interfaces; Q10 remains deferred.

Evidence: [catalog Q3](decision-catalog-25492057.md#q3--direct-goal-output-contract),
[map](../artifacts/build-25492057/decision-q3-map.json),
[validation](../artifacts/build-25492057/decision-q3-validation.json), plus the
preserved human-states, movement, flow and squad maps. Five new game entries,
598 instruction locations, two floats and one RTTI table checked. Actual arrival
and interruption timing need runtime verification. No game/editor execution.

### Q4. Morale inputs (`brain+0x474`)

Blank: meaning of S (what moves it).

Known: slot 29 `0x945010` clamps S + delta to 0–100; a negative delta holds
recovery for 10 units (`+0x478`); slot 35 then recovers 0.5·100/120 every 0.5
units. `Scene::Stats::eMoraleProcessor` (vtable `0x10FF320`) slot 4 `0xA5A490`
handles event types 3 and 5 and calls slot 29. Start from its event
publishers, recipient selection and all other callers of brain slot 29 (vtable
offset `0xE8`).

Done when each S-changing event has a name, delta, recipient rule (radius,
relationship) and phase.

**Closed:** catalog Q4 names health-change and casualty events, health-loss
cap, enemy rewards, distance-weighted ally distribution, near-ally exchange,
constructor/behavior/timed recovery and synchronous delivery/client gates.
Distribution totals are shared across cached allies; health loss applies a
separate health-percentage cap. Pointer/cache and event ordering limits are
explicit runtime integration checks.

Evidence: [catalog Q4](decision-catalog-25492057.md#q4--morale-events-and-freshness),
[map](../artifacts/build-25492057/decision-q4-map.json),
[validation](../artifacts/build-25492057/decision-q4-validation.json),
[slot29 receiver audit](../artifacts/build-25492057/decision-q4-slot29-audit.json).
18 game entries,1,168 instruction locations,eight floats and three RTTI tables
checked. Existing perception/ownership/flow/Q2 evidence is cited for joins.
No game/editor execution or modification.

### Q5. Unnamed brain fields read by decision code

Blank: meaning, then freshness.

- `+0x14C`: configured radius. Target ranking `0x9136E0` drops farther
  candidates when any lie inside it; Advance's subject gate `0x8D1C50` and
  engagement test `0x8D2120` use it.
- `+0x18`: timestamp whose age Advance `0x8D25B0` compares with five units.
- `+0x3E0`: human deadline with unmapped consumers (old frontier lead).
- `+0x39C` bits read by decision code without a name (Q1 lists them).

Done when each has a binding name or an explicit writer set.

**Closed:** catalog Q5 identifies14C as a scheduled weapon-range cache,18 as
the native attack-construction/issue timestamp,3E0 as a deferred equipment
policy deadline, and39C low six bits as one inhibition counter. Remaining
prototype flag rows have explicit writer sets and phase/ownership constraints.
The constant check also corrects Q2/Q4's behavior-recovery bonus to+5;
the earlier text is retained with an authoritative catalog correction.

Evidence: [catalog Q5](decision-catalog-25492057.md#q5--prototype-field-meanings-and-native-writers),
[map](../artifacts/build-25492057/decision-q5-map.json),
[validation](../artifacts/build-25492057/decision-q5-validation.json).
27 game entries,1,136 selected instruction locations,seven floats,three RTTI
tables and two byte ranges checked. Existing notes/manifests supply the
timestamp, deadline, aggregate and ownership joins. No full native census or
runtime execution is claimed.

## Open: next

No active research items. Add the required status summary, commit it and stop.

## Open: later

Needed when the prototype takes over target, cover, fire or stance choice.

- **Q6. Preferred subject `0x903050`.** Target refresh `0x943560` calls it at
  `0x943578`, stores the borrowed result at `+0x3D0` and retains it
  (`LOCK INC [subject+8]`). Release decrements; at zero with flag `+0xC` bit 0
  it disposes through `0x652A10` (bit 1) or vslot 0. Vehicle brains use it too.
  Close its sources (user, squad and order target assignments) and the
  ownership of its return value.
- **Q7. Cover-state meaning.** Direction masks `0x20`, `0x40`, `0x180`, `0x1C0`
  (cover battle `0x947CF0`, horizontal `0x948B60`, vertical `0x9493D0`);
  descriptor `+8`/`+0xC` roles (tables `0x14821E0`/`0x1482218`);
  healing-cover validator `Human::eCoversForHeal`.
- **Q8. Output enums.** Which decision code reads each fire_mode value
  (`+0x398` bits 10–11; "firing is held" in the outdoors interrupt, the
  attack-control predicate), and whether a write needs side effects beyond the
  `actor_state` path `0x99F940`. Names of the movement type/mode values that
  MovementChange uses in modes 2, 4 and 5 (`0x8F7330` → foot slots 44/45
  `0x848490`/`0x848710`; table `0x1481170`).
- **Q9. Remaining target/perception blanks.** The linked-actor type test in
  target eligibility `0x912AE0`; the meaning of the environment query behind
  the 0.13 vision factor in `0x88EA30`.
- **Q10. Secondary outputs.** Complex-order sequencing and failure propagation
  (`0x912370`, `0x900A60`); state events 0xB, 0xC, 0xF and 0x10–0x13 and the
  transitions they trigger; Rotate, LookAt, Aim, WeaponWaitReady `0x8D6470` and
  Take: effect, results, cancellation.
- **Q11. Native go-to commands.** `actor_to_waypoint` (factory `0x1A9740`) and
  `squad_to_waypoint` (`0x9CF980`): defaults and effects, compared with
  `0x902FB0`.

## Parked

- Full census of native decision reads: deferred; Q1/Q1a work is retained; resume only if a prototype row needs it.

Do not investigate unless an open item names the catalog blank it would fill.
Parked areas that are already mapped stay in the repository unchanged and
remain usable evidence.

- Physical simulation: contacts, contact solver, collision geometry, body
  integration and sleep, ground/water forces, throwing and landing, force
  writers.
- Spatial internals: shape and volume predicates and loaders, area lifecycle
  and worker queues, scene spatial state, collider profiles.
- Pathfinder internals: grid and network search, portals, line costs, path
  profiles and obstacles, route sampling. (J1 uses the terrain ID table.)
- Animation and FSM internals: animation control, resources and timelines,
  pose and idle profiles, FSM graph and handlers, marker actions and paths,
  interaction batches.
- Sound: definitions, inheritance, owners, channels. (J2 uses the signal names.)
- Weapon internals: ballistics, burst sampling, projectiles and contacts, bone
  limits, charger and animation producers, weaponry `+0x86`.
- Damage internals: armor and overmatch, component damage, materials, wounds,
  knockdown and throw-off, melee defence and blows, blinding producers.
- Vehicles, crew, boarding, placement, aircraft, animals, cannons.
- Inventory internals: queues, codecs, ownership, rearrangement, repair and
  tooling.
- Entity lifetime internals: ID banks, deferred disposal, component
  replacement. For this frontier, validity means "valid during the brain call".
- Clock internals beyond units and pause: slowdown stack, timer lists, event
  router, frame packets, RNG registry.
- Other native commands and conditions, mission controls, script APIs.
- Lower-priority decision details, until a catalog row needs them: Veterancy
  binding, target sorter partition/heap branches, ground/volume target
  resolution, Recon completed-status consumers, Advance byte `+0x39`.
- Other slot-35 blocks at `0x941F49`–`0x942253`, outside Q1's selection span: expand only if a catalog row identifies the read/output blank they fill.

- Q1a temporary ancestor-vector allocation/growth and allocator metadata: no remaining catalog meaning/freshness/output-contract blank; implementation parked.

- Q4 downstream stats networking, damage attribution internals and foreign slot29 implementations: parked after event/receiver binding; they fill no remaining morale row blank.

- Q5 inventory/ballistic range internals, UI control implementation and equipment-operation execution: parked at the first field/flag binding; resume only for a named prototype blank.

## Triage record

The old frontier's "Continue/Follow" text has 54 occurrences: 10 section
headings and 44 lead threads. 40 threads are parked. Four survive in part:
healing-cover validator (Q7), WeaponWaitReady (Q10), brain `+0x3E0` deadline
consumers (Q5) and brain factory selection (Q2).

From its worklist table these survive: morale/suppression links (Q4),
downstream squad-goal consumers (Q3), cover masks, descriptor `+8/+C` and the
healing validator (Q7), movement enum binding (Q8), environment factor and
linked-actor type (Q9), human-issued action families (Q10) and the go-to
native commands (Q11). The rest is parked.

Q1, Q2, Q6 and most of Q5 were not in the old frontier. J1–J8 were answerable
from existing notes; several were still recorded there as open.

## Q5 stop-point status — 2026-09-30

The required status summary is complete. This fulfills the instruction under
Open: next; there are no active research items. Stop here. Q6–Q11 remain in
Open: later until a prototype stage needs them. Earlier plans and intermediate
status text are retained as historical evidence; the closures below and the
catalog's later sections record the effective result.

### Closed and retained work

| Item | Status and result |
| --- | --- |
| Q1 / Q1a | Q1 seed and split retained; Q1a base reconsideration/state-dispatch inventory closed. These are evidence for the bounded catalog, not a claim of a complete native census. |
| Q1b | Partial checkpoint committed: 83 input records and 28 output boundaries for root/location, hazards and restoration. The remaining census is parked. |
| Q1z | Closed bounded prototype inventory: position, stance, cover, effective health, morale, A/B, behavior/deadline, modes/flags, subject fields, relationship and clock; selected/preferred/previous targets are distinct. The requested output interfaces and their native readers/writers are cataloged to the stated stage. |
| Q2 | Closed static behavior/deadline writer, copy/load and concrete brain-factory contracts. Human and human_client share selection but differ in refresh; A/B freshness is conditional on the scheduled job running. |
| Q3 | Closed direct-goal effect, order ownership/cancellation, idle/tactical/cover consumption and squad completion contracts. Clearing the task flag does not prove arrival. |
| Q4 | Closed morale event meanings, deltas, recipient selection and delivery/recovery phases, including health caps, casualty distribution and near-ally exchange. |
| Q5 | Closed field meanings and native writer sets: +0x14C is a scheduled weapon-range cache; +0x18 records native attack construction/issue; +0x3E0 is a deferred equipment-policy deadline; +0x39C low six bits form one inhibition counter. |

The [Q1z catalog overlay](decision-catalog-25492057.md#q1z--effective-prototype-catalog)
and appended [Q2](decision-catalog-25492057.md#q2--behavior-writers-and-concrete-brain-attachment),
[Q3](decision-catalog-25492057.md#q3--direct-goal-output-contract),
[Q4](decision-catalog-25492057.md#q4--morale-events-and-freshness) and
[Q5](decision-catalog-25492057.md#q5--prototype-field-meanings-and-native-writers)
sections are the effective contracts. Q5's checked shared constant makes the
behavior-0-to-2 morale bonus **+5**, superseding the retained +30 text in Q2/Q4.
No active prototype blank remains assigned to Q1z or Q2–Q5.

### Blanks needing runtime verification

- Establish the actual configured brain name/type, ABI, attached live objects
  and admitted callback in a mission. Static factory and borrow contracts do
  not establish an external adapter's entry conditions or thread safety.
- Establish the adapter's read/write phase relative to native selection,
  queued refresh, event delivery and order callbacks. Observe actual cadence,
  intervening writers and branch outcomes; two external field stores are not
  proved atomic by this map.
- Observe mission/mod values, initialized caches, scales and clock behavior.
  Sensor/target references can outlive actor bindings, and cached subject
  membership need not match the latest actor positions; validate admission and
  freshness at the chosen hook.
- Observe movement arrival, failure and cancellation in concrete scenarios,
  including state gates and interruptions. A goal request or cleared task bit
  is not a success receipt. Event/cache ordering and suppression/recovery
  behavior likewise require observation in the chosen integration.

No runtime verification was attempted; the game and editor were neither
launched nor modified.

### Deferred static blanks

- Q6 and Q9: preferred-query sources/return ownership; linked-target type and
  environment factor; remembered equipment lifetime, subject eligibility
  deadline and accumulator semantics.
- Q7 and Q8: cover descriptor roles, masks and handle validity, healing cover,
  full fire-mode consumer/side-effect contracts and movement-output enums.
- Q10 and Q11: complex-order sequencing/failure, secondary state events and
  outputs, and native actor/squad go-to commands.
- The rest of Q1b and Q1c–Q1g remain the single full-census deferral under
  Parked. Other parked producer internals remain parked. These are deferred
  static questions, not questions reclassified as runtime-only.

The focused game disassembly passes passed their recorded instruction, data
and RTTI checks; evidence and validation files are linked in each closed item.
Q1z and this summary consolidate existing evidence and add no disassembly,
manifest, validation file or verifier. Existing notes and artifacts, the old
frontier and the earlier compatibility work are retained. No push was made.
