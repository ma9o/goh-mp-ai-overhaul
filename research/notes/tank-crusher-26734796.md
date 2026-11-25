# Tank BattleCrusher and front-armor reverse pathing

Steam build **26734796**, game PE SHA-256 `81a13de787c1869b766004cdcadb9ff99012f0b1b58b89db9a7741b10b9f059a`. **`runtime_allowed: false`**. RVAs below are relative to the game image. Verification covered the installed PE, stock archive members, a standalone policy test and a cross-build.

### Which vehicles qualify

In the installed `gamelogic.pak`, `set/PATHER.SET` declares `BattleCrusher` (`profile+0xF0` bit `0x10`) on the `heavy_tank` (Priority 6) and `medium_tank` (Priority 5) templates, while `light_tank` omits the bit and uniquely uses Priority 4 (`profile+0xF4 == 4`). Amphibious tracked tanks (`t37`/`t40`/`lvt4`/`shermandd`) use Priority 3 with `eChassisTrack` (`0x1105E80`) and tank target classes (`tank_light`/`tank_medium`/`tank_heavy`/`tank`). The module admits all three tank groups on the main thread when constructing planning contexts and scoring reverse paths, while strictly preserving native `100000` blocks (so light tanks remain blocked from `building`/`building_vaultable`). The registry contains 21 obstacle classes and both occurrences of `flag_crush_in_battle` are comments. Wire has no separate class in this registry; wire entities inherit `post` (`properties/wire.inc`) and receive the post cost.

| Native boundary | Evidence on this PE |
| --- | --- |
| Actor context `0x8DF010` | Windows x64 RCX is destination, RDX is actor, RAX returns destination. `0x8DF130` indexes 0xF8-byte profiles by the byte at actor+0xDB4. Context+0xD0 borrows the profile. `0x8DF293–0x8DF2A3` copies area and actor IDs to +0x78/+0x7C. `0x8DF0E1–0x8DF111` derives context flag 0x10 from profile+0xF0 bit 4 and brain+0x39C bit 13. Copy `0x8C46A0` preserves those IDs, the profile and bit 4 (`0x8C48CA–0x8C48D7`), including the request copy at `0x8DDBE3`. |
| Object cost `0x8C3650` | RCX context, RDX direction XY pointer, R8 area, XMM0 float result. Area+0x3F is an unsigned class index, 0xFF means absent. `0x8DF6E0/0x8DF790` establish bounds at 0x15B3110/18, stride 0x40, MSVC string at +0 with size/capacity +0x10/+0x18 and inline capacity 15. Flags are at +0x20 and +0x28 points to per-profile float costs. The 0x10/4 shortcut is at `0x8C384E`. The hard cost loaded at `0x8C3951` is 100000. |
| Crush physics | `0x76AC6C` calls this same context constructor. `0x76AC7D–0x76ACB1` reads the **profile** bit and native obstacle flags/costs. This constructor caller receives the stock context. The module writes only bit 4 of an admitted planning context. Expansion additionally requires zero registered classes with the native crush flag set. |
| Track path estimate `0x86F430` | Primary track vtable 0x1105E80 slots 65/66 point to EstimatePathTime/CompilePath. RCX chassis, RDX stats, XMM0 float. `0x883F10` accumulates native-unit forward/reverse lengths at +8/+C, signed switch count at +10 and radians at +14. `0x4CF3F6/0x4CF441` set 0x15754A0 to the reciprocal of the world length scale. Native weights are 0.15/0.19 per metre, 2 per switch and 2.7 per full turn. |
| Gear choice `0x86F490` | Hull XY at chassis+0x5C, actor at +0x10. Bits 6/7 at +0x3C0 force reverse/forward. For multi-point paths a negative first-segment dot admits the comparison at `0x86F785/0x86F7B3`. Two-point paths score candidates via `0x855700` after their forced-gear checks. Only these three estimate callers qualify. |

### Path policy

The serial actor slot `0x804AC0` identifies the simulation thread. The quant-worker predicate `0xA7A80` compares the registered thread kind with 3. The remote predicate `0xC2B7A0` checks the multiplayer context, mode and authority virtual at +0xC8. A live non-remote scene also admits single-player. Tank checks require the concrete vehicle brain vtable 0x101FD68 and intact actor/brain/chassis links. Direct Control at chassis+0x18, weaponry+0x70 bit 1, and the six-bit inhibition counter at brain+0x39C inhibit adjustments. Because the module never creates, cancels, or hijacks orders—it only refines obstacle costs and gear selection for an already-issued move—active player right-click move orders (`brain+0x39C` bit 28) and Hold Position (`move_mode == 1`) remain eligible.

Battle expansion accepts brain+0x39C bits 13–16 (rebuilt at `0x91C350`), a future vehicle-brain+0x3F8 threat deadline (written at `0x9A97B2`, compared at `0x9A8F6E`), or a fresh known hostile in the owner's retained sensor category 1. The sensor path uses subject+0x28 last-observed XY with targetable-enemy and known-position flags (`(flags14 & 0x1E) == 2`, accepting newly-firing enemies even before recognition delay finishes) and requires current visibility (`flags10` bit 29) or observation age <= 20 s (`subject+0x64`). Removed, suppressed, detached, stale, and allied contacts are excluded.

The main thread classifies obstacle names at registry initialization for this scene. The worker calls the original cost function once and uses atomic reads of the authority, actor admission and class. It reads the engine's copied context and area, without accessing a live brain, sensor, registry string, logger or RNG. A direct-mapped admission slot compares both copied IDs. The serial actor dispatch and constructor refresh eligibility. An in-flight search observes the last published admission until the next check. Eviction or revocation falls back to native cost. Statistics count admitted contexts, newly expanded battle contexts and discounted reverse estimates. Disassembly of the MinGW DLL confirms that this detour has a single call to the original and uses plain loads for its atomic reads.

| Positive, traversable native cost | March cap | Battle cap |
| --- | ---: | ---: |
| stuff | 2 | 0.5 |
| construction, construction_vaultable | 4 | 1 |
| small_building, small_building_vaultable | 8 | 2 |
| tree, post | 12 | 3 |
| building, building_vaultable | native | 25 |

Caps only lower an existing cost. Zero, hard costs, impregnable areas, damage/human flags, marine and all other names pass through. For a fresh hostile within 180 m with normalized hull dot above 0.35, a reverse-dominant candidate of at most 45 m and at most 135° (`2.356 rad`, allowing pullbacks around a 90° cover corner) gets 0.08 seconds per reverse metre deducted. Native turn and gear-switch terms still apply. This biases only candidates the engine generates.

### Replication and verification

Move checks its remote flag and existing compiled path at `0x90B672–0x90B6A9`, then gates the native move command at `0x90B71F`. Receiver `0xBBB30` reads the raw waypoints and the compiled movement payload. The module emits no orders or custom packets. Vanilla-client agreement on route, gear, final facing and crush physics still requires a private host/client smoke test. [Move command contract](grenade-escape-26734796.md)

The exported shutdown marks the module inactive, disables all MinHook detours, and drains an atomic `inflight` guard counter covering all three hooks (up to 500 ms plus 50 ms margin) before `MH_Uninitialize` so mid-match hot-unload remains safe without blocking on scene teardown.

Stock member SHA-256 values are `ce3b57f48f77fd8315a5449d9c7da1b73386b67c5a3592f649aa135bf021d13a` for `set/PATHER.SET` and `9e11b782a2d4b971267f87bde44a52da184801442faaa50b3327968673d1f76d` for `set/registry/obstacles.reg`. The standalone suite runs with `c++ -std=c++17 -pthread research/tests/test_tank_crusher.cpp -o local-work/build-grenade/test_tank_crusher && local-work/build-grenade/test_tank_crusher`.
