#pragma once

#include "actor_coordinator.hpp"
#include "auto_smoke_module.hpp"
#include "config_and_log.hpp"
#include "game_runtime.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace goh::grenade_dodge {

using runtime::Address;
using runtime::Byte;
using runtime::HookGate;
using runtime::HookGuard;
using runtime::is_plausible_ptr;
using runtime::native;
using runtime::read;
using runtime::Tick;

inline constexpr const char* executable_sha256 = runtime::kBuild26734796Sha256;
inline constexpr unsigned hook_count = 5; // SimTick, SimDtor, ScannerUpd, AvoidInnerTick, AvoidPlanEscape

// XY coordinates and radius are in metres.
inline bool player_escape_already_safe(float sx, float sy, float dest_x, float dest_y, float gx, float gy, float land_x,
                                       float land_y, float safe_radius) noexcept {
    if (!runtime::finite(runtime::XY{sx, sy}) || !runtime::finite(runtime::XY{dest_x, dest_y}) ||
        !runtime::finite(runtime::XY{gx, gy}) || !runtime::finite(runtime::XY{land_x, land_y}) ||
        !std::isfinite(safe_radius) || safe_radius <= 0)
        return false;
    const double dx = double(dest_x) - sx, dy = double(dest_y) - sy;
    const double length2 = dx * dx + dy * dy;
    if (length2 <= .25 || std::hypot(double(dest_x) - gx, double(dest_y) - gy) < safe_radius ||
        std::hypot(double(dest_x) - land_x, double(dest_y) - land_y) < safe_radius)
        return false;
    const double x = double(sx) - land_x, y = double(sy) - land_y;
    const double t = std::clamp(-(x * dx + y * dy) / length2, 0., 1.);
    return std::hypot(x + t * dx, y + t * dy) >= (std::min)(.92 * std::hypot(x, y), double(safe_radius));
}

namespace detail {

namespace Off {
constexpr Address kActorFlags38 = 0x0038;
constexpr Address kEntityPos = 0x0074;
constexpr Address kEntityFlags1C0 = 0x01C0;
constexpr Address kActorOrderedSub = 0x0AF0;
constexpr Address kActorCover = 0x0DC0;
constexpr Address kActorBrain = 0x0DC8;
constexpr Address kActorSensor = 0x0DD0;
constexpr Address kActorWeaponry = 0x0DD8;
constexpr Address kActorChassis = 0x0DE0;

constexpr Address kBrainOwner = 0x0038;
constexpr Address kBrainCurrentOrder = 0x00F0;
constexpr Address kBrainFlags39C = 0x039C;

constexpr Address kSensorCat1Begin = 0x0280;
constexpr Address kSensorCat1End = 0x0288;

constexpr Address kSubjectFlags10 = 0x0010;
constexpr Address kSubjectFlags14 = 0x0014;
constexpr Address kSubjectPos = 0x0028;
constexpr Address kSubjectObsTime = 0x0064;
constexpr Address kSubjectSuppress = 0x006C;

constexpr Address kScannerBrain = 0x0008;
constexpr Address kScannerActive = 0x0010;
constexpr Address kScannerDangerZone = 0x0018;
constexpr Address kScannerEntity = 0x0028;

constexpr Address kOrderOwner = 0x0008;
constexpr Address kAvoidEntity = 0x0038;
constexpr Address kAvoidRadius = 0x0048;
constexpr Address kAvoidState = 0x004C;

constexpr Address kSimBody = 0x0008;
constexpr Address kSimEntity = 0x0308;

constexpr Address kFootSetModeVOff = 45 * 8;
constexpr Address kFootSetTypeVOff = 46 * 8;
} // namespace Off

using FnArmedGrenade = Byte(__fastcall*)(Address entity);
using FnTerrainHeight = float(__fastcall*)(Address terrain, const float* xy, int level);
using FnTerrainEnv = std::uint16_t*(__fastcall*)(std::uint16_t* out, const float* xy);
using FnMoveTaskPoint = Address(__fastcall*)(const float* xy, std::uint32_t flags);
using FnOrderAttachChild = void(__fastcall*)(Address parent, Address child);
using FnChassisSetInt = void(__fastcall*)(Address chassis, std::int32_t val, Byte force);

using FnSimTick = void(__fastcall*)(Address sim);
using FnSimDtor = void*(__fastcall*)(Address sim, std::uint32_t flags);
using FnScannerUpd = void(__fastcall*)(Address scanner);
using FnAvoidInnerTick = std::uint64_t(__fastcall*)(Address order);
using FnAvoidPlanEscape = Byte(__fastcall*)(Address order);

struct TrackedGrenade {
    Address sim{};
    Address entity{};
    Tick last_tick{};
    float x{}, y{}, z{};
    float vx{}, vy{}, vz{};
    float land_x{}, land_y{};
    float flight_time{};
    bool logged{};
};

struct OrderRetry {
    Tick last_tick{};
    int attempt{};
};

struct RuntimeState {
    std::mutex mutex;
    Address scene{};
    Tick last_tick{};
    std::vector<TrackedGrenade> grenades;
    std::unordered_map<Address, OrderRetry> retries;
};

inline RuntimeState& state() noexcept {
    static RuntimeState s{};
    return s;
}

inline HookGate gate{};
inline Address base{};
inline bool staged{};
inline bool rollback_pending{};

inline FnSimTick orig_sim_tick{};
inline FnSimDtor orig_sim_dtor{};
inline FnScannerUpd orig_scanner_upd{};
inline FnAvoidInnerTick orig_avoid_inner_tick{};
inline FnAvoidPlanEscape orig_avoid_plan_escape{};

inline std::atomic<std::uint32_t> stat_dodges{0};
inline std::atomic<std::uint32_t> stat_wakeups{0};

inline std::atomic<std::uint32_t> stat_player_escapes_preserved{0};

inline float spatial_scale() noexcept {
    const float L = read<float>(base + runtime::table().glob.spatial_scale_l);
    return (std::isfinite(L) && L > 1.0f && L < 200.0f) ? L : 20.0f;
}

inline float gravity() noexcept {
    const float g = read<float>(base + runtime::table().glob.gravity);
    return (std::isfinite(g) && g > 1e-7f && g < 1.0f) ? g : 0.0001962f;
}

inline float sample_terrain_height(float x, float y, float fallback_z) noexcept {
    const Address scene = read<Address>(base + runtime::table().glob.scene_ptr);
    if (!is_plausible_ptr(scene)) return fallback_z;
    const Address terrain = read<Address>(scene, 0x18);
    if (!is_plausible_ptr(terrain)) return fallback_z;
    const float xy[2] = {x, y};
    const float h = native<FnTerrainHeight>(base, runtime::table().fn.terrain_height)(terrain, xy, 0);
    return std::isfinite(h) ? h : fallback_z;
}

inline bool is_passable_dry_terrain(float x, float y, bool is_human = true) noexcept {
    if (!std::isfinite(x) || !std::isfinite(y)) return false;
    if (std::fabs(x) < 1.0f && std::fabs(y) < 1.0f) return false;
    const float xy[2] = {x, y};
    std::uint16_t env = 0;
    native<FnTerrainEnv>(base, runtime::table().fn.terrain_env)(&env, xy);
    const Byte cls = static_cast<Byte>(env & 0xFFu);
    if (cls == 2 || cls == 3 || cls == 4 || cls == 5) return false;
    if (is_human && cls == 8) return false;
    return true;
}

inline Address active_child(Address order, Address table) noexcept {
    if (table == base + runtime::table().vt.move_task_order || table == base + runtime::table().vt.move_to_cover)
        return read<Address>(order, 0x30);
    return auto_smoke::orders::active_child(base, order);
}

inline bool active_order_type(Address actor, Address brain, Address type) noexcept {
    auto order = read<Address>(brain, Off::kBrainCurrentOrder);
    for (unsigned depth = 0; is_plausible_ptr(order) && depth < 16; ++depth) {
        const auto vt = read<Address>(order);
        if (!is_plausible_ptr(vt) || read<Address>(order, 8) != actor + Off::kActorOrderedSub) return false;
        if (vt == base + type) return true;
        order = active_child(order, vt);
    }
    return false;
}

inline bool is_eligible_human(Address actor, Address& out_brain, Address& out_chassis) noexcept {
    out_brain = 0;
    out_chassis = 0;
    if (!is_plausible_ptr(actor)) return false;
    if ((read<std::uint32_t>(actor, Off::kActorFlags38) & 0x220u) != 0) return false;
    if ((read<std::uint32_t>(actor, Off::kEntityFlags1C0) & (1u << 28)) != 0) return false;

    const Address brain = read<Address>(actor, Off::kActorBrain);
    if (!is_plausible_ptr(brain)) return false;
    if (read<Address>(brain) != base + runtime::table().vt.human_brain) return false;
    if ((read<std::uint32_t>(brain, Off::kBrainFlags39C) & 0x3Fu) != 0) return false;

    const Address chassis = read<Address>(actor, Off::kActorChassis);
    if (!is_plausible_ptr(chassis)) return false;
    if (read<std::uint32_t>(chassis, 0x18) != 0) return false;

    const Address weaponry = read<Address>(actor, Off::kActorWeaponry);
    if (is_plausible_ptr(weaponry) && (read<std::uint32_t>(weaponry, 0x70) & 2u) != 0) return false;

    if (read<Address>(brain, Off::kBrainOwner) != actor) return false;
    out_brain = brain;
    out_chassis = chassis;
    return true;
}

// Stop at AvoidGrenade: its own movement child is never a player click.
inline bool player_destination(Address brain, runtime::XY& goal) noexcept {
    const auto flags = read<std::uint32_t>(brain, Off::kBrainFlags39C);
    if (!(flags & (1u << 28)) || (read<std::uint32_t>(brain, 0x398) & 3) != 1) return false;
    const auto owner = read<Address>(brain, Off::kBrainOwner) + Off::kActorOrderedSub;
    auto order = read<Address>(brain, Off::kBrainCurrentOrder);
    for (unsigned depth = 0; order && depth < 16; ++depth) {
        if (!is_plausible_ptr(order) || read<Address>(order, 8) != owner) return false;
        const auto vt = read<Address>(order);
        if (!is_plausible_ptr(vt) || vt == base + runtime::table().vt.attack_order) return false;
        if (vt == base + runtime::table().vt.avoid_grenade) break;
        if (vt == base + runtime::table().vt.move_order) {
            const auto path = read<runtime::Span>(order, 0x40);
            if (is_plausible_ptr(path.first) && is_plausible_ptr(path.end) && path.valid(12, 4096) &&
                path.end > path.first) {
                goal = read<runtime::XY>(path.end - 12);
                if (runtime::finite(goal)) return true;
            }
        } else if (vt == base + runtime::table().vt.move_task_order) {
            const auto task = read<Address>(order, 0x40);
            if (is_plausible_ptr(task)) {
                goal = read<runtime::XY>(task, 0x10);
                if (runtime::finite(goal) && (goal.x || goal.y)) return true;
            }
        }
        order = active_child(order, vt);
    }
    if (!(flags & (1u << 6))) return false;
    goal = read<runtime::XY>(brain, 0x130);
    return runtime::finite(goal);
}

inline bool safe_goal(runtime::XY goal, float sx, float sy, float scale, float radius_m,
                      const TrackedGrenade& g) noexcept {
    return player_escape_already_safe(sx / scale, sy / scale, goal.x / scale, goal.y / scale, g.x / scale, g.y / scale,
                                      g.land_x / scale, g.land_y / scale, radius_m + 2.f);
}

inline void predict_landing(float x0, float y0, float z0, float vx, float vy, float vz, float& out_x, float& out_y,
                            float& out_t) noexcept {
    const float G = gravity();
    const float ground0 = sample_terrain_height(x0, y0, z0);
    const float dz = z0 - ground0;
    float t = 0.0f;
    if (dz > 0.5f || vz > 0.001f) {
        const float disc = vz * vz + 2.0f * G * (dz > 0.0f ? dz : 0.0f);
        if (disc > 0.0f) t = (vz + std::sqrt(disc)) / G;
    }
    if (t < 0.0f) t = 0.0f;
    if (t > 2500.0f) t = 2500.0f;

    float px = x0 + vx * t;
    float py = y0 + vy * t;
    if (t > 10.0f) {
        const float ground1 = sample_terrain_height(px, py, ground0);
        const float dz1 = z0 - ground1;
        const float disc1 = vz * vz + 2.0f * G * (dz1 > 0.0f ? dz1 : 0.0f);
        if (disc1 > 0.0f) {
            const float t1 = (vz + std::sqrt(disc1)) / G;
            if (t1 >= 0.0f && t1 <= 2500.0f) {
                t = t1;
                px = x0 + vx * t;
                py = y0 + vy * t;
            }
        }
    }

    const float speed_xy = std::sqrt(vx * vx + vy * vy);
    if (t < 150.0f && speed_xy > 0.01f) {
        px += vx * 250.0f;
        py += vy * 250.0f;
    }

    out_x = std::isfinite(px) ? px : x0;
    out_y = std::isfinite(py) ? py : y0;
    out_t = t;
}

inline void refresh_scene_locked(RuntimeState& st, Tick now) {
    const auto scene = read<Address>(base, runtime::table().glob.scene_ptr);
    if (scene != st.scene || now < st.last_tick) {
        st.grenades.clear();
        st.retries.clear();
    }
    st.scene = scene;
    st.last_tick = now;
}

inline void prune_stale_locked(std::vector<TrackedGrenade>& list, Tick now) {
    std::size_t dst = 0;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const Tick age = now - list[i].last_tick;
        if (age <= 300 && is_plausible_ptr(list[i].entity)) {
            if (dst != i) list[dst] = list[i];
            ++dst;
        }
    }
    list.resize(dst);
}

inline const TrackedGrenade* find_tracked_locked(const std::vector<TrackedGrenade>& list, Address entity,
                                                 Tick now) noexcept {
    for (const auto& g : list) {
        if (g.entity == entity && (now - g.last_tick) <= 300) return &g;
    }
    return nullptr;
}

inline bool grenade_position(Address entity, Tick now, TrackedGrenade& result) {
    auto& st = state();
    std::lock_guard<std::mutex> lock(st.mutex);
    refresh_scene_locked(st, now);
    if (const auto* tracked = find_tracked_locked(st.grenades, entity, now)) {
        result = *tracked;
        return true;
    }
    result.x = result.land_x = read<float>(entity, Off::kEntityPos);
    result.y = result.land_y = read<float>(entity, Off::kEntityPos + 4);
    return false;
}

inline float danger_radius_m(Address object, Address offset) {
    const auto radius = read<float>(object, offset);
    return std::isfinite(radius) && radius >= 1.f && radius <= 20.f ? radius : 4.f;
}

inline bool is_threatened_by(const TrackedGrenade& g, float sx, float sy, float danger_radius, float max_detect_dist,
                             float& out_urgency_dist_sq) noexcept {
    const float dcx = sx - g.x;
    const float dcy = sy - g.y;
    const float cur_dist_sq = dcx * dcx + dcy * dcy;
    if (cur_dist_sq > max_detect_dist * max_detect_dist) return false;

    const float dlx = sx - g.land_x;
    const float dly = sy - g.land_y;
    const float land_dist_sq = dlx * dlx + dly * dly;
    const float rad_sq = danger_radius * danger_radius;

    if (land_dist_sq < rad_sq) {
        out_urgency_dist_sq = land_dist_sq;
        return true;
    }

    const float speed_xy = std::sqrt(g.vx * g.vx + g.vy * g.vy);
    const bool airborne = (g.flight_time > 120.0f);
    const float dot_toward = g.vx * dcx + g.vy * dcy;

    if (airborne) return false;
    if (speed_xy > 0.035f && dot_toward <= 0.0f) return false;

    if (cur_dist_sq < rad_sq) {
        out_urgency_dist_sq = cur_dist_sq;
        return true;
    }
    return false;
}

inline bool compute_away_from_enemies(Address actor, float sx, float sy, Tick now, float& out_ex,
                                      float& out_ey) noexcept {
    out_ex = 0.0f;
    out_ey = 0.0f;
    const Address sensor = read<Address>(actor, Off::kActorSensor);
    if (!is_plausible_ptr(sensor)) return false;

    const Address beg = read<Address>(sensor, Off::kSensorCat1Begin);
    const Address end = read<Address>(sensor, Off::kSensorCat1End);
    if (!is_plausible_ptr(beg) || !is_plausible_ptr(end) || end < beg) return false;
    const std::size_t count = (end - beg) / sizeof(Address);
    if (count == 0 || count > 128) return false;

    float sum_x = 0.0f;
    float sum_y = 0.0f;
    int used = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const Address subj = read<Address>(beg, i * sizeof(Address));
        if (!is_plausible_ptr(subj)) continue;
        if (read<Byte>(subj, Off::kSubjectSuppress) != 0) continue;

        const std::uint32_t f10 = read<std::uint32_t>(subj, Off::kSubjectFlags10);
        const std::uint32_t f14 = read<std::uint32_t>(subj, Off::kSubjectFlags14);
        if ((f14 & (1u << 1)) == 0) continue;
        if ((f14 & (1u << 4)) != 0) continue;
        if ((f10 & (1u << 28)) == 0) continue;

        const Tick obs_time = read<Tick>(subj, Off::kSubjectObsTime);
        if ((now - obs_time) > 20000) continue;

        const float ex = read<float>(subj, Off::kSubjectPos);
        const float ey = read<float>(subj, Off::kSubjectPos + 4);
        if (!std::isfinite(ex) || !std::isfinite(ey)) continue;

        const float dx = sx - ex;
        const float dy = sy - ey;
        const float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < 1.0f) continue;

        float w = 1.0f / (dist + 40.0f);
        if ((f10 & (1u << 29)) != 0) w *= 2.0f;
        sum_x += (dx / dist) * w;
        sum_y += (dy / dist) * w;
        ++used;
    }

    const float len = std::sqrt(sum_x * sum_x + sum_y * sum_y);
    if (used == 0 || len < 1e-5f) return false;
    out_ex = sum_x / len;
    out_ey = sum_y / len;
    return true;
}

inline int get_and_increment_retry(Address order, Tick now) {
    auto& st = state();
    std::lock_guard<std::mutex> lock(st.mutex);
    refresh_scene_locked(st, now);
    if (st.retries.size() > 512) {
        for (auto it = st.retries.begin(); it != st.retries.end();) {
            if ((now - it->second.last_tick) >= 10000)
                it = st.retries.erase(it);
            else
                ++it;
        }
    }
    auto it = st.retries.find(order);
    if (it != st.retries.end() && (now - it->second.last_tick) < 10000) {
        it->second.last_tick = now;
        return it->second.attempt++;
    }
    st.retries[order] = {now, 1};
    return 0;
}

inline bool is_actor_avoiding_live(Address actor) noexcept {
    if (!base || !is_plausible_ptr(actor)) return false;
    const Address brain = read<Address>(actor, Off::kActorBrain);
    if (!is_plausible_ptr(brain)) return false;
    return active_order_type(actor, brain, runtime::table().vt.avoid_grenade);
}

inline Byte __fastcall hook_avoid_plan_escape(Address order);

inline void __fastcall hook_sim_tick(Address sim) {
    HookGuard guard(gate);
    orig_sim_tick(sim);
    if (!gate.active() || !config::active().modules.grenade_dodge || !runtime::is_host_main_thread(base) ||
        !is_plausible_ptr(sim))
        return;

    const Address entity = read<Address>(sim, Off::kSimEntity);
    if (!is_plausible_ptr(entity)) return;
    if ((read<std::uint32_t>(entity, Off::kEntityFlags1C0) & (1u << 28)) != 0) return;
    if (native<FnArmedGrenade>(base, runtime::table().fn.armed_grenade_query)(entity) == 0) return;

    const Address body = sim + Off::kSimBody;
    const float x = read<float>(body, 0x28);
    const float y = read<float>(body, 0x2C);
    const float z = read<float>(body, 0x30);
    const float vx = read<float>(body, 0x48);
    const float vy = read<float>(body, 0x4C);
    const float vz = read<float>(body, 0x50);
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(vx) || !std::isfinite(vy) ||
        !std::isfinite(vz))
        return;

    const Tick now = runtime::read_clock(base);
    float land_x = x;
    float land_y = y;
    float flight_t = 0.0f;
    predict_landing(x, y, z, vx, vy, vz, land_x, land_y, flight_t);

    bool should_log = false;
    {
        auto& st = state();
        std::lock_guard<std::mutex> lock(st.mutex);
        refresh_scene_locked(st, now);
        prune_stale_locked(st.grenades, now);

        TrackedGrenade* slot = nullptr;
        for (auto& g : st.grenades) {
            if (g.sim == sim || g.entity == entity) {
                slot = &g;
                break;
            }
        }
        if (!slot) {
            if (st.grenades.size() < 1024) {
                st.grenades.emplace_back();
                slot = &st.grenades.back();
            } else {
                slot = &st.grenades[0];
                *slot = {};
            }
        }

        slot->sim = sim;
        slot->entity = entity;
        slot->last_tick = now;
        slot->x = x;
        slot->y = y;
        slot->z = z;
        slot->vx = vx;
        slot->vy = vy;
        slot->vz = vz;
        slot->land_x = land_x;
        slot->land_y = land_y;
        slot->flight_time = flight_t;
        if (!slot->logged) {
            slot->logged = true;
            should_log = true;
        }
    }

    if (should_log) {
        const float speed = std::sqrt(vx * vx + vy * vy + vz * vz);
        log::write(log::Level::verbose,
                   "FLYING GRENADE detected: ent=0x%llx pos=(%.1f,%.1f,%.1f) vel=%.4f predLand=(%.1f,%.1f) t=%.0f",
                   static_cast<unsigned long long>(entity), x, y, z, speed, land_x, land_y, flight_t);
    }
}

inline void* __fastcall hook_sim_dtor(Address sim, std::uint32_t flags) {
    HookGuard guard(gate);
    if (gate.active()) {
        auto& st = state();
        std::lock_guard<std::mutex> lock(st.mutex);
        for (std::size_t i = 0; i < st.grenades.size(); ++i) {
            if (st.grenades[i].sim == sim) {
                st.grenades[i] = st.grenades.back();
                st.grenades.pop_back();
                break;
            }
        }
    }
    return orig_sim_dtor(sim, flags);
}

inline void __fastcall hook_scanner_upd(Address scanner) {
    HookGuard guard(gate);
    orig_scanner_upd(scanner);
    if (!gate.active() || !config::active().modules.grenade_dodge || !runtime::is_host_main_thread(base) ||
        !is_plausible_ptr(scanner))
        return;

    const Address brain = read<Address>(scanner, Off::kScannerBrain);
    if (!is_plausible_ptr(brain)) return;
    const Address actor = read<Address>(brain, Off::kBrainOwner);

    Address chk_brain = 0;
    Address chk_chassis = 0;
    if (!is_eligible_human(actor, chk_brain, chk_chassis) || chk_brain != brain) return;

    const Tick now = runtime::read_clock(base);
    const float sx = read<float>(actor, Off::kEntityPos);
    const float sy = read<float>(actor, Off::kEntityPos + 4);
    const float L = spatial_scale();

    const float cfg_danger = danger_radius_m(scanner, Off::kScannerDangerZone);
    const float danger_rad = (cfg_danger + 1.0f) * L;
    const float detect_rad = config::active().tuning.grenade_detect_radius_m * L;

    runtime::XY goal{};
    const bool user_move = !is_actor_avoiding_live(actor) && player_destination(brain, goal);
    const Address stock = read<Byte>(scanner, Off::kScannerActive) ? read<Address>(scanner, Off::kScannerEntity) : 0;
    bool stock_safe = false, preserved = false;
    Address best_ent = 0;
    float best_urg_sq = 1e30f;
    {
        auto& st = state();
        std::lock_guard<std::mutex> lock(st.mutex);
        refresh_scene_locked(st, now);
        if (st.grenades.empty()) return;
        prune_stale_locked(st.grenades, now);
        for (const auto& g : st.grenades) {
            float urg_sq = 0.0f;
            const bool threatened = is_threatened_by(g, sx, sy, danger_rad, detect_rad, urg_sq);
            if (user_move && (threatened || g.entity == stock) && safe_goal(goal, sx, sy, L, cfg_danger, g)) {
                stock_safe |= g.entity == stock;
                preserved = true;
                continue;
            }
            if (threatened && urg_sq < best_urg_sq) {
                best_urg_sq = urg_sq;
                best_ent = g.entity;
            }
        }
    }

    if (best_ent != 0 && native<FnArmedGrenade>(base, runtime::table().fn.armed_grenade_query)(best_ent) != 0) {
        runtime::write<Byte>(scanner, Off::kScannerActive, 1);
        runtime::write<Address>(scanner, Off::kScannerEntity, best_ent);
    } else if (preserved && (!stock || stock_safe)) {
        if (stock_safe) runtime::write<Byte>(scanner, Off::kScannerActive, 0);
        ++stat_player_escapes_preserved;
    }
}

inline std::uint64_t __fastcall hook_avoid_inner_tick(Address order) {
    HookGuard guard(gate);
    if (gate.active() && config::active().modules.grenade_dodge && runtime::is_host_main_thread(base) &&
        is_plausible_ptr(order)) {
        const std::int32_t st_val = read<std::int32_t>(order, Off::kAvoidState);
        const Address g_ent = read<Address>(order, Off::kAvoidEntity);
        const Address owner_sub = read<Address>(order, Off::kOrderOwner);

        if (st_val != 2 && is_plausible_ptr(g_ent) && is_plausible_ptr(owner_sub) &&
            owner_sub > Off::kActorOrderedSub) {
            const Address actor = owner_sub - Off::kActorOrderedSub;
            Address brain = 0;
            Address chassis = 0;
            if (is_eligible_human(actor, brain, chassis) &&
                native<FnArmedGrenade>(base, runtime::table().fn.armed_grenade_query)(g_ent) != 0) {
                const Tick now = runtime::read_clock(base);
                TrackedGrenade tg_copy{};
                if (!grenade_position(g_ent, now, tg_copy)) return orig_avoid_inner_tick(order);
                const float sx = read<float>(actor, Off::kEntityPos);
                const float sy = read<float>(actor, Off::kEntityPos + 4);
                const float L = spatial_scale();
                const float ord_rad = danger_radius_m(order, Off::kAvoidRadius);
                runtime::XY goal{};
                if (player_destination(brain, goal) && safe_goal(goal, sx, sy, L, ord_rad, tg_copy)) return 1;
                const float danger_rad = (ord_rad + 1.0f) * L;
                float urg_sq = 0.0f;
                const bool in_danger = is_threatened_by(tg_copy, sx, sy, danger_rad, 25.0f * L, urg_sq);

                if (in_danger) {
                    if (st_val == 1) runtime::write<std::int32_t>(order, Off::kAvoidState, 0);
                    const Address cover = read<Address>(actor, Off::kActorCover);
                    if (is_plausible_ptr(cover) && read<std::int32_t>(cover, 0x24) != 0) {
                        runtime::write<std::int32_t>(order, Off::kAvoidState, 0);
                        const Byte ok = hook_avoid_plan_escape(order);
                        return ok ? 0ULL : 2ULL;
                    }
                }
            }
        }
    }
    return orig_avoid_inner_tick(order);
}

inline Byte __fastcall hook_avoid_plan_escape(Address order) {
    HookGuard guard(gate);
    if (!gate.active() || !config::active().modules.grenade_dodge || !runtime::is_host_main_thread(base) ||
        !is_plausible_ptr(order)) {
        return orig_avoid_plan_escape(order);
    }

    const Address owner_sub = read<Address>(order, Off::kOrderOwner);
    const Address g_ent = read<Address>(order, Off::kAvoidEntity);
    if (!is_plausible_ptr(owner_sub) || owner_sub <= Off::kActorOrderedSub || !is_plausible_ptr(g_ent)) {
        return orig_avoid_plan_escape(order);
    }

    const Address actor = owner_sub - Off::kActorOrderedSub;
    Address brain = 0;
    Address chassis = 0;
    if (!is_eligible_human(actor, brain, chassis)) {
        return orig_avoid_plan_escape(order);
    }

    const Tick now = runtime::read_clock(base);

    const float sx = read<float>(actor, Off::kEntityPos);
    const float sy = read<float>(actor, Off::kEntityPos + 4);
    const float L = spatial_scale();

    const float ord_rad = danger_radius_m(order, Off::kAvoidRadius);
    TrackedGrenade tg_copy{};
    const bool has_tg = grenade_position(g_ent, now, tg_copy);
    const float hx = tg_copy.land_x, hy = tg_copy.land_y;
    const float g_cur_x = tg_copy.x, g_cur_y = tg_copy.y;
    const float gvx = tg_copy.vx, gvy = tg_copy.vy;
    runtime::XY goal{};
    if (has_tg && player_destination(brain, goal) && safe_goal(goal, sx, sy, L, ord_rad, tg_copy)) return 0;
    const int attempt = get_and_increment_retry(order, now);
    if (attempt >= 5) return orig_avoid_plan_escape(order);
    const float safe_rad = (ord_rad + 2.5f) * L;
    const float qx = sx - hx;
    const float qy = sy - hy;
    const float dist_land = std::sqrt(qx * qx + qy * qy);
    const float dcx = sx - g_cur_x;
    const float dcy = sy - g_cur_y;
    const float dist_cur = std::sqrt(dcx * dcx + dcy * dcy);

    const bool flying_away = has_tg && (tg_copy.flight_time > 120.0f || ((std::sqrt(gvx * gvx + gvy * gvy) > 0.035f) &&
                                                                         (gvx * dcx + gvy * dcy <= 0.0f)));
    if (dist_land >= safe_rad && (flying_away || dist_cur >= (ord_rad + 1.5f) * L)) {
        runtime::write<std::int32_t>(order, Off::kAvoidState, 1);
        return 1;
    }

    float gx = 0.0f;
    float gy = 0.0f;
    if (dist_land > 1.0f) {
        gx = qx / dist_land;
        gy = qy / dist_land;
    }

    float ex = 0.0f;
    float ey = 0.0f;
    bool has_enemy_dir = compute_away_from_enemies(actor, sx, sy, now, ex, ey);
    if (!has_enemy_dir) {
        const float vxy = std::sqrt(gvx * gvx + gvy * gvy);
        if (vxy > 0.005f) {
            ex = gvx / vxy;
            ey = gvy / vxy;
            has_enemy_dir = true;
        }
    }

    float ux = 0.0f;
    float uy = 0.0f;
    if (dist_land > 1.0f && has_enemy_dir) {
        const float bx = 1.35f * gx + 0.85f * ex;
        const float by = 1.35f * gy + 0.85f * ey;
        const float blen = std::sqrt(bx * bx + by * by);
        if (blen > 0.15f && (bx * gx + by * gy) > 0.0f) {
            ux = bx / blen;
            uy = by / blen;
        } else {
            ux = -gy;
            uy = gx;
            if (ux * ex + uy * ey < 0.0f) {
                ux = -ux;
                uy = -uy;
            }
        }
    } else if (dist_land > 1.0f) {
        ux = gx;
        uy = gy;
    } else if (has_enemy_dir) {
        ux = ex;
        uy = ey;
    } else {
        ux = 1.0f;
        uy = 0.0f;
    }

    static constexpr float kAnglesRad[7] = {0.0f, 0.55f, -0.55f, 1.15f, -1.15f, 1.75f, -1.75f};
    float target_xy[2] = {0.0f, 0.0f};
    bool found_point = false;

    for (int k = 0; k < 7; ++k) {
        const int a_idx = (attempt + k) % 7;
        const float ca = std::cos(kAnglesRad[a_idx]);
        const float sa = std::sin(kAnglesRad[a_idx]);
        const float dx = ux * ca - uy * sa;
        const float dy = ux * sa + uy * ca;

        const float dot_qu = qx * dx + qy * dy;
        const float disc = dot_qu * dot_qu + safe_rad * safe_rad - (qx * qx + qy * qy);
        float s = (disc > 0.0f) ? (-dot_qu + std::sqrt(disc)) : safe_rad;
        if (s < 2.5f * L) s = 2.5f * L;
        s += 1.5f * L;

        const float cand_x = sx + dx * s;
        const float cand_y = sy + dy * s;
        if (is_passable_dry_terrain(cand_x, cand_y, true)) {
            target_xy[0] = cand_x;
            target_xy[1] = cand_y;
            found_point = true;
            break;
        }
    }

    if (!found_point) return orig_avoid_plan_escape(order);

    const Address chassis_vt = read<Address>(chassis);
    if (!is_plausible_ptr(chassis_vt)) return orig_avoid_plan_escape(order);

    const auto fn_set_type = read<FnChassisSetInt>(chassis_vt, Off::kFootSetTypeVOff);
    const auto fn_set_mode = read<FnChassisSetInt>(chassis_vt, Off::kFootSetModeVOff);
    fn_set_type(chassis, 0, 0);
    fn_set_mode(chassis, 2, 0);

    const Address move_child = native<FnMoveTaskPoint>(base, runtime::table().fn.move_task_point)(target_xy, 0);
    if (!is_plausible_ptr(move_child)) return orig_avoid_plan_escape(order);
    native<FnOrderAttachChild>(base, runtime::table().fn.order_attach_child)(order, move_child);

    // Notify unified coordinator at highest priority (100), preempting lower-priority jobs.
    coord::instance().claim(actor, read<std::uint32_t>(actor, 0xC0), coord::Module::grenade_dodge,
                            coord::Priority::grenade_dodge, now, 4000);

    const std::uint32_t count = stat_dodges.fetch_add(1, std::memory_order_relaxed) + 1;
    log::write(log::Level::verbose,
               "SPRINT ESCAPE #%u: actor=0x%llx from=(%.1f,%.1f) -> to=(%.1f,%.1f) haz=(%.1f,%.1f) enemyDir=%d att=%d",
               count, static_cast<unsigned long long>(actor), sx, sy, target_xy[0], target_xy[1], hx, hy,
               has_enemy_dir ? 1 : 0, attempt);
    return 1;
}

} // namespace detail

inline void on_actor_pre_quant(Address actor) {
    using namespace detail;
    if (!gate.active() || !config::active().modules.grenade_dodge || !runtime::is_host_main_thread(base)) return;

    Address brain = 0;
    Address chassis = 0;
    if (!is_eligible_human(actor, brain, chassis)) return;
    if (is_actor_avoiding_live(actor)) return;

    const Tick now = runtime::read_clock(base);
    const float sx = read<float>(actor, Off::kEntityPos);
    const float sy = read<float>(actor, Off::kEntityPos + 4);
    const float L = spatial_scale();
    const float danger_rad = config::active().tuning.grenade_danger_radius_m * L;
    const float detect_rad = config::active().tuning.grenade_detect_radius_m * L;

    runtime::XY goal{};
    const bool user_move = player_destination(brain, goal);
    bool wake = false;
    {
        auto& st = state();
        std::lock_guard<std::mutex> lock(st.mutex);
        refresh_scene_locked(st, now);
        if (st.grenades.empty()) return;
        prune_stale_locked(st.grenades, now);
        for (const auto& g : st.grenades) {
            float urg_sq = 0.0f;
            if (is_threatened_by(g, sx, sy, danger_rad, detect_rad, urg_sq)) {
                if (user_move && safe_goal(goal, sx, sy, L, config::active().tuning.grenade_danger_radius_m, g))
                    continue;
                wake = true;
                break;
            }
        }
    }

    if (wake) {
        auto* p_flags = reinterpret_cast<std::uint32_t*>(brain + Off::kBrainFlags39C);
        if (((*p_flags >> 25) & 1u) == 0) {
            *p_flags |= (1u << 25);
            stat_wakeups.fetch_add(1, std::memory_order_relaxed);
            coord::instance().claim(actor, read<std::uint32_t>(actor, 0xC0), coord::Module::grenade_dodge,
                                    coord::Priority::grenade_dodge, now, 3000);
        }
    }
}

template <class Create, class Remove>
inline bool prepare_hooks(Address image, const char* digest, Create create, Remove remove) {
    using namespace detail;
    if (!image || !digest || std::strcmp(digest, executable_sha256) != 0 || staged || rollback_pending) return false;

    const auto& h = runtime::table().hook;
    std::array<void*, hook_count> created{};
    unsigned count = 0;
    auto stage = [&](Address rva, void* detour, auto& orig) -> bool {
        void* target = reinterpret_cast<void*>(image + rva);
        void* trampoline = nullptr;
        if (create(target, detour, &trampoline) != 0) return false;
        orig = reinterpret_cast<std::decay_t<decltype(orig)>>(trampoline);
        created[count++] = target;
        return true;
    };

    const bool ok = stage(h.grenade_sim_tick, reinterpret_cast<void*>(hook_sim_tick), orig_sim_tick) &&
                    stage(h.grenade_sim_dtor, reinterpret_cast<void*>(hook_sim_dtor), orig_sim_dtor) &&
                    stage(h.grenade_scanner_upd, reinterpret_cast<void*>(hook_scanner_upd), orig_scanner_upd) &&
                    stage(h.avoid_inner_tick, reinterpret_cast<void*>(hook_avoid_inner_tick), orig_avoid_inner_tick) &&
                    stage(h.avoid_plan_escape, reinterpret_cast<void*>(hook_avoid_plan_escape), orig_avoid_plan_escape);

    if (!ok) {
        bool clean = true;
        while (count)
            clean &= (remove(created[--count]) == 0);
        if (clean) {
            orig_sim_tick = nullptr;
            orig_sim_dtor = nullptr;
            orig_scanner_upd = nullptr;
            orig_avoid_inner_tick = nullptr;
            orig_avoid_plan_escape = nullptr;
        } else {
            rollback_pending = true;
        }
        return false;
    }

    base = image;
    staged = true;
    stat_dodges.store(0, std::memory_order_relaxed);
    stat_wakeups.store(0, std::memory_order_relaxed);
    coord::instance().register_provider(coord::Module::grenade_dodge, coord::Priority::grenade_dodge,
                                        &is_actor_avoiding_live);
    gate.activate();
    return true;
}

inline void deactivate() noexcept {
    detail::gate.deactivate();
}

inline std::uint32_t inflight_count() noexcept {
    return detail::gate.count();
}

inline std::uint32_t total_dodges() noexcept {
    return detail::stat_dodges.load(std::memory_order_relaxed);
}

inline std::uint32_t total_wakeups() noexcept {
    return detail::stat_wakeups.load(std::memory_order_relaxed);
}

inline std::uint32_t total_player_escapes_preserved() noexcept {
    return detail::stat_player_escapes_preserved.load(std::memory_order_relaxed);
}

inline void after_hooks_removed() {
    using namespace detail;
    gate.deactivate();
    {
        auto& st = state();
        std::lock_guard<std::mutex> lock(st.mutex);
        st.scene = st.last_tick = 0;
        std::vector<TrackedGrenade>{}.swap(st.grenades);
        std::unordered_map<Address, OrderRetry>{}.swap(st.retries);
    }
    orig_sim_tick = nullptr;
    orig_sim_dtor = nullptr;
    orig_scanner_upd = nullptr;
    orig_avoid_inner_tick = nullptr;
    orig_avoid_plan_escape = nullptr;
    base = 0;
    staged = false;
    rollback_pending = false;
    stat_player_escapes_preserved.store(0);
}

} // namespace goh::grenade_dodge
