#pragma once

#include "auto_medic_module.hpp"
#include "auto_resupply_module.hpp"
#include "close_grenade_module.hpp"

namespace goh::squad_logistics {
namespace s = ai_support;
namespace med = auto_medic;
using s::Address;
using s::read;
using s::Vec3;
using s::World;
inline constexpr bool runtime_allowed = false;
inline std::atomic<unsigned> stat_ammo{}, stat_bandages{}, stat_scavenge{};
inline std::atomic<bool> stopping{};
inline std::atomic<unsigned> admissions{};

namespace policy {
inline bool needs_ammo(int reserve, int capacity, bool single) {
    return reserve >= 0 && capacity > 0 && reserve <= (single ? 0 : (std::max)(1, capacity / 2));
}
inline bool needs_bandage(float hp, float maximum, int count) {
    return std::isfinite(hp) && std::isfinite(maximum) && maximum > 0 && hp > 0 && hp / maximum < .70f && count == 0;
}
inline int ammo_amount(int stack, int reserve, int capacity, bool single, bool uses_key) {
    if (capacity <= 0 || stack <= 0 || reserve < stack) return 0;
    const int load = single ? 1 : capacity;
    const auto twice = 2LL * load;
    if (!uses_key) return static_cast<int>((std::min<long long>)(stack, twice));
    return reserve >= twice ? (std::min)(stack, reserve / 2) : 0;
}
inline int bandage_amount(int stack, int reserve, bool medic) {
    return stack > 0 && reserve >= stack && reserve >= (medic ? 3 : 2) ? 1 : 0;
}
struct Rank {
    bool same_squad{}, assistant{};
    float distance2{};
    unsigned id{};
};
inline bool rank_share_donor(Rank a, Rank b) {
    if (a.same_squad != b.same_squad) return a.same_squad;
    if (a.assistant != b.assistant) return a.assistant;
    if (a.distance2 != b.distance2) return a.distance2 < b.distance2;
    return a.id < b.id;
}
} // namespace policy

enum class Kind {
    ammo,
    bandage
};
struct Need {
    Kind kind{};
    Address gun{};
    int capacity{};
    bool single{};
};
struct Offer {
    Address item{};
    int amount{};
    bool assistant{};
};
struct Person {
    s::Identity key{};
    Address brain{};
    unsigned player{};
    static Person from(Address actor) {
        return {s::identity(actor), read<Address>(actor, 0xDC8), s::owner(actor)};
    }
    bool matches(Address a) const {
        return key.matches(a) && brain == read<Address>(a, 0xDC8) && player == s::owner(a);
    }
};
enum class Phase {
    assigned,
    walking,
    exchange,
    returning,
    cooldown
};
struct Job {
    Person receiver{}, donor{};
    Need need{};
    Phase phase{};
    Vec3 home{}, endpoint{};
    bool hold{}, moved{}, issued_return{}, interrupted{}, delivered{};
    bool scavenge{};
    Address ammo_def{};
    unsigned since{}, cooldown_at{};
    int before{}, amount{};
    med::ExchangeRef transfer{};
    s::OrderRef move{};
};
inline store::GrowableTable<Job, 32, 4096> jobs{};
inline Address scene{};
inline unsigned last_tick{};
inline bool ticking{};

inline s::LooseStack contents(Address actor, const Need& need) {
    if (need.kind == Kind::ammo) return s::loose_ammo(actor, need.gun);
    return s::loose_stacks(actor, [](Address def) { return classify::def_has_tag(def, "bandage"); });
}
inline bool needs(Address actor, const Need& need) {
    const int reserve = contents(actor, need).total;
    if (need.kind == Kind::bandage) {
        const auto vit = read<Address>(read<Address>(actor, 0x578), 0x18);
        if (!vit || reserve != 0) return false;
        using Health = float (*)(Address);
        return policy::needs_bandage(s::native<Health>(0x93D880)(vit), s::native<Health>(0x93D800)(vit), reserve);
    }
    return policy::needs_ammo(reserve, need.capacity, need.single) && auto_resupply::primary(actor).def == need.gun;
}

inline bool uses_ammo(Address donor, Address ammo_def) {
    const auto holder = s::native<s::Unary>(0x7E04B0)(donor);
    const auto hand = holder ? read<s::Unary>(read<Address>(holder), 0x18)(holder) : 0;
    auto uses = [=](Address item) {
        if (!item || read<int>(item, 0x48) <= 0) return false;
        const auto def = read<Address>(item, 0x40);
        return auto_resupply::weapon_definition(def) &&
               s::native<s::BinaryTest>(0x565A50)(ammo_def + 0xEC8, def + 0x120);
    };
    if (uses(hand)) return true;
    const auto box = s::box(donor);
    const auto rows = s::span(box, 0x10);
    if (!rows.valid(8, 2048)) return true;
    for (auto p = rows.first; p != rows.end; p += 8) {
        const auto item = read<Address>(p);
        if (item && item != hand && read<Address>(item, 0x30) == box && uses(item)) return true;
    }
    return false;
}
inline bool medic_breed(Address actor) {
    const auto breed = read<Address>(actor, 0x1390);
    if (!breed) return true; // Preserve two bandages for an unknown breed.
    const auto abilities = s::span(breed + 0x528, 0x20);
    if (!abilities.valid(8, 1024)) return true;
    for (auto p = abilities.first; p != abilities.end; p += 8)
        if (read<Address>(read<Address>(p)) == s::base + runtime::table().vt.info_medic) return true;
    return false;
}
inline Offer offer(Address donor, const Need& need) {
    const auto stock = contents(donor, need);
    if (!stock.item) return {};
    if (need.kind == Kind::bandage)
        return {stock.item, policy::bandage_amount(stock.count, stock.total, medic_breed(donor)), false};
    const bool uses = uses_ammo(donor, read<Address>(stock.item, 0x40));
    return {stock.item, policy::ammo_amount(stock.count, stock.total, need.capacity, need.single, uses), !uses};
}
using s::visible_near;
inline bool free_actor(const World& w, Address a, Address owned_order = 0,
                       s::Test selected = s::native<s::Test>(0x91B190)) {
    if (!s::autonomous_if(a, true, [](unsigned bits, unsigned flags) { return s::policy_allows(bits, flags, false); }))
        return false;
    if (selected(read<Address>(a, 0xDC8)) || !s::smoke::finite(s::pos(a)) ||
        !coord::instance().can_act(a, coord::Module::squad_logistics, coord::Priority::squad_logistics,
                                   read<unsigned>(a, 0xC0), w.now) ||
        med::occupied(a) || auto_resupply::occupied(a) || close_grenade::is_busy(w, a))
        return false;
    return owned_order ? s::current_order(a, owned_order) : s::free_slot(a);
}
inline const Person& walker(const Job& j) {
    return j.scavenge ? j.receiver : j.donor;
}

struct SourceBackoff {
    s::Identity receiver{}, source{};
    unsigned since{};
};
inline store::GrowableTable<SourceBackoff, 32, 2048> source_backoffs{};
inline bool source_blocked(const World& w, Address receiver, Address source) {
    for (const auto& row : source_backoffs)
        if (w.now - row.since < w.duration(30) && row.receiver.matches(receiver) && row.source.matches(source))
            return true;
    return false;
}
inline void fail_scavenge(const World& w, const Job& j) {
    SourceBackoff* slot = nullptr;
    for (auto& row : source_backoffs)
        if (!row.receiver.ptr || w.now - row.since >= w.duration(30)) {
            slot = &row;
            break;
        }
    if (!slot) slot = source_backoffs.append_slot();
    if (slot) *slot = {j.receiver.key, j.donor.key, w.now};
}

// Use the mounted inventory item's definition for the ammo query. The weapon's +0x98 is its firing profile.
inline bool vehicle_uses_ammo(Address vehicle, Address ammo,
                              s::BinaryTest matches = s::native<s::BinaryTest>(0x565A50)) {
    const auto weaponry = read<Address>(vehicle, 0xDD8);
    const auto weapons = s::span(weaponry, 0x30);
    if (!weaponry || !weapons.valid(8, 256)) return true;
    for (auto p = weapons.first; p != weapons.end; p += 8) {
        const auto weapon = read<Address>(p);
        if (!weapon || read<Address>(weapon, 0x90) != vehicle) return true;
        if (s::smoke::SmokeTagEquals(weapon + 8, "dummy")) continue;
        const auto def = read<Address>(read<Address>(weapon, 0x218), 0x40);
        if (!auto_resupply::weapon_definition(def) || matches(ammo + 0xEC8, def + 0x120)) return true;
    }
    return false;
}
// Use actual player diplomacy for unmanned neutral vehicles. A missing table is
// insufficient evidence of neutrality (the native relation helper also returns 0 for invalid owners).
inline bool neutral_owners(const World& w, unsigned a, unsigned b) {
    const auto stride = read<int>(w.base, 0x15C9710);
    const auto relations = read<Address>(w.base, 0x15C9700);
    return relations && a < 18 && b < 18 && stride > int(a) && stride > int(b) && stride <= 256 &&
           read<int>(relations, (b * static_cast<unsigned>(stride) + a) * sizeof(int)) == 0;
}
inline bool safe_vehicle_crew(const World& w, Address receiver, Address source, bool allied) {
    const auto placer = read<Address>(source, 0x568);
    if (!placer || read<Address>(placer, 0x40) != source || read<s::Byte>(placer, 0x48)) return false;
    const auto crew = s::span(placer, 0x10), seats = s::span(placer, 0x28);
    if (!crew.valid(sizeof(short), 1024) || !seats.valid(0x298, 1024) || seats.first == seats.end ||
        (!allied && crew.first != crew.end))
        return false;
    for (auto p = seats.first; p != seats.end; p += 0x298) {
        if (read<Address>(p, 0x290)) return false;
        const auto occupant = read<Address>(p, 0x280);
        if (occupant && (!allied || !s::smoke::has_actor(occupant) ||
                         !s::smoke::allies(w.base, s::owner(receiver), s::owner(occupant))))
            return false;
    }
    return true;
}
using Velocity = Vec3* (*)(Address, Vec3*);
inline bool scavenge_source(const World& w, Address receiver, Address source,
                            Velocity velocity = s::native<Velocity>(0x73A2A0)) {
    if (!s::live_entity(source) || source == receiver || !s::box(source) ||
        (read<unsigned>(source, 0x1C0) & (1u << 28)) || s::extender(source, runtime::table().vt.supply_zone_ext))
        return false;
    const auto fire = s::extender(source, runtime::table().vt.burning);
    if (fire && (read<Address>(fire, 0x88) != read<Address>(fire, 0x90) || read<int>(fire, 0xA8) ||
                 !(read<float>(fire, 0xA0) <= 0)))
        return false;
    const auto type = read<s::Byte>(source, 0x30);
    if (!s::smoke::has_actor(source)) return !(type & (4 | 8));
    const auto burn = read<unsigned>(w.base, 0x15C6A58);
    if (burn < 32 && (read<s::Byte>(source, 0xDAD + burn / 8) & (1u << (burn % 8)))) return false;
    if (type & 4) return s::dead_human(source);
    const auto mine = s::owner(receiver), theirs = s::owner(source);
    const bool allied = s::smoke::allies(w.base, mine, theirs);
    if (!allied && !neutral_owners(w, mine, theirs)) return false;
    if (!(type & 8)) return true;
    if (!safe_vehicle_crew(w, receiver, source, allied)) return false;
    const auto brain = s::authorized_vehicle_brain(w.base, source);
    if (!brain || !s::policy_allows(read<unsigned>(brain, 0x398), read<unsigned>(brain, 0x39C), false) ||
        (read<unsigned>(brain, 0x39C) & ((1u << 6) | (1u << 9) | (1u << 29))) || read<int>(brain, 0x88) ||
        read<Address>(brain, 0x98) != read<Address>(brain, 0xA0) || !s::free_slot(source))
        return false;
    Vec3 sample{};
    const auto* speed = velocity(source, &sample);
    return speed && speed->x == 0 && speed->y == 0 && speed->z == 0;
}
inline Offer scavenge_offer(Address source, const Need& need,
                            s::BinaryTest matches = s::native<s::BinaryTest>(0x565A50)) {
    if (need.kind != Kind::ammo || !need.gun) return {};
    const bool vehicle = (read<s::Byte>(source, 0x30) & 8) != 0;
    const auto stock = s::loose_stacks(source, [&](Address def) {
        return matches(def + 0xEC8, need.gun + 0x120) && (!vehicle || !vehicle_uses_ammo(source, def, matches));
    });
    return {stock.item, policy::ammo_amount(stock.count, stock.total, need.capacity, need.single, false), false};
}
inline bool receiver_threatened(const World& w, Address receiver) {
    return visible_near(receiver, 18 * w.length) || s::post_overrun(w, receiver, s::pos(receiver), 18);
}
inline bool scavenge_enabled(Address brain) {
    return brain && config::active().modules.squad_logistics && config::active().modules.ammo_scavenge &&
           s::policy_allows(read<unsigned>(brain, 0x398), read<unsigned>(brain, 0x39C), true);
}

inline bool active(const Job& j) {
    return j.receiver.key.ptr && !j.interrupted && j.phase != Phase::cooldown;
}
inline Job* assignment(Address a) {
    for (auto& j : jobs)
        if (active(j) && (walker(j).key.matches(a) ||
                          (j.phase != Phase::returning && (j.donor.key.matches(a) || j.receiver.key.matches(a)))))
            return &j;
    return nullptr;
}
inline bool occupied(Address a) {
    return assignment(a) != nullptr;
}
inline void preempt(Address a) {
    if (auto* j = assignment(a)) {
        j->interrupted = true;
        j->move.abandon();
    }
}
inline bool together(const World& w, Address donor, Address receiver, float metres) {
    return s::within(s::pos(donor), s::pos(receiver), metres * w.length);
}
inline bool pair(const World& w, const Job& j, Address donor, Address receiver) {
    if (j.scavenge) {
        if (!j.receiver.matches(receiver) || !j.donor.key.matches(donor) || !scavenge_enabled(j.receiver.brain) ||
            j.hold != s::hold_position(j.receiver.brain) || !free_actor(w, receiver, j.move.get()) ||
            !together(w, donor, receiver, config::active().tuning.scavenge_radius_m) ||
            !scavenge_source(w, receiver, donor) || receiver_threatened(w, receiver) ||
            s::post_overrun(w, receiver, s::pos(donor), 15))
            return false;
        return !j.ammo_def || !(read<s::Byte>(donor, 0x30) & 8) || !vehicle_uses_ammo(donor, j.ammo_def);
    }
    return j.donor.matches(donor) && j.receiver.matches(receiver) && j.hold == s::hold_position(j.donor.brain) &&
           s::smoke::allies(w.base, j.donor.player, j.receiver.player) &&
           together(w, donor, receiver, config::active().tuning.squad_share_radius_m) &&
           free_actor(w, donor, j.move.get()) && free_actor(w, receiver) && !visible_near(donor, 12 * w.length) &&
           !visible_near(receiver, 12 * w.length);
}
inline bool can_return(const World& w, const Job& j, Address mover) {
    const auto& person = walker(j);
    return j.hold && j.moved && person.matches(mover) && s::hold_position(person.brain) &&
           (!j.scavenge || scavenge_enabled(person.brain)) && free_actor(w, mover, j.move.get()) &&
           !visible_near(mover, (j.scavenge ? 18 : 12) * w.length) && !s::post_overrun(w, mover, j.home, 10);
}
inline bool timed_out(const World& w, const Job& j) {
    // A scavenger can walk up to 40 m in either direction. Keep the shorter bound for inventory work.
    const bool long_walk = j.scavenge && (j.phase == Phase::walking || j.phase == Phase::returning);
    return w.now - j.since >= w.duration(long_walk ? 45.f : 12.f);
}
inline bool permit(const World& w, Address mover, Address order) {
    auto* j = assignment(mover);
    if (!j || !walker(*j).key.matches(mover) || j->move.get() != order || scene != s::entity_scene(w) ||
        w.now < last_tick || stopping.load() || !config::active().modules.squad_logistics || timed_out(w, *j))
        return false;
    if (j->phase == Phase::returning) return j->issued_return && can_return(w, *j, mover);
    const auto other = s::resolve(w, j->scavenge ? j->donor.key : j->receiver.key);
    const auto donor = j->scavenge ? other : mover, receiver = j->scavenge ? mover : other;
    return j->phase == Phase::walking && pair(w, *j, donor, receiver) &&
           s::within(j->endpoint, s::pos(other), (j->scavenge ? 3.f : 3.5f) * w.length) && s::inventory_free(mover) &&
           s::inventory_free(other);
}
inline void cancel_transfer(const World& w, Job& j) {
    if (j.transfer.action >= 0) med::cancel(j.transfer, s::resolve(w, walker(j).key));
}
inline void cooldown(const World& w, Job& j) {
    j.move.abandon();
    cancel_transfer(w, j);
    if (j.phase != Phase::returning) j.cooldown_at = w.now;
    j.phase = Phase::cooldown;
}
inline void end_delivery(const World& w, Job& j, bool return_home) {
    if (j.scavenge && !j.delivered && j.phase != Phase::returning) fail_scavenge(w, j);
    if (!return_home) {
        cooldown(w, j);
        return;
    }
    cancel_transfer(w, j);
    // Changing phase withdraws the approach move's Exec permit. Keep its
    // generation until destruction so the return never replaces an active move.
    j.phase = Phase::returning;
    j.since = j.cooldown_at = w.now;
}
inline bool needs_return(const World& w, const Job& j) {
    const auto mover = s::resolve(w, walker(j).key);
    return can_return(w, j, mover) && !s::within(s::pos(mover), j.home, 2.f * w.length);
}
// A single receiver-side scan reserves both actors before lower-priority resupply runs.
// The moving soldier submits both the approach and the transfer from its actor callback.
inline void refresh(const World& w) {
    const bool reset = scene != s::entity_scene(w) || w.now < last_tick;
    if (!reset && last_tick == w.now) return;
    if (reset) source_backoffs.clear();
    for (auto& j : jobs) {
        if (!j.receiver.key.ptr) continue;
        if (reset) {
            j.move.abandon();
            cancel_transfer(w, j);
            j = {};
        } else if (j.phase == Phase::cooldown) {
            if (w.now - j.cooldown_at >= w.duration(j.delivered ? 10.f : 5.f)) j = {};
        } else if (j.interrupted)
            cooldown(w, j);
        else if (timed_out(w, j)) {
            end_delivery(w, j, j.phase != Phase::returning && needs_return(w, j));
        }
    }
    scene = s::entity_scene(w);
    last_tick = w.now;
}
struct Scan {
    s::Identity key{};
    unsigned at{};
};
inline store::GrowableTable<Scan, 64, 4096> scans{};
inline bool due(const World& w, Address actor) {
    Scan* available = nullptr;
    for (auto& row : scans) {
        if (row.key.matches(actor)) {
            if (w.now - row.at < (std::max)(1u, w.duration(.75f))) return false;
            row.at = w.now;
            return true;
        }
        if (!available && (!row.key.ptr || w.now - row.at >= (std::max)(1u, w.duration(.75f)))) available = &row;
    }
    if (!available) available = scans.append_slot();
    if (!available) return false;
    *available = {s::identity(actor), w.now};
    return true;
}
inline bool prefer_donor(const World& w, Address receiver, const Need& need, Job& candidate) {
    policy::Rank best{};
    const auto squad = read<Address>(read<Address>(receiver, 0xDC8), 0x100);
    const float radius = config::active().tuning.squad_share_radius_m * w.length;
    s::actors(w, [&](Address donor) {
        if (donor == receiver || !(read<s::Byte>(donor, 0x30) & 4) ||
            !s::within(s::pos(donor), s::pos(receiver), radius) ||
            !s::smoke::allies(w.base, s::owner(donor), s::owner(receiver)) || occupied(donor) ||
            !free_actor(w, donor) || !s::inventory_free(donor) || visible_near(donor, 18 * w.length))
            return;
        const auto offered = offer(donor, need);
        if (!offered.amount) return;
        const auto brain = read<Address>(donor, 0xDC8);
        policy::Rank rank{squad && read<Address>(brain, 0x100) == squad, offered.assistant,
                          s::smoke::distance2(s::pos(receiver), s::pos(donor)), read<unsigned>(donor, 0xC0)};
        if (!candidate.donor.key.ptr || policy::rank_share_donor(rank, best)) {
            best = rank;
            candidate.donor = Person::from(donor);
            candidate.home = s::pos(donor);
            candidate.hold = s::hold_position(brain);
        }
    });
    if (!candidate.donor.key.ptr) return false;
    candidate.need = need;
    return true;
}
inline bool prefer_scavenge(const World& w, Address receiver, const Need& ammo, Job& candidate) {
    const auto brain = read<Address>(receiver, 0xDC8);
    if (ammo.kind != Kind::ammo || !scavenge_enabled(brain) || receiver_threatened(w, receiver)) return false;
    const float radius = config::active().tuning.scavenge_radius_m * w.length;
    float best2 = radius * radius;
    s::entities(w, [&](Address source) {
        const float d2 = s::smoke::distance2(s::pos(receiver), s::pos(source));
        if (!(d2 <= best2) ||
            (candidate.donor.key.ptr && d2 == best2 && read<unsigned>(source, 0xC0) >= candidate.donor.key.id) ||
            occupied(source) || coord::instance().provider_busy(coord::Module::scavenge, source) ||
            source_blocked(w, receiver, source) || !s::inventory_free(source) ||
            !scavenge_source(w, receiver, source) || !scavenge_offer(source, ammo).amount ||
            s::post_overrun(w, receiver, s::pos(source), 15))
            return;
        best2 = d2;
        candidate.donor.key = s::identity(source);
        // Corpses can outlive their actor-map entry. All scavenge sources came from the scene entity vector.
        candidate.donor.key.mid = 0;
    });
    if (!candidate.donor.key.ptr) return false;
    candidate.scavenge = true;
    candidate.need = ammo;
    candidate.home = s::pos(receiver);
    candidate.hold = s::hold_position(brain);
    return true;
}
inline bool battlefield_corpse(const World& w, Address receiver, const Job& candidate) {
    return candidate.scavenge && config::active().modules.battlefield_scavenge &&
           (read<s::Byte>(candidate.donor.key.ptr, 0x30) & 4) &&
           together(w, receiver, candidate.donor.key.ptr, config::active().tuning.scavenge_search_radius_m);
}
inline void scan(const World& w, Address receiver) {
    if (!(read<s::Byte>(receiver, 0x30) & 4) || !due(w, receiver) || !free_actor(w, receiver) ||
        !s::inventory_free(receiver) || visible_near(receiver, 18 * w.length))
        return;
    Job* slot = nullptr;
    for (auto& j : jobs) {
        if (j.receiver.key.matches(receiver)) return;
        if (!slot && !j.receiver.key.ptr) slot = &j;
    }
    Job candidate{};
    const Need bandage{Kind::bandage};
    if (!needs(receiver, bandage) || !prefer_donor(w, receiver, bandage, candidate)) {
        const auto gun = auto_resupply::primary(receiver);
        if (!gun.def) return;
        const int capacity = read<int>(gun.def, 0x470);
        const Need ammo{Kind::ammo, gun.def, capacity,
                        capacity == 1 || classify::def_has_tag(gun.def, "rocketlauncher")};
        if (!policy::needs_ammo(contents(receiver, ammo).total, capacity, ammo.single) ||
            (!prefer_donor(w, receiver, ammo, candidate) && !prefer_scavenge(w, receiver, ammo, candidate)))
            return;
    }
    // When both scavenging options select a nearby body, let the battlefield
    // search consider rearming and heavy weapons before collecting its ammunition.
    if (battlefield_corpse(w, receiver, candidate)) return;
    if (!slot) slot = jobs.append_slot();
    if (slot) {
        candidate.receiver = Person::from(receiver);
        candidate.since = w.now;
        *slot = candidate;
    }
}

struct NativeDelivery {
    const World& w;
    Job& j;
    Address donor, receiver;
    Address mover() const {
        return j.scavenge ? receiver : donor;
    }
    Address other() const {
        return j.scavenge ? donor : receiver;
    }
    float range_m() const {
        return j.scavenge ? 3.f : 3.5f;
    }
    Offer selected_offer() const {
        return j.scavenge ? scavenge_offer(donor, j.need) : offer(donor, j.need);
    }
    bool move_to(bool home) {
        Vec3 goal{};
        const bool ok = home ? s::return_goal(w, mover(), j.home, goal)
                             : s::approach(w, mover(), s::pos(other()), range_m() * w.length, goal);
        if (!ok) return false;
        j.endpoint = goal;
        return s::move(mover(), goal, permit, j.move);
    }
    bool in_range() const {
        return together(w, donor, receiver, range_m());
    }
    bool at_post() const {
        return s::within(s::pos(mover()), j.home, 2.f * w.length);
    }
    bool safe_return() const {
        return can_return(w, j, mover());
    }
    bool ready() const {
        return s::inventory_free(donor) && s::inventory_free(receiver) && needs(receiver, j.need) &&
               selected_offer().amount;
    }
    bool send() const {
        const auto selected = selected_offer();
        j.before = contents(receiver, j.need).total;
        j.amount = selected.amount;
        if (!j.amount || j.before < 0) return false;
        if (!j.scavenge) return med::begin_exchange(j.transfer, donor, receiver, selected.item, j.amount);
        j.ammo_def = read<Address>(selected.item, 0x40);
        return med::begin_exchange(j.transfer, donor, receiver, selected.item, j.amount, receiver);
    }
    enum class Receipt {
        pending,
        success,
        failure
    };
    Receipt receipt() const {
        if (!in_range() || j.transfer.action < 0 || !j.transfer.operation || !j.transfer.transfer || j.before < 0 ||
            j.amount <= 0)
            return Receipt::failure;
        if (auto op = med::queued(j.transfer, mover()))
            return med::exclusive_exchange(op, mover(), other()) ? Receipt::pending : Receipt::failure;
        // The native manager owns placement and stack merging. Require its marked operation to
        // drain and verify the requested quantity in the receiver's compatible loose stacks.
        if ((j.transfer.transfer->generation == j.transfer.transfer_serial &&
             j.transfer.transfer->operation.load() == j.transfer.operation) ||
            !s::inventory_free(donor) || !s::inventory_free(receiver))
            return Receipt::failure;
        return contents(receiver, j.need).total >= static_cast<long long>(j.before) + j.amount ? Receipt::success
                                                                                               : Receipt::failure;
    }
};
template <class Delivery> inline void progress(const World& w, Job& j, Delivery& d) {
    if (j.phase == Phase::returning) {
        if (!d.safe_return() || d.at_post()) {
            cooldown(w, j);
            return;
        }
        if (j.move.get()) return;
        if (j.issued_return) {
            cooldown(w, j);
            return;
        }
        j.issued_return = true;
        if (!d.move_to(true)) cooldown(w, j);
        return;
    }
    if (j.phase == Phase::exchange) {
        const auto result = d.receipt();
        if (result == NativeDelivery::Receipt::pending) return;
        if (result == NativeDelivery::Receipt::success) {
            j.transfer = {};
            j.delivered = true;
            if (j.scavenge)
                ++stat_scavenge;
            else if (j.need.kind == Kind::bandage)
                ++stat_bandages;
            else
                ++stat_ammo;
        }
        end_delivery(w, j, d.safe_return() && !d.at_post());
        return;
    }
    if (!d.ready()) {
        end_delivery(w, j, d.safe_return() && !d.at_post());
        return;
    }
    if (!d.in_range()) {
        if (j.phase == Phase::walking) {
            if (!j.move.get()) end_delivery(w, j, d.safe_return() && !d.at_post());
            return;
        }
        j.phase = Phase::walking;
        if (!d.move_to(false)) {
            end_delivery(w, j, false);
            return;
        }
        j.moved = true;
        return;
    }
    // Let the owned move finish its normal Exec/destructor before submitting an inventory operation.
    if (j.move.get()) return;
    j.phase = Phase::exchange;
    j.since = w.now;
    if (!d.send()) end_delivery(w, j, d.safe_return() && !d.at_post());
}
inline void tick(Address actor) {
    World w{};
    if (!s::callback(actor, w) || ticking || !runtime::actor_ai_allowed(actor)) return;
    struct Scope {
        Scope() {
            ++admissions;
            ticking = true;
        }
        ~Scope() {
            ticking = false;
            --admissions;
        }
    } scope;
    if (scene != s::entity_scene(w) || w.now < last_tick) scans.clear();
    refresh(w);
    coord::instance().register_provider(coord::Module::squad_logistics, coord::Priority::squad_logistics, occupied,
                                        preempt);
    if (stopping.load() || !s::accepting.load() || !config::active().modules.squad_logistics) {
        if (auto* j = assignment(actor)) cooldown(w, *j);
        return;
    }
    if (auto* j = assignment(actor)) {
        const auto donor = s::resolve(w, j->donor.key);
        const auto receiver = s::resolve(w, j->receiver.key);
        if (j->phase != Phase::returning && !pair(w, *j, donor, receiver)) {
            end_delivery(w, *j, needs_return(w, *j));
            return;
        }
        if (actor == (j->scavenge ? receiver : donor)) {
            NativeDelivery d{w, *j, donor, receiver};
            progress(w, *j, d);
        }
    } else
        scan(w, actor);
}
inline void begin_shutdown() noexcept {
    stopping.store(true);
}
inline bool quiescent() noexcept {
    return !admissions.load() && !s::watched_count.load() && !med::transfer_count.load();
}
inline void after_hooks_removed() {
    for (auto& j : jobs)
        j.move.abandon();
    jobs.release();
    scans.release();
    source_backoffs.release();
    scene = last_tick = 0;
    stat_ammo = stat_bandages = stat_scavenge = 0;
    stopping.store(false);
}
} // namespace goh::squad_logistics
