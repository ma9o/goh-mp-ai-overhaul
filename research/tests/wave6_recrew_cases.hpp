#pragma once

#include "../../mods/recrew_weapons_module.hpp"
#include "vehicle_fixture_26734796.hpp"

namespace goh::test::wave6_recrew {
namespace r = recrew_weapons;
namespace d = r::detail;
namespace s = ai_support;
using s::Address;
using s::Byte;
using s::World;
using vehicle::Memory;

struct Soldier {
    vehicle::FakeHuman h{};
    Memory<0x580> root{};
    Memory<0x300> sensor{};
    Memory<0x100> manager{}, box{};
    void setup(Address base, unsigned index) {
        h.setup(base, 100 + index, static_cast<std::uint16_t>(200 + index), 1, 0, 1, 0);
        auto actor = h.actor.ptr();
        h.brain.put(0xe0, root.ptr());
        root.put(0, base + runtime::table().vt.root_soldier);
        root.put(0x30, h.brain.ptr());
        h.actor.put(0xdd0, sensor.ptr());
        sensor.put(0x260, actor);
        h.agg.put(0x28, manager.ptr());
        manager.put(0xa0, box.ptr());
        box.put(0x50, actor);
    }
};
struct Fixture {
    vehicle::FakeImage image{};
    Soldier a{}, b{}, c{};
    vehicle::FakeTank gun{};
    Memory<0x40> aggregate{};
    Memory<0xc40> cannon{};
    Memory<0x300> sensor{};
    Memory<0x80> scene{};
    Memory<0x60> tag{};
    std::array<Address, 4> actors{};
    std::array<Memory<0x1c0>, 12> orders{};
    std::array<int, 12> seats{};
    bool rounds{true}, authority{true};
    int constructed{}, cleared{}, discarded{};
    bool capture_on_factory{};
    Address current{}, swap_to_player_queue{};
    World w{};
    bool previous_accepting = s::accepting.load();
    Address previous_base = s::base;
    static inline Fixture* f{};
    Fixture() {
        f = this;
        s::accepting.store(true);
        s::base = image.base();
        w = {s::base, scene.ptr(), 100, 1, 10};
        gun.setup(s::base, 99, 199, 1);
        gun.actor.put(0xdb4, 0u); // An unmanned gun loses Operatable.
        gun.set_slot_count(2);
        gun.set_slot(0, "charger", "crew", 0, "", "", 0);
        gun.set_slot(1, "gunner", "crew", 0, "", "", 0);
        gun.track_name.ename(0, "gun");
        gun.actor.put(0x578, aggregate.ptr());
        aggregate.put(0, aggregate.ptr() + 0x28);
        aggregate.put(8, aggregate.ptr() + 0x30);
        aggregate.put(0x28, cannon.ptr());
        cannon.put(0, s::base + runtime::table().vt.cannon_ext);
        cannon.put(0x18, gun.actor.ptr());
        gun.actor.put(0xdd0, sensor.ptr());
        sensor.put(0x260, gun.actor.ptr());
        a.setup(s::base, 0);
        b.setup(s::base, 1);
        c.setup(s::base, 2);
        actors = {gun.actor.ptr(), a.h.actor.ptr(), b.h.actor.ptr(), c.h.actor.ptr()};
        scene.put(0x48, reinterpret_cast<Address>(actors.data()));
        scene.put(0x50, reinterpret_cast<Address>(actors.data() + actors.size()));
        d::ops.callback = [](Address actor, World& world) {
            world = f->w;
            return f->authority && actor == f->current;
        };
        d::ops.ammunition = [](const World&, Address) { return f->rounds; };
        d::ops.is_specialist = [](Address actor) { return d::specialist(actor, 0); };
        d::ops.board = [](Address gun_actor, Address seat) {
            assert(gun_actor == f->gun.actor.ptr() && seat >= f->gun.slots.ptr());
            const auto index = static_cast<int>((seat - f->gun.slots.ptr()) / 0x298);
            assert(index == 0 || index == 1);
            f->seats.at(f->constructed) = index;
            auto& o = f->orders.at(f->constructed++);
            o.put(0, s::base + runtime::table().vt.board_order);
            o.put(0x58, gun_actor);
            o.ename(0x158, s::native_string(seat + 0x40));
            if (f->capture_on_factory) {
                f->gun.actor.put<Byte>(0xda8, 2);
                f->a.h.actor.put<Byte>(0xda8, 2);
            }
            return o.ptr();
        };
        const auto setter = +[](Address brain, Address order) {
            runtime::write(brain, 0xf0, order);
            if (!order) {
                ++f->cleared;
                return;
            }
            const auto actor = s::read<Address>(brain, 0x38);
            runtime::write(order, 8, actor + 0xaf0);
            if (f->swap_to_player_queue) runtime::write(brain, 0xf0, f->swap_to_player_queue);
        };
        image.put(runtime::table().vt.human_brain + 0x68, reinterpret_cast<Address>(setter));
        image.put(runtime::table().vt.board_order, reinterpret_cast<Address>(+[](Address, unsigned) -> Address {
                      ++f->discarded;
                      return 0;
                  }));
        coord::instance().clear_providers();
        coord::instance().reset();
    }
    ~Fixture() {
        r::after_hooks_removed();
        coord::instance().reset();
        d::ops = {};
        s::base = previous_base;
        s::accepting.store(previous_accepting);
        f = nullptr;
    }
    void observe() {
        current = gun.actor.ptr();
        r::on_vehicle_observe(w, current);
    }
    void tick(Soldier& soldier) {
        current = soldier.h.actor.ptr();
        r::tick(current);
    }
    bool eligible() {
        auto* g = d::observe(w, gun.actor.ptr());
        return g && d::eligible_gun(w, gun.actor.ptr(), a.h.actor.ptr(), *g);
    }
    void seat(Soldier& soldier) {
        gun.slots.put(0x298 + 0x280, soldier.h.actor.ptr());
        soldier.h.actor.put(0x1c0, 1u << 28);
        soldier.h.actor.put(0x38, 0x200u);
    }
    void leave(Soldier& soldier) {
        gun.slots.put<Address>(0x298 + 0x280, 0);
        soldier.h.actor.put(0x1c0, 0u);
        soldier.h.actor.put(0x38, 0u);
    }
};

inline void gun_policy() {
    Fixture t;
    assert(t.eligible());
    t.gun.actor.put<Byte>(0xda8, 2);
    assert(!t.eligible()); // Same-team players still have different owner bytes.
    t.gun.actor.put<Byte>(0xda8, 1);
    t.gun.actor.put<Byte>(0xdad, 2);
    assert(!t.eligible());
    t.gun.actor.put<Byte>(0xdad, 0);
    t.cannon.put(0xc30, 1);
    assert(!t.eligible());
    t.cannon.put(0xc30, 0);
    t.gun.actor.put(0x1c0, 1u << 28);
    assert(!t.eligible());
    t.gun.actor.put(0x1c0, 0u);
    t.tag.ename(0, "destroyed");
    t.gun.actor.put(0x4e8, t.tag.ptr());
    t.gun.actor.put(0x4f0, t.tag.ptr() + 0x60);
    assert(!t.eligible());
    t.gun.actor.put(0x4f0, t.tag.ptr());
    t.gun.set_body_state(1);
    assert(!t.eligible());
    t.gun.set_body_state(0);
    t.gun.comp_rows.put<Byte>(0x28 + 0x24, 1);
    assert(!t.eligible());
    t.gun.comp_rows.put<Byte>(0x28 + 0x24, 0);
    t.rounds = false;
    assert(!t.eligible());
    t.rounds = true;
    t.gun.placer.put<Byte>(0x48, 1);
    assert(!t.eligible());
    t.gun.placer.put<Byte>(0x48, 0);
    t.gun.set_exit_walker(t.a.h.actor.ptr());
    assert(!t.eligible());
    t.gun.clear_walkers();
    t.gun.brain.put(0x39c, 1u << 28);
    assert(!t.eligible());
    t.gun.brain.put(0x39c, 0u);
    assert(t.eligible());
}
inline void exit_evidence() {
    Fixture t;
    t.seat(t.b);
    t.observe();
    t.leave(t.b);
    t.observe();
    assert(!t.eligible());
    t.w.now += 299;
    assert(!t.eligible());
    ++t.w.now;
    assert(t.eligible());
    t.seat(t.b);
    t.observe();
    t.leave(t.b);
    t.b.h.vit.put<Byte>(0x174, 1);
    t.observe();
    assert(t.eligible());
    assert(d::guns[0].lost == t.w.now);
    t.seat(t.c);
    t.observe();
    t.leave(t.c);
    t.actors[3] = 0;
    t.observe();
    assert(t.eligible()); // corpse removed from the roster
    t.actors[3] = t.c.h.actor.ptr();
    t.seat(t.c);
    t.observe();
    t.leave(t.c);
    t.observe();
    assert(!t.eligible());
    t.w.now = 1;
    t.observe();
    assert(t.eligible());
}
inline void dispatch_and_receipt() {
    Fixture t;
    auto& a = t.a.h;
    auto& gun = t.gun;
    t.authority = false;
    t.tick(t.a);
    assert(!t.constructed);
    t.authority = true;
    a.brain.put(0x398, 1u | (1u << 12));
    a.actor.put(0x74, s::Vec3{8.01f, 0, 0});
    t.tick(t.a);
    assert(!t.constructed);
    t.w.now += 16;
    a.actor.put(0x74, s::Vec3{8, 0, 0});
    t.tick(t.a);
    assert(t.constructed == 1 && t.seats[0] == 1 && a.brain.get<Address>(0xf0) == t.orders[0].ptr());
    t.b.h.actor.put(0x74, s::Vec3{28.01f, 0, 0});
    t.tick(t.b);
    assert(t.constructed == 1);
    t.w.now += 16;
    t.b.h.actor.put(0x74, s::Vec3{28, 0, 0});
    t.tick(t.b);
    assert(t.constructed == 2 && t.seats[1] == 0);
    t.tick(t.c);
    assert(t.constructed == 2);
    t.seat(t.a);
    t.observe();
    assert(r::stat_recrews == 1 && !d::occupied(a.actor.ptr()));
    t.observe();
    assert(r::stat_recrews == 1);
    gun.slots.put(0x280, t.b.h.actor.ptr());
    t.observe();
    assert(r::stat_recrews == 2 && !d::occupied(t.b.h.actor.ptr()));
}
inline void specialists_and_hazards() {
    Fixture t;
    auto& a = t.a.h;
    a.set_medic(t.w.base);
    t.tick(t.a);
    assert(!t.constructed);
    a.breed.put<Address>(0x550, a.breed.get<Address>(0x548)); // empty ability vector
    Memory<0x70> item;
    Memory<0x1200> def;
    Memory<0x60> tag;
    std::array<Address, 1> items{item.ptr()};
    t.a.box.put(0x10, reinterpret_cast<Address>(items.data()));
    t.a.box.put(0x18, reinterpret_cast<Address>(items.data() + 1));
    item.put(0x30, t.a.box.ptr());
    item.put(0x40, def.ptr());
    def.put(0x11e0, tag.ptr());
    def.put(0x11e8, tag.ptr() + 0x60);
    for (auto n : {"mgun", "rocketlauncher", "flame_thrower"}) {
        tag.ename(0, n);
        t.w.now += 16;
        t.tick(t.a);
        assert(!t.constructed);
    }
    // The held-object query returns a definition directly, even outside the pack.
    t.a.box.put(0x18, reinterpret_cast<Address>(items.data()));
    tag.ename(0, "mgun");
    assert(d::specialist(a.actor.ptr(), def.ptr()));
    tag.ename(0, "rifle");
    assert(!d::specialist(a.actor.ptr(), def.ptr()));
    Memory<0x80> sub;
    std::array<Address, 1> subjects{sub.ptr()};
    t.a.sensor.put(0x280, reinterpret_cast<Address>(subjects.data()));
    t.a.sensor.put(0x288, reinterpret_cast<Address>(subjects.data() + 1));
    sub.put(0x14, 2u);
    sub.put(0x10, 3u << 28);
    sub.put(0x28, s::Vec3{23, 0, 0});
    t.gun.actor.put(0x74, s::Vec3{20, 0, 0});
    t.w.now += 16;
    t.tick(t.a);
    assert(!t.constructed);
    sub.put(0x10, 0u);
    t.w.now += 16;
    t.tick(t.a);
    assert(t.constructed == 1);
}
inline void cancellation() {
    Fixture t;
    t.gun.set_slot_reserved(1, t.c.h.actor.ptr());
    t.tick(t.a);
    assert(t.constructed == 1 && t.seats[0] == 0);
    assert(coord::instance().claim(t.a.h.actor.ptr(), 100, coord::Module::auto_medic, coord::Priority::auto_medic,
                                   t.w.now, 3));
    t.tick(t.a);
    assert(t.cleared == 1 && !t.a.h.brain.get<Address>(0xf0) && !d::occupied(t.a.h.actor.ptr()));
    t.w.now += 99;
    t.tick(t.a);
    assert(t.constructed == 1);
    ++t.w.now;
    t.tick(t.a);
    assert(t.constructed == 2);
    t.w.now += 180;
    t.tick(t.a);
    assert(t.cleared == 2);
    t.w.now += 100;
    t.tick(t.a);
    assert(t.constructed == 3);
    Memory<0x80> player;
    t.a.h.brain.put(0xf0, player.ptr());
    t.tick(t.a);
    assert(t.cleared == 2 && t.a.h.brain.get<Address>(0xf0) == player.ptr());
    t.a.h.brain.put<Address>(0xf0, 0);
    t.w.now += 100;
    t.swap_to_player_queue = player.ptr();
    t.tick(t.a);
    assert(!d::occupied(t.a.h.actor.ptr()) && t.a.h.brain.get<Address>(0xf0) == player.ptr());
    t.swap_to_player_queue = 0;
    t.a.h.brain.put<Address>(0xf0, 0);
    t.w.now += 100;
    t.tick(t.a);
    assert(d::occupied(t.a.h.actor.ptr()));
    r::begin_shutdown();
    t.tick(t.a);
    assert(!d::occupied(t.a.h.actor.ptr()) && r::quiescent());
}
struct TestAmmoCalls {
    static inline bool compatible{};
    static bool dummy(Address) {
        return false;
    }
    static bool offers(const World&, Address, Address, Address gun, const auto_resupply::Gun& weapon) {
        assert(gun == Fixture::f->gun.actor.ptr() && weapon.place && weapon.def && weapon.item);
        return compatible;
    }
};
inline void mounted_ammo_and_supply() {
    Fixture t;
    Memory<0x230> place;
    Memory<0x1200> def;
    Memory<0x150> item;
    Memory<0x50> charger;
    Memory<0x20> charger_vt;
    Memory<0x60> weapon_tag, selector;
    Memory<0x100> manager, box;
    std::array<Address, 1> places{place.ptr()};
    t.gun.weaponry.put(0x30, reinterpret_cast<Address>(places.data()));
    t.gun.weaponry.put(0x38, reinterpret_cast<Address>(places.data() + 1));
    t.aggregate.put(0x28, manager.ptr());
    // The extender vector must remain independent of the inventory-manager field.
    t.aggregate.put(0, t.aggregate.ptr() + 0x30);
    t.aggregate.put(8, t.aggregate.ptr() + 0x38);
    t.aggregate.put(0x30, t.cannon.ptr());
    manager.put(0xa0, box.ptr());
    box.put(0x50, t.gun.actor.ptr());
    place.put(0x90, t.gun.actor.ptr());
    place.put(0x98, def.ptr());
    place.put(0xa0, place.ptr());
    place.put(0xb0, charger.ptr());
    place.put(0x218, item.ptr());
    charger.put(0, charger_vt.ptr());
    charger.put(8, item.ptr());
    charger_vt.put(8, reinterpret_cast<Address>(+[](Address c) { return s::read<Address>(c, 8); }));
    item.put(0x40, def.ptr());
    item.put(0x50, def.ptr());
    item.put(0xdc, 1);
    weapon_tag.ename(0, "weapon");
    def.put(0x11e0, weapon_tag.ptr());
    def.put(0x11e8, weapon_tag.ptr() + 0x60);
    def.put(0x120 + 0x318, selector.ptr());
    def.put(0x120 + 0x320, selector.ptr() + 0x60);
    assert(d::armed_or_supplied<TestAmmoCalls>(t.w, t.gun.actor.ptr()));
    item.put(0xdc, 0);
    assert(!d::armed_or_supplied<TestAmmoCalls>(t.w, t.gun.actor.ptr()));
    Memory<0x600> source;
    Memory<0x40> extensions;
    Memory<0x440> zone;
    t.actors[3] = source.ptr();
    source.put<Byte>(0x30, 1);
    source.put(0x578, extensions.ptr());
    extensions.put(0, extensions.ptr() + 0x30);
    extensions.put(8, extensions.ptr() + 0x38);
    extensions.put(0x30, zone.ptr());
    zone.put(0, t.w.base + runtime::table().vt.supply_zone_ext);
    zone.put(0x18, source.ptr());
    zone.put<Byte>(0x430, 1);
    zone.put(0x88, 5.f);
    zone.put(0x8c, 1.f);
    TestAmmoCalls::compatible = true;
    source.put(0x74, s::Vec3{5.01f, 0, 0});
    assert(!d::armed_or_supplied<TestAmmoCalls>(t.w, t.gun.actor.ptr()));
    source.put(0x74, s::Vec3{5, 0, 0});
    assert(d::armed_or_supplied<TestAmmoCalls>(t.w, t.gun.actor.ptr()));
    TestAmmoCalls::compatible = false;
    assert(!d::armed_or_supplied<TestAmmoCalls>(t.w, t.gun.actor.ptr()));
    zone.put<Byte>(0x430, 0);
    TestAmmoCalls::compatible = true;
    assert(!d::armed_or_supplied<TestAmmoCalls>(t.w, t.gun.actor.ptr()));
}
inline void removal() {
    Fixture t;
    t.tick(t.a);
    assert(d::occupied(t.a.h.actor.ptr()));
    t.actors[0] = 0;
    t.tick(t.a);
    assert(t.cleared == 1 && !d::occupied(t.a.h.actor.ptr()));
    t.actors[0] = t.gun.actor.ptr();
    t.w.now += 100;
    t.tick(t.a);
    assert(d::occupied(t.a.h.actor.ptr()));
    t.actors[1] = 0;
    t.w.now += 180;
    t.tick(t.b);
    assert(!d::occupied(t.a.h.actor.ptr()) && t.seats[t.constructed - 1] == 1);
}
inline void repair_departures() {
    namespace repair = tank_auto_repair::detail;
    Fixture t;
    const bool was_active = repair::active.exchange(true);
    auto& vehicle = repair::tanks[0];
    vehicle.key = repair::key(t.gun.actor.ptr());
    vehicle.scenario = repair::Scenario::turret_track;
    vehicle.crew[0].key = repair::key(t.b.h.actor.ptr());
    vehicle.crew[0].assigned = true;
    repair::members[201] = {1, 0};
    t.seat(t.b);
    t.observe();
    t.leave(t.b);
    t.observe();
    assert(!d::guns[0].manual_until && !t.eligible());
    // The player's other surviving crewman is independent of the assigned repairer.
    t.seat(t.c);
    t.observe();
    t.leave(t.c);
    t.observe();
    assert(d::guns[0].manual_until == t.w.now + t.w.duration(30.f));
    repair::members.clear();
    repair::tanks.clear();
    repair::active.store(was_active);
}
inline void ownership_changes() {
    Fixture t;
    t.capture_on_factory = true;
    t.tick(t.a);
    assert(t.constructed == 1 && t.discarded == 1 && !t.a.h.brain.get<Address>(0xf0) &&
           !d::occupied(t.a.h.actor.ptr()));
    t.capture_on_factory = false;
    t.w.now += 100;
    t.tick(t.a);
    assert(d::occupied(t.a.h.actor.ptr()));
    t.gun.actor.put<Byte>(0xda8, 3);
    t.a.h.actor.put<Byte>(0xda8, 3);
    t.seat(t.a);
    t.observe();
    assert(r::stat_recrews == 0 && !t.cleared);
    t.leave(t.a);
    t.observe();
    t.w.now += 301;
    t.a.h.brain.put<Address>(0xf0, 0);
    t.tick(t.a);
    assert(d::occupied(t.a.h.actor.ptr()));
    d::preempt(t.a.h.actor.ptr());
    t.seat(t.a);
    t.observe();
    assert(r::stat_recrews == 0);
    t.leave(t.a);
    t.observe();
    t.w.now += 301;
    t.a.h.brain.put<Address>(0xf0, 0);
    t.tick(t.a);
    assert(d::occupied(t.a.h.actor.ptr()));
    t.gun.actor.put<Byte>(0xda8, 4);
    t.a.h.actor.put<Byte>(0xda8, 4);
    t.tick(t.a);
    assert(!d::occupied(t.a.h.actor.ptr()) && !t.cleared);
}
inline void run() {
    repair_departures();
    ownership_changes();
    mounted_ammo_and_supply();
    removal();
    gun_policy();
    exit_evidence();
    dispatch_and_receipt();
    specialists_and_hazards();
    cancellation();
}
} // namespace goh::test::wave6_recrew
