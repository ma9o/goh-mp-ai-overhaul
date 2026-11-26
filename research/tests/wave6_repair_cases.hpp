#pragma once

#include "../../mods/tank_auto_repair_module.hpp"
#include "vehicle_fixture_26734796.hpp"

namespace goh::test::wave6_repair {
namespace r = tank_auto_repair;
namespace d = r::detail;
using runtime::Byte;
using vehicle::Address;
using vehicle::Memory;

struct Fixture {
    vehicle::FakeImage image{};
    vehicle::FakeTank gun{};
    vehicle::FakeHuman gunner{}, loader{};
    Memory<0x80> agg{}, subject{}, scene{}, callback{}, emit{}, destruct_vt{};
    Memory<0x300> sensor{};
    Memory<0xC40> cannon{};
    Memory<0xD0> inventory{}, box{}, item{};
    Memory<0x1200> kit_def{};
    Memory<0x60> kit_tag{};
    std::array<Address, 3> entities{};
    std::array<Address, 1> subjects{}, extenders{}, kits{};
    std::vector<int> emitted;
    bool excluded{}, remote{}, worker{};
    unsigned chassis_checks{};
    d::World world{};

    Fixture() {
        gun.setup(image.base(), 10, 1, 0);
        gunner.setup(image.base(), 11, 2, 0, gun.actor.ptr());
        loader.setup(image.base(), 12, 3, 0, gun.actor.ptr());
        gun.set_slot_count(2);
        gun.set_slot(0, "gunner", "crew", 0x1000, "", "", gunner.actor.ptr(), 11);
        gun.set_slot(1, "charger", "crew", 0x1000, "", "", loader.actor.ptr(), 12);
        gun.occ_ids[1] = 1;
        gun.placer.put<Address>(0x10, reinterpret_cast<Address>(gun.occ_ids.data()));
        gun.placer.put<Address>(0x18, reinterpret_cast<Address>(gun.occ_ids.data() + 2));
        gun.actor.put<Byte>(0xDAD, 1);
        gun.actor.put<Address>(0x578, agg.ptr());
        gun.actor.put<Address>(0xDD0, sensor.ptr());
        subjects[0] = subject.ptr();
        sensor.put<Address>(0x260, gun.actor.ptr());
        sensor.put<Address>(0x280, reinterpret_cast<Address>(subjects.data()));
        sensor.put<Address>(0x288, reinterpret_cast<Address>(subjects.data() + 1));
        subject.put<unsigned>(0x14, 2);
        subject.put<unsigned>(0x10, (1u << 29) | (1u << 28));
        subject.put<auto_smoke::Vec3>(0x28, {26, 0, 0});
        cannon.put<Address>(0, image.base() + runtime::table().vt.cannon_ext);
        cannon.put<Address>(0x18, gun.actor.ptr());
        extenders[0] = cannon.ptr();
        kit_tag.ename(0, "repair_kit");
        kits[0] = item.ptr();
        kit_def.put<Address>(0x11e0, kit_tag.ptr());
        kit_def.put<Address>(0x11e8, kit_tag.ptr() + 0x60);
        item.put<Address>(0x30, box.ptr());
        item.put<Address>(0x40, kit_def.ptr());
        item.put<int>(0x48, 2);
        box.put<Address>(0x50, gun.actor.ptr());
        box.put<Address>(0x10, reinterpret_cast<Address>(kits.data()));
        box.put<Address>(0x18, reinterpret_cast<Address>(kits.data() + 1));
        inventory.put<Address>(0xA0, box.ptr());
        agg.put<Address>(0x28, inventory.ptr());
        entities = {gun.actor.ptr(), gunner.actor.ptr(), loader.actor.ptr()};
        scene.put<Address>(0x48, reinterpret_cast<Address>(entities.data()));
        scene.put<Address>(0x50, reinterpret_cast<Address>(entities.data() + 3));
        image.put<Address>(0x15C7D58, scene.ptr());
        image.put<int>(0x14A490C, 1);
        world.base = image.base();
        world.scene = scene.ptr();
        world.ticks = 100;
        world.length = 1;
        world.now = 1000;
        image.put<Address>(0x15C9668, world.scene);
        image.put<float>(0x149E750, world.length);
        image.put<float>(0x149E758, world.ticks);
        image.put<runtime::Tick>(0x1584568, world.now);
    }
    void vehicle_class(std::string_view type) {
        gun.actor.ename(0xDE8, type);
        gun.def_tag.ename(0, type);
    }
    void install_cannon(bool on) {
        agg.put<Address>(0, reinterpret_cast<Address>(extenders.data()));
        agg.put<Address>(8, reinterpret_cast<Address>(extenders.data() + (on ? 1 : 0)));
    }
    d::Damage damage() {
        d::Damage result{};
        assert(d::inspect(gun.actor.ptr(), result));
        return result;
    }
};
inline Fixture* f{};

inline Byte is_worker() {
    return f->worker;
}
inline Byte is_remote() {
    return f->remote;
}
inline Byte excluded_vehicle(Address a) {
    assert(a == f->gun.actor.ptr());
    ++f->chassis_checks;
    return f->excluded;
}
inline int* duration(Address, int* out) {
    *out = 10;
    return out;
}
inline Address actor_by_route(const std::uint16_t* route) {
    return *route >= 1 && *route <= 3 ? f->entities[*route - 1] : 0;
}
inline Address root(Address boarder) {
    assert(boarder == f->gun.boarder.ptr());
    return boarder;
}
inline Address finish_callback(Address gun, float rate) {
    assert(gun == f->gun.actor.ptr() && rate == 1.f);
    return f->callback.ptr();
}
inline Address make_emit(int count, int place, Address cb) {
    assert(count == 1 && place == -1 && cb == f->callback.ptr());
    f->emit.put<Address>(0, f->destruct_vt.ptr());
    return f->emit.ptr();
}
inline void bind(Address o, Address owner) {
    assert(o == f->emit.ptr() && owner == f->gun.actor.ptr() + 0xAF0);
    f->emit.put<Address>(8, owner);
}
inline int execute_emit(Address o) {
    assert(o == f->emit.ptr());
    f->emitted.push_back(d::read<short>(o, 0x58));
    f->emit.put<short>(0x58, -1);
    return 0;
}
inline void end_emit(Address o) {
    assert(o == f->emit.ptr());
}
inline Address destroy_emit(Address o, unsigned release) {
    assert(o == f->emit.ptr() && release == 1);
    return 0;
}
inline Address resolve(Address image, Address offset) {
    assert(image == f->image.base());
    switch (offset) {
        case 0x855D60: return reinterpret_cast<Address>(&excluded_vehicle);
        case 0x8AACF0: return reinterpret_cast<Address>(&duration);
        case 0xA7C990: return reinterpret_cast<Address>(&actor_by_route);
        case 0x79D790: return reinterpret_cast<Address>(&root);
        case 0x9AC630: return reinterpret_cast<Address>(&finish_callback);
        case 0x8FC550: return reinterpret_cast<Address>(&make_emit);
        case 0x917D70: return reinterpret_cast<Address>(&bind);
        case 0x919F60: return reinterpret_cast<Address>(&execute_emit);
        case 0x91A0E0: return reinterpret_cast<Address>(&end_emit);
        default: assert(false); return 0;
    }
}

inline void run() {
    Fixture fixture{};
    f = &fixture;
    config::reset_defaults();
    d::base = f->image.base();
    d::resolve_native = &resolve;
    auto& auth = runtime::table().auth;
    const auto saved_auth = auth;
    auth.is_quant_worker = reinterpret_cast<Address>(is_worker) - d::base;
    auth.is_remote_client = reinterpret_cast<Address>(is_remote) - d::base;
    f->destruct_vt.put<Address>(0, reinterpret_cast<Address>(&destroy_emit));
    const auto a = f->gun.actor.ptr();
    d::active = true;
    const auto old_support_base = ai_support::base;
    ai_support::base = d::base;
    static_assert(!r::runtime_allowed);
    const auto partial = d::Scenario::turret_track;
    const auto none = d::Scenario::none;
    auto& modules = config::active().modules;
    assert(d::safety(a) && f->chassis_checks == 0);
    f->gun.comp_rows.put<Byte>(0x28 + 0x24, 1);
    assert(f->damage().is_tank && f->damage().trigger() == none); // Ordinary tank with only a broken track.
    f->vehicle_class("halftrack");
    assert(d::safety(a) && f->damage().trigger() == partial);
    f->gun.track_name.ename(0, "engine");
    assert(f->damage().broken_engine && f->damage().trigger() == partial);
    f->vehicle_class("armoredcar");
    f->gun.track_name.ename(0, "wheelright");
    assert(f->damage().wheels && f->damage().broken_wheel && f->damage().trigger() == partial);
    f->gun.comp_rows.put<Byte>(0x28 + 0x24, 0);
    assert(f->damage().trigger() == none);
    f->vehicle_class("truck");
    f->gun.set_body_state(1);
    assert(f->damage().trigger() == d::Scenario::full);
    f->excluded = true;
    assert(!d::safety(a));
    f->excluded = false;
    modules.nontank_auto_repair = false;
    assert(!d::safety(a));
    f->vehicle_class("tank_light");
    assert(d::safety(a));
    modules.tank_auto_repair = false;
    modules.nontank_auto_repair = true;
    assert(!d::safety(a));
    f->vehicle_class("at_gun");
    f->install_cannon(true);
    assert(d::safety(a));
    f->cannon.put<int>(0xC30, 1);
    assert(!d::safety(a));
    f->cannon.put<int>(0xC30, 0);
    f->gun.actor.put<unsigned>(0x1C0, 1u << 28);
    assert(!d::safety(a));
    f->gun.actor.put<unsigned>(0x1C0, 0);
    f->gun.brain.put<unsigned>(0x39C, 1);
    assert(!d::safety(a));
    f->gun.brain.put<unsigned>(0x39C, 0);
    f->gun.chassis.put<unsigned>(0x18, 1);
    assert(!d::safety(a));
    f->gun.chassis.put<unsigned>(0x18, 0);
    f->gun.body_name.ename(0, "gun");
    f->gun.track_name.ename(0, "shield");
    assert(f->damage().is_cannon && f->damage().weapon && f->damage().body == 0 && f->damage().trigger() == partial);
    f->gun.set_body_state(0);
    f->gun.comp_rows.put<Byte>(0x28 + 0x24, 1);
    assert(f->damage().working_gun && f->damage().weapon && f->damage().trigger() == partial);
    f->gun.track_name.ename(0, "wheel");
    auto damage = f->damage();
    assert(damage.total == 1 && damage.body == 0 && damage.trigger() == partial && damage.broken_wheel);

    // Run the actual dispatch loop with two crew and two kits. The gunner must stay even when
    // the quota allows two emissions. A visible enemy just inside 25 m holds both seats.
    f->worker = true;
    r::on_actor_post_quant(a);
    assert(!d::find(a));
    f->worker = false;
    f->remote = true;
    r::on_actor_post_quant(a);
    assert(!d::find(a));
    f->remote = false;
    auto initial_dispatch = r::stat_nontank_dispatch.load();
    f->subject.put<auto_smoke::Vec3>(0x28, {24.99f, 0, 0});
    r::on_actor_post_quant(a);
    auto* roster = d::find(a);
    assert(roster);
    assert(f->emitted.empty() && r::stat_nontank_dispatch == initial_dispatch);
    assert(r::has_active_episode(a));
    f->subject.put<auto_smoke::Vec3>(0x28, {25.01f, 0, 0});
    f->image.put<runtime::Tick>(0x1584568, ++f->world.now);
    r::on_actor_post_quant(a);
    assert((f->emitted == std::vector<int>{1}) && r::stat_nontank_dispatch == initial_dispatch + 1);
    assert(roster->crew[0].role == 2 && !roster->crew[0].dispatched && roster->crew[1].dispatched);
    f->gun.comp_rows.put<Byte>(0x28 + 0x24, 0);
    assert(f->damage().body == 0 &&
           f->damage().trigger() == none); // A completed hull-less gun remains eligible for reboarding.
    f->subject.put<auto_smoke::Vec3>(0x28, {1, 0, 0});
    assert(d::may_dismount(a, f->world, d::Scenario::full, damage, roster->crew[1], 2));
    Memory<0x60> attack{};
    attack.put<Address>(0, d::base + runtime::table().vt.attack_order);
    f->gun.brain.put<Address>(0xf0, attack.ptr());
    assert(!d::may_dismount(a, f->world, d::Scenario::full, damage, roster->crew[1], 2));
    f->gun.brain.put<Address>(0xf0, 0);
    f->subject.put<unsigned>(0x14, 4); // nearby friend
    assert(d::may_dismount(a, f->world, partial, damage, roster->crew[1], 2));
    f->subject.put<unsigned>(0x14, 2);
    f->subject.put<unsigned>(0x10, 1u << 28); // stale sighting
    assert(d::may_dismount(a, f->world, partial, damage, roster->crew[1], 2));

    assert(d::tier(partial, "gun2", 0) == 0 && d::tier(partial, "turret", 0) == 1 &&
           d::tier(partial, "mantlet", 1) == 2);
    assert(d::tier(partial, "engine", 1) == 3 && d::tier(partial, "trackright", 1) == 4 &&
           d::tier(partial, "wheel_l", 1) == 4);
    assert(d::tier(partial, "shield", 1) == 5 && d::tier(partial, "wheel_l", 0) == 6);
    modules.nontank_auto_repair = false;
    assert(!r::is_busy(f->loader.actor.ptr()) && !r::has_active_episode(a));
    r::on_actor_post_quant(a);
    assert(!d::find(a));
    auth = saved_auth;
    r::after_hooks_removed();
    ai_support::base = old_support_base;
    config::reset_defaults();
    f = nullptr;
}
} // namespace goh::test::wave6_repair
