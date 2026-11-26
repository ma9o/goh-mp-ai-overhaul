#pragma once

#include "actor_coordinator.hpp"
#include "config_and_log.hpp"
#include "dynamic_store.hpp"
#include "game_classifiers.hpp"
#include "tactical_objectives_policy.hpp"
#include "ai_support_26734796.hpp"
#include "auto_medic_module.hpp"
#include "auto_resupply_module.hpp"
#include "tank_auto_repair_module.hpp"
#include "target_prioritization_module.hpp"

namespace goh::tactical_objectives {
namespace s = ai_support;
namespace sim = auto_smoke;
namespace pr = target_prio::detail;
namespace p = policy;
using s::Address;
inline constexpr bool runtime_allowed = false;

namespace detail {
using p::Kind;
using p::XY;
using s::read;
using s::World;
inline std::atomic<bool> accepting{false};
inline std::atomic<unsigned> pursues{}, caps{};
inline p::Epoch epoch{};
inline double seconds(const World& w) {
    return double(w.now) / w.ticks;
}
inline XY position(Address a, const World& w) {
    return read<XY>(a, 0x74) * (1.f / w.length);
}

struct Key {
    Address ptr{}, definition{};
    unsigned id{}, owner{};
    bool operator==(const Key& b) const {
        return ptr == b.ptr && definition == b.definition && id == b.id && owner == b.owner;
    }
};
inline Key actor_key(Address a) {
    return {a, read<Address>(a, 0xb0), read<unsigned>(a, 0xc0), s::owner(a)};
}
struct Row {
    Key key{};
    double touched{};
};
struct GroupState : Row {
    p::Anchor anchor;
    p::Contacts contacts;
    double sampled{p::never};
};
struct Role {
    bool support{}, medic{}, at{};
};
struct Unit : Row {
    Role role{};
    double role_sample{p::never};
    p::Idle idle;
    bool external{};
    Key group{};
    std::uint64_t revision{};
    s::Identity arrived_flag{};
};
inline store::StableTable<GroupState, 32, 2048> squads{};
inline store::StableTable<GroupState, 64, 4096> solos{};
inline store::StableTable<Unit, 128, 8192> units{};

// Recycle an expired identity even if its native address and ID were reused.
template <class Table> auto* row(Table& rows, Key key, double now) {
    using T = typename Table::value_type;
    T* free = nullptr;
    for (auto& r : rows) {
        if (!r.key.ptr || now - r.touched > 60) {
            if (!free) free = &r;
        } else if (r.key == key) {
            r.touched = now;
            return &r;
        }
    }
    if (!free) free = store::try_append_slot(rows);
    if (free) {
        *free = {};
        free->key = key;
        free->touched = now;
    }
    return free;
}
struct Target {
    Kind kind{};
    Address enemy{};
    s::Identity flag{};
    XY xy{};
};
struct Cooldown {
    Key who{};
    Target target{};
    double until{};
    bool same(Key key, const Target& t) const {
        if (!(who == key) || target.kind != t.kind) return false;
        if (t.kind == Kind::cap) return target.flag.ptr == t.flag.ptr && target.flag.id == t.flag.id;
        return target.enemy && t.enemy ? target.enemy == t.enemy : p::within_radius(target.xy, t.xy, 3);
    }
};
struct Trip {
    Key actor{}, group{};
    Address brain{};
    std::uint64_t revision{};
    Target target{};
    XY goal{};
    s::OrderRef order{};
    p::Progress progress{};
    Cooldown* backoff{};
    bool support{};
    double review_at{};
    Unit* unit{};
    GroupState* state{};
};
inline store::StableTable<Trip, 64, 2048> trips{};
inline store::StableTable<Cooldown, 64, 4096> cooldowns{};
struct Flag {
    s::Identity entity{};
    XY xy{};
};
inline store::GrowableTable<Flag, 32, 256> flags{};
inline unsigned flag_count{};
inline double flag_sample = p::never;
inline Trip* trip(Key actor) {
    for (auto& t : trips)
        if (t.actor == actor) return &t;
    return nullptr;
}
inline bool cooling(Key actor, const Target& target, double now) {
    return std::any_of(cooldowns.begin(), cooldowns.end(),
                       [&](const Cooldown& c) { return c.until > now && c.same(actor, target); });
}
// Reserve a cooldown slot before probing terrain. Active trips retain their slot.
inline Cooldown* reserve(Key actor, const Target& target, double now) {
    Cooldown* free = nullptr;
    for (auto& c : cooldowns) {
        if (c.until > now && c.same(actor, target)) return nullptr;
        if (!free && (!c.who.ptr || c.until <= now)) free = &c;
    }
    if (!free) free = cooldowns.append_slot();
    if (free) *free = {actor, target, std::numeric_limits<double>::infinity()};
    return free;
}
inline void stop(Trip& t, double now, p::Result result) {
    if (t.backoff) t.backoff->until = now + p::cooldown(result);
    t.order.abandon();
    t = {};
}
inline void preempt_actor(Address actor) {
    for (auto& t : trips) {
        if (t.actor.ptr == actor) {
            t.order.abandon();
            t = {};
        }
    }
}
inline void clear() {
    for (auto& t : trips) {
        t.order.abandon();
        t = {};
    }
    squads.fill({});
    solos.fill({});
    units.fill({});
    cooldowns.fill({});
    flags.fill({});
    flag_count = 0;
    flag_sample = p::never;
    epoch = {};
}
inline void release_tables() {
    clear();
    trips.release();
    squads.release();
    solos.release();
    units.release();
    cooldowns.release();
    flags.release();
}
inline void advance(Address scene, const World& w) {
    if (epoch.changed(scene, w.now))
        clear();
    else if (epoch.tick != w.now) {
        const double now = seconds(w);
        // An actor can leave the scene without receiving another quant.
        for (auto& t : trips)
            if (t.actor.ptr && now - t.progress.start > 18) stop(t, now, p::Result::failed);
    }
    epoch = {scene, w.now};
}
inline bool concrete(Address a) {
    if (!s::live_entity(a) || !sim::has_actor(a) || !(read<s::Byte>(a, 0x30) & 4)) return false;
    auto b = read<Address>(a, 0xdc8);
    return b && read<Address>(b) == s::base + 0x110ff88 && read<Address>(b, 0x38) == a;
}
inline bool protected_order(Address a, Address b) {
    Address o = read<Address>(b, 0xf0);
    for (unsigned depth = 0; o && depth < 32; ++depth) {
        Address vt = read<Address>(o);
        if (read<Address>(o, 8) != a + 0xaf0 || vt == s::base + 0x110c018 || vt == s::base + 0x110d278) return true;
        o = sim::orders::active_child(s::base, o);
    }
    return o != 0;
}
inline bool busy(const World& w, Address a, bool external) {
    return external ||
           !coord::instance().can_act(a, coord::Module::tactical_objectives, coord::Priority::tactical_objectives,
                                      read<unsigned>(a, 0xC0), w.now) ||
           auto_medic::occupied(a) || auto_resupply::occupied(a) || sim::is_busy(w, a) || tank_auto_repair::is_busy(a);
}
inline bool admit(const World& w, Address a, bool external) {
    if (!s::autonomous_if(a, true, p::allows)) return false;
    Address b = read<Address>(a, 0xdc8), sensor = read<Address>(a, 0xdd0);
    if (!sensor || read<Address>(sensor, 0x260) != a || s::native<s::Test>(0x91b190)(b) || protected_order(a, b) ||
        busy(w, a, external))
        return false;
    auto sub = read<Address>(b, 0x3b8);
    if (!sub) return true;
    const auto list = pr::subjects(a);
    return s::Span{list.first, list.end}.valid(8, 4096) && pr::contains(list, sub) && !pr::is_visible(sub);
}

struct Group {
    Key key{};
    std::array<Address, 64> members{};
    unsigned count{};
    XY center{};
    bool squad{}, intent{}, blocked{}, free{true};
};
inline bool roster(const World& w, Address a, Group& g) {
    auto squad = read<Address>(read<Address>(a, 0xdc8), 0x100);
    g.squad = squad != 0;
    g.key = squad ? Key{squad, 0, 0, s::owner(a)} : actor_key(a);
    auto add = [&](Address member) {
        if (!s::live_entity(member)) return true;
        if (!concrete(member) || s::owner(member) != s::owner(a)) return false;
        auto b = read<Address>(member, 0xdc8);
        if (read<Address>(b, 0x100) != squad) return false;
        auto modes = read<unsigned>(b, 0x398), bits = read<unsigned>(b, 0x39c);
        bool selected = s::native<s::Test>(0x91b190)(b) != 0;
        g.intent |= p::player_intent(modes, bits, selected);
        g.blocked |= selected || (modes & 0x3000u) != 0 || p::user_move(modes, bits);
        g.free &= s::free_slot(member);
        g.center = g.center + position(member, w);
        g.members[g.count++] = member;
        return true;
    };
    if (squad) {
        if (read<Address>(squad) != s::base + 0x110f3a0) return false;
        g.blocked = read<Address>(squad, 0x1c8) != 0;
        g.intent = g.blocked && (read<unsigned>(read<Address>(a, 0xdc8), 0x398) & 3u) == 1;
        auto list = s::span(squad, 0x80);
        if (!list.valid(8, g.members.size())) return false;
        for (auto i = list.first; i != list.end; i += 8)
            if (!add(read<Address>(i))) return false;
    } else if (!add(a))
        return false;
    if (std::find(g.members.begin(), g.members.begin() + g.count, a) == g.members.begin() + g.count) return false;
    g.center = g.center * (1.f / g.count);
    return p::finite(g.center);
}

inline void item_role(Address item, Role& role) {
    if (!item || read<int>(item, 0x48) <= 0) return;
    Address def = read<Address>(item, 0x40);
    if (!def || !s::span(def, 0x11e0).valid(0x60, 4096)) return;
    role.support |= classify::def_is_heavy_support_weapon(def);
    role.at |= classify::def_is_at_weapon(def);
}
inline Role role_of(Address a) {
    Role r;
    r.support = r.medic = auto_medic::is_medic_actor(a);
    auto box = s::box(a);
    auto items = s::span(box, 0x10);
    if (items.valid(8, 1024))
        for (auto i = items.first; i != items.end; i += 8) {
            auto item = read<Address>(i);
            if (item && read<Address>(item, 0x30) == box) item_role(item, r);
        }
    item_role(pr::hand_item(a), r);
    return r;
}
struct Force {
    Role self{};
    unsigned mobile{}, eligible{}, slot{};
    bool at{};
};
inline bool strength(const World& w, Address actor, const Group& g, Force& f) {
    // hand_item uses the same fingerprint-bound target-prioritization image.
    if (pr::base != s::base) return false;
    float total = 0;
    unsigned mobile_before = 0, support_before = 0;
    bool seen = false, self_eligible = false;
    for (unsigned i = 0; i < g.count; ++i) {
        Address a = g.members[i], b = read<Address>(a, 0xdc8);
        float morale = read<float>(b, 0x474);
        if (!std::isfinite(morale) || morale < 0 || morale > 100) return false;
        total += morale;
        if (a == actor) seen = true;
        auto key = actor_key(a);
        auto* member = row(units, key, seconds(w));
        if (!member || !admit(w, a, member->external) || (!s::free_slot(a) && !trip(key)) || morale < 55 ||
            !p::odds_ok(read<float>(b, 0x518), read<float>(b, 0x51c), read<float>(b, 0x528)))
            continue;
        if (seconds(w) - member->role_sample >= 1) {
            member->role = role_of(a);
            member->role_sample = seconds(w);
        }
        const auto& role = member->role;
        ++f.eligible;
        f.at |= role.at;
        if (!role.support) ++f.mobile;
        if (!seen) {
            if (role.support)
                ++support_before;
            else
                ++mobile_before;
        }
        if (a == actor) {
            f.self = role;
            self_eligible = true;
        }
    }
    f.slot = f.self.support ? f.mobile + support_before : mobile_before;
    return self_eligible && p::morale_ok(read<float>(read<Address>(actor, 0xdc8), 0x474), total / g.count, g.squad);
}

inline void observe(const World& w, Address a, XY center, p::Contacts& out, bool group_member) {
    auto sensor = read<Address>(a, 0xdd0);
    if (!sensor || read<Address>(sensor, 0x260) != a || !(read<unsigned>(a, 0xdb4) & 0x100)) return;
    if (read<s::Byte>(a, 0x30) & 4) {
        auto aggregate = read<Address>(a, 0x578);
        auto vitality = aggregate ? read<Address>(aggregate, 0x18) : 0;
        if (!vitality || (read<unsigned>(vitality, 0x174) & 3)) return;
    }
    auto list = s::span(sensor, 0x280);
    if (!list.valid(8, 4096)) {
        out.incomplete = true;
        return;
    }
    for (auto i = list.first; i != list.end; i += 8) {
        Address sub = read<Address>(i);
        if (!sub || read<s::Byte>(sub, 0x6c)) continue;
        const auto bits = read<unsigned>(sub, 0x10);
        if (!(bits & (1u << 28)) || (bits & (1u << 16))) continue;
        unsigned rel = read<unsigned>(sub, 0x14);
        if ((rel & 0x16u) != 2) continue;
        const bool visible = pr::is_visible(sub);
        auto stamp = read<unsigned>(sub, 0x64);
        if (!visible && !sim::recent(w, stamp, 12)) continue;
        const double seen = visible ? seconds(w) : double(stamp) / w.ticks;
        if (group_member && (rel & 0x180)) out.squad_threat = (std::max)(out.squad_threat, seen);
        XY xy = read<XY>(sub, 0x28) * (1.f / w.length);
        if (!p::finite(xy)) {
            out.incomplete = true;
            continue;
        }
        if (!p::within_radius(center, xy, 70)) continue; // Includes the entire threat halo around each candidate.
        auto enemy = read<Address>(sub, 0x18);
        bool armor = read<float>(sub, 0x58) > 0;
        if (visible && enemy && sim::has_actor(enemy)) {
            auto tag = s::native_string(enemy + 0xde8 + 0x40);
            armor = tag.substr(0, 4) == "tank" || classify::is_armored_vehicle_class(tag);
        }
        out.add({enemy, xy, seen, read<float>(sub, 0x48), armor, (rel & 0x180) != 0, visible});
    }
}
inline void sample(const World& w, const Group& g, GroupState& state) {
    double now = seconds(w);
    if (now - state.sampled < .5) return;
    state.contacts = {};
    state.sampled = now;
    for (unsigned i = 0; i < g.count; ++i)
        observe(w, g.members[i], g.center, state.contacts, true);
    s::actors(w, [&](Address a) {
        if (!sim::allies(s::base, g.key.owner, s::owner(a)) || !p::within_radius(g.center, position(a, w), 45)) return;
        if (std::find(g.members.begin(), g.members.begin() + g.count, a) == g.members.begin() + g.count)
            observe(w, a, g.center, state.contacts, false);
    });
}
inline Address flag_point(Address flag) {
    if (!sim::has_actor(flag)) return 0;
    auto point = auto_resupply::extender(flag, 0x10fbff0);
    auto engine = point ? read<Address>(point, 0xb58) : 0;
    if (!engine || read<Address>(engine) != s::base + 0x116c550 || read<Address>(engine, 8) != point) return 0;
    float progress = read<float>(engine, 0x98);
    if (!(progress >= 0 && progress <= 1) || !std::isfinite(read<float>(engine, 0x9c))) return 0;
    for (auto weight : read<std::array<float, 10>>(engine, 0x108))
        if (!std::isfinite(weight) || weight < 0) return 0;
    return point;
}
enum class FlagState : std::uint8_t {
    invalid,
    unfinished,
    captured
};
struct Side {
    bool mp;
    Address team;
};
inline Side side(Address a) {
    return {s::native<sim::Role>(0xc2b5d0)() && !s::native<sim::Role>(0xc2b620)(), auto_resupply::team(a)};
}
inline FlagState flag_state(Address flag, Address a, Side mine) {
    if (mine.mp && !mine.team) return FlagState::invalid;
    auto point = flag_point(flag);
    if (!point) return FlagState::invalid;
    return auto_resupply::safe_flag(flag, point, a, mine.team) ? FlagState::captured : FlagState::unfinished;
}
inline void sample_flags(const World& w) {
    if (seconds(w) - flag_sample < 1) return;
    flag_sample = seconds(w);
    flag_count = 0;
    s::actors(w, [&](Address a) {
        if (!flag_point(a)) return;
        Flag* slot = flag_count < flags.size() ? &flags[flag_count] : flags.append_slot();
        if (slot) {
            *slot = {s::identity(a), position(a, w)};
            ++flag_count;
        }
    });
}

inline bool choose_flag(const World& w, Address a, const Group& g, const GroupState& state, const Force& force,
                        Side mine, Target& out) {
    auto self = position(a, w), anchor = state.anchor.xy;
    const auto& contacts = state.contacts;
    const Key key = actor_key(a);
    const float cap_r = config::active().tuning.objectives_cap_radius_m;
    double now = seconds(w);
    float best = std::numeric_limits<float>::infinity();
    for (unsigned i = 0; i < flag_count; ++i) {
        const auto& flag = flags[i];
        if (!p::in_leash(Kind::cap, flag.xy, self, g.center, anchor, p::default_pursue_radius_m, cap_r)) continue;
        Address live = s::resolve(w, flag.entity);
        if (flag_state(live, a, mine) != FlagState::unfinished) continue;
        XY point = position(live, w);
        if (p::within_radius(self, point, 6.5f)) return false;
        if (!p::cap_allowed(contacts, point, self, g.center, anchor, p::cap_strength(force.mobile, force.eligible),
                            false, now, cap_r))
            continue;
        const Target candidate{Kind::cap, 0, flag.entity, point};
        if (cooling(key, candidate, now)) continue;
        unsigned movers = 0;
        bool mobile_moving = false;
        for (const auto& t : trips)
            if (t.actor.ptr && t.group == g.key && t.target.flag.matches(live)) {
                ++movers;
                mobile_moving |= !t.support;
            }
        // Reserve the mobile member's place even if support ticks first.
        const unsigned limit = force.self.support && force.mobile == 1 && !mobile_moving ? 3u : 4u;
        if (movers >= limit) continue;
        float d = p::distance(g.center, point);
        if (d < best) {
            best = d;
            out = candidate;
        }
    }
    return std::isfinite(best);
}
inline bool choose(const World& w, Address a, const Group& g, const GroupState& state, const Force& force,
                   Target& out) {
    auto self = position(a, w), anchor = state.anchor.xy;
    const auto& contacts = state.contacts;
    double now = seconds(w);
    bool viable_pursuit = false;
    const Key key = actor_key(a);
    auto available = [&](const p::Observation& o) {
        viable_pursuit = true;
        return !cooling(key, {Kind::pursue, o.enemy, {}, o.xy}, now);
    };
    if (const auto* enemy = contacts.pursue(self, g.center, anchor, force.mobile, force.at, now, available,
                                            config::active().tuning.objectives_pursue_radius_m)) {
        if (force.self.support ||
            (read<int>(read<Address>(a, 0xdc0), 0x24) && contacts.threatened(self, g.center, now)))
            return false;
        out = {Kind::pursue, enemy->enemy, {}, enemy->xy};
        return true;
    }
    if (viable_pursuit || contacts.incomplete || contacts.threatened(self, g.center, now) ||
        (!g.squad && force.self.medic) || (force.self.support && force.mobile >= 2))
        return false;
    sample_flags(w);
    if (!flag_count) return false;
    return choose_flag(w, a, g, state, force, side(a), out);
}
inline bool destination(const World& w, Address a, const Group& g, const GroupState& state, const Force& force,
                        const Target& target, s::Vec3& goal) {
    s::PathContext path(a);
    auto start = s::pos(a);
    Address place = target.kind == Kind::pursue ? pr::selected_place(a) : 0;
    float range = place ? s::native<float (*)(Address)>(0x81f660)(place) / w.length : 0;
    return p::destination(
        target.kind, target.xy, g.center, state.anchor.xy, range, force.slot,
        [&](XY spot) {
            if (target.kind == Kind::cap && !p::reaches_cap(position(a, w), spot, target.xy)) return false;
            for (unsigned i = 0; i < g.count; ++i)
                if (g.members[i] != a && p::distance(spot, position(g.members[i], w)) < 2.5f) return false;
            for (const auto& t : trips)
                if (t.actor.ptr && p::distance(spot, t.goal) < 2.5f) return false;
            goal = {spot.x * w.length, spot.y * w.length, start.z};
            return s::ground(w, goal) && s::corridor(w, path, start, goal);
        },
        config::active().tuning.objectives_pursue_radius_m, config::active().tuning.objectives_cap_radius_m);
}
inline bool scope_ok(const World& w) {
    return accepting.load() && s::accepting.load() && s::base && epoch.scene &&
           !epoch.changed(s::entity_scene(w), w.now);
}
inline bool still_allowed(const World& w, Address a, const Unit& u, const Trip& t, const Group& g) {
    return u.group == g.key && t.group == g.key && t.revision == u.revision && !g.blocked &&
           read<Address>(a, 0xdc8) == t.brain && admit(w, a, u.external);
}
// Called inside ai_support's serial, host-only Exec wrapper. The current move can be
// in phase zero, so free_slot() is an issuance check and cannot be used here.
inline bool permit(const World& w, Address a, Address order) {
    if (!scope_ok(w) || !concrete(a)) return false;
    Key key = actor_key(a);
    auto* t = trip(key);
    if (!t || t->order.get() != order) return false;
    Group g;
    if (!roster(w, a, g)) return false;
    const auto* u = t->unit;
    const auto* state = t->state;
    if (!u || !(u->key == key) || !still_allowed(w, a, *u, *t, g) || !state || !(state->key == g.key) ||
        state->anchor.pending || state->anchor.revision != t->revision)
        return false;
    return t->target.kind != Kind::cap ||
           flag_state(s::resolve(w, t->target.flag), a, side(a)) == FlagState::unfinished;
}
inline void record_issue(Kind kind) {
    auto n = kind == Kind::cap ? ++caps : ++pursues;
    if (s::log_sink && (n <= 5 || n % 128 == 0))
        s::log_sink("Tactical objective %s #%u", kind == Kind::cap ? "cap" : "pursue", n);
}
inline void issue(const World& w, Address a, const Group& g, GroupState& state, Unit& u, const Force& f,
                  const Target& target) {
    Trip* slot = nullptr;
    for (auto& t : trips) {
        if (!t.actor.ptr) {
            slot = &t;
            break;
        }
    }
    if (!slot) slot = trips.append_slot();
    if (!slot) return;
    double now = seconds(w);
    auto* cooldown = reserve(u.key, target, now);
    if (!cooldown) return;
    s::Vec3 dest{};
    if (!destination(w, a, g, state, f, target, dest)) {
        cooldown->until = now + p::cooldown(p::Result::failed);
        return;
    }
    XY goal{dest.x / w.length, dest.y / w.length};
    float left = p::distance(position(a, w), goal);
    if (target.kind == Kind::pursue && left <= 3) {
        cooldown->until = now + p::cooldown(p::Result::arrived);
        return;
    }
    Group current;
    if (!scope_ok(w) || !roster(w, a, current) || !(current.key == g.key) || current.blocked ||
        !admit(w, a, u.external) || !s::free_slot(a) ||
        (target.kind == Kind::cap && flag_state(s::resolve(w, target.flag), a, side(a)) != FlagState::unfinished)) {
        *cooldown = {};
        return;
    }
    slot->actor = u.key;
    slot->group = g.key;
    slot->brain = read<Address>(a, 0xdc8);
    slot->revision = state.anchor.revision;
    slot->target = target;
    slot->goal = goal;
    slot->progress = {now, now, left};
    slot->backoff = cooldown;
    slot->support = f.self.support;
    slot->review_at = now + 1.5;
    slot->unit = &u;
    slot->state = &state;
    u.arrived_flag = {};
    u.idle.interrupt();
    if (s::move(a, dest, permit, slot->order))
        record_issue(target.kind);
    else
        stop(*slot, now, p::Result::failed);
}
inline bool finish_capture(GroupState& state, Unit& u, FlagState status, XY flag) {
    if (status != FlagState::captured || state.anchor.pending || !(state.key == u.group) ||
        state.anchor.revision != u.revision)
        return false;
    state.anchor.center(flag);
    u.revision = state.anchor.revision;
    u.arrived_flag = {};
    return true;
}
inline void monitor(const World& w, Address a, GroupState& state, Unit& u, Trip& t) {
    auto self = position(a, w);
    bool on_flag = false;
    double now = seconds(w);
    if (t.target.kind == Kind::cap) {
        Address live = s::resolve(w, t.target.flag);
        FlagState status = flag_state(live, a, side(a));
        if (status == FlagState::invalid) {
            stop(t, now, p::Result::failed);
            return;
        }
        XY point = position(live, w);
        if (finish_capture(state, u, status, point)) {
            stop(t, now, p::Result::arrived);
            return;
        }
        on_flag = p::within_radius(self, point, 6.5f);
    }
    float left = p::distance(self, t.goal);
    auto result = t.progress.update(now, left, left <= 3 || on_flag, s::current_order(a, t.order.get()));
    if (result != p::Result::moving) {
        if (result == p::Result::arrived && on_flag) u.arrived_flag = t.target.flag;
        stop(t, now, result);
    }
}
inline bool continue_trip(const World& w, Address a, const Group& g, GroupState& state, const Trip& t) {
    Force force;
    if (!strength(w, a, g, force)) return false;
    sample(w, g, state);
    const auto& contacts = state.contacts;
    const auto target = t.target.xy;
    const auto now = seconds(w);
    if (contacts.incomplete) return false;
    if (t.target.kind == Kind::pursue)
        return !force.self.support &&
               p::manageable(Kind::pursue, contacts.danger(target, 20, now), force.mobile, force.at);
    const auto self = position(a, w);
    const auto& tuning = config::active().tuning;
    return !(force.self.support && (force.mobile >= 2 || (!g.squad && force.self.medic))) &&
           !contacts.pursue(self, g.center, state.anchor.xy, force.mobile, force.at, now,
                            tuning.objectives_pursue_radius_m) &&
           p::cap_allowed(contacts, target, self, g.center, state.anchor.xy,
                          p::cap_strength(force.mobile, force.eligible), false, now, tuning.objectives_cap_radius_m);
}
// Orders often finish before ownership flips. Only the original actor still on
// that point, under the same player intent, may complete the anchor transition.
inline void await_capture(const World& w, Address a, GroupState& state, Unit& u, Side mine) {
    auto live = s::resolve(w, u.arrived_flag);
    if (!live || !p::within_radius(position(a, w), position(live, w), 6.5f)) {
        u.arrived_flag = {};
        return;
    }
    auto status = flag_state(live, a, mine);
    if (status == FlagState::invalid)
        u.arrived_flag = {};
    else
        finish_capture(state, u, status, position(live, w));
}
inline void evaluate(const World& w, Address a, bool external) {
    double now = seconds(w);
    Key key = actor_key(a);
    auto* t = trip(key);
    if (!concrete(a)) {
        if (t) stop(*t, now, p::Result::arrived);
        return;
    }
    auto* u = row(units, key, now);
    Group g;
    if (!u || !roster(w, a, g)) {
        if (t) stop(*t, now, p::Result::arrived);
        if (u) {
            u->idle.interrupt();
            u->arrived_flag = {};
        }
        return;
    }
    u->external = external;
    auto* state = g.squad ? row(squads, g.key, now) : row(solos, g.key, now);
    if (!state) {
        u->idle.interrupt();
        u->arrived_flag = {};
        if (t) stop(*t, now, p::Result::arrived);
        return;
    }
    if (!g.blocked || g.intent || state->anchor.initialized)
        state->anchor.observe(g.center, g.intent, g.free && !g.blocked);
    if (!(u->group == g.key) || u->revision != state->anchor.revision) {
        u->arrived_flag = {};
        u->idle.interrupt();
        u->group = g.key;
        u->revision = state->anchor.revision;
    }
    if (g.blocked || state->anchor.pending || !admit(w, a, external) ||
        (t && (t->revision != u->revision || !(t->group == g.key) || t->brain != read<Address>(a, 0xdc8)))) {
        if (t) stop(*t, now, p::Result::arrived);
        u->arrived_flag = {};
        u->idle.interrupt();
        return;
    }
    if (t) {
        monitor(w, a, *state, *u, *t);
        if (t->actor.ptr && now >= t->review_at) {
            t->review_at = now + 1.5;
            if (!continue_trip(w, a, g, *state, *t)) stop(*t, now, p::Result::arrived);
        }
        return;
    }
    if (!s::free_slot(a)) {
        u->idle.interrupt();
        u->arrived_flag = {};
        return;
    }
    if (!u->idle.ready(now, read<Address>(read<Address>(a, 0xdc8), 0x3b8) != 0)) return;
    if (u->arrived_flag.ptr) await_capture(w, a, *state, *u, side(a));
    Force f;
    if (!strength(w, a, g, f)) return;
    sample(w, g, *state);
    Target target;
    if (choose(w, a, g, *state, f, target)) issue(w, a, g, *state, *u, f, target);
}
} // namespace detail

inline void tick(Address actor, bool external_busy = false) {
    s::World w{};
    if (!detail::accepting.load() || !config::active().modules.tactical_objectives || !s::accepting.load() ||
        !s::base || !s::callback(actor, w) || !runtime::actor_ai_allowed(actor))
        return;
    auto scene = s::entity_scene(w);
    if (!scene) return;
    detail::advance(scene, w);
    detail::evaluate(w, actor, external_busy);
}
inline bool is_busy(Address actor) {
    s::World w{};
    if (!detail::accepting.load() || !s::accepting.load() || !s::base || !s::callback(actor, w) ||
        !detail::scope_ok(w) || !detail::concrete(actor))
        return false;
    const auto* t = detail::trip(detail::actor_key(actor));
    Address order = t ? t->order.get() : 0;
    return order && t->order.watch->permit == detail::permit && s::current_order(actor, order) &&
           detail::permit(w, actor, order);
}
// reset/clear_state are serial (or after draining callbacks). shutdown can be requested cross-thread.
inline void clear_state() {
    detail::clear();
}
inline void reset() {
    clear_state();
    detail::pursues = 0;
    detail::caps = 0;
    detail::accepting.store(true);
    coord::instance().register_provider(coord::Module::tactical_objectives, coord::Priority::tactical_objectives,
                                        &is_busy, &detail::preempt_actor);
}
inline void shutdown() {
    detail::accepting.store(false);
}
inline void after_hooks_removed() {
    shutdown();
    detail::release_tables();
}
inline unsigned stat_pursues() {
    return detail::pursues.load();
}
inline unsigned stat_caps() {
    return detail::caps.load();
}
} // namespace goh::tactical_objectives
