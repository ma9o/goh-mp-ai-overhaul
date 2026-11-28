#pragma once

#include "dynamic_store.hpp"

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
inline constexpr float kPreferredSpacingMinM = 5.0f;
inline constexpr float kPreferredSpacingMaxM = 8.0f;
inline constexpr float kClumpTriggerM = 4.0f;
inline constexpr float kClumpMinDestM = 4.5f;
inline constexpr float kClumpMinGainM = 2.0f;
inline constexpr double kDisperseCooldownS = 8.0;
inline constexpr double kDisperseRetryS = 2.5;
inline constexpr float kMarchCoverRadiusM = 18.0f;
inline constexpr double kMarchCoverCooldownS = 10.0;
inline constexpr double kMarchCoverRetryS = 2.5;
inline constexpr double kCoverBreakHoldS = 10.0;

struct Soldier {
    Posture posture{hold};
    bool initialized{}, covered{};
    double changed{never}, entered{never}, left_cover{never}, moved{never}, fire_until{never}, detour_until{never},
        next_search{never}, dispersed_at{never}, disperse_retry{never}, cover_break_until{never},
        march_cover_retry{never};
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
    wedge_left,
    wedge_right,
    fallback
};
inline bool is_wedge(Mode m) {
    return m == Mode::wedge_left || m == Mode::wedge_right;
}
inline float wedge_sign(Mode m) {
    return m == Mode::wedge_left ? 1.f : (m == Mode::wedge_right ? -1.f : 0.f);
}
inline bool advancing(Mode m) {
    return m == Mode::bound || m == Mode::left || m == Mode::right || is_wedge(m);
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
    double committed{never}, phase_at{never}, arrived_at{never}, contact_since{never}, last_contact{never};
    void observe_contact(double now, bool has_contact) {
        if (has_contact) {
            if (contact_since == never || now - last_contact > 4.0) contact_since = now;
            last_contact = now;
        } else if (last_contact != never && now - last_contact > 4.0) {
            contact_since = never;
        }
    }
    double contact_duration(double now) const {
        return contact_since == never ? 0.0 : (std::max)(0.0, now - contact_since);
    }
    void update(double now, Mode desired, float l, float r, bool arrived) {
        if (desired == Mode::left || desired == Mode::right) {
            if (mode == Mode::left) l += 6;
            if (mode == Mode::right) r += 6;
            desired = l >= r ? Mode::left : Mode::right;
        } else if (desired == Mode::wedge_left || desired == Mode::wedge_right) {
            if (mode == Mode::wedge_left) l += 6;
            if (mode == Mode::wedge_right) r += 6;
            desired = l >= r ? Mode::wedge_left : Mode::wedge_right;
        }
        const bool abort_wedge = is_wedge(mode) && desired == Mode::fallback;
        if (committed == never || abort_wedge || (desired != mode && now - committed >= 6)) {
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
        if (is_wedge(mode)) {
            if (now - phase_at >= 6.5 || (arrived && now - arrived_at >= 2.5)) {
                ++cycle;
                phase_at = now;
                arrived_at = never;
            }
            return;
        }
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
    Id gun_def{}, paired_gunner{};
};
using Roster = std::array<Member, 64>;
using Elements = std::array<bool, 64>;
inline float meters_between(XY a, XY b, float length) {
    return std::hypot(a.x - b.x, a.y - b.y) / length;
}
// Anti-HE/MG dispersion curve: prefers 5-8 m spacing with finite fallback down to <= 2.5 m
// so soldiers still take scarce cover rather than staying exposed in the open.
inline float crowding_penalty(float spacing_m) {
    if (!std::isfinite(spacing_m)) return spacing_m > 0.f ? 0.f : 22.f;
    if (spacing_m >= kPreferredSpacingMaxM) return 0.f;
    if (spacing_m <= 2.5f) return 22.f;
    if (spacing_m < 5.5f) return 4.f + (5.5f - spacing_m) * 6.f;
    return (kPreferredSpacingMaxM - spacing_m) * 1.6f;
}
inline float assistant_anchor_bonus(XY candidate, XY gunner_pos, float length) {
    const float d = meters_between(candidate, gunner_pos, length);
    if (d < 2.5f || d > 8.f) return 0.f;
    return d >= 4.5f ? 14.f : 12.f;
}
inline bool assistant_cohesive(XY from, XY to, XY gunner_pos, float length) {
    const float after = meters_between(to, gunner_pos, length);
    return after >= 1.5f && (after <= 10.f || after < meters_between(from, gunner_pos, length));
}
// Match only the gunner's carried definition. Roster order breaks equal-distance ties.
template <class Compatible>
const Member* match_gunner(const Member& mate, const Roster& gunners, std::size_t count, Compatible compatible) {
    if (mate.role == Role::heavy) return nullptr;
    const Member* best = nullptr;
    float distance = std::numeric_limits<float>::infinity();
    for (std::size_t i = 0; i < count; ++i) {
        const auto& g = gunners[i];
        const float d = squared(mate.position - g.position);
        if (g.actor != mate.actor && g.gun_def && d < distance && compatible(mate.actor, g.gun_def)) {
            distance = d;
            best = &g;
        }
    }
    return best;
}

struct Placement {
    bool was_mounted{}, prev_user_move{}, overflow_done{true};
    double dismounted_at{never}, spread_attempt{never}, overflow_until{never};
    XY overflow_anchor{};
    void observe(bool mounted, bool user_move, XY self, double now) {
        if (mounted || user_move) {
            overflow_done = true;
            dismounted_at = never;
            spread_attempt = now;
        } else {
            if (was_mounted) {
                dismounted_at = now;
                spread_attempt = never;
            }
            if (prev_user_move) {
                overflow_until = now + 3.;
                overflow_anchor = self;
                overflow_done = false;
            }
        }
        was_mounted = mounted;
        prev_user_move = user_move && !mounted;
    }
    bool recent_dismount(double now, double nearby_dismount) const {
        return (dismounted_at > spread_attempt && now - dismounted_at <= 3.) ||
               (nearby_dismount > spread_attempt + 3. && now - nearby_dismount <= 3.);
    }
};
inline bool overflow_cover_eligible(unsigned modes, unsigned flags, unsigned covered_mates, bool enemy) {
    const auto movement = modes & 0x3000u;
    return allows(modes & ~0x3000u, flags) &&
           ((movement == 0 && (covered_mates || enemy)) || (movement == 0x1000u && covered_mates >= 2));
}
inline float score_overflow_spot(float native_score, XY point, XY anchor, float length, float radius_m,
                                 bool hold_position, float spacing_m) {
    const float d = meters_between(point, anchor, length);
    if (!(spacing_m >= 1.5f && d <= radius_m) || (hold_position && !(d < (std::min)(radius_m, 7.f))))
        return -std::numeric_limits<float>::infinity();
    return native_score - d * .5f - crowding_penalty(spacing_m);
}
// Eight deterministic directions also separate passengers with identical exit positions.
inline XY pick_dismount_spread_offset(XY self, XY centroid, XY away, unsigned id, unsigned attempt, float length) {
    XY outward = unit(self - centroid);
    if (!squared(outward)) outward = squared(away) > 0 ? unit(away) : XY{1, 0};
    return rotate(outward, 45.f * ((id + attempt) % 8)) * (6.f * length);
}
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
template <class CanEngage>
Posture ineffective_posture(Posture posture, bool covered, bool no_retreat, unsigned enemy_flags,
                            float enemy_distance_m, CanEngage can_engage) {
    if (!covered && !no_retreat && ((enemy_flags & 0x180u) || enemy_distance_m <= 28.f) && !can_engage())
        return (std::min)(posture, fallback);
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
    store::GrowableTable<CoverClaim, 64, 4096> entries{};
    bool try_claim(CoverClaim c, double now) {
        if (!c.squad) return false;
        CoverClaim* free = nullptr;
        for (auto& e : entries) {
            if (e.actor == c.actor && e.scene == c.scene) {
                e = c;
                return true;
            }
            if (!free && (!e.actor || e.until <= now || e.scene != c.scene)) free = &e;
        }
        if (!free) free = entries.append_slot();
        if (free) *free = c;
        return free != nullptr;
    }
    void claim(CoverClaim c, double now) {
        try_claim(c, now);
    }
    float nearest(Id scene, Id squad, Id actor, XY point, double now) const {
        float d = std::numeric_limits<float>::infinity();
        if (squad)
            for (const auto& c : entries)
                if (c.scene == scene && c.squad == squad && c.actor != actor && c.until > now)
                    d = (std::min)(d, squared(point - c.point));
        return d;
    }
    bool has_outbound_claim(Id scene, Id squad, Id actor, XY from_point, float min_distance_sq, double now) const {
        if (!squad) return false;
        for (const auto& c : entries)
            if (c.scene == scene && c.squad == squad && c.actor == actor && c.until > now)
                return squared(c.point - from_point) >= min_distance_sq;
        return false;
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
    static constexpr unsigned limit = 32;
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
           crowding_penalty(spacing_m);
}
inline bool anchor_beats(bool mate_covered, bool mate_is_gunner, Role mate_role, unsigned mate_id, bool self_covered,
                         Role self_role, unsigned self_id) {
    if (mate_covered != self_covered) return mate_covered;
    if (mate_is_gunner) return true;
    const bool mate_heavy = mate_role == Role::heavy;
    const bool self_heavy = self_role == Role::heavy;
    if (mate_heavy != self_heavy) return mate_heavy;
    return mate_id < self_id;
}
inline bool should_disperse_clump(const Soldier& s, double now, bool under_threat, float nearest_mate_m,
                                  bool higher_priority_mate_close) {
    if (s.covered || !under_threat || !higher_priority_mate_close) return false;
    if (!std::isfinite(nearest_mate_m) || nearest_mate_m > kClumpTriggerM) return false;
    return now - s.dispersed_at >= kDisperseCooldownS && now >= s.disperse_retry;
}
inline bool dispersion_step_safe(XY from, XY to, XY threat) {
    const XY to_threat = unit(threat - from);
    if (!squared(to_threat)) return false;
    return dot(to - from, to_threat) <= 0.f;
}
inline bool spacing_improved(float current_spacing_m, float candidate_spacing_m, bool screened_cover) {
    if (!std::isfinite(candidate_spacing_m)) return candidate_spacing_m > 0.f;
    const float min_dest = screened_cover ? 3.5f : kClumpMinDestM;
    const float min_gain = screened_cover ? 1.5f : kClumpMinGainM;
    const float base = std::isfinite(current_spacing_m) ? current_spacing_m : 0.f;
    return candidate_spacing_m >= min_dest && candidate_spacing_m >= base + min_gain;
}
inline float score_disperse_cover(float native_score, Sight sight, float spacing_m, float dist_m, bool is_screened) {
    if (!is_screened || !std::isfinite(native_score) || !std::isfinite(dist_m) || dist_m < 2.f || dist_m > 10.f)
        return -std::numeric_limits<float>::infinity();
    return native_score + 16.f + (sight == Sight::open ? 10.f : 0.f) - crowding_penalty(spacing_m) - dist_m * .3f;
}
inline float score_disperse_offset(float spacing_m, float exposure, Sight sight, float dist_m, float outward_dot) {
    if (!std::isfinite(exposure) || !std::isfinite(dist_m) || dist_m < 3.5f || dist_m > 13.f)
        return -std::numeric_limits<float>::infinity();
    const float sight_term = sight == Sight::open ? 12.f : (sight == Sight::blocked ? -6.f : 0.f);
    return sight_term - crowding_penalty(spacing_m) - exposure * 4.f + 2.f * std::clamp(outward_dot, -1.f, 1.f) -
           dist_m * .25f;
}
inline std::array<XY, 10> lateral_disperse_candidates(XY self, XY threat, XY clump_center, unsigned actor_id,
                                                      float length) {
    const XY to_threat = unit(threat - self);
    const XY side_axis = left(to_threat);
    const float away_side = dot(self - clump_center, side_axis);
    const float first_sign =
        (std::fabs(away_side) > 0.25f * length) ? (away_side >= 0.f ? 1.f : -1.f) : ((actor_id & 1u) ? 1.f : -1.f);
    const float second_sign = -first_sign;
    const XY back = to_threat * -1.f;
    // Lateral (±90° at 6 / 7.5 / 12.5 m), oblique-back (±140° at 7.2 m), and rear (180° at 6 / 7.8 m):
    // every candidate satisfies dot(step, to_threat) <= 0 and gives 5-8 man clumps >= 5 m pairwise clearance.
    const XY ob1 = unit(side_axis * first_sign * .6427876f + back * .7660444f);
    const XY ob2 = unit(side_axis * second_sign * .6427876f + back * .7660444f);
    return {
        self + side_axis * (first_sign * 6.f * length),
        self + side_axis * (first_sign * 7.5f * length),
        self + side_axis * (second_sign * 6.f * length),
        self + side_axis * (second_sign * 7.5f * length),
        self + ob1 * (7.2f * length),
        self + ob2 * (7.2f * length),
        self + back * (7.8f * length),
        self + back * (6.f * length),
        self + side_axis * (first_sign * 12.5f * length),
        self + side_axis * (second_sign * 12.5f * length),
    };
}
inline bool ai_march_allows(unsigned modes, unsigned flags) {
    const auto ctrl = modes & 3u;
    const auto fire = (modes >> 10) & 3u;
    const auto move = modes & 0x3000u;
    return ctrl == 0u && move == 0u && !(flags & 0x0110003fu) && (fire == 0u || (fire == 2u && (flags & (1u << 18))));
}
inline bool ai_march_under_fire(float morale, bool losing_morale, bool recent_hit_signal, unsigned brain_flags,
                                bool hostile_threatening) {
    if (recent_hit_signal || losing_morale || hostile_threatening) return true;
    if (brain_flags & 0x1C000u) return true;
    return std::isfinite(morale) && morale >= 0.f && morale < 75.f;
}
inline bool should_seek_march_cover(const Soldier& s, double now, bool marching, bool under_fire) {
    return !s.covered && marching && under_fire && now >= s.cover_break_until && now >= s.march_cover_retry;
}
inline float score_march_cover(float native_score, bool is_screened, float spacing_m, float dist_m,
                               float radius_m = kMarchCoverRadiusM) {
    if (!is_screened || !std::isfinite(native_score) || !std::isfinite(dist_m) || !(spacing_m >= 1.5f) ||
        dist_m > radius_m)
        return -std::numeric_limits<float>::infinity();
    return native_score + 16.f - crowding_penalty(spacing_m) - dist_m * .45f;
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
inline bool should_wedge(std::size_t squad_count, unsigned covered_count, unsigned open_fire_count, bool armor_near,
                         float morale_avg, float enemy_dist_m, double contact_duration_s,
                         bool fire_advantage_or_blocked) {
    return squad_count >= 4 && covered_count >= 2 && open_fire_count >= 1 && !armor_near && fire_advantage_or_blocked &&
           std::isfinite(morale_avg) && morale_avg >= 62.f && std::isfinite(enemy_dist_m) && enemy_dist_m >= 16.f &&
           enemy_dist_m <= 50.f && contact_duration_s >= 5.0;
}
inline Elements wedge_elements(const Roster& roster, std::size_t count, XY center, XY threat, float flank_sign) {
    Elements selected{};
    count = (std::min)(count, roster.size());
    if (count < 4 || !(std::fabs(flank_sign) > 0.f)) return selected;
    const XY lat = left(unit(threat - center)) * flank_sign;
    std::array<std::size_t, 64> cands{};
    std::size_t n = 0;
    for (std::size_t i = 0; i < count; ++i)
        if (roster[i].role == Role::assault || roster[i].role == Role::rifle) cands[n++] = i;
    const std::size_t pick_max = (std::min)(std::size_t{2}, count - 2);
    std::sort(cands.begin(), cands.begin() + static_cast<std::ptrdiff_t>(n), [&](std::size_t a, std::size_t b) {
        const bool a_assault = roster[a].role == Role::assault;
        const bool b_assault = roster[b].role == Role::assault;
        if (a_assault != b_assault) return a_assault;
        const float pa = dot(roster[a].position - center, lat);
        const float pb = dot(roster[b].position - center, lat);
        if (std::fabs(pa - pb) > 1e-3f) return pa > pb;
        return roster[a].id < roster[b].id;
    });
    for (std::size_t i = 0; i < n && i < pick_max; ++i)
        selected[cands[i]] = true;
    return selected;
}
inline float wedge_flank_angle_deg(XY self, XY center, XY threat, float flank_sign) {
    const XY front = unit(threat - center);
    const XY lat = left(front) * flank_sign;
    if (!squared(front) || dot(self - center, lat) <= 0.f) return 0.f;
    const XY from_threat = unit(self - threat);
    const XY base_from_threat = unit(center - threat);
    if (!squared(from_threat) || !squared(base_from_threat)) return 0.f;
    const float cos_a = std::clamp(dot(from_threat, base_from_threat), -1.f, 1.f);
    return std::acos(cos_a) * 57.2957795f;
}
inline bool wedge_in_enfilade(float flank_angle_deg, float enemy_dist_m, bool covered, bool open_sight) {
    return covered && open_sight && flank_angle_deg >= 38.f && enemy_dist_m <= 34.f;
}
inline XY wedge_direction(XY self, XY center, XY threat, float flank_sign, float length) {
    const XY front = unit(threat - center);
    const XY lat = left(front) * flank_sign;
    if (!squared(front)) return {};
    const float angle = wedge_flank_angle_deg(self, center, threat, flank_sign);
    const float dist_m = meters_between(self, threat, length);
    if (angle < 38.f) return unit(front * 0.42f + lat * 0.91f);                // ~65 deg lateral-oblique hook
    if (dist_m > 18.f) return unit(unit(threat - self) * 0.85f + lat * 0.52f); // curve inward to enfilade/frag range
    return {};
}
inline bool wedge_cohesive(XY from, XY to, XY center, XY support, XY threat, float length) {
    auto inside_or_closer = [&](XY anchor, float meters) {
        const float before = squared(from - anchor), after = squared(to - anchor), limit = meters * length;
        return after <= limit * limit || (before > limit * limit && after < before);
    };
    return inside_or_closer(center, 38.f) && inside_or_closer(support, 32.f) &&
           meters_between(to, threat, length) >= 11.f;
}
inline float score_wedge_cover(float native_score, Sight sight, float spacing_m, float dist_m, float angle_before,
                               float angle_after, float enemy_dist_before, float enemy_dist_after, float progress_m,
                               bool is_screened) {
    if (!is_screened || !std::isfinite(native_score) || !std::isfinite(dist_m) || !std::isfinite(progress_m) ||
        !(spacing_m >= 2.5f))
        return -std::numeric_limits<float>::infinity();
    if (!(dist_m >= 4.5f && dist_m <= 21.f) || !(enemy_dist_after >= 11.f))
        return -std::numeric_limits<float>::infinity();
    // Beyond 45 m, require actual oblique forward closing instead of shuffling sideways along the back wall.
    if (enemy_dist_before > 45.f && !(enemy_dist_after <= enemy_dist_before - 3.f))
        return -std::numeric_limits<float>::infinity();
    const bool angle_gain = angle_after >= angle_before + 2.5f;
    const bool oblique_advance = progress_m >= 4.f && angle_after >= angle_before - 2.f;
    const bool enfilade_close = angle_after >= 38.f && enemy_dist_after <= 34.f;
    if (!angle_gain && !oblique_advance && !enfilade_close) return -std::numeric_limits<float>::infinity();
    const float closing = enemy_dist_before > 26.f ? std::clamp(enemy_dist_before - enemy_dist_after, 0.f, 16.f) : 0.f;
    return native_score + 14.f + (sight == Sight::open ? 12.f : 0.f) + 0.6f * std::clamp(progress_m, 0.f, 18.f) +
           0.35f * closing + 0.45f * std::clamp(angle_after - angle_before, -5.f, 35.f) +
           (angle_after >= 38.f ? 10.f : 0.f) - crowding_penalty(spacing_m) - dist_m * 0.2f;
}
} // namespace goh::tactical_infantry::policy
