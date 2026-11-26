// Infantry, support & tactical maneuver translation unit:
//   - Shared AI Support order guards (4 hooks)
//   - Auto-Smoke + Auto-Medic (15 hooks)
//   - Auto-Resupply (0 extra hooks)
//   - Tactical Infantry Movement & Cover (15 hooks)
//   - Weapon swap (1 hook) and squad logistics (post-quant)
//   - Crush scanner and root hazard arbitration (3 hooks)
//   - Engine fixes (8 hooks) and lull reloading
//   - Tactical Objectives: Pursue & Cap (0 extra hooks)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "ai_modules.hpp"
#include "config_and_log.hpp"
#include "combat_logistics_module.hpp"
#include "crush_avoidance_module.hpp"
#include "hit_the_dirt_module.hpp"
#include "squad_logistics_module.hpp"
#include "recrew_weapons_module.hpp"
#include "scavenge_module.hpp"
#include "engine_fixes_module.hpp"
#include "movement_recovery_module.hpp"
#include "tactical_objectives_module.hpp"

namespace goh::tu_infantry {

namespace {

bool g_SupportOk = false;
bool g_SmokeOk = false;
bool g_TacticalOk = false;

} // namespace

bool stage_hooks(runtime::Address base, const char* sha, HookCreateFn create, HookRemoveFn remove) {
    ai_support::log_sink = &log::sink_summary;
    g_SupportOk = ai_support::prepare_hooks(base, sha, true, create, remove);
    if (!g_SupportOk) {
        log::write(log::Level::error, "ERROR: ai_support::prepare_hooks failed");
    } else {
        log::write(log::Level::summary, "AI-Support 4 order/dtor hooks staged cleanly");
    }

    auto_smoke::log_sink = &log::sink_summary;
    g_SmokeOk = auto_medic::prepare_smoke_hooks(base, sha, true, create, remove);
    if (!g_SmokeOk) {
        log::write(log::Level::error, "ERROR: auto_medic::prepare_smoke_hooks failed");
    } else {
        log::write(log::Level::summary, "Auto-Smoke + Auto-Medic 15 hooks staged cleanly");
    }

    g_TacticalOk = tactical_infantry::prepare_hooks(base, sha, true, create, remove);
    if (!g_TacticalOk) {
        log::write(log::Level::error, "ERROR: tactical_infantry::prepare_hooks failed");
    } else {
        log::write(log::Level::summary, "Tactical Infantry 15 hooks staged cleanly");
    }

    const bool fixes_ok = engine_fixes::prepare_hooks(base, sha, create, remove);
    if (!fixes_ok) log::write(log::Level::error, "Engine fixes hook staging failed");
    const bool swap_ok = combat_logistics::prepare_hooks(base, sha, create, remove);
    if (!swap_ok) log::write(log::Level::error, "Weapon swap hook staging failed");
    const bool move_rec_ok = movement_recovery::init(base, sha);
    if (!move_rec_ok) log::write(log::Level::error, "Movement recovery initialization failed");
    const bool crush_ok = crush_avoidance::prepare_hooks(base, sha, create, remove);
    if (!crush_ok) log::write(log::Level::error, "Crush scanner and hazard priority hook staging failed");
    return g_SupportOk && g_SmokeOk && g_TacticalOk && fixes_ok && swap_ok && move_rec_ok && crush_ok;
}

void on_hooks_enabled(runtime::Address base) {
    tactical_objectives::reset();
    const runtime::Address master_ptr = runtime::read<runtime::Address>(base, runtime::table().glob.master_client);
    const runtime::Byte local_pl = runtime::read<runtime::Byte>(base, runtime::table().glob.local_player);
    const auto route = auto_smoke::vehicle::vehicle_control_route(base, master_ptr, local_pl);
    log::write(log::Level::summary, "Auto-Smoke vehicle route check: master=0x%llx localPl=%u routeCtx=0x%llx",
               static_cast<unsigned long long>(master_ptr), static_cast<unsigned>(local_pl),
               static_cast<unsigned long long>(route.context));
}

void begin_shutdown() noexcept {
    recrew_weapons::begin_shutdown();
    scavenge::begin_shutdown();
    movement_recovery::begin_shutdown();
    squad_logistics::begin_shutdown();
    tactical_objectives::shutdown();
}

bool drain_quiescence() {
    ai_support::accepting.store(false, std::memory_order_release);
    const auto drained = [] {
        return ai_support::quiescent() && auto_medic::quiescent() && squad_logistics::quiescent() &&
               recrew_weapons::quiescent() && scavenge::quiescent() && movement_recovery::quiescent();
    };
    for (int wait = 0; wait < 20 && !drained(); ++wait) {
        Sleep(10);
    }
    return drained();
}

void deactivate() noexcept {
    tactical_objectives::shutdown();
    crush_avoidance::deactivate();
    movement_recovery::deactivate();
    combat_logistics::deactivate();
    engine_fixes::deactivate();
    ai_support::deactivate();
    auto_smoke::deactivate();
    tactical_infantry::deactivate();
}

std::uint32_t inflight_count() noexcept {
    return crush_avoidance::inflight_count() + movement_recovery::inflight_count() +
           combat_logistics::inflight_count() + engine_fixes::inflight_count() + ai_support::inflight_count() +
           auto_smoke::inflight_count() + tactical_infantry::inflight_count();
}

void with_actor_quant(runtime::Address actor, ActorQuantNextFn next, void* ctx) {
    auto_smoke::ActorQuantScope smoke_scope(actor);
    ai_support::ActorQuantScope support_scope(actor);
    if (!runtime::actor_ai_allowed(actor)) {
        next(actor, ctx);
        return;
    }
    const auto support_actor_id = (runtime::global_accepting.load(std::memory_order_acquire) && ai_support::base &&
                                   ai_support::live_entity(actor))
                                      ? ai_support::identity(actor)
                                      : ai_support::Identity{};

    next(actor, ctx);

    ai_support::World world{};
    if (!runtime::global_accepting.load(std::memory_order_acquire) || !support_actor_id.ptr ||
        !ai_support::callback(actor, world))
        return;
    // These native order/inventory boundaries can synchronously remove or replace the actor.
    const auto present = [&] {
        return ai_support::resolve(world, support_actor_id) && runtime::actor_ai_allowed(actor);
    };
    if (!present()) return;
    if (config::active().modules.hit_the_dirt && (runtime::read<runtime::Byte>(actor, 0x30) & 4)) {
        hit_the_dirt::on_human_post_quant(world, actor);
        if (!present()) return;
    }
    auto_medic::tick(actor);
    if (!present()) return;
    squad_logistics::tick(actor);
    if (!present()) return;
    recrew_weapons::tick(actor);
    if (!present()) return;
    auto_resupply::tick(actor);
    if (!present()) return;
    scavenge::tick(actor);
    if (!present()) return;
    tactical_objectives::tick(actor, close_grenade::is_busy(world, actor));
    const auto& modules = config::active().modules;
    if (modules.engine_fixes && modules.lull_reload && present()) engine_fixes::on_human_post_quant(world, actor);
    if (present()) movement_recovery::on_actor_post_quant(actor);
}

double vehicle_hull_health(runtime::Address actor) {
    auto_smoke::vehicle::Observation body{};
    auto_smoke::vehicle::usableBody(actor, body);
    return body.hullHealth >= 0 ? body.hullHealth : std::numeric_limits<double>::quiet_NaN();
}

FieldSupportStats field_support_stats() noexcept {
    return {recrew_weapons::stat_recrews.load(std::memory_order_relaxed),
            scavenge::stat_rearm.load(std::memory_order_relaxed),
            scavenge::stat_heavy_pickup.load(std::memory_order_relaxed),
            scavenge::stat_corpse_loot.load(std::memory_order_relaxed)};
}

Stats snapshot_stats() noexcept {
    Stats s{};
    s.crush_escapes = crush_avoidance::stat_crush_escapes;
    s.hazard_priorities = crush_avoidance::stat_hazard_priorities;
    s.smoke_veh = auto_smoke::stat_veh_smoke;
    s.smoke_inf = auto_smoke::stat_inf_smoke;
    s.smoke_med = auto_smoke::stat_med_smoke;
    s.smoke_rel = auto_smoke::stat_inf_released;
    s.med_claim = auto_medic::stat_claims;
    s.med_fetch = auto_medic::stat_fetches;
    s.med_smoke = auto_medic::stat_smokes;
    s.med_rev = auto_medic::stat_revives;
    s.med_backoff = auto_medic::stat_patient_backoffs;
    s.sup_trip = auto_resupply::stat_trips;
    s.sup_refill = auto_resupply::stat_refilled;
    s.tact_ord = tactical_infantry::stat_issued();
    s.overflow_cover = tactical_infantry::stat_overflow_cover.load(std::memory_order_relaxed);
    s.dismount_spread = tactical_infantry::stat_dismount_spread.load(std::memory_order_relaxed);
    s.obj_pursue = tactical_objectives::stat_pursues();
    s.obj_cap = tactical_objectives::stat_caps();
    s.fast_swaps = combat_logistics::stat_fast_swaps;
    s.missile_holds = combat_logistics::stat_missile_holds;
    s.squad_ammo = squad_logistics::stat_ammo;
    s.squad_bandages = squad_logistics::stat_bandages;
    s.scavenge = squad_logistics::stat_scavenge;
    s.heal = engine_fixes::stats.heal;
    s.burn = engine_fixes::stats.burn;
    s.kit = engine_fixes::stats.kit;
    s.window = engine_fixes::stats.window;
    s.stance = engine_fixes::stats.stance;
    s.reload = engine_fixes::stats.reload;
    s.board = engine_fixes::stats.board;
    s.eligible = engine_fixes::stats.eligible;
    s.allegiance_veto = engine_fixes::stats.allegiance_veto;
    s.towed_gun_suppress = engine_fixes::stats.towed_gun_suppress;
    const auto mr = movement_recovery::snapshot_stats();
    s.move_nudge = mr.nudges;
    s.move_resume = mr.resumed;
    s.move_abort = mr.aborted;
    return s;
}

void after_hooks_removed() {
    scavenge::after_hooks_removed();
    recrew_weapons::after_hooks_removed();
    crush_avoidance::after_hooks_removed();
    movement_recovery::after_hooks_removed();
    squad_logistics::after_hooks_removed();
    combat_logistics::after_hooks_removed();
    engine_fixes::after_hooks_removed();
    tactical_objectives::after_hooks_removed();
    tactical_infantry::after_hooks_removed();
    auto_resupply::after_hooks_removed();
    if (g_SmokeOk) auto_medic::after_hooks_removed();
    if (g_SupportOk) ai_support::after_hooks_removed();
    g_SupportOk = g_SmokeOk = g_TacticalOk = false;
}

bool runtime_allowed_flag() noexcept {
    return tactical_infantry::runtime_allowed && engine_fixes::runtime_allowed && combat_logistics::runtime_allowed &&
           squad_logistics::runtime_allowed && movement_recovery::runtime_allowed && crush_avoidance::runtime_allowed &&
           hit_the_dirt::runtime_allowed && recrew_weapons::runtime_allowed && scavenge::runtime_allowed;
}

} // namespace goh::tu_infantry
