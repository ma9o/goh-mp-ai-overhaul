#pragma once

#include "target_prioritization_module.hpp"
#include "wave5_hooks.hpp"

namespace goh::combat_orders {
namespace s = ai_support;
namespace sim = auto_smoke;
namespace pr = target_prio::detail;
using runtime::Address;
using runtime::Byte;
using runtime::read;
using runtime::Tick;

inline constexpr bool runtime_allowed = false;
inline constexpr unsigned hook_count = 2;
inline std::atomic<unsigned> stat_advance_unfreeze{}, stat_attack_persists{}, stat_melee_prevented{};

enum class AdvanceKind {
    human,
    vehicle,
    cannon
};
inline bool should_unfreeze_advance(const sim::World& w, AdvanceKind kind, int state, int level, Address child,
                                    Tick entered, Tick fired, Tick deadline) {
    if (w.now < fired) return false;
    switch (kind) {
        case AdvanceKind::human:
            return (state == 3 || state == 4) && level == 2 && !child && w.now >= entered &&
                   w.now - entered >= w.duration(1.5f) && w.now - fired >= w.duration(2);
        case AdvanceKind::vehicle: return (state == 2 || state == 3) && level == 2 && w.now - fired >= w.duration(3);
        case AdvanceKind::cannon: return level == 1 && deadline && w.now >= deadline && w.now - fired >= w.duration(4);
    }
    return false;
}

namespace detail {
inline runtime::HookGate gate{};
inline runtime::HookBatch<hook_count> hooks{};
inline Address image{};
using Advance = void (*)(Address);
using Attack = std::uint64_t (*)(Address);
inline Advance original_advance{};
inline Attack original_attack{};

inline Address actor_context(sim::World& w) {
    const auto actor = s::current_actor();
    return s::callback(actor, w) ? actor : 0;
}
inline bool hard_blocked(const sim::World& w, Address actor, Address subject) {
    if (pr::scope.actor != actor || pr::depth != 1 || pr::base != image) return false;
    const auto target = read<Address>(subject, 0x18);
    const auto from = pr::raised_point(read<pr::Vec3>(actor, 0x74), actor, pr::RayPoint::muzzle, w.length);
    const auto to = pr::raised_point(read<pr::Vec3>(subject, 0x28), target, pr::RayPoint::target, w.length);
    if (!pr::finite(from) || !pr::finite(to) || !(pr::distance(from, to) > 0)) return false;
    bool evaluated = false;
    // The native human 0x8E7560 samples aim points and calls the shooter's pose trial.
    // Use the existing RNG-free terrain/solid-volume query; unknown rays retain stock.
    return !pr::segment({subject, target, 0, 0, from, to}, &evaluated, true) && evaluated;
}
struct Ops {
    Address (*context)(sim::World&) = actor_context;
    Address (*resolve)(Address, Address) = [](Address base, Address rva) { return base + rva; };
    bool (*blocked)(const sim::World&, Address, Address) = hard_blocked;
};
inline Ops ops{};
template <class Fn> inline Fn native(Address rva) {
    return reinterpret_cast<Fn>(ops.resolve(image, rva));
}

inline bool operational(Address actor) {
    if (!s::live_entity(actor) || !sim::has_actor(actor) || !(read<unsigned>(actor, 0xdb4) & 0x100)) return false;
    if (read<Byte>(actor, 0x30) & 4) {
        const auto vit = read<Address>(read<Address>(actor, 0x578), 0x18);
        return vit && !(read<Byte>(vit, 0x174) & 3);
    }
    return !s::has_destruction_tag(actor);
}
struct Context {
    sim::World w{};
    Address actor{}, brain{}, weaponry{};
    bool human{};
};
inline bool context(Address order, Address order_vt, Context& c) {
    if (!gate.active() || !runtime::is_host_main_thread(image) || !runtime::is_plausible_ptr(order)) return false;
    c.actor = ops.context(c.w);
    if (!c.actor || read<Address>(order) != image + order_vt || read<Address>(order, 8) != c.actor + 0xaf0)
        return false;
    c.brain = read<Address>(c.actor, 0xdc8);
    c.human = read<Address>(c.brain) == image + runtime::table().vt.human_brain;
    c.weaponry = read<Address>(c.actor, 0xdd8);
    if (!c.brain || !read<Address>(c.actor, 0x578) ||
        (!c.human && read<Address>(c.brain) != image + runtime::table().vt.vehicle_brain) ||
        !(read<Byte>(c.actor, 0x30) & (c.human ? 4 : 8)) || !operational(c.actor) || !c.weaponry ||
        !read<Address>(c.actor, 0xde0) || !read<std::uint16_t>(c.actor, 0xdaa) || s::owner(c.actor) >= 18 ||
        (read<unsigned>(c.actor, 0x1c0) & (1u << 28)) || read<Address>(c.brain, 0x38) != c.actor ||
        (read<unsigned>(c.brain, 0x398) & 3) > 1 || runtime::is_inhibited_or_direct_control(c.actor, c.brain))
        return false;
    auto node = read<Address>(c.brain, 0xf0);
    for (unsigned i = 0; node && i < 32; ++i) {
        if (node == order) return true;
        node = sim::orders::active_child(image, node);
    }
    return false;
}
inline bool observed(Address actor, Address target) {
    auto owner = static_cast<Byte>(s::owner(actor));
    return native<Byte (*)(Address, const Byte*)>(0x809b30)(target + 0xbe0, &owner) != 0;
}
inline bool fog_enabled() {
    return native<Byte (*)()>(0xc2b670)() != 0;
}
inline bool hostile_subject(Address subject) {
    if (!subject || read<Byte>(subject, 0x6c)) return false;
    const auto flags = read<unsigned>(subject, 0x10);
    return (read<unsigned>(subject, 0x14) & 0x1e) == 2 && !(flags & (1u << 16)) && (flags & (1u << 28));
}
inline bool human_threat(const Context& c) {
    const auto sub = read<Address>(c.brain, 0x3b8);
    if (!hostile_subject(sub) || !operational(read<Address>(sub, 0x18))) return false;
    const bool visible = (read<unsigned>(sub, 0x10) & (1u << 29)) != 0;
    if (!visible && !sim::recent(c.w, read<Tick>(sub, 0x64), 1.5f)) return false;
    const float range = read<float>(c.brain, 0x14c);
    if (range == 0 || !native<s::Unary>(runtime::table().fn.weapon_select)(c.weaponry) ||
        !engine_fixes::firing_permitted(c.brain) || (visible && native<s::BinaryTest>(0x95d710)(c.brain, sub)))
        return false;
    const auto point = read<sim::Vec3>(sub, 0x28);
    if (std::isfinite(range) && range > 0 && sim::finite(point) && !s::within(s::pos(c.actor), point, range))
        return false;
    return !visible || !ops.blocked(c.w, c.actor, sub);
}
inline bool vehicle_threat(const Context& c) {
    const float range = read<float>(c.brain, 0x14c);
    const auto places = s::span(c.weaponry, 0x30);
    const auto sensor = read<Address>(c.actor, 0xdd0);
    const auto subjects = s::span(sensor, 0x280);
    const auto self = s::pos(c.actor);
    if (!sim::finite(self) || !std::isfinite(range) || range < 0 || !places.valid(sizeof(Address), 1024) ||
        read<Address>(sensor, 0x260) != c.actor || !subjects.valid(sizeof(Address), 4096))
        return true;
    for (auto p = subjects.first; p != subjects.end; p += sizeof(Address)) {
        const auto sub = read<Address>(p);
        if (hostile_subject(sub) && (read<unsigned>(sub, 0x10) & (1u << 29)) && operational(read<Address>(sub, 0x18)) &&
            s::within(self, read<sim::Vec3>(sub, 0x28), range))
            return true;
    }
    for (auto p = places.first; p != places.end; p += sizeof(Address)) {
        const auto weapon = read<Address>(p), target = pr::active_entity(weapon), aimer = read<Address>(weapon, 0xa8);
        if (!target || !aimer || read<Address>(weapon, 0x90) != c.actor) continue;
        const auto state = read<int>(aimer, 0x18);
        if (state < 0 || state >= 7 || !operational(target) || !observed(c.actor, target)) continue;
        if (!read<Address>(weapon, 0x98) || !read<Address>(weapon, 0xa0) || !read<Address>(weapon, 0xb0)) return true;
        const float reach = native<float (*)(Address, Address)>(runtime::table().fn.weapon_max_range)(weapon, target);
        if (!std::isfinite(reach) || s::within(self, s::pos(target), reach)) return true;
    }
    return false;
}
inline bool cannon_threat(const Context& c) {
    const auto slots = s::span(c.brain, 0x3a0);
    if (!slots.valid(sizeof(Address), 4096)) return true;
    const auto sub = slots.first == slots.end ? 0 : read<Address>(slots.first);
    if (!sub) return false;
    // 0x8E7560 compares against 75% of brain slot +A8. The engine caches the
    // same slot's result at +14C in 0x91C96A; avoid repeating the inventory scan.
    const float range = read<float>(c.brain, 0x14c);
    const float distance = std::sqrt(sim::distance2(s::pos(c.actor), read<sim::Vec3>(sub, 0x28)));
    return !std::isfinite(range) || !std::isfinite(distance) || distance < .75f * range;
}
inline void hook_advance(Address order) {
    runtime::HookGuard guard(gate);
    original_advance(order);
    Context c{};
    if (!config::active().modules.advance_responsive || !context(order, runtime::table().vt.advance_order, c)) return;
    const bool cannon = native<s::Unary>(0x945b60)(c.actor) != 0;
    const auto kind = cannon ? AdvanceKind::cannon : c.human ? AdvanceKind::human : AdvanceKind::vehicle;
    const int level = read<int>(order, 0x40);
    if (!should_unfreeze_advance(c.w, kind, read<int>(order, 0x3c), level, read<Address>(order, 0x30),
                                 read<Tick>(order, 0x58), read<Tick>(c.brain, 0x18), read<Tick>(order, 0x6c)))
        return;
    if (cannon) {
        if (native<s::Test>(0x8e7090)(order) && (c.human ? human_threat(c) : cannon_threat(c))) return;
    } else if (c.human ? human_threat(c) : vehicle_threat(c))
        return;
    runtime::write(order, 0x40, level - 1);
    ++stat_advance_unfreeze;
}

struct AttackWatch {
    Address order{};
    s::Identity actor{}, target{};
    Tick seen{}, native_stamp{}, virtual_stamp{};
};
inline store::GrowableTable<AttackWatch, 32, 2048> watches{};
inline Address scene{};
inline Tick clock{};
inline AttackWatch* find_watch(const Context& c, Address order, Address target, bool visible) {
    const auto epoch = s::entity_scene(c.w);
    if (!epoch || epoch != scene || c.w.now < clock) watches.clear();
    scene = epoch;
    clock = c.w.now;
    if (!epoch) return nullptr;
    AttackWatch* free = nullptr;
    for (auto& row : watches) {
        if (row.order == order) {
            if (row.actor.matches(c.actor) && row.actor.mid == read<std::uint16_t>(c.actor, 0xdaa) &&
                row.target.matches(target) && row.target.mid == read<std::uint16_t>(target, 0xdaa) &&
                c.w.now - row.seen < c.w.duration(14))
                return &row;
            row = {};
        }
        if (!free && (!row.order || c.w.now - row.seen >= c.w.duration(14))) free = &row;
    }
    if (!visible) return nullptr;
    return free ? free : store::try_append_slot(watches);
}

// Both deadlines belong to the native order. Lend the adjusted tick only across
// the original dispatch, preserving any different value written there. The next
// call immediately regains stock timing on target loss, dry fire or a mode change.
class StockTimerLoan {
    Address owner_order{};
    unsigned offset{};
    Tick prior_tick{}, temporary{};

protected:
    void lend(Address o, unsigned field, Tick value) {
        owner_order = o;
        offset = field;
        prior_tick = read<Tick>(owner_order, offset);
        temporary = value;
        runtime::write(owner_order, offset, temporary);
    }

public:
    StockTimerLoan() = default;
    ~StockTimerLoan() {
        if (owner_order && read<Tick>(owner_order, offset) == temporary)
            runtime::write(owner_order, offset, prior_tick);
    }
    StockTimerLoan(const StockTimerLoan&) = delete;
    StockTimerLoan& operator=(const StockTimerLoan&) = delete;
};

struct AttackMemory : StockTimerLoan {
    explicit AttackMemory(Address o) {
        Context c{};
        if (!config::active().modules.attack_order_polish || !context(o, runtime::table().vt.attack_order, c) ||
            !fog_enabled())
            return;
        const auto target = read<Address>(o, 0x58);
        const bool live = operational(target), visible = live && observed(c.actor, target);
        auto* row = find_watch(c, o, live ? target : 0, visible);
        if (!row) return;
        if (!live) {
            *row = {};
            return;
        }
        const auto saved = read<Tick>(o, 0xec);
        if (visible) {
            *row = {o, s::identity(c.actor), s::identity(target), c.w.now, saved, c.w.now};
            return;
        }
        if (saved != row->native_stamp && saved != row->seen) {
            *row = {};
            return;
        }
        if (c.w.now - row->virtual_stamp >= c.w.duration(4.5f)) {
            row->virtual_stamp = c.w.now;
            ++stat_attack_persists;
        }
        if (row->virtual_stamp > saved && c.w.now - saved >= c.w.duration(4.5f)) lend(o, 0xec, row->virtual_stamp);
    }
};

inline bool loaded_firearm(Address weapon, Address actor) {
    if (!weapon || read<Address>(weapon, 0x90) != actor) return false;
    const auto def = read<Address>(weapon, 0x98), charger = read<Address>(weapon, 0xb0);
    if (!charger || (read<Byte>(charger, 0x14) & 0x14)) return false;
    bool firearm = false;
    const bool excluded = classify::def_any_tag(def, [&](std::string_view tag) {
        firearm |= tag == "rifle" || tag == "smg" || tag == "shortsmg" || tag == "pistol" || tag == "revolver" ||
                   tag == "mgun";
        return tag == "rocketlauncher" || tag == "missile" || tag == "grenade" || tag == "melee" || tag == "knife";
    });
    return firearm && !excluded && !native<s::Test>(0x820ce0)(charger) && !native<s::Test>(0x820c50)(charger);
}
struct MeleeTimer : StockTimerLoan {
    explicit MeleeTimer(Address order) {
        Context c{};
        if (!config::active().modules.attack_order_polish || !context(order, runtime::table().vt.attack_order, c) ||
            !c.human || !engine_fixes::firing_permitted(c.brain))
            return;
        const auto deadline = read<Tick>(order, 0xf8);
        const auto target = read<Address>(order, 0x58);
        const float radius = 6.f * c.w.length;
        if (deadline && c.w.now >= deadline && target != c.actor && operational(target) &&
            (read<Byte>(target, 0x30) & 4) && s::owner(target) < 18 &&
            !sim::allies(image, s::owner(c.actor), s::owner(target)) && observed(c.actor, target) &&
            sim::distance2(s::pos(c.actor), s::pos(target)) < radius * radius &&
            loaded_firearm(native<s::Unary>(runtime::table().fn.weapon_select)(c.weaponry), c.actor)) {
            lend(order, 0xf8, c.w.now + c.w.duration(1));
            ++stat_melee_prevented;
        }
    }
};
inline std::uint64_t hook_attack(Address order) {
    runtime::HookGuard guard(gate);
    MeleeTimer timer(order);
    return original_attack(order);
}
} // namespace detail

inline void deactivate() {
    detail::gate.deactivate();
}
inline unsigned inflight_count() {
    return detail::gate.count();
}
inline void after_hooks_removed() {
    using namespace detail;
    deactivate();
    hooks.reset();
    original_advance = nullptr;
    original_attack = nullptr;
    watches.release();
    image = scene = clock = 0;
    ops = {};
}
template <class Create, class Remove>
inline bool prepare_hooks(Address base, const char* digest, Create create, Remove remove) {
    using namespace detail;
    if (!base || !digest || std::strcmp(digest, runtime::kBuild26734796Sha256) != 0 || !hooks.empty()) return false;
    image = base;
    const auto& h = runtime::table().hook;
    if (!hooks.add(base, h.advance_engagement, hook_advance, original_advance, create) ||
        !hooks.add(base, h.attack_tick, hook_attack, original_attack, create)) {
        if (hooks.rollback(remove)) after_hooks_removed();
        return false;
    }
    gate.activate();
    return true;
}
} // namespace goh::combat_orders
