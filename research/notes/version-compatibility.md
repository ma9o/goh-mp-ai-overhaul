# Checking a game update

`check_update.py` compares installed game/editor files against a versioned
compatibility profile. It reads the executables and selected resource archive
members without loading executable code. It writes a complete JSON report, a
Markdown report, and candidate TSVs for Ghidra inspection. It does not stop at
the first mismatched hash or mapping.

## Run it

The original PE scanner still uses only the Python standard library. The update
audit additionally uses pinned Capstone instruction decoding:

```sh
python3 -m venv local-work/compat-venv
local-work/compat-venv/bin/python -m pip install -r research/requirements-compat.txt
```

Set these paths to your own installation and use the build ID from Steam's local
`appmanifest_400750.acf`. SHA-256 identifies the actual input; the build ID is a
report label and never overrides a hash mismatch.

```sh
export GOH_BINARIES='/path/to/Call to Arms - Gates of Hell/binaries/x64'
export GOH_RESOURCES='/path/to/Call to Arms - Gates of Hell/resource'
export GOH_BUILD_ID='25492057'

local-work/compat-venv/bin/python research/scripts/check_update.py \
  --profile research/artifacts/build-25492057/compatibility-profile.json \
  --binaries-dir "$GOH_BINARIES" \
  --resources-dir "$GOH_RESOURCES" \
  --build-id "$GOH_BUILD_ID" \
  --output-dir "local-work/compat/build-$GOH_BUILD_ID"
```

Run the same command after each update, changing the target build ID. Keep the
old profile as the comparison baseline until the new mappings have been reviewed.
The new binaries do not need Ghidra exports for this initial audit. Both game and
editor are checked independently; a missing or malformed executable produces
per-mapping failures while the other checks continue.

Outputs:

- `report.json`: every mapping, old/new locations and extracted values, reasons,
  baseline/target fingerprints, coverage counts, and capability dependencies.
- `report.md`: the readable equivalent.
- `game.candidates.tsv` and `editor.candidates.tsv`: uniquely located function
  candidates for the existing Ghidra exporter, including changed bodies that
  need inspection. These files are not runtime hook tables.

Exit codes are suitable for a local update job or a CI runner with access to the
installed files:

| Exit | Meaning |
|---|---|
| `0` | All required static checks passed; runtime compatibility remains unapproved |
| `1` | A required mapping changed, could not be resolved, or could not be checked |
| `2` | Invalid input/profile or report-writing error; no new successful audit should be assumed |

`--strict` also returns `1` for optional coverage gaps. Without `--resources-dir`,
the named resource checks are `NOT_CHECKED` and the required gate fails. Each
capability reports its own failed dependencies, even if another capability's
static checks pass. All capabilities currently report `runtime_allowed: false`.

## What the results mean

| Status | Interpretation |
|---|---|
| `UNCHANGED` | The recorded static checks pass at the same location |
| `RELOCATED` | The recorded checks pass at a different location |
| `CONTRACT_CHANGED` | A recorded code, data, operand, or structural assumption differs; inspect its meaning |
| `UNRESOLVED` | The locator found no usable candidate |
| `AMBIGUOUS` | The evidence does not select one candidate |
| `NOT_CHECKED` | Missing input, insufficient entry evidence, or no implemented contract |

`CONTRACT_CHANGED` does not assert a gameplay change. Compiler changes can also
change a recorded code contract. Conversely, `UNCHANGED` is not proof of complete
semantic equivalence. Similar instruction bodies can call different routines or
read different mutable globals.

The current profile snapshots six reviewed maps: initial/reviewed, fine, flow,
tactics, systems, and perception. It contains 337 required checks and 185 optional
coverage records. The required checks include 220 named method records, six code
sequences, 62 RTTI/vtable records, ten enum tables, seven resource members, and
32 selected operand checks across game and editor. Repeated method records retain
their source-map identities; these counts are not counts of unique functions.
Later research added to those source maps is not silently included in this
immutable snapshot. Source hashes identify exactly what was captured.

The selected operand checks cover the actor's brain pointer, pending-update flag
set/clear operations, reconsideration slot displacement, order subobject access,
advance/retreat ratio bindings and consumers, owner pointer, and movement-mode
and speed offsets, shifts, and masks. Broader field interpretations and inlined
decision semantics remain separate coverage gaps, including when a narrower
operand test exists for the same source record.

## Matching and its limits

Functions are located using RTTI/vtable relationships, direct-call contexts in
independently anchored virtual methods, or unique instruction signatures. A
signature-only candidate on a changed binary also needs unwind entry evidence;
small leaf functions without such evidence require review. A vtable slot alone
does not establish method identity: the checker also compares the recorded body.

Fingerprints preserve opcodes, member displacements, scalar constants, masks,
and internal branch displacements. They normalize RIP-relative address fields
and relative calls/jumps outside the recorded body. Selected referenced immutable
constants and strings are checked separately. Remaining external call targets,
mutable globals, transitive callees, calling conventions, and ownership are not
automatically proven.

Ghidra body ranges define the comparison windows because an x64 unwind record
may describe only a fragment. Operand and call-site locators search those
baseline-relative windows at the candidate location and require decoded
instruction starts. Recompilation, outlining, inlining, or substantially changed
body boundaries can require fresh Ghidra analysis. This version has no fuzzy
function matcher or automatic rewriting of field contracts.

Enum locators use ordered names rather than old pointer bytes. A unique table
with changed values is reported as changed. Multiple matching tables remain
ambiguous, even if one happens to contain the old values. Vtable extents are
executable-pointer scan heuristics. Archive checks cover the named installed
members, not effective mod override/load order.

## Review and advance the baseline

1. Keep the last reviewed profile and local baseline binaries/exports available.
2. Scan and import the new build with `scan_pe.py` and `run_headless.py` under its
   own build ID, as described in the README.
3. Export the audit's located candidates, then investigate unresolved entries
   with fresh RTTI, strings, and Ghidra version tracking or a binary diff tool.
   For example, after scanning/importing the new game:

   ```sh
   python3 research/scripts/run_headless.py --build-id "$GOH_BUILD_ID" export \
     --program call_to_arms.exe \
     --candidates "local-work/compat/build-$GOH_BUILD_ID/game.candidates.tsv" \
     --output "local-work/exports/build-$GOH_BUILD_ID/compat-game"
   ```

4. Review changed methods and field evidence, update the new build's maps, and
   create a new operand-contract file with independently checked instruction
   sites. Do not relabel a profile by replacing its binary hashes.
5. Generate a new static profile with the command below and audit that profile
   against its own baseline files. The builder refuses to overwrite a profile.
6. Once a callable runtime adapter exists, execute the controlled scenarios in
   [runtime-smoke.md](../compatibility/runtime-smoke.md) before approving runtime
   use. A passing static audit cannot grant that approval.

The initial profile can be reproduced into a new output path with:

```sh
local-work/compat-venv/bin/python research/scripts/create_compat_profile.py \
  --baseline-dir research/artifacts/build-25492057 \
  --binaries-dir "$GOH_BINARIES" \
  --exports-dir local-work/exports/build-25492057 \
  --contracts research/compatibility/contracts-25492057.json \
  --output local-work/compat/reproduced-profile.json
```

The builder requires exact baseline fingerprints and corresponding Ghidra
exports. Schema-2 exports provide body ranges directly. For older schema-1
exports, every assembly instruction start is decoded from the fingerprinted PE,
and the combined size must equal Ghidra's exported body size. Raw data sequences
are not guessed to be executable code.

## Stable integration boundary

`schema_version: 1`, symbolic mapping IDs, and capability IDs are the stable
interface supplied by this change. Capabilities currently group update
observation, ratio access, and movement modes separately for game and editor.
The reports diagnose which groups need work; they do not enable or disable
features inside the inherited loader.

A future Lua/C++ adapter should own the reviewed native addresses and layouts,
identify the loaded executable by its exact fingerprint, and expose operations
and opaque handles to scripts. Scripts should use that adapter's operations and
capability queries. Candidate TSVs, raw RVAs, and guessed layouts are research
artifacts. The inherited Windows SDK still has the ABI/lifecycle gaps documented
in [the upstream audit](upstream-audit.md); wiring candidates into it is not part
of a successful static check.

## Tests

```sh
local-work/compat-venv/bin/python -m unittest discover -s research/tests -v
```

Synthetic PE mutation tests cover relocation, offset/mask/string changes,
ambiguous signatures and enums, vtable reordering, missing/malformed inputs,
resource-only changes, partial unwind records, direct-call anchors for leaf
functions, legacy export bounds, and CLI report/exit behavior. They require no
game installation. Without Capstone, the compatibility test module is explicitly
skipped; install the pinned requirement to run these checks.
