// Infantry, support & tactical maneuver translation unit:
//   - Shared AI Support order guards (4 hooks)
//   - Auto-Smoke + Auto-Medic (15 hooks)
//   - Auto-Resupply (0 extra hooks)
//   - Tactical Infantry Movement & Cover (15 hooks)
//   - Tactical Objectives: Pursue & Cap (0 extra hooks)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "ai_modules.hpp"
#include "config_and_log.hpp"
#include "tactical_infantry_module.hpp"
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

    return g_SupportOk && g_SmokeOk && g_TacticalOk;
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
    tactical_objectives::shutdown();
}

bool drain_quiescence() {
    ai_support::accepting.store(false, std::memory_order_release);
    for (int wait = 0; wait < 20 && (!ai_support::quiescent() || !auto_medic::quiescent()); ++wait) {
        Sleep(10);
    }
    return ai_support::quiescent() && auto_medic::quiescent();
}

void deactivate() noexcept {
    tactical_objectives::shutdown();
    ai_support::deactivate();
    auto_smoke::deactivate();
    tactical_infantry::deactivate();
}

std::uint32_t inflight_count() noexcept {
    return ai_support::inflight_count() + auto_smoke::inflight_count() + tactical_infantry::inflight_count();
}

void with_actor_quant(runtime::Address actor, ActorQuantNextFn next, void* ctx) {
    auto_smoke::ActorQuantScope smoke_scope(actor);
    ai_support::ActorQuantScope support_scope(actor);
    const auto support_actor_id = (runtime::global_accepting.load(std::memory_order_acquire) && ai_support::base &&
                                   ai_support::live_entity(actor))
                                      ? ai_support::identity(actor)
                                      : ai_support::Identity{};

    next(actor, ctx);

    if (runtime::global_accepting.load(std::memory_order_acquire) && support_actor_id.ptr && ai_support::host()) {
        ai_support::World support_world{};
        if (auto_smoke::world(ai_support::base, support_world)) {
            if (const auto live_actor = ai_support::resolve(support_world, support_actor_id)) {
                auto_medic::tick(live_actor);
                auto_resupply::tick(live_actor);
                tactical_objectives::tick(live_actor, close_grenade::is_busy(support_world, live_actor));
            }
        }
    }
}

Stats snapshot_stats() noexcept {
    Stats s{};
    s.smoke_veh = auto_smoke::stat_veh_smoke;
    s.smoke_inf = auto_smoke::stat_inf_smoke;
    s.smoke_med = auto_smoke::stat_med_smoke;
    s.smoke_rel = auto_smoke::stat_inf_released;
    s.med_claim = auto_medic::stat_claims;
    s.med_fetch = auto_medic::stat_fetches;
    s.med_smoke = auto_medic::stat_smokes;
    s.med_rev = auto_medic::stat_revives;
    s.sup_trip = auto_resupply::stat_trips;
    s.sup_refill = auto_resupply::stat_refilled;
    s.tact_ord = tactical_infantry::stat_issued();
    s.obj_pursue = tactical_objectives::stat_pursues();
    s.obj_cap = tactical_objectives::stat_caps();
    return s;
}

void after_hooks_removed() {
    tactical_objectives::after_hooks_removed();
    tactical_infantry::after_hooks_removed();
    auto_resupply::after_hooks_removed();
    if (g_SmokeOk) auto_medic::after_hooks_removed();
    if (g_SupportOk) ai_support::after_hooks_removed();
    g_SupportOk = g_SmokeOk = g_TacticalOk = false;
}

bool runtime_allowed_flag() noexcept {
    return tactical_infantry::runtime_allowed;
}

} // namespace goh::tu_infantry
