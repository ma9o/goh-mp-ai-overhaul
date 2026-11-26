#pragma once

#include "ai_support_26734796.hpp"

namespace goh::auto_resupply {
using namespace ai_support;
inline constexpr bool runtime_allowed = false;
constexpr float safety_metres = 25;
inline float max_travel_metres() noexcept {
    return config::active().tuning.resupply_search_radius_m;
}
inline bool within_travel_budget(const World& w, float travelled) {
    return travelled <= max_travel_metres() * w.length;
}
inline constexpr unsigned max_inbound_per_source = 4u;
inline std::uint32_t stat_trips = 0;
inline std::uint32_t stat_refilled = 0;
inline std::uint32_t stat_returns = 0;
inline Address cached_template(Address actor) {
    const auto key = native_string(actor + 0xF0);
    const auto tree = read<Address>(base, 0x159BE88);
    if (!tree || key.empty()) return 0;
    auto node = read<Address>(tree, 8);
    for (unsigned depth = 0; node && !read<Byte>(node, 0x19) && depth < 64; ++depth) {
        auto name = native_string(node + 0x20);
        if (name.empty()) return 0;
        int cmp = key.compare(name);
        if (!cmp) return read<Address>(node, 0x70);
        node = read<Address>(node, cmp < 0 ? 0 : 0x10);
    }
    return 0;
}
struct Gun {
    Address def{}, item{}, place{};
    float rank{};
    bool human{};
};
enum class Ammo {
    unknown,
    empty,
    available
};
inline bool weapon_definition(Address def) {
    if (!def || !tag(span(def, 0x11E0), "weapon")) return false;
    auto required = span(def + 0x120, 0x318);
    return required.first != required.end && required.valid(0x60, 4096);
}
// Stock 0x7DC450 reads live loaded rounds at eItem+0xDC (eItem+0x58 is the
// magazine/chamber fill capacity, which stays non-zero when fired empty).
inline bool loaded_chamber(Address item) {
    return item && read<int>(item, 0xDC) > 0 && read<Address>(item, 0x50);
}
inline int ammo_count(Address actor, const Gun& g) {
    if (!g.def) return -1;
    int total = loaded_chamber(g.item) ? read<int>(g.item, 0xDC) : 0;
    const auto b = box(actor);
    auto items = span(b, 0x10);
    if (!b || !items.valid(8, 2048)) return total > 0 ? total : -1;
    for (auto p = items.first; p != items.end; p += 8) {
        auto item = read<Address>(p);
        if (!item || read<int>(item, 0x48) <= 0 || read<Address>(item, 0x30) != b) continue;
        auto def = read<Address>(item, 0x40);
        if (def && native<BinaryTest>(0x565A50)(def + 0xEC8, g.def + 0x120)) total += read<int>(item, 0x48);
        if (item != g.item && def == g.def && read<Address>(item, 0x50) && read<int>(item, 0xDC) > 0)
            total += read<int>(item, 0xDC);
    }
    return total;
}
inline Ammo ammo(Address actor, const Gun& g) {
    const int n = ammo_count(actor, g);
    if (n < 0) return Ammo::unknown;
    return n > 0 ? Ammo::available : Ammo::empty;
}
struct VehicleGun {
    Gun gun{};
    bool cannon{};
};
inline void merge_vehicle_gun(std::array<VehicleGun, 16>& found, std::size_t& count, const Gun& gun, bool is_cannon) {
    // Deduplicate twin turrets sharing one weapon definition (e.g. T-35's two 45 mm turrets),
    // keeping a loaded chamber if either turret still holds a round.
    for (std::size_t i = 0; i < count; ++i) {
        if (found[i].gun.def == gun.def) {
            if (!loaded_chamber(found[i].gun.item) && loaded_chamber(gun.item)) {
                found[i].gun.item = gun.item;
                found[i].gun.place = gun.place;
            }
            found[i].cannon |= is_cannon;
            return;
        }
    }
    if (count < found.size()) found[count++] = {gun, is_cannon};
}
inline Gun select_vehicle_gun(const VehicleGun* found, const bool* empty, std::size_t count, Address wanted_def = 0) {
    if (wanted_def) {
        for (std::size_t i = 0; i < count; ++i)
            if (found[i].gun.def == wanted_def) return found[i].gun;
        return {};
    }
    Gun empty_cannon{}, best_cannon{}, empty_mg{}, best_mg{};
    for (std::size_t i = 0; i < count; ++i) {
        const auto& g = found[i].gun;
        const bool is_empty = empty[i];
        if (found[i].cannon) {
            if (g.rank > best_cannon.rank) best_cannon = g;
            if (is_empty && g.rank > empty_cannon.rank) empty_cannon = g;
        } else {
            if (g.rank > best_mg.rank) best_mg = g;
            if (is_empty && g.rank > empty_mg.rank) empty_mg = g;
        }
    }
    if (empty_cannon.def) return empty_cannon;
    if (best_cannon.def) return best_cannon;
    return empty_mg.def ? empty_mg : best_mg;
}
inline Gun primary(Address actor, Address wanted_def = 0) {
    Gun best{};
    const auto b = box(actor);
    if (!b) return {};
    const bool human = (read<Byte>(actor, 0x30) & 4) != 0;
    if (human) {
        auto items = span(b, 0x10);
        if (!items.valid(8, 2048)) return {};
        auto holder = native<Unary>(0x7E04B0)(actor);
        auto hand = holder ? read<Unary>(read<Address>(holder), 0x18)(holder) : 0;
        auto consider = [&](Address item) {
            if (!item || read<int>(item, 0x48) <= 0) return;
            auto def = read<Address>(item, 0x40);
            if (!weapon_definition(def)) return;
            auto tags = span(def, 0x11E0);
            auto profile = native<Unary>(0x83F1E0)(def);
            if (!profile || !read<Address>(profile, 0x1600) || tag(tags, "missile") || tag(tags, "grenade")) return;
            float rank = tag(tags, "pistol") || tag(tags, "revolver") ? 1 : 4;
            if (item == hand) rank += .5f;
            if (rank > best.rank) best = {def, item, 0, rank, true};
        };
        consider(hand);
        for (auto p = items.first; p != items.end; p += 8) {
            auto i = read<Address>(p);
            if (i && read<Address>(i, 0x30) == b) consider(i);
        }
        return best;
    }
    if (!(read<Byte>(actor, 0x30) & 8) || !smoke::vehicle::hasWeaponAndPlaces(actor)) return {};
    auto ws = span(read<Address>(actor, 0xDD8), 0x30);
    if (!ws.valid(8, 256)) return {};
    std::array<VehicleGun, 16> found{};
    std::size_t count = 0;
    for (auto p = ws.first; p != ws.end; p += 8) {
        auto w = read<Address>(p);
        if (!w || smoke::SmokeTagEquals(w + 8, "dummy") || read<Address>(w, 0x90) != actor) continue;
        auto mounted = read<Address>(w, 0x218), charger = read<Address>(w, 0xB0);
        if (!mounted || !charger || native<Test>(0x820CE0)(charger)) continue;
        auto def = read<Address>(mounted, 0x40), profile = read<Address>(w, 0x98);
        if (!weapon_definition(def) || !profile || !read<Address>(w, 0xA0)) continue;
        float calibre = read<float>(profile, 0x1628);
        if (!std::isfinite(calibre) || calibre <= 0 || calibre > 1000) continue;
        auto charged = read<Unary>(read<Address>(charger), 8)(charger);
        const bool is_cannon =
            !tag(span(profile, 0x11E0), "mgun") && !tag(span(def, 0x11E0), "mgun") && calibre >= 20.f;
        merge_vehicle_gun(found, count, {def, charged, w, calibre, false}, is_cannon);
    }
    if (wanted_def) return select_vehicle_gun(found.data(), nullptr, count, wanted_def);
    std::array<bool, 16> empty{};
    for (std::size_t i = 0; i < count; ++i)
        empty[i] = ammo(actor, found[i].gun) == Ammo::empty;
    return select_vehicle_gun(found.data(), empty.data(), count);
}
inline bool affordable(int spent, int limit, int cost) {
    return spent >= 0 && limit >= 0 && cost >= 0 && (!limit || static_cast<std::int64_t>(spent) + cost <= limit);
}
inline bool capacity(Address zone, Address row, Address actor, const Gun& g) {
    if (g.human) {
        // The human worker searches its box for "weapon" before applying an ammo row.
        if (!g.item || read<Address>(g.item, 0x30) != box(actor)) return false;
        auto breed = read<Address>(actor, 0x1390);
        if (breed && !span(breed, 0x508).valid(0x670, 4096)) return false;
        using Capacity = int (*)(Address, Address, Address, Address, Address);
        return native<Capacity>(0x48F820)(zone, row, actor, g.def, g.item) > 0;
    }
    auto offered = read<Address>(row);
    auto mounted = read<Unary>(read<Address>(g.place), 0x18)(g.place);
    auto fill = mounted && read<Address>(mounted, 0x40) == g.def ? read<Address>(mounted, 0x50) : 0;
    if (fill && read<int>(g.def, 0x470) > 0 && native<BinaryTest>(0x565A50)(fill + 0x868, offered + 0x868)) return true;
    // Read resident templates without invoking the lazy loader 0x717680. On a
    // first-visit cache miss (raw == 0), accept when the weapon itself has
    // positive chamber/magazine capacity (+0x470) because supplies() already
    // proved offered ammo matches g.def+0x120.
    auto raw = cached_template(actor);
    if (!raw) return read<int>(g.def, 0x470) > 0;
    if (!(read<Byte>(raw, 0x30) & 1)) return false;
    auto b = box(raw);
    auto items = span(b, 0x10);
    if (b && items.valid(8, 2048))
        for (auto p = items.first; p != items.end; p += 8) {
            auto i = read<Address>(p);
            if (!i || read<int>(i, 0x48) <= 0) continue;
            auto idef = read<Address>(i, 0x40);
            if (idef && (idef == offered || native<BinaryTest>(0x565A50)(idef + 0x868, offered + 0x868))) return true;
        }
    return false;
}
inline const char* supply_rejection(Address zone, Address actor, const Gun& g) {
    auto rows = span(zone, 0x3D0);
    if (!rows.valid(0x18, 8192) || rows.first == rows.end) return "supply_rows";
    const char* reason = "ammo_match";
    for (auto r = rows.first; r != rows.end; r += 0x18) {
        auto offered = read<Address>(r);
        if (!offered || !tag(span(offered, 0x11E0), "ammo") ||
            !native<BinaryTest>(0x565A50)(offered + 0x868, g.def + 0x120))
            continue;
        if (read<int>(r, 8) <= 0 || read<int>(r, 0x10) < 0 ||
            !affordable(read<int>(zone, 0x94), read<int>(zone, 0x98), read<int>(r, 0x14))) {
            reason = "row_quantity_or_budget";
            continue;
        }
        if (g.human && (!g.item || read<Address>(g.item, 0x30) != box(actor))) return "weapon_outside_box";
        if (capacity(zone, r, actor, g)) return nullptr;
        reason = "capacity";
    }
    return reason;
}
inline bool supplies(Address zone, Address actor, const Gun& g) {
    return !supply_rejection(zone, actor, g);
}
inline Address team(Address actor) {
    auto game = native<Address (*)()>(0xD2BCC0)();
    auto players = span(game, 0x160);
    if (players.valid(8, 256))
        for (auto p = players.first; p != players.end; p += 8) {
            auto player = read<Address>(p);
            if (player && read<Byte>(player, 4) == owner(actor) && !native_string(player + 0x68).empty())
                return player + 0x28;
        }
    return 0;
}
inline bool uncontested(float progress, float delta, int team_code, const float* weights) {
    if (progress != 1 || delta != 0 || team_code < 'a' || team_code > 'j') return false;
    for (int i = 0; i < 10; ++i)
        if (!std::isfinite(weights[i]) || weights[i] < 0 || (weights[i] > 0 && i != team_code - 'a')) return false;
    return true;
}
inline bool safe_flag(Address flag, Address point, Address actor, Address team_tag) {
    if (!smoke::has_actor(flag) || !smoke::allies(base, owner(actor), owner(flag))) return false;
    auto engine = read<Address>(point, 0xB58);
    if (!engine || read<Address>(engine) != base + 0x116C550 || read<Address>(engine, 8) != point ||
        read<float>(engine, 0x98) != 1)
        return false;
    if (team_tag) {
        if (!native<BinaryTest>(0x565340)(engine + 0xA8, team_tag)) return false;
        const auto code = native<int (*)(Address)>(0xD0DFD0)(team_tag);
        const auto weights = read<std::array<float, 10>>(engine, 0x108);
        return uncontested(read<float>(engine, 0x98), read<float>(engine, 0x9C), code, weights.data());
    }
    // SP / Skirmish / Conquest fallback when MP eGame (0xD2BCC0) has no team entry:
    // allied owner is already verified above; require full capture progress, zero delta,
    // and at most one non-zero capture weight.
    const auto code = native<int (*)(Address)>(0xD0DFD0)(engine + 0xA8);
    const auto weights = read<std::array<float, 10>>(engine, 0x108);
    if (code >= 'a' && code <= 'j')
        return uncontested(read<float>(engine, 0x98), read<float>(engine, 0x9C), code, weights.data());
    return read<float>(engine, 0x9C) == 0.f;
}
inline Address parent_flag(const World& w, Address child) {
    auto bone = extender(child, 0x10FDD88);
    auto expected = bone ? read<Address>(bone, 0x88) : 0;
    Address flag = 0;
    actors(w, [&](Address a) {
        if (flag || (expected && expected != a) || !extender(a, 0x10FBFF0)) return;
        auto rows = span(read<Address>(a, 0x568), 0x28);
        if (rows.valid(0x298, 1024))
            for (auto r = rows.first; r != rows.end; r += 0x298)
                if (read<Address>(r, 0x280) == child) {
                    flag = a;
                    break;
                }
    });
    return flag;
}
inline bool source_owner(const World& w, Address source, Address zone, Address actor) {
    auto teams = span(zone, 0x3B8);
    if (!teams.valid(0x60, 256)) return false;
    const bool is_mp = native<smoke::Role>(0xC2B5D0)() && !native<smoke::Role>(0xC2B620)();
    auto mine = team(actor);
    const bool restricted = teams.first != teams.end;
    if (restricted && is_mp && (!mine || !native<BinaryTest>(0x1663C0)(zone + 0xA0, mine))) return false;
    auto point = extender(source, 0x10FBFF0);
    if (point) return (!is_mp || mine) && safe_flag(source, point, actor, mine);
    if (!smoke::has_actor(source)) {
        auto flag = parent_flag(w, source);
        if (flag) return (!is_mp || mine) && safe_flag(flag, extender(flag, 0x10FBFF0), actor, mine);
        return native_string(source + 0xF0) != "flagpoint_ammo" && !extender(source, 0x10FDD88);
    }
    if (!smoke::allies(base, owner(actor), owner(source))) return false;
    auto crew = span(read<Address>(source, 0x568), 0x10);
    if (!restricted && (read<Byte>(source, 0x30) & 8) && crew.first != crew.end && is_mp) {
        auto theirs = team(source);
        return mine && theirs && native<BinaryTest>(0x565340)(mine, theirs);
    }
    return true;
}
inline const char* service_rejection(Address a) {
    auto tags = span(a, 0x4E8);
    if (!tags.valid(0x60, 4096) || tag(tags, "crushed") || tag(tags, "destroyed")) return "destruction_tags";
    // Stock wooden supply crates declare "burning" even before ignition.
    auto fire = extender(a, 0x10FD478);
    if (fire && (read<Address>(fire, 0x88) != read<Address>(fire, 0x90) || read<int>(fire, 0xA8) ||
                 !(read<float>(fire, 0xA0) <= 0)))
        return "burning";
    if ((read<Byte>(a, 0x30) & 8) && smoke::has_actor(a)) {
        auto chassis = read<Address>(a, 0xDE0);
        if (!chassis || (read<unsigned>(chassis, 0x48) & 0x10)) return "source_chassis";
        if (!working_hull(a)) return "working_hull";
    }
    return nullptr;
}
inline bool serviceable(Address a) {
    return !service_rejection(a);
}
struct Destination {
    Identity source{};
    Vec3 goal{};
    float radius{}, period{};
};
inline bool goal_crowded(const World& w, Address actor, Vec3 point);
inline const char* source_rejection(const World& w, Address actor, const Gun& g, Address source, Address zone,
                                    Destination& d, bool select) {
    if (actor == source || !within(pos(actor), pos(source), max_travel_metres() * w.length)) return "range";
    if (!zone || !read<Byte>(zone, 0x430)) return "zone_disabled_or_missing";
    if (const auto* reason = service_rejection(source)) return reason;
    float r = read<float>(zone, 0x88) * w.length, period = read<float>(zone, 0x8C);
    if (!std::isfinite(r) || r <= 0 || !std::isfinite(period) || period <= 0 || period > 90)
        return "zone_radius_period";
    if (!source_owner(w, source, zone, actor)) return "source_owner";
    if (const auto* reason = supply_rejection(zone, actor, g)) return reason;
    auto threats = danger(w, actor, pos(source), safety_metres);
    if (threats.unsafe) return "source_danger";
    if (select && !approach(w, actor, pos(source), r * .9f, d.goal, [&](Vec3 point) {
            if (goal_crowded(w, actor, point)) return false;
            bool clear = true;
            actors(w, [&](Address other) {
                if (other != actor && other != source &&
                    within(pos(other), point,
                           ((read<Byte>(actor, 0x30) | read<Byte>(other, 0x30)) & 8 ? 5.f : 1.5f) * w.length))
                    clear = false;
            });
            return clear;
        }))
        return "approach_corridor";
    if (!within(d.goal, pos(source), r)) return "goal_outside_zone";
    auto route_end = danger(w, actor, d.goal, safety_metres);
    if (route_end.unsafe) return "approach_danger";
    d.source = identity(source);
    d.radius = r;
    d.period = period;
    return nullptr;
}
inline bool check_source(const World& w, Address actor, const Gun& g, Address source, Destination& d, bool select) {
    return !source_rejection(w, actor, g, source, extender(source, runtime::table().vt.supply_zone_ext), d, select);
}
template <class Visit> inline void source_entities(const World& w, Visit visit) {
    // 0x490670 enumerates registered actors through eScene+0x60. A truck is
    // still a source when absent from the +0x48 visibility-update vector.
    actors(w, visit);
    entities(w, [&](Address e) {
        if (!(read<Byte>(e, 0x30) & 2)) visit(e); // Uncrewed scenery such as supply crates.
    });
}
enum class Phase {
    travel,
    wait,
    returning,
    cooldown
};
struct Trip {
    Identity actor{};
    Destination dest{};
    OrderRef order{};
    Phase phase{Phase::cooldown};
    unsigned since{}, timeout{}, progress_at{}, owner_id{}, last_fill_tick{}, wait_started{};
    Address brain{}, weapon_def{};
    Vec3 previous{}, home{}, return_goal_pos{};
    float remaining{}, travelled{};
    int last_ammo{};
    bool return_home{}, return_issued{}, return_refilled{}, got_ammo{};
};
inline float distance_travelled(const Trip& trip, Vec3 current) {
    return trip.travelled + std::sqrt(smoke::distance2(trip.previous, current));
}
inline store::GrowableTable<Trip, 64, 4096> trips{};
inline Address scene{};
inline unsigned last_tick{};
inline bool busy{};
struct FailedSource {
    Identity unit{}, source{};
    unsigned tick{};
};
inline store::GrowableTable<FailedSource, 64, 4096> recent_failures{};
inline bool suppressed(const World& w, Address actor, Address source) {
    for (const auto& f : recent_failures)
        if (f.unit.matches(actor) && f.source.matches(source) && w.now - f.tick < w.duration(60)) return true;
    return false;
}
inline void stop(const World& w, Trip& t, float seconds, bool failed = true) {
    if (failed && t.dest.source.ptr && t.phase != Phase::cooldown) {
        FailedSource* slot = nullptr;
        for (auto& f : recent_failures)
            if (!f.unit.ptr || w.now - f.tick >= w.duration(60)) {
                slot = &f;
                break;
            }
        if (!slot) slot = recent_failures.append_slot();
        if (slot) *slot = {t.actor, t.dest.source, w.now};
    }
    t.order.abandon();
    t.phase = Phase::cooldown;
    t.since = w.now;
    t.timeout = w.duration(seconds);
}
struct Scan {
    Identity actor{};
    unsigned tick{}, trace_tick{};
    bool scanned{}, traced{};
};
inline store::GrowableTable<Scan, 1024, 1024> scans{};
inline Scan& scan_for(Address actor) {
    scans.ensure_min_size(1024);
    const auto id = read<unsigned>(actor, 0xC0);
    auto& entry = scans[(id ^ (id >> 10)) % scans.size()];
    if (!entry.actor.matches(actor)) entry = {identity(actor)};
    return entry;
}
inline bool due(const World& w, Address actor) {
    auto& entry = scan_for(actor);
    if (entry.scanned && w.now - entry.tick < w.duration(3)) return false;
    entry.scanned = true;
    entry.tick = w.now;
    return true;
}
inline void trace_rejection(const World& w, Address actor, const char* gate, Gun g = {}, int rounds = -1,
                            Address source = 0, unsigned visited = 0, unsigned zones = 0) {
    if (!log::enabled(log::Level::verbose)) return;
    auto& entry = scan_for(actor);
    if (entry.traced && w.now - entry.trace_tick < w.duration(10)) return;
    entry.traced = true;
    entry.trace_tick = w.now;
    const auto brain = read<Address>(actor, 0xDC8);
    const auto order = brain ? read<Address>(brain, 0xF0) : 0;
    const auto keys = g.def ? span(g.def + 0x868, 0x318) : Span{};
    const auto weapon =
        keys.first != keys.end && keys.valid(0x60, 4096) ? native_string(keys.first + 0x40) : std::string_view{};
    log::write(log::Level::verbose,
               "AUTO-RESUPPLY REJECT mid=%u gate=%s flags=%08x order=%llx weapon=%.*s ammo=%d "
               "entities=%u zones=%u source=%llx source_mid=%u dist=%.1fm",
               unsigned(read<std::uint16_t>(actor, 0xDAA)), gate, brain ? read<unsigned>(brain, 0x39C) : 0u,
               static_cast<unsigned long long>(order ? read<Address>(order) - base : 0),
               static_cast<int>(weapon.size()), weapon.empty() ? "" : weapon.data(), rounds, visited, zones,
               static_cast<unsigned long long>(source),
               smoke::has_actor(source) ? unsigned(read<std::uint16_t>(source, 0xDAA)) : 0u,
               source ? std::sqrt(smoke::distance2(pos(actor), pos(source))) / w.length : 0.f);
}
inline void reset() {
    for (auto& t : trips) {
        t.order.abandon();
        t = {};
    }
    recent_failures.fill({});
    scans.fill({});
    scene = 0;
    last_tick = 0;
}
inline void after_hooks_removed() {
    reset();
    trips.release();
    recent_failures.release();
    scans.release();
}
inline void refresh(const World& w) {
    if (w.scene != scene || w.now - last_tick > 0x7fffffffu) reset();
    for (auto& t : trips)
        if (t.actor.ptr && w.now - t.since >= t.timeout) {
            if (t.phase == Phase::cooldown)
                t = {};
            else
                stop(w, t, 20);
        }
    scene = w.scene;
    last_tick = w.now;
}
inline bool occupied(Address a) {
    for (const auto& t : trips)
        if (t.phase != Phase::cooldown && t.actor.matches(a)) return true;
    return false;
}
inline bool goal_crowded(const World& w, Address actor, Vec3 point) {
    const float sep = ((read<Byte>(actor, 0x30) & 8) ? 5.f : 1.5f) * w.length;
    for (const auto& t : trips)
        if (t.phase != Phase::cooldown && t.phase != Phase::returning && t.actor.ptr && !t.actor.matches(actor) &&
            within(t.dest.goal, point, sep))
            return true;
    return false;
}
inline unsigned claimed_count(Address source) {
    unsigned count = 0;
    for (const auto& t : trips)
        if (t.phase != Phase::cooldown && t.phase != Phase::returning && t.dest.source.matches(source)) ++count;
    return count;
}
inline bool claimed(Address source) {
    return claimed_count(source) >= max_inbound_per_source;
}
inline const char* recipient_rejection(Address actor) {
    if (!autonomous(actor, false)) return "autonomous";
    if (!inventory_free(actor)) return "inventory_busy";
    if (extender(actor, runtime::table().vt.supply_zone_ext)) return "supply_carrier";
    auto def = read<Address>(actor, 0x548);
    if (!def || tag(span(def, 0x330), "hidden")) return "recipient_definition";
    if (read<Byte>(actor, 0x30) & 4) {
        auto relation = read<Address>(actor, 0x560);
        return relation && read<int>(relation, 0x7C) != 5 ? nullptr : "recipient_link";
    }
    return service_rejection(actor);
}
inline bool recipient(Address actor) {
    return !recipient_rejection(actor);
}
inline void begin_return(const World& w, Trip& t, Address actor, bool refilled, bool source_failed) {
    if (refilled) {
        ++stat_refilled;
        if (log_sink) {
            log_sink("AUTO-RESUPPLY REFILLED #%u: actor=0x%llx source=0x%llx", stat_refilled, (unsigned long long)actor,
                     (unsigned long long)t.dest.source.ptr);
        }
    }
    if (source_failed && t.dest.source.ptr && t.phase != Phase::cooldown && t.phase != Phase::returning) {
        FailedSource* slot = nullptr;
        for (auto& f : recent_failures)
            if (!f.unit.ptr || w.now - f.tick >= w.duration(60)) {
                slot = &f;
                break;
            }
        if (!slot) slot = recent_failures.append_slot();
        if (slot) *slot = {t.actor, t.dest.source, w.now};
    }
    if (t.return_home && hold_position(t.brain) && recipient(actor) && !within(pos(actor), t.home, 2.f * w.length) &&
        !post_overrun(w, actor, t.home, 10.f)) {
        t.order.abandon();
        t.dest = {};
        t.phase = Phase::returning;
        t.return_issued = false;
        t.return_refilled = refilled;
        t.since = t.progress_at = w.now;
        t.timeout = w.duration(40);
        t.previous = pos(actor);
        t.travelled = 0.f;
        t.remaining = std::sqrt(smoke::distance2(pos(actor), t.home));
        t.return_goal_pos = {};
        return;
    }
    stop(w, t, refilled ? 30 : 20, false);
}
inline bool permit(const World& w, Address actor, Address order) {
    for (auto& t : trips)
        if (t.actor.matches(actor) && t.order.get() == order) {
            if (scene != w.scene || w.now - t.since >= t.timeout || w.now - t.progress_at >= w.duration(9) ||
                !recipient(actor) || read<Address>(actor, 0xDC8) != t.brain || owner(actor) != t.owner_id ||
                !current_order(actor, order) || !within_travel_budget(w, distance_travelled(t, pos(actor))))
                return false;
            if (t.phase == Phase::returning)
                return t.return_home && hold_position(t.brain) && !post_overrun(w, actor, t.home, 10.f);
            if (t.phase != Phase::travel) return false;
            auto g = primary(actor, t.weapon_def);
            auto s = resolve(w, t.dest.source);
            if (!s || t.weapon_def != g.def || !check_source(w, actor, g, s, t.dest, false)) return false;
            return ammo(actor, g) == Ammo::empty || (t.return_home && within(pos(actor), pos(s), t.dest.radius));
        }
    return false;
}
inline void tick(Address actor) {
    World w{};
    if (!config::active().modules.auto_resupply || busy || !accepting.load(std::memory_order_acquire) ||
        !callback(actor, w))
        return;
    struct Scope {
        Scope() {
            busy = true;
        }
        ~Scope() {
            busy = false;
        }
    } scope;
    supply_assignment = &occupied;
    coord::instance().register_provider(coord::Module::auto_resupply, coord::Priority::auto_resupply, &occupied);
    refresh(w);
    Trip* job = nullptr;
    for (auto& t : trips)
        if (t.actor.matches(actor)) {
            job = &t;
            break;
        }
    if (job && job->phase == Phase::cooldown) return;
    if (const auto* reason = recipient_rejection(actor)) {
        trace_rejection(w, actor, reason);
        if (job) stop(w, *job, 20, job->phase != Phase::returning);
        return;
    }
    if (job && job->phase == Phase::returning) {
        auto& t = *job;
        if (t.brain != read<Address>(actor, 0xDC8) || t.owner_id != owner(actor) || !t.return_home ||
            !hold_position(t.brain) || post_overrun(w, actor, t.home, 10.f)) {
            stop(w, t, t.return_refilled ? 30 : 20, false);
            return;
        }
        if (within(pos(actor), t.home, 2.f * w.length)) {
            stop(w, t, t.return_refilled ? 30 : 12, false);
            return;
        }
        auto owned = t.order.get();
        if (!owned) {
            if (t.return_issued) {
                stop(w, t, t.return_refilled ? 30 : 12, false);
                return;
            }
            if (!free_slot(actor)) {
                if (w.now - t.since >= w.duration(2)) stop(w, t, t.return_refilled ? 30 : 20, false);
                return;
            }
            Vec3 goal{};
            if (!return_goal(w, actor, t.home, goal) || !move(actor, goal, permit, t.order)) {
                stop(w, t, t.return_refilled ? 30 : 20, false);
                return;
            }
            t.return_issued = true;
            t.return_goal_pos = goal;
            t.previous = pos(actor);
            t.progress_at = w.now;
            t.remaining = std::sqrt(smoke::distance2(pos(actor), goal));
            ++stat_returns;
            if (log_sink) {
                log_sink("AUTO-RESUPPLY RETURN #%u: actor=0x%llx dist=%.1fm", stat_returns, (unsigned long long)actor,
                         t.remaining / w.length);
            }
            return;
        }
        if (!current_order(actor, owned)) {
            stop(w, t, t.return_refilled ? 30 : 20, false);
            return;
        }
        t.travelled = distance_travelled(t, pos(actor));
        t.previous = pos(actor);
        if (!within_travel_budget(w, t.travelled)) {
            stop(w, t, t.return_refilled ? 30 : 20, false);
            return;
        }
        float rem = std::sqrt(smoke::distance2(pos(actor), t.return_goal_pos));
        if (t.remaining - rem >= .5f * w.length) {
            t.progress_at = w.now;
            t.remaining = rem;
        }
        if (w.now - t.progress_at >= w.duration(9)) stop(w, t, t.return_refilled ? 30 : 20, false);
        return;
    }
    if (!job) {
        if (!free_slot(actor)) return trace_rejection(w, actor, "order_slot");
        if (medical_assignment && medical_assignment(actor)) return trace_rejection(w, actor, "medical_assignment");
        if (!coord::instance().can_act(actor, coord::Module::auto_resupply, coord::Priority::auto_resupply,
                                       read<std::uint32_t>(actor, 0xC0), w.now))
            return trace_rejection(w, actor, "coordinator");
        if (!due(w, actor)) return;
    }
    const auto g = primary(actor, job ? job->weapon_def : 0);
    const int cur_ammo = ammo_count(actor, g);
    const Ammo state = cur_ammo < 0 ? Ammo::unknown : (cur_ammo > 0 ? Ammo::available : Ammo::empty);
    if (!job) {
        if (state != Ammo::empty)
            return trace_rejection(w, actor,
                                   !g.def         ? "primary"
                                   : cur_ammo < 0 ? "ammo_unknown"
                                                  : "ammo_available",
                                   g, cur_ammo);
        for (auto& t : trips)
            if (!t.actor.ptr) {
                job = &t;
                break;
            }
        if (!job) job = trips.append_slot();
        if (!job) return trace_rejection(w, actor, "trip_table", g, cur_ammo);
        auto& t = *job;
        t.actor = identity(actor);
        t.brain = read<Address>(actor, 0xDC8);
        t.owner_id = owner(actor);
        t.home = pos(actor);
        t.return_home = hold_position(t.brain);
        t.return_issued = false;
        t.return_refilled = false;
        t.got_ammo = false;
        t.last_ammo = 0;
        t.last_fill_tick = w.now;
        t.wait_started = 0;
        float best = max_travel_metres() * w.length;
        best *= best;
        const char* rejection = "no_zone_extender";
        Address nearest{};
        float nearest_d2 = std::numeric_limits<float>::infinity();
        unsigned visited{}, zones{};
        source_entities(w, [&](Address s) {
            ++visited;
            const auto zone = extender(s, runtime::table().vt.supply_zone_ext);
            if (!zone) return;
            ++zones;
            float d2 = smoke::distance2(pos(actor), pos(s));
            if (t.dest.source.ptr && d2 == best && read<unsigned>(s, 0xC0) >= t.dest.source.id) return;
            Destination candidate{};
            const char* reason = "range";
            if (d2 <= best) {
                if (claimed(s))
                    reason = "claimed";
                else if (suppressed(w, actor, s))
                    reason = "recent_failure";
                else
                    reason = source_rejection(w, actor, g, s, zone, candidate, true);
            }
            if (!reason) {
                best = d2;
                t.dest = candidate;
            } else if (d2 < nearest_d2) {
                nearest_d2 = d2;
                nearest = s;
                rejection = reason;
            }
        });
        if (!t.dest.source.ptr) {
            trace_rejection(w, actor, visited ? rejection : "no_entities", g, cur_ammo, nearest, visited, zones);
            stop(w, t, 6);
            return;
        }
        t.weapon_def = g.def;
        t.phase = Phase::travel;
        t.since = t.progress_at = w.now;
        t.timeout = w.duration(40);
        t.previous = pos(actor);
        t.remaining = std::sqrt(smoke::distance2(pos(actor), t.dest.goal));
        if (!within(pos(actor), pos(t.dest.source.ptr), t.dest.radius) && !move(actor, t.dest.goal, permit, t.order)) {
            trace_rejection(w, actor, "move_install", g, cur_ammo, t.dest.source.ptr, visited, zones);
            stop(w, t, 20);
            return;
        }
        ++stat_trips;
        if (log_sink) {
            log_sink("AUTO-RESUPPLY TRIP #%u: actor=0x%llx source=0x%llx dist=%.1fm", stat_trips,
                     (unsigned long long)actor, (unsigned long long)t.dest.source.ptr, t.remaining / w.length);
        }
    }
    auto& t = *job;
    if (t.brain != read<Address>(actor, 0xDC8) || t.owner_id != owner(actor) || g.def != t.weapon_def) {
        begin_return(w, t, actor, t.got_ammo, !t.got_ammo);
        return;
    }
    auto source = resolve(w, t.dest.source);
    if (state == Ammo::available) {
        if (!t.got_ammo || cur_ammo > t.last_ammo) {
            t.got_ammo = true;
            t.last_ammo = cur_ammo;
            t.last_fill_tick = w.now;
        }
        if (!t.return_home) {
            begin_return(w, t, actor, true, false);
            return;
        }
        if (!source || !within(pos(actor), pos(source), t.dest.radius)) {
            begin_return(w, t, actor, true, false);
            return;
        }
    } else if (state != Ammo::empty) {
        begin_return(w, t, actor, t.got_ammo, !t.got_ammo);
        return;
    }
    if (!source || !check_source(w, actor, g, source, t.dest, false)) {
        begin_return(w, t, actor, t.got_ammo, !t.got_ammo);
        return;
    }
    auto owned = t.order.get();
    if (owned ? !current_order(actor, owned) : !free_slot(actor)) {
        stop(w, t, t.got_ammo ? 30 : 20, !t.got_ammo);
        return;
    }
    if (t.phase == Phase::wait) {
        if (!within(pos(actor), pos(source), t.dest.radius)) {
            begin_return(w, t, actor, t.got_ammo, !t.got_ammo);
            return;
        }
        if (t.got_ammo) {
            const float idle_limit = (std::max)(2.5f, 1.8f * t.dest.period + 1.0f);
            const float wait_limit = g.human ? 20.f : 35.f;
            if (w.now - t.last_fill_tick >= w.duration(idle_limit) || w.now - t.wait_started >= w.duration(wait_limit))
                begin_return(w, t, actor, true, false);
        } else if (w.now - t.since >= w.duration(4 + 2 * t.dest.period)) {
            begin_return(w, t, actor, false, true);
        }
        return;
    }
    t.travelled = distance_travelled(t, pos(actor));
    t.previous = pos(actor);
    if (!within_travel_budget(w, t.travelled)) {
        begin_return(w, t, actor, t.got_ammo, !t.got_ammo);
        return;
    }
    float remaining = std::sqrt(smoke::distance2(pos(actor), t.dest.goal));
    if (t.remaining - remaining >= .5f * w.length) {
        t.progress_at = w.now;
        t.remaining = remaining;
    }
    if (within(pos(actor), pos(source), t.dest.radius)) {
        // Let the owned point order end before waiting, so vanilla cleanup
        // runs and the next arriving actor cannot displace this one.
        if (owned) return;
        t.phase = Phase::wait;
        t.since = t.wait_started = w.now;
        t.timeout = w.duration(t.return_home ? (g.human ? 25.f : 40.f) : (6 + 2 * t.dest.period));
    } else if (!owned || w.now - t.progress_at >= w.duration(9) || w.now - t.since >= w.duration(38)) {
        begin_return(w, t, actor, t.got_ammo, !t.got_ammo);
    }
}
} // namespace goh::auto_resupply
