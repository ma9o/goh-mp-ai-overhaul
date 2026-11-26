# Stationary tank facing on Steam build 26734796

The unified DLL evaluates hull facing from the existing serial actor post-quant callback, after auto-repair and crew refill. The only gameplay output is a native point-target Rotate order. The module adds zero hooks, draws no engine RNG and requires game SHA-256 `81a13de787c1869b766004cdcadb9ff99012f0b1b58b89db9a7741b10b9f059a`. Verification is static and offline. `runtime_allowed: false`. [Adapter](../../mods/tank_facing_module.hpp) · [Policy](../../mods/tank_facing_policy.hpp)

## Eligibility and observations

Only an upright, operatable tracked tank with a working hull, engine (when fitted), tracks and a living seated driver can turn. Admission requires the concrete vehicle brain and tracked chassis, valid ownership and route, a tank class or definition tag, and absence of a cannon extender. Burning, sinking, destroyed, exploded, abandoned, linked, immobilized or repair-busy tanks yield. Direct Control, manual weaponry, brain inhibition, move/follow/user-facing flags, queued interface orders, a pending autonomous approach, and another module's higher-priority claim also block issuance. Consecutive actor samples detect continued horizontal translation after a movement order clears.

The host reads committed observations from the tank's sensor and vehicle-brain candidate vector. With no visible heavy threat of its own, it also checks operational allies within 45 game metres. A visible contact or a known position observed within ten simulation seconds qualifies within 200 metres. The decision uses the reported position at subject `+0x28`. It deduplicates by entity, or within four metres for position-only reports, and retains up to 16 contacts. Successive scans are at least 0.4 simulation seconds apart, including when the brain refreshes its threat deadline.

The policy ranks tanks and working cannon of at least 25 mm as heavy threats. Armed lighter vehicles and infantry with a tank attack rate or nearby carried AT gear count as light AT. Ordinary infantry count only within 18 metres while attacking or threatening. A position-only report needs a positive tank attack rate above 0.1 to qualify. Visibility, distance and hostile activity affect weight. A contact targeted by the main cannon gets a 1.85 multiplier to keep the hull committed when armor threatens from opposite flanks. Hold Fire filters out unaware contacts unless the shared recent hull-hit observation or the tank's penetration-effect tag indicates damage.

## Turning and order ownership

The policy compares the current heading, 36 headings at ten-degree intervals, and each qualifying contact's exact bearing. It scores the simulated front, side and rear exposure, with a turning penalty of 0.12 per radian. The selected heading needs at least 25% improvement in exposure and a yaw change of at least 24° by default. The target point lies 50 game metres along that heading at the tank's current height. Hull direction comes from chassis `+0x5C`, with entity `+0x50` as the fallback.

| Situation | Behavior |
| --- | --- |
| A foreign order is active | Leave it under engine or player control. A queued order also blocks installation. |
| An owned rotation is active | A retarget requires a heading change of at least 28° and observes the reissue cooldown. Ownership checks the current pointer, class, actor binding, and copied target point. |
| A rotation was issued recently | Wait four seconds from issue. A heavy contact at least 50° off the hull heading can shorten this to two seconds. |
| The order ends or is preempted within 1.5 seconds | Wait another 4.5 seconds from observing the loss before issuing any rotation. |
| The order is still active at eight seconds | Count one timeout, relinquish tracking and back off for six seconds. The engine retains the order. Further turns require a free order slot. |
| The scene, actor identity, owner, brain or clock changes | Discard stale ownership. A surviving order still occupies its slot. |

## Native contract

All RVAs below refer to the fingerprinted game executable. The rotate and chassis primary vtables, brain setter and chassis dispatch were checked against that PE without launching it.

| Boundary | Static evidence |
| --- | --- |
| Stack point target and factory | The adapter initializes an aligned 0x80-byte target with vtable `0x101FD18`, initializes its embedded name through `0x564D50`, and assigns the point through `0x853180`. Factory `0x90AB40` copies the target into order `+0x40` through `0x852910`. The adapter supplies `0.0f` for the scalar at `+0xC4`, then releases the temporary via `0x853240` and `0x565010`. |
| Installation | Vehicle-brain vtable slot 13 (`+0x68`) resolves to `0x91DB30`. The stock root path at `0x9AEDA0` constructs Rotate at `0x9AF3AC`, obtains a sequence using `0x92ABB0` (which calls the setter at `0x92ABEE`), and appends Rotate through `0x919270`. This module uses the same setter directly for a single order. It rechecks authority after allocation, discards an uninstalled order if authority changes, and records installation only after reading the pointer back from brain `+0xF0`. |
| Execution and replication | Startup audits `Order::eRotate` (`0x110C760`) and `Actor::eChassisTrack` (`0x1105E80`). Rotate slot 19 is `0x90AED0`. Phase helper `0x90AD10` records host-emit and remote-client flags, manages the rotation latch and phase timing. The host start path builds native `eOrderRotateCommand` with ID `0x18` at `0x90AF8A` and calls `0xBC260`. Native phase handling owns the subsequent sync and drop. |
| Hull motion | Tracked-chassis slot `+0x220` resolves to `0x870270`. It reads the point through `0x8527F0`, subtracts the actor position and passes the direction to chassis slot `+0x270`. The adapter writes no chassis, weaponry, brain policy, or transform fields. |

## Configuration and verification

The module defaults to enabled. Set `tank_facing = false` under `[modules]` to disable it. Under `[tuning]`, `tank_facing_min_angle_deg` defaults to 24 and clamps to 15–60. The unload summary reports successful installations in `tankFace`, the retarget subset in `faceRetarget`, and stalled orders in `faceTimeout`.

Offline fixtures use synthetic actor, brain, sensor, component, seat and order data. They exercise angular scoring, stealth, local and allied observations, authority changes during order creation, cooldowns, timeouts, recycled allocations, scene changes and clock rewind. The architecture suite covers INI bounds and RTTI registration. [Facing fixtures](../tests/test_tank_facing_26734796.cpp) · [Architecture fixtures](../tests/test_architecture_26734796.cpp) · [Build and test commands](../../DEVELOPMENT.md)

Live smoke validation remains pending for host/client agreement on hull yaw, preemption latency and turret firing/reloading continuity on this exact build.
