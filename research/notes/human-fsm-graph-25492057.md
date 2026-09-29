# Expanded human FSM graph — build25492057

This pass combines the installed base declarations with the native parser,
subset and path-search contracts in [fsm-routing-map](fsm-routing-map-25492057.md).
It is a static graph model, without runtime verification. The input is
`resource/properties.pak:properties/human_fsm.ext` and its15 included members.
The [derived artifact](../artifacts/build-25492057/human-fsm-graph.json) records
all16 member hashes, source locations, macro expansions, state indices, tags,
conditions, ordered edges, split parts, requests, subsets and switch pairs.

## Coverage and checks

The bounded extractor expands4 macro definitions through12 invocations. The
result contains220 states,85 requests,5 subsets,1 switch and1152 directed edges;
none of those edges are duplicates.27 states carry request conditions. All state
references resolve. Unknown forms fail rather than being silently skipped.

The model preserves forward-reference insertion order, `fromTagged` evaluation
at the declaration point, all-of tag masks, inherited subset rules with the last
matching rule winning, and reverse neighbor traversal during shortest-edge BFS.
Paths in the artifact are chronological and exclude their source; the engine
stores the reverse order. The model does not mutate the executable or run it.

The recovered native parser's structural checks produce **no diagnostics**:
every state except `dead`/`common` has exits and exactly one outgoing neighbor
tagged `dying`; every state except `boarding` has a bare graph path from `stand`.
`boarding` is the only bare-unreachable state and is the native exception.
These checks provide independent structural evidence for the expansion; they
do not prove that the bounded preprocessor reproduces every SDL feature.

## Equipment-dependent reachability

| Subset | Enabled states | Isolated `fire` route from `stand` | `repair` route from `stand` |
|---|---:|---|---|
|default|161|None|squat_from_stand → squat → repair|
|with_item|71|None|None|
|with_weapon|216|stand_aim_prepare → stand_aim → stand_fire|None|
|with_bazooka|182|stand_aim_prepare → stand_aim → stand_fire|None|
|on_board|33|None|None|

For `with_weapon`, `aim` stops at `stand_aim`, while `reload` goes directly to
`stand_reload`. For the first four subsets, `switch-weapon` reaches `stand_switch`
and `ground-drop` reaches `stand_drop`. `default`, `with_weapon` and
`with_bazooka` route `down` through `squat_from_stand`, `squat`, then
`squat_to_lie`: that transition already bears the requested tag. A successful
route therefore does not necessarily end at the final pose suggested by the
request's name.

The BFS accepts a source already bearing the tag, even if that source is
disabled by the current subset. The report separates already-tagged sources,
nonempty routes and reachability from enabled sources so these cases cannot be
mistaken for an available transition. For example, `default` has11 already-tagged
`fire` sources, but no nonempty route to that request and no enabled successful
source. Each of the85 requests is evaluated from all220 source states under all
five subsets; named example paths are also retained.

`lie_revive` is declared as a request but has no matching state tag anywhere in
this base graph. Its route is absent from every source and subset. This is a
static lead: callers, resource overrides and intended use remain to be traced;
the result alone does not establish a gameplay defect.

The `walk` switch has15 pairs:11 generated split-preserving pairs for inventory,
reload/unload, switch, holdback, belt and pistol states, plus4 explicit links
(`stand`→`walk`, `stand`→`walk_switch_move`, `stand_recoil`→`walk_recoil`,
`stand_bolt`→`walk_bolt`). The declared `stand_aim`→`walk` split rule generates no
pair because the other split parts do not match.

## Scope and reproduction

A route proves graph reachability under the selected subset and request
conditions. Actual progress additionally depends on request arbitration,
priority/protection, owner tokens, handler behavior and animation completion.
Those mechanisms are outside this isolated search model. It also does not
resolve mounted-archive overrides or mods, and models only the preprocessor
forms present in the hashed base resources.

Run with the installed archive path:

```sh
python3 research/scripts/map_human_fsm.py --archive /path/to/resource/properties.pak --build-id 25492057 --output research/artifacts/build-25492057/human-fsm-graph.json
python3 -m unittest discover -s research/tests -p test_human_fsm.py -v
```

Seven synthetic tests passed. They exercise macro/include expansion,
declaration ordering, reverse-neighbor ties, inherited subset precedence,
disabled-source behavior, condition/all-of filtering, split-switch matching,
and rejection of malformed or unsupported input. Native contract evidence
remains in the separately validated FSM routing manifest.

Continue with the12 concrete human handler families, animation quant/cancel
callbacks, equipment tag producers and unmatched-request callers.
