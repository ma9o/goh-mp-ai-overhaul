# Where the inherited extender leaves us

Audited source: upstream commit `d994e1750de1c015ed2557250ab461ece73cd721`
(2025-12-11 UTC). This is a source inspection, not a runtime test of the author's
historical build.

## Useful foundations

The repository supplies a Windows proxy/loader, MinHook integration, Lua bridge
experiments, a plugin API design, Ghidra scripts, and earlier analysis output.
These are useful starting points for an extension boundary once current engine
interfaces are recovered.

`LUA_OFFSETS.md` describes a reported working Lua bridge for **v1.059.0**, a
2025-12-03 build. That report does not establish compatibility with the locally
installed Steam build **25492057**. Keep the two claims separate.

## Missing or inconsistent pieces

| Area | Source evidence | Consequence for a custom brain |
|---|---|---|
| World access | `sdk/coa_loader.cpp:223–229`: `g_GameWorld` starts null; `GetGameWorld()` returns it with a TODO, and no assignment was found | No established world/actor enumeration path |
| Unit enumeration | `sdk/coa_api.cpp:258–269`: `RefreshUnitCache()` only clears the vector | The high-level unit API cannot yet supply observations |
| Time, selection, spawning | `sdk/coa_api.cpp:248`, `293`, `427`, `433`: constant/null TODO returns | Several advertised operations remain stubs |
| Plugin lifecycle | `sdk/coa_plugin_api.cpp:411`, `511`: tick and initializer definitions; no calls to either were found in the inherited source | Declared plugin events do not establish an engine tick integration |
| Loader/API mismatch | `sdk/coa_loader.cpp:290`, `319`: `bool ModInit(void)` invocation; `mods/example_mod.cpp:256`: expects `const COA_PluginAPI*` | The old loader and new example disagree about their entrypoint |
| Event production | Event enum in `sdk/coa_plugin_api.h`; implemented producers found for tick and Lua-loaded in `sdk/coa_plugin_api.cpp` | Spawn, death, damage, and AI event declarations are not native event hooks |
| Lua addresses | Separate tables in `sdk/coa_lua_bridge.cpp` and `sdk/coa_plugin_api.cpp` disagree, including `pushstring` | Resolve one current, fingerprinted table before connecting a plugin API |
| AI labels | `sdk/coa_hooks.h` assigns `0xAFBBC0` to target selection, sensing, and accuracy parsing | Labels cannot be treated as verified native method names |
| Object layouts | `sdk/coa_sdk.h` / analysis headers contain proposed layouts without current-build verification | Do not use them as authoritative C++ structures |

`coa_loader.cpp` initializes hooks, the Lua bridge, the overlay, and its old mod
loader. It does not call `COA::PluginAPI::Initialize`. This matters separately
from missing offsets: updating addresses alone would not complete the API.

## Current address check

The new PE surveys record every hexadecimal `constexpr uintptr_t` in
`sdk/coa_hooks.h` and every hexadecimal `LUA_*` define in the bridge/plugin
sources, then check them against each executable's file-backed code and unwind
records. `function_identity_verified` remains false for all inherited entries.

For the current **game** executable, the inherited `AI_UPDATE` RVA `0x2419F0`
lies inside an unwind range rather than at its beginning. The same problem
affects several inherited AI and Lua entries. An unwind start still would not
prove identity, and leaf functions can lack unwind records; this is an early
rejection/triage check, not a complete function finder.

## First recovery priorities

1. Trace `Actor::eHumanBrain` and `Order::eBrainUpdate` to establish the native
   update/dispatch relationship and the brain's owning actor.
2. Trace `Helper::eCmd_actor_state` and `Helper::eCmd_actor_to_cover` from GEM
   command metadata to their execution paths. Recover native actions that a
   future external decision system can request.
3. Follow `fire_mode`, `move_mode`, `advance_ratio`, and `retreat_ratio` loaders
   to connect configuration fields to behavior.
4. Recover observation/identity/lifetime access for actors and targets, then
   decide the smallest engine boundary needed by a replacement brain.
5. Reconcile and wire the plugin API after the boundary and execution context
   are known. Lua API recovery can proceed independently where it helps expose
   that boundary.

This ordering targets individual battlefield decisions. Existing multiplayer
`BotApi` commands alone do not establish access to those decisions.
