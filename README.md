# Gates of Hell AI research

Static analysis of **Call to Arms: Gates of Hell — Ostfront**, starting from
[NorthHead-Digital's script extender](https://github.com/NorthHead-Digital/coa_script_extender)
at `d994e1750de1c015ed2557250ab461ece73cd721`.

The goal is to recover enough of the native actor/brain/order interfaces to build
our own tactical decision system: cover, attack/retreat, target selection, and
weapon behavior. The upstream loader and SDK are retained as reference material.
**This is a research fork, not an updated or working extender release.**

## Start here

- [Upstream audit](research/notes/upstream-audit.md): what is implemented, missing,
  or unverified in the inherited SDK.
- [Current build findings](research/notes/build-25492057.md): evidence from the
  installed game and GEM editor, with addresses separated by executable.
- [Fine-grained brain map](research/notes/fine-map-25492057.md): field masks,
  enum values, small predicates, setter side effects, and inlined state rules.
- [Input-to-order trace](research/notes/flow-map-25492057.md): subject-score
  formulas, the morale connection, and state-dependent movement/cover orders.
- [Combat estimates and cover selection](research/notes/tactics-map-25492057.md):
  Human/Tank coefficients, relationship flags, cover weights and rejection rules,
  and the randomized choice among ranked candidates.
- [Build artifacts](research/artifacts/build-25492057): fingerprints, RTTI,
  vtables, string anchors, and candidate methods.
- [Original upstream README](README.upstream.md): historical build and loader
  instructions. Its compatibility and verification claims have not been
  reproduced here.

## Reproduce the structural scan

Python 3.10+; no third-party Python dependencies. Point `GOH_BINARIES` at your
own installed game's `binaries/x64` directory. Use the Steam build ID from your
local `appmanifest_400750.acf`.

```sh
export GOH_BINARIES='/path/to/Call to Arms - Gates of Hell/binaries/x64'
python3 research/scripts/scan_pe.py --binary "$GOH_BINARIES/call_to_arms.exe" --build-id 25492057 --output-dir research/artifacts/build-25492057
python3 research/scripts/scan_pe.py --binary "$GOH_BINARIES/call_to_arms_ed.exe" --build-id 25492057 --output-dir research/artifacts/build-25492057
python3 -m unittest discover -s research/tests -v
```

The scanner reads files without executing them. It checks PE section boundaries,
x64 unwind ranges, relative MSVC RTTI locators and their self-references, class
hierarchies, and vtable code pointers. A structural match is a lead, not proof of
a function's behavior or calling convention.

## Ghidra workflow

Test environment: Ghidra 12.1.4, OpenJDK 21, macOS arm64. The wrapper defaults to
the local Homebrew installations; use `--ghidra-home` and `--java-home`, or
`GHIDRA_HOME` and `JAVA_HOME`, on another machine.

```sh
python3 research/scripts/run_headless.py --build-id 25492057 import --binary "$GOH_BINARIES/call_to_arms_ed.exe" --binary "$GOH_BINARIES/call_to_arms.exe"
python3 research/scripts/run_headless.py --build-id 25492057 export --program call_to_arms_ed.exe
python3 research/scripts/run_headless.py --build-id 25492057 export --program call_to_arms.exe
```

Imports create `local-work/ghidra/GoH_25492057.gpr`. Open this project in Ghidra
after headless work finishes. Ghidra rejects project paths containing hidden
directory elements, so the project is deliberately outside `.work/`.

Exports check the imported executable's SHA-256 against the survey, require
exact function entries for candidate RVAs, and collect references, calls,
assembly, and decompilation. Schema-2 exports also include body ranges, basic
blocks, and outgoing flow edges so that inlined decisions can be mapped.
They do not rename functions or apply guessed types. Additional RVAs can be
supplied in a two-column TSV (`RVA`, `label`) via
`export --candidates path.tsv --output local-work/exports/followup`.
An optional `--anchors path.tsv` follows references to additional data/string
RVAs. The `members` command searches literal memory displacements across all
analyzed instructions; matches need manual pointer/type review. See the
[input-to-order workflow](research/notes/flow-map-25492057.md#reproduce-and-verify).

The checked-in `call_to_arms.followup.tsv` and `call_to_arms_ed.followup.tsv`
add the flag-clearing wrapper and actor dispatch path identified during the
first review. For example:

```sh
python3 research/scripts/run_headless.py --build-id 25492057 export --program call_to_arms.exe --candidates research/artifacts/build-25492057/call_to_arms.followup.tsv --output local-work/exports/build-25492057/game-followup
```

The `*.fine-pass.tsv` files extend the investigation into predicates, mode
parsers, target selection, and state transitions. The
[fine-map reproduction commands](research/notes/fine-map-25492057.md#reproduction-and-checks)
also run `verify_fine_map.py`, which checks binary hashes, enum tables, constants,
and instruction evidence locations. Maps with `resource_members` also require
`--resources-dir` and verify the named archive members by size and SHA-256.
Passing these checks does not prove the
analyst's semantic interpretations or a callable ABI.

Game binaries, Ghidra databases, logs, and decompiled bodies stay untracked in
`local-work/`. Commit tools, small address/evidence manifests, and analyst notes.
The importer does not overwrite existing programs; use a new build ID after a
game update and regenerate the survey before comparing it.

## Evidence rules

1. **Structural:** RTTI, references, vtables, or unwind records identify an
   address relationship. This does not identify the method's purpose.
2. **Static behavior reviewed:** disassembly/decompilation and callers support
   a proposed role. State the evidence and unresolved ABI/lifetime details.
3. **Runtime verified:** observations on the exact fingerprinted executable
   confirm the call, object, thread, and lifetime assumptions. Nothing in this
   research has this status yet.

All addresses in new reports are **RVAs** unless explicitly labeled otherwise.
Add the executable's module base to obtain a virtual address. Game and editor
addresses must be mapped independently; a matching vtable slot is a comparison
anchor, not a compatibility guarantee. Do not transfer research candidates into
the loader's hook table before their signatures and lifecycle are established.

Map the smallest behavior supported by evidence: individual bit fields,
predicates, score calculations, state transitions, and side effects, including
operations inlined into larger functions. Record unknown meanings explicitly;
pair game/editor behavior even when compilation changes function boundaries.

The upstream repository mentions MIT but contains no standalone license file;
this fork preserves that state and does not assign a new license to upstream
code or game-derived material.
