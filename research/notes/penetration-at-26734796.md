# Penetration-aware AT decisions for game build 26734796

The prototype adds two entry hooks to the 61 already staged by the DLL. Its host-facing build gate checks SHA-256 `81a13de787c1869b766004cdcadb9ff99012f0b1b58b89db9a7741b10b9f059a`. Static status is `runtime_allowed: false`. Scope is C-BATTLE target choice, the vehicle selector's ammo result and the native firing target. [Policy](../../mods/penetration_at_policy.hpp) · [Adapter](../../mods/penetration_at_module.hpp) · [Build manifest](../artifacts/build-26734796/penetration-at-26734796.json)

## Hook and ownership contracts

| RVA | Win64 interface and observed behavior |
| --- | --- |
| `0x92B6F0` | RCX is an initialized output `eTarget`, DL skips the native trial, R8 is the weapon place and R9 the target entity. The last two arguments are a tag and an optional previous target. The first instruction is `4c 89 4c 24 20`. MinHook continues at `0x92B6F5`. |
| `0x9A90C0` | The existing target-prioritization detour retains its row masks, reservation priority and mode gate. The new selector runs inside its current pass, including single-cannon assignments. A selected row must pass the point aimer at `0x833D80` with result other than 6. The point target uses the native tag constructor, point setter, target clear and tag destructor from `0x9A9306..0x9A93CA`. |
| `0x7E1AA0`, `0x7E35E0`, `0x7E1EC0` | The caller initializes its 0x40-byte location before selection. The adapter copy-assigns it at result+0x18 from the row record or a temporary native location constructed for a live inventory stack. It destroys only its temporary. Result+0/+8/+0x10 borrow the row, matched rule and ammo definition. The assignment caller resolves the location, requests its own reload if needed and destroys the result location. |
| `0x81F0C0` | RCX is the place, RDX the target, R8D the option, R9D the mode and the fifth argument the shot count. The first instruction is `48 89 5c 24 10`. MinHook continues at `0x81F0C5`. Only the mode-2, option-3 vehicle call returning to `0x9A85D1` can change to option 0. The original setter and replication path at `0x81FCB0` receive the resulting option and all other original arguments. Weapon update at `0x81BFE7` tests the option before setting the firing request bit. |

Both entries and the ranking and ammo callbacks require an admitted serial actor scope. The adapter checks the worker and remote-client predicates, owner/brain links, concrete brain, inhibition counter, chassis and weaponry controls, and fire posture. An explicit Attack can benefit from aiming at a volume on its already selected entity. The volume hook requires a current visible hostile observation. The other decisions defer during an active explicit Attack. [Target-prioritization contracts](target-prioritization-26734796.md)

## Profile damage and armor

The brief's proposed flat HEAT value has different units in this build. The parser at `0x851810` multiplies the cumulative property by world units per metre. Contact handling at `0x84B890` adds it to distance already traveled and stores a termination distance. The stock launcher ammo pattern describes that same behavior. The launcher weapon definition supplies a separate projectile damage table. Tagged HEAT uses its matched projectile profile and the native impact calculation. This preserves stock flat segments, range cutoffs, and profile-specific slope behavior. The manifest records hashes of the seven stock definitions used to check these bindings.

For each eligible clean muzzle, the scene query samples actual tagged volume centers. A useful result needs the target as first blocking contact, the requested live volume, a valid entry normal, direction and face. The scene record is 0x48 bytes. The volume writer at `0xA41520` tests the shape entry flag before writing kind 4. The query owner releases its scratch vector and final hit vector using native capacity and aligned-allocation conventions. Per-profile estimates use `0x841650` with distance bits in EDX, cosine in XMM2 at `0x7FC090`, `0x7FC310`, thickness estimate mode 1 and `0x7FCA60`. Impact resolution, damage application and RNG sampling stay in the engine. [Scene-hit and armor layouts](lof-detour-soft-cover-26734796.md)

The adapter represents up to ten candidates, giving each component family a place before duplicate names. It probes the first eight. Narrow tags retain their native meaning. For an empty or generic body tag, a clean body, turret, engine or ammo volume wins over a component fallback. Previous-volume stickiness requires a clean estimate. The native bind copies the primary tag, so the inverse lookup must resolve the exact candidate. For a trial, the native target position must be in range and the firing verdict must equal 3.

A request spends at most five native firing trials in total. The final ranked candidate can use the original selector with a unique tag and that bound volume as its previous target. After rejection, the original input pool gets a fallback when its native upper bound fits the remaining attempts. At exhaustion the adapter takes the native empty-target exit at `0x92BA09`. Replaying a full second selector could double the preview budget and advance the engine RNG. All scoring queries are independent of the native firing preview.

## Ammo, ranking and aim-only behavior

- The vehicle selector checks live availability, the current and proposed range profiles, native rule priority, reservations, the gunner gate and loaded-only selection. A full four-record row can contribute additional compatible, positive-count box stacks. The proposed definition stays scoped to the matching weapon and target in the current assignment.
- A ready or nearly ready chambered round with a clean lethal estimate wins. Among replacement rounds, the policy prefers a strong standard profile, then a viable HEAT profile. A replacement subcaliber round also needs a higher native penetration value than available standard ammo at this distance. HE can win on an exposed soft vehicle or as the component fallback. The native caller owns the reload.
- Ranking rewards a clean lethal volume. A below-threshold vehicle receives the full penalty only when an effective visible alternative is available and the comparison is complete. The danger adjustment checks actual gun components, including secondary guns. Visible human inventory and hand items supply the close AT threat signal. An isolated ordinary soldier beyond 40 m receives a ranking penalty in an unthreatened towed-gun or MG-less cannon assignment.
- Aim-only tracking requires a quiet actor. The adapter checks hostile aim, recent threatening or attacking observations, the sensor's incoming-fire stamp, the shared hull-damage observation and weaponry+0x90. The native firing-condition reader at `0x8A3FD2` supplies the latter binding. Against armor, tracking requires a complete, below-0.85 lethal estimate beyond 35 m. The isolated-infantry case requires positive inventory identification, a towed cannon or MG-less main gun, and distance beyond 40 m. Unknown armor, another close threat or a recent shot leaves normal native fire available.
- Cache keys include the simulation scene and tick, actor and target identities, weapon place, clean muzzle and weaponry revision. The cache shares contacts across ammo definitions and stores effective armor separately for each profile and volume. It resets on a different tick, including clock rewind or wrap, or scene change. There are 24 weapon/target entries and a total AT budget of 192 rays per tick. Exhaustion supplies no evidence for holding fire.
- Hook staging rolls back in reverse order. Shutdown closes admission, disables the entries and drains active detours and actor scopes before releasing trampolines. The shutdown log records volume choices, ammo handoffs and aim-only decisions. The DLL writes its log beside the loaded module.

## Offline verification

The fine-map verifier checked 60 method entries, 1,290 instruction locations, four RTTI tables, eight byte sequences and seven read-only archive members. The two hook RVAs are distinct from each other and all 61 prior entries. MinHook's x64 instruction copier displaced one complete five-byte instruction at each entry. Both tests checked the copied bytes and the relay and jump-back destinations. [Validation report](../artifacts/build-26734796/penetration-at-26734796-validation.json) · [Offline policy and adapter fixtures](../tests/test_penetration_at_26734796.cpp)

The focused export in `local-work/exports/build-26734796/call_to_arms-penetration-at` combines the existing game reports and small helper exports recorded in the manifest. To repeat the structural check against the local game data, run the following from the repository root.

```sh
python3 research/scripts/verify_fine_map.py \
  --map research/artifacts/build-26734796/penetration-at-26734796.json \
  --binaries-dir "$GAME_BINARIES" --resources-dir "$GAME_RESOURCES" \
  --exports-dir local-work/exports/build-26734796
```

All five C++ suites passed with ASan and UBSan, including the AT adapter and MinHook fixtures. The repository C++ lint and warning gate passed, and the compatibility environment passed all 36 Python tests. MinGW built the Windows x64 DLL with `-Wall -Wextra -Werror`. The resulting `local-work/build-grenade/goh_ai_v4_21.dll` exports `ShutdownAndUnload` and has SHA-256 `659bc614df6c729aace68a13bb9f96fa1469d5bf9e6199ca1430753531bb91af`.

Native callback timing, actual impact outcomes, replication and host/client agreement remain runtime verification items.
