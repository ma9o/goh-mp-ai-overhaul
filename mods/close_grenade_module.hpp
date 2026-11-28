#pragma once

#include "actor_coordinator.hpp"
#include "ai_support_26734796.hpp"
#include "config_and_log.hpp"
#include "dynamic_store.hpp"
#include "game_classifiers.hpp"
#include "target_prioritization_module.hpp"

namespace goh::close_grenade {
namespace pr = target_prio::detail;
namespace sim = auto_smoke;
using pr::base;
using pr::Byte;
using pr::native;
using pr::read;
using pr::Span;
using pr::span;
using pr::tag_equals;
using pr::Vec3;
using target_prio::Address;

enum class Payload {
    none,
    frag,
    charge,
    at,
    incendiary
};
inline Payload payload(Address def) {
    if (!def || !pr::has_tag(def, "missile") || !pr::has_tag(def, "grenade")) return Payload::none;
    Address props = read<Address>(def, 0x7D0);
    if (!props || read<Address>(props) != base + 0x1100908) return Payload::none;
    // Molotov definitions also carry the AT tag.
    if (pr::has_tag(def, "ic")) return Payload::incendiary;
    if (pr::has_tag(def, "dynamite")) return Payload::charge;
    if (pr::has_tag(def, "at")) return Payload::at;
    return pr::has_tag(def, "fg") ? Payload::frag : Payload::none;
}
enum class Vehicle {
    none,
    tank,
    armored_car,
    car,
    gun
};
inline Vehicle vehicle(Address a) {
    if (pr::tank_class(a)) return Vehicle::tank;
    Address tag = a + 0xDE8;
    if (tag_equals(tag, "armoredcar") || tag_equals(tag, "armoredcar_l")) return Vehicle::armored_car;
    if (tag_equals(tag, "car")) return Vehicle::car;
    for (const char* name : {"cannon_bullet", "cannon_small", "cannon_big", "cannon_zenite", "howitzer"})
        if (tag_equals(tag, name)) return Vehicle::gun;
    return Vehicle::none;
}
inline bool has(Span tags, const char* value) {
    if (!tags.valid(0x60)) return false;
    for (Address p = tags.first; p != tags.end; p += 0x60)
        if (tag_equals(p, value)) return true;
    return false;
}
struct Aim {
    Address entity{}, volume{};
    Vec3 point{};
    bool station_point{}, open_deck{}, gunner{};
};
// An unmarked vehicle must expose the occupied weapon station to a descending
// segment. A roof, hatch cover or other earlier contact disqualifies it.
inline bool exposed_from_above(Address target, Address crew, Vec3 seat, bool permit_clear_endpoint) {
    float scale = pr::spatial_scale();
    float hull = read<float (*)(Address)>(read<Address>(target), 0x80)(target);
    if (!pr::finite(seat) || !std::isfinite(hull) || hull <= 0) return false;
    Vec3 start = seat;
    start.z = (std::max)(seat.z, read<Vec3>(target, 0x74).z + hull) + 2.f * scale;
    alignas(16) std::byte query[0x100]{};
    pr::HitVec hits{};
    native<void* (*)(void*, unsigned, Address, int)>(0xA409C0)(query, 0x86803, pr::scope.actor, -1);
    native<void (*)(void*, const Vec3*, const Vec3*)>(0xA40A20)(query, &start, &seat);
    native<Byte (*)(void*, pr::HitVec*)>(0xA40AC0)(query, &hits);
    Address q = reinterpret_cast<Address>(query);
    pr::free_buffer(read<Address>(q, 0x90), read<Address>(q, 0xA0), 8);
    bool exposed = hits.begin == hits.end ? permit_clear_endpoint
                                          : crew && hits.begin && hits.begin->entity == crew && hits.begin->volume;
    pr::free_buffer(reinterpret_cast<Address>(hits.begin), reinterpret_cast<Address>(hits.cap), sizeof(pr::SceneHit));
    return exposed;
}
// Exact raw entity keys from the audited, unmarked stock definitions. A
// secondary gunner or an external rider alone cannot qualify unknown armour.
inline constexpr std::string_view unmarked_open[] = {"sdkfz250a_1",
                                                     "sdkfz250a_3",
                                                     "sdkfz250a_11",
                                                     "sdkfz251b",
                                                     "sdkfz251c",
                                                     "sdkfz251c_2",
                                                     "sdkfz251d_1",
                                                     "sdkfz251d_1_stuka",
                                                     "sdkfz251d_16",
                                                     "sdkfz251d_17",
                                                     "sdkfz251d_21",
                                                     "sdkfz251d_22",
                                                     "unic304f",
                                                     "su26",
                                                     "stuart5_recce",
                                                     "lvt4",
                                                     "lvt4_eng",
                                                     "m13_mgmc",
                                                     "m16_mgmc",
                                                     "m21_mortar_carrier",
                                                     "m4_mortar_carrier",
                                                     "m4a1_mortar_carrier",
                                                     "t19_hmc",
                                                     "t30_hmc",
                                                     "mortar_carrier_mk1",
                                                     "mortar_carrier_mk1_boys",
                                                     "mortar_carrier_mk2"};
inline bool supported_armour(bool marked, std::string_view key) {
    return marked || std::find(std::begin(unmarked_open), std::end(unmarked_open), key) != std::end(unmarked_open);
}
inline Aim inspect_station(Address target, Vehicle type, bool gunner_only) {
    const auto props = span(read<Address>(target, 0x548), 0x330);
    const bool open = has(props, "open_top") || has(props, "ammo_open_top_turret");
    const bool open_crew = has(props, "explosion_throw_crew");
    const auto key = ai_support::native_string(target + 0xF0);
    const bool known_open = open || open_crew || supported_armour(false, key);
    const bool roof_check = !open && (type == Vehicle::tank || type == Vehicle::armored_car || type == Vehicle::car);
    if ((type == Vehicle::tank || type == Vehicle::armored_car) && !known_open) return {};
    const bool halftrack = has(props, "halftrack");
    Aim vacant{};
    const Address placer = read<Address>(target, 0x568);
    if (!placer || read<Address>(placer, 0x40) != target) return {};
    const auto slots = span(placer, 0x28);
    if (!slots.valid(0x298)) return {};
    for (Address p = slots.first; p != slots.end; p += 0x298) {
        Address bone = read<Address>(p, 0x240);
        if (!tag_equals(p + 0x60, "crew") || !bone || !read<std::size_t>(p, 0x170)) continue;
        const bool car_driver = type == Vehicle::car && tag_equals(p, "driver");
        const bool gunner = tag_equals(p, "gunner");
        const bool auxiliary = tag_equals(p, "gunner2") || tag_equals(p, "gunner4");
        if (gunner_only && !gunner && !auxiliary) continue;
        if (!gunner && !auxiliary && !car_driver && !tag_equals(p, "charger")) continue;
        // An exterior AA gun, passenger or commander on a closed tank is not
        // its fighting compartment. Unmarked armour needs a permanent primary
        // gunner link, or an open halftrack/carrier's exposed gunner/charger link.
        if (roof_check && type != Vehicle::car && !(gunner && tag_equals(p + 0x120, "gunner")) &&
            !((halftrack || open_crew) && auxiliary &&
              (tag_equals(p + 0x120, "gunner2") || tag_equals(p + 0x120, "gunner4"))))
            continue;
        unsigned flags = read<unsigned>(p, 0x248);
        if ((flags & 0x34) || ((flags & 8) && !car_driver)) continue;
        Address crew = read<Address>(p, 0x280);
        if (!gunner_only && !crew && open && gunner && !vacant.entity && !(read<Byte>(bone, 0x10) & 1)) {
            Vec3 point = read<Vec3>(bone, 0xCC);
            if (pr::finite(point) && exposed_from_above(target, 0, point, true))
                vacant = {target, 0, point, true, true};
        }
        if (!sim::has_actor(crew) || !(read<Byte>(crew, 0x30) & 4) || !(read<Byte>(crew, 0x38) & 0x40) ||
            read<unsigned>(crew, 0xC0) != read<unsigned>(p, 0x250))
            continue;
        Address brain = read<Address>(crew, 0xDC8);
        if (!brain) continue;
        const bool targetable = (read<unsigned>(brain, 0x398) & (1u << 15)) != 0;
        if (!targetable && !car_driver) continue;
        using Relation = int (*)(Address, Address, Byte);
        if (native<Relation>(0xA65320)(pr::scope.actor, crew, 0) != 1) continue;
        if (!targetable && (read<Byte>(bone, 0x10) & 1)) continue;
        Vec3 point = targetable ? read<Vec3>(crew, 0x74) : read<Vec3>(bone, 0xCC);
        if (pr::finite(point) && (!roof_check || exposed_from_above(target, crew, point, car_driver)))
            return {crew, 0, point, !targetable, known_open || car_driver, gunner || auxiliary};
    }
    return vacant;
}
inline Aim fighting_station(Address target, Vehicle type) {
    return inspect_station(target, type, false);
}
inline bool small_arms_mount(Address target, Vehicle type) {
    if (type == Vehicle::gun) return true;
    if (type != Vehicle::car) return false;
    const auto tags = span(read<Address>(target, 0x548), 0x330);
    if (has(tags, "halftrack") || has(tags, "armoredcar") || has(tags, "tank") || has(tags, "apc") ||
        supported_armour(false, ai_support::native_string(target + 0xF0)))
        return false;
    const auto places = ai_support::span(read<Address>(target, 0xdd8), 0x30);
    if (!places.valid(8, 256)) return false;
    for (auto it = places.first; it != places.end; it += 8) {
        const auto place = read<Address>(it);
        if (place && read<Address>(place, 0x90) == target && pr::has_tag(read<Address>(place, 0x98), "mgun"))
            return true;
    }
    return false;
}
inline float chamber_range(Address def, Address ammo) {
    const auto weapon = native<pr::Unary>(0x83F1E0)(def);
    const auto profiles = read<Address>(weapon, 0x1600);
    if (!profiles || !native<Address (*)(Address, Address)>(0x83DE90)(profiles, ammo + 0xEC8)) return 0;
    return native<float (*)(Address, Address)>(0x83F210)(weapon, ammo);
}
template <class Range> bool loaded_small_arm_reaches(Address actor, Address item, float distance, Range range) {
    const auto def = read<Address>(item, 0x40), ammo = read<Address>(item, 0x50);
    if (!def || !ammo || read<int>(item, 0x48) <= 0 || !pr::has_tag(def, "weapon") ||
        !(pr::has_tag(def, "rifle") || pr::has_tag(def, "smg") || pr::has_tag(def, "mgun")) ||
        classify::def_is_ranged_at_weapon(def) || pr::has_tag(def, "pistol") || read<int>(item, 0xdc) <= 0 ||
        !read<Address>(actor, 0x1390) || !pr::usable_item(actor, def))
        return false;
    const float reach = range(def, ammo);
    return std::isfinite(reach) && distance > 0 && distance <= reach;
}
inline bool has_small_arm_shot(Address actor, float distance) {
    const auto hand = pr::hand_item(actor);
    if (loaded_small_arm_reaches(actor, hand, distance, chamber_range)) return true;
    const auto box = ai_support::box(actor);
    const auto items = ai_support::span(box, 0x10);
    if (!box || !items.valid(8, 1024)) return false;
    for (auto it = items.first; it != items.end; it += 8) {
        const auto item = read<Address>(it);
        if (item && item != hand && read<Address>(item, 0x30) == box && !read<Address>(item, 0x38) &&
            read<Address>(item, 0x10) == read<Address>(item, 0x18) &&
            loaded_small_arm_reaches(actor, item, distance, chamber_range))
            return true;
    }
    return false;
}
inline bool clear_gunner_ray(const pr::HitVec& hits, Address gun, Address crew, float distance) {
    for (auto h = hits.begin; h != hits.end; ++h) {
        if (!std::isfinite(h->distance)) return false;
        if (h->distance > distance) break;
        if (h->kind == 1 || h->kind == 2) return false;
        if (!h->volume || (read<Byte>(h->volume, 0x28) & 0x20)) continue;
        if (read<unsigned>(h->volume, 0x0c) & 2) return h->entity == crew;
        if (h->entity == gun) {
            const auto tags = span(read<Address>(h->volume, 0x30), 0x398);
            if (has(tags, "shield") || has(tags, "gun_shield") || has(tags, "armor")) return false;
        }
    }
    return true;
}
template <class Armed, class Clear>
bool gunner_shot(const sim::World& w, Address actor, Address target_gun, Vehicle type, const Aim& aim, Armed armed,
                 Clear clear) {
    const auto crew = aim.entity;
    if (!config::active().modules.close_grenade || !config::active().modules.mg_crew_small_arms ||
        !small_arms_mount(target_gun, type) || !aim.gunner || aim.station_point || crew == target_gun ||
        !sim::has_actor(crew) || !(read<Byte>(crew, 0x30) & 4) || !(read<unsigned>(crew, 0xdb4) & 0x100))
        return false;
    const auto brain = read<Address>(crew, 0xdc8), vit = read<Address>(read<Address>(crew, 0x578), 0x18);
    if (!brain || read<Address>(brain, 0x38) != crew || !(read<unsigned>(brain, 0x398) & (1u << 15)) || !vit ||
        (read<unsigned>(vit, 0x174) & 3))
        return false;
    const auto from = pr::raised_point(read<Vec3>(actor, 0x74), actor, pr::RayPoint::muzzle, w.length);
    const auto to = pr::raised_point(aim.point, crew, pr::RayPoint::target, w.length);
    const auto d = pr::distance(from, to);
    return pr::finite(from) && pr::finite(to) && armed(actor, d) && clear(pr::Ray{0, crew, 0, 0, from, to, false}, d);
}
inline bool exposed_mg_crew_shootable(const sim::World& w, Address actor, Address target_gun, Vehicle type,
                                      const Aim& aim) {
    return gunner_shot(w, actor, target_gun, type, aim, has_small_arm_shot, [&](const pr::Ray& ray, float d) {
        return pr::visit_segment(
            ray, [&](const pr::HitVec& hits) { return clear_gunner_ray(hits, target_gun, ray.target, d); });
    });
}
inline bool defer_for_gunner(Payload kind, bool held, bool inside, float meters) {
    return !(kind == Payload::frag && held && inside && meters < 10.f);
}
inline bool qualifies_cover_frag(unsigned cluster_count, bool target_covered, bool target_high_value,
                                 bool direct_shot_blocked, float speed_mps, float dist_m, float min_safe_m,
                                 float max_throw_m) noexcept {
    if (!std::isfinite(speed_mps) || !std::isfinite(dist_m) || !(dist_m >= min_safe_m && dist_m <= max_throw_m) ||
        !(speed_mps <= 1.2f))
        return false;
    if (cluster_count >= 2) return true;
    return target_covered && (target_high_value || direct_shot_blocked);
}
inline std::atomic<unsigned> stat_mg_small_arms_defer{}, stat_cover_frags{};
template <class Locate> inline Aim damage_volume(Address target, Payload kind, Locate locate) {
    Aim body{};
    bool intact_engine = false;
    auto volumes = span(target, 0x510);
    if (!volumes.valid(8)) return {};
    for (Address p = volumes.first; p != volumes.end; p += 8) {
        Address v = read<Address>(p);
        if (!v) continue;
        Address part = read<Address>(v, 0x38);
        if (part && read<Byte>(part, 0x24)) continue;
        Span tags = span(read<Address>(v, 0x30), 0x398);
        const bool engine = kind == Payload::incendiary && has(tags, "engine");
        intact_engine |= engine;
        if (!read<Address>(v, 0x48)) continue;
        if ((!engine && (body.entity || !has(tags, "body"))) || has(tags, "no_target")) continue;
        Vec3 point{};
        if (!locate(v, tags.first, point)) continue;
        if (!pr::finite(point)) continue;
        if (engine) return {target, v, point};
        body = {target, v, point};
        if (kind != Payload::incendiary) break;
    }
    return intact_engine ? Aim{} : body;
}
inline Aim aim_at(Address target, Vehicle type, Payload kind) {
    if (kind == Payload::frag) return fighting_station(target, type);
    return damage_volume(target, kind, [target](Address v, Address primary, Vec3& point) {
        // The native eTarget deep-copies the first name. Verify the inverse
        // lookup resolves the exact live volume before using its world center.
        using Find = Address (*)(Address, Address);
        if (native<Find>(0x7AA9C0)(target + 0x500, primary) != v) return false;
        native<void (*)(Address)>(0x7A8A30)(v);
        point = read<Vec3>(v, 0x7C);
        return true;
    });
}
inline bool has_explicit_attack(Address brain) {
    Address order = read<Address>(brain, 0xF0);
    for (unsigned i = 0; order && i < 32; ++i) {
        if (read<Address>(order) == base + 0x110C018) return true;
        order = sim::orders::active_child(base, order);
    }
    return order != 0;
}
inline bool human_policy(Address brain) {
    if (!pr::admitted(brain) || !pr::is_human_brain(brain)) return false;
    const Address a = pr::scope.actor;
    unsigned policy = read<unsigned>(brain, 0x398);
    return ((policy >> 10) & 3) != 3 && !has_explicit_attack(brain) && (policy & 3) <= 1 && sim::has_actor(a) &&
           (read<unsigned>(a, 0xDB4) & 0x100) && !(read<unsigned>(a, 0x1C0) & (1u << 28)) && read<Address>(a, 0x578) &&
           read<Address>(a, 0xDD0) && read<Address>(read<Address>(a, 0xDD0), 0x260) == a;
}
struct Claim {
    Address target{}, actor{};
    unsigned target_id{}, actor_id{}, start{}, duration{};
    bool released{};
    bool approaching{};
    Vec3 point{};
    bool cover_frag{};
};
inline store::GrowableTable<Claim, 64, 4096> claims{};
struct RushClaim {
    Address target{}, actor{}, order{};
    unsigned target_id{}, actor_id{}, start{}, duration{};
};
inline store::GrowableTable<RushClaim, 64, 4096> rushes{};
struct Failure {
    Address actor{}, target{};
    unsigned actor_id{}, target_id{}, start{}, duration{};
};
inline store::GrowableTable<Failure, 64, 4096> failures{};
inline unsigned failure_cursor{};
inline Address scene{};
inline unsigned last_tick{}, epoch{};
struct Poll {
    Address actor{};
    unsigned id{}, tick{};
};
inline store::GrowableTable<Poll, 1024, 1024> polls{};
struct Shot {
    std::atomic<Address> order{}, shooter{};
    Address actor{}, target{}, definition{}, aim_entity{};
    unsigned actor_id{}, target_id{}, issued{}, scene_epoch{};
    Aim bound{};
    Payload kind{};
    Byte abort_code{};
    bool spent{}, emitted{};
};
inline store::StableTable<Shot, 64, 4096> shots{};
inline runtime::HookGate hook_gate{};
inline std::atomic<bool> accepting{};
inline std::atomic<unsigned> admissions{};
inline void deactivate() {
    hook_gate.deactivate();
    accepting = false;
}
inline unsigned inflight_count() {
    return hook_gate.count();
}
struct Admission {
    Admission() {
        ++admissions;
    }
    ~Admission() {
        --admissions;
    }
};
inline bool holder_alive(Address actor, unsigned actor_id) {
    if (!base) return true;
    return sim::has_actor(actor) && read<unsigned>(actor, 0xC0) == actor_id && (read<unsigned>(actor, 0xDB4) & 0x100);
}
inline bool is_rushing(const sim::World& w, Address actor) {
    if (w.scene != scene || w.now < last_tick) return false;
    const auto id = read<unsigned>(actor, 0xC0);
    for (const auto& r : rushes)
        if (r.target && r.actor == actor && r.actor_id == id && w.now - r.start < r.duration) return true;
    return false;
}
inline bool is_rush_order(const sim::World& w, Address actor, Address order) {
    if (!order || w.scene != scene || w.now < last_tick) return false;
    const auto id = read<unsigned>(actor, 0xC0);
    for (const auto& r : rushes)
        if (r.target && r.actor == actor && r.actor_id == id && r.order == order && w.now - r.start < r.duration)
            return true;
    return false;
}
inline bool is_throwing(const sim::World& w, Address actor) {
    if (w.scene != scene || w.now < last_tick) return false;
    const auto id = read<unsigned>(actor, 0xC0);
    for (const auto& c : claims)
        if (c.target && !c.released && !c.approaching && c.actor == actor && c.actor_id == id &&
            w.now - c.start < c.duration)
            return true;
    for (const auto& s : shots)
        if ((s.order.load() || s.shooter.load()) && s.scene_epoch == epoch && s.actor == actor && s.actor_id == id)
            return true;
    return false;
}
inline bool is_busy(const sim::World& w, Address actor) {
    if (w.scene != scene || w.now < last_tick) return false;
    const auto id = read<unsigned>(actor, 0xC0);
    for (const auto& c : claims)
        if (c.target && !c.released && c.actor == actor && c.actor_id == id && w.now - c.start < c.duration)
            return true;
    return is_throwing(w, actor) || is_rushing(w, actor);
}
// Changing the current order is permissible on an empty slot, an autonomous
// rifle burst, or our own tracked AT-grenade rush order once inside throw range.
// Every other command, including a surrounding Attack or player move, wins.
inline bool replaceable(Address brain, const sim::World* w = nullptr, bool allow_rush = false) {
    Address root = read<Address>(brain, 0xE0);
    if (!root || read<Address>(root) != base + 0x1110CD0 || read<Address>(root, 0x30) != brain ||
        read<unsigned>(root, 0x4C0) || read<Address>(root, 0x568) || read<int>(brain, 0x88) ||
        read<Address>(brain, 0x98) != read<Address>(brain, 0xA0) || (read<unsigned>(brain, 0x39C) & 0x11100000u))
        return false;
    Address order = read<Address>(brain, 0xF0);
    if (!order) return true;
    if (read<Address>(order, 8) != pr::scope.actor + 0xAF0) return false;
    if (allow_rush && w && is_rush_order(*w, pr::scope.actor, order)) return true;
    if (read<Address>(order) == base + 0x110D490) {
        if ((read<unsigned>(order, 0x20) & 7) || read<Address>(order, 0x50) || read<std::size_t>(order, 0x48) > 1)
            return false;
        if (!read<std::size_t>(order, 0x48)) return true;
        order = sim::orders::active_child(base, order);
    }
    return order && read<Address>(order) == base + 0x110A6E0;
}
inline bool drain() {
    accepting = false;
    if (admissions.load()) return false;
    for (auto& s : shots) {
        if (s.order) return false;
        Address sh = s.shooter.load();
        if (sh) {
            if (s.emitted || s.spent || !read<Byte>(sh, 0x15)) {
                s.shooter.compare_exchange_strong(sh, 0);
            } else {
                return false;
            }
        }
    }
    return true;
}
inline unsigned long long mounts{}, fires{}, releases{}, vetoes{}, aborts{};
inline void clear_state() {
    claims.clear();
    rushes.clear();
    failures.clear();
    failure_cursor = 0;
    polls.clear();
    for (auto& shot : shots) {
        shot.order = 0;
        shot.shooter = 0;
    }
}
inline bool clock(sim::World& w) {
    if (!sim::world(base, w)) return false;
    if (w.scene != scene || w.now < last_tick) {
        claims.clear();
        rushes.clear();
        failures.clear();
        failure_cursor = 0;
        polls.clear();
        ++epoch; // old orders lose release authority until their native destructors run
        scene = w.scene;
    }
    last_tick = w.now;
    return true;
}
inline bool in_backoff(const sim::World& w, Address actor, Address target) {
    const unsigned aid = read<unsigned>(actor, 0xC0), tid = read<unsigned>(target, 0xC0);
    for (const auto& f : failures) {
        if (!f.actor || w.now - f.start >= f.duration) continue;
        if (f.actor == actor && f.actor_id == aid && f.target == target && f.target_id == tid) return true;
    }
    return false;
}
inline void note_failure(const sim::World& w, Address actor, unsigned aid, Address target, unsigned tid,
                         float seconds = 3.f) {
    Failure* empty = nullptr;
    for (auto& f : failures) {
        if (f.actor == actor && f.actor_id == aid && f.target == target && f.target_id == tid) {
            f.start = w.now;
            f.duration = w.duration(seconds);
            return;
        }
        if (!empty && (!f.actor || w.now - f.start >= f.duration)) empty = &f;
    }
    if (!empty) empty = failures.append_slot();
    if (!empty && !failures.empty()) empty = &failures[failure_cursor++ % failures.size()];
    if (empty) *empty = {actor, target, aid, tid, w.now, w.duration(seconds)};
}
inline void release_claim(Address actor, unsigned aid, Address target, unsigned tid) {
    for (auto& c : claims) {
        if (c.target == target && c.target_id == tid && c.actor == actor && c.actor_id == aid && !c.released) {
            c.target = 0;
            return;
        }
    }
}
inline Claim* available(const sim::World& w, Address target, Address actor, bool in_range = true) {
    Claim* empty = nullptr;
    for (auto& c : claims) {
        if (!c.target || w.now - c.start >= c.duration || (!c.released && !holder_alive(c.actor, c.actor_id))) {
            c.target = 0;
            if (!empty) empty = &c;
            continue;
        }
        if (c.target == target && c.target_id == read<unsigned>(target, 0xC0)) {
            if (!c.released &&
                ((c.actor == actor && c.actor_id == read<unsigned>(actor, 0xC0)) || (c.approaching && in_range)))
                return &c;
            return nullptr;
        }
    }
    if (!empty) empty = claims.append_slot();
    return empty;
}
inline void reserve(Claim& c, const sim::World& w, Address target, Address a, bool released, bool approaching = false,
                    Vec3 point = {}, bool cover_frag = false) {
    c = {target,
         a,
         read<unsigned>(target, 0xC0),
         read<unsigned>(a, 0xC0),
         w.now,
         w.duration(released ? 9.f : 3.f),
         released,
         approaching,
         point,
         cover_frag};
}
inline bool frag_zone_claimed(const sim::World& w, Vec3 point, Address actor) {
    const unsigned aid = read<unsigned>(actor, 0xC0);
    const float zone_r = 6.5f * w.length;
    for (auto& c : claims) {
        if (!c.target || w.now - c.start >= c.duration || (!c.released && !holder_alive(c.actor, c.actor_id))) {
            c.target = 0;
            continue;
        }
        if (!c.cover_frag || !pr::finite(c.point)) continue;
        if (!c.released && c.actor == actor && c.actor_id == aid) continue;
        if (pr::distance(c.point, point) <= zone_r) return true;
    }
    return false;
}
inline bool rush_available(const sim::World& w, Address target, Address actor) {
    const unsigned tid = read<unsigned>(target, 0xC0), aid = read<unsigned>(actor, 0xC0);
    for (auto& c : claims) {
        if (!c.target || w.now - c.start >= c.duration || (!c.released && !holder_alive(c.actor, c.actor_id))) {
            c.target = 0;
            continue;
        }
        if (c.target == target && c.target_id == tid && c.released) return false;
    }
    unsigned others = 0;
    for (auto& r : rushes) {
        if (!r.target || w.now - r.start >= r.duration || !holder_alive(r.actor, r.actor_id)) {
            r.target = 0;
            continue;
        }
        if (r.target == target && r.target_id == tid) {
            if (r.actor == actor && r.actor_id == aid) return true;
            ++others;
        }
    }
    return others < 2;
}
inline void note_rush(const sim::World& w, Address target, Address actor, Address order) {
    if (w.scene != scene || w.now < last_tick) {
        claims.clear();
        rushes.clear();
        failures.clear();
        failure_cursor = 0;
        polls.clear();
        ++epoch;
        scene = w.scene;
    }
    last_tick = w.now;
    const unsigned tid = read<unsigned>(target, 0xC0), aid = read<unsigned>(actor, 0xC0);
    RushClaim* slot = nullptr;
    for (auto& r : rushes) {
        if (r.actor == actor && r.actor_id == aid && r.target && w.now - r.start < r.duration) {
            slot = &r;
            break;
        }
        if (!slot && (!r.target || w.now - r.start >= r.duration || !holder_alive(r.actor, r.actor_id))) slot = &r;
    }
    if (!slot) slot = rushes.append_slot();
    if (slot) {
        const Address keep = (order == 0 && slot->actor == actor && slot->actor_id == aid && slot->target == target &&
                              slot->target_id == tid)
                                 ? slot->order
                                 : order;
        *slot = {target, actor, keep, tid, aid, w.now, w.duration(5.f)};
    }
}
inline void clear_rush(Address actor) {
    const unsigned aid = read<unsigned>(actor, 0xC0);
    for (auto& r : rushes)
        if (r.actor == actor && r.actor_id == aid) r.target = 0;
}
inline bool frag_immune_ally(Address ally) {
    if (read<Byte>(ally, 0x30) & 4) return false;
    Vehicle type = vehicle(ally);
    if (type != Vehicle::tank && type != Vehicle::armored_car) return false;
    const auto props = span(read<Address>(ally, 0x548), 0x330);
    return !has(props, "open_top") && !has(props, "ammo_open_top_turret") && !has(props, "explosion_throw_crew") &&
           !supported_armour(false, ai_support::native_string(ally + 0xF0));
}
inline float ally_clearance(float radius, float scale, Payload kind, bool closed_armour) noexcept {
    if (!closed_armour) return radius;
    if (kind == Payload::frag) return 0.f;
    if (kind == Payload::at || kind == Payload::incendiary) return (std::min)(radius, 1.5f * scale);
    return radius;
}
inline bool clear_area(const sim::World& w, Address a, Vec3 point, float radius, Payload kind = Payload::at) {
    // 0x15C7D58 holds the real eScene entity vector (+0x48); w.scene (0x15C9668) is eEnvironment.
    // Offline fixtures zero-initialize the mock image so live_scene == 0 and fall back to w.scene.
    const Address live_scene = base ? read<Address>(base + 0x15C7D58) : 0;
    auto list = span(live_scene ? live_scene : w.scene, 0x48);
    unsigned owner = read<Byte>(a, 0xDA8);
    int stride = read<int>(base + 0x15C9710);
    if (owner >= 18 || !read<Address>(base + 0x15C9700) || stride <= int(owner) || stride > 256) return false;
    if (!pr::finite(point) || !std::isfinite(radius) || !(radius > 0) || !list.valid(8) || !list.size(8) ||
        list.size(8) > 131072)
        return false;
    if (!(pr::distance(read<Vec3>(a, 0x74), point) > radius + .5f * w.length)) return false;
    for (Address p = list.first; p != list.end; p += 8) {
        Address ally = read<Address>(p);
        if (!sim::has_actor(ally) || ally == a) continue;
        if ((read<Byte>(ally, 0x30) & 8) && ai_support::has_destruction_tag(ally)) continue;
        unsigned player = read<Byte>(ally, 0xDA8);
        if (player < 18 && player >= unsigned(stride)) return false;
        if (!sim::allies(base, owner, player)) continue;
        const float clear_r = ally_clearance(radius, w.length, kind, frag_immune_ally(ally));
        if (!(clear_r > 0.f)) continue;
        using Radius = float (*)(Address);
        const float hull = read<Radius>(read<Address>(ally), 0x80)(ally);
        if (!std::isfinite(hull) || hull < 0 || !(pr::distance(read<Vec3>(ally, 0x74), point) > clear_r + hull))
            return false;
    }
    return true;
}
inline float effect_radius(const sim::World& w, Address def, Payload kind) {
    float r = kind == Payload::incendiary ? 2.5f * w.length : 0.f;
    if (!pr::shell_radius(def, r)) return 0.f;
    // Cap frag/AT clearance to the lethal inner zone so 10 m concussion edges
    // or 6 m AT outer edges do not block real close-range ambushes.
    // Keep full radius for satchels/dynamite (Payload::charge).
    if (kind == Payload::frag) r = (std::min)(r, 5.5f * w.length);
    if (kind == Payload::at) r = (std::min)(r, 4.5f * w.length);
    return r;
}
struct Choice {
    Address item{}, def{}, target{}, subject{};
    Aim aim{};
    Payload kind{};
    Vehicle type{};
    bool in_range{}, held{}, cover_frag{};
    unsigned cluster_count{};
    float distance{}, radius{}, max_range{};
};
inline bool choice(const sim::World& w, Address s, const pr::Pair& p, Payload kind, Vehicle type, Choice& out,
                   bool allow_rush_range = false) {
    if (kind == Payload::none || type == Vehicle::none) return false;
    Address a = pr::scope.actor, target = read<Address>(s, 0x18);
    Vec3 self = read<Vec3>(a, 0x74), observed = read<Vec3>(s, 0x28);
    if (!pr::finite(observed) || !pr::finite(self) || in_backoff(w, a, target)) return false;
    if (allow_rush_range) {
        if (!rush_available(w, target, a)) return false;
    } else if (!available(w, target, a)) {
        return false;
    }
    if (pr::distance(self, observed) > p.max_range + (allow_rush_range ? 20.f : 12.f) * w.length) return false;
    Aim aim = aim_at(target, type, kind);
    if (!aim.entity) return false;
    float d = pr::distance(self, aim.point);
    bool inside = d < p.max_range;
    if (config::active().modules.close_grenade && config::active().modules.mg_crew_small_arms &&
        small_arms_mount(target, type)) {
        const Aim crew = aim.gunner ? aim : inspect_station(target, type, true);
        if (crew.entity && defer_for_gunner(kind, p.held, inside, pr::distance(self, crew.point) / w.length) &&
            exposed_mg_crew_shootable(w, a, target, type, crew)) {
            stat_mg_small_arms_defer.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
    }
    if (!inside) {
        if (allow_rush_range) {
            if (!(d < p.max_range + 15.f * w.length)) return false;
            Vec3 velocity{};
            native<Vec3* (*)(Address, Vec3*)>(0x73A2A0)(target, &velocity);
            // Allow rushing stationary, approaching, or slowly maneuvering vehicles; skip vehicles fleeing rapidly.
            if (!pr::finite(velocity) || pr::dot(velocity, pr::diff(observed, self)) > .004f * w.length * d)
                return false;
        } else {
            if (!(d < p.max_range + 7.f * w.length)) return false;
            Vec3 velocity{};
            native<Vec3* (*)(Address, Vec3*)>(0x73A2A0)(target, &velocity);
            if (!pr::finite(velocity) || !(pr::dot(velocity, pr::diff(self, observed)) > 0)) return false;
        }
    }
    using Reserved = Byte (*)(Address, Address, const Vec3*, Address);
    if (native<Reserved>(0x89E880)(base + 0x15B1D50, a, &observed, p.ammo)) return false;
    float radius = effect_radius(w, p.ammo, kind);
    if (!(p.max_range > radius + .5f * w.length) || !clear_area(w, a, aim.point, radius, kind)) return false;
    out = {p.item, p.ammo, target, s, aim, kind, type, inside, p.held, false, 0, d, radius, p.max_range};
    return true;
}
inline bool human_high_value(Address target) {
    const Address hand = pr::hand_item(target);
    const Address def = hand ? read<Address>(hand, 0x40) : 0;
    return def && (classify::def_is_heavy_support_weapon(def) || classify::def_is_ranged_at_weapon(def));
}
inline bool cover_frag_choice(const sim::World& w, Address s, const pr::Pair& p, const Span& subjects, Choice& out,
                              const Vec3* locked_point = nullptr) {
    if (!config::active().modules.cover_frag || !s || !p.item || !p.ammo) return false;
    Address a = pr::scope.actor, target = read<Address>(s, 0x18);
    if (!sim::has_actor(target) || !(read<Byte>(target, 0x30) & 4) || !(read<unsigned>(target, 0xDB4) & 0x100) ||
        !tag_equals(target + 0xDE8, "human") || in_backoff(w, a, target))
        return false;
    // Skip downed / incapacitated enemy soldiers.
    const Address agg = read<Address>(target, 0x578);
    const Address vit = agg ? read<Address>(agg, 0x18) : 0;
    if (vit && (read<unsigned>(vit, 0x174) & 3)) return false;
    Vec3 self = read<Vec3>(a, 0x74), observed = read<Vec3>(s, 0x28);
    if (!pr::finite(self) || !pr::finite(observed)) return false;
    float radius = effect_radius(w, p.ammo, Payload::frag);
    if (!(radius > 0.f) || !(p.max_range > radius + 2.5f * w.length)) return false;
    // Count live visible hostile humans clustered within 4.5 m of target and compute cluster centroid.
    unsigned cluster = 1;
    Vec3 sum = observed;
    if (!locked_point) {
        const float cluster_r = 4.5f * w.length;
        for (Address it = subjects.first; it != subjects.end; it += 8) {
            const Address other_s = read<Address>(it);
            if (!other_s || other_s == s || !pr::hostile_observation(other_s) || read<Byte>(other_s, 0x6C)) continue;
            const Address other = read<Address>(other_s, 0x18);
            if (!sim::has_actor(other) || other == target || !(read<Byte>(other, 0x30) & 4) ||
                !(read<unsigned>(other, 0xDB4) & 0x100) || !tag_equals(other + 0xDE8, "human"))
                continue;
            const Vec3 pos = read<Vec3>(other_s, 0x28);
            if (pr::finite(pos) && pr::distance(pos, observed) <= cluster_r) {
                sum.x += pos.x;
                sum.y += pos.y;
                sum.z += pos.z;
                ++cluster;
            }
        }
    }
    const Vec3 aim_pt = locked_point ? *locked_point : Vec3{sum.x / cluster, sum.y / cluster, sum.z / cluster};
    if (locked_point && pr::distance(*locked_point, observed) > 4.0f * w.length) return false;
    const float d = pr::distance(self, aim_pt);
    const float dist_m = d / w.length;
    const float min_safe_m = radius / w.length + 1.5f;
    const float max_throw_m = (p.max_range / w.length) - 1.0f;
    if (!(dist_m >= min_safe_m && dist_m <= max_throw_m)) return false;
    if (!locked_point) {
        Vec3 vel{};
        native<Vec3* (*)(Address, Vec3*)>(0x73A2A0)(target, &vel);
        const float speed_mps = pr::finite(vel) ? (std::sqrt(vel.x * vel.x + vel.y * vel.y) * 1000.f / w.length) : 99.f;
        const Address cov = read<Address>(target, 0xDC0);
        const bool target_covered = cov && read<int>(cov, 0x24) != 0;
        const bool hv = target_covered && human_high_value(target);
        bool blocked = false;
        if (cluster < 2 && target_covered && !hv) {
            const Vec3 from = pr::raised_point(self, a, pr::RayPoint::muzzle, w.length);
            const Vec3 to = pr::raised_point(observed, target, pr::RayPoint::target, w.length);
            const float ray_d = pr::distance(from, to);
            if (pr::finite(from) && pr::finite(to) && ray_d > 0.f)
                blocked = !pr::visit_segment(pr::Ray{0, target, 0, 0, from, to, false}, [&](const pr::HitVec& hits) {
                    return clear_gunner_ray(hits, 0, target, ray_d);
                });
        }
        if (!qualifies_cover_frag(cluster, target_covered, hv, blocked, speed_mps, dist_m, min_safe_m, max_throw_m))
            return false;
        if (!available(w, target, a) || frag_zone_claimed(w, aim_pt, a)) return false;
    }
    using Reserved = Byte (*)(Address, Address, const Vec3*, Address);
    if (!locked_point && native<Reserved>(0x89E880)(base + 0x15B1D50, a, &aim_pt, p.ammo)) return false;
    if (!clear_area(w, a, aim_pt, radius, Payload::frag)) return false;
    Aim aim{target, 0, aim_pt, true, false, false};
    out = {p.item, p.ammo, target, s,       aim, Payload::frag, Vehicle::none,
           true,   p.held, true,   cluster, d,   radius,        p.max_range};
    return true;
}
inline bool move_free_rush_eligible(Address brain) {
    const unsigned policy = read<unsigned>(brain, 0x398), flags = read<unsigned>(brain, 0x39C);
    if ((policy & 0x3000u) != 0 || ((policy & 3u) == 1 && (flags & (1u << 28)))) return false;
    return !native<pr::Predicate>(0x91B190)(brain);
}
inline bool holds_ranged_at(Address actor) {
    Address hand = pr::hand_item(actor);
    if (!hand) return false;
    Address def = read<Address>(hand, 0x40);
    return def && (classify::def_is_ranged_at_weapon(def) || pr::has_tag(def, "flame"));
}
inline int vehicle_rank(Vehicle type) {
    switch (type) {
        case Vehicle::tank: return 4;
        case Vehicle::armored_car: return 3;
        case Vehicle::car: return 2;
        case Vehicle::gun: return 1;
        default: return 0;
    }
}
inline bool better(const Choice& a, const Choice& b) {
    if (!b.item || a.in_range != b.in_range) return !b.item || a.in_range;
    if (a.target == b.target) {
        if (a.held != b.held) return a.held;
        // On open-top vehicles, cars, and guns where frag is admitted, prefer
        // frag over AT/incendiary to save anti-tank grenades for heavy armor.
        const bool a_frag = a.kind == Payload::frag, b_frag = b.kind == Payload::frag;
        if (a_frag != b_frag) return a_frag;
    } else {
        const int ra = vehicle_rank(a.type), rb = vehicle_rank(b.type);
        if (ra != rb) return ra > rb;
        if (a.cover_frag && b.cover_frag && a.cluster_count != b.cluster_count)
            return a.cluster_count > b.cluster_count;
    }
    return a.distance < b.distance;
}
inline bool find_rush_target(const sim::World& w, Address a, Address brain, Choice& out) {
    if (!accepting || !config::active().modules.close_grenade || !base || pr::depth != 1 || a != pr::scope.actor ||
        !human_policy(brain) || !move_free_rush_eligible(brain) || holds_ranged_at(a))
        return false;
    auto list = pr::subjects(a);
    if (!list.valid(8) || list.first == list.end) return false;
    Address hand = pr::hand_item(a);
    Address box = native<pr::Unary>(0x7E0420)(a);
    auto items = span(box, 0x10);
    if (!items.valid(8)) return false;
    Choice best{};
    auto consider = [&](Address item) {
        if (!item || read<int>(item, 0x48) <= 0 || (item != hand && read<Address>(item, 0x30) != box)) return;
        Address def = read<Address>(item, 0x40);
        Payload kind = payload(def);
        if (kind == Payload::none || !pr::usable_item(a, def)) return;
        float range = read<float>(read<Address>(def, 0x7D0), 0x10);
        if (!std::isfinite(range) || range <= 0) return;
        pr::Pair pair{0, def, 0, range, item == hand, 0, item};
        for (Address p = list.first; p != list.end; p += 8) {
            Address s = read<Address>(p);
            if (!pr::hostile_observation(s)) continue;
            Address t = read<Address>(s, 0x18);
            if (!t || !sim::has_actor(t)) continue;
            Vehicle type = vehicle(t);
            if (type == Vehicle::none || !pr::eligible_subject(brain, s)) continue;
            // Prioritize rushing armored vehicles (and open-top vehicles/guns when frag/AT is valid).
            if (type != Vehicle::tank && type != Vehicle::armored_car && kind == Payload::frag) continue;
            Choice c{};
            if (choice(w, s, pair, kind, type, c, true) && better(c, best)) best = c;
        }
    };
    consider(hand);
    for (Address p = items.first; p != items.end; p += 8)
        if (read<Address>(p) != hand) consider(read<Address>(p));
    if (!best.item) return false;
    out = best;
    return true;
}
inline bool priority(Address brain, Address s, const pr::Pair& p) {
    if (!accepting || !config::active().modules.close_grenade || !sim::has_actor(read<Address>(s, 0x18)) ||
        !human_policy(brain))
        return false;
    const auto type = vehicle(read<Address>(s, 0x18));
    if (type == Vehicle::none) return false;
    sim::World w{};
    Choice result{};
    const bool allow_rush = move_free_rush_eligible(brain) && !holds_ranged_at(pr::scope.actor);
    return clock(w) && choice(w, s, p, payload(p.ammo), type, result, allow_rush);
}
inline Address live_subject(Address a, Address target, unsigned id) {
    auto list = pr::subjects(a);
    if (!list.valid(8)) return 0;
    for (Address p = list.first; p != list.end; p += 8) {
        Address s = read<Address>(p);
        if (pr::hostile_observation(s) && read<Address>(s, 0x18) == target && read<unsigned>(target, 0xC0) == id &&
            pr::eligible_subject(read<Address>(a, 0xDC8), s))
            return s;
    }
    return 0;
}
inline void destroy_order(Address order) {
    read<Address (*)(Address, unsigned)>(read<Address>(order))(order, 1);
}
inline Address take_order(Address item) {
    alignas(16) std::byte location[0x40]{};
    native<void* (*)(void*, Address)>(0x7E1AA0)(location, item);
    Address order = native<Address (*)(void*)>(0x8EBAD0)(location);
    native<void (*)(void*)>(0x7E1EC0)(location);
    return order;
}
inline void bind_aim(Address e_target, const Aim& aim) {
    if (aim.station_point) {
        native<void (*)(Address, const Vec3*)>(0x853180)(e_target, &aim.point);
        return;
    }
    native<void (*)(Address, Address, Address)>(0x852DC0)(e_target, aim.entity, aim.volume);
}
inline Address fire_order(const Aim& aim) {
    // The factory deep-copies the point. Bind its constructed eTarget to the
    // live volume/crew before transferring order ownership.
    struct Point {
        Address vt;
        unsigned kind;
        Vec3 pos;
    } point{0, 2, aim.point};
    static_assert(offsetof(Point, pos) == 0xC);
    Address order = native<Address (*)(const Point*, int, Byte)>(0x8EB9C0)(&point, 1, 0);
    if (order) bind_aim(order + 0x38, aim);
    return order;
}
inline void log_event(unsigned long long& count, const char* action, Address a, Address t, Payload k) {
    if (pr::logger && pr::sample_event(count))
        pr::logger("close_grenade %s #%llu actor=%p target=%p payload=%u", action, count, reinterpret_cast<void*>(a),
                   reinterpret_cast<void*>(t), unsigned(k));
}
inline Shot* free_shot() {
    for (auto& s : shots)
        if (!s.order && !s.shooter) return &s;
    return shots.append_slot();
}
inline void remember(Shot& s, const sim::World& w, Address a, const Choice& c, Address order, Address shooter) {
    s.actor = a;
    s.target = c.target;
    s.definition = c.def;
    s.aim_entity = c.aim.entity;
    s.actor_id = read<unsigned>(a, 0xC0);
    s.target_id = read<unsigned>(c.target, 0xC0);
    s.issued = w.now;
    s.scene_epoch = epoch;
    s.bound = c.aim;
    s.kind = c.kind;
    s.abort_code = 0;
    s.spent = s.emitted = false;
    s.shooter = shooter;
    s.order = order;
}
inline unsigned poll_identity(Address actor) {
    return accepting && base && sim::emitting_host(base) && actor && sim::has_actor(actor) &&
                   (read<Byte>(actor, 0x30) & 4)
               ? read<unsigned>(actor, 0xC0)
               : UINT32_MAX;
}
// Includes interior-seated humans (bit 0x200 set while aboard vehicles) so squad_after_quant
// can observe mount -> dismount transitions without preempting higher-priority close_grenade.
inline unsigned poll_squad_identity(Address actor) {
    return accepting && base && sim::emitting_host(base) && actor && (read<Byte>(actor, 0x30) & 4) &&
                   (read<Byte>(actor, 0x30) & 2) && !(read<unsigned>(actor, 0x38) & 0x820u)
               ? read<unsigned>(actor, 0xC0)
               : UINT32_MAX;
}
// The verified slot-23 caller brackets even seated humans and actors under player orders.
inline bool (*squad_after_quant)(Address){};
inline void poll_squad_settle(Address a, unsigned id) {
    Admission admission;
    if (!accepting || !squad_after_quant || id == UINT32_MAX || !base || pr::depth != 1 || a != pr::scope.actor ||
        !sim::serial_actor(a) || !sim::emitting_host(base) || !read<Address>(base + 0x15CA080) ||
        native<Address (*)(const unsigned*)>(0xA7E740)(&id) != a || !runtime::actor_ai_allowed(a))
        return;
    squad_after_quant(a);
}
inline void poll(Address a, unsigned id) {
    Admission admission;
    if (!accepting || !config::active().modules.close_grenade || id == UINT32_MAX || !base || pr::depth != 1 ||
        a != pr::scope.actor || !sim::serial_actor(a) || !sim::emitting_host(base) ||
        !read<Address>(base + 0x15CA080) || native<Address (*)(const unsigned*)>(0xA7E740)(&id) != a ||
        !runtime::actor_ai_allowed(a))
        return;
    Address brain = read<Address>(a, 0xDC8);
    if (!human_policy(brain)) return;
    sim::World w{};
    if (!clock(w)) return;
    if (!coord::instance().can_act(a, coord::Module::close_grenade, coord::Priority::close_grenade, id, w.now)) return;
    const bool plain_ok = replaceable(brain);
    const bool rush_preempt = !plain_ok && replaceable(brain, &w, true);
    if (!plain_ok && !rush_preempt) return;
    polls.ensure_min_size(1024);
    auto& last = polls[id % polls.size()];
    if (last.actor == a && last.id == id && w.now - last.tick < w.duration(.10f)) return;
    last = {a, id, w.now};
    for (const auto& shot : shots)
        if (shot.order && shot.actor == a && shot.actor_id == id) return;
    auto list = pr::subjects(a);
    if (!list.valid(8) || list.first == list.end) return;
    Choice best{};
    const bool allow_rush_equip = !rush_preempt && move_free_rush_eligible(brain) && is_rushing(w, a);
    // Build only the throwable subset. Rifle loadouts do not need ammo-rule
    // evaluation or scene scans at the equipment polling cadence.
    Address hand = pr::hand_item(a);
    Address box = native<pr::Unary>(0x7E0420)(a);
    auto items = span(box, 0x10);
    if (!items.valid(8)) return;
    auto consider = [&](Address item) {
        if (!item || read<int>(item, 0x48) <= 0 || (item != hand && read<Address>(item, 0x30) != box)) return;
        Address def = read<Address>(item, 0x40);
        Payload kind = payload(def);
        if (kind == Payload::none || !pr::usable_item(a, def)) return;
        float range = read<float>(read<Address>(def, 0x7D0), 0x10);
        if (!std::isfinite(range) || range <= 0) return;
        pr::Pair pair{0, def, 0, range, item == hand, 0, item};
        for (Address p = list.first; p != list.end; p += 8) {
            Address s = read<Address>(p);
            if (!pr::hostile_observation(s)) continue;
            Address t = read<Address>(s, 0x18);
            if (!t || !sim::has_actor(t)) continue;
            Vehicle type = vehicle(t);
            if (!pr::eligible_subject(brain, s)) continue;
            Choice c{};
            if (type != Vehicle::none) {
                if (choice(w, s, pair, kind, type, c, allow_rush_equip) && better(c, best)) best = c;
            } else if (!rush_preempt && kind == Payload::frag && cover_frag_choice(w, s, pair, list, c) &&
                       better(c, best)) {
                best = c;
            }
        }
    };
    consider(hand);
    for (Address p = items.first; p != items.end; p += 8)
        if (read<Address>(p) != hand) consider(read<Address>(p));
    if (!best.item) return;
    // Only preempt an active tactical AT-rush move once strictly inside throw range with margin.
    if (rush_preempt && !(best.in_range && best.distance < best.max_range - 1.5f * w.length)) return;
    if (!best.held && hand) {
        Address def = read<Address>(hand, 0x40);
        if (classify::def_is_ranged_at_weapon(def) || pr::has_tag(def, "flame")) return;
    }
    Claim* claim = available(w, best.target, a, best.in_range);
    if (!claim) return;
    if (best.held && !best.in_range) {
        reserve(*claim, w, best.target, a, false, true, best.aim.point, best.cover_frag);
        return;
    }
    Shot* slot = nullptr;
    Address shooter = 0;
    if (best.held) {
        // Prone soldiers (stance 1 or 3) frequently return aimer state 6 in
        // 0x8335A0 and abort WeaponFire. Rise to crouch via native replicating
        // SetMovementType (slot 46, +0x170) without changing position.
        Address chassis = read<Address>(a, 0xDE0);
        int stance = chassis ? read<int>(chassis, 0x2C) : 0;
        if (chassis && (stance == 1 || stance == 3)) {
            using SetType = void (*)(Address, int, Byte);
            read<SetType>(read<Address>(chassis), 0x170)(chassis, 2, 0);
            reserve(*claim, w, best.target, a, false, false, best.aim.point, best.cover_frag);
            return;
        }
        Address place = pr::selected_place(a);
        if (!place || !native<pr::Predicate>(0x81EDC0)(place)) return;
        shooter = read<Address>(place, 0xB8);
        if (!shooter || read<Address>(shooter) != base + 0x1102F20 || read<Address>(shooter, 8) != place ||
            read<Byte>(shooter, 0x14) || read<Byte>(shooter, 0x15))
            return;
        slot = free_shot();
        if (!slot) return;
    }
    // Both factories construct unstarted native orders. Neither schedules the
    // RNG-consuming SwitchWeapon timer or selects a destination.
    Address fresh = best.held ? fire_order(best.aim) : take_order(best.item);
    if (!fresh) return;
    if (!human_policy(brain) || !replaceable(brain, &w, rush_preempt)) {
        destroy_order(fresh);
        return;
    }
    const bool still_approaching = !best.held && !best.in_range;
    reserve(*claim, w, best.target, a, false, still_approaching, best.aim.point, best.cover_frag);
    if (best.held) clear_rush(a);
    if (slot) remember(*slot, w, a, best, fresh, shooter);
    native<void (*)(Address, Address)>(0x91DB30)(brain, fresh);
    log_event(best.held ? fires : mounts, best.held ? "fire-order" : "equip", a, best.target, best.kind);
}

inline Shot* order_record(Address order) {
    for (auto& s : shots)
        if (s.order.load() == order) return &s;
    return nullptr;
}
inline bool active_fire(Address brain, Address order) {
    // Follow only the current execution path. A native concurrent firing child
    // may keep its movement parent. Explicit Attack and WeaponShot remain opaque.
    Address node = read<Address>(brain, 0xF0);
    for (unsigned depth = 0; node && depth < 32; ++depth) {
        if (read<Address>(node, 8) != pr::scope.actor + 0xAF0) return false;
        if (node == order) return true;
        Address vt = read<Address>(node);
        if (vt != base + 0x110D490 && vt != base + 0x110C810 && vt != base + 0x1023970) return false;
        node = sim::orders::active_child(base, node);
    }
    return false;
}
inline bool held_by(Address item, Address actor) {
    Address holder = item ? read<Address>(item, 0x38) : 0;
    return holder && read<Address>(holder) == base + 0x10FF210 && read<Address>(holder, 0x3D0) == actor &&
           read<Address>(holder, 0x3D8) == item;
}
inline Byte live_choice(const sim::World& w, Shot& shot, Address weapon, Choice& c) {
    Address a = pr::scope.actor;
    if (!a || a != shot.actor || read<unsigned>(a, 0xC0) != shot.actor_id || !human_policy(read<Address>(a, 0xDC8)) ||
        shot.spent || shot.scene_epoch != epoch || !shot.order || !active_fire(read<Address>(a, 0xDC8), shot.order) ||
        w.now - shot.issued > w.duration(4.5f) || !weapon || pr::selected_place(a) != weapon ||
        read<Address>(weapon, 0x90) != a || read<Address>(weapon, 0xB8) != shot.shooter)
        return 2;
    Address item = pr::hand_item(a);
    if (!held_by(item, a) || read<int>(item, 0x48) <= 0 || read<Address>(item, 0x40) != shot.definition) return 3;
    Address subject = live_subject(a, shot.target, shot.target_id);
    if (!subject) return 4;
    Payload kind = payload(shot.definition);
    if (kind == Payload::none) return 3;
    float range = read<float>(read<Address>(shot.definition, 0x7D0), 0x10);
    pr::Pair pair{0, shot.definition, 0, range, true, 0, item};
    const Vehicle vtype = vehicle(shot.target);
    if (!std::isfinite(range)) return 5;
    if (vtype == Vehicle::none) {
        if (kind != Payload::frag || !shot.bound.station_point) return 5;
        const auto subjects = pr::subjects(a);
        if (!subjects.valid(8) || !cover_frag_choice(w, subject, pair, subjects, c, &shot.bound.point)) return 5;
        return 0;
    }
    if (!choice(w, subject, pair, kind, vtype, c) || !c.in_range || c.aim.entity != shot.aim_entity) return 5;
    return 0;
}
inline bool capture_native(Address order, Address brain, Address weapon, const sim::World& w) {
    Address a = pr::scope.actor;
    Address item = pr::hand_item(a);
    if (!item || read<int>(item, 0x48) <= 0) return true;
    Address def = read<Address>(item, 0x40);
    Payload kind = payload(def);
    if (kind == Payload::none) return true;
    Address shooter = read<Address>(weapon, 0xB8);
    if (!shooter || read<Address>(shooter) != base + 0x1102F20) return true;
    Address target_arg = order + 0x38;
    Address entity = read<int>(target_arg, 8) == 1 ? read<Address>(target_arg, 0x18) : 0;
    Address preferred = read<Address>(brain, 0x3B8);
    auto list = pr::subjects(a);
    if (!list.valid(8)) return true;
    for (Address p = list.first; p != list.end; p += 8) {
        Address s = read<Address>(p);
        if (!pr::hostile_observation(s)) continue;
        Address target = read<Address>(s, 0x18);
        if (!target || (entity && target != entity)) continue;
        if (!entity && (s != preferred || read<int>(target_arg, 8) != 2 ||
                        !(pr::distance(read<Vec3>(target_arg, 0xC), read<Vec3>(s, 0x28)) <= 2 * w.length)))
            continue;
        Vehicle type = vehicle(target);
        if (type == Vehicle::none) continue;
        float range = read<float>(read<Address>(def, 0x7D0), 0x10);
        pr::Pair pair{0, def, 0, range, true, 0, item};
        Choice c{};
        if (!pr::eligible_subject(brain, s) || !choice(w, s, pair, kind, type, c) || !c.in_range) return false;
        Shot* slot = free_shot();
        Claim* claim = available(w, target, a);
        if (!slot || !claim) return false;
        bind_aim(target_arg, c.aim);
        reserve(*claim, w, target, a, false);
        remember(*slot, w, a, c, order, shooter);
        return true;
    }
    return true;
}
// This shared hook also sees unrelated explicit/vehicle fire. A managed order
// can only start/update with its original item, active owner and live target.
inline bool matches_aim(Address weapon, const Aim& aim, float tolerance) {
    if (!weapon) return false;
    if (aim.station_point)
        return read<int>(weapon, 0xD0) == 2 && pr::distance(read<Vec3>(weapon, 0xD4), aim.point) <= tolerance;
    return read<int>(weapon, 0xD0) == 1 && read<Address>(weapon, 0xE0) == aim.entity &&
           (!aim.volume || native<pr::Unary>(0x8527C0)(weapon + 0xC8) == aim.volume);
}
inline void new_firing(Address shooter, Byte active) {
    if (!active || !shooter || !sim::serial_thread() || !sim::emitting_host(base) || read<Byte>(shooter, 0x14)) return;
    // A new stock off-to-on transition supersedes the previous canceled throw.
    for (auto& s : shots) {
        if (s.shooter != shooter) continue;
        Address place = read<Address>(shooter, 8);
        if (!s.order || (place && read<int>(place, 0xD0) != 0 && !matches_aim(place, s.bound, 0.f))) s.shooter = 0;
    }
}
inline int fire_status(Address order, unsigned phase) {
    Shot* s = order_record(order);
    if (!s && (phase || !pr::scope.actor || pr::depth != 1)) return -1;
    if (!s) {
        Admission admission;
        if (!accepting) return -1;
        Address a = pr::scope.actor;
        Address brain = read<Address>(a, 0xDC8);
        if (read<Address>(order, 8) != a + 0xAF0 || !human_policy(brain) || !active_fire(brain, order)) return -1;
        Address weapon = pr::selected_place(a);
        sim::World w{};
        if (!weapon || !clock(w)) return -1;
        return capture_native(order, brain, weapon, w) ? -1 : 2;
    }
    if (phase == 2) {
        // Stock End clears the currently selected weapon. Never clear a new
        // player target or a replacement weapon when an older order ends.
        if (!sim::emitting_host(base) || !sim::serial_thread() || read<Address>(order, 8) != s->actor + 0xAF0) return 0;
        Address a = s->actor, weapon = pr::selected_place(a);
        return read<unsigned>(a, 0xC0) == s->actor_id && weapon && s->shooter &&
                       read<Address>(weapon, 0xB8) == s->shooter && matches_aim(weapon, s->bound, 0.f)
                   ? -1
                   : 0;
    }
    if (!pr::scope.actor || pr::scope.actor != s->actor || !pr::admitted(read<Address>(pr::scope.actor, 0xDC8))) {
        s->abort_code = 1;
        return 2;
    }
    if (s->spent) return s->emitted ? 1 : 2;
    sim::World w{};
    Choice c{};
    Address weapon = pr::selected_place(s->actor);
    if (!clock(w)) {
        s->abort_code = 1;
        return 2;
    }
    Byte reason = live_choice(w, *s, weapon, c);
    if (reason) {
        s->abort_code = reason;
        return 2;
    }
    if (phase == 0) {
        s->bound = c.aim;
        bind_aim(order + 0x38, c.aim);
    }
    return -1;
}
using Destroy = Address (*)(Address, unsigned);
inline Destroy original_fire_destroy{};
inline Address fire_destroy(Address order, unsigned flags) {
    runtime::HookGuard guard(hook_gate);
    if (auto* s = order_record(order)) {
        // Keep a shooter with a pending release identifiable through native End.
        Address shooter = s->shooter.load();
        int aimer_state = -1, stance = -1;
        if (shooter) {
            if (s->emitted || s->spent || !read<Byte>(shooter, 0x15)) s->shooter = 0;
            if (sim::serial_thread() && pr::scope.actor && s->actor == pr::scope.actor) {
                Address place = pr::selected_place(s->actor);
                if (place) {
                    Address aimer = read<Address>(place, 0xA8);
                    if (aimer) aimer_state = read<int>(aimer, 0x18);
                    if (read<Address>(place, 0xB8) != shooter) s->shooter = 0;
                }
                Address chassis = read<Address>(s->actor, 0xDE0);
                if (chassis) stance = read<int>(chassis, 0x2C);
            }
        }
        if (!s->emitted && sim::serial_thread()) {
            sim::World w{};
            if (clock(w)) {
                release_claim(s->actor, s->actor_id, s->target, s->target_id);
                note_failure(w, s->actor, s->actor_id, s->target, s->target_id, 3.f);
                if (pr::logger && pr::sample_event(aborts)) {
                    const float age = w.ticks > 0 ? float(w.now - s->issued) / w.ticks : 0.f;
                    pr::logger(
                        "close_grenade abort #%llu actor=%p target=%p payload=%u code=%u aimer=%d stance=%d spent=%u "
                        "age=%.2fs",
                        aborts, reinterpret_cast<void*>(s->actor), reinterpret_cast<void*>(s->target),
                        unsigned(s->kind), unsigned(s->abort_code), aimer_state, stance, unsigned(s->spent), age);
                }
            }
        }
        s->order = 0;
    }
    return original_fire_destroy(order, flags);
}
inline void shooter_destroyed(Address shooter) {
    for (auto& s : shots) {
        Address expected = shooter;
        s.shooter.compare_exchange_strong(expected, 0);
    }
}
struct Launch {
    Shot& shot;
    Address shooter, weapon;
    Choice choice;
    sim::World world;
    bool saw_send{}, sent{}, spawned{};
    sim::Vec3 origin{}, velocity{}, spin{};
};
inline thread_local Launch* launch{};
inline bool frag_contact(const Choice& c, const sim::ThrowContact& hit, Vec3 point, float scale) {
    if (!hit.volume) return false;
    if (c.aim.entity != c.target && hit.entity == c.aim.entity) return true;
    if (!c.aim.open_deck || hit.entity != c.target) return false;
    Vec3 offset = pr::diff(point, c.aim.point);
    float width = 1.2f * scale;
    return offset.x * offset.x + offset.y * offset.y <= width * width && offset.z >= -1.4f * scale &&
           offset.z <= 1.2f * scale;
}
inline bool near_point_xy_z(Vec3 point, Vec3 ref, float max_xy, float min_z, float max_z) {
    Vec3 d = pr::diff(point, ref);
    return d.x * d.x + d.y * d.y <= max_xy * max_xy && d.z >= min_z && d.z <= max_z;
}
inline bool contact_allowed(Launch& l, const sim::Vec3& origin, const sim::Vec3& velocity, const sim::Vec3& spin) {
    if (!sim::finite(origin) || !sim::finite(velocity) || !sim::finite(spin)) return false;
    sim::ThrowContact hit{};
    if (!sim::first_throw_contact(base, l.shot.actor, l.weapon, origin, velocity, hit) || !sim::finite(hit.position))
        return false;
    Vec3 point{hit.position.x, hit.position.y, hit.position.z};
    const auto& c = l.choice;
    const float scale = l.world.length;
    if (!clear_area(l.world, l.shot.actor, point, c.radius, c.kind)) return false;

    // Account for native target leading (0x832EB0): entity velocity (0x73A2A0)
    // and projectile velocity share engine units/ms. Project aim point to impact.
    Vec3 target_vel{};
    native<Vec3* (*)(Address, Vec3*)>(0x73A2A0)(c.target, &target_vel);
    if (!pr::finite(target_vel)) target_vel = {};
    const float dx_h = point.x - origin.x, dy_h = point.y - origin.y;
    const float horiz_dist = std::sqrt(dx_h * dx_h + dy_h * dy_h);
    const float horiz_speed = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
    const float t_ms = horiz_speed > 1e-4f ? std::clamp(horiz_dist / horiz_speed, 0.f, 3500.f) : 0.f;
    const Vec3 predicted{c.aim.point.x + target_vel.x * t_ms, c.aim.point.y + target_vel.y * t_ms,
                         c.aim.point.z + target_vel.z * t_ms};
    float hull = read<float (*)(Address)>(read<Address>(c.target), 0x80)(c.target);
    if (!std::isfinite(hull) || hull < 0.f) hull = 1.5f * scale;

    if (c.kind == Payload::frag) {
        if (c.cover_frag) {
            if (hit.entity == c.target && hit.volume) return true;
            if (hit.volume && hit.entity && sim::has_actor(hit.entity) && (read<Byte>(hit.entity, 0x30) & 4) &&
                native<int (*)(Address, Address, Byte)>(0xA65320)(l.shot.actor, hit.entity, 0) == 1 &&
                near_point_xy_z(point, c.aim.point, 3.2f * scale, -2.5f * scale, 2.5f * scale))
                return true;
            // Accept ground landing or cover-wall bounce tightly inside the frag lethal zone of the cluster/defender.
            return near_point_xy_z(point, c.aim.point, 3.2f * scale, -2.5f * scale, 2.5f * scale) ||
                   near_point_xy_z(point, predicted, 3.2f * scale, -2.5f * scale, 2.5f * scale);
        }
        if (frag_contact(c, hit, point, scale)) return true;
        // Accept hitting another hostile human crewman in the same open compartment/gun.
        if (hit.volume && hit.entity && hit.entity != c.target && sim::has_actor(hit.entity) &&
            (read<Byte>(hit.entity, 0x30) & 4)) {
            using Relation = int (*)(Address, Address, Byte);
            if (native<Relation>(0xA65320)(l.shot.actor, hit.entity, 0) == 1 &&
                (pr::distance(point, c.aim.point) <= 2.5f * scale || pr::distance(point, predicted) <= 2.5f * scale))
                return true;
        }
        // Accept led hits on the vehicle deck or nearby ground for moving open-tops/cars/guns.
        if (hit.entity == c.target && hit.volume && c.aim.open_deck &&
            near_point_xy_z(point, predicted, 1.5f * scale, -1.5f * scale, 1.5f * scale))
            return true;
        if (hit.kind == 1 && near_point_xy_z(point, predicted, (std::max)(hull + 1.5f * scale, 2.5f * scale),
                                             -2.5f * scale, 2.5f * scale))
            return true;
        return false;
    }

    if (hit.entity == c.target && hit.volume) {
        if (c.kind != Payload::incendiary || hit.volume == c.aim.volume) return true;
        Span hit_tags = span(read<Address>(hit.volume, 0x30), 0x398);
        if (has(hit_tags, "engine")) return true;
        const float deck_r = (std::max)(2.0f * scale, hull * 0.75f);
        return near_point_xy_z(point, c.aim.point, deck_r, -1.2f * scale, 1.8f * scale) ||
               near_point_xy_z(point, predicted, deck_r, -1.2f * scale, 1.8f * scale);
    }
    // Accept hitting a hostile crewman seated on the vehicle or terrain at the
    // predicted moving-vehicle intercept / close splash zone (still vetoes short walls).
    if (hit.volume && hit.entity && sim::has_actor(hit.entity) && (read<Byte>(hit.entity, 0x30) & 4)) {
        using Relation = int (*)(Address, Address, Byte);
        if (native<Relation>(0xA65320)(l.shot.actor, hit.entity, 0) == 1 &&
            (pr::distance(point, c.aim.point) <= hull + 1.5f * scale ||
             pr::distance(point, predicted) <= hull + 1.5f * scale))
            return true;
    }
    if (hit.kind == 1) {
        const float splash_r =
            hull + (c.kind == Payload::incendiary ? 1.8f * scale : (std::max)(c.radius * 0.5f, 2.0f * scale));
        return near_point_xy_z(point, predicted, splash_r, -(hull + 2.0f * scale), hull + 2.0f * scale);
    }
    return false;
}
inline bool send_allowed(Address command) {
    if (!launch) return true;
    auto& l = *launch;
    l.saw_send = true;
    if (l.sent || !command || read<Address>(command) != base + 0x10231C0 || read<unsigned>(command, 8) != 0x0D ||
        read<unsigned>(command, 0x20) != 2 || read<Address>(command, 0x10) != l.shot.actor ||
        read<Address>(command, 0x18) != l.weapon)
        return false;
    l.origin = read<sim::Vec3>(command, 0x24);
    l.velocity = read<sim::Vec3>(command, 0x30);
    l.spin = read<sim::Vec3>(command, 0x3C);
    l.sent = contact_allowed(l, l.origin, l.velocity, l.spin);
    return l.sent;
}
inline bool spawn_allowed(Address shooter, const sim::Vec3*& origin, const sim::Vec3*& velocity,
                          const sim::Vec3*& spin) {
    if (!launch) return true;
    auto& l = *launch;
    if (l.spawned || l.shooter != shooter) return false;
    if (l.saw_send) {
        if (!l.sent) return false;
        // Local consumption uses the exact vectors already accepted for the peer.
        origin = &l.origin;
        velocity = &l.velocity;
        spin = &l.spin;
    } else if (!origin || !velocity || !spin || !contact_allowed(l, *origin, *velocity, *spin))
        return false;
    l.spawned = true;
    return true;
}
// Native 0x8313B0 resolves type-1 eTarget through its entity, and throws
// the right-hand item; the weapon place's definition may describe the thrower.
inline std::atomic<unsigned> stat_ff_vetoes{};
inline Payload vanilla_payload(Address def) {
    if (!def || sim::IsPureSmokeDefinition(def)) return Payload::none;
    const auto regular = payload(def);
    if (regular != Payload::none) return regular;
    const auto props = read<Address>(def, 0x7d0);
    if (!props || read<Address>(props) != base + 0x1100908 || !pr::has_tag(def, "missile") ||
        !pr::has_tag(def, "grenade"))
        return Payload::none;
    for (const auto* key : {"m15_wp", "m42_wp", "no77_wp"})
        if (sim::IsGrenadeDefinition(def, key)) return Payload::incendiary;
    return Payload::none;
}
inline bool veto_vanilla_release(Address shooter) {
    if (!hook_gate.active() || !accepting || !config::active().modules.grenade_ff_guard || !sim::serial_thread() ||
        !sim::emitting_host(base) || read<Address>(shooter) != base + runtime::table().vt.throwing_shooter)
        return false;
    const auto weapon = read<Address>(shooter, 8);
    const auto actor = read<Address>(weapon, 0x90);
    if (!sim::has_actor(actor) || !(read<Byte>(actor, 0x30) & 4) || !(read<unsigned>(actor, 0xdb4) & 0x100) ||
        read<Address>(weapon, 0xb8) != shooter)
        return false;
    // A delayed animation marker has no slot-23 frame. If a frame is still
    // active, another actor's nested release cannot borrow its authority.
    const auto* frame = sim::quant_frame();
    const auto scoped = ai_support::current_actor();
    if ((frame && frame->actor != actor) || (scoped && scoped != actor)) return false;
    const auto brain = read<Address>(actor, 0xdc8), vit = read<Address>(read<Address>(actor, 0x578), 0x18);
    if (!vit || (read<unsigned>(vit, 0x174) & 3) || !brain ||
        read<Address>(brain) != base + runtime::table().vt.human_brain || (read<unsigned>(brain, 0x398) & 3u) > 1 ||
        ((read<unsigned>(brain, 0x398) >> 10) & 3u) == 3 || !pr::authorized_actor(actor, brain))
        return false;
    if (has_explicit_attack(brain) || read<int>(weapon, 0x208) == 1) return false;
    sim::World w{};
    if (!sim::world(base, w)) return false;
    const auto def = native<pr::Unary>(runtime::table().fn.right_hand_definition)(actor);
    const auto kind = vanilla_payload(def);
    const auto type = read<unsigned>(weapon, 0xd0);
    const auto target = read<Address>(weapon, 0xe0);
    if (kind == Payload::none || (type != 2 && !(type == 1 && target))) return false;
    const auto point = type == 2 ? read<Vec3>(weapon, 0xd4) : read<Vec3>(target, 0x74);
    if (!pr::finite(point) || !(pr::distance(read<Vec3>(actor, 0x74), point) <= 50.f * w.length)) return false;
    const auto radius = effect_radius(w, def, kind);
    return radius > .5f * w.length && !clear_area(w, actor, point, radius + .5f * w.length, kind);
}
inline void release(sim::SmokeRelease original, Address shooter) {
    struct Frame {
        Launch* prev{launch};
        Frame() {
            launch = nullptr;
        }
        ~Frame() {
            launch = prev;
        }
    } frame;
    Shot* slot = nullptr;
    for (auto& s : shots)
        if (s.shooter == shooter) {
            slot = &s;
            if (s.order) break;
        }
    if (!slot) {
        if (veto_vanilla_release(shooter)) {
            ++stat_ff_vetoes;
            return;
        }
        original(shooter);
        return;
    }
    Admission active_release;
    if (!sim::emitting_host(base) || !sim::serial_thread()) return;
    if (!slot->order) {
        // Do not fire a canceled latch. A later unrelated order gets its normal release.
        slot->shooter = 0;
        return;
    }
    sim::World w{};
    Choice c{};
    Address weapon = read<Address>(shooter, 8);
    if (pr::depth != 1 || !pr::scope.actor || !pr::admitted(read<Address>(pr::scope.actor, 0xDC8)) || !clock(w) ||
        live_choice(w, *slot, weapon, c) != 0) {
        slot->abort_code = 6;
        slot->spent = true;
        return;
    }
    const bool aim_ok = slot->bound.station_point
                            ? (matches_aim(weapon, slot->bound, .75f * w.length) &&
                               pr::distance(read<Vec3>(weapon, 0xD4), c.aim.point) <= 4.f * w.length)
                            : matches_aim(weapon, c.aim, .75f * w.length);
    if (!aim_ok) {
        slot->abort_code = 6;
        slot->spent = true;
        return;
    }
    Claim* claim = available(w, c.target, slot->actor);
    if (!claim) {
        slot->abort_code = 6;
        slot->spent = true;
        return;
    }
    reserve(*claim, w, c.target, slot->actor, true, false, c.aim.point, c.cover_frag);
    const Address owner = slot->actor, order = slot->order.load();
    Launch sample{*slot, shooter, weapon, c, w};
    launch = &sample;
    slot->spent = true;
    original(shooter); // the one scheduled native sample
    if (slot->order == order) slot->emitted = sample.spawned;
    if (sample.spawned && c.cover_frag) stat_cover_frags.fetch_add(1, std::memory_order_relaxed);
    if (!sample.spawned && claim->actor == owner && claim->target == c.target && claim->start == w.now)
        claim->duration = w.duration(3.f);
    log_event(sample.spawned ? releases : vetoes, sample.spawned ? "release" : "contact-veto", owner, c.target, c.kind);
}
inline bool is_throwing_live(Address actor) {
    sim::World w{};
    return accepting.load() && base && sim::world(base, w) && is_throwing(w, actor);
}
inline void connect() {
    hook_gate.activate();
    accepting = true;
    pr::grenade_priority = priority;
    sim::grenades = {fire_status, release, send_allowed, spawn_allowed, shooter_destroyed, new_firing, is_busy};
    coord::instance().register_provider(coord::Module::close_grenade, coord::Priority::close_grenade,
                                        &is_throwing_live);
}
inline void disconnect() {
    deactivate();
    sim::grenades = {};
    pr::grenade_priority = nullptr;
    original_fire_destroy = nullptr;
    clear_state();
    claims.release();
    rushes.release();
    failures.release();
    polls.release();
    shots.release();
    scene = 0;
    last_tick = 0;
    mounts = fires = releases = vetoes = aborts = 0;
    stat_ff_vetoes = 0;
    stat_mg_small_arms_defer = 0;
    stat_cover_frags = 0;
}
} // namespace goh::close_grenade
