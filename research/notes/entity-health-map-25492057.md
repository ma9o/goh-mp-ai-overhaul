# Entity components, vitality and injury signals — build 25492057

This is a continuing static investigation. All addresses are **RVAs** for the
fingerprinted binaries; offsets are relative to the object named in each table.
RTTI, configuration tokens and diagnostics supply native names where available.
Other names describe the reviewed contract. Function discovery and byte validation
do not prove semantic completeness, safe calling conventions or runtime lifetime.

## Entity components and ownership

The actor has two distinct component mechanisms. Its direct Fsm, Cover, Brain,
Sensor, Weaponry and Chassis pointers are not members of the extender vector.
`eGameEntity +0x578` points to a separate, non-polymorphic **0x40-byte extender
collection**. It is not the RTTI type `Entity::eManager` merely because that type
was found nearby during discovery.

`0x719520` initializes this pointer to shared empty storage at `0x156EE40`.
`0x71DE00(entity, extender)` sets extender `+0x18` to the owner, invokes extender
slot 4 (`+0x20`) when the owner changes, and replaces an empty collection with a
new zeroed collection before calling `0x799590`. The attach helper also refreshes
entity `+0x1C0` bits 26/27 through separate helpers; their exact meanings remain open.

| Extender collection offset | Contract |
|---|---|
| `+0/+8/+10` | Vector begin/end/capacity, entries are extender pointers |
| `+18/+20/+28/+30` | Four cached extender positions, indexed by extender slot 6 |
| `+38`, uint32 | OR of each member extender's event mask |

`0x799590` calls extender slot 6 (`+0x30`); a nonnegative return selects a cached
position at `collection +0x18+8*index`. It then appends to the vector and optionally
rebuilds the aggregate mask. Actor vitality's implementation `0x928F00` returns
zero. This establishes the previously observed read chain:

```text
actor +578 -> extender collection +18 -> Actor::eVitalityExtender
                                           +18 -> owner actor
```

`0x7991B0` clones a collection. Extender `+0x80` bit 14 selects retaining the
existing object; otherwise the resource subobject's clone predicate decides
whether to retain or create/copy a new instance. `0x799370` rebinds the owner of
non-bit-14 entries and invokes their owner-changed slot. `0x72AA90` is an entity
clone path using these helpers, not a configuration loader.

The extender's primary vptr is at `+0` and resource/referenced subobject at `+8`.
Base constructor `0xBE9B0` initializes owner `+18`, name-like string `+20`, and
event mask `+80`. Its reference-count layout therefore differs from a standalone
referenced component: refcount is at complete-extender `+10`.

`0x7EF100` initially assigns shared defaults to actor `+DB8` Fsm, `+DC0` Cover,
`+DD0` Sensor, `+DD8` Weaponry and `+DE0` Chassis; `+DC8` Brain starts null.
`0x7F54F0` clones concrete components and binds owners at chassis `+10`, sensor
`+260` and brain `+38`. It is a clone helper, not the initial constructor.
`0x7F57F0` loads concrete components; `0x936860` creates a human cover component
when the current cover's owner `+A8` is null. Destruction `0x7EF340` releases
owned components using the corresponding default/concrete discriminators.

| Actor loader token ID | Destination / behavior |
|---|---|
| `0x59` | Pather lookup, actor `+DB4` low byte; rejects lookup `-1` |
| `0x5B` | Brain `+DC8`, registry factory if absent |
| `0x5C` | Fsm `+DB8`, allocate when current Fsm `+670` is zero |
| `0x5D` | Sensor `+DD0`, factory/rebind when owner `+260` is zero |
| `0x5E` | Chassis `+DE0`, optional named replacement |
| `0x64` | Weaponry `+DD8`, allocate when shared/default marker `+A0` is set |
| `0x6B` | Class string at actor `+DE8`, optional token set at `+E48` |

These token numbers are local to this loader. Complete registry name bindings,
chassis replacement and collection removal/destruction remain separate leads.

## Event domains must remain distinct

There are several unrelated numbered event spaces:

1. Entity notifications call actor primary slot 35 (`+0x118`). Human
   `0x936C60` chains actor `0x7F1AF0`, game entity `0x71A7C0`, then base entity
   `0x6FE6F0`. Selected notifications become extender events with different IDs.
2. Extender events use collection mask `+38` and extender mask `+80`.
   `0x799630` iterates the vector and calls subscribed extender slot 7 (`+38`).
   `0x798EE0` changes a subscription and rebuilds the owner's aggregate mask.
3. Battlefield signals enter actor primary slot 51 (`+0x198`), implemented by
   `0x7F29D0`, and sensor slot 17 (`+0x88`), implemented by `0x877D20`.
4. `Events::Entity::cmAEC_*` messages include Health `0xBC9`, Props `0xBC8`,
   Stamina `0xBCE` and Veterancy `0xBCF`. `0x7FAB00` only forwards these when
   global `0x159E218` exists and `0xAF7A90()` returns the same actor. This local
   actor-selection/control-facing channel is not an injury broadcast to all AI.

Extender factory `0x924350` subscribes vitality to IDs
`0,1,2,3,6,0xB,0xD,0x11,0x14`. It uses the same base/default initialization as
`0x9241A0`; factory `0x929500` creates a 0x180-byte instance and returns its
resource subobject (`complete object +8`). Cloning `0x928F10` also receives
adjusted resource pointers. Treating these pointers as complete vitality objects
would shift every field by eight bytes.

## Health, stamina and state fields

The concrete vitality owner assertion in `0x926370` says it can only be used with
an Actor. Health and stamina each have raw storage and separately transformed
effective values. Reading `+12C` directly is not a replacement for the effective
health accessor.

| Vitality offset | Type / reviewed meaning |
|---|---|
| `+18` | Owner actor pointer |
| `+80` | Extender subscription bitmask |
| `+88` | Current dying visual/notification state |
| `+8C/+90` | Dying start time / duration; duration can use time sentinel |
| `+94/+98` | Blinding start / expiration, distinct from dying timer |
| `+A0/+A4` | Raw maximum/current stamina, defaults 100/100 |
| `+A8` | Current stamina-limited movement mode, default 1 |
| `+AC..+B8` | Four load interpolation knots; defaults 0/20/40/60 |
| `+BC..+108` | Five arrays of four stamina rates; default zero |
| `+110/+118/+120` | Movement-limit vector begin/end/capacity; 12-byte records |
| `+128/+12C/+130` | Raw health maximum/current/regeneration, defaults 100/100/0.5 |
| `+138/+140` | `Damage` map tree / size |
| `+148/+150` | `DamageTPC` map tree / size |
| `+158` | `DamageBlast` multiplier, default 1 |
| `+15C` | Applicable veterancy level/count |
| `+160` | Periodic update deadline |
| `+168` | Scheduler diagnostic name `eVitalityExtender:update` |
| `+170` | One-use dying-duration override; default time sentinel |
| `+174` bit 0 | Final death/destruction state |
| `+174` bit 1 | Incapacitated state, used for blinding/knockdown as well as dying |
| `+174` bit 2 | Dying/revivable state within incapacitation |
| `+174` bits 3/4 | Loaded `Dead` / `Dying` requests |
| `+178/+17C` | Last health/stamina values used by replication threshold checks |

The actor's named `select` flag is resolved by initializer `0x57D20` to index
storage `0x157DFC8`. It is **not an alive flag**. `personage` uses `0x157DFD0`;
tag `0x15708C0` is literally **`bleeding`** (`0x48650`). Actor flag indices are
looked up dynamically and address a packed bitset beginning at `actor +DAD`.

`0x9291C0(vitality, value)` applies actor slot 62 (`+1F0`) parameter kind 1,
then a human Health ability multiplier, then veterancy health bonus.
`0x9292E0` similarly uses parameter kind `0xE`, Health_Regeneration and the
separate veterancy regeneration bonus. `0x929400` applies Stamina and veterancy
stamina multipliers. Ghidra incorrectly represents some float arguments/returns
as integers; assembly uses scalar SSE registers.

`0x925190` accepts an effective health value and divides by
`0x9291C0(vitality, 1.0)` to obtain raw `+12C`. It does not clamp here. On change,
it can emit a separate normalized health-change event, increments actor `+BD8`
and sends the local Health message. `0x9252D0` performs the analogous stamina
conversion with `0x929400(vitality, 1.0)` and sends Stamina. Direct health
notification helper `0x926AD0` increments `+BD8` and sends Health without
changing health itself.

`0x7F0500` applies actor parameter modifiers through `0xA5A9D0` and expands
selected parameter kinds by actor/chassis type. Human override `0x938B90` adds
parameter kinds `0x12/0x13/0x14` for original kinds `4/6/0xC` when a particular
breed tag is present and cover kind `+24` is 2. The tag name is still a lead.

## Incapacitation, revival, dying and blinding

`0x9259B0(vitality, incapacitated, dying, force)` is the central state setter.
When not forced, replication roles can reject local work or emit state sync.
Changing incapacitation propagates to:

- Brain `+39C` bit 19, with its slot 15 (`+78`) refresh callback.
- Sensor `+18` disabled byte, with slot 11 (`+58`) refresh callback.
- Entity availability/geometry helpers and actor property refresh.

If already incapacitated, the setter can change only dying bit 2. On leaving
incapacitation after dying, it clears bit 2 and start time, restores **half**
effective maximum health and stamina (each maximum floored at approximately
`1e-5`), clears the tagged `die_sound` flag and sends notifications. Leaving a
non-dying incapacitation does not perform that half-health restoration.

A transition into dying starts `+8C` at current simulation time. Without a
`+170` override it obtains parameter kinds `0xF` and `0x10`, each initialized
to 1000, and chooses an integer duration between their values with one RNG draw
when the integer difference is nonzero. The assembly contains a modulo operation
that Ghidra drops. The result is duration-converted, multiplied by `0.001`,
truncated and stored at `+90`. Do not infer wall-clock units from the default 1000
alone; the clock conversion remains an explicit dependency.

`0x924610` wraps this setter with `select` and `bleeding` updates. Entering dying
clears selectability, interrupts the brain with reason `0x107` and adds bleeding.
Revival sets selectability, exits incapacitation and removes bleeding. This is
the normal medic completion target already found in `0x371960`.

`0x924BA0` has the diagnostic name `eVitalityExtender::Die`. For a personage it
can enter the dying/revivable state instead of final death, set health/stamina
to zero, send actor notifications `0x30/0x25`, and request the die Fsm event.
For final death it sets vitality bit 0 and calls `0x926B80`.

Finalization clears the blinding timer, zeroes health/stamina, clears `select`,
sends actor notifications `0x27/0x25`, interrupts with reason `0x107`, releases
cover, clears weaponry work and sets brain `+39C` bit 20 before refreshing that
brain. It also clears sensor work and updates entity availability/spatial state.
These two brain bits are therefore wider availability controls: other callers
also use them for boarding and entity changes; they should not be named solely
after one health condition.

`0x9248B0` manages **blinding**, with expiration at `+98`; `0x926F20` emits
`start_blinded`/`stop_blinded`, enters/exits non-dying incapacitation, changes
selectability and uses interrupt reason `0x405`. This timer is not the bleeding
death countdown at `+8C/+90`.

Personage eligibility `0x9258E0` and post-initialize slot 9 `0x923FC0` compare
the applicable veterancy count with a Personage ability threshold. Missing
ability uses `UINT_MAX`. The editor-oriented `0x9262B0` UI clamps its Veterancy
input to 0..8, which is not evidence that every engine path is capped at 8.

## Periodic recovery and movement limits

Vitality event handler `0x926020`, event `0xB`, schedules `0x9263B0` at a
duration-converted **0.3** interval. It advances an existing deadline rather than
always rescheduling from now. The periodic method first manages the dying timer.
Carrying by an eligible human with an inventory predicate, or active medic
reservation at human `+12BA`, can extend the duration. Otherwise expiration
clears personage and invokes final death. Exact inventory predicate and extension
duration remain leads.

When neither dead nor incapacitated, it performs:

```text
raw_health += health_regen_transform(raw_regen * 0.3) / health_transform(1)
raw_stamina += stamina_rate() * 0.3 / stamina_transform(1)
```

Both results are clamped to `[0, raw_maximum]`. Changes larger than about `1e-5`
send the appropriate local message. A replication branch compares with last
sent values before sending `eVitalitySyncCommand`; threshold helper remains open.

`0x9253B0` selects one of five four-knot rate arrays according to whether the
chassis is moving and its actual movement mode. It interpolates using actor
`+A2C` against the four configured **load** knots. Positive/zero rates can be
multiplied by Stamina_Regeneration and veterancy regeneration; negative rates
are not changed by those regeneration bonuses. This is a concrete link between
load, movement and stamina, not a single fixed regeneration speed. The producer
of actor `+A2C` still needs mapping.

Movement-limit records contain `(mode, lower, upper)` at offsets `0/4/8`.
The periodic method scans backward: reaching `upper` chooses that mode;
between lower and upper it can retain the existing mode. With no records the
default is 2. Another actor predicate can force 3. It writes vitality `+A8`,
casts chassis to `Actor::eChassisFoot`, and calls `0x849160(mode, false)`.
This supplies a stamina-dependent actual movement cap.

## Configuration and damage coefficients

Token catalog and dispatch `0x9273D0` agree on the following local registry
(`0x1586620`): Dead `0x4FC`, Dying `0x4FD`, Stamina `0x4FE`, Health `0x4FF`,
Damage `0x500`, DamageTPC `0x501`, DamageBlast `0x502`, Veterancy `0x503`.
Dead/Dying set deferred flags; Dying can carry a duration override.

Health parser `0x923510` binds `max/current/inc` to the three health floats.
Stamina parser `0x922D40` binds `max/current`, `load` (four knots), `inc` (arrays
by named movement mode), and `movement_limits` (12-byte records, then sorted).
Mode lookup `0x748050` uses a name/value table at `0x10E18E0` and rejects unknown
names with `-1`. The table binds `slow=0`, `normal=1`, `fast=2`, `walk=3`,
`bow=4`, `assault=5`, `sneak=6`, `spot=7`, `none=-1`. These are native names;
their movement/animation behavior still needs the corresponding chassis paths.
The stamina-rate arrays are stationary `+BC`, walk/sneak/spot `+CC`, slow `+DC`,
normal `+EC`, fast/bow/assault `+FC`. Sort `0x9286F0` compares the integer mode
in its insertion/heap branches, so the backward recovery scan prefers larger
mode values. Its partition helper remains unreviewed.

`0x923930` loads named float coefficients into a tree with 0x88-byte nodes;
value is at node `+80`. `0x923F20` looks up a name and defaults to 1.0.
`0x925050` chooses DamageBlast when its fourth argument is true. Otherwise it
returns 1.0 without context flag `0x2000000`, and chooses DamageTPC versus Damage
using the source actor's chassis predicate before looking up context `+18`.
This helper is a damage multiplier selector, not a stamina accessor.

## Injury and battlefield signal path

Actor primary slot 61 (`+1E8`), `0x7F0430`, constructs sensor signal **type 4**
at the victim's current position. The source actor comes from context `+8` only
when context flags `+188` include `0x1000000` and that source has actor type bit 1.
It directly calls the victim's sensor slot 17, then sends extender event `0xD`.
Vitality handles `0xD` by enabling a timed visual effect/event `0xC` when alive
and the relevant global option is enabled; this is distinct from health reduction.

Damage worker `0x741710` (`eIA_Actor.cpp`) calls this slot **before** reading
effective health and writing `old_health - calculated_damage` through `0x925190`.
Its calculation invokes action virtual methods, source modifiers, cover/distance
modifiers and random gates. It normalizes the damage by effective max health,
chooses configured impact/death events and can emit `eHealthDamageCommand`.
Only this outer contract is reviewed; the action subclasses and all modifier
branches remain productive static leads. Replicated damage executor `0xACE10`
also calls slot 61 before applying a received health value.

Actor signal handler `0x7F29D0` calls the base signal handler, sensor slot 17 and
brain slot 25 (`+C8`). Type 6 first requires a source actor owned by the same
player and emits actor helper event `0xE`; type 5 can emit helper event `0xF`
when signal/source positions are within a half-scale box. Signal meanings must
be bound from their publishers rather than these numeric tests alone.

The **human brain's slot 25 is an empty function**, `0x903690`, shared with the
base and airborne brains in the current RTTI inventory. The human's response
comes through sensor state and later decision updates. Vehicle override
`0x98B200` handles type 6: it sets bytes `+3DD/+3DE`, chooses a 10..30 duration
for deadline `+3F8`, and applies a separately randomized 1..2 value to selected
weapon definitions. The meaning of those weapon helper calls is still open.

Type **6** has a concrete producer in brain update `0x903B10`: observing a dead
actor belonging to the same player can publish this signal once for that player,
tracked by a player-index bit in the dead actor's brain `+11C`. The sensor record
must satisfy observation flags `0x20020000`, a related classification bit and
the update's control gates. This binds type 6 to discovery of a friendly corpse,
not to weapon fire. Base entity signal method `0x71B510` forwards received
battlefield signals as extender event **10**, with the signal pointer as payload.

World publisher `0xA5CA90` constructs a 0x28-byte signal:
type `+0`, radius `+4`, XYZ `+8/+C/+10`, source actor `+18`, payload `+20`.
It queries a bounding sphere, excludes the source, requires recipient entity
`+1C0` bit 8 and appropriate entity-state gates, compares recipient configuration
`+548 -> +3C8` against radius, then calls recipient primary slot 51. It also
forwards the signal to another manager (`0x9D25D0`). Concrete type/radius producers
remain static leads. `0x74F220` publishes the signal descriptor at its action
`+28` from the source actor's position; effect creation/update methods
`0x4D2810/0x4D1D20/0x4D3F10` publish configured descriptors and can repeat at
configured intervals. Their definition parsers are still open.

## Editor comparison

Editor injury callback `0x672530` has the same reviewed type-4 construction,
source gates, actor offsets, sensor slot and extender-event ID as game `0x7F0430`.
Editor actor signal handler `0x674A90` preserves the same type-6 ownership gate,
sensor/brain forwarding and type-5 position test as game `0x7F29D0`.
Editor vitality event handler `0x7AD4C0` preserves the reviewed event IDs,
state fields and periodic-update contract of game `0x926020`. This is a comparison
of these specific contracts, not a claim that their entire call graphs match.

## Remaining scope

This pass does not close entity or vitality coverage. Next leads include damage
action subclasses and configured event execution, full signal publishers,
vehicle signal effects, blinding/knockdown callers, owner/component replacement,
world identity/enumeration/lifetime, load production and movement-mode behavior.
The broad `Entity::eExtender` vtable anchor hit the export reference limit; it
cannot establish complete factory coverage. Unreviewed RTTI roots and discovered
functions remain discovery artifacts rather than semantic mappings.

## Evidence checkpoint

The manifest records 80 game methods with reviewed or explicitly partial
contracts, six editor comparisons, 65 fields, the movement-mode table, nine
float constants, 15 RTTI tables and nine literal byte sequences. Structural
validation passed for 10,301 instruction locations. The retained exports contain
243 game functions and six editor functions; the larger discovery set is not
all semantically reviewed. Independent LLVM disassembly agreed with Ghidra on
632 instruction starts in six selected functions after normalizing LOCK prefixes.
The executables were read as files and were not launched or modified.
