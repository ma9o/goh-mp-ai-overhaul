#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace goh::tactical_infantry::policy {
using Id = std::uintptr_t;
constexpr double never = -1.e20;
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
inline float dot(XY a, XY b) {
    return a.x * b.x + a.y * b.y;
}
inline float squared(XY a) {
    return dot(a, a);
}
inline XY unit(XY p) {
    float n = std::hypot(p.x, p.y);
    return n > 0 && std::isfinite(n) ? p * (1.f / n) : XY{};
}
inline XY left(XY p) {
    return {-p.y, p.x};
}
inline XY rotate(XY p, float degrees) {
    constexpr float kDegToRad = 0.017453292519943295f;
    const float r = degrees * kDegToRad;
    const float c = std::cos(r), s = std::sin(r);
    return {p.x * c - p.y * s, p.x * s + p.y * c};
}

enum Posture {
    flee,
    fallback,
    hold,
    advance,
    assault
};
struct Balance {
    float a{}, b{}, morale{}, advance_ratio{}, retreat_ratio{};
    bool no_advance{}, no_retreat{};
    bool valid() const {
        return std::isfinite(a) && std::isfinite(b) && std::isfinite(morale) && std::isfinite(advance_ratio) &&
               std::isfinite(retreat_ratio) && a >= 0 && b >= 0 && advance_ratio >= 0 && retreat_ratio >= 0 &&
               morale >= 0 && morale <= 100;
    }
};
struct Soldier {
    Posture posture{hold};
    bool initialized{}, covered{};
    double changed{never}, entered{never}, left_cover{never}, moved{never}, fire_until{never}, detour_until{never},
        next_search{never};
    void observe_cover(bool present, double now) {
        if (present && !covered) entered = now;
        if (!present && covered) left_cover = now;
        covered = present;
    }
};
inline Posture decide(Soldier& s, const Balance& b, Posture original, double now) {
    const float k = s.covered ? 1.5f : (now - s.left_cover < 4 ? 1.35f : 1.f);
    const float tr = b.b * b.retreat_ratio * k, ta = b.a * b.advance_ratio * k;
    const auto old = s.initialized ? s.posture : original;
    const bool rally = old == flee && b.a > b.b * b.retreat_ratio && b.morale >= 50;
    Posture next = hold;
    if (!b.no_retreat && (b.a > 1.5f * tr || b.morale < 30))
        next = flee;
    else if (rally)
        next = s.covered || b.no_retreat ? hold : fallback;
    else if (!b.no_retreat && (b.a > 1.15f * tr || b.morale < 48))
        next = fallback;
    else if (old <= fallback && !b.no_retreat && !(b.a < .85f * tr && b.morale >= 55))
        next = old;
    else if (!b.no_advance && b.morale > 68 && b.b > 1.15f * ta && !(old <= fallback && b.a > b.b * b.retreat_ratio))
        next = b.b > 2.2f * ta ? assault : advance;
    const bool emergency = next == flee && !b.no_retreat && (b.morale < 30 || b.a > 2.f * tr);
    if (s.initialized && now - s.changed < 5 && !emergency && !(old <= fallback && b.no_retreat) &&
        !(old >= advance && b.no_advance))
        next = old;
    if (!s.initialized || old != next) s.changed = now;
    s.initialized = true;
    return s.posture = next;
}
inline bool active_user_move(unsigned modes, unsigned flags) {
    return (modes & 3u) == 1 && (flags & (1u << 28));
}
inline bool allows(unsigned modes, unsigned flags, bool selected = false, bool protected_order = false,
                   bool support_busy = false) {
    const auto fire = (modes >> 10) & 3u;
    return !selected && !protected_order && !support_busy && (modes & 3u) <= 1 && (modes & 0x3000u) == 0 &&
           !(flags & 0x0010007fu) && !active_user_move(modes, flags) &&
           (fire == 0 || (fire == 2 && (flags & (1u << 18))));
}
enum class Role {
    heavy,
    assault,
    rifle
};
inline Role classify(bool heavy, bool medic, bool smg, bool charge) {
    if (heavy || medic) return Role::heavy;
    return smg || charge ? Role::assault : Role::rifle;
}
enum class Mode {
    hold,
    bound,
    left,
    right,
    fallback
};
inline bool advancing(Mode m) {
    return m == Mode::bound || m == Mode::left || m == Mode::right;
}
inline Posture sync_squad_posture(Posture own, Mode mode, bool moving_element, bool lagging, bool no_advance,
                                  bool no_retreat) {
    if (mode == Mode::fallback) return no_retreat ? own : (std::min)(own, fallback);
    if (advancing(mode)) return (moving_element || lagging) && !no_advance ? (std::max)(own, advance) : hold;
    return lagging && !no_advance ? advance : hold;
}
struct Team {
    Mode mode{Mode::hold};
    unsigned phase{}, cycle{};
    double committed{never}, phase_at{never}, arrived_at{never};
    void update(double now, Mode desired, float l, float r, bool arrived) {
        if (desired == Mode::left || desired == Mode::right) {
            if (mode == Mode::left) l += 6;
            if (mode == Mode::right) r += 6;
            desired = l >= r ? Mode::left : Mode::right;
        }
        if (committed == never || (desired != mode && now - committed >= 6)) {
            mode = desired;
            committed = phase_at = now;
            arrived_at = never;
            phase = cycle = 0;
        }
        if (!advancing(mode)) return;
        if (!arrived)
            arrived_at = never;
        else if (arrived_at == never)
            arrived_at = now;
        if (now - phase_at >= 7 || (arrived && now - arrived_at >= 3.5)) {
            phase ^= 1;
            if (!phase) ++cycle;
            phase_at = now;
            arrived_at = never;
        }
    }
};
struct Member {
    Id actor{};
    unsigned id{};
    Role role{};
    XY position{};
};
using Roster = std::array<Member, 64>;
using Elements = std::array<bool, 64>;
inline Elements bounders(const Roster& roster, std::size_t count, unsigned phase, unsigned cycle = 0) {
    Elements selected{};
    count = (std::min)(count, roster.size());
    std::array<std::size_t, 64> mobile{};
    std::size_t size = 0;
    for (auto role : {Role::assault, Role::rifle})
        for (std::size_t i = 0; i < count; ++i)
            if (roster[i].role == role) mobile[size++] = i;
    auto cap = (std::min)(std::size_t{4}, (count + 1) / 2);
    // Rotate the starting pair in larger rosters so the four-member cap cannot starve the tail.
    if (count >= 2 && size == 1)
        selected[mobile[0]] = true;
    else
        for (std::size_t i = phase; i < size && cap; i += 2, --cap)
            selected[mobile[(i + 8ULL * cycle) % size]] = true;
    return selected;
}
inline XY axis(Mode mode, XY forward) {
    forward = unit(forward);
    if (mode == Mode::left || mode == Mode::right)
        return unit(forward + left(forward) * (mode == Mode::left ? 1.f : -1.f));
    return forward;
}
inline bool cohesive(XY from, XY to, XY center, XY support, float length) {
    auto inside_or_closer = [&](XY anchor, float meters) {
        float before = squared(from - anchor), after = squared(to - anchor), limit = meters * length;
        return after <= limit * limit || (before > limit * limit && after < before);
    };
    return inside_or_closer(center, 28) && inside_or_closer(support, 18);
}
struct BoundRange {
    float minimum, maximum;
    bool contains(float meters) const {
        return meters >= minimum && meters <= maximum;
    }
};
inline BoundRange bound_range(bool retreat) {
    return retreat ? BoundRange{8, 20} : BoundRange{5, 18};
}
inline bool bound_arrived(XY position, XY goal, float length, bool cover_goal, bool covered) {
    const float tolerance = (cover_goal ? 2.f : 2.5f) * length;
    return (!cover_goal || covered) && squared(position - goal) <= tolerance * tolerance;
}
// Native battle action IDs. Zero keeps the current position.
inline int point_move_action(bool rejoin, bool retreat, bool screened, bool keep_shot) {
    if (rejoin) return 6;
    if (retreat) return screened ? 0 : 8;
    return keep_shot ? 0 : 7;
}
inline bool replan(XY previous, XY destination, XY self, float length, bool combat) {
    const float d = squared(previous - destination), threshold = (combat ? 6.f : 2.f) * length;
    return d > threshold * threshold && d > .5f * squared(self - destination);
}
inline bool fire_first(float distance, float range, bool smg, bool open, bool cover, bool overwatch, bool dwell,
                       bool weapon_effective = true) {
    return weapon_effective && open && distance <= range * (smg ? .65f : .78f) && range > 0 &&
           (cover || overwatch || dwell);
}
inline bool at_rush_allowed(float morale, bool no_advance) {
    return !no_advance && std::isfinite(morale) && morale >= 30.f;
}
inline float at_rush_standoff(float max_range, float blast_radius, float scale) {
    const float safe_min = blast_radius + 2.5f * scale;
    const float upper = max_range - 1.5f * scale;
    if (!std::isfinite(max_range) || !std::isfinite(blast_radius) || !std::isfinite(scale) || !(scale > 0.f) ||
        !(upper > safe_min))
        return 0.f;
    return std::clamp(max_range * .75f, safe_min, upper);
}
inline Posture ineffective_posture(Posture posture, bool effective_weapon, bool screened, bool no_retreat) {
    if (!effective_weapon && !screened && !no_retreat) return (std::min)(posture, fallback);
    return posture;
}
inline std::uint32_t deadline(std::uint32_t now, std::uint32_t delta) {
    // A deadline above a wrapped clock would strand native unsigned comparisons.
    // The actor's own dwell still covers the final ticks of the old epoch.
    return now > UINT32_MAX - delta ? 1u : now + delta;
}
inline float dwell(Posture p, bool cover, bool screened) {
    if (p == flee) return cover ? (screened ? 4.5f : 2.f) : (screened ? 5.f : 2.5f);
    if (p == fallback) return cover ? 5.f : 3.f;
    return cover ? 5.5f : 4.f;
}
inline XY threat_term(XY delta, float rate, float age, bool attacking, bool threatening, float length) {
    float d = std::hypot(delta.x, delta.y) / length;
    if (!(rate > 0) || !std::isfinite(rate) || !(age >= 0 && age <= 12)) return {};
    return unit(delta) * (rate * (attacking ? 2.f : (threatening ? 1.5f : 1.f)) / ((d + 5) * (1 + age)));
}
inline XY withdraw(XY threat, XY ally) {
    XY away = unit(threat), friendly = unit(ally);
    return dot(away, friendly) > .25f ? unit(away * .75f + friendly * .25f) : away;
}
struct CoverClaim {
    Id scene{}, actor{}, squad{};
    unsigned id{};
    XY point{};
    double until{};
};
struct Claims {
    std::array<CoverClaim, 256> entries{};
    void claim(CoverClaim c, double now) {
        if (!c.squad) return;
        CoverClaim* free = nullptr;
        for (auto& e : entries) {
            if (e.actor == c.actor && e.scene == c.scene) {
                e = c;
                return;
            }
            if (!free && (!e.actor || e.until <= now || e.scene != c.scene)) free = &e;
        }
        if (free) *free = c;
    }
    float nearest(Id scene, Id squad, Id actor, XY point, double now) const {
        float d = std::numeric_limits<float>::infinity();
        if (squad)
            for (const auto& c : entries)
                if (c.scene == scene && c.squad == squad && c.actor != actor && c.until > now)
                    d = (std::min)(d, squared(point - c.point));
        return d;
    }
};
enum class Sight {
    unknown,
    blocked,
    open
};
// Descriptor height classes differ from chassis stances. Class zero has no usable height.
inline float cover_height(int protected_class, int firing_class, bool retreat) {
    switch (retreat ? protected_class : firing_class) {
        case 1: return .35f;
        case 2: return .9f;
        case 3: return 1.35f;
        default: return 0;
    }
}
struct CoverSelection {
    static constexpr unsigned limit = 12;
    unsigned probes{}, winner{limit};
    float score{-std::numeric_limits<float>::infinity()};
    template <class Probe> Sight probe(Probe measure) {
        if (probes == 6) return Sight::unknown;
        ++probes;
        return measure();
    }
    void consider(unsigned index, float value) {
        if (index < limit && std::isfinite(value) && value > score) {
            winner = index;
            score = value;
        }
    }
};
inline float cover_score(float native, Sight sight, bool retreat, float alignment, float spacing_m,
                         bool is_screened = false) {
    const bool useful = retreat ? sight == Sight::blocked : sight == Sight::open;
    return native + (useful ? 12.f : 0.f) + (is_screened ? 8.f : 0.f) + 6.f * std::clamp(alignment, 0.f, 1.f) -
           (spacing_m <= 2.5f ? 15.f : std::clamp((3.f - spacing_m) * 30.f, 0.f, 15.f));
}
inline bool stable_cover(double now, const Soldier& s, bool screened, bool taking_fire, bool can_fire) {
    return s.covered && now - (std::max)(s.entered, s.moved) < 9 && (screened || (!taking_fire && can_fire));
}
inline int rise(int stance, bool current_open, bool crouch_open, bool standing_open) {
    if (current_open || (stance != 1 && stance != 2)) return -1;
    if (stance == 1 && crouch_open) return 2;
    return standing_open ? 0 : -1;
}
inline float peek_score(bool passable, bool open, float exposure, float spacing, float flank, float meters) {
    if (!passable || !open) return -std::numeric_limits<float>::infinity();
    return cover_score(0, Sight::open, false, flank, spacing) - exposure - meters * .25f;
}
inline bool hold_anchor(unsigned modes, unsigned flags, bool covered, bool remembered) {
    return (modes & 0x3003u) == 0 && (flags & (1u << 11)) && (covered || remembered || (flags & (3u << 17)));
}
} // namespace goh::tactical_infantry::policy
