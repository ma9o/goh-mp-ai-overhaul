#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "live_telemetry.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace goh::config {

enum class LogLevel : std::uint8_t {
    off = 0,
    error = 1,
    summary = 2,
    verbose = 3
};

struct ModuleToggles {
    bool grenade_dodge{true};
    bool soft_cover_lof{true};
    bool attack_detour{true};
    bool target_prioritization{true};
    bool auto_smoke{true};
    bool tank_crusher{true};
    bool tank_auto_repair{true};
    bool auto_medic{true};
    bool auto_resupply{true};
    bool close_grenade{true};
    bool tactical_infantry{true};
    bool penetration_at{true};
    bool tactical_objectives{true};
    bool tank_facing{true};
    bool engine_fixes{true};
    bool lull_reload{true};
    bool advance_responsive{true};
    bool attack_order_polish{true};
    bool hit_the_dirt{true};
    bool crush_avoidance{true};
    bool nontank_auto_repair{true};
    bool recrew_weapons{true};
    bool battlefield_scavenge{true};
    bool fast_weapon_swap{true};
    bool grenade_ff_guard{true};
    bool safe_disembark{true};
    bool transport_unload{true};
    bool squad_logistics{true};
    bool tank_crew_fill{true};
    // Both extensions require the tank_crew_fill master switch.
    bool internal_crew_swap{true};
    bool nontank_crew_fill{true};
    bool ammo_scavenge{true};
    bool tank_target_memory{true};
    bool cannon_aim_settle{true};
    bool allegiance_guard{true};
    bool overflow_cover{true};
    bool weapon_team_cohesion{true};
    bool mg_crew_small_arms{true};
    bool movement_recovery{true};
    bool convoy_spacing{true};
    bool flanking_wedge{true};
    bool cover_frag{true};
    bool role_target_split{true};
    bool offensive_smoke{true};
};

struct Tuning {
    float grenade_detect_radius_m{22.0f};
    float grenade_danger_radius_m{4.0f};
    float smoke_vehicle_cooldown_s{25.0f};
    float smoke_infantry_cooldown_s{25.0f};
    float medic_search_radius_m{36.0f};
    float squad_share_radius_m{18.0f};
    float scavenge_radius_m{20.0f};
    float recrew_search_radius_m{28.0f};
    float scavenge_search_radius_m{16.0f};
    float resupply_search_radius_m{60.0f};
    float objectives_pursue_radius_m{28.0f};
    float objectives_cap_radius_m{35.0f};
    float lull_reload_ratio{0.35f};
    float tank_facing_min_angle_deg{24.f};
    float tank_memory_hold_s{5.0f};
    float cannon_settle_max_s{1.8f};
    float overflow_cover_radius_m{8.0f};
    float movement_stall_s{2.5f};
    float convoy_gap_m{8.0f};
    float crew_fill_emit_backoff_s{10.0f};
    std::uint32_t soft_cover_log_interval_ms{1000};
};

struct Settings {
    std::uint32_t ai_player_mask{0xFFFFFFFFu};
    float sim_speed{0.0f};
    bool skip_intro{false};
    bool auto_dismiss_loading{false};
    bool suppress_cursor_grab{false};
    bool hot_reload{false};
    std::uint32_t restart_seq{0};
    std::uint32_t quit_seq{0};
    std::string auto_mission;
    LogLevel log_level{LogLevel::summary};
    std::string log_filename{"goh_ai.log"};
    std::uint32_t log_flush_ms{500};
    ModuleToggles modules{};
    Tuning tuning{};
    // Optional extra non-WP smoke grenade keys and vehicle launcher program
    // tags (for DLC or mod packs). Stock WP items are always strictly blocked.
    std::vector<std::string> smoke_extra_grenades;
    std::vector<std::string> smoke_extra_programs;
    bool loaded_from_file{false};
};

namespace detail {

inline std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

inline std::string to_lower(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

inline bool parse_bool(std::string_view v, bool fallback) noexcept {
    const std::string low = to_lower(trim(v));
    if (low == "1" || low == "true" || low == "yes" || low == "on") return true;
    if (low == "0" || low == "false" || low == "no" || low == "off") return false;
    return fallback;
}

inline float parse_float(std::string_view v, float fallback, float lo, float hi) noexcept {
    const std::string buf(trim(v));
    if (buf.empty()) return fallback;
    char* end = nullptr;
    const float parsed = std::strtof(buf.c_str(), &end);
    if (!end || end == buf.c_str() || !std::isfinite(parsed)) return fallback;
    return std::clamp(parsed, lo, hi);
}

inline std::uint32_t parse_u32(std::string_view v, std::uint32_t fallback, std::uint32_t lo,
                               std::uint32_t hi) noexcept {
    const std::string buf(trim(v));
    if (buf.empty()) return fallback;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(buf.c_str(), &end, 10);
    if (!end || end == buf.c_str()) return fallback;
    const auto clamped = static_cast<std::uint32_t>(std::min<unsigned long>(parsed, static_cast<unsigned long>(hi)));
    return std::max(clamped, lo);
}

inline void parse_csv_tokens(std::string_view v, std::vector<std::string>& out) {
    out.clear();
    while (!v.empty()) {
        const std::size_t comma = v.find(',');
        const std::string_view part = comma == std::string_view::npos ? v : v.substr(0, comma);
        const std::string token = to_lower(trim(part));
        if (!token.empty()) out.push_back(token);
        if (comma == std::string_view::npos) break;
        v.remove_prefix(comma + 1);
    }
}

inline std::uint32_t parse_player_mask(std::string_view v) {
    const std::string buf(trim(v));
    if (buf.empty() || buf.front() == '-') return 0xFFFFFFFFu;
    char* end{};
    errno = 0;
    const unsigned long value = std::strtoul(buf.c_str(), &end, 0);
    if (errno == ERANGE || end != buf.data() + buf.size() || value > UINT32_MAX) return 0xFFFFFFFFu;
    return static_cast<std::uint32_t>(value);
}

inline bool safe_log_filename(std::string_view name) noexcept {
    if (name.empty() || name.size() > 128) return false;
    for (char c : name) {
        if (c == '/' || c == '\\' || c == ':' || c == '\0') return false;
    }
    return name != "." && name != "..";
}

inline bool safe_mission_path(std::string_view path) noexcept {
    if (path.empty() || path.size() > 128 || path.front() == '/' || path.back() == '/') return false;
    if (path.find("..") != std::string_view::npos || path.find("//") != std::string_view::npos) return false;
    bool has_slash = false;
    for (char c : path) {
        const auto uc = static_cast<unsigned char>(c);
        if (c == '/') {
            has_slash = true;
            continue;
        }
        if (!(std::isalnum(uc) || c == '_' || c == '-' || c == '.' || c == ':')) return false;
    }
    return has_slash;
}

} // namespace detail

// Pure C++17 INI parser (no Win32 dependency; identical on host tests and DLL).
inline Settings parse_ini(std::string_view text) {
    Settings s{};
    std::string section;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t nl = text.find('\n', pos);
        std::string_view line = nl == std::string_view::npos ? text.substr(pos) : text.substr(pos, nl - pos);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const std::size_t semi = line.find(';');
        if (semi != std::string_view::npos) line = line.substr(0, semi);
        const std::size_t hash = line.find('#');
        if (hash != std::string_view::npos) line = line.substr(0, hash);
        line = detail::trim(line);
        if (!line.empty()) {
            if (line.front() == '[' && line.back() == ']') {
                section = detail::to_lower(detail::trim(line.substr(1, line.size() - 2)));
            } else {
                const std::size_t eq = line.find('=');
                if (eq != std::string_view::npos) {
                    const std::string key = detail::to_lower(detail::trim(line.substr(0, eq)));
                    const std::string_view val = detail::trim(line.substr(eq + 1));
                    if (section == "logging") {
                        if (key == "level") {
                            const std::string lv = detail::to_lower(val);
                            if (lv == "off" || lv == "none" || lv == "0")
                                s.log_level = LogLevel::off;
                            else if (lv == "error" || lv == "1")
                                s.log_level = LogLevel::error;
                            else if (lv == "summary" || lv == "info" || lv == "2")
                                s.log_level = LogLevel::summary;
                            else if (lv == "verbose" || lv == "debug" || lv == "3")
                                s.log_level = LogLevel::verbose;
                        } else if ((key == "filename" || key == "file") && detail::safe_log_filename(val)) {
                            s.log_filename = std::string(val);
                        } else if (key == "flush_ms") {
                            s.log_flush_ms = detail::parse_u32(val, s.log_flush_ms, 0, 10000);
                        }
                    } else if (section == "testing") {
                        if (key == "ai_player_mask")
                            s.ai_player_mask = detail::parse_player_mask(val);
                        else if (key == "sim_speed") {
                            const std::string low = detail::to_lower(val);
                            if (low == "0" || low == "0.0" || low == "off" || low == "false")
                                s.sim_speed = 0.0f;
                            else
                                s.sim_speed = detail::parse_float(val, s.sim_speed, 0.25f, 20.0f);
                        } else if (key == "skip_intro") {
                            s.skip_intro = detail::parse_bool(val, s.skip_intro);
                        } else if (key == "auto_dismiss_loading") {
                            s.auto_dismiss_loading = detail::parse_bool(val, s.auto_dismiss_loading);
                        } else if (key == "suppress_cursor_grab") {
                            s.suppress_cursor_grab = detail::parse_bool(val, s.suppress_cursor_grab);
                        } else if (key == "hot_reload") {
                            s.hot_reload = detail::parse_bool(val, s.hot_reload);
                        } else if (key == "restart_seq") {
                            s.restart_seq = detail::parse_u32(val, s.restart_seq, 0u, UINT32_MAX);
                        } else if (key == "quit_seq") {
                            s.quit_seq = detail::parse_u32(val, s.quit_seq, 0u, UINT32_MAX);
                        } else if (key == "auto_mission") {
                            const std::string low = detail::to_lower(val);
                            if (low.empty() || low == "off" || low == "none" || low == "false")
                                s.auto_mission.clear();
                            else if (detail::safe_mission_path(val))
                                s.auto_mission = std::string(val);
                        }
                    } else if (section == "modules") {
                        auto& m = s.modules;
                        if (key == "grenade_dodge")
                            m.grenade_dodge = detail::parse_bool(val, m.grenade_dodge);
                        else if (key == "soft_cover_lof")
                            m.soft_cover_lof = detail::parse_bool(val, m.soft_cover_lof);
                        else if (key == "attack_detour")
                            m.attack_detour = detail::parse_bool(val, m.attack_detour);
                        else if (key == "target_prioritization")
                            m.target_prioritization = detail::parse_bool(val, m.target_prioritization);
                        else if (key == "auto_smoke")
                            m.auto_smoke = detail::parse_bool(val, m.auto_smoke);
                        else if (key == "tank_crusher")
                            m.tank_crusher = detail::parse_bool(val, m.tank_crusher);
                        else if (key == "tank_auto_repair")
                            m.tank_auto_repair = detail::parse_bool(val, m.tank_auto_repair);
                        else if (key == "auto_medic")
                            m.auto_medic = detail::parse_bool(val, m.auto_medic);
                        else if (key == "auto_resupply")
                            m.auto_resupply = detail::parse_bool(val, m.auto_resupply);
                        else if (key == "close_grenade")
                            m.close_grenade = detail::parse_bool(val, m.close_grenade);
                        else if (key == "tactical_infantry")
                            m.tactical_infantry = detail::parse_bool(val, m.tactical_infantry);
                        else if (key == "penetration_at")
                            m.penetration_at = detail::parse_bool(val, m.penetration_at);
                        else if (key == "tactical_objectives")
                            m.tactical_objectives = detail::parse_bool(val, m.tactical_objectives);
                        else if (key == "engine_fixes")
                            m.engine_fixes = detail::parse_bool(val, m.engine_fixes);
                        else if (key == "lull_reload")
                            m.lull_reload = detail::parse_bool(val, m.lull_reload);
                        else if (key == "tank_facing")
                            m.tank_facing = detail::parse_bool(val, m.tank_facing);
                        else if (key == "advance_responsive")
                            m.advance_responsive = detail::parse_bool(val, m.advance_responsive);
                        else if (key == "attack_order_polish")
                            m.attack_order_polish = detail::parse_bool(val, m.attack_order_polish);
                        else if (key == "hit_the_dirt")
                            m.hit_the_dirt = detail::parse_bool(val, m.hit_the_dirt);
                        else if (key == "nontank_auto_repair")
                            m.nontank_auto_repair = detail::parse_bool(val, m.nontank_auto_repair);
                        else if (key == "recrew_weapons")
                            m.recrew_weapons = detail::parse_bool(val, m.recrew_weapons);
                        else if (key == "battlefield_scavenge")
                            m.battlefield_scavenge = detail::parse_bool(val, m.battlefield_scavenge);
                        else if (key == "crush_avoidance")
                            m.crush_avoidance = detail::parse_bool(val, m.crush_avoidance);
                        else if (key == "fast_weapon_swap")
                            m.fast_weapon_swap = detail::parse_bool(val, m.fast_weapon_swap);
                        else if (key == "grenade_ff_guard")
                            m.grenade_ff_guard = detail::parse_bool(val, m.grenade_ff_guard);
                        else if (key == "safe_disembark")
                            m.safe_disembark = detail::parse_bool(val, m.safe_disembark);
                        else if (key == "transport_unload")
                            m.transport_unload = detail::parse_bool(val, m.transport_unload);
                        else if (key == "squad_logistics")
                            m.squad_logistics = detail::parse_bool(val, m.squad_logistics);
                        else if (key == "tank_crew_fill")
                            m.tank_crew_fill = detail::parse_bool(val, m.tank_crew_fill);
                        else if (key == "internal_crew_swap")
                            m.internal_crew_swap = detail::parse_bool(val, m.internal_crew_swap);
                        else if (key == "nontank_crew_fill")
                            m.nontank_crew_fill = detail::parse_bool(val, m.nontank_crew_fill);
                        else if (key == "ammo_scavenge")
                            m.ammo_scavenge = detail::parse_bool(val, m.ammo_scavenge);
                        else if (key == "tank_target_memory")
                            m.tank_target_memory = detail::parse_bool(val, m.tank_target_memory);
                        else if (key == "cannon_aim_settle")
                            m.cannon_aim_settle = detail::parse_bool(val, m.cannon_aim_settle);
                        else if (key == "allegiance_guard")
                            m.allegiance_guard = detail::parse_bool(val, m.allegiance_guard);
                        else if (key == "overflow_cover")
                            m.overflow_cover = detail::parse_bool(val, m.overflow_cover);
                        else if (key == "weapon_team_cohesion")
                            m.weapon_team_cohesion = detail::parse_bool(val, m.weapon_team_cohesion);
                        else if (key == "mg_crew_small_arms")
                            m.mg_crew_small_arms = detail::parse_bool(val, m.mg_crew_small_arms);
                        else if (key == "movement_recovery")
                            m.movement_recovery = detail::parse_bool(val, m.movement_recovery);
                        else if (key == "convoy_spacing")
                            m.convoy_spacing = detail::parse_bool(val, m.convoy_spacing);
                        else if (key == "flanking_wedge")
                            m.flanking_wedge = detail::parse_bool(val, m.flanking_wedge);
                        else if (key == "cover_frag")
                            m.cover_frag = detail::parse_bool(val, m.cover_frag);
                        else if (key == "role_target_split")
                            m.role_target_split = detail::parse_bool(val, m.role_target_split);
                        else if (key == "offensive_smoke")
                            m.offensive_smoke = detail::parse_bool(val, m.offensive_smoke);
                    } else if (section == "tuning") {
                        auto& t = s.tuning;
                        if (key == "grenade_detect_radius_m")
                            t.grenade_detect_radius_m =
                                detail::parse_float(val, t.grenade_detect_radius_m, 6.0f, 60.0f);
                        else if (key == "grenade_danger_radius_m")
                            t.grenade_danger_radius_m =
                                detail::parse_float(val, t.grenade_danger_radius_m, 2.0f, 20.0f);
                        else if (key == "smoke_vehicle_cooldown_s")
                            t.smoke_vehicle_cooldown_s =
                                detail::parse_float(val, t.smoke_vehicle_cooldown_s, 5.0f, 120.0f);
                        else if (key == "smoke_infantry_cooldown_s")
                            t.smoke_infantry_cooldown_s =
                                detail::parse_float(val, t.smoke_infantry_cooldown_s, 5.0f, 120.0f);
                        else if (key == "recrew_search_radius_m")
                            t.recrew_search_radius_m = detail::parse_float(val, t.recrew_search_radius_m, 8.f, 50.f);
                        else if (key == "scavenge_search_radius_m")
                            t.scavenge_search_radius_m =
                                detail::parse_float(val, t.scavenge_search_radius_m, 6.f, 35.f);
                        else if (key == "medic_search_radius_m")
                            t.medic_search_radius_m = detail::parse_float(val, t.medic_search_radius_m, 10.0f, 100.0f);
                        else if (key == "squad_share_radius_m")
                            t.squad_share_radius_m = detail::parse_float(val, t.squad_share_radius_m, 6.0f, 40.0f);
                        else if (key == "scavenge_radius_m")
                            t.scavenge_radius_m = detail::parse_float(val, t.scavenge_radius_m, 6.0f, 40.0f);
                        else if (key == "resupply_search_radius_m")
                            t.resupply_search_radius_m =
                                detail::parse_float(val, t.resupply_search_radius_m, 15.0f, 150.0f);
                        else if (key == "objectives_pursue_radius_m")
                            t.objectives_pursue_radius_m =
                                detail::parse_float(val, t.objectives_pursue_radius_m, 15.0f, 120.0f);
                        else if (key == "objectives_cap_radius_m")
                            t.objectives_cap_radius_m =
                                detail::parse_float(val, t.objectives_cap_radius_m, 10.0f, 80.0f);
                        else if (key == "lull_reload_ratio")
                            t.lull_reload_ratio = detail::parse_float(val, t.lull_reload_ratio, .10f, .75f);
                        else if (key == "tank_facing_min_angle_deg")
                            t.tank_facing_min_angle_deg =
                                detail::parse_float(val, t.tank_facing_min_angle_deg, 15.f, 60.f);
                        else if (key == "tank_memory_hold_s")
                            t.tank_memory_hold_s = detail::parse_float(val, t.tank_memory_hold_s, 1.0f, 15.0f);
                        else if (key == "cannon_settle_max_s")
                            t.cannon_settle_max_s = detail::parse_float(val, t.cannon_settle_max_s, 0.3f, 5.0f);
                        else if (key == "overflow_cover_radius_m")
                            t.overflow_cover_radius_m =
                                detail::parse_float(val, t.overflow_cover_radius_m, 3.0f, 20.0f);
                        else if (key == "movement_stall_s")
                            t.movement_stall_s = detail::parse_float(val, t.movement_stall_s, 1.0f, 8.0f);
                        else if (key == "convoy_gap_m")
                            t.convoy_gap_m = detail::parse_float(val, t.convoy_gap_m, 4.0f, 20.0f);
                        else if (key == "crew_fill_emit_backoff_s")
                            t.crew_fill_emit_backoff_s =
                                detail::parse_float(val, t.crew_fill_emit_backoff_s, 1.0f, 60.0f);
                        else if (key == "soft_cover_log_interval_ms")
                            t.soft_cover_log_interval_ms =
                                detail::parse_u32(val, t.soft_cover_log_interval_ms, 100, 60000);
                    } else if (section == "smoke") {
                        if (key == "extra_grenades")
                            detail::parse_csv_tokens(val, s.smoke_extra_grenades);
                        else if (key == "extra_programs")
                            detail::parse_csv_tokens(val, s.smoke_extra_programs);
                    }
                }
            }
        }
        if (nl == std::string_view::npos) break;
        pos = nl + 1;
    }
    return s;
}

inline Settings& startup_settings() noexcept {
    static Settings instance{};
    return instance;
}

inline live::PublishedSettings<Settings>& published() {
    static live::PublishedSettings<Settings> settings;
    return settings;
}

inline Settings& active() {
    return published().view(startup_settings());
}

inline bool player_ai_enabled(std::uint32_t player) noexcept {
    const auto mask = active().ai_player_mask;
    return mask == 0xFFFFFFFFu || (player < 32u && ((mask >> player) & 1u) != 0);
}

inline void reset_defaults() {
    active() = Settings{};
}

#if defined(_WIN32)
inline bool load_beside_module(HMODULE self_module, const wchar_t* ini_name = L"goh_ai.ini") {
    auto& startup = startup_settings();
    startup = Settings{};
    if (!self_module || !ini_name) return false;
    wchar_t path[4096]{};
    const DWORD n = GetModuleFileNameW(self_module, path, static_cast<DWORD>(std::size(path)));
    if (!n || n >= std::size(path)) return false;
    wchar_t* slash = std::wcsrchr(path, L'\\');
    if (!slash) return false;
    const std::size_t name_len = std::wcslen(ini_name) + 1;
    if (static_cast<std::size_t>(slash + 1 - path) + name_len > std::size(path)) return false;
    std::wmemcpy(slash + 1, ini_name, name_len);
    FILE* f = _wfopen(path, L"rb");
    if (!f) return false;
    std::string contents;
    char buf[4096];
    std::size_t read_n = 0;
    while ((read_n = std::fread(buf, 1, sizeof(buf), f)) > 0 && contents.size() < 262144) {
        contents.append(buf, read_n);
    }
    std::fclose(f);
    startup = parse_ini(contents);
    startup.loaded_from_file = true;
    return true;
}
#endif

} // namespace goh::config

namespace goh::log {

using Level = config::LogLevel;

struct State {
    std::mutex mutex;
    std::atomic<std::uint8_t> min_level{static_cast<std::uint8_t>(Level::summary)};
    FILE* file{};
    bool configured{};
    std::uint32_t flush_ms{500};
    std::uint64_t last_flush_ms{};
#if defined(_WIN32)
    HMODULE self_module{};
#endif
    std::string filename{"goh_ai.log"};
};

inline State& state() noexcept {
    static State instance{};
    return instance;
}

inline void configure(const config::Settings& s) {
    auto& st = state();
    std::lock_guard<std::mutex> lock(st.mutex);
    st.min_level.store(static_cast<std::uint8_t>(s.log_level), std::memory_order_release);
    st.flush_ms = s.log_flush_ms;
    if (!st.configured) {
        if (!s.log_filename.empty()) st.filename = s.log_filename;
        st.configured = true;
    }
}

#if defined(_WIN32)
inline void attach_module(HMODULE mod) noexcept {
    auto& st = state();
    std::lock_guard<std::mutex> lock(st.mutex);
    st.self_module = mod;
}
#endif

inline bool enabled(Level lvl) noexcept {
    const auto cur = state().min_level.load(std::memory_order_acquire);
    return lvl != Level::off && static_cast<std::uint8_t>(lvl) <= cur;
}

inline void close() {
    auto& st = state();
    std::lock_guard<std::mutex> lock(st.mutex);
    if (st.file) {
        std::fflush(st.file);
        std::fclose(st.file);
        st.file = nullptr;
    }
}

inline void vwrite(Level lvl, const char* fmt, std::va_list ap) {
    if (!enabled(lvl) || !fmt) return;
    auto& st = state();
    std::lock_guard<std::mutex> lock(st.mutex);
    if (!enabled(lvl)) return;
#if defined(_WIN32)
    if (!st.file && st.self_module) {
        wchar_t path[4096]{};
        const DWORD n = GetModuleFileNameW(st.self_module, path, static_cast<DWORD>(std::size(path)));
        if (n && n < std::size(path)) {
            wchar_t* slash = std::wcsrchr(path, L'\\');
            if (slash) {
                std::wstring wname;
                wname.reserve(st.filename.size());
                for (char c : st.filename)
                    wname.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
                if (static_cast<std::size_t>(slash + 1 - path) + wname.size() + 1 <= std::size(path)) {
                    std::wmemcpy(slash + 1, wname.c_str(), wname.size() + 1);
                    st.file = _wfopen(path, L"a");
                }
            }
        }
    }
    if (!st.file) return;
    SYSTEMTIME tm{};
    GetLocalTime(&tm);
    std::fprintf(st.file, "[%02d:%02d:%02d.%03d] ", tm.wHour, tm.wMinute, tm.wSecond, tm.wMilliseconds);
    std::vfprintf(st.file, fmt, ap);
    std::fputc('\n', st.file);
    const std::uint64_t now_ms = GetTickCount64();
    if (lvl != Level::verbose || st.flush_ms == 0 || (now_ms - st.last_flush_ms) >= st.flush_ms) {
        std::fflush(st.file);
        st.last_flush_ms = now_ms;
    }
#else
    (void)ap;
#endif
}

#if defined(__MINGW32__)
#define GOH_PRINTF_ATTR(f, a) __attribute__((format(__MINGW_PRINTF_FORMAT, f, a)))
#elif defined(__GNUC__) || defined(__clang__)
#define GOH_PRINTF_ATTR(f, a) __attribute__((format(printf, f, a)))
#else
#define GOH_PRINTF_ATTR(f, a)
#endif

GOH_PRINTF_ATTR(2, 3) inline void write(Level lvl, const char* fmt, ...) {
    if (!enabled(lvl)) return;
    std::va_list ap;
    va_start(ap, fmt);
    vwrite(lvl, fmt, ap);
    va_end(ap);
}

// Adapter matching existing module LogSink signature (`void (*)(const char*, ...)`).
GOH_PRINTF_ATTR(1, 2) inline void sink_summary(const char* fmt, ...) {
    if (!enabled(Level::summary)) return;
    std::va_list ap;
    va_start(ap, fmt);
    vwrite(Level::summary, fmt, ap);
    va_end(ap);
}

GOH_PRINTF_ATTR(1, 2) inline void sink_verbose(const char* fmt, ...) {
    if (!enabled(Level::verbose)) return;
    std::va_list ap;
    va_start(ap, fmt);
    vwrite(Level::verbose, fmt, ap);
    va_end(ap);
}

} // namespace goh::log
