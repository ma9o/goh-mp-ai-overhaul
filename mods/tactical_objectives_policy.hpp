#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace goh::tactical_objectives::policy {
using Key = std::uintptr_t;
constexpr double never = -1e20;
struct XY {
    float x{}, y{};
    XY operator+(XY p) const {
        return {x + p.x, y + p.y};
    }
    XY operator-(XY p) const {
        return {x - p.x, y - p.y};
    }
    XY operator*(float k) const {
        return {x * k, y * k};
    }
};
inline bool finite(XY p) {
    return std::isfinite(p.x) && std::isfinite(p.y);
}
inline float distance(XY a, XY b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}
inline bool within_radius(XY a, XY b, float meters) {
    const XY delta = a - b;
    return delta.x * delta.x + delta.y * delta.y <= meters * meters;
}
enum class Kind : std::uint8_t {
    pursue,
    cap
};
inline bool user_move(unsigned modes, unsigned flags) {
    return (modes & 3u) == 1 && (flags & (1u << 28));
}
inline bool player_intent(unsigned modes, unsigned flags, bool selected) {
    return (modes & 3u) == 1 && (selected || user_move(modes, flags) || (modes & 0x3000u));
}
inline bool allows(unsigned modes, unsigned flags) {
    unsigned ctrl = modes & 3u, fire = (modes >> 10) & 3u;
    return ctrl <= 1 && !(modes & 0x3000u) && !(flags & 0x0010007fu) && !user_move(modes, flags) &&
           (ctrl != 0 || !(flags & (1u << 11))) && (fire == 0 || (fire == 2 && (flags & (1u << 18))));
}
struct Anchor {
    XY xy{};
    bool initialized{}, pending{};
    std::uint64_t revision{};
    void center(XY point) {
        xy = point;
        initialized = true;
        pending = false;
        ++revision;
    }
    void observe(XY point, bool intent, bool free) {
        if (!initialized) center(point);
        if (intent)
            pending = true;
        else if (free && pending)
            center(point);
    }
};
inline bool in_leash(Kind kind, XY target, XY self, XY center, XY anchor) {
    if (kind == Kind::cap)
        return within_radius(target, self, 35) && within_radius(target, center, 35) &&
               within_radius(target, anchor, 35);
    return distance(target, self) >= 12 && within_radius(target, self, 50) && within_radius(target, center, 50) &&
           within_radius(target, anchor, 55);
}
inline bool morale_ok(float own, float average, bool squad) {
    return std::isfinite(own) && own >= 55 && own <= 100 &&
           (!squad || (std::isfinite(average) && average >= 58 && average <= 100));
}
inline bool odds_ok(float enemy, float friendly, float retreat_ratio) {
    return std::isfinite(enemy) && std::isfinite(friendly) && std::isfinite(retreat_ratio) && enemy >= 0 &&
           friendly >= 0 && retreat_ratio >= 0 && (friendly == 0 || enemy <= friendly * retreat_ratio);
}
struct Observation {
    Key enemy{};
    XY xy{};
    double seen{};
    float power{};
    bool armor{}, threatens{}, visible{};
};
struct Danger {
    float power{};
    bool armor{};
};
inline bool manageable(Kind kind, Danger d, unsigned eligible, bool anti_armor) {
    return eligible && d.power <= 1.5f * (std::min)(eligible, 8u) && (!d.armor || (kind == Kind::pursue && anti_armor));
}
inline unsigned cap_strength(unsigned mobile, unsigned eligible) {
    return (std::min)(4u, mobile >= 2 ? mobile : eligible);
}
struct Contacts {
    std::array<Observation, 64> rows{};
    unsigned size{};
    bool incomplete{};
    double squad_threat{never};
    void add(Observation obs) {
        if (!finite(obs.xy) || !std::isfinite(obs.power)) {
            incomplete = true;
            return;
        }
        for (unsigned i = 0; i < size; ++i) {
            auto& prev = rows[i];
            if (prev.enemy && obs.enemy ? prev.enemy != obs.enemy : !within_radius(prev.xy, obs.xy, 3)) continue;
            bool armor = prev.armor || obs.armor, threat = prev.threatens || obs.threatens;
            float power = (std::max)(prev.power, obs.power);
            Key key = obs.enemy ? obs.enemy : prev.enemy;
            if (obs.seen > prev.seen || (obs.seen == prev.seen && obs.visible)) prev = obs;
            prev.enemy = key;
            prev.power = power;
            prev.armor = armor;
            prev.threatens = threat;
            return;
        }
        if (size < rows.size())
            rows[size++] = obs;
        else
            incomplete = true;
    }
    Danger danger(XY point, float radius, double now) const {
        Danger d;
        for (unsigned i = 0; i < size; ++i) {
            const auto& o = rows[i];
            if (now - o.seen > 12 || !within_radius(point, o.xy, radius)) continue;
            d.power += (std::max)(.5f, o.power);
            d.armor |= o.armor;
        }
        return d;
    }
    bool threatened(XY self, XY center, double now) const {
        if (now - squad_threat <= 12) return true;
        for (unsigned i = 0; i < size; ++i) {
            const auto& o = rows[i];
            if (o.threatens && now - o.seen <= 12 && (within_radius(self, o.xy, 50) || within_radius(center, o.xy, 50)))
                return true;
        }
        return false;
    }
    template <class Available>
    const Observation* pursue(XY self, XY center, XY anchor, unsigned eligible, bool anti_armor, double now,
                              Available available) const {
        const Observation* best = nullptr;
        float top = -std::numeric_limits<float>::infinity();
        if (incomplete) return nullptr;
        for (unsigned i = 0; i < size; ++i) {
            const auto& o = rows[i];
            double age = now - o.seen;
            if (age < 0 || age > 12 || !in_leash(Kind::pursue, o.xy, self, center, anchor) ||
                !manageable(Kind::pursue, danger(o.xy, 20, now), eligible, anti_armor) || !available(o))
                continue;
            float score =
                -distance(center, o.xy) - float(age) * 1.5f + (o.visible ? 4.f : 0.f) + (o.threatens ? 6.f : 0.f);
            if (score > top) {
                best = &o;
                top = score;
            }
        }
        return best;
    }
    const Observation* pursue(XY self, XY center, XY anchor, unsigned eligible, bool anti_armor, double now) const {
        return pursue(self, center, anchor, eligible, anti_armor, now, [](const Observation&) { return true; });
    }
};
inline bool cap_allowed(const Contacts& contacts, XY flag, XY self, XY center, XY anchor, unsigned eligible,
                        bool has_pursue, double now) {
    return !contacts.incomplete && !has_pursue && !contacts.threatened(self, center, now) &&
           in_leash(Kind::cap, flag, self, center, anchor) && !within_radius(self, flag, 6.5f) &&
           manageable(Kind::cap, contacts.danger(flag, 25, now), eligible, false);
}
// A normal move finishes within 3 m of its endpoint. Choose a perimeter point
// whose approach enters the capture zone before that arrival tolerance fires.
inline bool reaches_cap(XY self, XY goal, XY flag) {
    float left = distance(self, goal);
    return left > 3 && within_radius(goal + (self - goal) * (3.f / left), flag, 6.5f);
}
inline float standoff(float range) {
    return std::isfinite(range) && range > 0 ? std::clamp(.6f * range, 12.f, 20.f) : 15.f;
}
// All positions are in metres. Slot zero faces the approaching group.
inline XY ring(XY target, XY center, float radius, float degrees) {
    XY axis = center - target;
    float n = distance(center, target);
    axis = n > 0 ? axis * (1.f / n) : XY{1, 0};
    float radians = degrees * .01745329252f, c = std::cos(radians), s = std::sin(radians);
    return target + XY{axis.x * c - axis.y * s, axis.x * s + axis.y * c} * radius;
}
template <class Accept>
bool destination(Kind kind, XY target, XY center, XY anchor, float range, unsigned slot, Accept accept) {
    if (kind == Kind::cap) {
        for (float radius : {4.5f, 3.f, 5.5f})
            for (unsigned i = 0; i < 6; ++i) {
                XY point = ring(target, center, radius, 60.f * ((slot + i) % 6));
                if (within_radius(point, anchor, 41) && accept(point)) return true;
            }
    } else {
        constexpr float angles[]{0, 18, -18, 36, -36, 54, -54};
        for (float delta : {0.f, -2.f, 2.f})
            for (float turn : {0.f, 20.f, -20.f, 40.f, -40.f}) {
                XY point =
                    ring(target, center, std::clamp(standoff(range) + delta, 12.f, 20.f), angles[slot % 7] + turn);
                if (within_radius(point, anchor, 55) && accept(point)) return true;
            }
    }
    return false;
}
struct Idle {
    double since{never}, next{};
    void interrupt() {
        since = never;
    }
    bool ready(double now, bool broken_los) {
        if (since == never) since = now;
        if (now - since < (broken_los ? 2. : 1.5) || now < next) return false;
        next = now + 1.5;
        return true;
    }
};
enum class Result : std::uint8_t {
    moving,
    arrived,
    failed
};
constexpr double cooldown(Result r) {
    return r == Result::failed ? 12. : 8.;
}
struct Progress {
    double start{}, last{};
    float remaining{};
    Result update(double now, float distance_left, bool at_goal, bool order_alive) {
        if (at_goal) return Result::arrived;
        if (now - start > 18) return Result::failed;
        if (!order_alive) return Result::arrived;
        if (remaining - distance_left >= 1.5f) {
            last = now;
            remaining = distance_left;
        }
        return now - last >= 6 ? Result::failed : Result::moving;
    }
};
struct Epoch {
    Key scene{};
    std::uint32_t tick{};
    bool changed(Key next, std::uint32_t time) const {
        return next != scene || time < tick;
    }
};
} // namespace goh::tactical_objectives::policy
