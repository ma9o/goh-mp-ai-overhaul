#pragma once

#include "tactical_infantry_module.hpp"

namespace goh::combat_logistics {
namespace s = ai_support;
namespace sim = auto_smoke;
using runtime::Address;
using runtime::Byte;
using runtime::read;
using runtime::Tick;

inline constexpr bool runtime_allowed = false;
inline constexpr unsigned hook_count = 1;
inline std::atomic<unsigned> stat_fast_swaps{}, stat_missile_holds{};

namespace detail {
using Swap = Byte (*)(Address, Address);
inline runtime::HookGate gate{};
inline Swap original{};
inline Address image{}, scene{};
inline Tick clock{};
struct Attempt {
    s::Identity soldier{};
    Tick at{};
};
inline store::GrowableTable<Attempt, 32, 4096> attempts{};
inline Address actor_context(sim::World& w) {
    const auto actor = s::current_actor();
    return s::callback(actor, w) ? actor : 0;
}
inline int reserve_rounds(Address actor, Address def) {
    return s::loose_ammo(actor, def).total;
}
struct Ops {
    Address (*context)(sim::World&) = actor_context;
    Address (*resolve)(Address, Address) = [](Address base, Address rva) { return base + rva; };
    int (*reserve)(Address, Address) = reserve_rounds;
};
inline Ops ops{};
template <class Fn> inline Fn native(Address rva) {
    return reinterpret_cast<Fn>(ops.resolve(image, rva));
}

inline bool authorized(Address a, Address b) {
    const auto flags = read<unsigned>(b, 0x39c), policy = read<unsigned>(b, 0x398);
    return read<Address>(a, 0xdc8) == b &&
           s::autonomous_if(a, true, [](unsigned pol, unsigned state) { return s::policy_allows(pol, state, true); }) &&
           (((policy >> 10) & 3u) != 2u || (flags & (1u << 18))) && !tactical_infantry::detail::protected_order(b, a) &&
           !native<s::Test>(0x7e05b0)(a) && !native<s::Test>(0x91b190)(b);
}
inline Address fresh_hostile(const sim::World& w, Address subject, float seconds) {
    if (!subject || read<Byte>(subject, 0x6c)) return 0;
    const auto flags = read<unsigned>(subject, 0x10), relation = read<unsigned>(subject, 0x14);
    if ((relation & 0x1e) != 2 || (flags & (1u << 16)) || !(flags & (1u << 28)) ||
        (!(flags & (1u << 29)) && !sim::recent(w, read<Tick>(subject, 0x64), seconds)) ||
        !sim::finite(read<sim::Vec3>(subject, 0x28)))
        return 0;
    const auto target = read<Address>(subject, 0x18);
    if (!sim::has_actor(target) || !(read<unsigned>(target, 0xdb4) & 0x100)) return 0;
    return target;
}
inline bool operational_vehicle(Address target) {
    return (read<Byte>(target, 0x30) & 8) && !s::has_destruction_tag(target);
}
inline bool loaded_sidearm(Address actor) {
    const auto box = s::box(actor);
    const auto items = s::span(box, 0x10);
    if (!box || !items.valid(sizeof(Address), 2048)) return false;
    for (auto p = items.first; p != items.end; p += sizeof(Address)) {
        const auto item = read<Address>(p);
        if (!item || read<Address>(item, 0x30) != box || read<int>(item, 0x48) <= 0 || read<Address>(item, 0x38) ||
            read<Address>(item, 0x10) != read<Address>(item, 0x18))
            continue;
        const auto def = read<Address>(item, 0x40);
        for (auto tag : {"pistol", "revolver", "smg", "shortsmg"})
            if (classify::def_has_tag(def, tag) && auto_resupply::loaded_chamber(item)) return true;
    }
    return false;
}
inline bool urgent(const sim::World& w, Address actor, Address subject, Address target) {
    if (!target) return false;
    const auto weapon = native<s::Unary>(runtime::table().fn.weapon_select)(read<Address>(actor, 0xdd8));
    const auto def = read<Address>(weapon, 0x98);
    if (!def || read<Address>(weapon, 0x90) != actor) return false;
    if (operational_vehicle(target)) {
        const auto hand = native<s::Unary>(runtime::table().fn.right_hand_definition)(actor);
        return !classify::def_is_at_weapon(def) && !classify::def_is_at_weapon(hand) &&
               classify::inspect_carried_at_inventory(actor, w.base).has_at_weapon;
    }
    if (!(read<Byte>(target, 0x30) & 4)) return false;
    const auto vit = read<Address>(read<Address>(target, 0x578), 0x18);
    if (!vit || (read<unsigned>(vit, 0x174) & 3)) return false;
    if (classify::def_has_tag(def, "rocketlauncher")) return true;
    const auto charger = read<Address>(weapon, 0xb0);
    // 0x820C50 returns true for dummy chargers too. 0x95C530 checks both reload bits.
    if (!charger || (read<Byte>(charger, 0x14) & 0x14) || native<s::Test>(0x820ce0)(charger) ||
        !native<s::Test>(0x820c50)(charger) || !auto_resupply::weapon_definition(def))
        return false;
    const int reserve = ops.reserve(actor, def);
    if (reserve <= 0) return reserve == 0;
    const float reach = 15.f * w.length;
    return sim::distance2(s::pos(actor), read<sim::Vec3>(subject, 0x28)) < reach * reach &&
           (classify::def_has_tag(def, "mgun") || classify::def_has_tag(def, "rifle")) && loaded_sidearm(actor);
}
inline bool record_attempt(const sim::World& w, Address a) {
    Attempt* free = nullptr;
    for (auto& row : attempts) {
        const bool recent = row.soldier.ptr && w.now - row.at < w.duration(1.f);
        if (row.soldier.matches(a)) {
            if (recent) return false;
            free = &row;
            break;
        }
        if (!recent && !free) free = &row;
    }
    if (!free) free = store::try_append_slot(attempts);
    if (!free) return false;
    *free = {s::identity(a), w.now};
    return true;
}
inline bool adjust(const sim::World& w, Address a, Address b) {
    const auto current_scene = s::entity_scene(w);
    if (!current_scene) return false;
    if (scene != current_scene || w.now < clock) attempts.clear();
    scene = current_scene;
    clock = w.now;
    const auto stow = read<Tick>(b, 0x3a4), lock = read<Tick>(b, 0x47c);
    bool hold = stow && (stow <= w.now || stow - w.now <= w.duration(.5f));
    const bool locked = w.now && lock > w.now;
    if (!hold && !locked) return false;
    const auto sensor = read<Address>(a, 0xdd0);
    const auto list = s::span(sensor, 0x280);
    if (!sensor || read<Address>(sensor, 0x260) != a || !list.valid(sizeof(Address), 4096)) return false;
    if (hold) {
        const auto hand = native<s::Unary>(runtime::table().fn.right_hand_definition)(a);
        hold = hand && !sim::IsPureSmokeDefinition(hand) && native<s::Test>(0x95e590)(b);
    }
    const auto selected = read<Address>(b, 0x3b8);
    bool bypass = false;
    for (auto p = list.first; p != list.end; p += sizeof(Address)) {
        const auto subject = read<Address>(p);
        if (hold && operational_vehicle(fresh_hostile(w, subject, 10)) &&
            s::within(s::pos(a), read<sim::Vec3>(subject, 0x28), 60.f * w.length)) {
            runtime::write<Tick>(b, 0x3a4, w.now + w.duration(2.5f));
            ++stat_missile_holds;
            hold = false;
        }
        if (locked && selected && subject == selected && !bypass)
            bypass = urgent(w, a, subject, fresh_hostile(w, subject, 5));
    }
    if (!bypass || !record_attempt(w, a)) return false;
    runtime::write<Tick>(b, 0x47c, w.now);
    return true;
}
inline Byte swap(Address brain, Address ctx) {
    runtime::HookGuard frame(gate);
    sim::World w{};
    const auto a = gate.active() && config::active().modules.fast_weapon_swap && ctx ? ops.context(w) : 0;
    const bool bypass = a && authorized(a, brain) && adjust(w, a, brain);
    const auto before = bypass ? read<Address>(ctx, 8) : 0;
    const auto result = original(brain, ctx);
    // Stowing a missile or retrieving a dropped gun can also return 1. Only
    // mount_weapon writes a fresh eWeaponTake order to this context slot.
    if (bypass && result) {
        const auto issued = read<Address>(ctx, 8);
        if (issued != before && read<Address>(issued) == image + 0x110aa28) ++stat_fast_swaps;
    }
    return result;
}
} // namespace detail
template <class Create, class Remove> bool prepare_hooks(Address base, const char* digest, Create create, Remove) {
    if (!base || !digest || std::strcmp(digest, runtime::kBuild26734796Sha256) != 0 || detail::original) return false;
    void* trampoline = nullptr;
    if (create(reinterpret_cast<void*>(base + runtime::table().hook.human_weapon_swap),
               reinterpret_cast<void*>(detail::swap), &trampoline))
        return false;
    detail::original = reinterpret_cast<detail::Swap>(trampoline);
    detail::image = base;
    detail::gate.activate();
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
    detail::original = nullptr;
    detail::attempts.release();
    detail::image = detail::scene = detail::clock = 0;
    detail::ops = {};
    stat_fast_swaps = stat_missile_holds = 0;
}
} // namespace goh::combat_logistics
