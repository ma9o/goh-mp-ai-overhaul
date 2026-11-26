#pragma once

// Attach after PE/RTTI verification, observe inside RunOrigSlot23 before
// calling the trampoline, close after draining all hooks. No hook installation lives here.
#ifdef _WIN32
#include "live_telemetry.hpp"
#include "ai_modules.hpp"
#include "config_and_log.hpp"

namespace goh::live {
static_assert(std::string_view(fingerprint) == runtime::kBuild26734796Sha256);

struct SampleClock {
    runtime::Address base, scene;
    runtime::Tick now;
    float length, ticks;
};

inline ActorSnapshot read_snapshot(runtime::Address actor, runtime::Address brain, bool human, const SampleClock& w) {
    using runtime::Address;
    using runtime::read;
    ActorSnapshot a{};
    a.mid = read<std::uint16_t>(actor, 0xDAA);
    a.address = actor;
    a.sim_tick = w.now;
    a.ticks_per_second = w.ticks;
    a.is_human = human;
    a.player = read<runtime::Byte>(actor, 0xDA8);
    auto policy = read<unsigned>(brain, 0x398);
    a.control = policy & 3;
    a.move_mode = (policy >> 12) & 3;
    a.fire_mode = (policy >> 10) & 3;
    const auto pos = read<runtime::Vec3>(actor, 0x74);
    a.pos_x = pos.x;
    a.pos_y = pos.y;
    a.hp_ratio = a.morale = std::numeric_limits<double>::quiet_NaN();
    if (human) {
        a.morale = read<float>(brain, 0x474);
        constexpr std::string_view posture[] = {"flee", "fallback", "hold", "advance", "assault"};
        const auto state = read<unsigned>(brain, 0x52c);
        a.behavior = state < std::size(posture) ? posture[state] : "unknown";
        const auto vit = read<Address>(read<Address>(actor, 0x578), 0x18);
        if (vit) {
            using Health = float (*)(Address);
            const float max = runtime::native<Health>(w.base, 0x93D800)(vit);
            if (max > 0) a.hp_ratio = runtime::native<Health>(w.base, 0x93D880)(vit) / max;
        }
        a.grenade_land_m = tu_combat::nearest_tracked_grenade_m(w.scene, w.now, pos, w.length);
    } else {
        a.behavior = "vehicle";
        a.hp_ratio = tu_infantry::vehicle_hull_health(actor);
        auto forward = read<runtime::XY>(read<Address>(actor, 0xDE0), 0x5c);
        if (!runtime::finite(forward) || std::hypot(forward.x, forward.y) < 1e-4f)
            forward = read<runtime::XY>(actor, 0x50);
        if (runtime::finite(forward) && std::hypot(forward.x, forward.y) >= 1e-4f)
            a.heading_deg = std::atan2(forward.y, forward.x) * 57.295779513;
        const auto targets = tu_combat::vehicle_weapon_targets(brain);
        a.mg_target = targets.mg;
        a.cannon_target = targets.cannon;
    }
    return a;
}

class Observer {
    Writer writer_;
    Files files_;
    bool attached_{};
    bool speed_applied_{};
    runtime::Tick last_speed_tick_{};

public:
    bool attach(HMODULE dll, std::string_view verified_sha) {
        wchar_t image[4096]{};
        const auto size = GetModuleFileNameW(nullptr, image, static_cast<DWORD>(std::size(image)));
        if (!size || size >= std::size(image) || verified_sha != fingerprint) return false;
        std::wstring_view path(image, size);
        const auto slash = path.find_last_of(L"\\/");
        attached_ = slash != std::wstring_view::npos && _wcsicmp(image + slash + 1, L"call_to_arms.exe") == 0 &&
                    files_.attach(dll);
        return attached_;
    }

    // Called exclusively from the serial actor-slot callback, before its original trampoline.
    template <class Publish> void observe(runtime::Address base, runtime::Address actor, Publish publish) {
        using runtime::read;
        if (!attached_ || !runtime::global_accepting.load(std::memory_order_acquire) ||
            !runtime::is_host_main_thread(base) || !actor || !(read<runtime::Byte>(actor, 0x30) & 2u) ||
            (read<unsigned>(actor, 0x38) & 0xA20u))
            return;
        const auto& globals = runtime::table().glob;
        SampleClock w{base, read<runtime::Address>(base, globals.scene_ptr), read<runtime::Tick>(base, 0x1584568),
                      read<float>(base, globals.spatial_scale_l), read<float>(base, 0x149E758)};
        if (!w.scene || !(w.length > 0 && w.length < 10000 && w.ticks >= 1 && w.ticks <= 1000000)) return;
        const auto brain = runtime::read<runtime::Address>(actor, 0xDC8);
        if (!brain || runtime::read<runtime::Address>(brain, 0x38) != actor) return;
        const auto vt = runtime::read<runtime::Address>(brain);
        const bool human = vt == w.base + runtime::table().vt.human_brain;
        if (!human && vt != w.base + runtime::table().vt.vehicle_brain) return;
        const auto now = GetTickCount64();
        files_.poll(now, [&](std::string_view text, const Config& cfg) {
            const auto parsed = config::parse_ini(text);
            if (!files_.configure(writer_, cfg, executable, fingerprint, now, w.now)) return false;
            publish(parsed);
            const auto& t = parsed.tuning;
            if (writer_.active()) {
                writer_.module_event(now, w.now, "tuning", "grenade_danger_radius_m", 0, 0, t.grenade_danger_radius_m);
                writer_.module_event(now, w.now, "tuning", "medic_search_radius_m", 0, 0, t.medic_search_radius_m);
                writer_.module_event(now, w.now, "tuning", "squad_share_radius_m", 0, 0, t.squad_share_radius_m);
                writer_.module_event(now, w.now, "tuning", "resupply_search_radius_m", 0, 0,
                                     t.resupply_search_radius_m);
                writer_.module_event(now, w.now, "tuning", "tank_facing_min_angle_deg", 0, 0,
                                     t.tank_facing_min_angle_deg);
            }
            return true;
        });
        const float sim_speed = config::active().sim_speed;
        if (sim_speed > 0.0f && (!speed_applied_ || w.now != last_speed_tick_)) {
            const auto mgr = base + globals.quant_manager;
            const float target = 1.0f / sim_speed;
            const float current = read<float>(mgr, 0x50);
            if (std::isfinite(current) && std::fabs(current - target) > 0.005f) {
                using FnSetSlowdown = void(__fastcall*)(runtime::Address, runtime::Address, float, float);
                runtime::native<FnSetSlowdown>(base, runtime::table().fn.set_slowdown)(mgr, mgr, target, 1.0f);
            }
            last_speed_tick_ = w.now;
            speed_applied_ = true;
        }
        if (!writer_.active()) return;
        if (writer_.stats_due(now))
            writer_.stats(
                now, w.now,
                counters(tu_combat::snapshot_stats(), tu_infantry::snapshot_stats(), tu_tanks::snapshot_stats()));
        const auto id = runtime::read<unsigned>(actor, 0xc0);
        writer_.sample(now, id, [&] { return read_snapshot(actor, brain, human, w); });
    }
    void close() {
        writer_.close();
        attached_ = false;
        speed_applied_ = false;
        last_speed_tick_ = 0;
    }
};
} // namespace goh::live
#endif
