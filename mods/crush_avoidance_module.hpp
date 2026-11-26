#pragma once

#include "movement_recovery_module.hpp"
#include "wave5_hooks.hpp"

namespace goh::crush_avoidance {
namespace m = movement_orders;
namespace s = ai_support;
namespace sim = auto_smoke;
namespace geom = movement_geometry;
using runtime::Address;
using runtime::Byte;
using runtime::read;
using runtime::XY;
using s::Vec3;
using s::World;
inline constexpr bool runtime_allowed = false;
inline constexpr unsigned hook_count = 3;
inline std::atomic<unsigned> stat_crush_escapes{}, stat_hazard_priorities{};

namespace detail {
inline Address image{};
inline runtime::HookGate gate{};
inline runtime::HookBatch<hook_count> batch{};
struct Originals {
    void (*update)(Address){};
    Address (*build)(Address){};
    void (*execute)(Address){};
};
inline Originals original{};

struct Lane {
    s::Identity vehicle{};
    XY origin{}, heading{};
    float length{}, radius{};
    bool underfoot{};
    float clearance(XY p) const {
        float along = std::clamp(geom::dot(geom::delta(p, origin), heading), 0.f, length);
        return geom::distance(p, geom::offset(origin, heading, along, 0, 1)) - radius;
    }
};
struct Threats {
    std::array<Lane, 8> lanes{};
    unsigned count{};
    bool overflow{};
};
template <class Passable>
std::optional<XY> choose_escape(const Threats& threats, XY self, float scale, const s::Danger& enemy,
                                Passable passable) {
    if (!threats.count || threats.overflow) return {};
    std::optional<XY> best;
    float best_clear{}, best_enemy{};
    for (unsigned i = 0; i < threats.count; ++i) {
        const Lane& lane = threats.lanes[i];
        for (float back : {0.f, -1.5f}) {
            for (float side : {1.f, -1.f}) {
                auto goal = geom::offset(self, lane.heading, back * scale, side * (lane.radius + 2.f * scale), 1);
                if (!m::valid_point(goal)) continue;
                float margin = lane.clearance(goal);
                for (unsigned j = 0; j < threats.count; ++j)
                    margin = std::min(margin, threats.lanes[j].clearance(goal));
                const float danger_dist = enemy.direction ? geom::distance(goal, m::xy(enemy.towards)) : 0.f;
                if (margin <= 0 ||
                    (best && (margin < best_clear || (margin == best_clear && danger_dist <= best_enemy))))
                    continue;
                if (passable(goal)) {
                    best = goal;
                    best_clear = margin;
                    best_enemy = danger_dist;
                }
            }
        }
    }
    return best;
}
inline std::optional<XY> route(const World& w, Address actor, const Threats& threats) {
    const auto start = s::pos(actor);
    s::Danger enemy{};
    s::observe(w, actor, start, 35.f * w.length, enemy);
    s::PathContext path(actor);
    return choose_escape(threats, m::xy(start), w.length, enemy, [&](XY p) {
        auto end = m::at(p, start.z);
        return s::ground(w, end) && s::corridor(w, path, start, end);
    });
}

inline Vec3 velocity(Address vehicle) {
    Vec3 v{};
    s::native<Vec3* (*)(Address, Vec3*)>(0x73A2A0)(vehicle, &v);
    return v;
}
inline float radius(Address vehicle) {
    const auto get = read<float (*)(Address)>(read<Address>(vehicle), 0x80);
    return get ? get(vehicle) : 0.f;
}
inline Byte excluded_brain(Address b) {
    return s::native<s::Test>(0x91B200)(b);
}
inline Byte excluded_vehicle(Address v) {
    return s::native<s::Test>(0x855D60)(v);
}
inline Address movement_change() {
    return s::native<Address (*)(int, int, int)>(0x90ABF0)(-1, 2, 1);
}
inline Address brain_update() {
    return s::native<Address (*)()>(0x8E2C50)();
}
struct Ops {
    decltype(&s::callback) callback = s::callback;
    decltype(&velocity) motion = velocity;
    decltype(&radius) hull = radius;
    s::Test excluded_brain = detail::excluded_brain;
    s::Test excluded_vehicle = detail::excluded_vehicle;
    decltype(&route) escape = route;
    Address (*movement_change)() = detail::movement_change;
    Address (*brain_update)() = detail::brain_update;
};
inline Ops ops{};

inline bool explicit_attack(Address brain) {
    Address order = read<Address>(brain, 0xF0);
    for (unsigned i = 0; order && i < 32; ++i) {
        if (read<Address>(order) == image + runtime::table().vt.attack_order) return true;
        order = sim::orders::active_child(image, order);
    }
    return order != 0;
}
struct Context {
    World w{};
    Address root{}, brain{}, actor{};
};
inline bool context(Address root, Context& c) {
    if (!gate.active() || !config::active().modules.crush_avoidance ||
        read<Address>(root) != image + runtime::table().vt.root_soldier)
        return false;
    const Address b = read<Address>(root, 0x30), a = read<Address>(b, 0x38);
    if (!ops.callback(a, c.w) || c.w.base != image || !s::entity_scene(c.w)) return false;
    c.root = root;
    c.brain = b;
    c.actor = a;
    return true;
}
inline bool eligible(const Context& c) {
    const Address b = c.brain, a = c.actor, root = c.root;
    if (!s::live_entity(a) || !runtime::actor_ai_allowed(a) || !sim::has_actor(a) || !(read<Byte>(a, 0x30) & 4) ||
        read<Address>(a, 0xDC8) != b || read<Address>(b) != image + runtime::table().vt.human_brain ||
        read<Address>(b, 0xE0) != root ||
        read<Address>(root, 0x3E0) != image + runtime::table().vt.crush_scanner_vehicle ||
        read<Address>(root, 0x3E8) != b || (read<unsigned>(b, 0x398) & 3) > 1 || s::owner(a) >= 18 ||
        !read<std::uint16_t>(a, 0xDAA) || (read<unsigned>(a, 0xDB4) & 0x40100) != 0x100 ||
        (read<unsigned>(a, 0x1C0) & (1u << 28)) || !read<Address>(a, 0xDE0) || !read<Address>(a, 0xDD8) ||
        runtime::is_inhibited_or_direct_control(a, b))
        return false;
    const auto vit = read<Address>(read<Address>(a, 0x578), 0x18);
    return vit && !(read<Byte>(vit, 0x174) & 3) && !explicit_attack(b) && !ops.excluded_brain(b);
}
inline bool scanner_context(Address scanner, Context& c) {
    // This update/build implementation is shared by scanners on other roots.
    if (read<Address>(scanner) != image + runtime::table().vt.crush_scanner_vehicle) return false;
    auto b = read<Address>(scanner, 8), root = read<Address>(b, 0xE0);
    return root && root + 0x3E0 == scanner && context(root, c);
}
inline std::optional<Lane> threat(const Context& c, Address v) {
    const auto& w = c.w;
    if (!v || v == c.actor || !(read<Byte>(v, 0x30) & 8) || !(read<unsigned>(v, 0xDB4) & 0x100) ||
        (read<unsigned>(v, 0x1C0) & (1u << 28)))
        return {};
    const auto self = s::pos(c.actor), origin = s::pos(v);
    if (!sim::finite(self) || !sim::finite(origin) || std::fabs(self.z - origin.z) >= 3.f * w.length ||
        geom::distance(m::xy(self), m::xy(origin)) > 12.f * w.length)
        return {};
    const auto ch = read<Address>(v, 0xDE0);
    if (!ch || read<Address>(ch, 0x10) != v || !read<Address>(v, 0x548) || s::has_destruction_tag(v) ||
        ops.excluded_vehicle(v))
        return {};
    const auto motion = m::xy(ops.motion(v));
    // 0x73A2A0 returns world units per tick, including player-driven reverse motion.
    const float speed = geom::norm(motion) * w.ticks;
    if (!std::isfinite(speed) || speed < 1.2f * w.length) return {};
    const float hull = ops.hull(v);
    if (!std::isfinite(hull) || !(hull > 0)) return {};
    const float margin = sim::allies(w.base, s::owner(c.actor), s::owner(v)) ? .85f : 1.5f;
    Lane lane{s::identity(v),
              m::xy(origin),
              geom::unit(motion),
              std::min(2.5f * speed, 10.f * w.length),
              std::clamp(hull, 1.8f * w.length, 4.5f * w.length) + (margin + .5f) * w.length,
              geom::distance(m::xy(self), m::xy(origin)) <= hull + 1.2f * w.length};
    if (lane.clearance(m::xy(self)) > 0) return {};
    return lane;
}

struct Record {
    Address scanner{}, brain{};
    s::Identity actor{};
    runtime::Tick at{};
    Threats threats{};
    bool armed{};
};
inline store::GrowableTable<Record, 16, 2048> records{};
struct Escape {
    s::Identity actor{};
    Address order{};
    XY goal{};
    runtime::Tick issued{}, cooldown_until{};
};
inline store::GrowableTable<Escape, 16, 2048> escapes{};

inline Escape* escape_state(const Context& c, bool create) {
    Escape* free = nullptr;
    for (auto& st : escapes) {
        if (st.actor.matches(c.actor) && st.actor.mid == read<std::uint16_t>(c.actor, 0xDAA)) return &st;
        if (!free && (!st.order || st.actor.ptr == c.actor || c.w.now - st.issued >= c.w.duration(2.5f))) free = &st;
    }
    if (!create) return nullptr;
    return free ? free : store::try_append_slot(escapes);
}
inline bool keep_escape(const Context& c, const Threats& threats, const Escape* st) {
    if (!st || !st->actor.matches(c.actor) || st->actor.mid != read<std::uint16_t>(c.actor, 0xDAA)) return false;
    bool goal_clear = !threats.overflow, underfoot = false;
    for (unsigned i = 0; i < threats.count; ++i) {
        goal_clear &= threats.lanes[i].clearance(st->goal) > 0;
        underfoot |= threats.lanes[i].underfoot;
    }
    const auto elapsed = c.w.now - st->issued;
    if (read<Address>(c.brain, 0xF0) == st->order && goal_clear && elapsed < c.w.duration(2.5f)) return true;
    return underfoot ? elapsed < c.w.duration(.5f) : static_cast<std::int32_t>(st->cooldown_until - c.w.now) > 0;
}
inline Address last_scene{};
inline runtime::Tick last_tick{};
// Read scene membership once per simulation tick (or an actor-count change).
// Cache only identities. A vehicle can move or disappear between two soldiers'
// turns, so position, velocity and lifetime are checked for each observer.
// Above 512 vehicles the bounded scene actor iterator remains the fallback.
struct VehicleIndex {
    std::array<s::Identity, 512> entries{};
    unsigned used{};
    bool overflow{}, valid{};
    runtime::Tick at{};
    std::size_t scene_count{};
};
inline VehicleIndex vehicles{};
template <class Visit> void visit_vehicles(const World& w, Visit visit) {
    const auto count = s::live_runtime(w) ? read<std::size_t>(last_scene, 0x68)
                                          : (s::span(last_scene, 0x48).end - s::span(last_scene, 0x48).first) / 8;
    if (!vehicles.valid || vehicles.at != w.now || vehicles.scene_count != count) {
        vehicles.used = 0;
        vehicles.overflow = false;
        vehicles.valid = true;
        vehicles.at = w.now;
        vehicles.scene_count = count;
        s::actors(w, [&](Address a) {
            if (!(read<Byte>(a, 0x30) & 8)) return;
            if (vehicles.used < vehicles.entries.size())
                vehicles.entries[vehicles.used++] = s::identity(a);
            else
                vehicles.overflow = true;
        });
    }
    if (vehicles.overflow) return s::actors(w, visit);
    for (unsigned i = 0; i < vehicles.used; ++i)
        if (const auto a = s::resolve(w, vehicles.entries[i])) visit(a);
}
inline void sync(const World& w) {
    const Address scene = s::entity_scene(w);
    if (last_scene != scene || w.now < last_tick) {
        records.clear();
        escapes.clear();
        vehicles.valid = false;
    }
    last_scene = scene;
    last_tick = w.now;
}
inline Record* lookup(Address scanner) {
    for (auto& r : records)
        if (r.scanner == scanner) return &r;
    return nullptr;
}
inline Record* reserve(runtime::Tick now) {
    for (auto& r : records)
        if (!r.scanner || now - r.at > 1) return &r;
    return store::try_append_slot(records);
}
inline bool fresh(const Context& c, const Record* r) {
    return r && r->brain == c.brain && r->actor.mid == read<std::uint16_t>(c.actor, 0xDAA) &&
           r->actor.matches(c.actor) && c.w.now - r->at <= 1;
}
inline void update(Address scanner) {
    runtime::HookGuard guard(gate);
    original.update(scanner);
    Context c{};
    if (!scanner_context(scanner, c)) return;
    sync(c.w);
    auto* row = lookup(scanner);
    if (row) *row = {};
    if (!eligible(c)) return;
    // Native paths retain their normal BuildOrder. Remember extra lanes alongside them
    // so arbitration still sees a manually driven vehicle already underfoot.
    Threats threats{};
    visit_vehicles(c.w, [&](Address v) {
        if (auto lane = threat(c, v)) {
            if (threats.count < threats.lanes.size())
                threats.lanes[threats.count++] = *lane;
            else
                threats.overflow = true;
        }
    });
    if (!threats.count) return;
    if (!row) row = reserve(c.w.now);
    if (!row) return;
    // Native scene enumeration can differ when actors are replaced mid-tick.
    // Preserve a fixed trial order for equal-scoring escape candidates.
    std::sort(threats.lanes.begin(), threats.lanes.begin() + threats.count,
              [](const Lane& a, const Lane& b) { return a.vehicle.id < b.vehicle.id; });
    const auto native = s::span(scanner, 0x18);
    const bool arm = native.first == native.end && !read<Byte>(scanner, 0x10) && !threats.overflow &&
                     !keep_escape(c, threats, escape_state(c, false));
    *row = {scanner, c.brain, s::identity(c.actor), c.w.now, threats, arm};
    if (arm) runtime::write<Byte>(scanner, 0x10, 1);
}
inline Threats current_threats(const Context& c, const Threats& previous) {
    Threats result{};
    result.overflow = previous.overflow;
    for (unsigned i = 0; i < previous.count; ++i) {
        const auto v = s::resolve(c.w, previous.lanes[i].vehicle);
        if (auto lane = threat(c, v)) result.lanes[result.count++] = *lane;
    }
    return result;
}
inline Address sequence(XY goal) {
    auto& order = m::native;
    const auto q = order.sequence();
    if (!q) return 0;
    const std::array<Address, 3> children{ops.movement_change(), order.point(goal), ops.brain_update()};
    if (!children[0] || !children[1] || !children[2]) {
        for (Address child : children)
            order.discard(child);
        order.discard(q);
        return 0;
    }
    for (Address child : children)
        order.push(q, child, 0);
    return q;
}
inline Address build(Address scanner) {
    runtime::HookGuard guard(gate);
    if (!gate.active() || !config::active().modules.crush_avoidance ||
        read<Address>(scanner) != image + runtime::table().vt.crush_scanner_vehicle)
        return original.build(scanner);
    const auto native = s::span(scanner, 0x18);
    if (native.first != native.end || !read<Byte>(scanner, 0x10)) return original.build(scanner);
    Context c{};
    if (!scanner_context(scanner, c)) return original.build(scanner);
    sync(c.w);
    const auto* row = lookup(scanner);
    if (!row || !row->armed || row->brain != c.brain || row->actor.ptr != c.actor) return original.build(scanner);
    // Our own activation with no safe/authorized escape must return null.
    // Stock's empty-vector branch draws RNG. Root checks the nullable result.
    if (!eligible(c) || !fresh(c, row)) return 0;
    auto threats = current_threats(c, row->threats);
    if (!threats.count || threats.overflow) return 0;
    auto* st = escape_state(c, true);
    if (!st || keep_escape(c, threats, st)) return 0;
    const auto goal = ops.escape(c.w, c.actor, threats);
    if (!goal) return 0;
    const auto result = sequence(*goal);
    if (result) {
        *st = {row->actor, result, *goal, c.w.now, c.w.now + c.w.duration(1.5f)};
        ++stat_crush_escapes;
    }
    return result;
}
// The root can be entered directly. Check membership before dereferencing
// the entity borrowed from the last native scanner update.
inline bool live_in_scene(const World& w, Address entity) {
    const auto entities = s::span(s::entity_scene(w), 0x48);
    if (!entity || !entities.valid(sizeof(Address), 131072)) return false;
    for (auto p = entities.first; p != entities.end; p += sizeof(Address))
        if (read<Address>(p) == entity) return s::live_entity(entity);
    return false;
}
inline bool underfoot(const Context& c) {
    const auto pos = s::pos(c.actor);
    const auto span = s::span(c.root, 0x3F8);
    if (!span.valid(sizeof(Address), 64)) return true;
    for (Address p = span.first; p != span.end; p += sizeof(Address)) {
        auto vehicle = read<Address>(p);
        if (!live_in_scene(c.w, vehicle) || s::within(pos, s::pos(vehicle), 3.5f * c.w.length)) return true;
    }
    const auto* r = lookup(c.root + 0x3E0);
    if (fresh(c, r)) {
        if (r->threats.overflow) return true;
        for (unsigned i = 0; i < r->threats.count; ++i) {
            auto v = s::resolve(c.w, r->threats.lanes[i].vehicle);
            if (v && s::within(pos, s::pos(v), 3.5f * c.w.length)) return true;
        }
    }
    return false;
}
struct Restore {
    const Context& c;
    s::Identity actor;
    Address scene;
    Byte burn, crush;
    ~Restore() {
        // The native call can replace the root. Do not write through a retired owner.
        World after{};
        if (!ops.callback(c.actor, after) || s::entity_scene(after) != scene || after.now < c.w.now) return;
        const auto a = s::resolve(after, actor);
        if (!a || read<Address>(a, 0xDC8) != c.brain || read<Address>(c.brain, 0xE0) != c.root) return;
        if (burn) runtime::write<Byte>(c.root, 0x448, burn);
        if (crush) runtime::write<Byte>(c.root, 0x3F0, crush);
    }
};
inline void execute(Address root) {
    runtime::HookGuard guard(gate);
    Context c{};
    if (read<Address>(root) != image + runtime::table().vt.root_soldier || !read<Byte>(root, 0x478) ||
        read<Byte>(root, 0x428) || !context(root, c) || !eligible(c))
        return original.execute(root);
    const auto burn = read<Byte>(root, 0x448), crush = read<Byte>(root, 0x3F0);
    if (!burn && !crush) return original.execute(root);
    const auto bit = read<unsigned>(image, 0x15C6A58);
    if (bit >= 32 || (read<Byte>(c.actor, 0xDAD + bit / 8) & (1u << (bit % 8)))) return original.execute(root);
    // 0x91FFA0 pairs the active byte with the current sensor entity at scanner+0x28.
    const auto grenade = read<Address>(root, 0x490);
    if (!live_in_scene(c.w, grenade)) return original.execute(root);
    sync(c.w);
    const bool mask_crush = crush && s::within(s::pos(c.actor), s::pos(grenade), 6.f * c.w.length) && !underfoot(c);
    if (!burn && !mask_crush) return original.execute(root);
    Restore restore{c, s::identity(c.actor), s::entity_scene(c.w), burn, static_cast<Byte>(mask_crush ? crush : 0)};
    if (burn) runtime::write<Byte>(root, 0x448, 0);
    if (mask_crush) runtime::write<Byte>(root, 0x3F0, 0);
    ++stat_hazard_priorities;
    original.execute(root);
}
} // namespace detail

template <class Create, class Remove> bool prepare_hooks(Address base, const char* sha, Create create, Remove remove) {
    using namespace detail;
    if (!base || !sha || std::strcmp(sha, runtime::kBuild26734796Sha256) != 0 || !batch.empty()) return false;
    const auto& hook = runtime::table().hook;
    if (!batch.add(base, hook.crush_update, update, original.update, create) ||
        !batch.add(base, hook.crush_build, build, original.build, create) ||
        !batch.add(base, hook.root_execute, execute, original.execute, create)) {
        if (batch.rollback(remove)) original = {};
        return false;
    }
    image = base;
    gate.activate();
    return true;
}
inline void deactivate() {
    detail::gate.deactivate();
}
inline unsigned inflight_count() {
    return detail::gate.count();
}
inline void after_hooks_removed() {
    deactivate();
    detail::original = {};
    detail::batch.reset();
    detail::records.release();
    detail::escapes.release();
    detail::vehicles.valid = false;
    detail::last_scene = detail::last_tick = detail::image = 0;
    detail::ops = {};
    stat_crush_escapes = stat_hazard_priorities = 0;
}
} // namespace goh::crush_avoidance
