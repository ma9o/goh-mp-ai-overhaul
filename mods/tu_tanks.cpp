// Armored vehicle AI translation unit:
//   - Tank Crusher / Pathing (3 hooks)
//   - Tank Auto-Repair & Crew Reboard (5 hooks)
//   - Exit-door selection (1 hook) and threatened transport unload (post-quant)
//   - Stationary hull facing, vehicle crew redistribution and passenger refill (0 extra hooks)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "ai_modules.hpp"
#include "config_and_log.hpp"
#include "tank_auto_repair_module.hpp"
#include "tank_crew_fill_module.hpp"
#include "tank_crusher_module.hpp"
#include "tank_facing_module.hpp"
#include "safe_disembark_module.hpp"
#include "convoy_spacing_module.hpp"

namespace goh::tu_tanks {

namespace {

bool g_CrusherOk = false;
bool g_RepairOk = false;
bool g_CrewFillOk = false;
bool g_FacingOk = false;

} // namespace

bool stage_hooks(runtime::Address base, const char* sha, HookCreateFn create, HookRemoveFn remove) {
    tank_crusher::log_sink = &log::sink_summary;
    g_CrusherOk = tank_crusher::prepare_hooks(base, sha, create, remove);
    if (!g_CrusherOk) {
        log::write(log::Level::error, "ERROR: tank_crusher::prepare_hooks failed");
    } else {
        log::write(log::Level::summary, "Tank Crusher 3 hooks staged cleanly");
    }

    tank_auto_repair::log_sink = &log::sink_summary;
    g_RepairOk = tank_auto_repair::prepare_hooks(base, sha, create, remove);
    if (!g_RepairOk) {
        log::write(log::Level::error, "ERROR: tank_auto_repair::prepare_hooks failed");
    } else {
        log::write(log::Level::summary, "Tank Auto-Repair 5 hooks staged cleanly");
    }

    tank_crew_fill::log_sink = &log::sink_summary;
    g_CrewFillOk = tank_crew_fill::init(base, sha);
    if (!g_CrewFillOk) {
        log::write(log::Level::error, "ERROR: tank_crew_fill::init failed");
    } else {
        log::write(log::Level::summary, "Tank Crew Fill initialized cleanly (0 extra hooks)");
    }

    g_FacingOk = tank_facing::init(base, sha);
    if (!g_FacingOk) log::write(log::Level::error, "Tank facing initialization failed");
    const bool exit_ok = safe_disembark::prepare_hooks(base, sha, create, remove);
    if (!exit_ok) log::write(log::Level::error, "Exit door hook staging failed");
    const bool convoy_ok = convoy_spacing::init(base, sha);
    if (!convoy_ok) log::write(log::Level::error, "Convoy spacing initialization failed");
    return g_CrusherOk && g_RepairOk && g_CrewFillOk && g_FacingOk && exit_ok && convoy_ok;
}

void begin_shutdown() noexcept {
    convoy_spacing::begin_shutdown();
}

bool drain_quiescence() {
    for (int wait = 0; wait < 20 && !convoy_spacing::quiescent(); ++wait) {
        Sleep(10);
    }
    return convoy_spacing::quiescent();
}

void deactivate() noexcept {
    convoy_spacing::deactivate();
    safe_disembark::deactivate();
    tank_facing::deactivate();
    tank_crusher::deactivate();
    tank_crew_fill::deactivate();
    tank_auto_repair::deactivate();
}

std::uint32_t inflight_count() noexcept {
    const int c = tank_crusher::inflight_count();
    const int r = tank_auto_repair::inflight_count();
    const int f = tank_crew_fill::inflight_count();
    return static_cast<std::uint32_t>((c > 0 ? c : 0) + (r > 0 ? r : 0) + (f > 0 ? f : 0)) +
           tank_facing::inflight_count() + safe_disembark::inflight_count() + convoy_spacing::inflight_count();
}

void with_actor_quant(runtime::Address actor, ActorQuantNextFn next, void* ctx) {
    tank_crusher::ActorScope tank_scope(actor);
    ai_support::World world{};
    const auto vehicle_id = runtime::global_accepting.load(std::memory_order_acquire) &&
                                    (runtime::read<runtime::Byte>(actor, 0x30) & 8) &&
                                    ai_support::callback(actor, world) && ai_support::live_entity(actor)
                                ? ai_support::identity(actor)
                                : ai_support::Identity{};
    next(actor, ctx);
    if (runtime::global_accepting.load(std::memory_order_acquire)) {
        if (vehicle_id.ptr && !ai_support::resolve(world, vehicle_id)) return;
        tank_auto_repair::on_actor_post_quant(actor);
        if (vehicle_id.ptr && config::active().modules.transport_unload) {
            if (!ai_support::resolve(world, vehicle_id)) return;
            safe_disembark::on_actor_post_quant(actor);
            // Native SetOrder can synchronously change the scene roster.
            if (!ai_support::resolve(world, vehicle_id)) return;
        }
        tank_crew_fill::on_actor_post_quant(actor);
        tank_facing::on_actor_post_quant(actor);
        if (vehicle_id.ptr && ai_support::resolve(world, vehicle_id)) convoy_spacing::on_actor_post_quant(actor);
    }
}

Stats snapshot_stats() noexcept {
    Stats s{};
    s.tank_ctx = tank_crusher::admitted_contexts.load(std::memory_order_relaxed);
    s.tank_exp = tank_crusher::expanded_contexts.load(std::memory_order_relaxed);
    s.tank_disc = tank_crusher::discounted_obstacles.load(std::memory_order_relaxed);
    s.tank_rev = tank_crusher::biased_estimates.load(std::memory_order_relaxed);
    s.rep_full = tank_auto_repair::stat_dispatch_full.load(std::memory_order_relaxed);
    s.rep_tt = tank_auto_repair::stat_dispatch_turret_track.load(std::memory_order_relaxed);
    s.rep_emit = tank_auto_repair::stat_crew_emitted.load(std::memory_order_relaxed);
    s.rep_assign = tank_auto_repair::stat_excrew_assigned.load(std::memory_order_relaxed);
    s.rep_chain = tank_auto_repair::stat_repair_chained.load(std::memory_order_relaxed);
    s.rep_board = tank_auto_repair::stat_reboard_queued.load(std::memory_order_relaxed);
    s.rep_pick = tank_auto_repair::stat_picker_overrides.load(std::memory_order_relaxed);
    s.crew_total = tank_crew_fill::stat_transfers.load(std::memory_order_relaxed);
    s.crew_breed = tank_crew_fill::stat_transfers_crew_breed.load(std::memory_order_relaxed);
    s.crew_inf = tank_crew_fill::stat_transfers_infantry.load(std::memory_order_relaxed);
    s.crew_internal = tank_crew_fill::stat_transfers_internal.load(std::memory_order_relaxed);
    s.crew_nontank = tank_crew_fill::stat_transfers_nontank.load(std::memory_order_relaxed);
    s.safe_doors = safe_disembark::stat_safe_doors;
    s.transport_unloads = safe_disembark::stat_transport_unloads;
    s.tank_face = tank_facing::stat_rotations.load(std::memory_order_relaxed);
    s.tank_face_retargets = tank_facing::stat_retargets.load(std::memory_order_relaxed);
    s.tank_face_timeouts = tank_facing::stat_timeouts.load(std::memory_order_relaxed);
    const auto cs = convoy_spacing::snapshot_stats();
    s.convoy_yield = cs.yields;
    s.convoy_pass = cs.passes;
    s.convoy_resume = cs.resumed;
    return s;
}

void after_hooks_removed() {
    convoy_spacing::after_hooks_removed();
    safe_disembark::after_hooks_removed();
    if (g_FacingOk) tank_facing::after_hooks_removed();
    if (g_CrewFillOk) tank_crew_fill::after_hooks_removed();
    if (g_RepairOk) tank_auto_repair::after_hooks_removed();
    if (g_CrusherOk) tank_crusher::after_hooks_removed();
    g_CrusherOk = g_RepairOk = g_CrewFillOk = g_FacingOk = false;
}

} // namespace goh::tu_tanks
