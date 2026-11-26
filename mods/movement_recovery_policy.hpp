#pragma once

#include "game_runtime.hpp"
#include <optional>

namespace goh::movement_geometry {
using runtime::XY;
inline XY delta(XY a, XY b) {
    return {a.x - b.x, a.y - b.y};
}
inline float norm(XY a) {
    return std::hypot(a.x, a.y);
}
inline float dot(XY a, XY b) {
    return a.x * b.x + a.y * b.y;
}
inline float distance(XY a, XY b) {
    return norm(delta(a, b));
}
inline XY unit(XY a) {
    const auto n = norm(a);
    return n > 1e-5f && std::isfinite(n) ? XY{a.x / n, a.y / n} : XY{};
}
inline XY offset(XY pt, XY fwd, float along, float lateral, float length) {
    return {pt.x + length * (along * fwd.x - lateral * fwd.y), pt.y + length * (along * fwd.y + lateral * fwd.x)};
}
} // namespace goh::movement_geometry

namespace goh::movement_recovery {
using runtime::XY;
namespace geom = movement_geometry;

struct StallDetector {
    XY anchor_pos{}, dest{};
    std::optional<XY> previous_nudge;
    double stall_since{}, cooldown_until{};
    unsigned attempts{};
    bool has_dest{}, anchored{};

    void interrupt() {
        anchored = false;
    }
    bool stalled(XY self, XY goal, float length, double now, float stall_s) {
        if (!has_dest || geom::distance(dest, goal) > 1.5f * length) {
            anchored = false;
            attempts = 0;
            previous_nudge.reset();
            dest = goal;
            has_dest = true;
        }
        const auto displacement = geom::distance(self, anchor_pos);
        if (!anchored || displacement >= .6f * length) {
            anchor_pos = self;
            anchored = true;
            stall_since = now;
            return false;
        }
        return attempts < 2 && now >= cooldown_until && now >= stall_since + stall_s && displacement < .45f * length;
    }
    void tried(double now, std::optional<XY> nudge) {
        ++attempts;
        if (nudge) previous_nudge = nudge;
        if (attempts >= 2) cooldown_until = now + 20.;
        interrupt();
    }
};

enum class Passage {
    blocked,
    reachable,
    onward_clear
};

// Probe grades the first leg and a short section of the onward route. Equal grades
// keep the shorter lateral escape ahead of a diagonal or back-out.
template <class Probe>
std::optional<XY> pick_nudge_point(XY self, XY dest, float length, Probe probe, std::optional<XY> tried = {}) {
    const auto fwd = geom::unit(geom::delta(dest, self));
    if (!runtime::finite(self) || !(length > 0) || geom::norm(fwd) < .5f) return {};
    std::optional<XY> fallback;
    for (XY metres : {XY{0, 2}, XY{0, 3.5f}, XY{2, 2.5f}, XY{0, 5}, XY{-2, 1.5f}, XY{-3.5f, 1.5f}}) {
        for (float side : {1.f, -1.f}) {
            auto point = geom::offset(self, fwd, metres.x, side * metres.y, length);
            if (tried && geom::distance(point, *tried) < 1.25f * length) continue;
            const auto grade = probe(point);
            if (grade == Passage::onward_clear) return point;
            if (grade == Passage::reachable && !fallback) fallback = point;
        }
    }
    return fallback;
}
} // namespace goh::movement_recovery
