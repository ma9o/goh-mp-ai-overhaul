#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <utility>

namespace goh::penetration_at::policy {
// Values describe GEM inventory definitions and tagged eTarget volumes.
enum class Shell {
    unavailable,
    standard,
    cumulative,
    subcaliber,
    explosive
};
enum class Part {
    other,
    body,
    turret,
    engine,
    ammo,
    gun,
    mantlet,
    track
};

inline Part part(std::string_view name) {
    for (const auto& e : {std::pair{"mantlet", Part::mantlet},
                          {"gun", Part::gun},
                          {"track", Part::track},
                          {"engine", Part::engine},
                          {"ammo", Part::ammo},
                          {"turret", Part::turret},
                          {"aux_turret", Part::turret},
                          {"body", Part::body}})
        if (name.substr(0, std::string_view(e.first).size()) == e.first) return e.second;
    return Part::other;
}
inline bool lethal(Part p) {
    return p == Part::body || p == Part::turret || p == Part::engine || p == Part::ammo;
}
inline bool permitted_volume(std::string_view requested, bool exact, bool forbidden, bool broken, Part p) {
    return !broken && !forbidden && (requested.empty() || requested == "body" ? p != Part::other : exact);
}
struct Estimate {
    float pen{}, armor{}, cosine{};
    bool valid() const {
        return std::isfinite(pen) && pen >= 0 && std::isfinite(armor) && armor >= 0 && std::isfinite(cosine) &&
               cosine > 0 && cosine <= 1;
    }
    float ratio() const {
        return pen / (std::max)(1.f, armor);
    }
    bool clean() const {
        return valid() && pen > 0 && (pen - armor >= 5 || ratio() >= 1.05f);
    }
    bool strong() const {
        return clean() && (pen - armor >= 10 || ratio() >= 1.10f);
    }
};
inline float volume_score(Part p, Estimate e, bool previous) {
    if (!e.valid()) return -1;
    const float certainty = 5 * (std::min)(e.ratio(), 2.f) + 5 * e.cosine - (e.cosine < .5f ? 6.f : 0.f);
    if (lethal(p) && e.clean())
        return 90 + certainty + ((p == Part::body || p == Part::turret) ? 3 : 0) + (previous ? 2 : 0);
    if (p == Part::gun || p == Part::mantlet || p == Part::track)
        return 50 + certainty + (p == Part::track ? 0 : 2) + (previous && e.clean() ? 2 : 0);
    return 15 + certainty;
}
struct Outcome {
    bool seen{}, clean{}, strong{}, component{};
    float ratio{}, penetration{};
    void observe(Part part, Estimate e) {
        if (!e.valid()) return;
        if (!lethal(part)) {
            component |= part != Part::other;
            return;
        }
        seen = true;
        clean |= e.clean();
        strong |= e.strong();
        ratio = (std::max)(ratio, e.ratio());
        penetration = (std::max)(penetration, e.pen);
    }
    void merge(const Outcome& o) {
        seen |= o.seen;
        clean |= o.clean;
        strong |= o.strong;
        component |= o.component;
        ratio = (std::max)(ratio, o.ratio);
        penetration = (std::max)(penetration, o.penetration);
    }
};
enum ShellPriority {
    no_result,
    component_he,
    borderline_penetration,
    subcaliber_penetration,
    standard_penetration,
    cumulative_penetration,
    strong_standard,
    open_vehicle,
    chambered_round
};
inline ShellPriority shell_priority(Shell kind, const Outcome& o, bool chambered, bool open_top) {
    if (kind == Shell::unavailable || (!o.seen && !o.component)) return no_result;
    if (chambered && o.clean) return chambered_round;
    if (kind == Shell::explosive) return open_top ? open_vehicle : component_he;
    if (!o.clean) return o.seen && o.ratio >= 1.f ? borderline_penetration : no_result;
    if (open_top && kind == Shell::cumulative) return open_vehicle;
    if (kind == Shell::standard) return o.strong ? strong_standard : standard_penetration;
    return kind == Shell::cumulative ? cumulative_penetration : subcaliber_penetration;
}
inline bool ready_soon(bool ready, bool timed, float seconds_remaining) {
    return ready || (timed && std::isfinite(seconds_remaining) && seconds_remaining < 1.5f);
}
inline bool actively_reloading(bool flags_reloading, bool timed, float seconds_remaining) {
    return flags_reloading ||
           (timed && std::isfinite(seconds_remaining) && seconds_remaining > 0.f && seconds_remaining <= 20.f);
}
inline bool subcaliber_improves(float proposed, float standard, bool chambered) {
    return chambered || (std::isfinite(proposed) && proposed > standard);
}
inline float killability(const Outcome& o, bool complete, bool alternative) {
    if (o.clean) return .35f;
    if (complete && alternative && o.seen && o.ratio < .85f) return -.35f;
    return (o.seen && o.ratio >= .85f) || o.component ? .10f : 0.f;
}
struct Infantry {
    bool launcher{}, rifle{}, grenade{}, known{};
    bool carries_at() const {
        return launcher || rifle || grenade;
    }
    bool immediate(float metres) const {
        return (launcher && metres <= 75) || (grenade && metres <= 28);
    }
};
inline float danger(float native, bool defanged, Infantry infantry, float metres) {
    return (defanged ? .35f * native : native) + (infantry.immediate(metres) ? 1.5f : 0.f);
}
inline bool stray(bool solitary, bool main_without_mg, Infantry infantry, float metres) {
    return main_without_mg && solitary && infantry.known && !infantry.carries_at() && metres > 40;
}
struct AimSettleState {
    float last_target_yaw{};
    std::uint32_t acquired_tick{}, aligned_since_tick{};
    bool seen{}, large_traverse{}, aligned{};
};
inline bool slow_cannon(float calibre, int capacity) {
    return std::isfinite(calibre) && calibre >= 37.f && capacity >= 0 && capacity <= 2;
}
inline bool should_hold_for_settle(AimSettleState& state, bool new_target, float target_yaw, float muzzle_yaw,
                                   int aimer_state, bool quiet, float metres, float calibre, std::uint32_t now,
                                   float ticks_per_second, float max_seconds) {
    constexpr float degree = 0.017453292519943295f;
    const auto delta = [](float a, float b) { return std::abs(std::remainder(a - b, 6.28318530718f)); };
    const bool has_muzzle = std::isfinite(muzzle_yaw);
    if (!std::isfinite(target_yaw) || !std::isfinite(metres)) {
        state = {};
        return false;
    }
    if (new_target || !state.seen) {
        state.large_traverse = (has_muzzle && delta(target_yaw, muzzle_yaw) >= 18.f * degree) ||
                               (state.seen && delta(target_yaw, state.last_target_yaw) >= 18.f * degree);
        state.acquired_tick = now;
        state.aligned = false;
    }
    state.seen = true;
    state.last_target_yaw = target_yaw;
    if (!quiet || metres <= 35.f || now - state.acquired_tick >= max_seconds * ticks_per_second)
        state.large_traverse = false;
    if (!state.large_traverse) return false;
    if ((aimer_state != 3 && aimer_state != 4) || (has_muzzle && delta(target_yaw, muzzle_yaw) > 2.5f * degree)) {
        state.aligned = false;
        return true;
    }
    if (!state.aligned) state.aligned_since_tick = now;
    state.aligned = true;
    if (now - state.aligned_since_tick < (calibre >= 75.f ? .50f : .35f) * ticks_per_second) return true;
    state.large_traverse = false;
    return false;
}
inline int fire_option(int mode, int option, bool quiet, float metres, bool stray_infantry, bool armored,
                       const Outcome& all, bool complete) {
    if (mode != 2 || option != 3 || !quiet || !std::isfinite(metres)) return option;
    if (stray_infantry && metres > 40) return 0;
    return armored && metres > 35 && complete && all.seen && !all.clean && all.ratio < .85f ? 0 : option;
}
} // namespace goh::penetration_at::policy
