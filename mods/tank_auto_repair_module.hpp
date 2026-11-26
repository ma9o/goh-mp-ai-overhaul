#pragma once

#include "ai_support_26734796.hpp"
#include <algorithm>

namespace goh::tank_auto_repair {
namespace sim = goh::auto_smoke;
namespace support = goh::ai_support;
using sim::Address;
inline constexpr const char* executable_sha256 = runtime::kBuild26734796Sha256;
static_assert(std::string_view(sim::executable_sha256) == executable_sha256);
inline constexpr bool runtime_allowed = false;
inline void (*log_sink)(const char* fmt, ...)
#if defined(__MINGW_PRINTF_FORMAT)
    __attribute__((format(__MINGW_PRINTF_FORMAT, 1, 2)))
#endif
    = nullptr;
inline std::atomic<unsigned> stat_dispatch_full{}, stat_dispatch_turret_track{}, stat_crew_emitted{},
    stat_excrew_assigned{}, stat_repair_chained{}, stat_reboard_queued{}, stat_picker_overrides{},
    stat_nontank_dispatch{};

namespace detail {
using sim::read;
using sim::World;
using support::Span;
using support::span;
using Byte = std::uint8_t;
using Tick = std::uint32_t;
inline Address base{};
inline Address native_address(Address image, Address rva) {
    return image + rva;
}
inline auto resolve_native = &native_address;
template <class Fn> inline Fn native(Address rva) {
    return reinterpret_cast<Fn>(resolve_native(base, rva));
}
constexpr Address human_vt = 0x110FF88, vehicle_vt = 0x101FD68, smart_vt = 0x10225B0, repair_vt = 0x110D0E8,
                  board_vt = 0x110B590, exchange_vt = 0x10224F8, avoid_vt = 0x110D278, sequence_vt = 0x110D490;
inline std::atomic<bool> active{};
inline std::atomic<int> inflight{};
inline std::atomic<DWORD> serial{};
inline bool rollback_pending{};
struct Guard {
    Guard() {
        runtime::global_inflight.fetch_add(1, std::memory_order_acq_rel);
        ++inflight;
    }
    ~Guard() {
        --inflight;
        runtime::global_inflight.fetch_sub(1, std::memory_order_acq_rel);
    }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
};

struct Key {
    support::Identity entity{};
    std::uint16_t route{};
    explicit operator bool() const {
        return entity.ptr != 0;
    }
    bool matches(Address a) const {
        return entity.matches(a) && read<std::uint16_t>(a, 0xDAA) == route;
    }
};
inline Key key(Address actor) {
    return {support::identity(actor), read<std::uint16_t>(actor, 0xDAA)};
}
inline Address resolve(const Key& k) {
    if (!k || !k.route || !read<Address>(base, 0x15C7D58)) return 0;
    const auto a = native<Address (*)(const std::uint16_t*)>(0xA7C990)(&k.route);
    return k.matches(a) ? a : 0;
}
inline bool pending(Tick deadline, Tick now) {
    return deadline && static_cast<std::int32_t>(deadline - now) > 0;
}
enum class Scenario : Byte {
    none,
    full,
    turret_track
};
struct Crew {
    Key key{};
    short seat{-1};
    Byte role{}, player{};
    bool assigned{}, needs_board{}, started{}, boarding{}, dispatched{};
    Address smart{}, exit_callback{};
    Tick retry{};
};
constexpr std::size_t crew_cap = 32;
struct Tank {
    Key key{};
    std::array<Crew, crew_cap> crew{};
    Scenario scenario{};
    Byte player{};
    bool is_tank{}, serviced{}, announced{}, body_was_broken{};
    Tick last_tick{}, death_until{}, ejection_until{};
};
// Short actor IDs route callbacks in O(1). Every hint is checked against the live triple.
struct Member {
    std::uint16_t tank{}, slot{};
};
inline store::GrowableTable<Tank, 64, 4096> tanks{};
inline store::SparseTable<Member, std::uint16_t> members{};
struct Override {
    support::Identity actor{};
    Tick deadline{};
};
inline store::SparseTable<Override, std::uint16_t> overrides{};
inline Address last_scene{};
inline Tick last_clock{};
inline bool coordinating{};
inline void reset() {
    tanks.clear();
    members.clear();
    overrides.clear();
    last_scene = 0;
    last_clock = 0;
}
inline bool serial_world(World& w) {
    return active.load() && serial.load() == GetCurrentThreadId() && sim::emitting_host(base) && sim::world(base, w) &&
           w.scene == last_scene && w.now >= last_clock;
}
inline Tank* find(Address actor) {
    for (auto& t : tanks)
        if (t.key.matches(actor)) return &t;
    return nullptr;
}
inline bool enabled(bool is_tank) {
    const auto& modules = config::active().modules;
    return is_tank ? modules.tank_auto_repair : modules.nontank_auto_repair;
}
inline Crew* member(Address actor, Tank*& tank) {
    if (!actor || !runtime::actor_ai_allowed(actor)) return nullptr;
    const auto* h = members.find(read<std::uint16_t>(actor, 0xDAA));
    if (!h || !h->tank || h->tank > tanks.size()) return nullptr;
    auto& t = tanks[h->tank - 1];
    auto& c = t.crew[h->slot];
    if (!t.key || !enabled(t.is_tank) || !config::player_ai_enabled(t.player) || !c.key.matches(actor)) return nullptr;
    tank = &t;
    return &c;
}
inline bool has_ability(Address a, Address index) {
    const auto bit = read<unsigned>(base, index);
    return bit < 32 && (read<Byte>(a, 0xDAD + bit / 8) & (1u << (bit % 8)));
}
// Seated crew inside closed-top (no linkbone) or sealed tank places have
// entity+0x38 bit 9 (0x200, INACTIVE) set by ePlacer::Include until Exclude.
inline bool has_seated_actor(Address a) {
    return a && (read<Byte>(a, 0x30) & 2) && !(read<unsigned>(a, 0x38) & 0x820u);
}
inline Address brain_of(Address a, Address table, bool allow_seated = false) {
    if (!(allow_seated ? has_seated_actor(a) : sim::has_actor(a))) return 0;
    const auto b = read<Address>(a, 0xDC8);
    return b && read<Address>(b) == base + table && read<Address>(b, 0x38) == a ? b : 0;
}
inline bool manual(Address a, Address b) {
    const auto ch = read<Address>(a, 0xDE0), wp = read<Address>(a, 0xDD8);
    return !ch || !wp || read<Address>(ch, 0x10) != a || read<unsigned>(ch, 0x18) || (read<Byte>(wp, 0x70) & 2) ||
           (read<unsigned>(b, 0x39C) & 0x3f);
}
// Emit belongs to the vehicle. An observed occupied seat may carry the single
// native placement inhibition until Exclude. Never issue an order to that brain.
inline bool available_seat(Address human, Address tank, Address b) {
    if (!runtime::actor_ai_allowed(human)) return false;
    const auto ch = read<Address>(human, 0xDE0), wp = read<Address>(human, 0xDD8);
    const auto flags = read<unsigned>(b, 0x39C), policy = read<unsigned>(b, 0x398);
    if (!ch || !wp || read<Address>(ch, 0x10) != human || read<unsigned>(ch, 0x18) || (read<Byte>(wp, 0x70) & 2) ||
        (flags & (1u << 28)) || (policy & 3) > 1 || ((policy >> 12) & 3) > 2)
        return false;
    if (!(flags & 0x3f)) return true;
    const auto link = read<Address>(human, 0x560);
    return (flags & 0x3f) == 1 && (flags & (1u << 19)) && link && read<Address>(link, 8) == tank;
}
inline bool overridden(Address a, Address b) {
    return manual(a, b) || (read<unsigned>(b, 0x39C) & (1u << 28));
}
inline unsigned vitality(Address a) {
    const auto agg = read<Address>(a, 0x578), vit = agg ? read<Address>(agg, 0x18) : 0;
    return vit ? read<Byte>(vit, 0x174) & 3 : 3;
}
inline bool human_policy(Address a) {
    if (!runtime::actor_ai_allowed(a)) return false;
    const auto b = brain_of(a, human_vt);
    if (!b || vitality(a) || overridden(a, b)) return false;
    const auto mode = read<unsigned>(b, 0x398);
    if ((mode & 3) > 1 || ((mode >> 12) & 3) > 2) return false;
    auto o = read<Address>(b, 0xF0);
    for (unsigned depth = 0; o && depth < 32; ++depth) {
        if (read<Address>(o) == base + avoid_vt) return false;
        o = sim::orders::active_child(base, o);
    }
    return !o;
}
inline bool safety(Address a) {
    if (!runtime::actor_ai_allowed(a)) return false;
    const auto b = brain_of(a, vehicle_vt);
    if (!b || manual(a, b) || (read<unsigned>(b, 0x398) & 3) > 1 || !has_ability(a, 0x15ABBB0) ||
        has_ability(a, 0x15C6A58) || has_ability(a, 0x15C6A5C) || !(read<float>(a, 0x70) >= 0.34202014f))
        return false;
    const auto tags = span(a, 0x4E8);
    const auto def = read<Address>(a, 0x548);
    if (!def || !tags.valid(0x60, 4096)) return false;
    for (const char* name : {"destroyed", "exploded", "abandoned", "no_auto_repair"})
        if (support::tag(tags, name) || support::tag(span(def, 0x330), name)) return false;
    const bool is_tank = support::tank_class(a);
    if (!enabled(is_tank)) return false;
    if (is_tank) return true;
    if (!(read<Byte>(a, 0x30) & 8) || support::owner(a) >= 18 || (read<unsigned>(a, 0x1C0) & (1u << 28)) ||
        (read<unsigned>(b, 0x39C) & (1u << 28)) || (!(read<unsigned>(a, 0xDB4) & 0x100) && !find(a)) ||
        native<support::Test>(runtime::table().fn.excluded_vehicle)(a))
        return false;
    const auto cannon = support::extender(a, runtime::table().vt.cannon_ext, base);
    return !cannon || !read<int>(cannon, 0xC30);
}
inline std::string_view component_name(Address def) {
    const auto names = span(def, 0x328);
    return names.valid(0x60, 4096) && names.first != names.end ? support::native_string(names.first + 0x40)
                                                               : std::string_view{};
}
inline bool gun(std::string_view n) {
    return n == "gun" || (n.size() == 4 && n.substr(0, 3) == "gun" && n[3] >= '1' && n[3] <= '6');
}
inline bool turret(std::string_view n) {
    return n == "turret" || (n.size() == 11 && n.substr(0, 10) == "aux_turret" && n[10] >= '1' && n[10] <= '4');
}
inline bool damage(Address row) {
    const auto state = read<Byte>(row, 0x24);
    if (state != 1 && !(state == 0 && read<float>(row, 0x14) < read<float>(row, 0x1C))) return false;
    int duration{};
    native<int* (*)(Address, int*)>(runtime::table().fn.repair_duration)(row, &duration);
    return duration >= read<int>(base, 0x14A490C);
}
struct Damage {
    Span rows{};
    int body{-1}, total{}, free{};
    bool is_tank{}, is_cannon{}, tracks{}, broken_track{}, wheels{}, broken_wheel{}, broken_engine{}, weapon{},
        working_gun{};
    Scenario trigger() const {
        if (body == 1) return Scenario::full;
        if (body != 0) return Scenario::none;
        if (is_tank) return broken_track && weapon ? Scenario::turret_track : Scenario::none;
        if (weapon || broken_wheel || (!is_cannon && (broken_engine || broken_track))) return Scenario::turret_track;
        return Scenario::none;
    }
};
inline bool inspect(Address a, Damage& d) {
    d.is_tank = support::tank_class(a);
    d.is_cannon = support::extender(a, runtime::table().vt.cannon_ext, base) != 0;
    d.rows = span(a, 0xB90);
    if (!d.rows.valid(0x28, 1024)) return false;
    bool intact_cannon_part = false;
    for (auto p = d.rows.first; p != d.rows.end; p += 0x28) {
        const auto def = read<Address>(p);
        if (!def || read<Address>(p, 8) != a) return false;
        auto n = component_name(def);
        if (n.empty()) continue;
        const auto state = read<Byte>(p, 0x24);
        if (n == "body") d.body = state;
        if (gun(n) && state == 0) d.working_gun = true;
        const auto names = span(def, 0x328);
        if (n == "trackleft" || n == "trackright" || support::tag(names, "track") ||
            (!d.is_tank && n.substr(0, 5) == "track")) {
            d.tracks = true;
            d.broken_track |= state == 1;
        }
        const bool wheel = n.substr(0, 5) == "wheel" || support::tag(names, "wheel");
        if (wheel) {
            d.wheels = true;
            d.broken_wheel |= state == 1;
        }
        if (n == "engine" && state == 1) d.broken_engine = true;
        const bool hurt = damage(p);
        const bool assembly = gun(n) || turret(n) || n == "mantlet" || (!d.is_tank && d.is_cannon && n == "shield");
        if (assembly && (state == 1 || hurt)) d.weapon = true;
        if (d.is_cannon && (assembly || wheel) && state == 0) intact_cannon_part = true;
        if (hurt) {
            ++d.total;
            if (!(read<Byte>(p, 0x25) & 1)) ++d.free;
        }
    }
    // A gun without a hull row must remain inspectable after its last repair so its crew can board again.
    if (d.body == -1 && !d.is_tank && d.is_cannon && (intact_cannon_part || (d.total && (d.weapon || d.broken_wheel))))
        d.body = 0;
    return (!d.is_tank || d.tracks) && (d.body == 0 || d.body == 1);
}
inline bool ready(Address a) {
    auto b = brain_of(a, human_vt);
    if (!b || !human_policy(a) || (read<unsigned>(a, 0x1C0) & (1u << 28)) || read<unsigned>(b, 0x88) ||
        read<Address>(b, 0x98) != read<Address>(b, 0xA0))
        return false;
    auto q = read<Address>(b, 0xF0);
    return !q || (read<Address>(q) == base + sequence_vt && read<Address>(q, 8) == a + 0xAF0 &&
                  !(read<unsigned>(q, 0x20) & 7) && !read<Address>(q, 0x48) && !read<Address>(q, 0x50));
}
inline bool install(Address a, Address order) {
    if (!order) return false;
    if (!ready(a)) {
        support::discard(order);
        return false;
    }
    auto b = read<Address>(a, 0xDC8), q = read<Address>(b, 0xF0);
    if (q)
        native<Address (*)(Address, Address, int, Byte)>(0x919270)(q, order, 0, 1);
    else
        native<void (*)(Address, Address)>(0x91DB30)(b, order);
    return true;
}
struct Orders {
    Address smart{}, repair{}, exchange{}, board{};
};
inline Orders orders(Address actor, Address tank) {
    Orders r{};
    auto b = brain_of(actor, human_vt);
    auto o = b ? read<Address>(b, 0xF0) : 0;
    for (unsigned i = 0; o && i < 32; ++i, o = sim::orders::active_child(base, o)) {
        if (read<Address>(o, 8) != actor + 0xAF0) break;
        auto table = read<Address>(o) - base;
        if (table == smart_vt && read<Address>(o, 0x58) == tank) r.smart = o;
        if (table == repair_vt && read<Address>(o, 0x68) == tank) r.repair = o;
        if (table == exchange_vt && (read<Address>(o, 0x38) == tank || r.smart)) r.exchange = o;
        if (table == board_vt && read<Address>(o, 0x58) == tank) r.board = o;
    }
    return r;
}
struct Claims {
    std::array<bool, 1024> used{};
};
inline Claims claims(const World& w, Address a, Span rows, Address except = 0) {
    Claims c{};
    support::actors(w, [&](Address human) {
        if (!(read<Byte>(human, 0x30) & 4)) return;
        auto o = orders(human, a);
        if (!o.repair || o.repair == except) return;
        auto p = read<Address>(o.repair, 0xD0);
        if (p >= rows.first && p < rows.end && (p - rows.first) % 0x28 == 0) {
            c.used[(p - rows.first) / 0x28] = true;
        }
    });
    return c;
}
inline int tier(Scenario s, std::string_view n, Byte state, Span names = {}) {
    if (s == Scenario::turret_track) {
        if (gun(n)) return 0;
        if (turret(n)) return 1;
        if (n == "mantlet") return 2;
        if (n == "engine") return 3;
        if (state == 1 && (n.substr(0, 5) == "track" || n.substr(0, 5) == "wheel" || support::tag(names, "track") ||
                           support::tag(names, "wheel")))
            return 4;
        return state == 1 ? 5 : 6;
    }
    return state == 1 ? 3 : 4;
}
using Picker = Address (*)(const Address*);
using Finish = void (*)(Address, Address);
using Exec = int (*)(Address, int);
using Destroy = Address (*)(Address, unsigned);
using Dispatch = void (*)(Address);
inline Picker original_picker{};
inline Finish original_finish{};
inline Exec original_exec{};
inline Destroy original_destroy{};
inline Dispatch original_dispatch{};
template <class Damaged>
inline Address unclaimed_component(Address stock, Span rows, Span rejected, const Claims& claimed, Damaged damaged) {
    if (!rows.valid(0x28, claimed.used.size()) || !rejected.valid(8, 4096) || stock < rows.first || stock >= rows.end ||
        (stock - rows.first) % 0x28 || !claimed.used[(stock - rows.first) / 0x28])
        return stock;
    Address best{};
    int best_tier{}, best_priority{};
    for (auto row = rows.first; row != rows.end; row += 0x28) {
        const auto def = read<Address>(row);
        if (!def || claimed.used[(row - rows.first) / 0x28] || (read<Byte>(row, 0x25) & 1) || !damaged(row)) continue;
        bool tried = false;
        for (auto it = rejected.first; it != rejected.end; it += 8)
            if (read<Address>(it) == row) {
                tried = true;
                break;
            }
        if (tried) continue;
        const int rank = read<Byte>(row, 0x24) == 1 ? 0 : 1, priority = read<int>(def, 0x344);
        if (!best || rank < best_tier || (rank == best_tier && priority < best_priority)) {
            best = row;
            best_tier = rank;
            best_priority = priority;
        }
    }
    return best ? best : stock;
}
inline Address global_pick(const Address* arg, bool (*damaged)(Address) = damage) {
    const auto stock = original_picker(arg);
    const auto human = engine_fixes::current();
    if (!stock || !arg || !human || !engine_fixes::authorized(human, read<Address>(human, 0xdc8))) return stock;
    const auto order = *arg, target = read<Address>(order, 0x68);
    if (read<Address>(order, 8) != human + 0xaf0 || read<Byte>(order, 0x131) || !support::live_entity(target))
        return stock;
    const auto rows = span(target, 0xb90), rejected = span(order, 0xf0);
    World w{};
    if (!rows.valid(0x28, 1024) || !rejected.valid(8, 4096) || !sim::world(base, w)) return stock;
    const auto picked = unclaimed_component(stock, rows, rejected, claims(w, target, rows, order), damaged);
    if (picked != stock) ++stat_picker_overrides;
    return picked;
}
inline Address pick(const Address* arg) {
    Guard guard;
    World w{};
    if (!serial_world(w) ||
        (!config::active().modules.tank_auto_repair && !config::active().modules.nontank_auto_repair))
        return global_pick(arg);
    const auto o = *arg, a = read<Address>(o, 0x68), owner = read<Address>(o, 8);
    Tank* t = nullptr;
    auto human = owner ? owner - 0xAF0 : 0;
    auto* c = member(human, t);
    if (!c || !c->assigned || !c->smart || !t->key.matches(a)) return global_pick(arg);
    if (!human_policy(human) || !safety(a)) return original_picker(arg);
    const auto path = orders(human, a);
    if (path.smart != c->smart || path.repair != o) return original_picker(arg);
    Damage d{};
    const auto rejected = span(o, 0xF0);
    if (!inspect(a, d) || !rejected.valid(8, 4096)) return original_picker(arg);
    const auto claimed = claims(w, a, d.rows, o);
    Address best{};
    int best_tier{}, best_priority{};
    bool best_claim{};
    for (auto p = d.rows.first; p != d.rows.end; p += 0x28) {
        if ((read<Byte>(p, 0x25) & 1) || !damage(p)) continue;
        bool tried = false;
        for (auto j = rejected.first; j != rejected.end; j += 8)
            if (read<Address>(j) == p) {
                tried = true;
                break;
            }
        if (tried) continue;
        auto def = read<Address>(p);
        auto comp = component_name(def);
        if (comp.empty()) continue;
        int rank = tier(d.body == 1 ? Scenario::full : t->scenario, comp, read<Byte>(p, 0x24), span(def, 0x328));
        int priority = read<int>(def, 0x344);
        bool taken = claimed.used[(p - d.rows.first) / 0x28];
        if (!best || taken < best_claim ||
            (taken == best_claim && (rank < best_tier || (rank == best_tier && priority < best_priority)))) {
            best = p;
            best_claim = taken;
            best_tier = rank;
            best_priority = priority;
        }
    }
    const auto n = ++stat_picker_overrides;
    if (log_sink && best && (n <= 20 || (n % 25) == 0)) {
        const auto name = component_name(read<Address>(best));
        log_sink("TANK AUTO-REPAIR PICK #%u: tank=0x%llx crew=0x%llx comp=%.*s tier=%d prio=%d claimed=%d", n,
                 static_cast<unsigned long long>(a), static_cast<unsigned long long>(human),
                 static_cast<int>(name.size()), name.data(), best_tier, best_priority, best_claim ? 1 : 0);
    }
    return best;
}

struct Skill {
    bool mechanic{};
    float rate{1};
};
inline Skill skill(Address human) {
    auto breed = read<Address>(human, 0x1390);
    if (!breed) return {};
    const auto abilities = span(breed + 0x528, 0x20), levels = span(breed + 0x560, 0x20);
    if (!abilities.valid(8, 1024) || !levels.valid(8, 1024)) return {};
    for (auto p = abilities.first; p != abilities.end; p += 8) {
        const auto info = read<Address>(p);
        if (!info || read<Address>(info) != base + 0x107F0F8) continue;
        // Mechanic's availability predicate is constant true. Its parameter reader
        // copies the required level and float rate directly from this concrete info.
        if (read<unsigned>(info, 0x18)) {
            if (!read<Address>(human, 0x1178)) return {};
            for (auto q = levels.first; q != levels.end; q += 8) {
                auto row = read<Address>(q), ability = row ? read<Address>(row, 8) : 0;
                if (!ability) return {};
                auto test = read<Address>(read<Address>(ability), 0x28);
                if (test != base + 0x1B51C0 && test != base + 0x368AC0) return {};
            }
            if (native<unsigned (*)(Address)>(0x94F3D0)(human) < read<unsigned>(info, 0x18)) return {};
        }
        const float rate = read<float>(info, 0x1C);
        return std::isfinite(rate) ? Skill{true, rate} : Skill{};
    }
    return {};
}
inline int kit_count(Address item) {
    if (!item) return 0;
    auto d = read<Address>(item, 0x40);
    return d && support::tag(span(d, 0x11E0), "repair_kit")
               ? std::clamp(read<int>(item, 0x48), 0, static_cast<int>(crew_cap))
               : 0;
}
inline int carried(Address a) {
    const auto box = support::box(a);
    const auto items = span(box, 0x10), holders = span(support::manager(a), 0x88);
    int count = 0;
    if (items.valid(8, 8192))
        for (auto p = items.first; p != items.end; p += 8) {
            auto item = read<Address>(p);
            if (item && read<Address>(item, 0x30) == box) count += kit_count(item);
        }
    if (holders.valid(8, 128))
        for (auto p = holders.first; p != holders.end; p += 8) {
            auto h = read<Address>(p);
            if (!h || read<Address>(h) != base + 0x10FF210 || read<Address>(h, 0x3D0) != a) continue;
            auto item = read<Address>(h, 0x3D8);
            if (item && read<Address>(item, 0x38) == h && (!box || read<Address>(item, 0x30) != box))
                count += kit_count(item);
        }
    return count;
}
inline bool in_range(const World& w, Address a, Address b, float metres) {
    return support::within(support::pos(a), support::pos(b), w.length * metres);
}
inline int ground_kits(const World& w, Address a) {
    int n = 0;
    support::entities(w, [&](Address e) {
        if (n >= static_cast<int>(crew_cap) || (read<unsigned>(e, 0x1C0) & (1u << 28)) || !in_range(w, a, e, 20))
            return;
        const auto group = read<Address>(e, 0x578), ext = group ? read<Address>(group, 0x20) : 0;
        if (ext && read<Address>(ext) == base + 0x10FD200 && read<Address>(ext, 0x18) == e && !read<Address>(ext, 0x98))
            n += kit_count(read<Address>(ext, 0x88));
    });
    return n;
}
inline bool seated_crew(Address seat) {
    return sim::SmokeTagEquals(seat + 0x60, "crew") || (read<unsigned>(seat, 0x248) & 0x1000) ||
           read<Address>(seat, 0x110);
}
inline Tank* reserve(Address a) {
    if (auto* t = find(a)) return t;
    auto occupied = span(read<Address>(a, 0x568), 0x10);
    if (occupied.first == occupied.end) return nullptr;
    for (auto& t : tanks)
        if (!t.key || !resolve(t.key)) {
            t = {};
            t.key = key(a);
            t.player = static_cast<Byte>(support::owner(a));
            return &t;
        }
    if (auto* t = store::try_append_slot(tanks)) {
        *t = {};
        t->key = key(a);
        t->player = static_cast<Byte>(support::owner(a));
        return t;
    }
    return nullptr;
}
// Protect secondary-turret cannon gunners (e.g., gun1/gun2 AP turrets on T-35,
// SMK, M3 Lee/Grant) without shielding ordinary hull bow-MG gunners (gunner3).
inline bool cannon_gunner_seat(Address a, std::string_view seat) {
    if (seat == "gunner") return true;
    if (seat.empty() || seat == "driver") return false;
    const auto ws = span(read<Address>(a, 0xDD8), 0x30);
    if (!ws.valid(8, 256)) return false;
    for (auto p = ws.first; p != ws.end; p += 8) {
        const auto w = read<Address>(p);
        if (!w || read<Address>(w, 0x90) != a) continue;
        const auto def = read<Address>(w, 0x98), place = read<Address>(w, 0xC0);
        if (!def || !place || read<Address>(place, 0x10) != a) continue;
        const auto tags = span(def, 0x11E0);
        if (support::tag(tags, "mgun") || support::tag(tags, "mrl")) continue;
        const float calibre = read<float>(def, 0x1628);
        if (!std::isfinite(calibre) || calibre < 37.f) continue;
        const auto names = span(place, 0x5E0);
        if (!names.valid(0x60, 64)) continue;
        for (auto n = names.first; n != names.end; n += 0x60)
            if (support::native_string(n + 0x40) == seat) return true;
    }
    return false;
}
inline bool snapshot(Tank& t, Address a) {
    const auto placer = read<Address>(a, 0x568);
    if (!placer || read<Address>(placer, 0x40) != a) return false;
    auto seats = span(placer, 0x28), occupied = span(placer, 0x10);
    if (!seats.valid(0x298, 4096) || !occupied.valid(2, 4096)) return false;
    for (auto& c : t.crew)
        c.seat = -1;
    for (auto p = occupied.first; p != occupied.end; p += 2) {
        short index = read<short>(p);
        if (index < 0 || static_cast<unsigned>(index) >= (seats.end - seats.first) / 0x298) return false;
        auto slot = seats.first + static_cast<Address>(index) * 0x298, h = read<Address>(slot, 0x280);
        if (!h || !seated_crew(slot) || !brain_of(h, human_vt, true) || !read<std::uint16_t>(h, 0xDAA)) continue;
        Crew* entry = nullptr;
        for (auto& c : t.crew) {
            if (c.key.matches(h)) {
                entry = &c;
                break;
            }
            if (!entry && !c.key) entry = &c;
        }
        if (!entry) return false;
        if (!entry->key.matches(h)) {
            *entry = {};
            entry->key = key(h);
            entry->player = static_cast<Byte>(support::owner(h));
        }
        auto name = support::native_string(slot + 0x40);
        entry->seat = index;
        entry->role = name == "driver" ? 0 : cannon_gunner_seat(a, name) ? 2 : 1;
        members[entry->key.route] = {static_cast<std::uint16_t>(&t - tanks.data() + 1),
                                     static_cast<std::uint16_t>(entry - t.crew.data())};
    }
    return true;
}
inline bool walking(Span walkers, Address human) {
    for (auto p = walkers.first; p != walkers.end; p += 0x90)
        if (read<Address>(p, 8) == human) return true;
    return false;
}
inline bool backed_off(const World& w, Address h) {
    const auto* b = overrides.find(read<std::uint16_t>(h, 0xDAA));
    return b && b->actor.matches(h) && pending(b->deadline, w.now);
}
inline void release(Crew& c, const World& w, Address h) {
    overrides[c.key.route] = {support::identity(h), w.now + w.duration(25)};
    c.assigned = c.needs_board = c.boarding = false;
}
inline void finish(Address cb, Address human) {
    Guard guard;
    World w{};
    Tank* t = nullptr;
    if (serial_world(w)) {
        auto c = member(human, t);
        if (c && c->exit_callback == cb && read<Address>(cb, 0x28) == t->key.entity.ptr &&
            runtime::actor_ai_allowed(read<Address>(cb, 0x28))) {
            // Only our factory-created callback. Let the walker finish unlinking.
            // The coordinator installs SmartRepair on a fresh, idle ground borrow.
            c->exit_callback = 0;
            return;
        }
    }
    original_finish(cb, human);
}
inline int smart_exec(Address order, int phase) {
    Guard guard;
    World w{};
    if (phase == 2 || !serial_world(w)) return original_exec(order, phase);
    auto owner = read<Address>(order, 8);
    auto human = owner ? owner - 0xAF0 : 0;
    Tank* t = nullptr;
    auto c = member(human, t);
    if (!c || c->smart != order) return original_exec(order, phase);
    auto a = read<Address>(order, 0x58), b = brain_of(human, human_vt);
    if (!runtime::actor_ai_allowed(a)) return original_exec(order, phase);
    if (b && overridden(human, b)) release(*c, w, human);
    if (!c->assigned || !a || !t->key.matches(a) || support::owner(a) != t->player ||
        support::owner(human) != c->player || !human_policy(human) || !safety(a))
        return 2;
    Damage d{};
    if (!inspect(a, d)) return 2;
    return original_exec(order, phase);
}
inline Address smart_destroy(Address o, unsigned flags) {
    Guard guard;
    // The slot remains native-owned. Compare addresses only during destruction.
    if (active.load() && serial.load() == GetCurrentThreadId()) {
        for (auto& t : tanks)
            if (t.key)
                for (auto& c : t.crew)
                    if (c.smart == o) c.smart = 0;
    }
    return original_destroy(o, flags);
}
inline bool smart(Crew& c, Address h, Address a) {
    if (!ready(h)) return false;
    const float rate = skill(h).rate;
    auto o = native<Address (*)(Address, float)>(0x915070)(a, rate);
    c.smart = o;
    if (!install(h, o)) {
        c.smart = 0;
        return false;
    }
    const bool chained = c.started;
    const auto n = chained ? ++stat_repair_chained : ++stat_excrew_assigned;
    c.dispatched = c.started = c.assigned = c.needs_board = true;
    if (log_sink) {
        log_sink("TANK AUTO-REPAIR %s #%u: tank=0x%llx crew=0x%llx rate=%.2f", chained ? "CHAIN" : "ASSIGN", n,
                 static_cast<unsigned long long>(a), static_cast<unsigned long long>(h), static_cast<double>(rate));
    }
    return true;
}
inline bool board(Crew& c, Address h, Address a, const World& w) {
    alignas(16) std::array<std::byte, 0x60> name{};
    native<void* (*)(void*)>(0x564D50)(name.data());
    auto o = native<Address (*)(Address, const void*, Byte)>(0x8FADC0)(a, name.data(), 0);
    native<void (*)(void*)>(0x565010)(name.data());
    if (!install(h, o)) return false;
    c.boarding = true;
    c.assigned = false;
    c.retry = w.now + w.duration(1);
    const auto n = ++stat_reboard_queued;
    if (log_sink) {
        log_sink("TANK AUTO-REPAIR REBOARD #%u: tank=0x%llx crew=0x%llx", n, static_cast<unsigned long long>(a),
                 static_cast<unsigned long long>(h));
    }
    return true;
}
inline bool emit(Tank& t, Crew& c, Address h, Address a, float rate) {
    const auto placer = read<Address>(a, 0x568);
    const auto seats = span(placer, 0x28);
    if (c.seat < 0 || !seats.valid(0x298, 4096) || static_cast<unsigned>(c.seat) >= (seats.end - seats.first) / 0x298 ||
        read<Address>(seats.first + static_cast<Address>(c.seat) * 0x298, 0x280) != h || !t.key.matches(a))
        return false;
    auto cb = native<Address (*)(Address, float)>(0x9AC630)(a, rate);
    if (!cb) return false;
    // A private one-seat Emit runs one native Start. End releases its optional pacing Wait.
    // The boarder independently retains the callback and owns the rest of traversal.
    auto o = native<Address (*)(int, int, Address)>(0x8FC550)(1, -1, cb);
    native<void (*)(Address, Address)>(0x917D70)(o, a + 0xAF0);
    std::memcpy(reinterpret_cast<void*>(o + 0x58), &c.seat, sizeof(c.seat));
    const short seatIndex = c.seat;
    c.exit_callback = cb;
    c.assigned = c.needs_board = true;
    native<int (*)(Address)>(0x919F60)(o);
    const auto left = read<short>(o, 0x58) < 0;
    native<void (*)(Address)>(0x91A0E0)(o);
    support::discard(o);
    if (!left) {
        c.exit_callback = 0;
        c.assigned = c.needs_board = false;
        return false;
    }
    c.seat = -1;
    c.dispatched = true;
    const auto n = ++stat_crew_emitted;
    if (log_sink) {
        log_sink("TANK AUTO-REPAIR EMIT #%u: tank=0x%llx crew=0x%llx seat=%d role=%u rate=%.2f", n,
                 static_cast<unsigned long long>(a), static_cast<unsigned long long>(h), static_cast<int>(seatIndex),
                 static_cast<unsigned>(c.role), static_cast<double>(rate));
    }
    return true;
}

struct Candidate {
    Crew* crew{};
    Address human{};
    Skill ability{};
    int kits{};
};
struct Census {
    std::array<Candidate, crew_cap> candidates{};
    std::size_t count{};
    int live{}, living_crew{}, busy{}, kits{}, public_kits{};
    bool outside{};
    std::array<Address, crew_cap> counted{};
    std::size_t counted_size{};
};
inline bool working(Orders o) {
    return o.smart || o.repair || o.exchange;
}
inline void census(Tank& t, Address a, const World& w, const Damage& d, Span walkers, Census& s) {
    s.public_kits = carried(a);
    s.kits = s.public_kits;
    for (auto& c : t.crew) {
        if (!c.key) continue;
        auto h = resolve(c.key);
        const bool walker = h && walking(walkers, h);
        auto b = h ? brain_of(h, human_vt, c.seat >= 0 || walker) : 0;
        if (!b || (vitality(h) & 1)) {
            if (c.assigned && (c.started || c.smart)) t.death_until = w.now + w.duration(8);
            c = {};
            continue;
        }
        if (support::owner(h) != c.player || !sim::allies(base, t.player, c.player)) {
            c = {};
            continue;
        }
        Tank* current = nullptr;
        if (member(h, current) != &c) {
            c = {};
            continue;
        }
        if (c.seat >= 0 && !walker) {
            c.assigned = c.needs_board = c.started = c.boarding = c.dispatched = false;
        }
        if (c.seat < 0 && (c.dispatched || c.needs_board || t.scenario == Scenario::full)) s.outside = true;
        if (vitality(h)) continue;
        if (c.seat < 0 && !walker && !in_range(w, a, h, 30)) {
            if (!c.smart) c = {};
            continue;
        }
        const bool linked = (read<unsigned>(h, 0x1C0) & (1u << 28)) != 0;
        if (c.seat < 0 && !walker && linked) {
            c = {};
            continue;
        }
        ++s.living_crew;
        if (c.seat < 0 && !walker && !linked && overridden(h, b)) release(c, w, h);
        if (backed_off(w, h)) continue;
        if (c.seat < 0 && !walker && !human_policy(h)) continue;
        if (c.seat >= 0 && !available_seat(h, a, b)) continue;
        ++s.live;
        int kits = carried(h);
        s.kits += kits;
        if (c.seat < 0 && (c.dispatched || t.scenario == Scenario::full)) c.needs_board = true;

        if (walker) continue; // Count from the boarder's current vector below.
        c.exit_callback = 0;  // No walker retains a module callback for this actor.
        if (c.seat < 0) {
            auto path = orders(h, a);
            // Even between native orders, a dispatched repairer owns one quota slot.
            // Scene census below counts active native orders, including outsiders.
            if (working(path)) {
                ++s.busy;
                s.counted[s.counted_size++] = h;
            } else if ((c.assigned || c.dispatched) && !path.board)
                ++s.busy;
            if (working(path) || path.board || c.smart) continue;
            if (!ready(h)) continue;
        } else {
            if (d.body == 0 && t.scenario == Scenario::full) continue;
        }
        if (pending(c.retry, w.now)) continue;
        s.candidates[s.count++] = {&c, h, {}, kits};
    }
    for (auto p = walkers.first; p != walkers.end; p += 0x90) {
        const auto final_node = read<Address>(p, 0x30);
        if (read<Address>(p, 8) && final_node && read<int>(final_node, 4) == 1) ++s.busy;
    }
    if (s.count && d.free) {
        const int limit = std::min(s.live, t.scenario == Scenario::full ? d.total : 2);
        // Includes native and player-issued work by people outside our roster.
        if (s.busy < limit)
            support::actors(w, [&](Address h) {
                if (s.busy >= limit || !(read<Byte>(h, 0x30) & 4)) return;
                const auto end = s.counted.begin() + s.counted_size;
                if (std::find(s.counted.begin(), end, h) == end && working(orders(h, a))) ++s.busy;
            });
        if (!s.public_kits || s.kits < std::min(s.live, t.scenario == Scenario::full ? d.total : 2)) {
            int loose = ground_kits(w, a);
            s.kits += loose;
            s.public_kits += loose;
        }
    }
}
inline int rank(const Candidate& v, bool spare_gunner) {
    // Prefer an existing repairer at a continuation boundary, then a ground
    // crewman. Protect the primary gunner while an operational gun can fire.
    return (v.crew->assigned ? 0 : 64) + (v.crew->seat < 0 ? 0 : 32) + (spare_gunner && v.crew->role == 2 ? 16 : 0) +
           (v.kits ? 0 : 8) + (v.ability.mechanic ? 0 : 4) + v.crew->role;
}
inline Address crew_provider(Tank& t, Address taker, Address a, const World& w) {
    for (auto& donor : t.crew) {
        const auto h = resolve(donor.key);
        const auto b = h ? brain_of(h, human_vt, donor.seat >= 0) : 0;
        if (!b || vitality(h) || h == taker || support::owner(h) != donor.player ||
            (donor.seat >= 0 ? !available_seat(h, a, b) : !human_policy(h)) || backed_off(w, h) ||
            !in_range(w, a, h, 30) || !support::inventory_free(h))
            continue;
        // Preserve the donor's last kit while they are committed to a repair.
        if (carried(h) > (donor.assigned ? 1 : 0)) return h;
    }
    return 0;
}
inline bool unassigned_work(const Damage& d, const Claims& taken) {
    for (auto p = d.rows.first; p != d.rows.end; p += 0x28)
        if (!taken.used[(p - d.rows.first) / 0x28] && !(read<Byte>(p, 0x25) & 1) && damage(p)) return true;
    return false;
}
inline bool may_dismount(Address vehicle, const World& w, Scenario scenario, const Damage& d, const Crew& c,
                         int living_crew) {
    if (!d.is_tank && !support::free_slot(vehicle)) return false;
    if (scenario != Scenario::turret_track) return true;
    if (c.role == 2 && d.working_gun && living_crew >= 2) return false;
    if (d.is_tank) {
        if (!d.working_gun) return true;
        const auto sen = read<Address>(vehicle, 0xDD0);
        const bool under_fire =
            sen && read<Address>(sen, 0x260) == vehicle && sim::recent(w, read<unsigned>(sen, 0x2F4), 5);
        return !(under_fire && support::visible_near(vehicle, 40.f * w.length));
    }
    return !support::visible_near(vehicle, 25.f * w.length);
}
inline void service(Tank& t, Address a, const World& w, Damage d) {
    if (t.player != support::owner(a)) {
        t = {};
        return;
    }
    t.is_tank = d.is_tank;
    if (!snapshot(t, a)) return;
    if (std::none_of(t.crew.begin(), t.crew.end(), [](const Crew& c) { return bool(c.key); })) {
        t = {};
        return;
    }
    const auto trigger = d.trigger();
    if (trigger == Scenario::full && !t.body_was_broken) t.ejection_until = w.now + w.duration(16);
    t.body_was_broken = d.body == 1;
    if (trigger == Scenario::full || t.scenario == Scenario::none) {
        if (trigger != t.scenario) t.announced = false;
        t.scenario = trigger;
    }
    if (t.scenario == Scenario::none) {
        bool occupied = false;
        for (auto& c : t.crew) {
            if (c.seat < 0)
                c = {};
            else
                occupied = true;
        }
        if (!occupied) t = {};
        return;
    }
    if (t.serviced && t.last_tick == w.now) return;
    t.last_tick = w.now;
    t.serviced = true;
    const auto boarder = read<Address>(a, 0x570);
    if (!boarder || read<Address>(boarder, 0x58) != a) return;
    const auto root = native<Address (*)(Address)>(0x79D790)(boarder);
    const auto walkers = span(root, 0x60);
    if (!root || !walkers.valid(0x90, 4096)) return;
    Census s{};
    census(t, a, w, d, walkers, s);
    if (!d.total && !s.outside && !pending(t.ejection_until, w.now)) {
        t.scenario = Scenario::none;
        t.announced = false;
        for (auto& c : t.crew)
            if (c.seat < 0) c = {};
        return;
    }
    if (!s.count) return;
    const auto cap = t.scenario == Scenario::full ? std::max(1, d.total) : 2;
    const int max_dismount = t.scenario == Scenario::turret_track ? std::max(0, s.living_crew - 1) : s.live;
    int desired = std::min({s.live, s.kits, cap, max_dismount});
    const bool spare_gunner = t.scenario == Scenario::turret_track && s.living_crew > 2 && d.working_gun;
    for (std::size_t i = 0; i < s.count; ++i)
        s.candidates[i].ability = skill(s.candidates[i].human);
    std::sort(s.candidates.begin(), s.candidates.begin() + s.count, [&](const Candidate& left, const Candidate& right) {
        auto x = rank(left, spare_gunner), y = rank(right, spare_gunner);
        return x != y ? x < y : left.crew->key.entity.id < right.crew->key.entity.id;
    });
    bool free_work = d.free > 0;
    if (free_work && d.body == 0) free_work = unassigned_work(d, claims(w, a, d.rows));
    for (std::size_t i = 0; i < s.count; ++i) {
        auto& v = s.candidates[i];
        auto& c = *v.crew;
        if (!resolve(t.key) || t.player != support::owner(a) || !safety(a)) break;
        if (resolve(c.key) != v.human || c.player != support::owner(v.human) || vitality(v.human)) continue;
        auto live_brain = brain_of(v.human, human_vt, c.seat >= 0);
        if (!live_brain || (c.seat >= 0 && !available_seat(v.human, a, live_brain))) continue;
        if (c.seat < 0 && (!ready(v.human) || !in_range(w, v.human, a, 30))) continue;
        if (c.seat < 0 && c.needs_board && d.body == 0 && !free_work) {
            if (carried(v.human)) {
                install(v.human, native<Address (*)(Address, Byte)>(0x915280)(a, 0));
                c.retry = w.now + w.duration(1);
            } else
                board(c, v.human, a, w);
            continue;
        }
        const bool continuing = c.assigned && c.seat < 0;
        if ((!continuing && s.busy >= desired) || !s.kits || !free_work ||
            (!continuing && pending(t.death_until, w.now)))
            continue;
        if (c.seat < 0 && t.scenario == Scenario::turret_track && !c.needs_board && !c.assigned) continue;
        if (c.seat < 0 && !v.kits && !s.public_kits) {
            auto donor = crew_provider(t, v.human, a, w);
            if (!donor) continue;
            if (install(v.human, native<Address (*)(Address, Byte)>(0x915280)(donor, 1))) {
                c.needs_board = c.assigned = true;
                if (!continuing) ++s.busy;
                c.retry = w.now + w.duration(1);
            }
            continue;
        }
        if (c.seat >= 0 && !may_dismount(a, w, t.scenario, d, c, s.living_crew)) continue;
        bool ok = c.seat >= 0 ? emit(t, c, v.human, a, v.ability.rate) : smart(c, v.human, a);
        if (!ok) {
            c.retry = w.now + w.duration(1);
            continue;
        }
        if (!continuing) ++s.busy;
        if (!t.announced) {
            t.announced = true;
            const bool isFull = t.scenario == Scenario::full;
            const auto n = isFull ? ++stat_dispatch_full : ++stat_dispatch_turret_track;
            if (!d.is_tank) ++stat_nontank_dispatch;
            if (log_sink) {
                log_sink("TANK AUTO-REPAIR %s #%u: tank=0x%llx body=%d dmg=%d free=%d liveCrew=%d kits=%d desired=%d",
                         isFull ? "FULL" : "TURRET+TRACK", n, static_cast<unsigned long long>(a), d.body, d.total,
                         d.free, s.live, s.kits, desired);
            }
        }
    }
}
inline void dispatch(Address state) {
    Guard guard;
    World w{};
    if (serial_world(w)) {
        auto b = read<Address>(state, 0x30), a = b ? read<Address>(b, 0x38) : 0;
        auto* t = find(a);
        Damage d{};
        // Stock repair polling can run before the shared post-quant callback.
        // A qualifying episode has one dispatcher, including during casualty backoff.
        if (t && t->player == support::owner(a) && brain_of(a, vehicle_vt) == b && safety(a) && inspect(a, d) &&
            (t->scenario != Scenario::none || d.trigger() != Scenario::none))
            return;
    }
    original_dispatch(state);
}
} // namespace detail

inline void on_actor_post_quant(Address actor) {
    using namespace detail;
    Guard guard;
    if (!active.load() || !actor || runtime::is_quant_worker(base) || !runtime::actor_ai_allowed(actor)) return;
    serial.store(GetCurrentThreadId(), std::memory_order_relaxed);
    if (coordinating) return;
    World w{};
    if (runtime::is_remote_client(base) || !sim::world(base, w)) {
        if (last_scene) reset();
        return;
    }
    if (w.scene != last_scene || w.now < last_clock) {
        reset();
        last_scene = w.scene;
    }
    last_clock = w.now;
    if (!sim::has_actor(actor)) return;
    if ((read<Byte>(actor, 0x30) & 8) &&
        (!config::active().modules.tank_auto_repair || !config::active().modules.nontank_auto_repair)) {
        if (auto* old = find(actor); old && !enabled(old->is_tank)) *old = {};
        if (!enabled(support::tank_class(actor))) return;
    }
    struct Scope {
        Scope() {
            coordinating = true;
        }
        ~Scope() {
            coordinating = false;
        }
    } scope;
    Address a = actor;
    Tank* t = nullptr;
    if (read<Byte>(actor, 0x30) & 4) {
        auto c = member(actor, t);
        if (!c || t->scenario == Scenario::none) return;
        a = resolve(t->key);
        if (!a) {
            *t = {};
            return;
        }
        const auto b = brain_of(actor, human_vt);
        if (b && c->seat < 0 && !(read<unsigned>(actor, 0x1C0) & (1u << 28)) && overridden(actor, b) &&
            (c->assigned || c->needs_board))
            release(*c, w, actor);
        if (t->serviced && t->last_tick == w.now) return;
    }
    if (!safety(a)) return;
    Damage d{};
    if (!inspect(a, d)) return;
    if (!t) {
        if (!read<std::uint16_t>(a, 0xDAA)) return;
        t = reserve(a);
    }
    if (t) service(*t, a, w, d);
}

inline bool is_busy(Address actor);

template <class Create, class Remove>
inline bool prepare_hooks(Address image, const char* measured_sha256, Create create, Remove remove) {
    using namespace detail;
    if (base || rollback_pending || !image || !measured_sha256 || std::strcmp(measured_sha256, executable_sha256) != 0)
        return false;
    std::array<void*, 5> created{};
    std::size_t n{};
    auto add = [&](Address rva, auto hook, auto& orig) {
        auto target = reinterpret_cast<void*>(image + rva);
        void* trampoline = nullptr;
        if (create(target, reinterpret_cast<void*>(hook), &trampoline) != 0) return false;
        created[n++] = target;
        orig = reinterpret_cast<decltype(hook)>(trampoline);
        return true;
    };
    if (!add(0x9159D0, pick, original_picker) || !add(0x9AC1A0, finish, original_finish) ||
        !add(0x913A20, smart_exec, original_exec) || !add(0xB5380, smart_destroy, original_destroy) ||
        !add(0x9B0E80, dispatch, original_dispatch)) {
        while (n)
            if (remove(created[--n]) != 0) rollback_pending = true;
        if (rollback_pending && log_sink) log_sink("Tank auto-repair hook rollback requires cleanup");
        return false;
    }
    base = image;
    stat_dispatch_full = stat_dispatch_turret_track = stat_crew_emitted = stat_excrew_assigned = stat_repair_chained =
        stat_reboard_queued = stat_picker_overrides = stat_nontank_dispatch = 0;
    active.store(true);
    coord::instance().register_provider(coord::Module::tank_auto_repair, coord::Priority::tank_auto_repair, &is_busy);
    return true;
}
inline bool is_busy(Address actor) {
    detail::Tank* tank{};
    const auto* c = detail::member(actor, tank);
    return c && (c->assigned || c->needs_board || c->started || c->boarding || c->dispatched || c->smart ||
                 c->exit_callback);
}
inline bool has_active_episode(Address actor) {
    const auto* t = detail::active.load() ? detail::find(actor) : nullptr;
    return t && detail::enabled(t->is_tank) && t->scenario != detail::Scenario::none;
}
inline std::size_t collect_returning_crew(Address tank, Address* out, std::size_t cap) {
    if (!detail::active.load() || !tank || !out || !cap) return 0;
    const auto* t = detail::find(tank);
    if (!t || !detail::enabled(t->is_tank)) return 0;
    std::size_t count = 0;
    for (const auto& c : t->crew) {
        if (count >= cap) break;
        if (!c.key || c.seat >= 0) continue;
        if (!(c.assigned || c.needs_board || c.started || c.boarding || c.dispatched || c.smart || c.exit_callback))
            continue;
        const auto h = detail::resolve(c.key);
        if (!h || detail::vitality(h) != 0 || (detail::read<unsigned>(h, 0x1C0) & (1u << 28))) continue;
        out[count++] = h;
    }
    return count;
}
inline void deactivate() noexcept {
    detail::active.store(false);
}
inline int inflight_count() noexcept {
    return detail::inflight.load();
}
inline void after_hooks_removed() {
    using namespace detail;
    deactivate();
    reset();
    tanks.release();
    members.release();
    overrides.release();
    base = 0;
    serial = 0;
    rollback_pending = false;
    original_picker = nullptr;
    original_finish = nullptr;
    original_exec = nullptr;
    original_destroy = nullptr;
    original_dispatch = nullptr;
    resolve_native = &native_address;
}
} // namespace goh::tank_auto_repair
