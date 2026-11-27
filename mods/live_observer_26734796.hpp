#pragma once

// Attach after PE/RTTI verification, observe inside RunOrigSlot23 before
// calling the trampoline, close after draining all hooks. No hook installation lives here.
#ifdef _WIN32
#include "live_telemetry.hpp"
#include "ai_modules.hpp"
#include "config_and_log.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace goh::live {
static_assert(std::string_view(fingerprint) == runtime::kBuild26734796Sha256);

class GemFileName {
    alignas(16) std::byte buf_[0x80]{};
    runtime::Address base_{};

public:
    GemFileName(runtime::Address base, const char* text) : base_(base) {
        using FnCtor = void(__fastcall*)(void*, const char*);
        runtime::native<FnCtor>(base_, runtime::table().fn.filename_ctor)(buf_, text);
    }
    ~GemFileName() {
        using FnDtor = void(__fastcall*)(void*);
        runtime::native<FnDtor>(base_, runtime::table().fn.filename_dtor)(buf_);
    }
    void* ptr() noexcept {
        return buf_;
    }
    GemFileName(const GemFileName&) = delete;
    GemFileName& operator=(const GemFileName&) = delete;
};

// Safely posts a payload-free 0x38-byte engine event (e.g. cmGameRestart 0xFC4 or cmQuit 9)
// onto the engine's critical-section-guarded deferred event queue.
inline void post_simple_event(runtime::Address base, runtime::Address vtable_rva, std::uint32_t event_id) {
    if (!base) return;
    struct SimpleEvent {
        runtime::Address vtable{};
        std::uint32_t id{};
        std::uint32_t pad{};
        std::uint64_t f10{};
        std::uint64_t f18{};
        std::uint64_t f20{};
        std::uint64_t f28{};
        std::uint64_t f30{};
    };
    static_assert(sizeof(SimpleEvent) == 0x38);
    SimpleEvent ev{};
    ev.vtable = base + vtable_rva;
    ev.id = event_id;
    using FnEventPost = void(__fastcall*)(void*, void*);
    runtime::native<FnEventPost>(base, runtime::table().fn.event_post)(&ev, nullptr);
}

// Safely posts Events::Scene::cmGameRestart (0xFC4) (same path as gui.gm.restart).
inline void post_game_restart(runtime::Address base) {
    post_simple_event(base, runtime::table().vt.cm_game_restart, 0xFC4u);
}

// Safely posts Events::cmQuit (9) to trigger clean eDP_Shutdown without a confirmation dialog.
inline void post_game_quit(runtime::Address base) {
    post_simple_event(base, runtime::table().vt.cm_quit, 9u);
}

// Main-thread only: navigates LocationManager to a verified singleplayer mission path
// (e.g. "single/00-bootcamp/02_vehicle-control:0") and shows eSceneLauncher_Singleplayer
// using the active profile difficulty (-1). Pre-validates every segment to avoid GEM throws.
inline bool launch_singleplayer_mission(runtime::Address base, std::string_view mission_path) {
    if (!base || !runtime::is_host_main_thread(base) || !config::detail::safe_mission_path(mission_path)) return false;
    std::vector<std::string> segments;
    std::size_t pos = 0;
    while (pos < mission_path.size()) {
        const auto slash = mission_path.find('/', pos);
        const auto part =
            slash == std::string_view::npos ? mission_path.substr(pos) : mission_path.substr(pos, slash - pos);
        if (part.empty()) return false;
        segments.emplace_back(part);
        if (slash == std::string_view::npos) break;
        pos = slash + 1;
    }
    if (segments.size() < 2) return false;

    const auto& fn = runtime::table().fn;
    const auto root = runtime::read<runtime::Address>(base, runtime::table().glob.location_root);
    if (!runtime::is_plausible_ptr(root)) {
        log::write(log::Level::error, "Testing: auto_mission failed (location root unavailable)");
        return false;
    }

    using FnFindChild = runtime::Address(__fastcall*)(runtime::Address, void*);
    runtime::Address cur = root;
    for (auto& seg : segments) {
        GemFileName name(base, seg.c_str());
        runtime::Address next = runtime::native<FnFindChild>(base, fn.loc_find_child)(cur, name.ptr());
        if (!runtime::is_plausible_ptr(next)) {
            // Singleplayer mission logs append ":0" (e.g. "02_vehicle-control:0"), while
            // LocationManager nodes are keyed by directory name ("02_vehicle-control").
            const auto colon = seg.find(':');
            if (colon != std::string::npos && colon > 0) {
                std::string stripped = seg.substr(0, colon);
                GemFileName alt_name(base, stripped.c_str());
                next = runtime::native<FnFindChild>(base, fn.loc_find_child)(cur, alt_name.ptr());
                if (runtime::is_plausible_ptr(next)) seg = std::move(stripped);
            }
        }
        if (!runtime::is_plausible_ptr(next)) {
            log::write(log::Level::error, "Testing: auto_mission segment '%s' not found in '%.*s'", seg.c_str(),
                       static_cast<int>(mission_path.size()), mission_path.data());
            return false;
        }
        cur = next;
    }

    using FnVoid0 = void(__fastcall*)();
    using FnEnter = void(__fastcall*)(void*);
    using FnCurrent = runtime::Address(__fastcall*)();
    using FnAlloc = runtime::Address(__fastcall*)(std::size_t);
    using FnLauncherCtor = runtime::Address(__fastcall*)(runtime::Address, runtime::Address, std::int32_t);
    using FnPageInit = void(__fastcall*)(runtime::Address);
    using FnPageMgrGet = runtime::Address(__fastcall*)();
    using FnPageMgrShow = void(__fastcall*)(runtime::Address, runtime::Address, std::int32_t);

    runtime::native<FnVoid0>(base, fn.loc_reset_root)();
    for (const auto& seg : segments) {
        GemFileName name(base, seg.c_str());
        runtime::native<FnEnter>(base, fn.loc_enter)(name.ptr());
    }
    const auto loc = runtime::native<FnCurrent>(base, fn.loc_current)();
    if (!runtime::is_plausible_ptr(loc)) return false;

    const auto mem = runtime::native<FnAlloc>(base, fn.game_alloc)(0xB0u);
    if (!mem) return false;
    const auto launcher = runtime::native<FnLauncherCtor>(base, fn.sp_launcher_ctor)(mem, loc, -1);
    if (!launcher) return false;
    runtime::native<FnPageInit>(base, fn.page_init)(launcher);
    const auto mgr = runtime::native<FnPageMgrGet>(base, fn.page_mgr_get)();
    if (!mgr) return false;
    runtime::native<FnPageMgrShow>(base, fn.page_mgr_show)(mgr, launcher, 1);
    log::write(log::Level::summary, "Testing: auto-launched mission '%.*s'", static_cast<int>(mission_path.size()),
               mission_path.data());
    return true;
}

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
    static constexpr runtime::Tick kMinLifecycleTick = 500u;

    Writer writer_;
    Files files_;
    bool attached_{};
    bool speed_applied_{};
    runtime::Tick last_speed_tick_{};
    runtime::Address last_scene_{};
    std::uint32_t last_restart_seq_{};
    std::uint32_t pending_restart_seq_{};
    bool restart_seq_ready_{};
    bool has_pending_restart_{};
    std::uint32_t last_quit_seq_{};
    std::uint32_t pending_quit_seq_{};
    bool quit_seq_ready_{};
    bool has_pending_quit_{};

public:
    bool attach(HMODULE dll, std::string_view verified_sha) {
        wchar_t image[4096]{};
        const auto size = GetModuleFileNameW(nullptr, image, static_cast<DWORD>(std::size(image)));
        if (!size || size >= std::size(image) || verified_sha != fingerprint) return false;
        std::wstring_view path(image, size);
        const auto slash = path.find_last_of(L"\\/");
        attached_ = slash != std::wstring_view::npos && _wcsicmp(image + slash + 1, L"call_to_arms.exe") == 0 &&
                    files_.attach(dll);
        last_restart_seq_ = config::startup_settings().restart_seq;
        pending_restart_seq_ = last_restart_seq_;
        restart_seq_ready_ = attached_;
        has_pending_restart_ = false;
        last_quit_seq_ = config::startup_settings().quit_seq;
        pending_quit_seq_ = last_quit_seq_;
        quit_seq_ready_ = attached_;
        has_pending_quit_ = false;
        last_scene_ = 0;
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
        if (w.scene != last_scene_) {
            last_scene_ = w.scene;
            speed_applied_ = false;
        }
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
            if (quit_seq_ready_ && parsed.quit_seq != last_quit_seq_) {
                pending_quit_seq_ = parsed.quit_seq;
                has_pending_quit_ = true;
            }
            if (restart_seq_ready_ && parsed.restart_seq != last_restart_seq_) {
                pending_restart_seq_ = parsed.restart_seq;
                has_pending_restart_ = true;
            }
            return true;
        });
        // Defer scene-lifecycle events past starter:firstQuant so eGUI_PageLoading and
        // page_scene_game finish activating before cmGameRestart / cmQuit dispatches.
        if (w.now >= kMinLifecycleTick) {
            if (has_pending_quit_) {
                has_pending_quit_ = false;
                has_pending_restart_ = false;
                last_quit_seq_ = pending_quit_seq_;
                writer_.flush(now);
                post_game_quit(base);
                log::write(log::Level::summary, "=== goh_ai testing: posted cmQuit (seq=%u) ===", last_quit_seq_);
                return;
            }
            if (has_pending_restart_) {
                has_pending_restart_ = false;
                last_restart_seq_ = pending_restart_seq_;
                speed_applied_ = false;
                writer_.flush(now);
                post_game_restart(base);
                log::write(log::Level::summary,
                           "=== goh_ai testing: posted cmGameRestart (seq=%u) ===", last_restart_seq_);
                return;
            }
        }
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
        if (writer_.stats_due(now, w.now))
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
        last_scene_ = 0;
        last_restart_seq_ = 0;
        pending_restart_seq_ = 0;
        restart_seq_ready_ = false;
        has_pending_restart_ = false;
        last_quit_seq_ = 0;
        pending_quit_seq_ = 0;
        quit_seq_ready_ = false;
        has_pending_quit_ = false;
    }
};
} // namespace goh::live
#endif
