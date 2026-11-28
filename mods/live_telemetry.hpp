#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <locale>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace goh::live {

constexpr char executable[] = "call_to_arms.exe";
constexpr char fingerprint[] = "81a13de787c1869b766004cdcadb9ff99012f0b1b58b89db9a7741b10b9f059a";

struct Config {
    bool enabled{};
    char filename[129]{"goh_telemetry.jsonl"};
    std::uint32_t snapshot_interval_ms{500}, flush_ms{500};
};

inline std::string_view trim(std::string_view s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1);
}

inline std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    return out;
}

inline bool jsonl_name(std::string_view name) {
    auto s = lower(name);
    if (s.size() <= 6 || s.size() > 128 || s.substr(s.size() - 6) != ".jsonl" ||
        s.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-.") != std::string::npos || s.front() == '.')
        return false;
    const auto stem = s.substr(0, s.find('.'));
    return stem != "con" && stem != "prn" && stem != "nul" && stem != "aux" &&
           !(stem.size() == 4 && (stem.substr(0, 3) == "com" || stem.substr(0, 3) == "lpt") && stem[3] >= '0' &&
             stem[3] <= '9');
}

inline Config parse_ini(std::string_view text) {
    Config cfg;
    bool selected = false;
    while (!text.empty()) {
        const auto nl = text.find('\n');
        auto line = text.substr(0, nl);
        text.remove_prefix(nl == std::string_view::npos ? text.size() : nl + 1);
        line = trim(line.substr(0, line.find_first_of(";#")));
        if (line.empty()) continue;
        if (line.front() == '[') {
            selected = line.back() == ']' && lower(trim(line.substr(1, line.size() - 2))) == "telemetry";
            continue;
        }
        const auto eq = line.find('=');
        if (!selected || eq == std::string_view::npos) continue;
        const auto key = lower(trim(line.substr(0, eq)));
        const auto val = trim(line.substr(eq + 1));
        if (key == "enabled") {
            const auto v = lower(val);
            if (v == "true" || v == "yes" || v == "on" || v == "1") cfg.enabled = true;
            if (v == "false" || v == "no" || v == "off" || v == "0") cfg.enabled = false;
        } else if (key == "filename" && jsonl_name(val)) {
            std::memcpy(cfg.filename, val.data(), val.size());
            cfg.filename[val.size()] = 0;
        } else if (!val.empty()) {
            std::uint32_t number{};
            const auto parsed = std::from_chars(val.data(), val.data() + val.size(), number);
            if (parsed.ec != std::errc{} || parsed.ptr != val.data() + val.size()) continue;
            if (key == "snapshot_interval_ms") cfg.snapshot_interval_ms = std::clamp(number, 50u, 60000u);
            if (key == "flush_ms") cfg.flush_ms = std::min(number, 60000u);
        }
    }
    return cfg;
}

// A reader has its own settings after the first publication. Startup owns the frozen smoke lists
// and filename. Copies are owned by PublishedSettings (never non-trivial thread_local objects) so
// Windows/MinGW __cxa_thread_atexit never pins the DLL across hot-unloads.
template <class Settings> class PublishedSettings {
    using Modules = decltype(Settings::modules);
    using Tuning = decltype(Settings::tuning);
    static_assert(std::is_trivially_copyable_v<Modules> && std::is_trivially_copyable_v<Tuning>);
    Modules modules_{};
    Tuning tuning_{};
    decltype(Settings::log_level) level_{};
    std::uint32_t flush_{};
    std::uint32_t ai_player_mask_{};
    float sim_speed_{};
    bool auto_dismiss_loading_{};
    bool suppress_cursor_grab_{};
    std::uint32_t restart_seq_{};
    std::uint32_t quit_seq_{};
    std::mutex mutex_;
    std::vector<std::unique_ptr<Settings>> thread_copies_;
    std::atomic<std::uint64_t> generation_{};
    std::uint64_t id_{next_id()};

    static std::uint64_t next_id() noexcept {
        static std::atomic<std::uint64_t> seq{1};
        return seq.fetch_add(1, std::memory_order_relaxed);
    }

public:
    // One instance per process. Call publish only in the serial simulation callback.
    void publish(const Settings& next) {
        std::lock_guard<std::mutex> lock(mutex_);
        modules_ = next.modules;
        tuning_ = next.tuning;
        level_ = next.log_level;
        flush_ = next.log_flush_ms;
        ai_player_mask_ = next.ai_player_mask;
        sim_speed_ = next.sim_speed;
        auto_dismiss_loading_ = next.auto_dismiss_loading;
        suppress_cursor_grab_ = next.suppress_cursor_grab;
        restart_seq_ = next.restart_seq;
        quit_seq_ = next.quit_seq;
        generation_.fetch_add(1, std::memory_order_release);
    }
    Settings& view(Settings& startup) {
        const auto current = generation_.load(std::memory_order_acquire);
        if (!current) return startup;
        thread_local Settings* copy = nullptr;
        thread_local std::uint64_t owner = 0;
        thread_local std::uint64_t seen = 0;
        if (owner != id_ || !copy) {
            std::lock_guard<std::mutex> lock(mutex_);
            thread_copies_.push_back(std::make_unique<Settings>(startup));
            copy = thread_copies_.back().get();
            owner = id_;
            seen = 0;
        }
        if (seen != current) {
            std::lock_guard<std::mutex> lock(mutex_);
            copy->modules = modules_;
            copy->tuning = tuning_;
            copy->log_level = level_;
            copy->log_flush_ms = flush_;
            copy->ai_player_mask = ai_player_mask_;
            copy->sim_speed = sim_speed_;
            copy->auto_dismiss_loading = auto_dismiss_loading_;
            copy->suppress_cursor_grab = suppress_cursor_grab_;
            copy->restart_seq = restart_seq_;
            copy->quit_seq = quit_seq_;
            seen = generation_.load(std::memory_order_relaxed);
        }
        return *copy;
    }
};

// Serial caller supplies monotonic wall time, independent of game pause/speed/scene rewind.
struct ReloadClock {
    std::uint64_t last_poll{}, applied_stamp{};
    std::uint32_t applied_size{};
    bool polled{}, applied{};
    bool due(std::uint64_t now) {
        if (polled && now - last_poll < 1000) return false;
        polled = true;
        last_poll = now;
        return true;
    }
    bool changed(std::uint64_t stamp, std::uint32_t size) const {
        return !applied || applied_stamp != stamp || applied_size != size;
    }
    void commit(std::uint64_t stamp, std::uint32_t size) {
        applied = true;
        applied_stamp = stamp;
        applied_size = size;
    }
};

struct ActorSnapshot {
    std::uint32_t sim_tick{}, mid{}, player{}, control{}, move_mode{}, fire_mode{};
    bool is_human{};
    double pos_x{}, pos_y{}, hp_ratio{}, morale{};
    std::string_view behavior;
    std::uintptr_t address{}, cannon_target{}, mg_target{};
    double grenade_land_m{std::numeric_limits<double>::quiet_NaN()};
    double ticks_per_second{};
    double heading_deg{std::numeric_limits<double>::quiet_NaN()};
};

inline std::string quoted(std::string_view s) {
    constexpr char hex[] = "0123456789abcdef";
    std::string out(1, '"');
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c < 32 || c >= 127) {
            out += "\\u00";
            out += hex[c >> 4];
            out += hex[c & 15];
        } else
            out += static_cast<char>(c);
    }
    return out + '"';
}

inline std::string number(double v) {
    if (!std::isfinite(v)) return "null";
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream.precision(10);
    stream << v;
    return stream.str();
}

// Counter names follow the three public snapshot_stats() structures. The index is stable across samples.
#define GOH_LIVE_COMBAT(F) \
    F(dodges)              \
    F(player_escapes)      \
    F(soft_lof)            \
    F(soft_rej)            \
    F(det_inf)             \
    F(det_veh)             \
    F(at_volumes)          \
    F(at_ammo)             \
    F(at_holds)            \
    F(settle_holds)        \
    F(memory_holds)        \
    F(ff_vetoes)           \
    F(mg_small_arms_defer) \
    F(cover_frags)         \
    F(role_splits)         \
    F(advance_unfreeze)    \
    F(attack_persists)     \
    F(melee_prevented)     \
    F(trapped_hides)       \
    F(hit_dirt)
#define GOH_LIVE_INFANTRY(F) \
    F(smoke_veh)             \
    F(smoke_inf)             \
    F(smoke_med)             \
    F(smoke_off)             \
    F(smoke_rel)             \
    F(med_claim)             \
    F(med_fetch)             \
    F(med_smoke)             \
    F(med_rev)               \
    F(med_backoff)           \
    F(sup_trip)              \
    F(sup_refill)            \
    F(tact_ord)              \
    F(overflow_cover)        \
    F(dismount_spread)       \
    F(wedge_moves)           \
    F(obj_pursue)            \
    F(obj_cap)               \
    F(fast_swaps)            \
    F(missile_holds)         \
    F(squad_ammo)            \
    F(squad_bandages)        \
    F(scavenge)              \
    F(heal)                  \
    F(burn)                  \
    F(kit)                   \
    F(window)                \
    F(stance)                \
    F(reload)                \
    F(board)                 \
    F(eligible)              \
    F(allegiance_veto)       \
    F(towed_gun_suppress)    \
    F(move_nudge)            \
    F(move_resume)           \
    F(move_abort)            \
    F(crush_escapes)         \
    F(hazard_priorities)
#define GOH_LIVE_TANKS(F)  \
    F(tank_ctx)            \
    F(tank_exp)            \
    F(tank_disc)           \
    F(tank_rev)            \
    F(rep_full)            \
    F(rep_tt)              \
    F(rep_emit)            \
    F(rep_assign)          \
    F(rep_chain)           \
    F(rep_board)           \
    F(rep_pick)            \
    F(crew_total)          \
    F(crew_breed)          \
    F(crew_inf)            \
    F(crew_internal)       \
    F(crew_nontank)        \
    F(safe_doors)          \
    F(transport_unloads)   \
    F(tank_face)           \
    F(tank_face_retargets) \
    F(tank_face_timeouts)  \
    F(convoy_yield)        \
    F(convoy_pass)         \
    F(convoy_resume)
#define GOH_LIVE_NAME(n) #n,
inline constexpr const char* counter_names[] = {GOH_LIVE_COMBAT(GOH_LIVE_NAME) GOH_LIVE_INFANTRY(GOH_LIVE_NAME)
                                                    GOH_LIVE_TANKS(GOH_LIVE_NAME)};
#undef GOH_LIVE_NAME
using Counters = std::array<std::uint32_t, std::size(counter_names)>;
template <class C, class I, class T> Counters counters(const C& combat, const I& infantry, const T& tanks) {
#define GOH_LIVE_C(n) combat.n,
#define GOH_LIVE_I(n) infantry.n,
#define GOH_LIVE_T(n) tanks.n,
    return {GOH_LIVE_COMBAT(GOH_LIVE_C) GOH_LIVE_INFANTRY(GOH_LIVE_I) GOH_LIVE_TANKS(GOH_LIVE_T)};
#undef GOH_LIVE_C
#undef GOH_LIVE_I
#undef GOH_LIVE_T
}
#undef GOH_LIVE_COMBAT
#undef GOH_LIVE_INFANTRY
#undef GOH_LIVE_TANKS

// Only the serial host callback may feed this writer. Close after hooks drain. No engine calls.
class Writer {
    FILE* file_{};
    Config cfg_{};
    std::uint64_t flushed_{}, window_{}, stats_ms_{};
    std::size_t used_{};
    std::array<std::uint32_t, 256> sampled_{};
    bool sampled_stats_{};
    Counters previous_{};
    std::uint32_t previous_tick_{};

    bool emit(std::string_view kind, std::uint64_t now, std::uint32_t tick, const std::string& fields) {
        if (!file_) return false;
        const auto row = "{\"event\":" + quoted(kind) + ",\"ms\":" + std::to_string(now) +
                         ",\"sim_tick\":" + std::to_string(tick) + fields + "}\n";
        if (std::fwrite(row.data(), 1, row.size(), file_) != row.size()) {
            close();
            return false;
        }
        return flush_due(now);
    }

public:
    Writer() = default;
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    ~Writer() {
        close();
    }
    bool active() const {
        return file_ != nullptr;
    }
    void close() {
        if (file_) std::fclose(file_);
        file_ = nullptr;
    }
    bool flush(std::uint64_t now) {
        if (!file_) return false;
        if (std::fflush(file_) != 0) {
            close();
            return false;
        }
        flushed_ = now;
        return true;
    }
    bool flush_due(std::uint64_t now) {
        if (!file_) return false;
        if (now - flushed_ >= cfg_.flush_ms) return flush(now);
        return true;
    }
    // Takes ownership of stream even when the identity check fails.
    bool start(FILE* stream, Config cfg, std::string_view basename, std::string_view sha, std::uint64_t now,
               std::uint32_t tick, unsigned day_ms = 0) {
        close();
        file_ = stream;
        cfg_ = cfg;
        if (!file_ || !cfg.enabled || lower(basename) != executable || sha != fingerprint) {
            close();
            return false;
        }
        sampled_stats_ = false;
        used_ = 0;
        window_ = flushed_ = now;
        if (!emit("session_start", now, tick,
                  ",\"exe\":" + quoted(executable) + ",\"sha256\":" + quoted(sha) +
                      ",\"build\":26734796,\"local_day_ms\":" + std::to_string(day_ms)))
            return false;
        if (std::fflush(file_) != 0) {
            close();
            return false;
        }
        return true;
    }
    bool reloaded(const Config& cfg, std::uint64_t now, std::uint32_t tick) {
        // The Windows owner reopens on a filename change.
        cfg_ = cfg;
        bool ok = emit("config_reloaded", now, tick,
                       ",\"enabled\":" + std::string(cfg.enabled ? "true" : "false") + ",\"snapshot_interval_ms\":" +
                           std::to_string(cfg.snapshot_interval_ms) + ",\"flush_ms\":" + std::to_string(cfg.flush_ms));
        if (!cfg.enabled) close();
        return ok;
    }
    bool reset_pending(std::uint32_t tick) const {
        return sampled_stats_ && tick < previous_tick_ && previous_tick_ - tick > 500;
    }
    bool stats_due(std::uint64_t now) const {
        return active() && (!sampled_stats_ || now - stats_ms_ >= cfg_.snapshot_interval_ms);
    }
    bool stats_due(std::uint64_t now, std::uint32_t tick) const {
        return active() && (!sampled_stats_ || reset_pending(tick) || now - stats_ms_ >= cfg_.snapshot_interval_ms);
    }
    void stats(std::uint64_t now, std::uint32_t tick, const Counters& current) {
        if (!stats_due(now, tick)) return;
        if (reset_pending(tick)) {
            emit("scene_reset", now, tick, ",\"previous_tick\":" + std::to_string(previous_tick_));
            used_ = 0;
            window_ = now;
        }
        std::string values;
        if (sampled_stats_ && tick >= previous_tick_) {
            for (std::size_t i = 0; i < current.size(); ++i) {
                const auto delta = current[i] - previous_[i];
                if (delta) {
                    if (!values.empty()) values += ',';
                    values += quoted(counter_names[i]) + ":" + std::to_string(delta);
                }
            }
        }
        if (!values.empty())
            emit("stats_delta", now, tick,
                 ",\"since_tick\":" + std::to_string(previous_tick_) + ",\"counters\":{" + values + "}");
        previous_ = current;
        previous_tick_ = tick;
        stats_ms_ = now;
        sampled_stats_ = true;
        flush_due(now);
    }
    // Supply only visible, active actors and a stable entity ID. Query expensive fields in the lambda.
    template <class Read> bool sample(std::uint64_t now, std::uint32_t actor, Read read) {
        if (!active()) return false;
        if (now - window_ >= cfg_.snapshot_interval_ms) {
            window_ = now;
            used_ = 0;
        }
        if (used_ == sampled_.size() ||
            std::find(sampled_.begin(), sampled_.begin() + used_, actor) != sampled_.begin() + used_)
            return false;
        sampled_[used_++] = actor;
        const ActorSnapshot a = read();
        return emit("actor_snapshot", now, a.sim_tick,
                    ",\"actor\":" + std::to_string(actor) + ",\"mid\":" + std::to_string(a.mid) +
                        ",\"player\":" + std::to_string(a.player) + ",\"is_human\":" + (a.is_human ? "true" : "false") +
                        ",\"control\":" + std::to_string(a.control) + ",\"move_mode\":" + std::to_string(a.move_mode) +
                        ",\"fire_mode\":" + std::to_string(a.fire_mode) + ",\"pos_x\":" + number(a.pos_x) +
                        ",\"pos_y\":" + number(a.pos_y) + ",\"hp_ratio\":" + number(a.hp_ratio) +
                        ",\"morale\":" + number(a.morale) + ",\"behavior\":" + quoted(a.behavior) +
                        ",\"heading_deg\":" + number(a.heading_deg) + ",\"address\":" + std::to_string(a.address) +
                        ",\"cannon_target\":" + std::to_string(a.cannon_target) + ",\"mg_target\":" +
                        std::to_string(a.mg_target) + ",\"grenade_land_m\":" + number(a.grenade_land_m) +
                        ",\"ticks_per_second\":" + number(a.ticks_per_second) +
                        ",\"stats_tick\":" + (sampled_stats_ ? std::to_string(previous_tick_) : "null"));
    }
    bool module_event(std::uint64_t now, std::uint32_t tick, std::string_view module, std::string_view action,
                      std::uint32_t actor_mid, std::uint32_t target_mid = 0,
                      double value = std::numeric_limits<double>::quiet_NaN()) {
        return active() && emit("module_event", now, tick,
                                ",\"module\":" + quoted(module) + ",\"action\":" + quoted(action) +
                                    ",\"mid\":" + std::to_string(actor_mid) +
                                    ",\"target_mid\":" + std::to_string(target_mid) + ",\"value\":" + number(value));
    }
};

#ifdef _WIN32
class Files {
    std::wstring directory_, output_;
    ReloadClock clock_;

public:
    bool attach(HMODULE dll) {
        wchar_t path[4096]{};
        const auto n = dll ? GetModuleFileNameW(dll, path, static_cast<DWORD>(std::size(path))) : 0;
        if (!n || n >= std::size(path)) return false;
        std::wstring full(path, n);
        const auto slash = full.find_last_of(L"\\/");
        if (slash == std::wstring::npos) return false;
        directory_ = full.substr(0, slash + 1);
        return true;
    }
    // Open without write-sharing. Size, timestamp and bytes come from the same file generation.
    template <class Apply> bool poll(std::uint64_t now, Apply apply) {
        if (directory_.empty() || !clock_.due(now)) return false;
        HANDLE h = CreateFileW((directory_ + L"goh_ai.ini").c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        struct FileGuard {
            HANDLE file;
            ~FileGuard() {
                CloseHandle(file);
            }
        } guard{h};
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(h, &info) || info.nFileSizeHigh || info.nFileSizeLow > 262144u) return false;
        const auto stamp = (static_cast<std::uint64_t>(info.ftLastWriteTime.dwHighDateTime) << 32) |
                           info.ftLastWriteTime.dwLowDateTime;
        if (!clock_.changed(stamp, info.nFileSizeLow)) return false;
        std::string bytes(info.nFileSizeLow, '\0');
        DWORD n{};
        if (!ReadFile(h, bytes.data(), info.nFileSizeLow, &n, nullptr) || n != info.nFileSizeLow ||
            bytes.find('\0') != std::string::npos)
            return false;
        if (!apply(std::string_view(bytes), parse_ini(bytes))) return false;
        clock_.commit(stamp, info.nFileSizeLow);
        return true;
    }
    bool configure(Writer& writer, const Config& cfg, std::string_view basename, std::string_view verified_sha,
                   std::uint64_t now, std::uint32_t tick) {
        if (!cfg.enabled) {
            if (writer.active()) writer.reloaded(cfg, now, tick);
            return true;
        }
        if (directory_.empty() || !jsonl_name(cfg.filename) || lower(basename) != executable ||
            verified_sha != fingerprint)
            return false;
        const std::string name(cfg.filename);
        const auto path = directory_ + std::wstring(name.begin(), name.end());
        if (!writer.active() || path != output_) {
            FILE* stream = _wfopen(path.c_str(), L"ab");
            SYSTEMTIME tm{};
            GetLocalTime(&tm);
            const unsigned day_ms = ((tm.wHour * 60u + tm.wMinute) * 60u + tm.wSecond) * 1000u + tm.wMilliseconds;
            if (!stream || !writer.start(stream, cfg, basename, verified_sha, now, tick, day_ms)) return false;
            output_ = path;
        }
        return writer.reloaded(cfg, now, tick);
    }
};
#endif
} // namespace goh::live
