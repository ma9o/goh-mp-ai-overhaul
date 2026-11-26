#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace goh::tank_facing::policy {

constexpr float pi = 3.14159265358979323846f;
constexpr float radians(float degrees) noexcept {
    return degrees * (pi / 180.f);
}

enum class ThreatTier : std::uint8_t {
    none,
    close_infantry,
    light_at,
    heavy_at
};
struct ThreatSample {
    float bearing_rad{}, dist_m{};
    ThreatTier tier{};
    bool visible{}, attacking_or_threatening{}, is_main_target{};
};
struct FacingConfig {
    float min_angle_rad{radians(24)}, min_improvement{0.25f};
    bool hold_fire_mode{}, recently_hit{};
};
struct FacingDecision {
    bool should_rotate{};
    float desired_yaw_rad{}, angle_error_rad{}, current_cost{}, best_cost{}, improvement_ratio{};
};

inline float wrap_angle(float rad) noexcept {
    const float angle = std::remainder(rad, 2.f * pi);
    return angle <= -pi ? pi : angle;
}
inline float threat_weight(const ThreatSample& t) noexcept {
    if (!std::isfinite(t.bearing_rad) || !std::isfinite(t.dist_m) || t.dist_m < 0.f) return 0.f;
    float weight{};
    switch (t.tier) {
        case ThreatTier::heavy_at: weight = 4.f; break;
        case ThreatTier::light_at: weight = 2.5f; break;
        case ThreatTier::close_infantry:
            if (t.dist_m > 18.f || !t.attacking_or_threatening) return 0.f;
            weight = 1.f;
            break;
        default: return 0.f;
    }
    return weight * std::clamp(160.f / std::max(t.dist_m, 20.f), 0.5f, 4.f) * (t.visible ? 1.5f : 0.85f) *
           (t.attacking_or_threatening ? 1.6f : 1.f) * (t.is_main_target ? 1.85f : 1.f);
}
inline float armor_exposure(float abs_delta_rad) noexcept {
    if (abs_delta_rad <= radians(35)) return 0.15f + 0.35f * (abs_delta_rad / radians(35));
    if (abs_delta_rad <= radians(125)) return 1.f + 0.60f * ((abs_delta_rad - radians(35)) / radians(90));
    return 2.4f;
}
inline bool stealth_allows(const ThreatSample& t, const FacingConfig& cfg) noexcept {
    return !cfg.hold_fire_mode || cfg.recently_hit || t.attacking_or_threatening;
}
inline FacingDecision evaluate_facing(float current, const ThreatSample* threats, std::size_t count,
                                      const FacingConfig& cfg = {}) noexcept {
    FacingDecision d{};
    if (!std::isfinite(current) || !threats || !count) return d;
    current = wrap_angle(current);
    auto exposure = [&](float heading) {
        float cost = 0.f;
        for (std::size_t i = 0; i < count; ++i) {
            const auto& t = threats[i];
            const float weight = threat_weight(t);
            if (weight > 0.f && stealth_allows(t, cfg))
                cost += weight * armor_exposure(std::fabs(wrap_angle(t.bearing_rad - heading)));
        }
        return cost;
    };
    d.current_cost = d.best_cost = exposure(current);
    d.desired_yaw_rad = current;
    if (d.current_cost <= 1e-5f) return d;
    float best_objective = d.current_cost;
    auto consider = [&](float heading) {
        const float error = std::fabs(wrap_angle(heading - current));
        const float cost = exposure(heading);
        const float objective = cost + 0.12f * error;
        if (objective < best_objective) {
            best_objective = objective;
            d.desired_yaw_rad = wrap_angle(heading);
            d.angle_error_rad = error;
            d.best_cost = cost;
        }
    };
    for (int i = 0; i < 36; ++i)
        consider(radians(static_cast<float>(10 * i)));
    for (std::size_t i = 0; i < count; ++i)
        if (stealth_allows(threats[i], cfg) && threat_weight(threats[i]) > 0.f) consider(threats[i].bearing_rad);
    d.improvement_ratio = (d.current_cost - d.best_cost) / d.current_cost;
    d.should_rotate = d.angle_error_rad >= std::clamp(cfg.min_angle_rad, radians(15), radians(60)) &&
                      d.improvement_ratio >= cfg.min_improvement;
    return d;
}

struct RotationGate {
    bool owns_order{}, backoff{}, cooldown{}, heavy_threat{};
    float active_target_yaw{}, seconds_since_issue{};
};
inline bool can_issue_rotation(const FacingDecision& d, const RotationGate& gate) noexcept {
    if (!d.should_rotate || gate.backoff) return false;
    if (gate.cooldown && (gate.seconds_since_issue < 2.f || !gate.heavy_threat || d.angle_error_rad < radians(50)))
        return false;
    return !gate.owns_order || std::fabs(wrap_angle(d.desired_yaw_rad - gate.active_target_yaw)) >= radians(28);
}

} // namespace goh::tank_facing::policy
