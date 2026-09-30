# Modifiers, entity filters and selectors — build 25492057

This pass maps how native scripts choose entities and how scene modifiers change
armor, accuracy and weapon ranges. It also follows the shared native command
dispatcher up to the concrete command call. The broader investigation remains
active in [the frontier](static-frontier-25492057.md).

The exact binary identities, paired contracts, fields, vtables, enum spellings,
literal bytes and instruction evidence are in
[`modifier-map.json`](../artifacts/build-25492057/modifier-map.json), with
[`modifier-validation.json`](../artifacts/build-25492057/modifier-validation.json)
for structural checks. These checks do not prove semantics, completeness or a
callable ABI. No game/editor code was executed.

There are 140 paired contracts and three game-only multiplayer helpers. The
map records 126 fields, 146 RTTI tables, 16 enum tables and 16 named-cover token
registrations. Ghidra and LLVM agree on 22,582 instruction starts after normalizing
LOCK prefixes; 26 additional raw code regions have independent decoding.
The 70-name native command registry is an inventory, not 70 completed command
implementations. Offsets and RVAs below are hexadecimal; slot numbers and bit
indices are decimal. Game/editor pairs use `/`.

## Modifier preparation and application

Entity refresh `A0DA40/898340` first prepares the entity, then visits manager
files `30..38`, each file's modifiers `58..60`, and each matching modifier's
parameters `B8..C0`. A nonzero file byte `30` skips that file. Its constructor
zeros this byte; this pass does not assign an authoring-property name to it.
Modifier filter `28` must match. Parameters execute in stored order through
virtual slot 5, and the entity receives virtual slot 35 with event `10` afterward.

Scene refresh `A0DB50/898450` applies that sequence to nonnull scene-vector
entities having type bit 0 at `30`. Located callers include scene initialization,
player-dependent refresh and modifier file/property changes; GEM also has an
explicit reload/refresh window path. This evidence does not establish continuous
per-frame reevaluation of arbitrary state-dependent filters.

Preparation `A0DCE0/8985D0` restores armor scale to 1 and active weapon-place
shooter scales to 1. Armor accessor `719FD0/594D40` returns entity `580` or a
shared default; it performs no local copy-on-write. For a private weapon parameter
set, preparation restores the **currently selected** ammunition profile's three
range fields from the raw template's corresponding place, or its raw weapon
default. It does not reset every ammunition profile in the private set.

Place lookup `806260/6888D0` searches the weaponry's **definition-pointer** vector
`18..20` by tag. Definition `2B8` is the runtime-place link. This is distinct from
the active-place vector `30..38`. Several reset-path dereferences depend on these
objects agreeing; the local reset routine does not validate every missing-place
case.

| Parameter | Matching and effect |
|---|---|
| `armor` | Assign factor `8` to armor scalar `0` |
| `accuracy` | Match active place definition `C0+58` against string pattern `8`; call shooter `B8` slot 4 with float factor `28` |
| `aim_range` | Same place matching; make parameter set private, then scale selected profile `20` and `24`, and inversely scale `28` for a nonnegative factor |

The range bindings name `20=aim_range`, `24=max_range`, `28=min_range`.
For nonnegative `k`, the range transformation is `(a*k, b*k, c/k)`; negative `k`
leaves minimum range unchanged. Zero is not guarded before the reciprocal.
Later armor/accuracy parameters overwrite earlier matching assignments;
aim-range parameters compose on the selected profile.

The common name matcher `676070/4EBBF0` is case-sensitive and recognizes only
`*` specially. `?` is literal. It uses a literal prefix, first matching intermediate
substring and a final suffix. The final suffix's length is checked against the
whole input rather than only the unconsumed tail, permitting overlap. It should
not be replaced with an assumed general glob/regular-expression contract.

The manager owns file objects; files own modifiers; modifiers own parameter and
filter children. File reload destroys old modifiers before parsing the new file.
SceneDestroy clears manager children. Path resolution preserves a supplied
directory component and otherwise joins the scene resource path. Generic parser
and string allocation internals are bounded to these observed effects.

## Weapon profile ownership

Place `A0` references an `eParamsByAmmo` container. Equality with raw weapon
`98+1600` identifies the shared state; a different pointer identifies private
state. `80AC60/68D590` changes ownership only when the requested state differs.
Repeated requests for private state retain the existing private container.

Clone `8291D0/6AC500` allocates a `30`-byte referenced container, copies tagged
records of stride `338`, and clones/shares the default and each tagged profile
through policy mode 2. Both located concrete weapon profile types use mode bit 1
to request cloning. The base profile is `80` bytes; the bullet profile is `270`.
They acquire distinct timing/range scalar storage, while base auxiliary pointers
`30/38/40/48` are shared with reference increments. Destruction releases each
profile through its secondary referenced interface at `8`.

Current-profile accessor `809EF0/68C7F0` obtains ammunition tags from the current
item definition, or uses an empty tag set. Lookup `829140/6AC450` returns the
first matching tagged profile, otherwise default `28`. This return is borrowed:
there is no allocation or reference increment. A reader must account for weapon
replacement and private/shared toggles invalidating earlier pointers.

## Filter evaluation

The inline filter is `70` bytes. Required terms occupy vector `20..28`; excluded
terms occupy `58..60`. `9E2D10/86CBD0` rejects if **any exclusion matches**, then
requires **every include term to match**. Both vectors empty means true.

| Term | Reviewed predicate |
|---|---|
| `zone` | Named zone; actors use a binding-root bounding circle, other entities a position point query |
| `mids` | Actor ushort ID `DAA` occurs in the configured vector |
| `tag` | Exact runtime tag in entity `4E8..4F0` |
| `player` | Actual owner `DA8`, with a game multiplayer control-group special case |
| `relation` | Diplomacy result equals configured UI relation index minus one |
| `prop` | Exact definition property tag in `548+330..338` |
| `discovered` | Entity `1C0` bit 19 |
| `state` | All requested recognized state bits pass; unknown higher bits add no test |
| `with_item` | Additional checks on the first matching inventory item only |
| `broken_components` | Any configured name mask and component row damage byte `24 != 0` |
| `destroyed_components` | Same search with damage byte `24 == 2` |
| `movement_type` | Actor chassis `DE0+2C` equals configured type other than -1 |
| `entity_stuff` | StuffExtender item definition has a matching tag |
| `cover` | Actor has resident cover kind, optionally with matching associated cover name |
| `team` | Game multiplayer team-name test; GEM always returns false |
| `order` | Query kind 5 with configured string through the resolved order's slot 11 |

Empty component-name masks accept **any actor**, even one without a damaged
component. Item modes use native spellings `using`, `existance`, `amount`.
`using` requires item holder `38`; `existance` optionally checks filled state or
matching ammunition; `amount` compares its minimum with the iterator's requested
count. Nonnegative counts are used as-is, including zero; negative counts become
one. Failure of these extra checks does not search for a second matching item.

State bit names and actual reads are separate from the older selector's single
state enum:

| Bit | Name | Input |
|---|---|---|
| 0 | dead | Vitality `174` bit 0 or 2 |
| 1 | operatable | Actor `DB4` bit 8 |
| 2 | moveable | Chassis predicate `83F9B0/6C3180` |
| 3 | chassis_work | Chassis `3C0` bit 0 clear |
| 4 | inhabited | Occupied placer IDs or nonempty binder vector |
| 5 | inactive | Entity `38` bit 9 |
| 6 | linked | Entity `1C0` bit 28 |
| 7 | user_control | Brain `398` low two bits equal 1 |
| 8 | fallen | Entity `1C0` bit 12 |
| 9 | examined | Entity `1C0` bit 20 |
| 10 | discovered | Entity `1C0` bit 19 |
| 11 | in_squad | Brain squad `100` nonnull |
| 12 | commander | Squad `A8` equals brain owner `38` |

State parsing is case-insensitive and ignores unrecognized tokens. Relation UI
indices are `0=-, 1=neutral, 2=enemy, 3=ally`; native diplomacy is `0/1/2`.
The player's control-group special case depends on game multiplayer state;
GEM's player predicate compares actual ownership directly.

Reference visitation is **not** predicate evaluation or candidate selection.
Filter visitor `9E2E20/86CCE0` traverses the excluded vector twice and never visits
the included vector in both binaries. Matching still uses the proper two vectors.
This is a concrete traversal discrepancy, not a demonstrated runtime failure.

## Selector sources, ordering and shared state

The `28`-byte selector layout corrects the constructor-only waypoint map:
`8` allowed-source mask, `10` owned source pointer, `18` configuration integer,
`1C` actual source, `20` default source, `24` ignore-captured-by-user flag.
The constructor's qword zero at `18` spans two integers, not a second pointer.

Native source values are `standart/local=1`, `advanced=2`, `waypoint=3`,
`current=4`. Kind 3 requires a waypoint context and then chooses the current actor,
or the current squad's members. Kind 4 follows the context's current selector;
there is no local cycle/self-reference guard. Candidate collectors do not apply
the captured-actor flag; command dispatch does that later.

The standard selector chooses its enumeration source in priority order:
explicit actor IDs, runtime tag, zone cache, then the actor registry. Remaining
configured predicates are applied afterward. It rejects deletion flag `38` bit 5;
the all-registry fallback also skips inactive actors. It does not deduplicate.
No configured filter means **no candidates**, even if only an amount is set.
Relation alone does not impose a test: it is consumed with a player/user-player
filter. Unknown type/state enum values add no constraint.

The advanced selector owns group/sort/amount children; cached sort and amount
pointers refer into that owned set. Groups union candidates through pointer
deduplication and their include/exclude filters. Gatherers are `mids`, `tag`,
`zone`, `zone_ex`, `squad`, 32-bit `id`, and `inventory_items`. A group with no
gatherers scans the actor registry, rejecting an actor that is **both inactive
and linked**. This differs from the standard fallback and zone-cache rules.

Collection uses writable global output, actor-only and current-filter slots,
with no local save/restore. Inventory gathering uses a nested selector and shared
scratch vector. A nested advanced selector writes the same selection globals.
This exposes a nesting/reentrancy lead; it is not evidence that an actual authored
mission triggers corruption. Admissible configurations and outer caller invariants
remain to be traced.

Amount is integer truncation of `mean + (2U-1)*spread`; near-zero spread avoids
the RNG call. A cap is applied only if the result is positive and below the
candidate count. **Zero or negative means keep all.** Shuffling consumes `n-1`
draws using high-24-bit modulo selection. Standard amount handling shuffles first;
advanced handling does so only if no sort child exists. Even a no-op sort child
suppresses that automatic shuffle.

Sort kinds are shuffle, waypoint center and first selected entity center.
Distance order uses **full 3D squared distance**, nearest or farthest. Missing
centers do nothing. The inspected sort uses insertion/partition/heap paths and
does not promise stable ordering or an ID tie-breaker for equal distances.

Zone membership cache `9D1830/85B3D0` refreshes on a quarter-second deadline and
stores invalidating soft links of stride `10`. It includes noninactive or linked
actors whose binding-root bounding circle intersects the zone. Position results
can be stale until refresh; deletion can invalidate entries sooner. Final selector
vectors contain raw pointers and do not retain entities themselves.

## Named cover and native command boundary

Named covers occupy relocating `80`-byte records. Tokens establish name `0`,
position `60`, orientation `68`, automatic entity-ID/integer-coordinate link
`6C..74`, priority `78` and flags `7A`. Loading clears old records. Replacement
copies selected rows; lookup returns a borrowed pointer into the vector.

The cover filter copies the resident `88`-byte descriptor and associates it with
an eligible named record. Matcher `89A9D0/71F520` skips kind 3 and any descriptor
already associated at `28`. Link matching requires equal integer IDs and nearby
integer coordinates. Among eligible descriptors it chooses the nearest XY, with
an early exit below a small squared-distance epsilon, and a final global distance
gate. The initializer records preserve the gate's actual multiplication formula;
link tolerance is `0.6 * spatial unit`, and the association threshold is half
that tolerance. The consumer compares it directly with squared distance. No inferred unit or
squared-radius correction is substituted for those instructions.

The first qualifying name wins because associated descriptors are skipped.
Existing resident associations are therefore preserved. Record clear/reload can
invalidate borrowed pointers; these local functions do not walk resident actors
to repair them. Live replacement and caller ordering remain lifetime leads.

`GameEntityCommand` dispatch `9B1310/83AE00` collects entities, excludes captured
actors (`DB4` bit 11) when requested by selector `24`, builds a per-entity context,
then calls slot 14. The context carries entity/actor pointers, player and XY/radius.
Waypoint source substitutes the waypoint center/reach radius; otherwise it uses
entity position and zero radius.

Actor-command gate `99D880/827050` rejects missing actors and vitality bit 0.
It does **not** reject vitality bit 2 alone, unlike the named `dead` filter.
Optional drop bits reset brain/order/weapon, sensor or the human FSM before
concrete slot 16/17 dispatch. The concrete command implementations and their
default drop flags are the next pass. The registry's 70 names are retained as
verified name/factory bytes to make that work finite and auditable.

Remaining work also includes alias-aware global/field writers, outer worker and
live-replacement invariants, additional component/goal/action families, and the
frontier's other AI inputs. This pass does not claim that static analysis is
exhausted.
