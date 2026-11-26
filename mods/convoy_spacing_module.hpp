#pragma once

#include "convoy_spacing_policy.hpp"
#include "movement_recovery_module.hpp"
#include "tank_auto_repair_module.hpp"

namespace goh::convoy_spacing {
namespace m = movement_orders;
namespace s = ai_support;
using runtime::Address;
using runtime::read;
inline constexpr bool runtime_allowed = false;
inline constexpr unsigned hook_count = 0;
inline std::atomic<unsigned> stat_yields{}, stat_passes{}, stat_resumed{};
namespace detail {
struct History : Following {
    double next_scan{}, next_pass_probe{}, last_progress{};
    XY progress_anchor{};
    void interrupt() {
        reset_observation();
        next_scan = 0;
        next_pass_probe = 0;
    }
};
inline m::Controller<History> control{coord::Module::convoy_spacing, coord::Priority::convoy_spacing};
inline bool mobile(Address a) {
    const auto parts = s::span(a, 0xB90);
    if (!parts.valid(0x28, 4096)) return false;
    bool hull{}, running_gear{};
    for (auto p = parts.first; p != parts.end; p += 0x28) {
        auto def = read<Address>(p);
        if (!def || read<Address>(p, 8) != a) return false;
        auto names = s::span(def, 0x328);
        if (!names.valid(0x60, 4096)) return false;
        auto name = tank_auto_repair::detail::component_name(def);
        const bool body = name == "body" || name == "engine";
        const bool gear = name.substr(0, 5) == "track" || name.substr(0, 5) == "wheel" || s::tag(names, "track") ||
                          s::tag(names, "wheel");
        if ((body || gear) && read<m::Byte>(p, 0x24)) return false;
        hull |= body;
        running_gear |= gear;
    }
    return hull && running_gear;
}
inline XY forward(Address a, XY fallback) {
    auto v = geom::unit(read<XY>(read<Address>(a, 0xDE0), 0x5C));
    if (geom::norm(v) < .5f) v = geom::unit(read<XY>(a, 0x50));
    return geom::norm(v) >= .5f ? v : geom::unit(fallback);
}
inline XY velocity(const s::World& w, Address a) {
    s::Vec3 v{};
    s::native<s::Vec3* (*)(Address, s::Vec3*)>(0x73A2A0)(a, &v);
    return {v.x * w.ticks / w.length, v.y * w.ticks / w.length};
}
inline auto speed_vector = &velocity;
inline bool allowed(const s::World& w, Address a) {
    if (!config::active().modules.convoy_spacing || !(read<m::Byte>(a, 0x30) & 8) || !m::eligible(w, a) ||
        !s::autonomous_vehicle_brain(w.base, a) || s::extender(a, runtime::table().vt.cannon_ext) ||
        tank_auto_repair::has_active_episode(a) || !mobile(a))
        return false;
    return true;
}
inline bool reversing(const s::World& w, Address a, XY fwd, XY v) {
    const float speed = geom::dot(v, fwd);
    return !runtime::finite(v) || geom::norm(fwd) < .5f || speed < -.2f ||
           (speed < 0 && s::hostile_near(w, a, s::pos(a), 60.f * w.length));
}
inline Leader leader(const s::World& w, Address a, XY fwd, float gap) {
    Leader best{};
    const auto self = s::pos(a);
    s::actors(w, [&](Address other) {
        if (other == a || !(read<m::Byte>(other, 0x30) & 8) || !(read<unsigned>(other, 0xDB4) & 0x100)) return;
        const auto p = s::pos(other);
        if (!s::within(self, p, (gap + 6.f) * w.length)) return;
        const auto along = blocking_distance(m::xy(self), fwd, m::xy(p), w.length, gap);
        const auto id = read<unsigned>(other, 0xC0);
        if (!along || (best.present && (*along > best.along || (*along == best.along && id >= best.id))) ||
            !m::sim::allies(w.base, s::owner(a), s::owner(other)) || s::has_destruction_tag(other))
            return;
        const auto v = speed_vector(w, other);
        if (!runtime::finite(v)) return;
        best = {id, m::xy(p), *along, geom::norm(v), geom::dot(fwd, forward(other, v)), geom::dot(fwd, v), true};
    });
    return best;
}
inline float segment_distance(XY from, XY to, XY point) {
    const auto path = geom::delta(to, from);
    const auto square = geom::dot(path, path);
    if (!(square > 0)) return geom::distance(from, point);
    const auto fraction = std::clamp(geom::dot(geom::delta(point, from), path) / square, 0.f, 1.f);
    return geom::distance(point, {from.x + fraction * path.x, from.y + fraction * path.y});
}
inline bool lane_clear(const s::World& w, Address a, XY from, XY to) {
    bool free = true;
    s::actors(w, [&](Address other) {
        if (!free || other == a || !(read<m::Byte>(other, 0x30) & 8)) return;
        const XY pos = m::xy(s::pos(other));
        // At bumper distance permit a lateral exit only if it immediately
        // increases separation and finishes outside the clearance radius.
        const float radius = 4.5f * w.length;
        if (!runtime::finite(pos) || geom::distance(to, pos) < radius) {
            free = false;
            return;
        }
        if (geom::distance(from, pos) < radius)
            free = geom::dot(geom::delta(to, from), geom::delta(from, pos)) >= 0;
        else
            free = segment_distance(from, to, pos) >= radius;
    });
    return free;
}
inline bool permit(const s::World& w, Address a, Address order) {
    auto* r = control.find(a);
    if (!r || !r->trip.root) return false;
    auto check = [&](const s::World& live, Address actor) {
        if (!allowed(live, actor)) return false;
        const auto self = m::xy(s::pos(actor));
        const auto fwd = forward(actor, geom::delta(r->trip.goal, self));
        if (reversing(live, actor, fwd, speed_vector(live, actor))) return false;
        if (order == r->trip.back.get() && r->trip.yielding)
            return m::seconds(live) >= r->trip.started + 1.5 &&
                   !leader(live, actor, fwd, config::active().tuning.convoy_gap_m).present;
        if (read<unsigned>(order, 0x18) & (1u << 16)) return true;
        if (order == r->trip.outbound.get()) return lane_clear(live, actor, self, r->trip.waypoint);
        if (order == r->trip.approach.get()) return lane_clear(live, actor, self, r->trip.shoulder);
        return true;
    };
    return control.permit(w, a, order, check, stat_resumed);
}
struct PassingRoute {
    XY waypoint{};
    std::optional<XY> shoulder;
};
template <class Traversable>
std::optional<PassingRoute> passing_route(const s::World& w, Address a, const Leader& ahead, XY fwd,
                                          Traversable terrain) {
    const auto self = s::pos(a);
    std::optional<PassingRoute> fallback;
    const auto selected = pick_pass_point(ahead.pos, fwd, w.length, [&](XY point) {
        auto p = m::at(point, self.z);
        if (!m::valid_point(point) || !terrain(self, p)) return false;
        if (lane_clear(w, a, m::xy(self), point)) return true;
        // The direct diagonal may cut across the leader's rear. Move sideways
        // to the same passing lane first, then travel parallel to its heading.
        if (fallback) return false;
        const auto d = geom::delta(point, m::xy(self));
        const auto lateral = fwd.x * d.y - fwd.y * d.x;
        auto turn = m::at(geom::offset(m::xy(self), fwd, 0, lateral, 1.f), self.z);
        if (!m::valid_point(m::xy(turn)) || !terrain(self, turn) || !terrain(turn, p) ||
            !lane_clear(w, a, m::xy(self), m::xy(turn)) || !lane_clear(w, a, m::xy(turn), point))
            return false;
        fallback = PassingRoute{point, m::xy(turn)};
        return false;
    });
    if (selected) return PassingRoute{*selected, {}};
    return fallback;
}
inline std::optional<PassingRoute> pass(const s::World& w, Address a, const Leader& ahead, XY fwd) {
    s::PathContext path(a);
    return passing_route(w, a, ahead, fwd,
                         [&](s::Vec3 from, s::Vec3& to) { return s::ground(w, to) && s::corridor(w, path, from, to); });
}
inline auto probe = &pass;
} // namespace detail
inline bool occupied(Address a) {
    return detail::control.occupied(a);
}
inline void preempt(Address a) {
    if (auto* r = detail::control.find(a)) r->interrupt();
}
inline bool init(Address image, const char* sha) {
    if (!detail::control.init(image, sha, occupied, preempt)) return false;
    stat_yields = stat_passes = stat_resumed = 0;
    return true;
}
inline void on_actor_post_quant(Address a) {
    using namespace detail;
    runtime::HookGuard guard(control.gate);
    s::World w{};
    if (!control.context(a, w) || control.issuing || !control.sync(w)) return;
    if (!allowed(w, a) || !control.can_act(a, w)) {
        preempt(a);
        return;
    }
    auto* old = control.find(a);
    const bool active = old && old->trip.root;
    if (active && !old->trip.owns(a)) {
        preempt(a);
        return;
    }
    const auto source = active ? m::Move{old->trip.root, 0, old->trip.goal} : m::active_move(a);
    const auto self = m::xy(s::pos(a));
    if (!source.root || (!active && geom::distance(self, source.dest) < 6.f * w.length)) {
        preempt(a);
        return;
    }
    const auto fwd = forward(a, geom::delta(source.dest, self)), v = speed_vector(w, a);
    if (reversing(w, a, fwd, v)) {
        preempt(a);
        return;
    }
    auto& row = control.get(a);
    const auto now = m::seconds(w);
    if (active && !row.trip.yielding) {
        if (geom::distance(self, row.history.progress_anchor) >= .6f * w.length) {
            row.history.progress_anchor = self;
            row.history.last_progress = now;
        }
        if (geom::distance(self, row.trip.waypoint) <= 1.2f * w.length || now >= row.history.last_progress + 8.)
            row.trip.advance(a);
        return;
    }
    auto& history = row.history;
    if (!active && (row.observed.order != source.order || row.observed.root != source.root)) history.interrupt();
    row.observed = source;
    history.destination(source.dest, w.length);
    if (now < history.next_scan) return;
    history.next_scan = now + .25;
    const auto gap = config::active().tuning.convoy_gap_m;
    const auto ahead = leader(w, a, fwd, gap);
    auto action = history.sample(ahead, geom::norm(v), geom::dot(v, fwd), gap, now);
    if (active) {
        if (!ahead.present && now - row.trip.started >= 1.5) {
            row.trip.advance(a);
            return;
        }
        row.trip.extend_wait(a);
        if (action != Action::try_pass || now < history.next_pass_probe) return;
    }
    if (action == Action::none) return;
    std::optional<PassingRoute> route;
    if (action == Action::try_pass) {
        route = probe(w, a, ahead, fwd);
        history.next_pass_probe = now + 4.;
        if (!route && active) return;
    }
    auto still_allowed = [&](const s::World& live, Address actor) {
        return allowed(live, actor) && !reversing(live, actor, fwd, speed_vector(live, actor));
    };
    const std::optional<XY> point = route ? std::optional<XY>{route->waypoint} : std::nullopt;
    if (!control.issue(w, a, row, source, point, permit, still_allowed, route ? route->shoulder : std::nullopt)) return;
    history.issued(point.has_value(), now);
    history.progress_anchor = self;
    history.last_progress = now;
    if (point)
        ++stat_passes;
    else
        ++stat_yields;
}
inline void begin_shutdown() noexcept {
    detail::control.gate.deactivate();
}
inline void deactivate() noexcept {
    begin_shutdown();
}
inline auto inflight_count() noexcept {
    return detail::control.gate.count();
}
inline bool quiescent() {
    return !inflight_count() && s::quiescent();
}
inline void after_hooks_removed() {
    detail::control.removed();
}
struct Stats {
    unsigned yields, passes, resumed;
};
inline Stats snapshot_stats() noexcept {
    return {stat_yields.load(), stat_passes.load(), stat_resumed.load()};
}
} // namespace goh::convoy_spacing
