# Combat estimates and cover selection

This pass continues the [input-to-order trace](flow-map-25492057.md) using only
the installed build 25492057 files. It identifies the two score channels, traces
the cached friendly/self estimate, and separates cover scoring, rejection and
selection. The game and GEM were not launched or changed.

Addresses below are RVAs. The checked manifest is
[tactics-map.json](../artifacts/build-25492057/tactics-map.json).

## The score pair is Human / Tank

The previous report used P and Q for two unnamed float pairs. Their channel
names are now supported by configuration-reading code in both executables:

| Operation | Game | Editor | Observed behavior |
|---|---|---|---|
| Load comparison table | `0x812A40` | `0x695560` | Read `hitpoints`; store `human` then `tank` at record `+0x330/+0x334` |
| Load attack table | `0x813110` | `0x695C70` | Read `attackrate`; same two channel offsets |
| Attack lookup | `0x813C20` | Not followed this pass | First matching property filter wins; no match gives `(0,0)` |
| Refresh subject | `0x878740` | `0x6FC3E0` | Fill P at `+0x48`, range at `+0x50`, Q at `+0x54` |

Both tables use `0x338`-byte records. The attack lookup matches object properties
at `+0xEC8` through `0x556F20`; the comparison lookup uses a different entity
property source. The full property-expression language is still unreviewed.

The installed `resource/gamelogic.pak` contains
`set/registry/hitpoints.reg`, with **HitPoints** and **AttackRate** sections.
For example, the archive's AttackRate pair for `weapon mgun` is `(4, 0.1)`;
for `weapon rifle`, `(1, 0.1)`; for `weapon at_rifle`, `(0.2, 1)`.
HitPoints has `(0, 2)` for `tank` and `(3, 0)` for `vehicle`.
These are class-based coefficients used by the AI estimate. The label
AttackRate does not establish literal damage per second or weapon accuracy.

The earlier human special case still matters: Q for a human reads a value via
`Human::eParams_Health` and sets its Tank channel to zero. It does not simply
use the archive's human row on that path.

The archive member's bytes are fingerprinted in the manifest and checked by
the verifier. This is evidence of installed configuration, not proof of the
active configuration after registry loading or mod overrides. The executable
parsers establish the key-to-channel connection independently of that file.

### The cached pair can combine different weapons

For a subject with flag `+0x14` bit 2 set, refresh takes P from the subject
actor's component `+0x35C`, rather than the selected equipment lookup.
`0x875860` passes that exact address and the component's owner into `0x813870`.

`0x813870` starts at `(0,0)`, examines eligible inventory/equipment items and
takes a **component-wise maximum** of their AttackRate pairs. It does not sum
all weapons, and the two maxima need not come from the same weapon. For
example, eligible `(4,0.1)` and `(0.2,1)` entries would yield `(4,1)`.
Inventory, availability and ammunition-related helper conditions still need
their own contracts; the statement applies to items passing those conditions.
The callback's cadence is also unresolved.

This sharpens the previous A/B interpretation: the native brain compares
weighted Human/Tank combat estimates, with a different source for classified
support/self records and other perceived records.

## How relationship flags are produced

`0x878560` updates subject flags at `+0x14`. Its input is the owner component and
the subject, not a brain pointer. It sets bit 0, then calls `0xA467B0` with
owner, subject actor and the old bit 5 as a boolean selector.

| Bit | Condition written by this updater |
|---|---|
| 0 | Set on entry to this classification update |
| 1 | Relationship result is 1, actor exists, and its brain `+0x398` bit 15 is set |
| 2 | Relationship result is 2 |
| 3 | A second relationship call, with selector true, returns 2 |
| 4 | Actor exists and either actor `+0xDB4` bit 8 is clear or actor `+0x38` bit 9 is set |

`0xA467B0` returns 2 when owner `+0xDA8` matches the subject's selected ID
(`+0xDAC` or `+0xDA8`) or its `+0xDA8` ID. Otherwise, eligible IDs below 18
index a mutable relationship matrix; invalid/ineligible cases return 0.
This supports a same-side interpretation for bit 2. The matrix initialization,
ID field names and brain bit 15 meaning remain open, so result 1 is retained
numerically rather than asserted to mean every possible hostile relationship.

The earlier threat/support filters now have a producer, but complete list
membership is not mapped. `0x8753C0` inserts new records into component
`+0x268 + category*0x18`, and stores the category in subject `+0x10` bits 21–22.
Categories 1 and 2 lead to the lists used by A/B. Every category-assignment
caller has not yet been traced.

## Cover search is several separable operations

The reviewed movement path builds a search context, then:

```text
collect cover candidates
  -> calculate component scores
  -> reject invalid candidates
  -> sort surviving candidates by descending total score
  -> choose a ranked candidate probabilistically
  -> copy its 0x88-byte descriptor into the movement-order path
```

The functions are `0x89FEB0` (search), `0x8A0ED0` (score/filter),
`0x8A4680` (sort), `0x8A0660` (selection), and `0x951210` (adapter).
Search `+0x120` enables scoring/sorting and is set by the initializer.
Different search flags control candidate sources; their complete meanings
are not recovered here. The scorer's threat set also differs from A/B:
it requires `(subject.flags14 & 0x402) != 0`, a non-null actor, and bit 4 clear.
It can additionally insert a synthetic subject when search `+0x12C` is set.

### Eight weighted terms, plus a direct priority

`0x8A0ED0` stores a total float at candidate `+0x80`. Search weights are eight
floats at `+0x170..+0x18C`; they multiply these candidate terms:

| Weight | Candidate field | Static interpretation |
|---|---|---|
| `+0x170` | `+0x5C` | Inverted, normalized path cost; larger favors cheaper reachable positions |
| `+0x174` | `+0x60` | Current-target screening term: 1 or 0.5 under a detector-flag gate; otherwise initialized zero |
| `+0x178` | `+0x64` | Geometrically screened fraction of weighted Human-channel threat estimates; normally 1 when the denominator is zero |
| `+0x17C` | `+0x68` | Detector-flag score of 0, 0.5 or 1; special handling for candidate kind 3 |
| `+0x180` | `+0x6C` | 1 if candidate-to-target distance is greater than `7*unit_scale` and less than selected-equipment range, with owner/target gates |
| `+0x184` | `+0x70` | 1 if no collected threat marks the candidate as inside its range; otherwise 0 |
| `+0x188` | `+0x74` | Boolean from candidate `+0x30/+0x34`; exact metadata meaning unresolved |
| `+0x18C` | `+0x78` | Candidate ID at `+0x18` equals search ID at `+0x190` |

Candidate `+0x7C` is added directly without a weight. Where a linked cover
record exists, it is populated from that record's signed 16-bit `+0x78`.
Its designer-facing name is unconfirmed. The formula is:

```text
total = w184*c70 + w174*c60 + w178*c64 + w17c*c68
      + w170*c5c + w180*c6c + w188*c74 + w18c*c78 + c7c
```

The ordering shown follows the floating-point additions in the game assembly.
Editor `0x725C30` uses the same terms and offsets, with the first two summands
exchanged. This is a structural/behavioral comparison, not a whole-function
equivalence proof.
Scores are initialized by `0x8A5970`, which clears `+0x54..+0x80`.
The scorer does not populate every term identically for every candidate kind.

The path term comes from `0x8A1EF0`: a path-network evaluation supplies raw
costs; missing/invalid nodes receive 100000. For finite values it transforms
each cost with `1-clamp(cost/D,0,1)`, where
`D=max(1,min(search_budget,max_valid_cost+1))` and the search budget is
`(distance(origin,search_center)+search_radius)*search.+0x148`.
Candidates with resulting term zero are removed if its weight is positive.
This is a concrete path-cost rejection, not just a scoring preference.

The screening term uses the first/Human component from `0x876F60`, the same
position/age weighting helper used in A. It compares contributions that pass
geometric checks against the total. `0x8A55A0` tests 2D segment intersection
with epsilon handling; other candidate kinds use additional helpers. This
does not yet establish complete bullet collision or line-of-sight behavior.

### Retreat and advance actually change the weights

In the movement routine, search construction first clears all eight weights.
It then sets two common weights to 10 and adds two state-specific weights:

| Brain state | Nonzero weights in this path, all equal to 10 |
|---|---|
| 0 or 1 | Path cost `170`, screening fraction `178`, detector score `17C`, outside-threat-range `184` |
| 3 or 4 | Path cost `170`, screening fraction `178`, current-target screening `174`, own engagement-range term `180` |
| Other | The two common weights, if this setup is reached |

This configuration is inlined into game `0x9538A0` at `0x954060..0x9540E6`.
The editor keeps the counterpart in a separate routine, `0x7DBC00`.
These are weights for this caller, not universal cover-search defaults.

`0x9516E0` fills the separate bounding circle at search `+0x150` using leader
or movement-zone state. Despite the initial investigation label, it does
**not** set the score weights. Its effect below candidate collection remains
a useful next static lead.

### Direction validation returns true to reject

RTTI names the callback **`Human::eDirectionValidator`**, derived from
`Cover::Validator::eBase`. Primary slot 0 is game `0x955800`, editor `0x7DCCC0`.
The caller removes a candidate when this callback returns true.

For an ordinary nonzero XY displacement, let `d` be its length, `u` its unit
direction, `m=validator.+0x10` and `v=validator.+0x14/+0x18`. The rejection rule is:

```text
d < m OR (m > 0 AND dot(u,v) < 0.5)
```

With a unit-length requested direction, the second test describes a 60-degree
half-angle cone. When squared displacement is at most float32(0.00001), the
code uses zero distance and a fallback vector from mutable global data.
On acceptance, candidate kind `+0x04 == 3` also gets byte `+0x48 = 1`.
The callback therefore has a side effect as well as a predicate result.

### Selection favors rank, not score magnitude

The sort comparator at `0x8A2E60` is a tiny, unrecognized leaf body:
`candidate_a.score80 > candidate_b.score80`. Its exact bytes are recorded and
independently disassembled. The score gap between candidates does not appear
in the subsequent selector.

`0x951210` passes **0.5** to `0x8A0660`. The selector repeatedly partitions
the remaining random interval, generating geometric rank preference, then
clamps the rank to the last candidate. Assuming the recovered initial random
scale and uniformly distributed generator states, this is approximately
50%, 25%, 12.5%, ... by descending rank, with the remaining tail assigned to
the last candidate. Finite precision and inclusive boundaries matter for
exact probabilities. Tied-score ordering is not established.

The scale is not guessed: leaf code at `0x61060` writes
`1 / 16777216` to `0x1584DAC`, with a function pointer at `0xFF8D98`.
The selector multiplies that scale by the generator's upper 24 bits.
These are static initialization and use sites; an observed runtime
distribution or absence of later writes is not claimed.

## What this gives a replacement brain

We can now describe separate candidate interfaces for combat estimation,
cover collection, score construction, rejection, rank selection and order
creation. A custom policy could eventually supply its own priorities or
destination while reusing selected native mechanisms. None of these addresses
is yet a verified callable API: context construction, object lifetimes,
thread/timing assumptions and final order acceptance remain open.

Further static work should focus on candidate-source flags, the bounding-zone
filter, sensor/visibility terms, classification-matrix initialization and the
callback scheduler. Runtime remains useful later for checking actual list
contents, configured values and accepted orders, not required to recover the
contracts documented here.

## Reproduce and verify

Run sequentially against the existing Ghidra project. Set `GOH_BINARIES` as in
the README and `GOH_RESOURCES` to the installed game's `resource` directory.

```sh
python3 research/scripts/run_headless.py --build-id 25492057 export --program call_to_arms.exe --candidates research/artifacts/build-25492057/call_to_arms.tactics-pass.tsv --anchors research/artifacts/build-25492057/call_to_arms.tactics-anchors.tsv --output local-work/exports/build-25492057/call_to_arms-tactics
python3 research/scripts/run_headless.py --build-id 25492057 export --program call_to_arms_ed.exe --candidates research/artifacts/build-25492057/call_to_arms_ed.tactics-pass.tsv --anchors research/artifacts/build-25492057/call_to_arms_ed.tactics-anchors.tsv --output local-work/exports/build-25492057/call_to_arms_ed-tactics
python3 research/scripts/verify_fine_map.py --map research/artifacts/build-25492057/tactics-map.json --binaries-dir "$GOH_BINARIES" --resources-dir "$GOH_RESOURCES" --exports-dir local-work/exports/build-25492057 --output research/artifacts/build-25492057/tactics-validation.json
```

The report checks binary/export fingerprints, exact entries, instruction
locations, constants, RTTI, small leaf bodies and the archive member hash.
All 24 game and 11 editor requested candidates were exact function entries;
the exports completed 60 game and 35 editor decompilations. The new map checks
173 instruction evidence locations. LLVM independently agreed with Ghidra on
110 instruction boundaries across the two direction validators and the cached
pair bridge; both sort-comparator leaves and the random-scale initializer
were also independently disassembled.

The six PE scanner tests and all three map verifications passed. Negative
checks rejected an incorrect resource hash, resource size, comparator byte
sequence, a mid-instruction address and a missing resource directory.

These checks do not prove the semantic interpretations. Decompiled code stays
local and game configuration contents are not committed; the committed material
consists of research notes, small evidence manifests and tooling.
