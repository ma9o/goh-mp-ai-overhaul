# Armor, component damage and interruption events — build 25492057

All addresses are game RVAs unless explicitly marked editor. This
continues [signals and damage](signals-damage-map-25492057.md). Field and function
names below come from native diagnostics/configuration where available; remaining
descriptive names do not assert recovered source declarations or runtime ABI.

## Equipped armor and its volume mask

`Inventory::eHolderArmor` has complete-object size `0x778`, primary vptr `+0`,
property-interface vptr `+68` and another interface at `+78`. Factory `0x7D1FC0`
allocates the complete object, calls constructor `0x7D21D0`, then returns **+68**.
Property wrapper `0x7D1C70` subtracts that adjustment before dispatching to
`0x7D1D50`. Registry `0x1579790` establishes:

| Local token | Field | Meaning / default |
|---|---|---|
| `5B Bone` | `+3E0` | attachment bone tag |
| `5C Volumes` | `+440` | eligible hit-volume tag set |
| `5D PierceAbsorb` | `+770` | projectile health absorption, default0 |
| `5E HeatDamping` | `+774` | heat multiplier, default1 |

The prior pass's provisional `+758/+760` "contents" interpretation is corrected:
these are the tag set's vector pointers. `0x556D00` initializes a `0x330`-byte
tag-set object, with tag-vector begin/end/capacity at `+318/+320/+328`.
`0x15F350` scans that vector in `0x60`-byte tag steps. Armor `+440+318=+758`.
`0x557260` parses volume tags into this set, including clear/removal forms.

The equipped item is instead holder `+3D8`, returned by slot3 `0x7D1490`.
Slot6 `0x7D13B0` reports empty when that pointer is null, its handle definition
at item `+40` is null, or its count at `+48` is zero. These checks complement
the nonempty volume mask and `armor` item-tag tests already mapped in `0x7D1B70`.
`0x7D1EB0` uses the same first-eligible-holder search as projectile absorption,
returning `HeatDamping` rather than `PierceAbsorb`; no match returns1.

`0x7D16F0` refreshes an attached visual: resolves the owner bone, removes an old
`armor_<holder name>` child, loads/clones the equipped item's entity definition,
attaches it to the bone, then issues entity notification7. Its resource-reference
and attachment helpers remain separate lifetime work. Slot1 `0x7D1660` invokes
the base attach path then this refresh; slot7 `0x7D16B0` refreshes an existing item.

## Vehicle armor query objects

`0x7E7560` builds a `0x28`-byte query context:

| Offset | Input |
|---|---|
| `+0` | entity armor object, entity `+580` or shared default via `0x719FD0` |
| `+8` | hit geometry record |
| `+10` | selected armor setting |
| `+18` | projectile/damage parameters |
| `+20` | armor multiplier, default1; nonhuman actors apply parameter-kind B |
| `+24` | result of source/target helper `0xA46940` |

`0x7E9040` searches armor settings, stride `0xB8`, in armor `+8/+10`.
It returns the first setting whose volume-pattern vector `+88/+90` is empty or
matches the hit part name; no match returns shared storage `0x157BDB0`.
Pattern matching is through `0x676070`, not the exact tag predicate used for
equipped human armor. Precise pattern syntax remains a dependency.

Armor parser `0x7E8730`, registry `0x157BB70`, creates or reuses a setting by name:

| Field | Configuration |
|---|---|
| `+28` | `Setting` name tag |
| `+88/+90/+98` | `Volumes` pattern vector |
| `+A0/+A4/+A8/+AC` | `Thickness`, one/two/four-value form |
| `+B0` bit0 | `Disable_damage_acc` |
| `+B0` bit1 | `Whole_wound` |
| `+B0` bit2 | `Ray_cast_thickness` |
| `+0/+4` | `Threshold/Pierce/Koef`, `Value` |
| `+8/+C` | `Threshold/Blast/Koef`, `Value` |
| `+10` | `Threshold/Pierce/Calibre` |
| `+14/+18` | `Durability/Pierce/Koef`, `Value` |
| `+1C/+20` | `Durability/Blast/Koef`, `Value` |

One thickness value fills all four groups; two values fill the first group with
the first and the other three with the second. Wounds are parsed separately at
armor `+20` through `0x7EA860`; accumulation/destruction is still open.

## Thickness, angle and material calculations

Base thickness `0x7E7800` reads hit geometry face index `+24` (`-1` falls back to0).
The hit record's `+28` points to a volume instance, whose `+30` is its definition.
Volume definition thickness floats start at `+3B0`, one per face. Parser
`0x7969F0` and registry `0x1573270` identify the mapping:

| Face | Native side | Definition offset | Setting override |
|---|---|---|---|
| 0 | Front | `+3B0` | `+A0` |
| 1 | Top | `+3B4` | `+AC` |
| 2 | Left | `+3B8` | `+A4` |
| 3 | Right | `+3BC` | `+A4` |
| 4 | Rear | `+3C0` | `+A8` |
| 5 | Bottom | `+3C4` | `+AC` |

Zero setting overrides preserve the volume definition's value. With
`Ray_cast_thickness`, the detailed path queries shape slot3 and accepts results1/2;
an unexpected result reports unknown thickness and uses the original value.
The estimate path instead uses the smallest bounding-box extent. The geometric
thickness is converted by reciprocal spatial scale,1000 and material `+E0`.
The result is multiplied by armor object's `+0` scalar and query `+20` modifier.
Material `+E0`'s own parser and units are still open.

Projectile context builder `0x7E72E0` receives incidence cosine, uses native
`acosf` (`0xF51FE4` diagnostic proves the name), and stores degrees at `+8`.
The caller supplies caliber at `+4`, obtained from source definition `+1628`;
`0x82CD60` binds `Calibre` to that field. Context `+24` is the secant capped by
`g_bulletSlopeMax` (`0x1480CD8`), initialized to10 by `0x56F70`.

Angle tables supply power-law coefficients `+C/+10`, fixed factor `+14`, slope
factor `+18` or caliber damping `+1C`. Their sampled angles are in radians;
the stored degrees serve later material formulas. Helper `0x6C7F30` clamps below
the first knot but returns failure and0 **above the last knot**. That behavior
differs from the endpoint-clamping damage sampler `0x3FB290`. The full selection
chain tries the power-law table, then the fixed/slope tables, then damping, and
falls back to defaults when none applies. The power-law and damping paths also
handle a single knot as a constant; multi-knot inputs above the last knot fall
through to the next alternative/default.

Property dispatcher `0x82D390` obtains the projectile definition through
`0x829140(source+1600, registry0x1580F00)`; the accessor's ownership is still open.
Registry `0x1581710` supplies the following bindings:

| Token | Projectile definition | Parsed input |
|---|---|---|
| `BF OvermatchDamping` | vector `+138/+140/+148` | float angle in degrees, damping |
| `C0 OvermatchCalibre` | vector `+150/+158/+160` | integer angle in degrees, coefficient, exponent |
| `C1 OvermatchFixed` | vector `+168/+170/+178` | integer angle in degrees, fixed factor |
| `C2 OvermatchSlope` | vector `+180/+188/+190` | integer angle in degrees, slope factor |
| `B0 ProjectileDamageThreshold` | `+1A0` | float floored at1 |
| `AE ProjectileDamage` | `+198` | typed impact scalar |
| `AF ProjectileDamageClip` | `+19C` | float |
| `B1 HealthDamage` | `+1A4` | health scalar |
| `B2 HealthDamageClip` | `+1A8` | float |
| `B4 Speed` | `+260` | input multiplied by velocity unit scale |

Table angles are converted to radians during parsing. OvermatchDamping clears
its existing vector before appending and sorts it. The other three cases append;
there is no equivalent clear in those case bodies. This difference matters for
inherited/repeated definitions; complete definition construction remains open.
Setting ProjectileDamage also derives HealthDamage through typed conversion0→1
when existing health damage is negligible; HealthDamage similarly derives impact
through1→0. Other projectile properties are outside this parser slice.

For ordinary penetration, `0x7E7CB0` transforms base thickness T. Let C be caliber,
A incidence degrees and S capped secant. The initial slope multiplier is:

- Explicit power-law pair: `max(1, coefficient * (T/C)^exponent)`.
- Otherwise nonzero fixed factor: `max(1, fixed)`.
- Otherwise nonzero slope factor: `max(1, slope*S)`.
- Otherwise `1 + (S-1)*clamp((T/C)*damping, overmatchMin, overmatchMax)`.

`g_bulletOvermatchMin` is initialized to0.2 by `0x56BE0`; Max to1 by `0x56B40`.
These are writable named settings, not immutable constants. Sibling `0x7E8130`
returns this slope multiplier alone. For damage kind2, `0x7E7CB0` immediately
returns T and skips the following material factors.

Volume parser `0x7969F0` binds `HighHardness` at `+10`, positive `CastSteel` at
`+14`, and `Quality` at `+18`. Quality strings map no_flaw0, small_flaw1,
medium_flaw2, large_flaw3; unrecognized strings fall back to0.
The quality multipliers are respectively1 and the lesser of1 and:

- small flaw: `0.871 - 0.0004*A + 0.111*T/C`;
- medium flaw: `0.744 - 0.0008*A + 0.222*T/C`;
- large flaw: `0.741 - 0.0023*A + 0.235*T/C`.

When CastSteel is nonnegative, its multiplier is
`min(1, 0.001238*T + CastSteel - 0.0002628*C + 0.02706*T/C)`.
HighHardness enables `min(1, 9.7707*C^0.06111*(T/C)^0.2821*BHN(T)^-0.4363)`.
`0xA6DF70` samples BHN(T) from configured `brinell_hardness_multiplier`, already
identified in `set/damage.set`. Final effective thickness multiplies T by slope,
quality, cast-steel and hardness factors. There is no additional lower clamp in
these displayed material factors. Input invariants and every caller still matter.

Energy-threshold helper `0x7E8030` selects Pierce fields for kind0 and Blast
fields for kind2; other kinds assert. It obtains effective thickness, then uses
nonzero fixed Value; otherwise nonzero Koef times T, flooring the coefficient at
epsilon; otherwise T divided by projectile parameter `+20` (also floored when
nonzero); otherwise T/1.5. The earlier pass's vehicle damage callers use this
threshold or the detailed penetration thickness, preserving their different gates.

## Component health has two stages

`0x893550` accepts adjacent health floats A/B and positive damage. It consumes A
first, spilling excess into B and flooring B at0. When A remains positive, B is
unchanged. Its outer caller passes component `+14`, so the two current pools are
component `+14/+18`.

`0x895480` clears component `+25` bit0, resets `+10` to the time sentinel and
requests a state transition. A0→2 request explicitly visits1 before2.
`0x895530` sets `+25` bit1 and `+24` state. State0 restores `+14` from `+1C`
and removes matching wounds; state1 zeros `+14`; state2 zeros both current pools.
It resolves/rebuilds the hit-part context, dispatches the definition's event ID
at interaction offsets `+CC/+CE/+D0`, marks the owner dirty and sends entity
notification `1B(component,state)`. Native names for these states/events and full
repair/wound handling remain open.

`0x894720` supplies component damage scale, returning1 without a match.
Lookup `0x894750` tries a component-name tree first, then the first matching
component-tag rule. The scale resides at matching record `+8`. Rule parser,
precedence under duplicate definitions and record ownership remain open.

## Named events reach native actions

`0x76E0B0` resolves an event tag through entity definition `+548 -> +10`.
Its `+30/+38` vector contains `0x68`-byte records: tag `+0`, short event ID `+60`.
With the global event-disable gate clear, a matching non-`-1` ID invokes `0x76ABE0`.

`0x76ABE0` runs the definition's action layers through `0x76AE80`, creates entity
interaction-instance `+540` when necessary, then notifies subscribed extenders
through `0x766ED0`. The action table is definition `+18`, indexed by
`eventID + layer*definition+28`. Missing layers fall back downward. A nonzero
list flag requests prior-layer execution before the current list. Context bytes
`+188/+189` receive layer and event ID while higher context flag bits remain.
It selects an RNG context from entity state, temporarily binds it through
`0x554CE0`, executes the action-list adapter `0x761B50`, then restores the prior
context if nonnull. The adapter invokes slot2 of its `+10` subobject; concrete
list/conditional execution types remain an open static lead.

Instance event subscriptions at `+A0/+A8` have four-byte records: short event
ID and signed extender index byte. `0x766ED0` verifies the index against the
entity extender vector and calls extender slot8 with event ID and context.
This differs from the collection-wide numbered notification path from earlier passes.

## Knockdown, throw-off and recovery

Human action wrapper `0x743C30` requires entity type bit2 then invokes action
slot9. `Interaction::eKnockDown` uses `0x7446E0`: after its role gate it samples
the action `+28/+2C` duration pair (mean ± deviation), converts to native time,
and calls `0x937BF0(human,duration,reason3,force=false)`. It skips the random draw
when deviation is negligible. Pair parser `0x552FB0` zero-initializes and accepts
up to two floats.

Native KnockDown `0x937BF0` rejects role-disallowed, dead or already incapacitated
actors unless forced. Except for reason4, it first sends entity notification25.
It stores position at human `+13DC`, sets deadline `+13D4=now+duration`, clears
human `+1420` bit4 and optionally emits `eHumanKnockDownCommand`.
An ordinary duration requests FSM `knock-down` then calls vitality's state setter
with incapacitated=true, dying=false. The sentinel-duration branch instead requests
`kick` and `stop-move` (initializers `0x58530/0x58C50`).

`0x937DC0` requests recovery when a deadline exists, otherwise clears incapacitation
directly. `0x93ABE0` clears the deadline and bit4, optionally replicates, and clears
vitality/chassis state plus sends current order interrupt E when recovery is enabled.
Recovery is forced on for a victim who is not in vitality's dying state.
The relevant slice of `eHuman::Quant` (`0x939EC0`) runs while not finally dead and
the deadline is nonzero. It observes `keep-knock-down` (initializer `0x58500`), sets human
`+1420` bit4 and invokes `0x95FC00` with diagnostic `knockDownTime.Active()`.
On `deadline <= now`, it zeros the deadline and, when the role gate permits,
calls `0x93ABE0(human,recover=false,force=false)`. The dying-state override above
therefore determines whether this expiry clears incapacitation.

Human property registry `0x1586E60` names token71 `Knockdown`.
Load `0x938F70` parses that timer through `0x65E850`; save `0x939320` writes it
through `0x65E8A0` only while `+1420` bit4 is set. Save also preserves the active
FSM condition. Initialization `0x936860` restores the knockdown requests, saved
position and cleared bit4 using the remaining timer. The timer I/O helpers and
general clock serialization are separate dependencies.

`Interaction::eAction_Die` slot9 `0x7416A0` calls vitality `0x924BA0` after entity
role-mask checks; that method retains the personage/revivable distinction already
mapped. `ThrowOnGround` slot9 `0x744690` calls `0x937E20`, which sets FSM-derived
state bit0 then calls the entity transform/ground helper `0x6FDD20`.

ThrowOff parser `0x743C90` binds random pairs `up +28`, `dir +30`, `forward +38`,
`turn +40` (degrees converted to radians), flags `force +48`, `die +49`, probability
`rnd_die +4C` and `rotate_to_dir +50`. Its execution `0x749670` constructs velocity
from context direction, actor forward and upward motion, adds linked-parent velocity,
tests/adjusts the prospective trajectory and either dispatches a death event or
calls `0x93B520`. Assembly `0x7499D0..0x749A40` restores a calculation omitted by
the decompiler: turn angle is the same mean±deviation sample, passed in XMM0 to
the sine/cosine helpers before horizontal velocity rotation. Negligible deviation
skips the random draw. Trajectory geometry remains partial.

`0x93B520` can leave cover and interrupt the current order; its short-reaction
branch calls forced knockdown or forced Die. Its full-physics branch enables
simulation and reaches `0x93CBC0`, diagnosed `eHuman::StartThrowOff`. That method
updates volume flags, records human `+148C` fatal intent, creates simulator motion,
clears FSM requests and requests `throwoff`. Simulator callbacks and landing are open.

Installed `properties/human_fsm/request.inc` gives throwoff/knock-down/kick
priorities48/47/46, all interrupting; keep-knock-down has priority46 and duration2.
`human_fsm/die.inc` maps standing/lying knock-down conditions through `knock_down`
toward lie, and throwoff through throwoff_end toward lie/dead.
The human interaction definition's throw-off events add equipment/linked-actor
gates. `throw_off_end` requests knockdown4±1 unless `not_knocked`; `throw_high`
can call Die on an already senseless but living victim. Resource conditions and
native state machinery both contribute to the resulting behavior.

## Further static work

Ability lookup `0x9744E0` scans manager `+20/+28` info pointers. Info `+8` supplies
the ability; slot9 supplies its ID, slot5 checks applicability, and the first
matching applicable entry dispatches slot4 to fill requested parameters. This
resolves the lookup contract used by Avoidance. Actual level selection and info
mutation remain open.

Remaining leads include material coefficients, wounds and repair, concrete
action-list evaluation, full simulator landing transitions, and actor/subscription
lifetime. This is a productive static frontier, not a runtime boundary or a claim
of exhausted analysis.

## Evidence and verification

[Machine-readable map](../artifacts/build-25492057/armor-events-map.json) and
[validation](../artifacts/build-25492057/armor-events-validation.json) preserve
65 game contracts/slices, eight editor pairs,101 fields,18 read-only constants,
12 RTTI tables,43 literal/property stubs and seven installed resource hashes.
The frozen local exports contain92 game functions and nine editor functions;
export count is not semantic coverage. Eight editor comparisons cover selected
KnockDown/Die/ThrowOff/ThrowOnGround wrappers and armor-holder properties. They
agree on the reviewed offsets, gates and operations; full transitive equivalence
was not tested.

Independent LLVM disassembly matches1941 Ghidra instruction starts across ten
selected functions after accounting for LLVM's standalone LOCK prefix. Assembly
review specifically checks float argument/results and the omitted throw rotation
sample. Manifest validation checks binary identity, exact entries, instructions,
data and resources; it does not prove semantics, ABI, lifetime or completeness.
