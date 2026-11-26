#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace goh::engine_fixes::policy {
inline bool suppress_coax_slew(float threat_bearing_rad, float candidate_bearing_rad, bool casemate, bool armor,
                               bool urgent_at, float metres, bool main_or_coax) {
    constexpr float degree = 0.017453292519943295f;
    return main_or_coax && !armor && !urgent_at && std::isfinite(metres) && metres > 30.f &&
           std::abs(std::remainder(candidate_bearing_rad - threat_bearing_rad, 360.f * degree)) >
               (casemate ? 12.f : 20.f) * degree;
}
struct Point {
    float x{}, y{};
};
struct Circle {
    Point center{};
    float radius{};
};
inline float squared(Point a, Point b) {
    const float x = a.x - b.x, y = a.y - b.y;
    return x * x + y * y;
}
template <class Passable>
bool choose_burn_escape(Point self, const Circle* circles, std::size_t n, const Point* enemy, float length,
                        Passable passable, Point& out) {
    if (!n || n > 64 || !(length > 0) || !std::isfinite(length)) return false;
    float nearest = std::numeric_limits<float>::infinity(), largest = 0, bearing = 0;
    bool threatened = false;
    for (std::size_t i = 0; i < n; ++i) {
        const auto& c = circles[i];
        const float d = squared(self, c.center);
        if (!std::isfinite(d) || !std::isfinite(c.radius) || c.radius < 0) return false;
        threatened |= d <= c.radius * c.radius;
        largest = std::max(largest, c.radius);
        if (d < nearest) {
            nearest = d;
            bearing = std::atan2(self.y - c.center.y, self.x - c.center.x);
        }
    }
    if (!threatened) return false;
    for (int ring = 1; ring <= 11; ++ring) {
        const float r = ring == 11 ? largest + 1.25f * length : ring * .75f * length;
        bool found = false;
        float exposure = -1;
        for (int j = 0; j < 12; ++j) {
            const float angle = bearing + j * .5235987756f;
            Point pt{self.x + r * std::cos(angle), self.y + r * std::sin(angle)};
            const bool clear = std::all_of(circles, circles + n, [&](const Circle& c) {
                const float margin = c.radius + .35f * length;
                return squared(pt, c.center) > margin * margin;
            });
            const float score = enemy ? squared(pt, *enemy) : 0.f;
            if (clear && (!found || score > exposure) && passable(pt)) {
                out = pt;
                found = true;
                exposure = score;
            }
        }
        if (found) return true;
    }
    return false;
}
inline bool upward(int current, int requested) {
    return (current == 1 && (requested == 0 || requested == 2)) || (current == 2 && requested == 0);
}
enum class Sight {
    unknown,
    blocked,
    clear
};
template <class Probe> int hold_stance(int current, int requested, float metres, Probe probe) {
    // -1 asks for the lowest usable stance. A concrete request may only authorize itself.
    if (!(metres >= 0) || !std::isfinite(metres) || (current != 1 && current != 2)) return -1;
    if (metres < 7.5f && (requested == -1 || requested == 0)) return 0;
    if (requested != -1 && !upward(current, requested)) return -1;
    if (probe(current) != Sight::blocked) return -1;
    for (int candidate : {2, 0})
        if ((requested == -1 || requested == candidate) && upward(current, candidate) &&
            probe(candidate) == Sight::clear)
            return candidate;
    return -1;
}
inline bool unfinished(int timer, int zero_time, unsigned state) {
    return timer != zero_time || state != 0;
}
inline bool board_replan(float movement2, float threshold, float relative_limit) {
    return movement2 > threshold * threshold && movement2 > relative_limit;
}
inline bool low_magazine(int current, int capacity, float ratio) {
    return capacity >= 4 && current > 0 && current <= capacity * ratio;
}
inline bool target_flags(unsigned relation, unsigned flags, bool entity, bool recent_contact) {
    if (relation & 1) return (relation & 2) != 0;
    return entity && (flags & (1u << 28)) && ((relation & (1u << 10)) || recent_contact);
}
} // namespace goh::engine_fixes::policy
