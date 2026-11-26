#pragma once

#include "tank_crew_fill_module.hpp"
#include "tank_facing_module.hpp"
#include <limits>

namespace goh::safe_disembark {
namespace s = ai_support;
namespace sim = auto_smoke;
namespace crew = tank_crew_fill;
using runtime::Address;
using runtime::Byte;
using runtime::read;
using runtime::Tick;
using runtime::XY;
using sim::Vec3;
using sim::World;

inline constexpr unsigned hook_count = 1;
inline constexpr bool runtime_allowed = false;
inline std::atomic<unsigned> stat_safe_doors{}, stat_transport_unloads{};

namespace policy {
struct Door {
    Address ptr{};
    Vec3 point{};
};
inline Address pick_safest_door(Vec3 origin, XY enemy, const Door* doors, std::size_t count, Address stock) {
    if (count < 2 || !runtime::finite(enemy) || std::hypot(enemy.x, enemy.y) < 1e-5f) return 0;
    float best = -std::numeric_limits<float>::infinity(), worst = -best;
    Address safest = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const auto& door = doors[i];
        const float dx = door.point.x - origin.x, dy = door.point.y - origin.y;
        const float len = std::hypot(dx, dy);
        if (!std::isfinite(len) || len < 1e-5f) continue;
        const float score = -(dx * enemy.x + dy * enemy.y) / len;
        if (score > best || (score == best && door.ptr == stock)) {
            best = score;
            safest = door.ptr;
        }
        worst = std::min(worst, score);
    }
    return best > worst + 1e-5f ? safest : 0;
}
struct Damage {
    bool burning{}, body_or_engine{}, immobilized{}, tracks{};
};
inline bool should_unload_transport(const Damage& d, bool stationary, bool active_threat) {
    return d.burning || d.body_or_engine || ((d.immobilized || stationary) && active_threat);
}
} // namespace policy

namespace detail {
inline Address base{};
inline runtime::HookGate gate{};
using SelectDoor = Address (*)(Address, const short*, const Vec3*, unsigned);
inline SelectDoor original{};

inline void update_bone(Address bone) {
    if (read<Byte>(bone, 0x10) & 1) runtime::native<void (*)(Address)>(base, 0x5D11A0)(bone);
}
using Probe = Byte (*)(void*, const Vec3*, const XY*, float, int);
inline bool probe_exit(void* context, Address door, Address point_bone, void (*update)(Address), Probe probe) {
    const auto path = reinterpret_cast<Address>(context);
    runtime::write(path, 0xE8, read<unsigned>(path, 0xE8) & ~1u);
    const auto bone = read<Address>(door, 0x60);
    update(bone);
    const XY normal{-read<float>(bone, 0xA8), -read<float>(bone, 0xAC)};
    const float range = read<float>(read<Address>(path, 0xD0), 0x20);
    return probe(context, reinterpret_cast<const Vec3*>(point_bone + 0xCC), &normal, range, 3) != 0;
}
inline bool obstacle(Address occupant, Address door, Address point_bone) {
    s::PathContext ctx(occupant);
    return probe_exit(ctx.bytes, door, point_bone, update_bone, runtime::native<Probe>(base, 0x8C1AD0));
}
inline Address serial_actor() {
    return s::serial_thread() ? s::current_actor() : 0;
}
struct Ops {
    Address (*current)() = serial_actor;
    bool (*host)(Address) = runtime::is_host_main_thread;
    bool (*world)(Address, World&) = sim::world;
    Address (*resolve)(Address, Address) = [](Address image, Address rva) { return image + rva; };
    void (*update)(Address) = update_bone;
    bool (*blocked)(Address, Address, Address) = obstacle;
    void (*discard)(Address) = s::discard;
};
inline Ops ops{};
template <class Fn> inline Fn native(Address rva) {
    return reinterpret_cast<Fn>(ops.resolve(base, rva));
}

inline bool available_passenger(Address human, Address vehicle) {
    if (!crew::detail::has_seated_actor(human) || !(read<Byte>(human, 0x30) & 4) ||
        !sim::allies(base, s::owner(vehicle), s::owner(human)) || crew::detail::vitality(human) ||
        !read<std::uint16_t>(human, 0xDAA))
        return false;
    const auto b = read<Address>(human, 0xDC8);
    return b && read<Address>(b) == base + runtime::table().vt.human_brain && read<Address>(b, 0x38) == human &&
           crew::detail::available_seat(human, vehicle, b);
}

struct Contact {
    Address entity{};
    Vec3 point{};
    float metres{};
    Tick stamp{};
    bool visible{};
};
struct Threats {
    std::array<Contact, 32> nearest{};
    std::size_t count{};
    bool close_at{};
    void add(const Contact& c) {
        std::size_t farthest = 0;
        for (std::size_t i = 0; i < count; ++i) {
            auto& old = nearest[i];
            if ((c.entity && old.entity == c.entity) ||
                (!c.entity && !old.entity && c.point.x == old.point.x && c.point.y == old.point.y)) {
                if (c.visible != old.visible ? c.visible : c.stamp > old.stamp) old = c;
                return;
            }
            if (old.metres > nearest[farthest].metres) farthest = i;
        }
        if (count < nearest.size())
            nearest[count++] = c;
        else if (c.metres < nearest[farthest].metres)
            nearest[farthest] = c;
    }
    XY direction(Vec3 origin) const {
        XY result{};
        for (std::size_t i = 0; i < count; ++i) {
            const auto& c = nearest[i];
            const auto dx = c.point.x - origin.x, dy = c.point.y - origin.y;
            const float weight = (c.visible ? 1.f : 0.6f) / (std::hypot(dx, dy) * std::max(c.metres, 1.f));
            result.x += dx * weight;
            result.y += dy * weight;
        }
        return result;
    }
};
inline Threats gather(const World& w, Address vehicle, bool at_only) {
    const auto self = s::pos(vehicle);
    const auto player = s::owner(vehicle);
    Threats threats{};
    auto inspect = [&](Address observer) {
        const auto sensor = read<Address>(observer, 0xDD0);
        if (!sensor || read<Address>(sensor, 0x260) != observer) return;
        const auto list = s::span(sensor, 0x280);
        if (!list.valid(sizeof(Address), 4096)) return;
        for (auto p = list.first; p != list.end; p += sizeof(Address)) {
            const auto sub = read<Address>(p);
            if (!sub || read<Byte>(sub, 0x6C)) continue;
            const auto f = read<unsigned>(sub, 0x10), relation = read<unsigned>(sub, 0x14);
            Contact c{};
            c.stamp = read<Tick>(sub, 0x64);
            c.visible = (f & (1u << 29)) != 0;
            if ((relation & 0x1Eu) != 2 || (f & (1u << 16)) ||
                (!c.visible && (!(f & (1u << 28)) || !sim::recent(w, c.stamp, 10))))
                continue;
            c.point = read<Vec3>(sub, 0x28);
            if (!sim::finite(c.point)) continue;
            c.metres = std::hypot(c.point.x - self.x, c.point.y - self.y) / w.length;
            if (!(c.metres > 0.f && c.metres <= (at_only ? 80.f : 120.f))) continue;
            c.entity = read<Address>(sub, 0x18);
            if (!at_only) {
                threats.add(c);
            } else {
                const auto last_attack = read<Tick>(sub, 0x68);
                if (!((relation & 0x100u) && sim::recent(w, last_attack, 1)) &&
                    !((relation & 0x80u) && sim::recent(w, last_attack, 2)))
                    continue;
                const auto tier =
                    tank_facing::detail::classify_contact(c.entity, read<float>(sub, 0x58), c.metres, true, player);
                using Tier = tank_facing::policy::ThreatTier;
                if (tier == Tier::heavy_at || tier == Tier::light_at) {
                    threats.close_at = true;
                    return;
                }
            }
        }
    };
    inspect(vehicle);
    if (!threats.close_at)
        s::actors(w, [&](Address ally) {
            if (!threats.close_at && ally != vehicle && (read<unsigned>(ally, 0xDB4) & 0x100u) &&
                s::within(self, s::pos(ally), 45.f * w.length) && sim::allies(base, player, s::owner(ally)) &&
                (!(read<Byte>(ally, 0x30) & 4) || !crew::detail::vitality(ally)))
                inspect(ally);
        });
    return threats;
}

// Once the native obstacle pass starts, finish it exactly once in the stock order. Its mode-3 trials consume RNG.
inline Address choose(Address root, Address node, Address slot, s::Span doors, Vec3 origin, XY threat, const Vec3* ref,
                      unsigned flags) {
    std::array<policy::Door, 32> accepted{};
    std::size_t count = 0;
    Address stock = 0;
    float closest = read<float>(base, 0x1099C40);
    for (auto d = doors.first; d != doors.end; d += 0xF8) {
        if (!read<Address>(d, 0x60)) continue;
        const auto target = native<Address (*)(Address, Address)>(0x79E8A0)(root, d);
        if (!native<Byte (*)(Address, Address, Address)>(0x79EDE0)(root, node, target)) continue;
        const auto bone = read<Address>(d, 0x60);
        ops.update(bone);
        if (read<Address>(d, 0x68)) continue;
        const auto until = read<Tick>(d, 0x74);
        if (until && until > native<Tick (*)()>(runtime::table().auth.clock_read)()) continue;
        const auto occupant = read<Address>(slot, 0x280);
        if (ops.blocked((read<Byte>(occupant, 0x30) & 2) ? occupant : 0, d, bone)) continue;
        const auto point = read<Vec3>(bone, 0xCC);
        accepted[count++] = {d, point};
        if (flags & 4u) {
            const auto seat_bone = read<Address>(slot, 0x240);
            if (seat_bone) ops.update(seat_bone);
            const auto pivot = seat_bone ? read<Vec3>(seat_bone, 0xCC) : *ref;
            const float dx = point.x - pivot.x, dy = point.y - pivot.y, dz = point.z - pivot.z;
            const float dist = (dy * dy + dx * dx) + dz * dz;
            if (dist < closest) {
                closest = dist;
                stock = d;
            }
        } else {
            stock = d;
        }
    }
    const auto safest = policy::pick_safest_door(origin, threat, accepted.data(), count, stock);
    if (safest) ++stat_safe_doors;
    return safest ? safest : stock;
}
inline Address select_door(Address boarder, const short* place, const Vec3* ref, unsigned flags) {
    runtime::HookGuard guard(gate);
    auto stock = [&]() { return original(boarder, place, ref, flags); };
    if (!gate.active() || !config::active().modules.safe_disembark || (flags & 1u) || !boarder || !place)
        return stock();
    const auto current = ops.current();
    if (!current || !ops.host(base)) return stock();
    const auto a = read<Address>(boarder, 0x58);
    World w{};
    if (!s::authorized_vehicle_brain(base, a, true) || read<Address>(a, 0x570) != boarder || !ops.world(base, w))
        return stock();
    const auto seats = s::span(read<Address>(a, 0x568), 0x28);
    if (*place < 0 || !seats.valid(0x298, 4096) || std::size_t(*place) >= (seats.end - seats.first) / 0x298)
        return stock();
    const auto slot = native<Address (*)(Address, short)>(0x799150)(boarder, *place);
    const auto occupant = read<Address>(slot, 0x280);
    if (slot != seats.first + std::size_t(*place) * 0x298 || !available_passenger(occupant, a) ||
        ((flags & 4u) && !ref && !read<Address>(slot, 0x240)))
        return stock();
    const auto node = native<Address (*)(Address, const short*)>(0x79E930)(boarder, place);
    const auto root = native<Address (*)(Address)>(0x79D790)(boarder);
    const auto doors = s::span(root, 0xA8);
    const auto hull = read<Address>(root, 0x58);
    if (!node || !root || read<Address>(hull, 0x570) != root || !s::authorized_vehicle_brain(base, hull) ||
        (current != a && current != occupant && current != hull) || !sim::allies(base, s::owner(a), s::owner(hull)) ||
        !doors.valid(0xF8, 32) || doors.end - doors.first < 2 * 0xF8)
        return stock();
    const auto origin = s::pos(hull);
    const auto threat = gather(w, hull, false).direction(origin);
    if (!runtime::finite(threat) || std::hypot(threat.x, threat.y) < 1e-5f) return stock();
    return choose(root, node, slot, doors, origin, threat, ref, flags);
}

inline bool loaded(Address a) {
    const auto placer = read<Address>(a, 0x568), boarder = read<Address>(a, 0x570);
    if (!placer || read<Address>(placer, 0x40) != a || read<Byte>(placer, 0x48) || !boarder ||
        read<Address>(boarder, 0x58) != a || native<Address (*)(Address)>(0x79D790)(boarder) != boarder)
        return false;
    const auto walkers = s::span(boarder, 0x60), seats = s::span(placer, 0x28), occupied = s::span(placer, 0x10);
    if (!walkers.valid(0x90, 4096) || crew::detail::has_exit_walker(walkers) || !seats.valid(0x298, 4096) ||
        !occupied.valid(sizeof(short), 4096))
        return false;
    bool has_passenger = false;
    for (auto p = occupied.first; p != occupied.end; p += sizeof(short)) {
        const auto i = read<short>(p);
        if (i < 0 || std::size_t(i) >= (seats.end - seats.first) / 0x298) return false;
        const auto seat = seats.first + std::size_t(i) * 0x298;
        const auto facts = crew::detail::read_slot(seat, i);
        // eEmit Start 0x8FCCD0 mode 1 skips only registered crew (0x7943C0). Every other occupant can leave.
        if (facts.flags & 0x1000u) continue;
        const auto h = read<Address>(seat, 0x280);
        if (!crew::policy::is_passenger_source(facts, false) || facts.reserved || !available_passenger(h, a) ||
            !s::free_slot(h) || crew::detail::is_walking(walkers, h))
            return false;
        has_passenger = true;
    }
    return has_passenger;
}
inline bool inspect_damage(Address a, policy::Damage& d) {
    const auto parts = s::span(a, 0xB90);
    if (!parts.valid(0x28, 1024)) return false;
    for (auto p = parts.first; p != parts.end; p += 0x28) {
        const auto def = read<Address>(p);
        if (!def || read<Address>(p, 8) != a) return false;
        const auto names = s::span(def, 0x328);
        if (!names.valid(0x60, 4096)) return false;
        if (names.first == names.end) continue;
        const auto name = s::native_string(names.first + 0x40);
        const bool track = name.substr(0, 5) == "track" || s::tag(names, "track");
        d.tracks |= track;
        if (read<Byte>(p, 0x24)) {
            d.body_or_engine |= name == "body" || name == "engine";
            d.immobilized |= track || name == "engine" || name.substr(0, 5) == "wheel";
        }
    }
    const auto burning = read<unsigned>(base, 0x15C6A58);
    d.burning = burning < 32 && (read<Byte>(a, 0xDAD + burning / 8) & (1u << (burning % 8)));
    return true;
}
struct State {
    s::Identity id{};
    Address brain{};
    unsigned owner{};
    Tick next_scan{}, cooldown{};
};
inline store::GrowableTable<State, 64, 4096> states{};
inline Address last_scene{};
inline Tick last_clock{};
inline bool issuing{};
inline State* reserve(Address a, Address b, Tick now) {
    const auto id = s::identity(a);
    const auto player = s::owner(a);
    State* spare = nullptr;
    for (auto& row : states) {
        if (row.id.ptr == a) {
            if (row.id.matches(a) && row.id.mid == id.mid && row.brain == b && row.owner == player) return &row;
            spare = &row;
            break;
        }
        if (!spare && (!row.id.ptr || (!tank_auto_repair::detail::pending(row.cooldown, now) &&
                                       !tank_auto_repair::detail::pending(row.next_scan, now))))
            spare = &row;
    }
    if (!spare) spare = states.append_slot();
    if (spare) *spare = {id, b, player, 0, 0};
    return spare;
}
inline bool idle(Address a, Address b) {
    return !(read<unsigned>(b, 0x39C) & ((1u << 6) | (1u << 9) | (1u << 20) | (1u << 28) | (1u << 29))) &&
           !read<int>(b, 0x88) && read<Address>(b, 0x98) == read<Address>(b, 0xA0) && s::free_slot(a);
}
inline void unload(const World& w, Address a, Address b) {
    if (!idle(a, b) || s::extender(a, runtime::table().vt.cannon_ext, base)) return;
    auto* st = reserve(a, b, w.now);
    if (!st || tank_auto_repair::detail::pending(st->cooldown, w.now) ||
        tank_auto_repair::detail::pending(st->next_scan, w.now))
        return;
    st->next_scan = w.now + std::max<Tick>(1, w.duration(0.5f));
    policy::Damage d{};
    if (!inspect_damage(a, d) || (d.tracks && s::tank_class(a)) || !loaded(a)) return;
    bool stationary = false, threat = false;
    if (!d.burning && !d.body_or_engine) {
        Vec3 velocity{};
        if (!d.immobilized) {
            native<Vec3* (*)(Address, Vec3*)>(0x73A2A0)(a, &velocity);
            stationary = sim::finite(velocity) && velocity.x == 0.f && velocity.y == 0.f && velocity.z == 0.f;
            if (!stationary) return;
        }
        threat = tank_auto_repair::detail::pending(read<Tick>(b, 0x3F8), w.now) || gather(w, a, true).close_at;
    }
    if (!policy::should_unload_transport(d, stationary, threat)) return;
    const auto cb = native<Address (*)(Address)>(0x9AC4E0)(a);
    if (!cb) return;
    const auto emit = native<Address (*)(int, int, Address)>(0x8FC550)(-1, 1, cb);
    if (!emit) {
        ops.discard(cb); // The new callback never acquired an order owner.
        return;
    }
    if (!gate.active() || !ops.host(base) || ops.current() != a || s::resolve(w, st->id) != a ||
        s::authorized_vehicle_brain(base, a) != b || !idle(a, b) || !loaded(a)) {
        ops.discard(emit); // eEmit's deleting destructor releases its callback.
        return;
    }
    st->cooldown = w.now + w.duration(15);
    // SetOrder takes ownership, including when native callbacks subsequently replace the installed root.
    read<void (*)(Address, Address)>(read<Address>(b), 0x68)(b, emit);
    const auto live = s::resolve(w, st->id);
    if (live && read<Address>(live, 0xDC8) == b && read<Address>(b, 0xF0) == emit) ++stat_transport_unloads;
}
} // namespace detail

template <class Create, class Remove> inline bool prepare_hooks(Address image, const char* sha, Create create, Remove) {
    using namespace detail;
    if (!image || base || !sha || std::strcmp(sha, runtime::kBuild26734796Sha256) != 0) return false;
    void* trampoline{};
    if (create(reinterpret_cast<void*>(image + runtime::table().hook.select_exit_door),
               reinterpret_cast<void*>(&select_door), &trampoline))
        return false;
    base = image;
    original = reinterpret_cast<SelectDoor>(trampoline);
    stat_safe_doors = stat_transport_unloads = 0;
    gate.activate();
    return true;
}
inline void on_actor_post_quant(Address a) {
    using namespace detail;
    runtime::HookGuard guard(gate);
    if (!gate.active() || !config::active().modules.transport_unload || !a || ops.current() != a || !ops.host(base) ||
        issuing)
        return;
    World w{};
    if (!ops.world(base, w)) return;
    const auto scene = s::entity_scene(w);
    if (last_scene != scene || last_clock > w.now) states.clear();
    last_scene = scene;
    if (!scene) return;
    last_clock = w.now;
    const auto b = s::authorized_vehicle_brain(base, a);
    if (!b) return;
    struct Scope {
        Scope() {
            issuing = true;
        }
        ~Scope() {
            issuing = false;
        }
    } scope;
    unload(w, a, b);
}
inline void deactivate() {
    detail::gate.deactivate();
}
inline unsigned inflight_count() {
    return detail::gate.count();
}
inline void after_hooks_removed() {
    deactivate();
    detail::states.release();
    detail::last_scene = detail::last_clock = 0;
    detail::ops = {};
    detail::original = nullptr;
    detail::base = 0;
}
} // namespace goh::safe_disembark
