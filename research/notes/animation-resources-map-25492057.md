# Animation resource format, time coordinates and marker audit

Build25492057, static review only. Addresses are RVAs, offsets hexadecimal and
virtual slots decimal. This extends the
[animation control](animation-control-map-25492057.md),
[human profiles](human-animation-profiles-map-25492057.md) and
[idle policy](idle-actions-map-25492057.md) passes. It connects animation names
to binary timelines and bone inputs, which constrain movement, weapon actions,
boarding, stance transitions and their interaction markers.

The machine-readable contracts are in
[`animation-resources-map.json`](../artifacts/build-25492057/animation-resources-map.json).
[`human-animation-resources.json`](../artifacts/build-25492057/human-animation-resources.json)
contains file hashes, shared bone layouts, compressed frame-coordinate runs,
declaration joins and anomalies. It contains no mesh payloads or full pose arrays.
The game/GEM exports are read-only; neither executable was run or patched.

## Resource loading and declaration lookup

`5D0B40 / 447ED0` parses sequence definitions. Its default file expression is the
animation name; `file` replaces it. `6749A0` lowercases ASCII and changes backslashes
to slashes; non-ASCII folding is delegated to another helper. `674C10` determines
whether the expression denotes a mounted directory. A file expression gets `.anm`
appended unless its extension already equals `.anm`. A directory expression uses
`666AC0/666C10` to enumerate immediate children with that extension, excluding
deeper descendants. Iterator order and mounted precedence remain open.

Each selected path goes through `663AF0`: repository lookup, lazy creation under
a critical section when required, cached resource reuse and reference retention.
The sequence parser requires the result to cast to `eBinaryAnimation`. Its variant
vector is at `+80/+88/+90`. Factory `673B90` case4 constructs the F0-byte binary
animation object. The broader extension registry and repository lifetime are only
partially reviewed here.

Blend parser `5CED80 / 445FB0` takes a `pose` filename, appends `.anm`, loads the
binary resource and chooses its beginning or end. This path does not perform the
sequence directory-variant expansion.

Definition lookup `5CD280` searches registered definitions at animation-system28.
Request `5CD380` does not automatically register a missing name from the filesystem;
the prior completion fallback still applies. `5CD7E0 / 4448E0` creates declarations
and rejects duplicate names. Explicit insertion `5CD5D0 / 4446A0` also rejects
duplicates. Its observed direct caller `C5BE0 / B6990` constructs nested blendspace
definitions with generated names such as `%s(%g)`. That is not evidence for
automatic registration of the two unmatched native idle names.

All1545 base declarations join to1292 distinct files when names are resolved
relative to `properties/animation/human` in the installed `properties.pak`.
43 declarations join to immediate-child directory sets. This is an explicit
archive-relative assumption, not a reconstruction of the engine's mount table.
`stand_idle_empty` and `stand_idle_strap_4` still have no declaration in the base
catalog; their bare filenames are also absent from this archive scope. The
script's numbered empty-idle choices are declared. No claim about all mods or
runtime definition overrides follows from this join.

## Binary animation object and stream

Loader `5D8CF0 / 450170` reads the remaining resource stream into a buffer and calls
`5D9290 / 450740`. Parsing copies retained data into owned vectors/payloads. The
temporary input buffer is released afterward. `5D8B60 / 44FFE0` frees the loaded
frame-action mesh allocations and vectors during binary-resource destruction.

| Object offset | Meaning |
|---|---|
|10|Declared last coordinate: FRMS count minus1|
|18/20/28|Vector of16-byte stored-frame records|
|30/38/40|Vector of70-byte bone records|
|48,50,B0|Placement mirror byte, name token and pose-track index|
|B8/C0/C8|Vector of32-byte quaternion/translation poses|
|D0/D8/E0|Vector of16-byte frame-action records|
|E8|Pose-track count allocated during first stored frame|
|EC|At least one binary mesh payload was loaded|
|ED|Shared binary has had its one-time rotation attempted|

The little-endian stream begins `EANM`, signed version at least30000, `FRMS`,
positive signed declared count, then `BMAP`. Versions are hexadecimal here.
The loader subsequently accepts `FRMN` or `FRM2` chunks and rejects other tags.
It also rejects an empty stored-frame vector. FRMS is a coordinate bound, not a
count of the actual frame chunks.

Bone parser `5D9610 / 450AC0` accepts fewer than256 bone entries. Each has a signed
byte count and name bytes. The first `_lod` substring splits the visible name from
a numeric LOD suffix. Helper `69A540` initializes the result to0, calls an integer
parser with base0, and assigns only after some input was consumed and its error
state is not22. General CRT number syntax is not reproduced by our scanner; its
accepted suffix subset is unambiguous decimal. All scanned human LOD values are0.
The runtime bone record holds mirror at0, token at8 and pose-track index at68,
initially-1. LOD values are kept separately while loading and copied as a byte
into frame actions.

## Sparse pose tracks and frame actions

`5D9AF0 / 450F90` checks `0 <= coordinate <= binary10`, appends a frame record,
sets its pose offset to the current pose-vector length, then copies all cached
poses. A frame record is `(coordinate, pose_offset, action_offset, action_count)`,
four32-bit fields. Offsets count records, not bytes. No monotonicity check or
sorting was observed in this loader.

`5D9BA0 / 451040` allocates a pose track **only while exactly one frame is stored**.
Each pose update in that first frame appends a default pose, assigns the bone's
track index and mirror byte, and increments binaryE8. File-initial default globals
are quaternion `(1,0,0,0)` and translation `(0,0,0)`; those globals are writable.
Later updates use the previously assigned index and write both the frame pose and
the cache. They do not allocate tracks for a newly encountered bone. Unchanged
rotation or translation is therefore carried forward. Bone-map order and
pose-track order are separate.

`FRMN` parser `5D9DB0 / 451270` reads an int32 coordinate followed by `BONE` plus
int32 bone index and one or more subchunks:

- `MATR`:12 floats, a3x3 matrix followed by translation. It checks the cross product
  of the first two rows against the third, using epsilon0.001. A reflected matrix
  negates the third row and sets the mirror flag. Other non-orthogonality warns
  once per loader. It converts the matrix to a quaternion, normalizes it and stores
  both quaternion and translation.
- `VISI`: int32 converted to boolean visibility.
- `MESH`: signed byte count followed by copied opaque mesh bytes; sets binaryEC.

Within one BONE block, VISI and MESH share a frame-action record. That record holds
mesh pointer0, bone-map ordinal8, visibility9 (`0`hide, `1`show, `2`unchanged),
and LOD byteA. The scanner does not decode mesh bytes or duplicate native matrix
conversion. Its reflection classification uses wider Python arithmetic.

`FRM2` parser `5DA3C0 / 451880` reads uint16 coordinate and uint8 update count.
Each update starts with uint8 bone index and uint16 flags:

| Mask | Payload / behavior |
|---|---|
|1|Three translation floats|
|2|Three quaternion components; first component becomes `sqrt(abs(1-sum squares))`|
|4|Mirror flag when first-frame pose track is allocated|
|8/10|Visibility update when either bit is set; bit8 means show, otherwise hide|
|20|Signed mesh byte count and payload|

Payload order is translation, quaternion, mesh. Quaternion and translation writes
share one track-binding call when either mask1/2 is set. Visibility and mesh share
one frame-action record for that update. Higher bits have no reviewed payload
effect; the scanner records them as anomalies rather than inferring semantics.

After parsing, the first bone named `placement` supplies binary48/50/B0. This
connects the format to the placement-driven entity transform path mapped earlier.

## Rates and shared rotations

Sequence factory `5D40E0 / 44B4F0` initializes raw speed to1. After parsing properties,
`5D0B40` multiplies it by `50 / mutable14776C8` and stores it at definition78.
The file-initial clock conversion scale is1000, giving a default normalized
rate0.05. This is a coordinate rate in the controller's units; the full clock,
pause and time-scale producer remains open. The existing duration getter is
`(last_coordinate+1)/(controller_scale*normalized_rate)`, not remaining duration
or a dynamic-speed integral.

Dynamic-speed rows begin with `(-1e38,0)`. Each `(start,end,factor)` scales factor
by50/14776C8, replaces the previous start if it is within approximately1e-5, or
appends a new start, then appends `(end,0)`. After resource loading the parser adds
`(1e38,0)`, replaces all rates<=0 by the normalized default and rejects
non-increasing coordinates. These are definition108 vector rows of8 bytes.

`rotate` stores a bone name, axis and angle converted from degrees to radians.
For a nonzero angle/nonempty name, the sequence parser invokes `5D8EF0 / 4503A0`
only when binaryED is clear, then sets ED even if no matching bone exists. Thus
the gate is on the shared cached resource. The helper uses a bone-map ordinal from
`5DBD00 / 4533D0` directly as a pose offset; it does not read bone68. This assumption
needs attention in any replacement loader or transform implementation.

The four base `run_spot_{back,left}-strafe_gun_{up,down}` definitions rotate `body`
around Z by-90 degrees. Both down files have body at bone/track0. Both up files
have no body entry, so this helper has no pose to rotate for those files. These
four do not demonstrate a bone/track index mismatch.

## Full base archive audit

The bounded scanner processes1606 human ANM members:1605 parse, one is rejected.
There are217154 stored frames,216967 FRM2 and187 FRMN; six resources contain FRMN.
Parsed versions are851 at60000,748 at60001, one at50000 and five at40000.
All parsed coordinate sequences are strictly increasing, start at0 and finish at
the declared last coordinate.77 files store fewer frames than their FRMS count.
No unknown flags, nonfinite pose values or duplicate first-frame pose allocations
were found. No human file in this scope carries a mesh payload.

The24-byte `weapon/anim_crouch_to_lie_gun.anm` has no stored frames, matching the
native empty-frame rejection condition. Two smoke resources first update seven
bones after the first frame, leaving those bones' track indices-1 under the
reviewed allocation rule: `idle_mission/idle_passive_stand_gun_strap_smoke.anm` and
`stand_smoke_1-old.anm`. None of these three files is selected by the1545 base
declarations. Their presence is not evidence that gameplay loads them.

The prior `5D6110` dispatcher lower-bounds sorted marker rows and emits **one**
matching row for the current stored-frame coordinate. The archive join exposes27
rows with no stored coordinate in their selected file:

- Six `walk_normal_*_down` definitions retain leg markers at42/59/74/88 while their
  selected locomotion files end at36:24 rows in total.
- `board_lowtank_stand` and `_1` declare `metal_footstep` at144; both files end120.
- `stand_fire_pistol_down` declares `prone_to_stand` at1; its selected binary stores
  only coordinates0 and10 although1 is inside the declared range.

`board_plane_right` declares both `metal_footstep` and `jump` at100. Only the first
sorted equal-coordinate row can be selected by this dispatcher. Tie ordering is
not established; declaration order must not be assumed to choose the winner.
These are static data/consumer mismatches, not runtime demonstrations of missed
sounds or actions. Mounted overrides, chosen variants and caller playback flags
remain relevant.

## Reproduction and remaining leads

Run `research/scripts/map_animation_resources.py` with `--archive properties.pak`,
`--catalog research/artifacts/build-25492057/human-animation-catalog.json` and an
output path. The committed artifact records every input member's SHA256. The
scanner adds its own bounds checks and rejection reporting; it does not claim
that the engine performs those checks. Eleven synthetic tests cover packed and
matrix forms, sparse tracks, visibility/mesh combination, malformed streams,
late/duplicate tracks, directory joins and marker gaps.

`verify_fine_map.py` checks the manifest against both original PEs and the frozen
`*-animation-resources` exports, including representative resource hashes. The
independent LLVM check agrees on8490 instruction starts across42 reviewed
game/GEM functions after normalizing LLVM's standalone LOCK prefix. The manifest
checks26 game contracts,16 editor counterparts,33 fields, eight RTTI tables and
12 representative resource hashes. Structural
checks do not prove the semantic interpretation or a usable ABI.

Continue bone interpolation/constraints and root transforms; concrete marker
action lists; mount-table/path context and directory iteration order; shared
binary/definition lifetime; mesh queue drain; rate/time-scale producers; and
the action-level effects of the duplicate/sparse marker cases. The broader
[static frontier](static-frontier-25492057.md) remains open.
