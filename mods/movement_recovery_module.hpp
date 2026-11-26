#pragma once

#include "ai_support_26734796.hpp"
#include "movement_recovery_policy.hpp"

// The two Stream D adapters share the same native point-task and queue ownership rules.
namespace goh::movement_orders {
namespace s = ai_support;
namespace sim = auto_smoke;
namespace geom = movement_geometry;
using runtime::Address;
using runtime::Byte;
using runtime::read;
using runtime::XY;
using s::Vec3;
using s::World;
constexpr Address sequence_vt = 0x110D490, point_vt = 0x1108C90;
inline XY xy(Vec3 p) {
    return {p.x, p.y};
}
inline Vec3 at(XY p, float z) {
    return {p.x, p.y, z};
}
inline double seconds(const World& w) {
    return static_cast<double>(w.now) / w.ticks;
}

struct Native {
    static Address make_sequence() {
        auto mem = s::native<Address (*)(std::size_t)>(runtime::table().fn.game_alloc)(0x58);
        return mem ? s::native<s::Unary>(0x918560)(mem) : 0;
    }
    static Address make_point(const XY& goal) {
        return s::native<Address (*)(const XY*, unsigned)>(runtime::table().fn.move_task_point)(&goal, 0);
    }
    static Address make_wait(float duration) {
        return s::native<Address (*)(float)>(0x8E2710)(duration);
    }
    static void append(Address q, Address o, int layer) {
        s::native<Address (*)(Address, Address, int, Byte)>(0x919270)(q, o, layer, 1);
    }
    static void clear_front(Address q) {
        s::native<void (*)(Address, int)>(0x9193F0)(q, 1);
    }
    static void set_order(Address brain, Address order) {
        read<void (*)(Address, Address)>(read<Address>(brain), 13 * sizeof(Address))(brain, order);
    }
    decltype(&s::callback) callback = s::callback;
    decltype(&s::resolve) resolve = s::resolve;
    decltype(&make_sequence) sequence = make_sequence;
    decltype(&make_point) point = make_point;
    decltype(&make_wait) wait = make_wait;
    decltype(&append) push = append;
    decltype(&clear_front) keep_last = clear_front;
    decltype(&set_order) set = set_order;
    decltype(&s::discard) discard = s::discard;
};
inline Native native{};
inline bool valid_point(XY p) {
    // Both coordinates are checked individually by the stock point factory.
    return runtime::finite(p) && std::fabs(p.x) > .00001f && std::fabs(p.y) > .00001f;
}
inline bool same(XY a, XY b) {
    return a.x == b.x && a.y == b.y;
}
inline std::optional<XY> task_point(Address order) {
    const auto task = read<Address>(order, 0x40);
    if (!task || read<Address>(task) != s::base + point_vt) return {};
    const auto p = read<XY>(task, 0x10);
    return valid_point(p) ? std::optional<XY>{p} : std::nullopt;
}
inline bool weapon_busy(Address weapon, Address actor) {
    if (!weapon) return false;
    const auto shooter = read<Address>(weapon, 0xB8), charger = read<Address>(weapon, 0xB0);
    return read<Address>(weapon, 0x90) != actor || read<int>(weapon, 0xD0) || read<Byte>(shooter, 0x14) ||
           read<Byte>(shooter, 0x15) || (read<Byte>(charger, 0x14) & 0x14);
}
inline bool quiet(const World& w, Address actor) {
    const auto wp = read<Address>(actor, 0xDD8);
    if (sim::recent(w, read<unsigned>(wp, 0x90), 2.f)) return false;
    for (unsigned offset : {0x18u, 0x30u}) {
        const auto list = s::span(wp, offset);
        if (!list.valid(sizeof(Address), 256)) return false;
        for (auto p = list.first; p != list.end; p += sizeof(Address)) {
            const auto entry = read<Address>(p);
            if (weapon_busy(offset == 0x18 ? read<Address>(entry, 0x2B8) : entry, actor)) return false;
        }
    }
    return true;
}
inline bool eligible(const World& w, Address a) {
    // Allow Hold Position (move_mode == 1) and active user moves (brain+0x39C bit 28) ONLY
    // because callers strictly require an existing active eMove/eMoveTask destination and
    // always resume that exact destination (aborting immediately on any new player order).
    auto move_policy = [](unsigned pol, unsigned flags) {
        return (pol & 3u) <= 1u && ((pol >> 12) & 3u) <= 2u && !(flags & 0x0010003fu);
    };
    return s::autonomous_if(a, false, move_policy) && sim::finite(s::pos(a)) && quiet(w, a);
}

struct Move {
    Address root{}, order{};
    XY dest{};
    bool equals(const Move& b) const {
        return root == b.root && order == b.order && same(dest, b.dest);
    }
};
inline bool ordinary(Address actor, Address o) {
    return o && read<Address>(o, 8) == actor + 0xAF0 && !(read<unsigned>(o, 0x18) & (1u << 23));
}
inline Move active_move(Address a) {
    const auto b = read<Address>(a, 0xDC8);
    if (read<unsigned>(b, 0x39C) & (1u << 24)) return {};
    const auto root = read<Address>(b, 0xF0);
    auto o = root;
    if (!ordinary(a, o)) return {};
    if (read<Address>(o) == s::base + sequence_vt) {
        if (read<std::size_t>(o, 0x48) != 1 || (read<unsigned>(o, 0x20) & 7) || read<Address>(o, 0x50)) return {};
        o = sim::orders::active_child(s::base, o);
        if (!ordinary(a, o)) return {};
    }
    const auto vt = read<Address>(o);
    const auto& tables = runtime::table().vt;
    if (vt == s::base + tables.move_task_order) {
        const auto dest = task_point(o);
        const auto child = read<Address>(o, 0x30);
        if (!dest || (child && (!ordinary(a, child) || read<Address>(child) != s::base + tables.move_order ||
                                read<Address>(child, 0x30))))
            return {};
        return {root, o, *dest};
    }
    if (vt != s::base + tables.move_order || read<Address>(o, 0x30)) return {};
    const auto route = s::span(o, 0x40);
    if (!route.valid(sizeof(Vec3), 65536) || route.end == route.first) return {};
    const auto final = read<Vec3>(route.end - sizeof(Vec3));
    return sim::finite(final) && valid_point(xy(final)) ? Move{root, o, xy(final)} : Move{};
}
inline bool replaceable(Address a, const Move& observed) {
    if (!observed.order || !observed.equals(active_move(a))) return false;
    // Replacing a listened-to stock order would complete someone else's handle.
    if (read<Address>(observed.root, 0x10) || read<Address>(observed.order, 0x10)) return false;
    return !s::tracked(observed.order);
}

// ai_support::install requires an empty live slot. Reserve the same generation-checked
// watch for each still-private point task before transferring the completed sequence.
inline s::OrderRef watch_move(Address o, s::Permit permit) {
    s::Watched* slot{};
    for (auto& item : s::watches)
        if (!item.order.load()) {
            slot = &item;
            break;
        }
    if (!slot) slot = s::watches.append_slot();
    if (!slot) return {};
    if (!++s::next_generation) ++s::next_generation;
    slot->generation = s::next_generation;
    slot->skipped_start = false;
    slot->permit = permit;
    slot->order.store(o);
    ++s::watched_count;
    return {slot, slot->generation};
}

struct Course {
    Address root{}, first{}, spare{};
    s::OrderRef approach{}, outbound{}, back{};
    XY goal{}, waypoint{}, shoulder{};
    double started{};
    bool yielding{};

    void forget() {
        if (s::base) {
            approach.abandon();
            outbound.abandon();
            back.abandon();
        }
        *this = {};
    }
    // The tail watch identifies this allocation generation even after the first leg ends.
    bool owns(Address actor) const {
        const auto tail = back.get();
        if (!root || !tail || read<Address>(read<Address>(actor, 0xDC8), 0xF0) != root ||
            read<Address>(root) != s::base + sequence_vt || !ordinary(actor, root))
            return false;
        const auto count = read<std::size_t>(root, 0x48);
        const auto blocks = read<std::size_t>(root, 0x38), map = read<Address>(root, 0x30);
        if (!count || count > ((spare || approach.get()) ? 3u : 2u) || !map || !blocks || (blocks & (blocks - 1)) ||
            (count + 1) / 2 > blocks || read<Address>(root, 0x50))
            return false;
        auto child = [&](std::size_t i) {
            const auto index = read<std::size_t>(root, 0x40) + i;
            return read<Address>(read<Address>(map, ((index >> 1) & (blocks - 1)) * sizeof(Address)),
                                 (index & 1) * sizeof(Address));
        };
        if (child(count - 1) != tail || !ordinary(actor, tail)) return false;
        const auto dest = task_point(tail);
        if (!dest || !same(*dest, goal)) return false;
        if (count == 1) return true;
        const auto head = child(0);
        if (!ordinary(actor, head)) return false;
        if (yielding) return (head == first || (spare && head == spare)) && (count < 3 || child(1) == spare);
        auto matches_move = [&](Address o, const s::OrderRef& ref, XY expected) {
            if (!o || o != ref.get() || !ordinary(actor, o)) return false;
            const auto pt = task_point(o);
            const auto nested = read<Address>(o, 0x30);
            return pt && same(*pt, expected) &&
                   (!nested ||
                    (ordinary(actor, nested) && read<Address>(nested) == s::base + runtime::table().vt.move_order &&
                     !read<Address>(nested, 0x30)));
        };
        return matches_move(child(count - 2), outbound, waypoint) &&
               (count == 2 || matches_move(head, approach, shoulder));
    }
    bool advance(Address a) const {
        const auto b = read<Address>(a, 0xDC8);
        if (!owns(a) || (read<unsigned>(b, 0x39C) & (1u << 24)) || (read<unsigned>(root, 0x20) & 7)) return false;
        if (read<std::size_t>(root, 0x48) > 1) native.keep_last(root);
        return true;
    }
    void extend_wait(Address a) {
        if (!owns(a) || read<std::size_t>(root, 0x48) != 2 || (read<unsigned>(root, 0x20) & 7) ||
            (read<unsigned>(read<Address>(a, 0xDC8), 0x39C) & (1u << 24)))
            return;
        const auto wait = native.wait(2.f);
        if (!wait) return;
        first = sim::orders::active_child(s::base, root);
        spare = wait;
        native.push(root, wait, 1); // Between the active wait and the guarded return.
    }
};

template <class History> struct Row {
    s::Identity id{};
    Address brain{};
    Move observed{};
    unsigned owner{};
    Course trip{};
    History history{};
    bool matches(Address a) const {
        return id.matches(a) && id.mid == read<std::uint16_t>(a, 0xDAA) && brain == read<Address>(a, 0xDC8) &&
               owner == s::owner(a);
    }
    void interrupt() {
        trip.forget();
        history.interrupt();
        observed = {};
    }
};
template <class History> struct Controller {
    using Record = Row<History>;
    coord::Module module;
    coord::Priority priority;
    Address image{}, scene{}, environment{};
    runtime::Tick clock{};
    runtime::HookGate gate{};
    store::LazyBox<store::SparseTable<Record, std::uint16_t>> rows;
    bool issuing{};
    Controller(coord::Module m, coord::Priority p) noexcept : module(m), priority(p) {}
    void clear() {
        if (auto* table = rows.try_get()) table->for_each([](auto, auto& r) { r.trip.forget(); });
        rows.release();
    }
    bool init(Address base, const char* sha, coord::Coordinator::LiveBusyFn busy,
              coord::Coordinator::PreemptFn preempt) {
        if (image || !base || !sha || std::strcmp(sha, runtime::kBuild26734796Sha256) != 0) return false;
        image = base;
        coord::instance().register_provider(module, priority, busy, preempt);
        gate.activate();
        return true;
    }
    bool context(Address a, World& w) {
        return a && gate.active() && image == s::base && s::accepting.load() && native.callback(a, w) &&
               w.base == image;
    }
    bool same_world(const World& w) const {
        return environment == w.scene && scene && scene == s::entity_scene(w) && w.now >= clock;
    }
    bool sync(const World& w) {
        if (!same_world(w)) clear();
        environment = w.scene;
        scene = s::entity_scene(w);
        clock = w.now;
        coord::instance().sync_world(scene, clock);
        return scene != 0;
    }
    Record* find(Address a) {
        auto* table = rows.try_get();
        auto* r = table ? table->find(read<std::uint16_t>(a, 0xDAA)) : nullptr;
        return r && r->matches(a) ? r : nullptr;
    }
    Record& get(Address a) {
        const auto id = s::identity(a);
        auto& r = rows.get()[id.mid];
        if (!r.matches(a)) {
            r.trip.forget();
            r = {id, read<Address>(a, 0xDC8), {}, s::owner(a), {}, {}};
        }
        return r;
    }
    bool can_act(Address a, const World& w) const {
        return coord::instance().can_act(a, module, priority, read<unsigned>(a, 0xC0), w.now);
    }
    bool occupied(Address a) {
        const auto* r = gate.active() && a ? find(a) : nullptr;
        return r && r->trip.root && r->trip.owns(a);
    }
    void removed() {
        gate.deactivate();
        coord::instance().register_provider(module, priority, nullptr, nullptr);
        clear();
        image = scene = environment = 0;
        clock = 0;
    }

    template <class Allowed>
    bool issue(const World& w, Address a, Record& r, Move source, std::optional<XY> waypoint, s::Permit permission,
               Allowed allowed, std::optional<XY> shoulder = {}) {
        const bool was_yield = r.trip.root && source.root == r.trip.root && r.trip.yielding;
        auto current = [&] {
            return was_yield
                       ? r.trip.root == source.root && same(r.trip.goal, source.dest) && r.trip.owns(a) &&
                             !(read<unsigned>(r.brain, 0x39C) & (1u << 24)) && !(read<unsigned>(source.root, 0x20) & 7)
                       : replaceable(a, source);
        };
        if (issuing || !valid_point(source.dest) || (waypoint && !valid_point(*waypoint)) ||
            (shoulder && (!waypoint || !valid_point(*shoulder))) || !current())
            return false;
        issuing = true;
        struct Reset {
            bool& flag;
            ~Reset() {
                flag = false;
            }
        } reset{issuing};
        Course c{};
        c.goal = source.dest;
        c.yielding = !waypoint;
        c.waypoint = waypoint.value_or(xy(s::pos(a)));
        c.started = seconds(w);
        c.root = native.sequence();
        if (!c.root) return false;
        if (shoulder) {
            const auto leg = native.point(*shoulder);
            if (leg) {
                native.push(c.root, leg, 0);
                c.shoulder = *shoulder;
                c.approach = watch_move(leg, permission);
            }
            if (!c.approach.get()) {
                native.discard(c.root);
                return false;
            }
        }
        c.first = waypoint ? native.point(*waypoint) : native.wait(2.f);
        if (c.first) native.push(c.root, c.first, 0);
        const auto tail = c.first ? native.point(source.dest) : 0;
        if (tail) {
            native.push(c.root, tail, 0);
            c.back = watch_move(tail, permission);
            if (waypoint) c.outbound = watch_move(c.first, permission);
        }
        auto valid = [&] {
            World live{};
            return context(a, live) && same_world(live) && native.resolve(live, r.id) == a && r.matches(a) &&
                   allowed(live, a) && current();
        };
        if (!c.back.get() || (waypoint && !c.outbound.get()) || !valid() ||
            !coord::instance().claim(a, r.id.id, module, priority, w.now)) {
            native.discard(c.root);
            return false;
        }
        if (!valid() || !can_act(a, w)) {
            coord::instance().release(a, module);
            native.discard(c.root);
            return false;
        }
        r.trip.forget();
        r.trip = c;
        native.set(r.brain, c.root);
        coord::instance().release(a, module); // The live provider now represents ownership.
        World live{};
        if (context(a, live) && same_world(live) && native.resolve(live, r.id) == a && r.matches(a) && r.trip.owns(a))
            return true;
        r.interrupt();
        return false;
    }

    template <class Allowed>
    bool permit(const World& w, Address a, Address order, Allowed allowed, std::atomic<unsigned>& resumed) {
        if (!gate.active() || image != w.base || !same_world(w) || !allowed(w, a) || !can_act(a, w)) return false;
        auto* r = find(a);
        if (!r || !r->trip.owns(a) || sim::orders::active_child(image, r->trip.root) != order) return false;
        if (order == r->trip.outbound.get() || order == r->trip.approach.get()) return true;
        if (order != r->trip.back.get()) return false;
        // At entry to the authorized return's Exec the detour is over. Relinquish
        // our watch on the serial thread before handing execution back to stock.
        auto* watch = r->trip.back.watch;
        watch->permit = nullptr;
        if (watch->order.exchange(0) == order) --s::watched_count;
        r->trip.back = {};
        r->interrupt();
        ++resumed;
        return true;
    }
};
} // namespace goh::movement_orders

namespace goh::movement_recovery {
namespace m = movement_orders;
namespace s = ai_support;
using runtime::Address;
inline constexpr bool runtime_allowed = false;
inline constexpr unsigned hook_count = 0;
inline std::atomic<unsigned> stat_nudges{}, stat_resumed{}, stat_aborted{};
namespace detail {
inline m::Controller<StallDetector> control{coord::Module::movement_recovery, coord::Priority::movement_recovery};
inline bool allowed(const s::World& w, Address a) {
    if (!config::active().modules.movement_recovery || !m::eligible(w, a)) return false;
    if (runtime::read<Address>(runtime::read<Address>(a, 0xDC8)) == w.base + runtime::table().vt.human_brain)
        return true;
    const auto cannon = s::extender(a, runtime::table().vt.cannon_ext);
    return cannon && runtime::read<int>(cannon, 0xC30) == 1 && s::autonomous_vehicle_brain(w.base, a);
}
inline bool permit(const s::World& w, Address actor, Address order) {
    return control.permit(w, actor, order, allowed, stat_resumed);
}
template <class Traversable>
std::optional<XY> nudge_route(const s::World& w, Address a, XY dest, std::optional<XY> tried, Traversable terrain) {
    const auto self = s::pos(a);
    return pick_nudge_point(
        m::xy(self), dest, w.length,
        [&](XY point) {
            auto p = m::at(point, self.z);
            if (!m::valid_point(point) || !terrain(self, p)) return Passage::blocked;
            bool clear = true;
            s::actors(w, [&](Address other) {
                const float radius =
                    ((runtime::read<m::Byte>(a, 0x30) | runtime::read<m::Byte>(other, 0x30)) & 8) ? 3.5f : .75f;
                if (other != a && s::within(p, s::pos(other), radius * w.length)) clear = false;
            });
            if (!clear) return Passage::blocked;
            auto towards = geom::delta(dest, point);
            auto end = m::at(
                geom::offset(point, geom::unit(towards), (std::min)(geom::norm(towards) / w.length, 3.f), 0, w.length),
                p.z);
            return terrain(p, end) ? Passage::onward_clear : Passage::reachable;
        },
        tried);
}
inline std::optional<XY> nudge(const s::World& w, Address a, XY dest, std::optional<XY> tried) {
    s::PathContext path(a);
    return nudge_route(w, a, dest, tried,
                       [&](s::Vec3 from, s::Vec3& to) { return s::ground(w, to) && s::corridor(w, path, from, to); });
}
inline auto probe = &nudge;
} // namespace detail
inline bool occupied(Address a) {
    return detail::control.occupied(a);
}
inline void preempt(Address a) {
    if (auto* r = detail::control.find(a)) {
        if (r->trip.root) ++stat_aborted;
        r->interrupt();
    }
}
inline bool init(Address image, const char* sha) {
    if (!detail::control.init(image, sha, occupied, preempt)) return false;
    stat_nudges = stat_resumed = stat_aborted = 0;
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
    if (old && old->trip.root) {
        if (!old->trip.owns(a)) {
            preempt(a);
            return;
        }
        if (geom::distance(m::xy(s::pos(a)), old->trip.waypoint) <= 1.2f * w.length ||
            m::seconds(w) - old->trip.started >= 6.)
            old->trip.advance(a);
        return;
    }
    const auto move = m::active_move(a);
    const auto self = m::xy(s::pos(a));
    if (!move.order || geom::distance(self, move.dest) < 3.f * w.length) {
        preempt(a);
        return;
    }
    auto& r = control.get(a);
    if (r.observed.order != move.order || r.observed.root != move.root) r.history.interrupt();
    r.observed = move;
    const auto now = m::seconds(w);
    if (!r.history.stalled(self, move.dest, w.length, now, config::active().tuning.movement_stall_s)) return;
    const auto point = probe(w, a, move.dest, r.history.previous_nudge);
    r.history.tried(now, point);
    if (point && control.issue(w, a, r, move, point, permit, allowed)) ++stat_nudges;
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
    unsigned nudges, resumed, aborted;
};
inline Stats snapshot_stats() noexcept {
    return {stat_nudges.load(), stat_resumed.load(), stat_aborted.load()};
}
} // namespace goh::movement_recovery
