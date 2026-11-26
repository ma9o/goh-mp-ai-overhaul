# Close-range infantry grenades · build 26734796

**Static review candidate.** This fills the Q5/Q6/Q9 infantry target, equipment and firing-output blanks for stock `call_to_arms.exe`, SHA-256 `81a13de787c1869b766004cdcadb9ff99012f0b1b58b89db9a7741b10b9f059a`. The manifest keeps `runtime_allowed: false`. [Implementation](../../mods/close_grenade_module.hpp) · [Fine map](../artifacts/build-26734796/close-grenade-26734796.json) · [Validation](../artifacts/build-26734796/close-grenade-26734796-validation.json)

## Choosing a vehicle and equipping the thrower

The targeting module gives a visible, recognized, hostile vehicle a score above 2 when the soldier has an eligible throwable, a usable aim point, sufficient clearance and an available reservation. An ordinary infantry target tops out at 1.32 including the 0.20 incumbent and 0.12 preference bonuses. The grenade candidate skips the shooter-to-target bullet probe and keeps precedence through the native picker. An independent equipment check runs at most once per soldier per 0.10 simulation seconds, including when stock rifle-range filtering removed the vehicle from the rank list. The range is the carried item's live throw range. Up to 7 m beyond it, a positive closing component of vehicle velocity allows pre-equipping. A release requires the aim point strictly inside range. [Target ranking](../../mods/target_prioritization_module.hpp)

At `0x95CEA0` the stock picker gates on `brain+0x47C`, rearms its poll for 0.25–1 s and sets 5–10 s after a mount. `0x96C140` calls it. Brain update `0x95A680` refreshes subjects. Battle states `0x960AE0/0x962990/0x962420` consume native battle and cover decisions. `0x9705F0/0x970990` can try a scratch weapon and a stochastic aim. The new path uses the stock mount factory directly, then a standalone native WeaponFire order once the grenade is in hand. An autonomous rifle burst can end immediately via `SetOrder`. The timer, grenade preparation time, stance and movement stay under their native owners. Throw readiness at `0x8335A0` can return 6 for prone stances 1/3 during a chassis transition. The policy waits for native readiness and never forces standing.

## Where the grenade is aimed

| Payload | Aim and first-contact requirement |
| --- | --- |
| AT grenade, satchel or dynamite | Bind an intact body volume. Require first contact with a volume on the intended vehicle and clearance for the full effect radius. |
| Molotov, including a definition tagged both `ic` and `at` | Scan all live volumes for an intact `engine` before accepting `body`, including on cars. A present but unresolvable intact engine defers the shot. Require first contact with the bound volume or, for an internal engine, its covering body panel within 0.6 m horizontally and 1.25 m above the engine center. |
| Frag | A stock `open_top` or `ammo_open_top_turret` property admits a compartment search. Unmarked armour requires an exact audited open-model key and an exposed occupied gunner station. Cars and crewed guns use live crew stations. Match a permanent crew link, occupant ID, visibility and hostility. Bind the exposed crew member or an eligible seat point. A tagged vacant primary gunner station needs a current bone and a clear vertical segment. Unmarked armour requires first contact with the intended crew member. A verified open deck can also accept contact within 0.55 m horizontally of the seat, from 1.25 m below to 0.15 m above it. Ground and intervening hull contacts fail. |

Stock `ammo.set` specifies the car body for Molotovs and `grenade_fg_target` for frags on armour. A case-insensitive byte census found zero occurrences of that tag in all 113,317 vehicle archive members and all 1,793 property archive members. Marder II, SU-76 and Wespe declare open tops. The Sd.Kfz. 251 C supplies linked `gunner2/gunner4` crew. The T-34/76 model 1941, Panzer IV H and Tiger I E do not qualify through passengers, external riders or a commander hatch. The manifest lists all 27 audited keys for unmarked armour. Other unmarked armoured models defer. [Stock member fingerprints and census](../artifacts/build-26734796/close-grenade-26734796.json)

Stock `0x92C9C0` matches the ammo rule. `0x95D360 → 0x92B5A0` dispatches ground or volume selection. `0x92B6F0` skips broken parts and can sample volumes using engine RNG. The module scans the native list, checks the part state and verifies that the primary volume name resolves back to the same instance. `0x852DC0` binds the entity and volume. The real throw aimer at `0x832EB0 → 0x852810` resolves that name, refreshes its transform and uses its world center. The Willys MB model declares a volume named `engine`. The T-34 and Sd.Kfz. 251 C use `engine1/engine2`. The stock `vehicle.ext` macro assigns the `engine` component and tag to numbered volumes up to `engine5`, preserving each volume’s own binding name. The native aimer can lead a moving entity. The placer binds link names at `0x791BD7` and records occupant identity at `0x78F575..0x78F585`.

## Authority, reservations and the release boundary

- Admit concrete human brains in the serial actor update with `!0xA7A80() && !0xC2B7A0()`. This includes SP, Skirmish, Conquest and multiplayer hosts for both control 0 and 1. Check Direct Control, the inhibition counter, active explicit Attack, Hold Fire and unalerted Return Fire. Hold Position permits a stationary throw. An active player movement order retains its parent and queue. Only an empty slot or a verified autonomous firing order may be replaced.
- Keep one managed carrier per vehicle across payload types. A pending claim lasts 3 simulation seconds and a committed throw holds the vehicle for 9 s. Recheck the native owner/bucket/spatial reservation at `0x89E880`. The native writer `0x89E9C0` draws RNG. Stock spawn performs its own bookkeeping. Rule bit 5 checks distance and cover through `0x7F5870` and `0x8AE880`. The suggested `0x7E0AC0` is inside a MOV in inventory code. Friendly clearance therefore includes the thrower and nearby allied actor bounds at both aim and sampled first contact. Use the largest configured effect radius, including a 2.5 m floor for incendiaries.
- The M32 satchel inherits 16 m throw range and has a 6 m outer blast area. The generic dynamite pattern has 6.2 m nominal range and a 12 m outer blast area. A charge whose safe throw envelope is empty stays in inventory.
- Stock `0x8EBAD0 → 0x8ED880` equips using the native inventory operation and command 0x10. `0x8EB9C0 → 0x91DB30 → 0x8ECBF0` installs and drives WeaponFire. The stock release at `0x8313B0` samples once. Its mode-2 command 0x0D and local spawn at `0x831FD0` share the accepted vectors. In SP the first-contact check runs on the local call. Custom code takes no RNG samples. The native release retains its ordinary draws, including when its sampled arc is vetoed. Diagnostics use the existing throttled `grenade_dodge.log` sink.

The only additional MinHook entry is the WeaponFire deleting destructor at **`0x8EF8E0`**. Its first complete instruction covers the five-byte entry jump. The existing actor, rank, picker, cover, WeaponFire, shooter lifecycle, send and spawn hooks supply the other callbacks. The reviewed checkout has 11 base, 7 targeting, 14 smoke and 3 tank-pathing entries, giving **36 unique entries** after integration. Shutdown can defer while a managed order or release is outstanding.

## Verification and review boundary

The focused game export has 25 exact entries. To reproduce it, derive the candidate TSV from `.methods[] | [.entry_rvas.game,.label] | @tsv` and use `run_headless.py --build-id 26734796 export --program call_to_arms.exe --focused` with output `local-work/exports/build-26734796/call_to_arms-close-grenade`. Run the verifier from the repository root with `GOH` set to the install directory.

```sh
python3 research/scripts/verify_fine_map.py \
  --map research/artifacts/build-26734796/close-grenade-26734796.json \
  --binaries-dir "$GOH/binaries/x64" --resources-dir "$GOH/resource" \
  --exports-dir local-work/exports/build-26734796
```

Offline fixtures cover incumbent precedence, bullet-probe exemption, closed-model rejection, engine fallback, compartment contacts, blast clearance, claim expiry, pointer reuse and paired send/spawn. Actual post-contact bounce, moving-target interception, frame cost, stance transitions, equipment latency and host/vanilla-peer agreement remain runtime review items. [Offline fixtures](../tests/test_close_grenade_26734796.cpp)
