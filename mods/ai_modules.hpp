#pragma once

#include "game_runtime.hpp"

#include <cstdint>

namespace goh {

// MinHook-compatible plain function pointers so translation units do not need
// to include MinHook.h inside this shared interface header.
using HookCreateFn = int (*)(void* target, void* detour, void** trampoline);
using HookRemoveFn = int (*)(void* target);
using ActorQuantNextFn = void (*)(runtime::Address actor, void* ctx);

namespace tu_combat {

struct Stats {
    std::uint32_t dodges{};
    std::uint32_t player_escapes{};
    std::uint32_t soft_lof{};
    std::uint32_t soft_rej{};
    std::uint32_t det_inf{};
    std::uint32_t det_veh{};
    std::uint32_t at_volumes{};
    std::uint32_t at_ammo{};
    std::uint32_t at_holds{};
    std::uint32_t settle_holds{};
    std::uint32_t memory_holds{};
    std::uint32_t ff_vetoes{};
    std::uint32_t mg_small_arms_defer{};
};

constexpr unsigned kHookCount = 20; // grenade_dodge(5) + lof_detour(5) + target_prio(7) + close_grenade(1) + at(2)

bool stage_hooks(runtime::Address base, const char* sha, HookCreateFn create, HookRemoveFn remove);
void connect();
bool drain_quiescence();
void deactivate() noexcept;
std::uint32_t inflight_count() noexcept;
void with_actor_quant(runtime::Address actor, ActorQuantNextFn next, void* ctx);
Stats snapshot_stats() noexcept;
void after_hooks_removed();

} // namespace tu_combat

namespace tu_infantry {

struct Stats {
    std::uint32_t smoke_veh{};
    std::uint32_t smoke_inf{};
    std::uint32_t smoke_med{};
    std::uint32_t smoke_rel{};
    std::uint32_t med_claim{};
    std::uint32_t med_fetch{};
    std::uint32_t med_smoke{};
    std::uint32_t med_rev{};
    std::uint32_t med_backoff{};
    std::uint32_t sup_trip{};
    std::uint32_t sup_refill{};
    std::uint32_t tact_ord{};
    std::uint32_t overflow_cover{};
    std::uint32_t dismount_spread{};
    std::uint32_t obj_pursue{};
    std::uint32_t obj_cap{};
    std::uint32_t fast_swaps{}, missile_holds{}, squad_ammo{}, squad_bandages{}, scavenge{};
    std::uint32_t heal{}, burn{}, kit{}, window{}, stance{}, reload{}, board{}, eligible{};
    std::uint32_t allegiance_veto{}, towed_gun_suppress{};
    std::uint32_t move_nudge{}, move_resume{}, move_abort{};
};

constexpr unsigned kHookCount = 43; // support(4) + smoke/medic(15) + tactical(15) + engine fixes(8) + weapon swap(1)

bool stage_hooks(runtime::Address base, const char* sha, HookCreateFn create, HookRemoveFn remove);
void on_hooks_enabled(runtime::Address base);
void begin_shutdown() noexcept;
bool drain_quiescence();
void deactivate() noexcept;
std::uint32_t inflight_count() noexcept;
void with_actor_quant(runtime::Address actor, ActorQuantNextFn next, void* ctx);
Stats snapshot_stats() noexcept;
void after_hooks_removed();
bool runtime_allowed_flag() noexcept;

} // namespace tu_infantry

namespace tu_tanks {

struct Stats {
    std::uint32_t tank_ctx{};
    std::uint32_t tank_exp{};
    std::uint32_t tank_disc{};
    std::uint32_t tank_rev{};
    std::uint32_t rep_full{};
    std::uint32_t rep_tt{};
    std::uint32_t rep_emit{};
    std::uint32_t rep_assign{};
    std::uint32_t rep_chain{};
    std::uint32_t rep_board{};
    std::uint32_t rep_pick{};
    std::uint32_t crew_total{};
    std::uint32_t crew_breed{};
    std::uint32_t crew_inf{};
    std::uint32_t crew_internal{};
    std::uint32_t crew_nontank{};
    std::uint32_t safe_doors{}, transport_unloads{};
    std::uint32_t tank_face{}, tank_face_retargets{}, tank_face_timeouts{};
    std::uint32_t convoy_yield{}, convoy_pass{}, convoy_resume{};
};

constexpr unsigned kHookCount = 9; // tank crusher(3) + auto-repair(5) + exit door(1)

bool stage_hooks(runtime::Address base, const char* sha, HookCreateFn create, HookRemoveFn remove);
void begin_shutdown() noexcept;
bool drain_quiescence();
void deactivate() noexcept;
std::uint32_t inflight_count() noexcept;
void with_actor_quant(runtime::Address actor, ActorQuantNextFn next, void* ctx);
Stats snapshot_stats() noexcept;
void after_hooks_removed();

} // namespace tu_tanks

} // namespace goh
