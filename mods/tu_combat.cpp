// Combat & ballistic AI translation unit:
//   - Feature 1: Grenade Dodge (5 hooks)
//   - Features 2 & 3: Soft-Cover LOF & Attack Detour (5 hooks)
//   - Target Prioritization & Multi-Turret (7 hooks)
//   - Close-Range Anti-Vehicle Grenades (1 hook)
//   - Armor Penetration AT Aim & Ammo Selection (2 hooks)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "ai_modules.hpp"
#include "config_and_log.hpp"
#include "grenade_dodge_module.hpp"
#include "lof_detour_module.hpp"
#include "penetration_at_module.hpp"

namespace goh::tu_combat {

namespace {

bool g_GrenadeOk = false;
bool g_LofOk = false;
bool g_TargetPrioOk = false;
bool g_CloseGrenadeOk = false;
bool g_PenAtOk = false;

} // namespace

bool stage_hooks(runtime::Address base, const char* sha, HookCreateFn create, HookRemoveFn remove) {
    g_GrenadeOk = grenade_dodge::prepare_hooks(base, sha, create, remove);
    if (!g_GrenadeOk) {
        log::write(log::Level::error, "ERROR: grenade_dodge::prepare_hooks failed");
    } else {
        log::write(log::Level::summary, "Grenade Dodge 5 hooks staged cleanly");
    }

    g_LofOk = lof_detour::prepare_hooks(base, sha, create, remove);
    if (!g_LofOk) {
        log::write(log::Level::error, "ERROR: lof_detour::prepare_hooks failed");
    } else {
        log::write(log::Level::summary, "Soft-Cover LOF + Attack Detour 5 hooks staged cleanly");
    }

    g_TargetPrioOk = target_prio::install(base, sha, create, remove, &log::sink_summary);
    if (!g_TargetPrioOk) {
        log::write(log::Level::error, "ERROR: target_prio::install failed");
    } else {
        log::write(log::Level::summary, "Target Prioritization 7 hooks staged cleanly");
    }

    void* fire_tramp = nullptr;
    const runtime::Address fire_rva = runtime::table().hook.weapon_fire_dtor;
    g_CloseGrenadeOk = create(reinterpret_cast<void*>(base + fire_rva),
                              reinterpret_cast<void*>(close_grenade::fire_destroy), &fire_tramp) == 0;
    if (g_CloseGrenadeOk) {
        close_grenade::original_fire_destroy = reinterpret_cast<close_grenade::Destroy>(fire_tramp);
        log::write(log::Level::summary, "Close Grenade WeaponFire::~dtor hook staged @ 0x%llx",
                   static_cast<unsigned long long>(fire_rva));
    } else {
        log::write(log::Level::error, "ERROR: close_grenade fire_destroy hook failed");
    }

    g_PenAtOk = penetration_at::prepare_hooks(base, sha, create, remove);
    if (!g_PenAtOk) {
        log::write(log::Level::error, "ERROR: penetration_at::prepare_hooks failed");
    } else {
        log::write(log::Level::summary, "Penetration AT 2 hooks staged cleanly");
    }

    return g_GrenadeOk && g_LofOk && g_TargetPrioOk && g_CloseGrenadeOk && g_PenAtOk;
}

void connect() {
    close_grenade::connect();
}

bool drain_quiescence() {
    return close_grenade::drain();
}

void deactivate() noexcept {
    grenade_dodge::deactivate();
    lof_detour::deactivate();
    target_prio::deactivate();
    close_grenade::deactivate();
    penetration_at::deactivate();
}

std::uint32_t inflight_count() noexcept {
    return grenade_dodge::inflight_count() + lof_detour::inflight_count() + target_prio::inflight_count() +
           close_grenade::inflight_count() + penetration_at::inflight_count();
}

void with_actor_quant(runtime::Address actor, ActorQuantNextFn next, void* ctx) {
    grenade_dodge::on_actor_pre_quant(actor);
    struct TargetPrioScope {
        runtime::Address owner;
        explicit TargetPrioScope(runtime::Address a) noexcept : owner(a) {
            target_prio::enter_actor_scope(owner);
        }
        ~TargetPrioScope() {
            target_prio::exit_actor_scope(owner);
        }
    } tp_scope(actor);

    const unsigned grenade_actor_id = close_grenade::poll_identity(actor);
    next(actor, ctx);
    if (runtime::global_accepting.load(std::memory_order_acquire)) {
        close_grenade::poll(actor, grenade_actor_id);
    }
}

Stats snapshot_stats() noexcept {
    Stats s{};
    s.dodges = grenade_dodge::total_dodges();
    s.soft_lof = lof_detour::total_soft_overrides();
    s.soft_rej = lof_detour::total_soft_rejects();
    s.det_inf = lof_detour::total_detours_inf();
    s.det_veh = lof_detour::total_detours_veh();
    s.at_volumes = penetration_at::detail::volumes.load(std::memory_order_relaxed);
    s.at_ammo = penetration_at::detail::shells.load(std::memory_order_relaxed);
    s.at_holds = penetration_at::detail::holds.load(std::memory_order_relaxed);
    return s;
}

void after_hooks_removed() {
    penetration_at::after_hooks_removed();
    close_grenade::disconnect();
    target_prio::after_hooks_removed();
    lof_detour::after_hooks_removed();
    grenade_dodge::after_hooks_removed();
    g_GrenadeOk = g_LofOk = g_TargetPrioOk = g_CloseGrenadeOk = g_PenAtOk = false;
}

} // namespace goh::tu_combat
