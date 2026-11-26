#pragma once

#include "movement_recovery_policy.hpp"

namespace goh::convoy_spacing {
using runtime::XY;
namespace geom = movement_geometry;

struct Leader {
    std::uint32_t id{};
    XY pos{};
    float along{}, speed{}, heading_dot{}, along_speed{};
    bool present{};
};
inline std::optional<float> blocking_distance(XY self, XY fwd, XY other, float length, float gap) {
    const auto d = geom::delta(other, self);
    const float along = geom::dot(fwd, d) / length;
    const float lateral = (fwd.x * d.y - fwd.y * d.x) / length;
    if (!(along > .5f && along < gap && std::fabs(lateral) < 3.6f && geom::norm(d) <= (gap + 6.f) * length)) return {};
    return along;
}
enum class Action {
    none,
    yield,
    try_pass
};
struct Following {
    std::optional<XY> dest;
    unsigned passes{};
    double cooldown_until{};
    std::optional<std::uint32_t> leader;
    std::optional<double> stopped_since, blocked_since;
    void reset_observation() {
        leader.reset();
        stopped_since.reset();
        blocked_since.reset();
    }
    void destination(XY goal, float length) {
        if (!dest || geom::distance(goal, *dest) > 1.5f * length) {
            dest = goal;
            passes = 0;
            reset_observation();
        }
    }
    Action sample(const Leader& ahead, float own_speed, float forward_speed, float gap, double now) {
        if (!ahead.present || leader != ahead.id) reset_observation();
        if (!ahead.present) return Action::none;
        leader = ahead.id;
        if (ahead.speed <= .35f) {
            if (!stopped_since) stopped_since = now;
        } else
            stopped_since.reset();
        if (own_speed < .35f) {
            if (!blocked_since) blocked_since = now;
        } else
            blocked_since.reset();
        if (now < cooldown_until) return Action::none;
        if (ahead.speed > .4f && ahead.heading_dot > .5f) {
            return ahead.along < (std::max)(4.5f, gap * .65f) && forward_speed > ahead.along_speed ? Action::yield
                                                                                                   : Action::none;
        }
        if ((stopped_since && now >= *stopped_since + 1.5) || (blocked_since && now >= *blocked_since + 2.))
            return passes < 2 ? Action::try_pass : Action::yield;
        return Action::none;
    }
    void issued(bool passing, double now) {
        if (passing) ++passes;
        cooldown_until = now + 4.;
    }
};

template <class Probe> std::optional<XY> pick_pass_point(XY leader, XY forward, float length, Probe clear) {
    for (float lateral : {5.5f, -5.5f, 7.5f, -7.5f}) {
        const XY p = geom::offset(leader, forward, 4.f, lateral, length);
        if (clear(p)) return p;
    }
    return {};
}
} // namespace goh::convoy_spacing
