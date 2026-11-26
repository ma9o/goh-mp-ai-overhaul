// Combat & ballistic AI translation unit:
//   - Feature 1: Grenade Dodge (5 hooks)
//   - Features 2 & 3: Soft-Cover LOF & Attack Detour (5 hooks)
//   - Target Prioritization & Multi-Turret (7 hooks)
//   - Close-Range Anti-Vehicle Grenades (1 hook)
//   - Advance and Attack orders (2 hooks), explosion signals (1 hook)
//   - Armor Penetration AT Aim & Ammo Selection (2 hooks)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "ai_modules.hpp"
#include "config_and_log.hpp"
#include "combat_orders_module.hpp"
#include "hit_the_dirt_module.hpp"
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

    const bool orders_ok = combat_orders::prepare_hooks(base, sha, create, remove);
    if (!orders_ok) log::write(log::Level::error, "Combat order hook staging failed");
    const bool dirt_ok = hit_the_dirt::prepare_hooks(base, sha, create, remove);
    if (!dirt_ok) log::write(log::Level::error, "Explosion signal hook staging failed");
    return g_GrenadeOk && g_LofOk && g_TargetPrioOk && g_CloseGrenadeOk && g_PenAtOk && orders_ok && dirt_ok;
}

void connect() {
    close_grenade::connect();
}

bool drain_quiescence() {
    return close_grenade::drain();
}

void deactivate() noexcept {
    hit_the_dirt::deactivate();
    combat_orders::deactivate();
    grenade_dodge::deactivate();
    lof_detour::deactivate();
    target_prio::deactivate();
    close_grenade::deactivate();
    penetration_at::deactivate();
}

std::uint32_t inflight_count() noexcept {
    return combat_orders::inflight_count() + hit_the_dirt::inflight_count() + grenade_dodge::inflight_count() +
           lof_detour::inflight_count() + target_prio::inflight_count() + close_grenade::inflight_count() +
           penetration_at::inflight_count();
}

void with_actor_quant(runtime::Address actor, ActorQuantNextFn next, void* ctx) {
    struct TargetPrioScope {
        runtime::Address owner;
        explicit TargetPrioScope(runtime::Address a) noexcept : owner(a) {
            target_prio::enter_actor_scope(owner);
        }
        ~TargetPrioScope() {
            target_prio::exit_actor_scope(owner);
        }
    } tp_scope(actor);

    if (!runtime::actor_ai_allowed(actor)) {
        next(actor, ctx);
        return;
    }
    grenade_dodge::on_actor_pre_quant(actor);
    const unsigned grenade_actor_id = close_grenade::poll_identity(actor);
    const unsigned squad_actor_id = close_grenade::poll_squad_identity(actor);
    next(actor, ctx);
    if (runtime::global_accepting.load(std::memory_order_acquire)) {
        close_grenade::poll(actor, grenade_actor_id);
        close_grenade::poll_squad_settle(actor, squad_actor_id);
    }
}

double nearest_tracked_grenade_m(runtime::Address scene, runtime::Tick now, runtime::Vec3 pos, float length) {
    auto& state = grenade_dodge::detail::state();
    std::lock_guard<std::mutex> lock(state.mutex);
    double nearest = std::numeric_limits<double>::quiet_NaN();
    if (state.scene == scene) {
        for (const auto& grenade : state.grenades) {
            if (now - grenade.last_tick > 300) continue;
            const auto distance = std::hypot(double(pos.x) - grenade.land_x, double(pos.y) - grenade.land_y) / length;
            nearest = std::fmin(nearest, distance);
        }
    }
    return nearest;
}

WeaponTargets vehicle_weapon_targets(runtime::Address brain) {
    namespace pr = target_prio::detail;
    WeaponTargets result{};
    const auto rows = pr::span(brain, 0x3b8);
    if (!rows.valid(pr::row_size) || rows.size(pr::row_size) > 32) return result;
    for (auto row = rows.first; row != rows.end; row += pr::row_size) {
        const auto weapon = runtime::read<runtime::Address>(row, 8);
        const auto target = pr::active_entity(weapon);
        if (!target) continue;
        const auto kind = pr::row_kind(weapon);
        if (kind == pr::RowKind::mg && !result.mg) result.mg = target;
        if (kind == pr::RowKind::main_gun && !result.cannon) result.cannon = target;
    }
    return result;
}

Stats snapshot_stats() noexcept {
    Stats s{};
    s.advance_unfreeze = combat_orders::stat_advance_unfreeze;
    s.attack_persists = combat_orders::stat_attack_persists;
    s.melee_prevented = combat_orders::stat_melee_prevented;
    s.trapped_hides = hit_the_dirt::stat_trapped_hides;
    s.hit_dirt = hit_the_dirt::stat_hit_dirt;
    s.ff_vetoes = close_grenade::stat_ff_vetoes.load(std::memory_order_relaxed);
    s.mg_small_arms_defer = close_grenade::stat_mg_small_arms_defer.load(std::memory_order_relaxed);
    s.dodges = grenade_dodge::total_dodges();
    s.player_escapes = grenade_dodge::total_player_escapes_preserved();
    s.soft_lof = lof_detour::total_soft_overrides();
    s.soft_rej = lof_detour::total_soft_rejects();
    s.det_inf = lof_detour::total_detours_inf();
    s.det_veh = lof_detour::total_detours_veh();
    s.at_volumes = penetration_at::detail::volumes.load(std::memory_order_relaxed);
    s.at_ammo = penetration_at::detail::shells.load(std::memory_order_relaxed);
    s.at_holds = penetration_at::detail::holds.load(std::memory_order_relaxed);
    s.settle_holds = penetration_at::detail::settle_holds.load(std::memory_order_relaxed);
    s.memory_holds = target_prio::stat_memory_holds.load(std::memory_order_relaxed);
    return s;
}

void after_hooks_removed() {
    hit_the_dirt::after_hooks_removed();
    combat_orders::after_hooks_removed();
    penetration_at::after_hooks_removed();
    close_grenade::disconnect();
    target_prio::after_hooks_removed();
    lof_detour::after_hooks_removed();
    grenade_dodge::after_hooks_removed();
    g_GrenadeOk = g_LofOk = g_TargetPrioOk = g_CloseGrenadeOk = g_PenAtOk = false;
}

} // namespace goh::tu_combat
