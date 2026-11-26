#pragma once

// Shared native boundaries for Auto-Medic and Auto-Resupply. See both build notes.
#include "auto_smoke_module.hpp"
#include <array>
#include <string_view>

namespace goh::ai_support {
namespace smoke = goh::auto_smoke;
using smoke::Address;
using smoke::read;
using smoke::Vec3;
using smoke::World;
using Byte = std::uint8_t;
inline constexpr bool runtime_allowed = false;
inline constexpr char executable_sha256[] = "81a13de787c1869b766004cdcadb9ff99012f0b1b58b89db9a7741b10b9f059a";
static_assert(std::string_view(smoke::executable_sha256) == executable_sha256, "Auto-Smoke build mismatch");
inline Address base{};
template <class Fn> inline Fn native(Address rva) {
    return reinterpret_cast<Fn>(base + rva);
}
using Unary = Address (*)(Address);
using Test = Byte (*)(Address);
using BinaryTest = Byte (*)(Address, Address);

inline void (*log_sink)(const char* fmt, ...) = nullptr;
inline std::atomic<bool> accepting{true};

inline bool host() {
    return base && smoke::emitting_host(base);
}
// Construct only at the verified actor slot 23 on the serial simulation path.
inline std::atomic<DWORD> main_tid{};
inline DWORD actor_tls = TLS_OUT_OF_INDEXES;
inline bool serial_thread() {
    return main_tid.load(std::memory_order_relaxed) == GetCurrentThreadId();
}
inline Address current_actor() {
    if (actor_tls == TLS_OUT_OF_INDEXES) return 0;
    DWORD saved = GetLastError();
    auto actor = reinterpret_cast<Address>(TlsGetValue(actor_tls));
    SetLastError(saved);
    return actor;
}
class ActorQuantScope {
    Address previous{};
    bool entered{};

public:
    explicit ActorQuantScope(Address actor) {
        if (!actor || !host() || actor_tls == TLS_OUT_OF_INDEXES) return;
        main_tid.exchange(GetCurrentThreadId(), std::memory_order_relaxed);
        previous = current_actor();
        entered = TlsSetValue(actor_tls, reinterpret_cast<void*>(actor)) != 0;
    }
    ~ActorQuantScope() {
        if (entered) TlsSetValue(actor_tls, reinterpret_cast<void*>(previous));
    }
    ActorQuantScope(const ActorQuantScope&) = delete;
    ActorQuantScope& operator=(const ActorQuantScope&) = delete;
};
inline bool callback(Address actor, World& w) {
    return actor && host() && serial_thread() && current_actor() == actor && smoke::world(base, w);
}
struct Span {
    Address first, end;
    bool valid(std::size_t stride, std::size_t cap) const {
        return end >= first && (first || !end) && (end - first) % stride == 0 && (end - first) / stride <= cap;
    }
};
inline Span span(Address object, unsigned offset) {
    return object ? read<Span>(object, offset) : Span{};
}
inline std::string_view native_string(Address str) {
    auto len = read<std::size_t>(str, 0x10), cap = read<std::size_t>(str, 0x18);
    auto bytes = cap <= 15 ? str : read<Address>(str);
    if (!bytes || !len || len > 1024 || cap < len) return {};
    return {reinterpret_cast<const char*>(bytes), len};
}
inline unsigned owner(Address actor) {
    return read<Byte>(actor, 0xDA8);
}
inline Vec3 pos(Address entity) {
    return read<Vec3>(entity, 0x74);
}
inline bool within(Vec3 a, Vec3 b, float radius) {
    return radius >= 0 && std::isfinite(radius) && smoke::distance2(a, b) <= radius * radius;
}
struct Identity {
    Address ptr{}, definition{};
    unsigned id{};
    std::uint16_t mid{};
    bool matches(Address live) const {
        return live && live == ptr && read<unsigned>(live, 0xC0) == id && read<Address>(live, 0xB0) == definition;
    }
};
inline Identity identity(Address a) {
    const std::uint16_t mid = smoke::has_actor(a) ? read<std::uint16_t>(a, 0xDAA) : 0;
    return {a, read<Address>(a, 0xB0), read<unsigned>(a, 0xC0), mid};
}
inline bool live_entity(Address a) {
    return a && (read<Byte>(a, 0x30) & 1) && !(read<unsigned>(a, 0x38) & 0x10A20u) &&
           read<unsigned>(a, 0xC0) != 0xffffffffu;
}
inline bool live_runtime(const World& w) {
    return w.base != 0 && actor_tls != TLS_OUT_OF_INDEXES;
}
inline Address entity_scene(const World& w) {
    // 0x15C9668 (w.scene) is eEnvironment (terrain at +0x18); 0x15C7D58 is the
    // real eScene whose entity vector lives at +0x48..+0x50 (0xA39C70) and actor
    // map lives at +0x60..+0x68 (0x490670 / 0xA7C990). Offline fixtures have
    // actor_tls == TLS_OUT_OF_INDEXES and pass a synthetic eScene in w.scene.
    return live_runtime(w) ? read<Address>(w.base, 0x15C7D58) : w.scene;
}
template <class Visit> inline void entities(const World& w, Visit visit) {
    const Span v = span(entity_scene(w), 0x48);
    if (!v.valid(8, 131072)) return;
    for (auto p = v.first; p != v.end; p += 8) {
        const Address a = read<Address>(p);
        if (live_entity(a)) visit(a);
    }
}
template <class Visit> inline void actors(const World& w, Visit visit) {
    const Address es = entity_scene(w);
    if (!es) return;
    if (!live_runtime(w)) {
        entities(w, [&](Address a) {
            if (smoke::has_actor(a)) visit(a);
        });
        return;
    }
    const Address head = read<Address>(es, 0x60);
    const std::size_t count = read<std::size_t>(es, 0x68);
    if (!head || !count || count > 16384) return;
    Address node = read<Address>(head, 0);
    std::size_t visited = 0;
    while (node && node != head && !read<Byte>(node, 0x19) && visited++ < count) {
        const Address a = read<Address>(node, 0x28);
        if (live_entity(a) && smoke::has_actor(a)) visit(a);
        Address right = read<Address>(node, 0x10);
        if (right && !read<Byte>(right, 0x19)) {
            node = right;
            for (unsigned depth = 0; depth < 64; ++depth) {
                Address left = read<Address>(node, 0);
                if (!left || read<Byte>(left, 0x19)) break;
                node = left;
            }
        } else {
            Address parent = read<Address>(node, 8);
            for (unsigned depth = 0;
                 parent && !read<Byte>(parent, 0x19) && node == read<Address>(parent, 0x10) && depth < 64; ++depth) {
                node = parent;
                parent = read<Address>(node, 8);
            }
            node = parent;
        }
    }
}
inline Address resolve(const World& w, Identity key) {
    if (!key.ptr) return 0;
    // Fast O(log N) actor lookup through eScene+0x60 via 0xA7C990 when mid is known.
    if (live_runtime(w) && key.mid && read<Address>(w.base, 0x15C7D58)) {
        const auto a = native<Address (*)(const std::uint16_t*)>(0xA7C990)(&key.mid);
        return (a == key.ptr && live_entity(a) && key.matches(a)) ? a : 0;
    }
    // Resolve non-actor entities (or offline fixtures) through eScene+0x48.
    const Span v = span(entity_scene(w), 0x48);
    if (v.valid(8, 131072))
        for (auto p = v.first; p != v.end; p += 8) {
            const auto a = read<Address>(p);
            if (a == key.ptr && live_entity(a) && key.matches(a)) return a;
        }
    return 0;
}
inline bool policy_allows(unsigned policy, unsigned flags, bool medic) {
    const auto ctrl = policy & 3u;
    const auto fire = (policy >> 10) & 3u;
    const auto move = (policy >> 12) & 3u;
    // Allow AI (0) and player-owned (1) units across Move Free (0), Hold Position (1, with
    // return-to-post), and Move in Zone (2); reject invalid move mode (3), Hold Fire (fire==1,
    // when firing/medic), Direct Control/inhibition (bits 0..5), dispatch inhibitor (bit 20),
    // and active user movement tasks (bit 28).
    // Do NOT mask bit 24 (0x01000000): the engine sets bit 24 while inside any
    // current-order Exec callback (free_slot checks bit 24 separately).
    return ctrl <= 1u && move <= 2u && !(flags & 0x1010003fu) && (!medic || fire == 0u || fire == 2u);
}
inline bool hold_position(Address brain) {
    return brain && ((read<unsigned>(brain, 0x398) >> 12) & 3u) == 1u;
}
inline bool donor_policy_allows(unsigned policy, unsigned flags) {
    const auto ctrl = policy & 3u;
    // Passive smoke donors never move or fire; allow Hold Position / Hold Fire
    // while strictly rejecting Direct Control, inhibition, dispatch block, and active user orders.
    return ctrl <= 1u && !(flags & 0x1010003fu);
}
template <class Policy> inline bool autonomous_if(Address actor, bool human_only, Policy permits) {
    if (!live_entity(actor) || !smoke::has_actor(actor) || owner(actor) >= 18 || !read<std::uint16_t>(actor, 0xDAA) ||
        !(read<unsigned>(actor, 0xDB4) & 0x100) || (read<unsigned>(actor, 0x1C0) & (1u << 28)))
        return false;
    const Address b = read<Address>(actor, 0xDC8);
    if (!b || !read<Address>(actor, 0x578) || read<Address>(b, 0x38) != actor) return false;
    const auto vt = read<Address>(b);
    if (vt != base + 0x110FF88 && (human_only || vt != base + 0x101FD68)) return false;
    if (!(read<Byte>(actor, 0x30) & (vt == base + 0x110FF88 ? 4u : 8u))) return false;
    const Address ch = read<Address>(actor, 0xDE0), wp = read<Address>(actor, 0xDD8);
    if (!permits(read<unsigned>(b, 0x398), read<unsigned>(b, 0x39C))) return false;
    if (!ch || !wp || read<int>(ch, 0x18) || (read<Byte>(wp, 0x70) & 2) || read<int>(b, 0x88) ||
        read<Address>(b, 0x98) != read<Address>(b, 0xA0))
        return false;
    if (vt == base + 0x110FF88) {
        const auto vit = read<Address>(read<Address>(actor, 0x578), 0x18);
        if (!vit || (read<unsigned>(vit, 0x174) & 3)) return false;
        const Address root = read<Address>(b, 0xE0);
        if (!root || read<Address>(root) != base + 0x1110CD0 || read<Address>(root, 0x30) != b ||
            read<int>(root, 0x4C0) || read<Address>(root, 0x568))
            return false;
    }
    return true;
}
inline bool autonomous(Address actor, bool medic, bool passive_donor = false) {
    return autonomous_if(actor, medic, [=](unsigned pol, unsigned flags) {
        return passive_donor ? donor_policy_allows(pol, flags) : policy_allows(pol, flags, medic);
    });
}
inline bool free_slot(Address actor) {
    const Address b = read<Address>(actor, 0xDC8);
    if (read<unsigned>(b, 0x39C) & 0x01000000u) return false;
    auto o = read<Address>(b, 0xF0);
    return !o || (read<Address>(o) == base + 0x110D490 && read<Address>(o, 8) == actor + 0xAF0 &&
                  !(read<unsigned>(o, 0x20) & 7) && !read<Address>(o, 0x48) && !read<Address>(o, 0x50));
}
inline bool current_order(Address actor, Address order) {
    auto root = read<Address>(read<Address>(actor, 0xDC8), 0xF0);
    return order && (root == order ||
                     (root && read<Address>(root) == base + 0x110D490 && read<Address>(root, 8) == actor + 0xAF0 &&
                      read<Address>(root, 0x48) == 1 && smoke::orders::active_child(base, root) == order));
}
inline Address manager(Address actor) {
    const auto agg = read<Address>(actor, 0x578);
    return agg ? read<Address>(agg, 0x28) : 0;
}
inline Address box(Address actor) {
    auto m = manager(actor), b = m ? read<Address>(m, 0xA0) : 0;
    return b && read<Address>(b, 0x50) == actor ? b : 0;
}
inline bool inventory_free(Address actor) {
    auto m = manager(actor);
    return m && !read<Address>(m, 0xC8) && read<Address>(m, 0xA8) == read<Address>(m, 0xB0);
}
inline bool tag(Span rows, const char* name) {
    if (rows.valid(0x60, 4096))
        for (auto p = rows.first; p != rows.end; p += 0x60)
            if (smoke::SmokeTagEquals(p, name)) return true;
    return false;
}
inline bool working_hull(Address actor) {
    auto parts = span(actor, 0xB90);
    if (!parts.valid(0x28, 4096)) return false;
    bool working = false;
    for (auto p = parts.first; p != parts.end; p += 0x28) {
        auto def = read<Address>(p);
        if (!def || read<Address>(p, 8) != actor) return false;
        auto keys = span(def, 0x328);
        if (!keys.valid(0x60, 4096)) return false;
        if (keys.first != keys.end && native_string(keys.first + 0x40) == "body") working = !read<Byte>(p, 0x24);
    }
    return working;
}

struct PathContext {
    alignas(16) std::byte bytes[0xF0]{};
    explicit PathContext(Address actor) {
        native<void* (*)(void*, Address)>(0x8DF010)(bytes, actor);
    }
    ~PathContext() {
        native<void (*)(void*)>(0xBF640)(bytes);
    }
    PathContext(const PathContext&) = delete;
    PathContext& operator=(const PathContext&) = delete;
};
// Pre-clamp each short segment so the native terrain sampler skips its jitter branch.
inline bool corridor(const World& w, PathContext& path, Vec3 start, Vec3 end) {
    if (!smoke::finite(start) || !smoke::finite(end)) return false;
    const float length = std::sqrt(smoke::length2_xy(start, end));
    if (!(length <= 4096)) return false;
    using Clamp = Byte (*)(void*, Vec3*);
    using Block = Byte (*)(void*, const Vec3*, const Vec3*, Address);
    const int steps = 1 + static_cast<int>(length / 12.f);
    Vec3 last = start;
    if (native<Clamp>(0x8DF4D0)(path.bytes, &last)) return false;
    const auto terrain = read<Address>(w.scene, 0x18);
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / steps;
        Vec3 next{start.x + (end.x - start.x) * t, start.y + (end.y - start.y) * t, end.z};
        float xy[]{next.x, next.y};
        if (native<Clamp>(0x8DF4D0)(path.bytes, &next) || !smoke::dry_ground(base, terrain, xy) ||
            !(smoke::length2_xy(last, next) < 16.f * 16.f) || native<Block>(0x8C19E0)(path.bytes, &last, &next, 0))
            return false;
        last = next;
    }
    return true;
}
inline bool ground(const World& w, Vec3& p) {
    float xy[]{p.x, p.y};
    auto terrain = read<Address>(w.scene, 0x18);
    if (!smoke::finite(p) || !smoke::dry_ground(base, terrain, xy)) return false;
    p.z = native<float (*)(Address, const float*, int)>(0xA5D440)(terrain, xy, 0);
    return smoke::finite(p);
}
template <class Accept>
inline bool approach(const World& w, Address actor, Vec3 center, float radius, Vec3& goal, Accept accept) {
    const Vec3 self = pos(actor);
    float dx = self.x - center.x, dy = self.y - center.y, len = std::hypot(dx, dy);
    if (!std::isfinite(len) || !(radius > 0)) return false;
    if (within(self, center, radius)) {
        goal = self;
        return true;
    }
    if (len > 0) {
        dx /= len;
        dy /= len;
    } else {
        dx = 1;
        dy = 0;
    }
    constexpr float turns[] = {0, .7853982f, -.7853982f, 1.5707963f, -1.5707963f, 3.1415927f};
    std::array<Vec3, std::size(turns)> endpoints{};
    std::size_t count{};
    PathContext path(actor);
    for (float angle : turns) {
        float c = std::cos(angle), s = std::sin(angle);
        Vec3 p{center.x + radius * .8f * (dx * c - dy * s), center.y + radius * .8f * (dx * s + dy * c), center.z};
        if (!ground(w, p) || !within(p, center, radius) || !accept(p)) continue;
        if (corridor(w, path, self, p)) {
            goal = p;
            return true;
        }
        endpoints[count++] = p;
    }
    if (!count) return false;
    // Try bounded detours around a short obstacle. The native move order still
    // receives only the destination and owns the actual route.
    const Vec3 first_endpoint = endpoints.front();
    for (float side : {3.f, -3.f, 7.f, -7.f}) {
        Vec3 via{(self.x + first_endpoint.x) * .5f - side * w.length * dy,
                 (self.y + first_endpoint.y) * .5f + side * w.length * dx, (self.z + first_endpoint.z) * .5f};
        if (!ground(w, via) || !corridor(w, path, self, via)) continue;
        for (std::size_t i = 0; i < count; ++i)
            if (corridor(w, path, via, endpoints[i])) {
                goal = endpoints[i];
                return true;
            }
    }
    return false;
}

inline bool approach(const World& w, Address actor, Vec3 center, float radius, Vec3& goal) {
    return approach(w, actor, center, radius, goal, [](Vec3) { return true; });
}
using Occupied = bool (*)(Address);
inline Occupied medical_assignment{}, supply_assignment{};

struct Danger {
    bool observed{}, unsafe{};
    Vec3 towards{};
    bool direction{};
};
// Use last observed positions. Incoming fire on an observer near the
// destination also vetoes a trip when the shooter's position is outside it.
inline void observe(const World& w, Address observer, Vec3 point, float radius, Danger& d) {
    if (!(read<unsigned>(observer, 0xDB4) & 0x100)) return;
    if (read<Byte>(observer, 0x30) & 4) {
        auto agg = read<Address>(observer, 0x578);
        auto vit = agg ? read<Address>(agg, 0x18) : 0;
        if (!vit || (read<unsigned>(vit, 0x174) & 3)) return;
    }
    const auto sen = read<Address>(observer, 0xDD0);
    if (!sen || read<Address>(sen, 0x260) != observer) return;
    const Span subjects = span(sen, 0x280);
    if (!subjects.valid(8, 4096)) return;
    const bool nearby = within(pos(observer), point, radius);
    d.observed |= nearby;
    if (nearby && smoke::recent(w, read<unsigned>(sen, 0x2F4), 4)) d.unsafe = true;
    for (auto p = subjects.first; p != subjects.end; p += 8) {
        const auto sub = read<Address>(p);
        if (!sub || read<Byte>(sub, 0x6C)) continue;
        auto rel = read<unsigned>(sub, 0x14), f = read<unsigned>(sub, 0x10);
        if ((rel & 0x1e) != 2 || !(f & (1u << 28))) continue;
        const Vec3 seen = read<Vec3>(sub, 0x28);
        bool hit = (rel & 0x180) && smoke::recent(w, read<unsigned>(sub, 0x68), 4);
        if ((nearby && hit) ||
            (((f & (1u << 29)) || smoke::recent(w, read<unsigned>(sub, 0x64), 5)) && within(seen, point, radius))) {
            d.unsafe = true;
            if (smoke::finite(seen) &&
                (!d.direction || smoke::distance2(seen, point) < smoke::distance2(d.towards, point))) {
                d.direction = true;
                d.towards = seen;
            }
        }
    }
}
inline Danger danger(const World& w, Address actor, Vec3 point, float metres) {
    Danger d{};
    observe(w, actor, point, metres * w.length, d);
    actors(w, [&](Address ally) {
        if (ally != actor && smoke::allies(base, owner(actor), owner(ally)))
            observe(w, ally, point, metres * w.length, d);
    });
    return d;
}
// Return-to-post safety: allow returning to a frontline Hold Position during nearby
// firefights, but abort if a visible/recently seen enemy is actually on top of the post.
inline bool hostile_near(const World& w, Address observer, Vec3 point, float radius) {
    if (!(read<unsigned>(observer, 0xDB4) & 0x100)) return false;
    if (read<Byte>(observer, 0x30) & 4) {
        auto agg = read<Address>(observer, 0x578);
        auto vit = agg ? read<Address>(agg, 0x18) : 0;
        if (!vit || (read<unsigned>(vit, 0x174) & 3)) return false;
    }
    const auto sen = read<Address>(observer, 0xDD0);
    if (!sen || read<Address>(sen, 0x260) != observer) return false;
    const Span subjects = span(sen, 0x280);
    if (!subjects.valid(8, 4096)) return false;
    for (auto p = subjects.first; p != subjects.end; p += 8) {
        const auto sub = read<Address>(p);
        if (!sub || read<Byte>(sub, 0x6C)) continue;
        auto rel = read<unsigned>(sub, 0x14), f = read<unsigned>(sub, 0x10);
        if ((rel & 0x1e) != 2 || !(f & (1u << 28))) continue;
        const Vec3 seen = read<Vec3>(sub, 0x28);
        if (((f & (1u << 29)) || smoke::recent(w, read<unsigned>(sub, 0x64), 5)) && within(seen, point, radius))
            return true;
    }
    return false;
}
inline bool post_overrun(const World& w, Address actor, Vec3 point, float metres) {
    const float r = metres * w.length;
    if (hostile_near(w, actor, point, r)) return true;
    bool overrun = false;
    actors(w, [&](Address ally) {
        if (!overrun && ally != actor && smoke::allies(base, owner(actor), owner(ally)) &&
            hostile_near(w, ally, point, r))
            overrun = true;
    });
    return overrun;
}
inline bool return_goal(const World& w, Address actor, Vec3 home, Vec3& goal) {
    const bool is_vehicle = (read<Byte>(actor, 0x30) & 8) != 0;
    auto clear_spot = [&](Vec3 point) {
        bool clear = true;
        actors(w, [&](Address other) {
            if (!clear || other == actor) return;
            const bool other_veh = (read<Byte>(other, 0x30) & 8) != 0;
            const float sep = (is_vehicle && other_veh) ? 4.5f : (other_veh ? 3.5f : (is_vehicle ? 0.f : 0.5f));
            if (sep > 0.f && within(pos(other), point, sep * w.length)) clear = false;
        });
        return clear;
    };
    Vec3 exact = home;
    if (ground(w, exact) && clear_spot(exact)) {
        PathContext path(actor);
        if (corridor(w, path, pos(actor), exact)) {
            goal = exact;
            return true;
        }
    }
    if (approach(w, actor, home, (is_vehicle ? 4.5f : 2.f) * w.length, goal, clear_spot)) return true;
    // Home is a spot this unit stood on seconds ago; if around a building corner beyond
    // corridor's short detour probes, let native Order::eMove A* route back to it.
    if (ground(w, exact) && clear_spot(exact)) {
        goal = exact;
        return true;
    }
    return false;
}

// Keep ownership independent of the engine's replaceable external listener.
using Permit = bool (*)(const World&, Address, Address);
struct Watched {
    std::atomic<Address> order{};
    Permit permit{};
    bool skipped_start{};
    std::uint64_t generation{};
};
struct OrderRef {
    Watched* watch{};
    std::uint64_t generation{};
    Address get() const {
        return watch && watch->generation == generation ? watch->order.load() : 0;
    }
    void abandon() {
        if (get()) watch->permit = nullptr;
        *this = {};
    }
};
inline std::uint64_t next_generation{};
inline std::array<Watched, 512> watches{};
inline std::atomic<unsigned> watched_count{};
using Exec = int (*)(Address, int);
using Destroy = Address (*)(Address, unsigned);
inline Exec orig_move{}, orig_medic{};
inline Destroy orig_destroy_move{}, orig_destroy_medic{};
inline Watched* tracked(Address order) {
    if (order && watched_count.load())
        for (auto& slot : watches)
            if (slot.order.load() == order) return &slot;
    return nullptr;
}
inline int guarded(Exec orig, Address order, int phase) {
    auto* watch = tracked(order);
    if (!watch) return orig(order, phase);
    if (!serial_thread() || !host()) return phase == 2 ? 1 : 2;
    if (phase == 2) return watch->skipped_start ? 1 : orig(order, phase);
    const auto ordered = read<Address>(order, 8);
    const auto actor = ordered ? ordered - 0xAF0 : 0;
    World w{};
    if (!actor || !accepting.load(std::memory_order_acquire) || !callback(actor, w) || !watch->permit ||
        !watch->permit(w, actor, order)) {
        if (phase == 0) watch->skipped_start = true;
        watch->permit = nullptr;
        return 2;
    }
    if (phase == 0) watch->skipped_start = false;
    return orig(order, phase);
}
inline int move_exec(Address o, int ph) {
    return guarded(orig_move, o, ph);
}
inline int medic_exec(Address o, int ph) {
    return guarded(orig_medic, o, ph);
}
inline Address destroy(Destroy orig, Address order, unsigned flags) {
    if (auto* watch = tracked(order)) {
        if (watch->order.exchange(0) == order) --watched_count;
    }
    return orig(order, flags);
}
inline Address destroy_move(Address o, unsigned f) {
    return destroy(orig_destroy_move, o, f);
}
inline Address destroy_medic(Address o, unsigned f) {
    return destroy(orig_destroy_medic, o, f);
}
inline void discard(Address o) {
    if (o) read<Address (*)(Address, unsigned)>(read<Address>(o))(o, 1);
}
inline bool install(Address actor, Address fresh, Permit permit, OrderRef& owned) {
    if (!fresh) return false;
    if (free_slot(actor))
        for (auto& watch : watches)
            if (!watch.order.load()) {
                if (!++next_generation) ++next_generation;
                watch.permit = permit;
                watch.skipped_start = false;
                watch.generation = next_generation;
                watch.order.store(fresh);
                ++watched_count;
                owned = {&watch, next_generation};
                const auto brain = read<Address>(actor, 0xDC8), queue = read<Address>(brain, 0xF0);
                if (queue)
                    native<Address (*)(Address, Address, int, Byte)>(0x919270)(queue, fresh, 0, 1);
                else
                    native<void (*)(Address, Address)>(0x91DB30)(brain, fresh);
                return true;
            }
    discard(fresh);
    return false;
}
inline bool move(Address actor, Vec3 goal, Permit permit, OrderRef& owned) {
    if (!smoke::finite(goal) || (std::fabs(goal.x) < 0.01f && std::fabs(goal.y) < 0.01f)) return false;
    const float xy[]{goal.x, goal.y};
    return install(actor, native<Address (*)(const float*, unsigned)>(0x90A8C0)(xy, 0), permit, owned);
}
template <class Create, class Remove>
inline bool prepare_hooks(Address image, const char* digest, bool stock, Create create, Remove remove) {
    if (base || !image || !stock || !digest || std::strcmp(digest, executable_sha256) != 0) return false;
    actor_tls = TlsAlloc();
    if (actor_tls == TLS_OUT_OF_INDEXES) return false;
    std::array<Address, 4> created{};
    std::size_t count = 0;
    auto add = [&](Address rva, auto hook, auto& orig) {
        void* p = nullptr;
        if (create(reinterpret_cast<void*>(image + rva), reinterpret_cast<void*>(hook), &p)) return false;
        created[count++] = image + rva;
        orig = reinterpret_cast<decltype(hook)>(p);
        return true;
    };
    if (!add(0x90E3F0, &move_exec, orig_move) || !add(0x378710, &medic_exec, orig_medic) ||
        !add(0xBB490, &destroy_move, orig_destroy_move) || !add(0x379CA0, &destroy_medic, orig_destroy_medic)) {
        while (count)
            remove(reinterpret_cast<void*>(created[--count]));
        TlsFree(actor_tls);
        actor_tls = TLS_OUT_OF_INDEXES;
        return false;
    }
    base = image;
    accepting.store(true, std::memory_order_release);
    return true;
}
inline bool quiescent() {
    return watched_count.load() == 0;
}
inline void after_hooks_removed() {
    accepting.store(false, std::memory_order_release);
    if (actor_tls != TLS_OUT_OF_INDEXES) TlsFree(actor_tls);
    actor_tls = TLS_OUT_OF_INDEXES;
    main_tid = 0;
    base = 0;
    orig_medic = nullptr;
    orig_move = nullptr;
    orig_destroy_move = nullptr;
    orig_destroy_medic = nullptr;
    medical_assignment = nullptr;
    supply_assignment = nullptr;
    log_sink = nullptr;
}
} // namespace goh::ai_support
