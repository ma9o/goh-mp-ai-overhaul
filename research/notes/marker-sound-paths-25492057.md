# Human marker sound requests joined to inherited AI signals

Build25492057; a static dependency/resource join using the native contracts in
[marker actions](marker-actions-map-25492057.md) and
[sound definitions](sound-definitions-map-25492057.md).
[Machine-readable result](../artifacts/build-25492057/human-marker-sound-paths.json).
This pass adds no runtime observations or new native function claims.

## Scope and expression handling

The31 base human marker handlers yield147 potential sound-action rows after
following the three named events and resolving the two positional macro bodies:
114 start_sound,32 play_sound and one stop_sound. Eight macro invocations produce
65 literal request strings/64 normalized paths. The two spellings of normal/metal
with and without a trailing slash refer to the same base directory.

Every row retains source line, enclosing conditions, preceding failed else tests,
named-call provenance and macro invocation IDs. These are potential branch paths,
not observed execution or a claim that every condition combination is reachable.
Sequential siblings do not inherit the guards of an earlier conditional sibling.

An unusual declaration is preserved explicitly: leg_left/right invoke
`play_leg_sound` without arguments, while that macro forwards `%0` as the second
argument of `play_move_sound`. The latter reads only its first argument, one of
fast/lying/normal. The extractor resolves arguments when they reach emitted
syntax, records the six unused second arguments, and never invents a value for
the omitted one. All emitted sound paths resolve without it. **Native treatment
of omitted forwarded arguments is still unverified**; this is a bounded expression
dependency result, not a complete recreation of the SDL preprocessor.

## Potential movement publications

Velocity16 is tested first, then lying, then the normal fallback. Under successful
event dispatch, base resources and the previously mapped publication gates:

| Branch | First request | Terrain-dependent request | Other sound request |
|---|---|---|---|
| fast | gear_rattle/fast: movement8 | fast surface: movement15 | breath alias, no base AI signal |
| normal | gear_rattle/normal: movement8 | normal surface: movement5 | none |
| lying | gear_rattle/lying: no matching base asset | lying surface: movement1 | none |

Numbers are authored radius inputs, before the spatial scale. The walking gear
request can therefore publish a larger radius than its footstep request. This is
not a claim about final detection range: source flags, recipient thresholds and
sensor handling still apply.

The first request occurs before terrain testing. Terrain aliases road/rock/bridge
select road, bridge_wood selects wood and bridge_steel selects metal. Water and
shallow_water select shallow_water. Country-road and ground branches use rain to
choose mud versus ground. The grass branches both request grass; their additional
view actions differ. Ice, snow, sand and puddle each have their own request.
There is no final catch-all footstep request after the terrain chain.

## Four base-resource gaps

No matching asset exists in the seven audited sound archives for:

- human/move/fast/sand/
- human/move/normal/sand/
- human/move/lying/sand/
- human/move/gear_rattle/lying/

The first three are concrete terrain=sand branch expressions; the fourth is
requested before terrain testing in the lying branch. The audit does not turn
an absent trailing-slash path into a claim about the mounted filesystem's
classification. DLC/mod resources, overrides and full preprocessing remain
relevant. These gaps alone do not establish that a soldier is silent to AI.

## One-shots, repeats and RNG effects

All114 start_sound rows are one-shots through755120, including those with a
trailing integer1. That parsed integer is not read by this executor. Consequently,
the inherited walk/run period0.5 does not by itself make these marker requests
register periodic AI signals. The only explicit play_sound loops in this closure
are the two fast-branch breath requests; their base folder has no AI signal.

The directories used by gear, ordinary surface steps and breath contain multiple
WAVs, so each successful selection consumes one current-context RNG draw. For a
matched non-sand terrain branch with base aliases present, the sound selections
alone account for three draws in fast, two in normal and one in lying. The sand
branch has no matching step asset, leaving two/one/zero respectively in this
base dependency model. Other marker actions and callers can consume additional
draws; these totals are not a whole-frame RNG model.

The result also joins posture, repair, digging, pickup, body-fall and other marker
aliases. Their typical inherited descriptor is sound3. Splash uses hit5 and paddle
uses movement15. Base burn, breath, piano and dropped-bagpipe sounds have no signal
descriptor, although selecting their multi-asset directories can still consume RNG.

## Alias and validation limits

Alias targets are candidates drawn from35 base spawn links and two declarations
inside the reached closure. Native playback uses the first matching live alias;
this extractor does not simulate which registrations have already happened or
prove ownership across inherited event layers. The stop_sound bagpipes row has
no binding inside this closure; it is explicitly reported, not fabricated.

The tool checks the human.inc content hash and the name/size/CRC metadata for all
2,943 sound asset directories against the prior audit. Input artifact hashes are
recorded. Four tests cover unused versus required positional arguments, ordered
else guards and named calls, alias/loop interpretation, immediate-directory
selection and stale inventories. Unknown used arguments, unsupported conditions,
cycles and unreviewed request shapes fail closed.

Further static work: native preprocessor omitted-argument behavior; broader alias
registrations/teardown; mounted overrides; exact event producers and recipient
threshold/area lifetime. These remain static leads rather than runtime boundaries.

```sh
python3 research/scripts/map_marker_sound_paths.py \
  --markers research/artifacts/build-25492057/human-marker-actions.json \
  --sounds research/artifacts/build-25492057/base-sound-folders.json \
  --resources-dir "$GAME_ROOT/resource" \
  --output research/artifacts/build-25492057/human-marker-sound-paths.json
python3 -m unittest discover -s research/tests -p 'test_map_marker_sound_paths.py'
```
