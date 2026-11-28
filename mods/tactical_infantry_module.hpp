#pragma once
#include "actor_coordinator.hpp"
#include "config_and_log.hpp"
#include "dynamic_store.hpp"
#include "game_classifiers.hpp"
#include "game_runtime.hpp"
#include "tactical_infantry_policy.hpp"
#include "auto_medic_module.hpp"
#include "auto_resupply_module.hpp"
#include "close_grenade_module.hpp"
#include "tank_auto_repair_module.hpp"

namespace goh::tactical_infantry {
namespace p = policy;
namespace s = ai_support;
namespace sim = auto_smoke;
namespace pr = target_prio::detail;
using p::XY;
using s::Address;
using s::Byte;
using s::native;
using s::read;
using s::World;
inline constexpr bool runtime_allowed = false;
inline constexpr auto& executable_sha256 = s::executable_sha256;
inline std::atomic<unsigned> stat_overflow_cover{}, stat_dismount_spread{}, stat_clump_spread{}, stat_march_cover{},
    stat_wedge_moves{};
namespace detail {
using Tick = std::uint32_t;
template <class T> inline void put(Address ptr, unsigned off, const T& v) {
    std::memcpy(reinterpret_cast<void*>(ptr + off), &v, sizeof(v));
}
struct ActorState : p::Soldier, p::Placement {
    Address key{}, brain{};
    unsigned id{};
    double last{}, role_at{p::never}, sight_at{p::never}, issued_phase{p::never};
    Address issued_squad{}, sight_target{};
    XY goal{};
    p::Role role{};
    p::Sight visibility{};
    bool smg{}, issued_cover{}, at_throwable{}, rush_active{};
    Address gun_def{}, paired_gunner{}, paired_def{}, pair_squad{}, placement_squad{};
    unsigned gunner_id{};
    double pair_at{p::never}, next_follow{p::never};
};
struct SquadState : p::Team {
    Address key{};
    double last{}, issue_phase{p::never};
    unsigned issued_bounds{};
    double dismounted_at{p::never};
    XY dismount_position{};
};
inline store::StableTable<ActorState, 128, 8192> soldiers{};
inline store::StableTable<SquadState, 64, 2048> squads{};
inline p::Claims claims{};
inline Address scene{};
inline Tick clock{};
inline std::atomic<unsigned> issued{};
inline std::atomic<bool> accepting{};
inline std::atomic<unsigned> inflight{};
inline bool installed{}, rollback_pending{};
struct Guard {
    Guard() {
        ++inflight;
        runtime::global_inflight.fetch_add(1, std::memory_order_acq_rel);
    }
    ~Guard() {
        runtime::global_inflight.fetch_sub(1, std::memory_order_acq_rel);
        --inflight;
    }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
};
inline void reset() {
    soldiers.fill({});
    squads.fill({});
    claims.entries.fill({});
    scene = 0;
    clock = 0;
}
inline void release_tables() {
    soldiers.release();
    squads.release();
    claims.entries.release();
    scene = 0;
    clock = 0;
}
template <class Table> auto* record(Table& table, Address key, double now) {
    using T = typename Table::value_type;
    T* available = nullptr;
    for (auto& row : table) {
        if (row.key == key) {
            if (now - row.last > 30) row = {};
            row.key = key;
            row.last = now;
            return &row;
        }
        if (!available && (!row.key || now - row.last > 30)) available = &row;
    }
    if (!available) available = store::try_append_slot(table);
    if (available) {
        *available = {};
        available->key = key;
        available->last = now;
    }
    return available;
}
inline Address complex_front(Address order) noexcept {
    const auto count = read<std::size_t>(order, 0x48);
    const Address map = read<Address>(order, 0x30);
    const auto blocks = read<std::size_t>(order, 0x38);
    const auto start = read<std::size_t>(order, 0x40);
    if (!count || count > 0x7fff || !map || !blocks || (blocks & (blocks - 1)) || (count + 1) / 2 > blocks) return 0;
    const Address block = read<Address>(map, ((start >> 1) & (blocks - 1)) * sizeof(Address));
    return block ? read<Address>(block, (start & 1) * sizeof(Address)) : 0;
}
inline bool protected_order(Address brain, Address actor) {
    Address o = read<Address>(brain, 0xF0);
    const auto& tbl = runtime::table().vt;
    for (unsigned i = 0; o && i < 32; ++i) {
        auto vt = read<Address>(o);
        if (read<Address>(o, 8) != actor + 0xAF0 || vt == s::base + tbl.attack_order ||
            vt == s::base + tbl.avoid_grenade)
            return true;
        if (vt == s::base + tbl.move_order || vt == s::base + tbl.weapon_fire_order) return false;
        if (vt == s::base + tbl.move_task_order || vt == s::base + tbl.advance_order ||
            vt == s::base + tbl.move_to_cover) {
            o = read<Address>(o, 0x30);
            continue;
        }
        if (vt == s::base + 0x110D490) {
            o = complex_front(o);
            continue;
        }
        o = sim::orders::active_child(s::base, o);
    }
    return o != 0;
}
inline bool selected(Address brain) {
    return native<s::Test>(0x91B190)(brain) != 0;
}
inline bool movement_available(const World& w, Address a, Address b, bool overflow) {
    if (!config::active().modules.tactical_infantry) return false;
    const auto permits = [overflow](unsigned modes, unsigned flags) {
        if (overflow && (modes & 0x3000u) == 0x1000u) modes &= ~0x3000u;
        return p::allows(modes, flags);
    };
    if (!s::autonomous_if(a, true, permits) || read<Address>(a, 0xDC8) != b || !read<Address>(a, 0xDC0) ||
        !read<Address>(a, 0xDD0))
        return false;
    if (selected(b) || protected_order(b, a)) return false;
    if (!coord::instance().can_act(a, coord::Module::tactical_infantry, coord::Priority::tactical_infantry,
                                   read<unsigned>(a, 0xC0), w.now))
        return false;
    return !auto_medic::occupied(a) && !auto_resupply::occupied(a) && !sim::is_busy(w, a) &&
           !close_grenade::is_throwing(w, a) && !tank_auto_repair::is_busy(a);
}
inline bool available(const World& w, Address a, Address b) {
    return movement_available(w, a, b, false);
}
inline Address target(Address a, Address b) {
    Address sub = read<Address>(b, 0x3b8);
    return sub && pr::contains(pr::subjects(a), sub) && pr::eligible_subject(b, sub) ? sub : 0;
}
inline ActorState* actor_record(Address actor, Address brain, double now) {
    auto* row = record(soldiers, actor, now);
    if (row) {
        auto id = read<unsigned>(actor, 0xc0);
        if (row->id != id || row->brain != brain) {
            *row = {};
            row->key = actor;
            row->brain = brain;
            row->id = id;
            row->last = now;
        }
        row->observe_cover(read<int>(read<Address>(actor, 0xdc0), 0x24) != 0, now);
    }
    return row;
}
inline p::Balance balance(Address b) {
    return {read<float>(b, 0x518), read<float>(b, 0x51c),     read<float>(b, 0x474),    read<float>(b, 0x524),
            read<float>(b, 0x528), read<Byte>(b, 0x520) != 0, read<Byte>(b, 0x521) != 0};
}
inline void refresh_carried_role(ActorState& row, double now, Address hand, bool medic) {
    bool heavy = false, smg = false, charge = false, at_throw = false;
    row.gun_def = 0;
    auto inspect = [&](Address item) {
        if (!item || read<int>(item, 0x48) <= 0) return;
        auto def = read<Address>(item, 0x40);
        if (!def) return;
        const bool cohesion = config::active().modules.weapon_team_cohesion;
        const bool support = (!cohesion || pr::has_tag(def, "weapon")) &&
                             (classify::def_is_heavy_support_weapon(def) || (cohesion && pr::has_tag(def, "flame")));
        heavy |= support;
        if (support && !row.gun_def) row.gun_def = def;
        smg |= pr::has_tag(def, "smg");
        auto kind = close_grenade::payload(def);
        charge |= kind == close_grenade::Payload::at || kind == close_grenade::Payload::charge;
        at_throw |= kind == close_grenade::Payload::at || kind == close_grenade::Payload::incendiary ||
                    kind == close_grenade::Payload::charge;
    };
    auto box = s::box(row.key);
    auto items = s::span(box, 0x10);
    inspect(hand);
    if (items.valid(8, 1024))
        for (auto i = items.first; i != items.end; i += 8)
            if (auto item = read<Address>(i); item && item != hand && read<Address>(item, 0x30) == box) inspect(item);
    row.role = p::classify(heavy, medic, smg, charge);
    row.smg = smg;
    row.at_throwable = at_throw;
    row.role_at = now;
    row.pair_at = p::never;
}
inline void load_role(ActorState& row, double now) {
    if (now - row.role_at >= 1)
        refresh_carried_role(row, now, pr::hand_item(row.key), auto_medic::is_medic_actor(row.key));
}
struct Context {
    World w{};
    Address actor{}, brain{}, sub{}, squad{};
    double now{};
    ActorState* soldier{};
    SquadState* team{};
    XY self{}, center{}, support{}, threat{};
    bool moving_element{}, wedge_element{};
    p::Roster members{};
    std::size_t count{};
    Address gunner{};
    XY gunner_pos{}, placement_center{}, spread_away{};
    float clump_spacing{std::numeric_limits<float>::infinity()};
};
inline bool movement_roster_permits(Address squad, Address self, bool overflow) {
    if (!squad) return true;
    if (read<Address>(squad) != s::base + 0x110F3A0 || read<Address>(squad, 0x1c8)) return false;
    auto roster = s::span(squad, 0x80);
    if (!roster.valid(8, 64)) return false;
    bool found = false;
    for (auto it = roster.first; it != roster.end; it += 8) {
        auto a = read<Address>(it);
        if (!s::live_entity(a)) continue;
        auto b = read<Address>(a, 0xdc8);
        if (!b || read<Address>(b, 0x38) != a || selected(b)) return false;
        const auto modes = read<unsigned>(b, 0x398);
        if (p::active_user_move(modes, read<unsigned>(b, 0x39c)) || (!overflow && (modes & 0x3000u) == 0x1000u))
            return false;
        found |= a == self;
    }
    return found;
}
inline bool roster_permits(Address squad, Address self) {
    return movement_roster_permits(squad, self, false);
}
// Strictly AI-controlled (control == 0) Move-at-Will squads; permits active squad waypoints (squad+0x1C8).
inline bool ai_march_roster_permits(Address squad, Address self) {
    if (!squad || read<Address>(squad) != s::base + 0x110F3A0) return false;
    const auto roster = s::span(squad, 0x80);
    if (!roster.valid(8, 64)) return false;
    bool found = false;
    for (auto it = roster.first; it != roster.end; it += 8) {
        const auto a = read<Address>(it);
        if (!s::live_entity(a)) continue;
        const auto b = read<Address>(a, 0xdc8);
        if (!b || read<Address>(b, 0x38) != a || selected(b)) return false;
        const auto modes = read<unsigned>(b, 0x398);
        if ((modes & 3u) != 0u || (modes & 0x3000u) != 0u || p::active_user_move(modes, read<unsigned>(b, 0x39c)))
            return false;
        found |= a == self;
    }
    return found;
}
inline bool ai_march_available(const World& w, Address a, Address b) {
    if (!config::active().modules.tactical_infantry) return false;
    if (!s::autonomous_if(a, true, p::ai_march_allows) || read<Address>(a, 0xDC8) != b || !read<Address>(a, 0xDC0) ||
        !read<Address>(a, 0xDD0))
        return false;
    if (selected(b) || protected_order(b, a)) return false;
    if (!coord::instance().can_act(a, coord::Module::tactical_infantry, coord::Priority::tactical_infantry,
                                   read<unsigned>(a, 0xC0), w.now))
        return false;
    return !auto_medic::occupied(a) && !auto_resupply::occupied(a) && !sim::is_busy(w, a) &&
           !close_grenade::is_busy(w, a) && !tank_auto_repair::is_busy(a);
}
inline bool observe_epoch(Context& c) {
    const Address epoch = s::entity_scene(c.w);
    if (!epoch) return false;
    if (scene != epoch || c.w.now < clock) {
        reset();
        scene = epoch;
    }
    clock = c.w.now;
    c.now = double(clock) / c.w.ticks;
    return true;
}
inline bool context(Context& c, Address b) {
    if (!accepting.load() || !b || !s::serial_thread()) return false;
    c.actor = s::current_actor();
    if (!s::callback(c.actor, c.w) || !runtime::actor_ai_allowed(c.actor) || pr::scope.actor != c.actor ||
        pr::depth != 1 || read<Address>(c.actor, 0xdc8) != b || !available(c.w, c.actor, b))
        return false;
    c.squad = read<Address>(b, 0x100);
    if (!roster_permits(c.squad, c.actor)) return false;
    if (!observe_epoch(c)) return false;
    c.brain = b;
    c.soldier = actor_record(c.actor, b, c.now);
    if (!c.soldier) return false;
    c.sub = target(c.actor, b);
    c.self = read<XY>(c.actor, 0x74);
    return std::isfinite(c.self.x) && std::isfinite(c.self.y);
}
inline bool ai_march_context(Context& c, Address b) {
    if (!accepting.load() || !b || !s::serial_thread()) return false;
    c.actor = s::current_actor();
    if (!s::callback(c.actor, c.w) || !runtime::actor_ai_allowed(c.actor) || pr::scope.actor != c.actor ||
        pr::depth != 1 || read<Address>(c.actor, 0xdc8) != b || !ai_march_available(c.w, c.actor, b))
        return false;
    c.squad = read<Address>(b, 0x100);
    if (!ai_march_roster_permits(c.squad, c.actor)) return false;
    if (!observe_epoch(c)) return false;
    c.brain = b;
    c.soldier = actor_record(c.actor, b, c.now);
    if (!c.soldier) return false;
    c.sub = target(c.actor, b);
    c.self = read<XY>(c.actor, 0x74);
    return std::isfinite(c.self.x) && std::isfinite(c.self.y);
}
inline bool for_state(Context& c, Address state) {
    return accepting.load() && s::serial_thread() && s::current_actor() && state &&
           context(c, read<Address>(state, 0x30));
}
inline bool for_ai_march_state(Context& c, Address state) {
    return accepting.load() && s::serial_thread() && s::current_actor() && state &&
           ai_march_context(c, read<Address>(state, 0x30));
}
inline bool screened_against(Address descriptor, XY enemy_xy, XY point) {
    if (!std::isfinite(enemy_xy.x) || !std::isfinite(enemy_xy.y)) return false;
    return native<Byte (*)(Address, const XY*, const XY*)>(0x8BB590)(descriptor, &enemy_xy, &point) != 0;
}
inline bool screened(const Context& c, Address descriptor, XY point) {
    if (!c.sub) return false;
    return native<Byte (*)(Address, Address, const XY*)>(0x8BB590)(descriptor, c.sub + 0x28, &point) != 0;
}
inline bool screened(const Context& c) {
    return c.soldier->covered && screened(c, read<Address>(c.actor, 0xdc0) + 0x20, c.self);
}
inline p::Sight sight(const Context& c, XY point, float height) {
    if (!c.sub) return p::Sight::unknown;
    auto enemy = read<Address>(c.sub, 0x18);
    sim::Vec3 ground{point.x, point.y, read<float>(c.actor, 0x7c)};
    if ((point.x != c.self.x || point.y != c.self.y) && !s::ground(c.w, ground)) return p::Sight::unknown;
    pr::Vec3 from{point.x, point.y, ground.z + height * c.w.length};
    auto to = pr::raised_point(read<pr::Vec3>(c.sub, 0x28), enemy, pr::RayPoint::target, c.w.length);
    if (!pr::finite(from) || !pr::finite(to) || !(pr::distance(from, to) > 0)) return p::Sight::unknown;
    bool evaluated = false;
    if (pr::segment({c.sub, enemy, 0, 0, from, to, false}, &evaluated)) return p::Sight::open;
    return evaluated ? p::Sight::blocked : p::Sight::unknown;
}
inline float height(int stance) {
    return (stance == 1 || stance == 3) ? .35f : stance == 2 ? .9f : 1.35f;
}
inline int stance(const Context& c) {
    return read<int>(read<Address>(c.actor, 0xde0), 0x2c);
}
inline p::Sight current_sight(Context& c) {
    auto& row = *c.soldier;
    if (row.sight_target != c.sub || row.sight_at != c.now) {
        row.visibility = sight(c, c.self, height(stance(c)));
        row.sight_at = c.now;
        row.sight_target = c.sub;
    }
    return row.visibility;
}
inline bool on_foot(Address actor) {
    const auto parent = read<Address>(read<Address>(actor, 0x560), 8);
    return !(read<unsigned>(actor, 0x1c0) & (1u << 28)) && !(parent && (read<Byte>(parent, 0x30) & 8));
}
inline bool live_squad_human(Address actor, Address brain, Address squad, bool allow_seated = false) {
    const bool alive_flags = allow_seated && !on_foot(actor) ? (actor && (read<Byte>(actor, 0x30) & 3) == 3 &&
                                                                !(read<unsigned>(actor, 0x38) & 0x10820u) &&
                                                                read<unsigned>(actor, 0xc0) != 0xffffffffu)
                                                             : (s::live_entity(actor) && sim::has_actor(actor));
    if (!alive_flags || !(read<Byte>(actor, 0x30) & 4) || !(read<unsigned>(actor, 0xdb4) & 0x100) ||
        read<Address>(brain) != s::base + runtime::table().vt.human_brain || read<Address>(brain, 0x38) != actor ||
        read<Address>(brain, 0x100) != squad)
        return false;
    const auto vit = read<Address>(read<Address>(actor, 0x578), 0x18);
    return vit && !(read<unsigned>(vit, 0x174) & 3);
}
inline std::size_t find_gunners(const Context& c, p::Roster& guns) {
    const auto list = s::span(c.squad, 0x80);
    if (!list.valid(8, 64)) return 0;
    std::size_t count = 0;
    for (auto it = list.first; it != list.end; it += 8) {
        const auto actor = read<Address>(it);
        if (!s::live_entity(actor) || !sim::has_actor(actor)) continue;
        const auto brain = read<Address>(actor, 0xdc8);
        if (!live_squad_human(actor, brain, c.squad) || !on_foot(actor)) continue;
        auto* row = actor_record(actor, brain, c.now);
        if (!row) continue;
        load_role(*row, c.now);
        if (row->gun_def) guns[count++] = {actor, row->id, row->role, read<XY>(actor, 0x74), row->gun_def};
    }
    return count;
}
inline void assign_assistant(Context& c, ActorState& row, p::Member& mate, const p::Roster& guns, std::size_t count) {
    if (mate.role == p::Role::heavy) return;
    const p::Member* paired = nullptr;
    if (row.pair_squad == c.squad && c.now - row.pair_at < 1.) {
        if (!row.paired_gunner) return;
        for (std::size_t i = 0; i < count; ++i)
            if (guns[i].actor == row.paired_gunner && guns[i].id == row.gunner_id && guns[i].gun_def == row.paired_def)
                paired = &guns[i];
    }
    if (!paired) {
        paired = p::match_gunner(mate, guns, count,
                                 [](Address actor, Address def) { return s::loose_ammo(actor, def).total > 0; });
        row.paired_gunner = paired ? paired->actor : 0;
        row.gunner_id = paired ? paired->id : 0;
        row.paired_def = paired ? paired->gun_def : 0;
        row.pair_at = c.now;
        row.pair_squad = c.squad;
    }
    if (paired) {
        mate.role = p::Role::heavy;
        mate.paired_gunner = paired->actor;
        if (mate.actor == c.actor) {
            c.gunner = paired->actor;
            c.gunner_pos = paired->position;
        }
    }
}
inline void coordinate(Context& c) {
    if (!c.squad || c.count) return;
    auto roster = s::span(c.squad, 0x80);
    if (!roster.valid(8, 64)) return;
    unsigned contacts = 0, front = 0, seen = 0, covered_count = 0, open_fire_count = 0;
    bool armor_near = false;
    float a = 0, b = 0, morale = 0, ta = 0, tr = 0, l = 0, r = 0;
    std::array<ActorState*, 64> records{};
    for (auto it = roster.first; it != roster.end; it += 8) {
        Address actor = read<Address>(it);
        if (!s::live_entity(actor)) continue;
        Address brain = read<Address>(actor, 0xdc8);
        if (!brain || !available(c.w, actor, brain)) continue;
        auto bal = balance(brain);
        if (!bal.valid()) continue;
        auto* row = actor_record(actor, brain, c.now);
        if (!row) continue;
        load_role(*row, c.now);
        auto pos = read<XY>(actor, 0x74);
        if (!std::isfinite(pos.x) || !std::isfinite(pos.y)) continue;
        c.members[c.count] = {actor, row->id, row->role, pos};
        records[c.count++] = row;
        if (row->covered) ++covered_count;
        c.center = c.center + pos;
        a += bal.a;
        b += bal.b;
        morale += bal.morale;
        ta += bal.a * bal.advance_ratio;
        tr += bal.b * bal.retreat_ratio;
        Address sub = target(actor, brain);
        if (!sub) continue;
        const XY tpos = read<XY>(sub, 0x28);
        c.threat = c.threat + tpos;
        ++contacts;
        const Address tgt_actor = read<Address>(sub, 0x18);
        if (sim::has_actor(tgt_actor) &&
            classify::is_armored_vehicle_class(s::native_string(tgt_actor + 0xde8 + 0x40)) &&
            !s::has_destruction_tag(tgt_actor) && p::meters_between(pos, tpos, c.w.length) <= 65.f)
            armor_near = true;
        if (c.now - row->sight_at <= 2 && row->sight_target == sub && row->visibility != p::Sight::unknown) {
            ++seen;
            if (row->visibility == p::Sight::open) {
                ++front;
                ++open_fire_count;
            }
        }
    }
    if (config::active().modules.weapon_team_cohesion && c.count) {
        p::Roster guns{};
        const auto gun_count = find_gunners(c, guns);
        for (std::size_t i = 0; i < c.count; ++i)
            assign_assistant(c, *records[i], c.members[i], guns, gun_count);
    }
    auto* team = c.count >= 2 ? record(squads, c.squad, c.now) : nullptr;
    if (team) team->observe_contact(c.now, contacts > 0);
    if (!contacts || c.count < 2 || !team) return;
    c.center = c.center * (1.f / c.count);
    c.threat = c.threat * (1.f / contacts);
    // Also check self sensor list for any live enemy armored vehicle within 65 m of squad center.
    if (!armor_near) {
        const auto subs = pr::subjects(c.actor);
        if (subs.valid(8)) {
            for (auto psub = subs.first; psub != subs.end; psub += 8) {
                const auto sub = read<Address>(psub);
                if (!pr::hostile_observation(sub) || read<Byte>(sub, 0x6c)) continue;
                const auto ent = read<Address>(sub, 0x18);
                if (!sim::has_actor(ent) || !classify::is_armored_vehicle_class(s::native_string(ent + 0xde8 + 0x40)) ||
                    s::has_destruction_tag(ent))
                    continue;
                if (p::meters_between(c.center, read<XY>(sub, 0x28), c.w.length) <= 65.f) {
                    armor_near = true;
                    break;
                }
            }
        }
    }
    XY lateral = p::left(p::unit(c.threat - c.center));
    for (std::size_t i = 0; i < c.count; ++i) {
        float side = p::dot(c.members[i].position - c.center, lateral) / c.w.length;
        float value = (records[i]->covered ? 2.f : 0.f) +
                      (c.now - records[i]->sight_at <= 2 && records[i]->visibility == p::Sight::open ? 3.f : 0.f);
        if (side > 0)
            l += value;
        else if (side < 0)
            r += value;
    }
    auto desired = p::Mode::hold;
    const float enemy_dist_m = p::meters_between(c.center, c.threat, c.w.length);
    const bool wedge_ready = (b >= 1.05f * ta) || (seen >= 2 && front * 2 < seen);
    if (a > 1.15f * tr || morale < 48 * c.count)
        desired = p::Mode::fallback;
    else if (b > 1.15f * ta && morale > 68 * c.count)
        desired = seen && front * 2 < seen ? p::Mode::left : p::Mode::bound;
    else if (config::active().modules.flanking_wedge &&
             p::should_wedge(c.count, covered_count, open_fire_count ? open_fire_count : contacts, armor_near,
                             morale / c.count, enemy_dist_m, team->contact_duration(c.now), wedge_ready))
        desired = p::Mode::wedge_left;
    auto active = p::is_wedge(team->mode)
                      ? p::wedge_elements(c.members, c.count, c.center, c.threat, p::wedge_sign(team->mode))
                      : p::bounders(c.members, c.count, team->phase, team->cycle);
    bool arrived = true, any = false;
    for (std::size_t i = 0; i < c.count; ++i)
        if (active[i]) {
            any = true;
            auto& row = *records[i];
            arrived &= row.issued_squad == c.squad && row.issued_phase == team->phase_at &&
                       p::bound_arrived(c.members[i].position, row.goal, c.w.length, row.issued_cover, row.covered);
        }
    team->update(c.now, desired, l, r, any && arrived);
    c.team = team;
    active = p::is_wedge(team->mode)
                 ? p::wedge_elements(c.members, c.count, c.center, c.threat, p::wedge_sign(team->mode))
                 : p::bounders(c.members, c.count, team->phase, team->cycle);
    unsigned supporting = 0;
    for (std::size_t i = 0; i < c.count; ++i) {
        if (!active[i]) {
            c.support = c.support + c.members[i].position;
            ++supporting;
        }
        if (c.members[i].actor == c.actor) {
            c.moving_element = active[i] && p::advancing(team->mode);
            c.wedge_element = active[i] && p::is_wedge(team->mode);
        }
    }
    c.support = supporting ? c.support * (1.f / supporting) : c.center;
}
inline bool remembered(Address sub) {
    return sub && (read<unsigned>(sub, 0x10) & (1u << 28)) && (read<unsigned>(sub, 0x14) & 0x16u) == 2 &&
           !read<Byte>(sub, 0x6c);
}
inline XY away(const Context& c) {
    auto list = pr::subjects(c.actor);
    XY sum{};
    if (list.valid(8))
        for (auto it = list.first; it != list.end; it += 8) {
            auto sub = read<Address>(it);
            if (!remembered(sub)) continue;
            unsigned flags = read<unsigned>(sub, 0x14);
            float age = pr::is_visible(sub) ? 0.f : (c.w.now - read<Tick>(sub, 0x64)) / c.w.ticks;
            sum = sum + p::threat_term(c.self - read<XY>(sub, 0x28), read<float>(sub, 0x48), age, flags & 0x100,
                                       flags & 0x80, c.w.length);
        }
    if (p::squared(sum) == 0 && c.sub) sum = c.self - read<XY>(c.sub, 0x28);
    return p::withdraw(sum, c.team ? c.center - c.self : XY{});
}
inline float spacing(const Context& c, XY point) {
    float d = claims.nearest(scene, c.squad, c.actor, point, c.now);
    if (c.squad) {
        const auto list = s::span(c.squad, 0x80);
        if (list.valid(8, 64))
            for (auto it = list.first; it != list.end; it += 8) {
                const auto a = read<Address>(it);
                if (a == c.actor || !s::live_entity(a)) continue;
                d = (std::min)(d, p::squared(point - read<XY>(a, 0x74)));
                const auto cover = read<Address>(a, 0xdc0);
                if (cover && read<int>(cover, 0x24)) d = (std::min)(d, p::squared(point - read<XY>(cover, 0x30)));
            }
    }
    return std::sqrt(d) / c.w.length;
}
struct Descriptor {
    alignas(8) std::array<std::byte, 0x88> data{};
    Address ptr() const {
        return reinterpret_cast<Address>(data.data());
    }
    XY xy() const {
        return read<XY>(ptr(), 0x10);
    }
};
static_assert(sizeof(Descriptor) == 0x88);
struct DirectionValidator {
    Address vtable;
    XY origin;
    float minimum;
    XY direction;
};
static_assert(sizeof(DirectionValidator) == 0x20 && offsetof(DirectionValidator, direction) == 0x14);
struct Search {
    DirectionValidator validator{};
    alignas(16) std::array<std::byte, 0x1a0> data{};
    Address ptr() {
        return reinterpret_cast<Address>(data.data());
    }
    Search(const Context& c, Address state, XY goal, float radius) {
        native<Address (*)(Address, Address, const XY*, float)>(0x8B54A0)(ptr(), c.actor, &goal, radius * c.w.length);
        if (state) native<Byte (*)(Address, Address)>(0x96A060)(state, ptr() + 0x150);
    }
    void directed(const Context& c, XY direction, float minimum, bool retreat) {
        validator = {s::base + 0x1110AD8, c.self, minimum * c.w.length, direction};
        put(ptr(), 0x168, reinterpret_cast<Address>(&validator));
        put(ptr(), 0x125, Byte{1});
        put(ptr(), 0x12a, static_cast<Byte>(stance(c) == 1));
        native<void (*)(Address, float)>(0x8BB160)(ptr() + 0x170, 0.f);
        for (unsigned offset : {0x170u, 0x178u, retreat ? 0x17cu : 0x180u, retreat ? 0x184u : 0x174u})
            put(ptr(), offset, 10.f);
    }
    ~Search() {
        native<void (*)(Address)>(0xBF640)(ptr() + 0x18);
        pr::free_buffer(read<Address>(ptr()), read<Address>(ptr(), 0x10), sizeof(Descriptor));
    }
    Search(const Search&) = delete;
    Search& operator=(const Search&) = delete;
};
enum class Purpose {
    native,
    bound,
    retreat,
    peek,
    update,
    overflow,
    spread,
    assist,
    disperse_cover,
    march_cover,
    wedge
};
inline bool withdrawing(const Context& c) {
    return !read<Byte>(c.brain, 0x521) && read<int>(c.brain, 0x52c) <= p::fallback;
}
inline XY forward(const Context& c) {
    if (c.team) return p::axis(c.team->mode, c.threat - c.center);
    return p::unit(read<XY>(c.sub, 0x28) - c.self);
}
inline bool lagging(const Context& c) {
    if (c.gunner) return p::meters_between(c.self, c.gunner_pos, c.w.length) > 10.f;
    const float max_leash = c.wedge_element ? 38.f : 28.f;
    return c.team && p::squared(c.self - c.center) > max_leash * max_leash * c.w.length * c.w.length;
}
inline bool within_team(const Context& c, XY point) {
    if (c.gunner && !p::assistant_cohesive(c.self, point, c.gunner_pos, c.w.length)) return false;
    if (!c.team) return true;
    if (c.wedge_element) return p::wedge_cohesive(c.self, point, c.center, c.support, c.threat, c.w.length);
    return p::cohesive(c.self, point, c.center, c.support, c.w.length);
}
inline float alignment(const Context& c, XY point) {
    if (!c.team || (c.team->mode != p::Mode::left && c.team->mode != p::Mode::right)) return 0;
    return p::dot(p::unit(point - c.self), forward(c));
}
struct Choice {
    Descriptor spot{};
    float score{-std::numeric_limits<float>::infinity()};
};
inline float placement_spacing(const Context& c, XY point) {
    return spacing(c, point);
}
inline float placement_score(const Context& c, Purpose purpose, XY point, float native_score) {
    const float spacing_m = placement_spacing(c, point);
    if (purpose == Purpose::overflow)
        return p::score_overflow_spot(native_score, point, c.soldier->overflow_anchor, c.w.length,
                                      config::active().tuning.overflow_cover_radius_m, s::hold_position(c.brain),
                                      spacing_m);
    const float d = p::meters_between(point, c.placement_center, c.w.length);
    if (!(spacing_m >= 1.5f)) return -std::numeric_limits<float>::infinity();
    if (purpose == Purpose::assist) {
        if (!(d >= 2.5f && d <= 8.f)) return -std::numeric_limits<float>::infinity();
        return native_score + p::assistant_anchor_bonus(point, c.placement_center, c.w.length) -
               p::crowding_penalty(spacing_m) * .5f - p::meters_between(point, c.self, c.w.length) * .5f;
    }
    if (!(d >= 3.5f && d <= 8.5f) || p::dot(point - c.self, c.spread_away) < 0)
        return -std::numeric_limits<float>::infinity();
    return native_score - p::crowding_penalty(spacing_m) + (std::min)(spacing_m, 8.f) * .5f;
}
template <class ScreenedFn, class SightFn>
inline bool select_screened_cover(Context& c, s::Span list, Purpose purpose, Choice& best, ScreenedFn is_screened,
                                  SightFn probe_sight) {
    if (!list.valid(sizeof(Descriptor), 65536)) return false;
    struct Candidate {
        Descriptor desc{};
        XY point{};
        float native_score{};
        float spacing_m{};
        float dist_m{};
        float probe_height{};
        float base_val{-std::numeric_limits<float>::infinity()};
        p::Sight visibility{p::Sight::unknown};
        bool probed{};
    };
    std::array<Candidate, p::CoverSelection::limit> pool{};
    unsigned count = 0;
    const bool is_march = purpose == Purpose::march_cover;
    for (auto it = list.first; it != list.end && count < p::CoverSelection::limit; it += sizeof(Descriptor)) {
        const auto desc = read<Descriptor>(it);
        if (!read<int>(desc.ptr(), 4)) continue;
        const XY pt = desc.xy();
        const float dist_m = p::meters_between(pt, c.self, c.w.length);
        if (!std::isfinite(dist_m)) continue;
        if (is_march) {
            if (dist_m > p::kMarchCoverRadiusM) continue;
        } else {
            if (dist_m < 2.f || dist_m > 10.f || !within_team(c, pt) || !p::dispersion_step_safe(c.self, pt, c.threat))
                continue;
        }
        const float sp = spacing(c, pt);
        if (is_march) {
            if (!(sp >= 1.5f)) continue;
        } else if (!p::spacing_improved(c.clump_spacing, sp, true)) {
            continue;
        }
        if (!is_screened(desc.ptr(), c.threat, pt)) continue;
        const float nat = read<float>(desc.ptr(), 0x80);
        float base_val = is_march ? p::score_march_cover(nat, true, sp, dist_m)
                                  : p::score_disperse_cover(nat, p::Sight::unknown, sp, dist_m, true);
        if (!std::isfinite(base_val)) continue;
        if (c.gunner && p::assistant_cohesive(c.self, pt, c.gunner_pos, c.w.length))
            base_val += p::assistant_anchor_bonus(pt, c.gunner_pos, c.w.length);
        const float ph = is_march ? 0.f : p::cover_height(read<int>(desc.ptr(), 8), read<int>(desc.ptr(), 0xc), false);
        pool[count++] = {desc, pt, nat, sp, dist_m, ph, base_val, p::Sight::unknown, false};
    }
    if (!count) return false;
    if (!is_march && c.sub) {
        // Spend at most 4 firing-LOF ray probes on the highest base-scored screened cover candidates.
        for (unsigned step = 0; step < 4; ++step) {
            unsigned pick = count;
            float top = -std::numeric_limits<float>::infinity();
            for (unsigned i = 0; i < count; ++i)
                if (!pool[i].probed && pool[i].probe_height > 0.f && pool[i].base_val > top) {
                    top = pool[i].base_val;
                    pick = i;
                }
            if (pick == count) break;
            pool[pick].probed = true;
            pool[pick].visibility = probe_sight(pool[pick].point, pool[pick].probe_height);
        }
    }
    unsigned winner = count;
    float winner_score = -std::numeric_limits<float>::infinity();
    for (unsigned i = 0; i < count; ++i) {
        float val = pool[i].base_val;
        if (!is_march && pool[i].visibility == p::Sight::open) val += 10.f;
        if (std::isfinite(val) && val > winner_score) {
            winner_score = val;
            winner = i;
        }
    }
    if (winner == count) return false;
    best = {pool[winner].desc, winner_score};
    return true;
}
inline bool select_cover(Context& c, s::Span list, Purpose purpose, Choice& best) {
    if (!list.valid(sizeof(Descriptor), 65536)) return false;
    if (purpose == Purpose::overflow || purpose == Purpose::spread || purpose == Purpose::assist) {
        // Occupied trench slots can fill the entire head of the sorted result vector.
        unsigned eligible = 0;
        for (auto it = list.first; it != list.end && eligible < p::CoverSelection::limit; it += sizeof(Descriptor)) {
            const auto d = read<Descriptor>(it);
            if (!read<int>(d.ptr(), 4)) continue;
            const float score = placement_score(c, purpose, d.xy(), read<float>(d.ptr(), 0x80));
            if (!std::isfinite(score)) continue;
            ++eligible;
            if (score > best.score) best = {d, score};
        }
        return eligible != 0;
    }
    if (purpose == Purpose::march_cover || purpose == Purpose::disperse_cover) {
        return select_screened_cover(
            c, list, purpose, best, [](Address desc, XY enemy, XY pt) { return screened_against(desc, enemy, pt); },
            [&](XY pt, float h) { return sight(c, pt, h); });
    }
    if (purpose == Purpose::wedge && c.team && p::is_wedge(c.team->mode)) {
        const float flank_sign = p::wedge_sign(c.team->mode);
        const float angle_before = p::wedge_flank_angle_deg(c.self, c.center, c.threat, flank_sign);
        const float enemy_dist_before = p::meters_between(c.self, c.threat, c.w.length);
        const XY wdir = p::wedge_direction(c.self, c.center, c.threat, flank_sign, c.w.length);
        struct WedgeCand {
            Descriptor desc{};
            XY point{};
            float native_score{}, spacing_m{}, dist_m{}, angle_after{}, enemy_dist_after{}, progress_m{},
                probe_height{};
            float base_val{-std::numeric_limits<float>::infinity()};
            p::Sight visibility{p::Sight::unknown};
            bool probed{};
        };
        std::array<WedgeCand, p::CoverSelection::limit> pool{};
        unsigned count = 0;
        for (auto it = list.first; it != list.end && count < p::CoverSelection::limit; it += sizeof(Descriptor)) {
            const auto d = read<Descriptor>(it);
            if (!read<int>(d.ptr(), 4)) continue;
            const XY pt = d.xy();
            const float dist_m = p::meters_between(pt, c.self, c.w.length);
            if (!std::isfinite(dist_m) || !within_team(c, pt)) continue;
            const bool scr = screened(c, d.ptr(), pt);
            if (!scr) continue;
            const float sp = spacing(c, pt);
            const float angle_after = p::wedge_flank_angle_deg(pt, c.center, c.threat, flank_sign);
            const float enemy_dist_after = p::meters_between(pt, c.threat, c.w.length);
            const float progress_m = p::dot(pt - c.self, wdir) / c.w.length;
            const float nat = read<float>(d.ptr(), 0x80);
            const float base_val = p::score_wedge_cover(nat, p::Sight::unknown, sp, dist_m, angle_before, angle_after,
                                                        enemy_dist_before, enemy_dist_after, progress_m, true);
            if (!std::isfinite(base_val)) continue;
            const float ph = p::cover_height(read<int>(d.ptr(), 8), read<int>(d.ptr(), 0xc), false);
            pool[count++] = {
                d,    pt, nat, sp, dist_m, angle_after, enemy_dist_after, progress_m, ph, base_val, p::Sight::unknown,
                false};
        }
        if (!count) return false;
        p::CoverSelection selection;
        for (unsigned step = 0; step < 6; ++step) {
            unsigned pick = count;
            float top = -std::numeric_limits<float>::infinity();
            for (unsigned i = 0; i < count; ++i)
                if (!pool[i].probed && pool[i].probe_height > 0.f && pool[i].base_val > top) {
                    top = pool[i].base_val;
                    pick = i;
                }
            if (pick == count) break;
            pool[pick].probed = true;
            pool[pick].visibility =
                selection.probe([&] { return sight(c, pool[pick].point, pool[pick].probe_height); });
        }
        for (unsigned i = 0; i < count; ++i) {
            const auto& cand = pool[i];
            const float val =
                p::score_wedge_cover(cand.native_score, cand.visibility, cand.spacing_m, cand.dist_m, angle_before,
                                     cand.angle_after, enemy_dist_before, cand.enemy_dist_after, cand.progress_m, true);
            selection.consider(i, val);
        }
        if (selection.winner == p::CoverSelection::limit) return false;
        best = {pool[selection.winner].desc, selection.score};
        return true;
    }
    const bool retreat = purpose == Purpose::retreat || (purpose != Purpose::peek && withdrawing(c));
    const bool directed = purpose == Purpose::bound || purpose == Purpose::retreat;
    const bool check_screen = directed || purpose == Purpose::update || purpose == Purpose::native;
    struct Candidate {
        unsigned index{};
        Descriptor desc{};
        XY point{};
        float native_score{};
        float align_val{};
        float spacing_m{};
        float assist_bonus{};
        float probe_height{};
        float base_val{-std::numeric_limits<float>::infinity()};
        p::Sight visibility{p::Sight::unknown};
        bool is_screened{};
        bool probed{};
    };
    std::array<Candidate, p::CoverSelection::limit> pool{};
    unsigned count = 0;
    for (unsigned i = 0; i < p::CoverSelection::limit && list.first + i * sizeof(Descriptor) < list.end; ++i) {
        const auto d = read<Descriptor>(list.first + i * sizeof(Descriptor));
        const XY point = d.xy();
        const float distance = std::hypot(point.x - c.self.x, point.y - c.self.y) / c.w.length;
        if (!std::isfinite(distance) || !within_team(c, point)) continue;
        if (directed && !p::bound_range(purpose == Purpose::retreat).contains(distance)) continue;
        const float nat = read<float>(d.ptr(), 0x80);
        const float align_val = alignment(c, point);
        const float sp = spacing(c, point);
        const bool scr = check_screen && screened(c, d.ptr(), point);
        const float assist_bonus = c.gunner ? p::assistant_anchor_bonus(point, c.gunner_pos, c.w.length) : 0.f;
        const float base_val = p::cover_score(nat, p::Sight::unknown, retreat, align_val, sp, scr) + assist_bonus;
        const float ph = p::cover_height(read<int>(d.ptr(), 8), read<int>(d.ptr(), 0xc), retreat);
        pool[count++] = {i, d, point, nat, align_val, sp, assist_bonus, ph, base_val, p::Sight::unknown, scr, false};
    }
    p::CoverSelection selection;
    // Prioritize the 6 LOF ray probes for well-spaced, screened candidates instead of crowded head slots.
    for (unsigned step = 0; step < 6; ++step) {
        unsigned pick = count;
        float top = -std::numeric_limits<float>::infinity();
        for (unsigned i = 0; i < count; ++i)
            if (!pool[i].probed && pool[i].probe_height > 0.f && pool[i].base_val > top) {
                top = pool[i].base_val;
                pick = i;
            }
        if (pick == count) break;
        pool[pick].probed = true;
        pool[pick].visibility = selection.probe([&] { return sight(c, pool[pick].point, pool[pick].probe_height); });
    }
    for (unsigned i = 0; i < count; ++i) {
        const auto& cand = pool[i];
        if (purpose == Purpose::peek && cand.visibility != p::Sight::open) continue;
        const float value = p::cover_score(cand.native_score, cand.visibility, retreat, cand.align_val, cand.spacing_m,
                                           cand.is_screened) +
                            cand.assist_bonus;
        selection.consider(cand.index, value);
    }
    if (selection.winner == p::CoverSelection::limit) return false;
    best = {read<Descriptor>(list.first + selection.winner * sizeof(Descriptor)), selection.score};
    return true;
}
inline bool choose(Context& c, Address query, Purpose purpose, Choice& best) {
    return native<s::Test>(0x8B57F0)(query) && select_cover(c, s::span(query, 0), purpose, best);
}
inline void claim(const Context& c, XY goal) {
    claims.claim({scene, c.actor, c.squad, c.soldier->id, goal, c.now + 12}, c.now);
}
inline void record_bound(Context& c, const Descriptor* spot, const XY* point) {
    ++c.team->issued_bounds;
    c.soldier->goal = spot ? spot->xy() : *point;
    c.soldier->issued_cover = spot != nullptr;
    c.soldier->issued_squad = c.squad;
    c.soldier->issued_phase = c.team->phase_at;
}
// Build the children before the native state helper transfers the new complex to brain slot 13.
inline bool issue(Context& c, Address state, const Descriptor* spot, const XY* point, int type, int action,
                  bool squad_bound = false, Address* issued_order = nullptr) {
    if (!s::free_slot(c.actor) || !available(c.w, c.actor, c.brain) || !roster_permits(c.squad, c.actor)) return false;
    auto change = native<Address (*)(int, int, Byte)>(0x90ABF0)(type, spot ? 4 : point ? 5 : -1, 1);
    auto move = spot    ? native<Address (*)(const Descriptor*)>(0x8E2770)(spot)
                : point ? native<Address (*)(const XY*, int)>(0x90A8C0)(point, 0)
                        : 0;
    auto notice = native<Address (*)(Address, int, Byte)>(0x92AE10)(state, 0x10, 1);
    Address seq = change && (move || (!spot && !point)) && notice ? native<s::Unary>(0x92ABB0)(state) : 0;
    if (!seq) {
        s::discard(notice);
        s::discard(move);
        s::discard(change);
        return false;
    }
    native<void (*)(Address, Byte, int)>(0x96CF70)(state, spot != nullptr, action);
    auto push = native<Address (*)(Address, Address, int, Byte)>(0x919270);
    push(seq, change, 0, 1);
    if (move) push(seq, move, 0, 1);
    push(seq, notice, 0, 1);
    if (move) {
        if (squad_bound) record_bound(c, spot, point);
        claim(c, spot ? spot->xy() : *point);
    }
    if (issued_order) *issued_order = seq;
    const unsigned count = issued.fetch_add(1, std::memory_order_relaxed) + 1;
    if ((count <= 4 || count % 256 == 0) && s::log_sink)
        s::log_sink("Tactical infantry order #%u actor=%u action=%d", count, c.soldier->id, action);
    return true;
}
inline bool armored_class(Address actor) {
    return classify::is_armored_vehicle_class(s::native_string(actor + 0xde8 + 0x40));
}
inline bool held_effective_vs_target(Address actor, Address sub) {
    const Address enemy = sub ? read<Address>(sub, 0x18) : 0;
    if (!sim::has_actor(enemy) || !armored_class(enemy)) return true;
    const Address hand = pr::hand_item(actor);
    if (!hand) return false;
    const Address def = read<Address>(hand, 0x40);
    return def &&
           (close_grenade::holds_ranged_at(actor) || close_grenade::payload(def) != close_grenade::Payload::none);
}
inline bool visible_soft_target(const Context& c) {
    const auto list = s::span(read<Address>(c.actor, 0xdd0), 0x280);
    if (!list.valid(sizeof(Address), 4096)) return false;
    const float radius = 45.f * c.w.length;
    for (auto it = list.first; it != list.end; it += sizeof(Address)) {
        const auto sub = read<Address>(it);
        if (sub == c.sub || !pr::hostile_observation(sub) || read<Byte>(sub, 0x6c) ||
            !(p::squared(read<XY>(sub, 0x28) - c.self) <= radius * radius))
            continue;
        const auto enemy = read<Address>(sub, 0x18);
        if (sim::has_actor(enemy) && !armored_class(enemy)) return true;
    }
    return false;
}
inline bool within_closest_rushers(const Context& c, Address target_ent, XY target_xy) {
    if (!c.squad || close_grenade::is_rushing(c.w, c.actor)) return true;
    const auto roster = s::span(c.squad, 0x80);
    if (!roster.valid(8, 64)) return true;
    const float own_d2 = p::squared(c.self - target_xy);
    unsigned closer = 0;
    for (auto it = roster.first; it != roster.end; it += 8) {
        const Address mate = read<Address>(it);
        if (mate == c.actor || !s::live_entity(mate)) continue;
        const Address mb = read<Address>(mate, 0xdc8);
        if (!mb || !available(c.w, mate, mb) || close_grenade::holds_ranged_at(mate) ||
            close_grenade::in_backoff(c.w, mate, target_ent))
            continue;
        auto* row = actor_record(mate, mb, c.now);
        if (!row) continue;
        load_role(*row, c.now);
        if (!row->at_throwable) continue;
        const float d2 = p::squared(read<XY>(mate, 0x74) - target_xy);
        if (d2 < own_d2 || (d2 == own_d2 && mate < c.actor)) {
            if (++closer >= 2) return false;
        }
    }
    return true;
}
inline bool at_rush(const Context& c, close_grenade::Choice& rush) {
    if (c.gunner) return false;
    const auto bal = balance(c.brain);
    if (!bal.valid() || !p::at_rush_allowed(bal.morale, bal.no_advance)) return false;
    if (!close_grenade::find_rush_target(c.w, c.actor, c.brain, rush) || rush.in_range) return false;
    if (p::at_rush_standoff(rush.max_range, rush.radius, c.w.length) <= 0.f) return false;
    return within_closest_rushers(c, rush.target, {rush.aim.point.x, rush.aim.point.y});
}
inline bool issue_at_rush(Context& c, Address state, const close_grenade::Choice& rush) {
    if (!s::free_slot(c.actor) || !available(c.w, c.actor, c.brain) || !roster_permits(c.squad, c.actor)) return false;
    const float standoff = p::at_rush_standoff(rush.max_range, rush.radius, c.w.length);
    if (!(standoff > 0.f)) return false;
    const XY aim_xy{rush.aim.point.x, rush.aim.point.y};
    const XY away_dir = p::unit(c.self - aim_xy);
    if (p::squared(away_dir) < .5f) return false;
    c.soldier->next_search = c.now + .5;
    s::PathContext path(c.actor);
    const sim::Vec3 self_pos = s::pos(c.actor);
    XY chosen{}, fallback_corridor{}, fallback_ground{};
    bool has_chosen = false, has_corridor = false, has_ground = false;
    for (float deg : {0.f, 25.f, -25.f, 45.f, -45.f}) {
        const XY cand = aim_xy + p::rotate(away_dir, deg) * standoff;
        sim::Vec3 pt{cand.x, cand.y, 0};
        if (!s::ground(c.w, pt)) continue;
        if (!has_ground) {
            fallback_ground = cand;
            has_ground = true;
        }
        if (s::corridor(c.w, path, self_pos, pt)) {
            if (spacing(c, cand) >= 1.5f) {
                chosen = cand;
                has_chosen = true;
                break;
            }
            if (!has_corridor) {
                fallback_corridor = cand;
                has_corridor = true;
            }
        }
    }
    if (!has_chosen && has_corridor) {
        chosen = fallback_corridor;
        has_chosen = true;
    }
    if (!has_chosen) {
        sim::Vec3 detoured{};
        const sim::Vec3 target_vec{rush.aim.point.x, rush.aim.point.y, rush.aim.point.z};
        if (s::approach(c.w, c.actor, target_vec, standoff, detoured)) {
            chosen = {detoured.x, detoured.y};
            has_chosen = true;
        } else if (has_ground) {
            chosen = fallback_ground;
            has_chosen = true;
        }
    }
    if (!has_chosen) return false;
    Address seq = 0;
    // Standing sprint (type=0, mode=5 via point move, action=2 advance) directly into throw envelope.
    if (!issue(c, state, nullptr, &chosen, 0, 2, false, &seq)) return false;
    c.soldier->rush_active = true;
    c.soldier->fire_until = p::never;
    close_grenade::note_rush(c.w, rush.target, c.actor, seq);
    return true;
}
inline bool hold_shot(Context& c) {
    load_role(*c.soldier, c.now);
    float range = read<float>(c.brain, 0x14c);
    auto place = pr::selected_place(c.actor);
    if (place) range = native<float (*)(Address)>(0x81F660)(place);
    float distance = pr::distance(read<pr::Vec3>(c.sub, 0x28), read<pr::Vec3>(c.actor, 0x74));
    return p::fire_first(distance, range, c.soldier->smg, current_sight(c) == p::Sight::open, c.soldier->covered,
                         c.soldier->role == p::Role::heavy || c.gunner || (c.team && !c.moving_element),
                         c.now < c.soldier->fire_until, held_effective_vs_target(c.actor, c.sub));
}
inline bool placement_ready(const Context& c, bool overflow) {
    return on_foot(c.actor) && s::free_slot(c.actor) && movement_available(c.w, c.actor, c.brain, overflow) &&
           movement_roster_permits(c.squad, c.actor, overflow) && !close_grenade::is_busy(c.w, c.actor);
}
inline bool clump_ready(const Context& c) {
    if (!on_foot(c.actor) || !s::free_slot(c.actor) || !movement_available(c.w, c.actor, c.brain, false) ||
        close_grenade::is_busy(c.w, c.actor))
        return false;
    const bool ai_unit = (read<unsigned>(c.brain, 0x398) & 3u) == 0u;
    return ai_unit ? ai_march_roster_permits(c.squad, c.actor) : movement_roster_permits(c.squad, c.actor, false);
}
inline bool losing_morale(const World& w, Address brain) {
    const auto recovery = read<Tick>(brain, 0x478);
    if (!recovery) return false;
    const auto remaining = recovery - w.now;
    return remaining > w.duration(7.f) && remaining <= w.duration(10.f);
}
inline bool ordinary_order(Address actor, Address o) {
    return o && read<Address>(o, 8) == actor + 0xaf0 && !(read<unsigned>(o, 0x18) & (1u << 23)) &&
           !read<Address>(o, 0x10) && !s::tracked(o);
}
// Inspect whether brain+0xF0 is either free or an unlistened, untracked march leg
// (eMove / eMoveTask / eAdvance, standalone or front of eComplex) safe to replace on AI units.
inline void march_order_state(Address actor, Address brain, bool& active_march, bool& replaceable) {
    active_march = false;
    replaceable = false;
    if ((read<unsigned>(brain, 0x39c) & (1u << 24)) || read<int>(brain, 0x88) != 0 ||
        read<Address>(brain, 0x98) != read<Address>(brain, 0xa0))
        return;
    if (s::free_slot(actor)) {
        replaceable = true;
        return;
    }
    const auto root = read<Address>(brain, 0xf0);
    if (!ordinary_order(actor, root)) return;
    Address o = root;
    if (read<Address>(root) == s::base + 0x110D490) {
        // Require a single-item eComplex so replacing brain+0xF0 never drops queued tail orders.
        if (read<std::size_t>(root, 0x48) != 1 || (read<unsigned>(root, 0x20) & 7u) || read<Address>(root, 0x50))
            return;
        o = complex_front(root);
        if (!ordinary_order(actor, o)) return;
    }
    const auto vt = read<Address>(o);
    const auto& tbl = runtime::table().vt;
    const bool is_move = vt == s::base + tbl.move_order;
    const bool is_move_task = vt == s::base + tbl.move_task_order;
    const bool is_advance = vt == s::base + tbl.advance_order;
    if (!is_move && !is_move_task && !is_advance) return;
    const auto child = read<Address>(o, 0x30);
    if (child) {
        if (!ordinary_order(actor, child)) return;
        const auto cvt = read<Address>(child);
        if (is_move) return;
        if (is_move_task && cvt != s::base + tbl.move_order) return;
        if (is_advance && cvt != s::base + tbl.move_order && cvt != s::base + tbl.move_task_order &&
            cvt != s::base + tbl.weapon_fire_order)
            return;
    }
    active_march = true;
    replaceable = true;
}
inline void scan_sensor_threat(const World& w, Address actor, Address brain, XY ref_pos, bool own_sensor,
                               bool& under_fire, bool& has_threat, int& best_rank, float& best_d2, XY& threat_xy) {
    const auto sensor = read<Address>(actor, 0xdd0);
    const bool hit_sig = sensor && sim::recent(w, read<Tick>(sensor, 0x2f4), 4.f);
    const float morale = read<float>(brain, 0x474);
    const unsigned bflags = read<unsigned>(brain, 0x39c);
    bool hostile_threatening = false;
    const auto contacts = pr::subjects(actor);
    const float max_d2 = (100.f * w.length) * (100.f * w.length);
    if (contacts.valid(8)) {
        for (auto it = contacts.first; it != contacts.end; it += 8) {
            const auto sub = read<Address>(it);
            if (!remembered(sub)) continue;
            const bool vis = pr::is_visible(sub);
            if (!vis && !sim::recent(w, read<Tick>(sub, 0x64), own_sensor ? 8.f : 6.f)) continue;
            const XY pos = read<XY>(sub, 0x28);
            if (!std::isfinite(pos.x) || !std::isfinite(pos.y)) continue;
            const float d2 = p::squared(pos - ref_pos);
            if (!(d2 > 0.f && d2 <= max_d2)) continue;
            const unsigned rel = read<unsigned>(sub, 0x14);
            const bool recent_atk = (rel & 0x180u) && (vis || sim::recent(w, read<Tick>(sub, 0x68), 4.f));
            hostile_threatening |= recent_atk;
            const int rank = (own_sensor ? 8 : 0) + (recent_atk ? 4 : 0) + (vis ? 2 : 0) + 1;
            if (!has_threat || rank > best_rank || (rank == best_rank && d2 < best_d2)) {
                has_threat = true;
                best_rank = rank;
                best_d2 = d2;
                threat_xy = pos;
            }
        }
    }
    if (p::ai_march_under_fire(morale, losing_morale(w, brain), hit_sig, bflags, hostile_threatening))
        under_fire = true;
}
inline void find_squad_threat(const Context& c, XY& threat_xy, bool& under_fire, bool& has_threat) {
    under_fire = false;
    has_threat = false;
    int best_rank = -1;
    float best_d2 = std::numeric_limits<float>::infinity();
    scan_sensor_threat(c.w, c.actor, c.brain, c.self, true, under_fire, has_threat, best_rank, best_d2, threat_xy);
    if (c.squad) {
        const auto members = s::span(c.squad, 0x80);
        if (members.valid(8, 64))
            for (auto it = members.first; it != members.end; it += 8) {
                const auto mate = read<Address>(it);
                if (!s::live_entity(mate) || mate == c.actor) continue;
                const auto mb = read<Address>(mate, 0xdc8);
                if (!live_squad_human(mate, mb, c.squad) || !on_foot(mate)) continue;
                scan_sensor_threat(c.w, mate, mb, c.self, false, under_fire, has_threat, best_rank, best_d2, threat_xy);
            }
    }
    if (!has_threat) under_fire = false;
}
inline void resolve_cached_gunner(Context& c) {
    if (c.gunner || !c.soldier || !c.squad) return;
    if (c.soldier->pair_squad != c.squad || !c.soldier->paired_gunner) return;
    const auto g = c.soldier->paired_gunner;
    const auto gb = s::live_entity(g) ? read<Address>(g, 0xdc8) : 0;
    if (!live_squad_human(g, gb, c.squad) || !on_foot(g) || read<unsigned>(g, 0xc0) != c.soldier->gunner_id) return;
    const XY gpos = read<XY>(g, 0x74);
    if (!std::isfinite(gpos.x) || !std::isfinite(gpos.y)) return;
    c.gunner = g;
    c.gunner_pos = gpos;
}
inline bool detect_clump(const Context& c, XY& clump_center, float& nearest_m, bool& higher_priority) {
    nearest_m = std::numeric_limits<float>::infinity();
    higher_priority = false;
    if (!c.squad || !c.soldier) return false;
    const auto members = s::span(c.squad, 0x80);
    if (!members.valid(8, 64)) return false;
    const float trigger_sq = (p::kClumpTriggerM * c.w.length) * (p::kClumpTriggerM * c.w.length);
    XY sum = c.self;
    unsigned close = 0;
    for (auto it = members.first; it != members.end; it += 8) {
        const auto mate = read<Address>(it);
        if (!s::live_entity(mate) || mate == c.actor) continue;
        const auto mb = read<Address>(mate, 0xdc8);
        if (!live_squad_human(mate, mb, c.squad) || !on_foot(mate)) continue;
        const XY pos = read<XY>(mate, 0x74);
        if (!std::isfinite(pos.x) || !std::isfinite(pos.y)) continue;
        const float d2 = p::squared(pos - c.self);
        if (d2 > trigger_sq) continue;
        // Ignore a squadmate who has already claimed a dispersion/cover destination clear of this clump.
        if (claims.has_outbound_claim(scene, c.squad, mate, c.self, trigger_sq, c.now)) continue;
        const float dist_m = std::sqrt(d2) / c.w.length;
        nearest_m = (std::min)(nearest_m, dist_m);
        sum = sum + pos;
        ++close;
        const auto mcov = read<Address>(mate, 0xdc0);
        const bool mate_cov = mcov && read<int>(mcov, 0x24) != 0;
        const auto* mrow = mcov ? actor_record(mate, mb, c.now) : nullptr;
        const p::Role mrole = mrow ? mrow->role : p::Role::rifle;
        const unsigned mid = mrow ? mrow->id : read<unsigned>(mate, 0xc0);
        if (p::anchor_beats(mate_cov, mate == c.gunner, mrole, mid, c.soldier->covered, c.soldier->role, c.soldier->id))
            higher_priority = true;
    }
    if (!close) return false;
    clump_center = sum * (1.f / static_cast<float>(close + 1));
    return true;
}
inline float point_exposure(const Context& c, XY xy) {
    float exposure = 0.f;
    const auto list = pr::subjects(c.actor);
    if (list.valid(8))
        for (auto it = list.first; it != list.end; it += 8) {
            const auto sub = read<Address>(it);
            if (remembered(sub) && pr::is_visible(sub))
                exposure += 1.f / (1.f + std::sqrt(p::squared(xy - read<XY>(sub, 0x28))) / c.w.length);
        }
    return exposure;
}
template <class PassableFn, class SightFn>
inline bool choose_disperse_offset(const Context& c, PassableFn is_passable, SightFn probe_sight, XY& winner) {
    const auto candidates =
        p::lateral_disperse_candidates(c.self, c.threat, c.placement_center, c.soldier->id, c.w.length);
    const XY outward = p::unit(c.self - c.placement_center);
    struct OffsetCand {
        XY xy{};
        float spacing_m{};
        float dist_m{};
        float exposure{};
        float outward_dot{};
        float base_val{-std::numeric_limits<float>::infinity()};
        p::Sight vis{p::Sight::unknown};
        bool probed{};
    };
    std::array<OffsetCand, 10> pool{};
    unsigned count = 0;
    for (const XY& cand : candidates) {
        if (count >= pool.size()) break;
        if (!std::isfinite(cand.x) || !std::isfinite(cand.y)) continue;
        if (!within_team(c, cand) || !p::dispersion_step_safe(c.self, cand, c.threat)) continue;
        const float sp = spacing(c, cand);
        if (!p::spacing_improved(c.clump_spacing, sp, false)) continue;
        if (!is_passable(cand)) continue;
        const float dist_m = p::meters_between(cand, c.self, c.w.length);
        const float exp = point_exposure(c, cand);
        const float odot = p::squared(outward) > 0.f ? p::dot(p::unit(cand - c.self), outward) : 0.f;
        const float bval = p::score_disperse_offset(sp, exp, p::Sight::unknown, dist_m, odot);
        if (!std::isfinite(bval)) continue;
        pool[count++] = {cand, sp, dist_m, exp, odot, bval, p::Sight::unknown, false};
    }
    if (!count) return false;
    if (c.sub) {
        // Probe firing LOF on up to 4 best lateral candidates (using crouch height if prone so shooters stay effective).
        const int cur = stance(c);
        const float probe_h = cur == 1 ? .9f : height(cur);
        for (unsigned step = 0; step < 4; ++step) {
            unsigned pick = count;
            float top = -std::numeric_limits<float>::infinity();
            for (unsigned i = 0; i < count; ++i)
                if (!pool[i].probed && pool[i].base_val > top) {
                    top = pool[i].base_val;
                    pick = i;
                }
            if (pick == count) break;
            pool[pick].probed = true;
            pool[pick].vis = probe_sight(pool[pick].xy, probe_h);
        }
    }
    unsigned best_idx = count;
    float best_score = -std::numeric_limits<float>::infinity();
    for (unsigned i = 0; i < count; ++i) {
        const float val = p::score_disperse_offset(pool[i].spacing_m, pool[i].exposure, pool[i].vis, pool[i].dist_m,
                                                   pool[i].outward_dot);
        if (std::isfinite(val) && val > best_score) {
            best_score = val;
            best_idx = i;
        }
    }
    if (best_idx == count) return false;
    winner = pool[best_idx].xy;
    return true;
}
// These orders can also be issued in idle. Only battle states own battle bookkeeping and event 0x10.
inline bool place_soldier(Context& c, Purpose purpose) {
    const bool overflow = purpose == Purpose::overflow;
    const bool is_march = purpose == Purpose::march_cover;
    const bool is_disperse = purpose == Purpose::disperse_cover;
    const float radius = overflow                     ? config::active().tuning.overflow_cover_radius_m
                         : is_march                   ? p::kMarchCoverRadiusM
                         : is_disperse                ? 9.f
                         : purpose == Purpose::spread ? 8.5f
                                                      : 8.f;
    Search search(c, 0, is_march || is_disperse ? c.self : c.placement_center, radius);
    put(search.ptr(), 0x124, std::uint16_t{0});
    if (is_march || is_disperse) {
        native<void (*)(Address, float)>(0x8BB160)(search.ptr() + 0x170, 0.f);
        put(search.ptr(), 0x170, 10.f);
        put(search.ptr(), 0x174, 10.f);
        put(search.ptr(), 0x178, 10.f);
    } else {
        put(search.ptr(), 0x170, 10.f); // Require native path reachability for the descriptor.
    }
    Choice best{};
    Address order = 0;
    XY point{};
    if (choose(c, search.ptr(), purpose, best)) {
        point = best.spot.xy();
        order = native<Address (*)(const Descriptor*)>(0x8E2770)(&best.spot);
    } else if (is_disperse) {
        s::PathContext path(c.actor);
        const sim::Vec3 self_pos = s::pos(c.actor);
        auto passable = [&](XY cand) {
            sim::Vec3 dest{cand.x, cand.y, 0.f};
            return s::ground(c.w, dest) && s::corridor(c.w, path, self_pos, dest);
        };
        if (choose_disperse_offset(c, passable, [&](XY pt, float h) { return sight(c, pt, h); }, point))
            order = native<Address (*)(const XY*, unsigned)>(0x90A8C0)(&point, 0);
    } else if (!overflow && !is_march) {
        s::PathContext path(c.actor);
        for (unsigned i = 0; i < 8; ++i) {
            point = c.placement_center + p::pick_dismount_spread_offset(c.self, c.placement_center, c.spread_away,
                                                                        c.soldier->id, i, c.w.length);
            if (!std::isfinite(placement_score(c, purpose, point, 0.f))) continue;
            sim::Vec3 dest{point.x, point.y, 0};
            if (!s::ground(c.w, dest) || !s::corridor(c.w, path, s::pos(c.actor), dest)) continue;
            order = native<Address (*)(const XY*, unsigned)>(0x90A8C0)(&point, 0);
            break;
        }
    }
    if (!order) return false;
    bool ready = false;
    if (is_march) {
        bool active_m = false, repl = false;
        march_order_state(c.actor, c.brain, active_m, repl);
        ready = on_foot(c.actor) && repl && ai_march_available(c.w, c.actor, c.brain) &&
                ai_march_roster_permits(c.squad, c.actor);
    } else if (is_disperse) {
        ready = clump_ready(c);
    } else {
        ready = placement_ready(c, overflow);
    }
    if (!ready || !claims.try_claim({scene, c.actor, c.squad, c.soldier->id, point, c.now + 12}, c.now)) {
        s::discard(order);
        return false;
    }
    if ((is_march || is_disperse) && (read<unsigned>(c.brain, 0x39c) & (1u << 9)))
        native<void (*)(Address, Byte)>(0x91B360)(c.brain, 0);
    const auto queue = read<Address>(c.brain, 0xf0);
    if (queue && s::free_slot(c.actor))
        native<Address (*)(Address, Address, int, Byte)>(0x919270)(queue, order, 0, 1);
    else
        native<void (*)(Address, Address)>(0x91DB30)(c.brain, order);
    issued.fetch_add(1, std::memory_order_relaxed);
    if (overflow) stat_overflow_cover.fetch_add(1, std::memory_order_relaxed);
    if (purpose == Purpose::spread) stat_dismount_spread.fetch_add(1, std::memory_order_relaxed);
    if (is_disperse) stat_clump_spread.fetch_add(1, std::memory_order_relaxed);
    if (is_march) stat_march_cover.fetch_add(1, std::memory_order_relaxed);
    return true;
}
template <class Place> bool follow_gunner(Context& c, Place place) {
    if (!c.gunner || !lagging(c) || c.now < c.soldier->next_search || read<Byte>(c.brain, 0x520) ||
        !placement_ready(c, false))
        return false;
    c.placement_center = c.gunner_pos;
    c.soldier->next_search = c.now + 1.;
    return place(c, Purpose::assist);
}
inline bool movement(Context& c, Address state) {
    if (c.gunner && lagging(c)) return follow_gunner(c, place_soldier);
    close_grenade::Choice rush{};
    if (c.now >= c.soldier->next_search && at_rush(c, rush)) return issue_at_rush(c, state, rush);
    if (!c.sub || c.now < c.soldier->fire_until || c.now < c.soldier->next_search) return false;
    bool rejoin = lagging(c), retreat = withdrawing(c);
    if (c.team && c.team->issue_phase != c.team->phase_at) {
        c.team->issued_bounds = 0;
        c.team->issue_phase = c.team->phase_at;
    }
    const bool squad_bound = c.team && c.moving_element && !rejoin && !retreat;
    if (squad_bound && (c.team->issued_bounds >= 4 ||
                        (c.soldier->issued_phase == c.team->phase_at && c.soldier->issued_squad == c.squad)))
        return false;
    if (!retreat && read<Byte>(c.brain, 0x520)) return false;
    if (c.wedge_element && !rejoin && !retreat) {
        // Real fire-and-maneuver: never peel out of cover while an enemy is actively shooting directly at you.
        if (c.soldier->covered && (read<unsigned>(c.sub, 0x14) & 0x100u)) return false;
        const float flank_sign = p::wedge_sign(c.team->mode);
        const float flank_angle = p::wedge_flank_angle_deg(c.self, c.center, c.threat, flank_sign);
        const float enemy_dist_m = p::meters_between(c.self, c.threat, c.w.length);
        if (p::wedge_in_enfilade(flank_angle, enemy_dist_m, c.soldier->covered, current_sight(c) == p::Sight::open))
            return false;
        const XY wdir = p::wedge_direction(c.self, c.center, c.threat, flank_sign, c.w.length);
        if (p::squared(wdir) < .9f) return false;
        const XY wgoal = c.self + wdir * (12.f * c.w.length);
        c.soldier->next_search = c.now + 1.5;
        Search search(c, state, wgoal, 11.f);
        auto q = search.ptr();
        put(q, 0x124, std::uint16_t{0});
        native<void (*)(Address, float)>(0x8BB160)(q + 0x170, 0.f);
        put(q, 0x170, 10.f);
        put(q, 0x174, 10.f);
        put(q, 0x178, 10.f);
        Choice choice{};
        if (choose(c, q, Purpose::wedge, choice) && issue(c, state, &choice.spot, nullptr, -1, 7, squad_bound)) {
            stat_wedge_moves.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        return false; // Strictly cover-to-cover: never sprint into open fields without cover.
    }
    if (!rejoin) {
        if (!retreat && c.team && !c.moving_element) return false;
        if (!retreat && !c.team && c.soldier->posture < p::advance) return false;
    }
    const bool keep_shot = !rejoin && !retreat && hold_shot(c);
    const int point_action = p::point_move_action(rejoin, retreat, !rejoin && retreat && screened(c), keep_shot);
    if (!point_action && (!squad_bound || c.soldier->covered)) return false;
    XY direction = rejoin ? p::unit(c.center - c.self) : retreat ? away(c) : forward(c);
    if (p::squared(direction) < .9f) return false;
    XY goal = c.self + direction * ((retreat ? 14.f : 12.f) * c.w.length);
    c.soldier->next_search = c.now + 1;
    Search search(c, state, goal, 6);
    search.directed(c, direction, p::bound_range(retreat).minimum, retreat);
    Choice choice;
    if (choose(c, search.ptr(), retreat ? Purpose::retreat : Purpose::bound, choice))
        return issue(c, state, &choice.spot, nullptr, -1, retreat ? 8 : 7, squad_bound);
    if (!point_action) return false;
    const XY lat = p::left(direction);
    const sim::Vec3 self_pos = s::pos(c.actor);
    s::PathContext path(c.actor);
    XY chosen{};
    float best_sp = -1.f;
    for (float off : {0.f, 5.5f, -5.5f, 7.5f, -7.5f}) {
        const XY cand = goal + lat * (off * c.w.length);
        if (!within_team(c, cand)) continue;
        const float sp = spacing(c, cand);
        if (sp < 2.5f) continue;
        sim::Vec3 endpoint{cand.x, cand.y, 0.f};
        if (!s::ground(c.w, endpoint) || !s::corridor(c.w, path, self_pos, endpoint)) continue;
        if (sp >= p::kPreferredSpacingMinM) {
            chosen = cand;
            best_sp = sp;
            break;
        }
        if (sp > best_sp) {
            chosen = cand;
            best_sp = sp;
        }
    }
    if (best_sp < 0.f) return false;
    return issue(c, state, nullptr, &chosen, -1, point_action, squad_bound);
}
inline bool try_battle_clump_disperse(Context& c, Address state) {
    if (c.soldier->covered || !c.squad || !c.sub) return false;
    XY clump_center{};
    float clump_spacing = std::numeric_limits<float>::infinity();
    bool higher_priority = false;
    if (!detect_clump(c, clump_center, clump_spacing, higher_priority) ||
        !p::should_disperse_clump(*c.soldier, c.now, true, clump_spacing, higher_priority))
        return false;
    const XY threat_xy = read<XY>(c.sub, 0x28);
    if (!std::isfinite(threat_xy.x) || !std::isfinite(threat_xy.y)) return false;
    c.threat = threat_xy;
    c.placement_center = clump_center;
    c.clump_spacing = clump_spacing;
    c.soldier->disperse_retry = c.now + p::kDisperseRetryS;
    Search search(c, state, c.self, 9.f);
    auto q = search.ptr();
    put(q, 0x124, std::uint16_t{0});
    native<void (*)(Address, float)>(0x8BB160)(q + 0x170, 0.f);
    put(q, 0x170, 10.f);
    put(q, 0x174, 10.f);
    put(q, 0x178, 10.f);
    Choice choice{};
    if (choose(c, q, Purpose::disperse_cover, choice)) {
        if (issue(c, state, &choice.spot, nullptr, -1, 7)) {
            c.soldier->dispersed_at = c.now;
            stat_clump_spread.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        return false;
    }
    s::PathContext path(c.actor);
    const sim::Vec3 self_pos = s::pos(c.actor);
    auto passable = [&](XY cand) {
        sim::Vec3 dest{cand.x, cand.y, 0.f};
        return s::ground(c.w, dest) && s::corridor(c.w, path, self_pos, dest);
    };
    XY offset_pt{};
    if (choose_disperse_offset(
            c, passable, [&](XY pt, float h) { return sight(c, pt, h); }, offset_pt) &&
        issue(c, state, nullptr, &offset_pt, -1, 7)) {
        c.soldier->dispersed_at = c.now;
        stat_clump_spread.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    return false;
}
inline bool update_cover(Context& c, Address state) {
    coordinate(c);
    close_grenade::Choice rush{};
    if (c.now >= c.soldier->next_search && at_rush(c, rush)) return issue_at_rush(c, state, rush);
    if (!c.sub || c.now < c.soldier->fire_until || c.now < c.soldier->next_search) return false;
    if (c.team && (c.moving_element || lagging(c))) {
        // The follower branch reaches update_cover independently of native formation twitch.
        return movement(c, state);
    }
    if (!c.soldier->covered && try_battle_clump_disperse(c, state)) return true;
    const bool screen = screened(c);
    const auto visibility = current_sight(c);
    const bool keep_open_shot = !withdrawing(c) && hold_shot(c);
    if ((screen && c.team) || visibility == p::Sight::unknown ||
        p::stable_cover(c.now, *c.soldier, screen, read<unsigned>(c.sub, 0x14) & 0x180, visibility == p::Sight::open) ||
        (c.soldier->covered && keep_open_shot))
        return false;
    Search search(c, state, c.self, c.soldier->covered ? 6.f : 10.f);
    // Evaluate both descriptors with identical weights. Native zeroes w170 only for the incumbent.
    auto q = search.ptr();
    put(q, 0x124, std::uint16_t{0});
    native<void (*)(Address, float)>(0x8BB160)(q + 0x170, 0.f);
    put(q, 0x170, 10.f); // Positive path weight also enables the native unreachable-candidate rejection.
    put(q, 0x174, 10.f);
    put(q, 0x178, 10.f);
    c.soldier->next_search = c.now + 1;
    Choice choice;
    if (!choose(c, q, Purpose::update, choice)) return false;
    if (!c.soldier->covered && keep_open_shot && !screened(c, choice.spot.ptr(), choice.spot.xy())) return false;
    if (c.soldier->covered) {
        auto current = read<Descriptor>(read<Address>(c.actor, 0xdc0), 0x20);
        native<void (*)(Address)>(0x8BB180)(current.ptr() + 0x5c);
        native<void (*)(Address)>(0x8BB1A0)(current.ptr() + 0x4c);
        put(q, 0x11f, Byte{1});
        put(current.ptr(), 0x80, std::numeric_limits<float>::quiet_NaN());
        native<Byte (*)(Address, Address)>(0x8B6090)(q, current.ptr());
        float incumbent =
            p::cover_score(read<float>(current.ptr(), 0x80), visibility, withdrawing(c), 0, spacing(c, c.self), screen);
        if (!std::isfinite(incumbent) || choice.score < incumbent + 8) return false;
    }
    return issue(c, state, &choice.spot, nullptr, -1, 7);
}
inline bool detour(Context& c, Address state) {
    if (!c.sub || c.now < c.soldier->detour_until || !s::free_slot(c.actor)) return false;
    c.soldier->detour_until = c.now + 4;
    coordinate(c);
    int current = stance(c);
    if (current_sight(c) != p::Sight::blocked) return false;
    const bool crouch = current == 1 && sight(c, c.self, .9f) == p::Sight::open;
    int up = p::rise(current, false, crouch,
                     !crouch && (current == 1 || current == 2) && sight(c, c.self, 1.35f) == p::Sight::open);
    if (up >= 0) return issue(c, state, nullptr, nullptr, up, 1);
    auto aim = p::unit(read<XY>(c.sub, 0x28) - c.self);
    auto side = p::left(aim);
    XY winner{};
    float best = -std::numeric_limits<float>::infinity();
    s::PathContext path(c.actor);
    for (float offset : {2.5f, -2.5f, 5.f, -5.f, 7.5f, -7.5f}) {
        XY xy = c.self + side * (offset * c.w.length);
        if (c.team && (c.team->mode == p::Mode::left || c.team->mode == p::Mode::right))
            xy = xy + forward(c) * (.5f * c.w.length);
        sim::Vec3 pt{xy.x, xy.y, 0};
        if (!within_team(c, xy) || !s::ground(c.w, pt) || !s::corridor(c.w, path, s::pos(c.actor), pt)) continue;
        float exposure = 0;
        auto list = pr::subjects(c.actor);
        if (list.valid(8))
            for (auto it = list.first; it != list.end; it += 8) {
                auto sub = read<Address>(it);
                if (remembered(sub) && pr::is_visible(sub))
                    exposure += 1.f / (1.f + std::sqrt(p::squared(xy - read<XY>(sub, 0x28))) / c.w.length);
            }
        float score = p::peek_score(true, sight(c, xy, height(current)) == p::Sight::open, exposure, spacing(c, xy),
                                    alignment(c, xy), std::abs(offset));
        if (score > best) {
            best = score;
            winner = xy;
        }
    }
    if (!std::isfinite(best)) return false;
    Search search(c, state, winner, 1.5f);
    Choice choice;
    if (choose(c, search.ptr(), Purpose::peek, choice)) return issue(c, state, &choice.spot, nullptr, -1, 1);
    return issue(c, state, nullptr, &winner, -1, 1);
}

using Void = void (*)(Address);
using Test = Byte (*)(Address);
using SearchFn = Byte (*)(Address, Descriptor*, Address);
using Retreat = void (*)(Address, XY*, float);
struct Trampolines {
    Void refresh{}, base{}, cover_execute{}, outdoor_timer{}, cover_timer{}, idle{};
    Test follower{}, follow{}, replan{}, update{}, detour{}, injury{}, movement{};
    SearchFn search{};
    Retreat retreat{};
};
inline Trampolines original{};
#if defined(_MSC_VER)
#define GOH_TACTICAL_CALLER() reinterpret_cast<Address>(_ReturnAddress())
#else
#define GOH_TACTICAL_CALLER() reinterpret_cast<Address>(__builtin_return_address(0))
#endif
inline void refresh(Address b) {
    Guard guard;
    original.refresh(b);
    if (GOH_TACTICAL_CALLER() != s::base + 0x95A69E) return;
    Context c{};
    if (context(c, b) && c.sub) {
        const auto old = read<int>(b, 0x52c);
        if (old != p::flee && !(old == p::advance && read<Tick>(b, 0x530))) return;
        auto bal = balance(b);
        if (bal.valid() && bal.a > bal.b * bal.retreat_ratio && bal.morale >= 50) {
            // Avoid sampling the vanilla rally timer. Keep the previous policy for hysteresis at dispatch.
            if (!c.soldier->initialized) c.soldier->posture = p::flee;
            put(b, 0x52c, int(p::fallback));
            put(b, 0x530, Tick{});
        }
    }
}
inline void dispatch(Address b) {
    Guard guard;
    Context c{};
    if (GOH_TACTICAL_CALLER() == s::base + 0x95ABDB && context(c, b) && c.sub) {
        auto bal = balance(b);
        if (bal.valid()) {
            if (c.squad && (c.soldier->sight_target != c.sub || c.now - c.soldier->sight_at >= .35)) current_sight(c);
            auto prior = c.soldier->posture == p::flee ? p::flee
                                                       : static_cast<p::Posture>(std::clamp(read<int>(b, 0x52c), 0, 4));
            auto posture = p::decide(*c.soldier, bal, prior, c.now);
            coordinate(c);
            if (c.team)
                posture = p::sync_squad_posture(posture, c.team->mode, c.moving_element, lagging(c), bal.no_advance,
                                                bal.no_retreat);
            close_grenade::Choice rush{};
            if (at_rush(c, rush)) {
                posture = p::assault;
                c.soldier->fire_until = p::never;
                close_grenade::note_rush(c.w, rush.target, c.actor, 0);
                // Clear both OutdoorsBattle (0x1110738) and CoverBattle (0x1110538)
                // tactical timers at state+0x3EC so 0x96C220 is not stalled for 4-5.5 s.
                const Address leaf = read<Address>(b, 0xe8);
                if (leaf && read<Address>(leaf, 0x30) == b) {
                    const Address vt = read<Address>(leaf);
                    if (vt == s::base + 0x1110738 || vt == s::base + 0x1110538) {
                        const Tick due = read<Tick>(leaf, 0x3ec);
                        if (!due || due > c.w.now) put(leaf, 0x3ec, c.w.now ? c.w.now : Tick{1});
                    }
                }
            } else {
                posture = p::ineffective_posture(
                    posture, c.soldier->covered, bal.no_retreat, read<unsigned>(c.sub, 0x14),
                    p::meters_between(c.self, read<XY>(c.sub, 0x28), c.w.length),
                    [&] { return held_effective_vs_target(c.actor, c.sub) || visible_soft_target(c); });
            }
            put(b, 0x52c, int(posture));
            put(b, 0x530, Tick{});
        }
    } else if (GOH_TACTICAL_CALLER() == s::base + 0x95ABDB && ai_march_context(c, b) &&
               c.now < c.soldier->cover_break_until && (read<unsigned>(b, 0x39c) & (1u << 9))) {
        native<void (*)(Address, Byte)>(0x91B360)(b, 0);
    }
    original.base(b);
}
inline void cover_execute(Address state) {
    Guard guard;
    Context c{};
    if (for_state(c, state) && c.sub) {
        if (read<unsigned>(c.brain, 0x39c) & (1u << 9)) {
            coordinate(c);
            if (c.team) native<void (*)(Address, Byte)>(0x91B360)(c.brain, 0);
        }
        close_grenade::Choice rush{};
        if (at_rush(c, rush)) {
            const Tick due = read<Tick>(state, 0x3ec);
            if (!due || due > c.w.now) put(state, 0x3ec, c.w.now ? c.w.now : Tick{1});
        }
    } else if (for_ai_march_state(c, state) && c.now < c.soldier->cover_break_until &&
               (read<unsigned>(c.brain, 0x39c) & (1u << 9))) {
        native<void (*)(Address, Byte)>(0x91B360)(c.brain, 0);
    }
    original.cover_execute(state);
}
inline Byte follower(Address b) {
    Guard guard;
    // This one branch chooses the commander-only movement path.
    if (GOH_TACTICAL_CALLER() == s::base + 0x963427) {
        Context c{};
        if (context(c, b) && c.sub) {
            coordinate(c);
            if (c.team) return 0;
        } else if (ai_march_context(c, b) && c.now < c.soldier->cover_break_until) {
            return 0;
        }
    }
    return original.follower(b);
}
#undef GOH_TACTICAL_CALLER
inline Byte follow(Address state) {
    Guard guard;
    Context c{};
    if (for_state(c, state) && c.sub) {
        coordinate(c);
        if (c.team) return 0;
    } else if (for_ai_march_state(c, state) && c.now < c.soldier->cover_break_until) {
        return 0;
    }
    return original.follow(state);
}
inline Byte replan(Address state) {
    Guard guard;
    Context c{};
    if (!for_state(c, state)) {
        const auto actor = engine_fixes::current();
        const auto brain = read<Address>(state, 0x30);
        if (!engine_fixes::authorized(actor, brain)) return original.replan(state);
        const float length = read<float>(s::base, runtime::table().glob.spatial_scale_l);
        if (!(length > 0) || !std::isfinite(length)) return original.replan(state);
        return p::replan(read<XY>(state, 0x3cc), read<XY>(brain, 0x138), read<XY>(actor, 0x74), length, false);
    }
    coordinate(c);
    return p::replan(read<XY>(state, 0x3cc), read<XY>(c.brain, 0x138), c.self, c.w.length, c.team != nullptr);
}
inline Byte move(Address state) {
    Guard guard;
    Context c{};
    if (!for_state(c, state) || !c.sub) return original.movement(state);
    coordinate(c);
    return movement(c, state);
}
template <bool Cover> void post_move(Address state) {
    Guard guard;
    Context c{};
    if (!for_state(c, state)) {
        (Cover ? original.cover_timer : original.outdoor_timer)(state);
        return;
    }
    const bool was_rush = c.soldier->rush_active;
    c.soldier->rush_active = false;
    close_grenade::Choice rush{};
    if (was_rush || at_rush(c, rush)) {
        c.soldier->moved = c.now;
        c.soldier->fire_until = p::never;
        put(state, 0x3ec, p::deadline(c.w.now, c.w.duration(.25f)));
        return;
    }
    auto posture = static_cast<p::Posture>(std::clamp(read<int>(c.brain, 0x52c), 0, 4));
    float wait = p::dwell(posture, Cover, screened(c));
    c.soldier->moved = c.now;
    c.soldier->fire_until = c.now + wait;
    const auto duration = c.w.duration(wait);
    put(state, 0x3ec, p::deadline(c.w.now, duration));
}
inline Byte search(Address state, Descriptor* out, Address query) {
    Guard guard;
    Context c{};
    const bool normal = for_state(c, state);
    if ((!normal && !for_ai_march_state(c, state)) || !c.sub || !out || !query ||
        read<Address>(query, 0x108) != c.actor)
        return original.search(state, out, query);
    if (normal) coordinate(c);
    Choice best;
    if (!choose(c, query, Purpose::native, best)) return 0;
    *out = best.spot;
    claim(c, out->xy());
    return 1;
}
inline Byte update(Address state) {
    Guard guard;
    Context c{};
    return for_state(c, state) && c.sub ? update_cover(c, state) : original.update(state);
}
inline void retreat(Address sensor, XY* out, float radius) {
    Guard guard;
    Context c{};
    Address actor = accepting.load() && s::serial_thread() ? s::current_actor() : 0;
    if (!actor || !out || read<Address>(actor, 0xdd0) != sensor || !context(c, read<Address>(actor, 0xdc8)) ||
        !std::isfinite(radius) || radius <= 0) {
        original.retreat(sensor, out, radius);
        return;
    }
    coordinate(c);
    *out = c.self + away(c) * radius;
}
inline Byte blocked(Address state) {
    Guard guard;
    Context c{};
    return for_state(c, state) && c.sub ? detour(c, state) : original.detour(state);
}
inline Byte injury(Address state) {
    Guard guard;
    Context c{};
    if (for_state(c, state) && screened(c)) {
        const auto observations = pr::subjects(c.actor);
        const Address descriptor = read<Address>(c.actor, 0xdc0) + 0x20;
        if (observations.valid(8)) {
            for (auto it = observations.first; it != observations.end; it += 8) {
                auto other = read<Address>(it);
                if (other != c.sub && remembered(other) && pr::is_visible(other) &&
                    (read<unsigned>(other, 0x14) & 0x180) &&
                    !native<Byte (*)(Address, Address, const XY*)>(0x8BB590)(descriptor, other + 0x28, &c.self))
                    return original.injury(state);
            }
            return 0;
        }
    }
    return original.injury(state);
}
inline void idle(Address state) {
    Guard guard;
    Context c{};
    if (for_state(c, state)) {
        unsigned mode = read<unsigned>(c.brain, 0x398), flags = read<unsigned>(c.brain, 0x39c);
        if (!(mode & 3) && (flags & (1u << 11))) {
            auto list = pr::subjects(c.actor);
            bool nearby_enemy = false;
            if (list.valid(8))
                for (auto it = list.first; it != list.end; it += 8) {
                    auto sub = read<Address>(it);
                    if (remembered(sub) && p::squared(c.self - read<XY>(sub, 0x28)) <= 3600 * c.w.length * c.w.length) {
                        nearby_enemy = true;
                        break;
                    }
                }
            if (p::hold_anchor(mode, flags, c.soldier->covered, nearby_enemy))
                native<void (*)(Address, Byte)>(0x91B740)(c.brain, 0);
        }
    }
    original.idle(state);
}

template <class Place> bool settle_squad(Context& c, Place place) {
    const auto& modules = config::active().modules;
    const auto actor = c.actor;
    c.brain = read<Address>(actor, 0xdc8);
    c.squad = read<Address>(c.brain, 0x100);
    if (!runtime::actor_ai_allowed(actor) || !live_squad_human(actor, c.brain, c.squad, true) || !observe_epoch(c))
        return false;
    c.self = read<XY>(actor, 0x74);
    if (!std::isfinite(c.self.x) || !std::isfinite(c.self.y)) return false;
    c.soldier = actor_record(actor, c.brain, c.now);
    if (!c.soldier) return false;
    auto& row = *c.soldier;
    if (c.squad != row.placement_squad) {
        static_cast<p::Placement&>(row) = {};
        row.placement_squad = c.squad;
    }
    if (!c.squad || read<Address>(c.squad) != s::base + 0x110F3A0) return false;
    const unsigned modes = read<unsigned>(c.brain, 0x398), flags = read<unsigned>(c.brain, 0x39c);
    const bool mounted = !on_foot(actor), dismount = row.was_mounted && !mounted;
    row.observe(mounted, p::active_user_move(modes, flags), c.self, c.now);
    if (row.covered || !modules.overflow_cover) row.overflow_done = true;
    if (mounted) return false;
    auto* team = record(squads, c.squad, c.now);
    if (!team) return false;
    if (modules.weapon_team_cohesion && dismount && row.dismounted_at == c.now) {
        team->dismounted_at = c.now;
        team->dismount_position = c.self;
    }
    const double nearby_dismount =
        p::meters_between(c.self, team->dismount_position, c.w.length) <= 2.5f ? team->dismounted_at : p::never;
    const bool spread = modules.weapon_team_cohesion && !row.covered && p::allows(modes, flags) &&
                        row.recent_dismount(c.now, nearby_dismount);
    const bool overflow = !row.overflow_done && c.now <= row.overflow_until;
    const bool follow_due = modules.weapon_team_cohesion && c.now >= row.next_follow && p::allows(modes, flags);
    if (follow_due) row.next_follow = c.now + 1.;
    if ((spread || overflow) && placement_ready(c, overflow)) {
        unsigned covered = 0, close = 0;
        XY sum = c.self;
        const auto members = s::span(c.squad, 0x80); // placement_ready validated this roster.
        for (auto it = members.first; it != members.end; it += 8) {
            const auto mate = read<Address>(it);
            if (!s::live_entity(mate) || mate == actor) continue;
            const auto mb = read<Address>(mate, 0xdc8);
            if (!live_squad_human(mate, mb, c.squad) || !on_foot(mate)) continue;
            const auto pos = read<XY>(mate, 0x74);
            if (overflow && read<int>(read<Address>(mate, 0xdc0), 0x24) &&
                p::meters_between(pos, row.overflow_anchor, c.w.length) <=
                    config::active().tuning.overflow_cover_radius_m)
                ++covered;
            if (spread && p::meters_between(pos, c.self, c.w.length) <= 2.5f && s::free_slot(mate) &&
                available(c.w, mate, mb)) {
                sum = sum + pos;
                ++close;
            }
        }
        const auto contacts = pr::subjects(actor);
        float nearest = std::numeric_limits<float>::infinity();
        if (contacts.valid(8))
            for (auto it = contacts.first; it != contacts.end; it += 8) {
                const auto sub = read<Address>(it);
                if (!remembered(sub)) continue;
                const XY delta = c.self - read<XY>(sub, 0x28);
                if (const float d = p::squared(delta); d < nearest) {
                    nearest = d;
                    c.spread_away = p::unit(delta);
                }
            }
        if (overflow && p::overflow_cover_eligible(modes, flags, covered, std::isfinite(nearest))) {
            row.overflow_done = true;
            c.placement_center = row.overflow_anchor;
            if (place(c, Purpose::overflow)) return true;
        }
        if (spread && close >= 2 && placement_ready(c, false)) {
            row.spread_attempt = c.now;
            c.placement_center = sum * (1.f / (close + 1));
            if (place(c, Purpose::spread)) return true;
        }
    }
    // Goal 3: Opportunistic cover-seeking during AI (control == 0) Move-at-Will open-field marches under fire.
    if (!row.covered && p::ai_march_allows(modes, flags) && c.now >= row.cover_break_until &&
        c.now >= row.march_cover_retry && ai_march_available(c.w, actor, c.brain) &&
        ai_march_roster_permits(c.squad, actor)) {
        bool active_march = false, replaceable = false;
        march_order_state(actor, c.brain, active_march, replaceable);
        const bool marching =
            active_march || read<Address>(c.squad, 0x1c8) != 0 || (flags & ((1u << 6) | (1u << 9))) != 0;
        if (marching && replaceable) {
            XY threat_xy{};
            bool under_fire = false, has_threat = false;
            find_squad_threat(c, threat_xy, under_fire, has_threat);
            if (p::should_seek_march_cover(row, c.now, marching, under_fire && has_threat)) {
                resolve_cached_gunner(c);
                c.threat = threat_xy;
                c.placement_center = c.self;
                row.march_cover_retry = c.now + p::kMarchCoverRetryS;
                if (place(c, Purpose::march_cover)) {
                    row.cover_break_until = c.now + p::kCoverBreakHoldS;
                    row.march_cover_retry = c.now + p::kMarchCoverCooldownS;
                    return true;
                }
            }
        }
    }
    // Goal 2: Active open-ground anti-clump dispersion for Move-at-Will infantry under threat.
    if (!row.covered && p::allows(modes, flags) && c.now - row.dispersed_at >= p::kDisperseCooldownS &&
        c.now >= row.disperse_retry && clump_ready(c)) {
        XY threat_xy{};
        bool under_fire = false, has_threat = false;
        find_squad_threat(c, threat_xy, under_fire, has_threat);
        const Address raw_sub = read<Address>(c.brain, 0x3b8);
        const Address own_sub =
            s::live_runtime(c.w)
                ? target(actor, c.brain)
                : (raw_sub && pr::contains(pr::subjects(actor), raw_sub) && remembered(raw_sub) ? raw_sub : 0);
        const bool under_threat =
            has_threat &&
            (own_sub != 0 || under_fire || p::squared(threat_xy - c.self) <= (80.f * c.w.length) * (80.f * c.w.length));
        if (under_threat) {
            if (s::live_runtime(c.w)) load_role(row, c.now);
            resolve_cached_gunner(c);
            XY clump_center{};
            float clump_spacing = std::numeric_limits<float>::infinity();
            bool higher_priority = false;
            if (detect_clump(c, clump_center, clump_spacing, higher_priority) &&
                p::should_disperse_clump(row, c.now, under_threat, clump_spacing, higher_priority)) {
                c.sub = own_sub;
                c.threat = threat_xy;
                c.placement_center = clump_center;
                c.clump_spacing = clump_spacing;
                row.disperse_retry = c.now + p::kDisperseRetryS;
                if (place(c, Purpose::disperse_cover)) {
                    row.dispersed_at = c.now;
                    if ((modes & 3u) == 0u && read<Address>(c.squad, 0x1c8) != 0)
                        row.cover_break_until = (std::max)(row.cover_break_until, c.now + 6.0);
                    return true;
                }
            }
        }
    }
    if (!follow_due || !placement_ready(c, false)) return false;
    load_role(row, c.now);
    if (row.role == p::Role::heavy) return false;
    p::Roster guns{};
    const auto count = find_gunners(c, guns);
    p::Member self{actor, row.id, row.role, c.self};
    assign_assistant(c, row, self, guns, count);
    return follow_gunner(c, place);
}
inline bool settle_after_quant(Address actor) {
    const auto& modules = config::active().modules;
    Context c{};
    c.actor = actor;
    return accepting.load() && modules.tactical_infantry && pr::depth == 1 && pr::scope.actor == actor &&
           s::callback(actor, c.w) && settle_squad(c, place_soldier);
}
} // namespace detail
inline constexpr unsigned hook_count = 15;
template <class Create, class Remove>
bool prepare_hooks(Address image, const char* sha, bool enable_prototype, Create create, Remove remove) {
    using namespace detail;
    if (!image || !sha || std::strcmp(sha, executable_sha256) != 0 || installed || rollback_pending) return false;
    std::array<void*, hook_count> staged{};
    unsigned count = 0;
    auto add = [&](Address rva, auto hook, auto& original) {
        auto target = reinterpret_cast<void*>(image + rva);
        void* trampoline = nullptr;
        if (create(target, reinterpret_cast<void*>(hook), &trampoline) != 0) return false;
        staged[count++] = target;
        original = reinterpret_cast<decltype(hook)>(trampoline);
        return true;
    };
    if (!add(0x95BEE0, refresh, original.refresh) || !add(0x91C350, dispatch, original.base) ||
        !add(0x960120, cover_execute, original.cover_execute) || !add(0x91B310, follower, original.follower) ||
        !add(0x963C30, follow, original.follow) || !add(0x969EB0, replan, original.replan) ||
        !add(0x96C220, move, original.movement) || !add(0x963E60, post_move<false>, original.outdoor_timer) ||
        !add(0x961360, post_move<true>, original.cover_timer) || !add(0x969B90, search, original.search) ||
        !add(0x96D4C0, update, original.update) || !add(0x88CBC0, retreat, original.retreat) ||
        !add(0x964160, blocked, original.detour) || !add(0x96BF00, injury, original.injury) ||
        !add(0x968F20, idle, original.idle)) {
        while (count)
            if (remove(staged[--count]) != 0) rollback_pending = true;
        if (!rollback_pending) original = {};
        return false;
    }
    installed = true;
    close_grenade::squad_after_quant = enable_prototype ? settle_after_quant : nullptr;
    accepting.store(enable_prototype);
    return true;
}
inline void deactivate() {
    detail::accepting.store(false);
}
inline unsigned inflight_count() {
    return detail::inflight.load();
}
inline unsigned stat_issued() {
    return detail::issued.load(std::memory_order_relaxed);
}
inline void after_hooks_removed() {
    deactivate();
    detail::original = {};
    detail::installed = detail::rollback_pending = false;
    detail::release_tables();
    detail::issued = 0;
    close_grenade::squad_after_quant = nullptr;
    stat_overflow_cover = 0;
    stat_dismount_spread = 0;
    stat_clump_spread = 0;
    stat_march_cover = 0;
    stat_wedge_moves = 0;
}
} // namespace goh::tactical_infantry
