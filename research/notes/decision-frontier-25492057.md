# Decision frontier — build 25492057

This file replaces the [static frontier](static-frontier-25492057.md) as the
working plan for static research. That file remains as a historical log; do not
take leads from it directly. Addresses are **game** RVAs and offsets are
hexadecimal. Everything here is static evidence, not runtime verification.

## Objective and scope rule

Map the infantry brain's decision logic, its inputs to the depth of meaning,
and its outputs to the depth of contract.

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
- Take work only from the open lists below, one item per pass, in order.
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
- When no open items remain, add a completion summary: what is closed, which
  blanks need runtime verification, and what is parked.

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

## Open: next

The Q1 splits below finish the catalog **inventory**. Each fills its named
coverage row's read-site/access-path and issued-output inventory blank by adding
individual variable/output rows with all five columns, using existing evidence
first. Do not treat a coverage row as a substitute for those individual rows.
Contract details not established by consolidation stay explicit and are triaged
by Q1g; Q2–Q11 retain their scope and order afterwards.

### Q1a. Base reconsideration and state dispatch catalog

Catalog row **C-BASE**, blank: per-variable inputs and issued outputs of
`0x903B10` and the state dispatch/interrupt boundaries
`0x912110/170/1C0/200/260/370/3D0/460/4D0/520` in the movement manifest.
Enumerate individual `+0x39C` bits, queue/state pointers, aggregate inputs,
external query results and emitted events. Reuse I-THREATENING/I-ATTACKING.
Stop at query/output contracts; do not expand state bodies assigned below.

### Q1b. Root, location and hazard policy catalog

Catalog row **C-ROOT**, blank: per-variable inputs and issued outputs of
soldier/civilian root and location selection, root interrupt `0x955A90`, and
hazard-scanner query interfaces in the movement manifest. Include saved-order
and restoration fields; reuse I-COVER-GROUP. Order execution, physical escape
and path internals are outside this census.

### Q1c. Idle, following, healing and medic policy catalog

Catalog row **C-IDLE**, blank: per-variable inputs and issued outputs of idle
Interrupt/Execute and their following/healing/medic/explicit-boarding policy
helpers in the human-states manifest. Include task/mode/follow flags, timers,
health queries and patient-selection interfaces. Reuse O-DIRECT-GOAL where
appropriate; record factory contracts without tracing animation or health
implementation internals.

### Q1d. Battle, cover and tactical movement policy catalog

Catalog row **C-BATTLE**, blank: per-variable inputs and issued outputs of
outdoors/cover battle Interrupt/Execute and tactical movement/cover helpers in
the human-states and tactics manifests. Include descriptor/mask fields and
cover-scoring query interfaces, keeping current-value reads distinct from
hypothetical queries and their side effects. Reuse I-BEHAVIOR and O-DIRECT-GOAL.

### Q1e. Find-enemy and Recon interface catalog

Catalog row **C-SEARCH**, blank: per-variable inputs and issued outputs of
`0x94C410/750/8A0`, look-point policy `0x94DD50`, and their Recon query
contracts. Include current/pending phases, zone handles, assignment, deadlines
and results. Reuse I-SEARCHED; distinguish its bit from zone completion and
do not reopen Recon implementation once its query contract fills the row.

### Q1f. Target, stance and equipment policy helper catalog

Catalog row **C-HELPERS**, blank: remaining per-variable inputs and issued
outputs of target refresh/picking/ranking/eligibility, attack/stance and
weapon/pickup policy helpers in the six Q1 manifests. Record hypothetical
geometry/visibility query inputs, returns and side effects at their boundary.
Reuse I-TERRAIN/I-LINKED/I-TARGETABLE/I-OPERATABLE, and create the precise
variable/query rows needed by Q5–Q9. Do not map their producers past the first
step that fills a meaning or freshness blank.

### Q1g. Output contracts and catalog reconciliation

Catalog row **C-OUTPUTS**, blank: complete issuing-call coverage and five-column
output contracts for the setters, direct-goal path and factories used by the
enumerated methods. Consolidate engagement-orders only to effect/result/
ownership/cancellation/side-effect depth. Link Q2–Q11 to exact variable/output
rows, reconcile the family inventories and assign row-specific follow-ups for
remaining static meaning/freshness/validity blanks. Runtime-only blanks must
say why. Done when the original Q1 completeness condition is checkable; no
producer, physics or animation internals are reopened merely for completeness.

### Q2. Everything that can change the behavior state between decisions

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

### Q5. Unnamed brain fields read by decision code

Blank: meaning, then freshness.

- `+0x14C`: configured radius. Target ranking `0x9136E0` drops farther
  candidates when any lie inside it; Advance's subject gate `0x8D1C50` and
  engagement test `0x8D2120` use it.
- `+0x18`: timestamp whose age Advance `0x8D25B0` compares with five units.
- `+0x3E0`: human deadline with unmapped consumers (old frontier lead).
- `+0x39C` bits read by decision code without a name (Q1 lists them).

Done when each has a binding name or an explicit writer set.

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
