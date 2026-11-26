// Offline Stream B fixtures for the exact 26734796 actor, sensor and weapon layouts.
#include <windows.h>
#ifndef _WIN32
// The minimal host-side Windows shim has no TLS backing. Model the actor callback slot.
inline thread_local void* fixture_actor_slot{};
inline void* fixture_get_slot(DWORD) {
    return fixture_actor_slot;
}
inline int fixture_set_slot(DWORD, void* p) {
    fixture_actor_slot = p;
    return 1;
}
#define TlsGetValue fixture_get_slot
#define TlsSetValue fixture_set_slot
#endif
#include "../../mods/engine_fixes_module.hpp"
#include "../../mods/penetration_at_module.hpp"
#include "../../mods/tank_facing_module.hpp"
#include "vehicle_fixture_26734796.hpp"
#include <cstdio>

namespace ef = goh::engine_fixes;
namespace pr = goh::target_prio::detail;
namespace at = goh::penetration_at;
namespace tf = goh::tank_facing;
namespace rt = goh::runtime;
namespace su = goh::ai_support;
namespace cfg = goh::config;
using namespace goh::test::vehicle;
using rt::Byte;

namespace {
constexpr float degree = tf::policy::radians(1);
struct Fixture {
    FakeImage image;
    FakeTank tank{}, enemy{}, second{};
    FakeHuman rifle{};
    Memory<0x340> sensor{};
    Memory<0x98> armor{}, other{}, soldier{}, weaponry{};
    Memory<0x100> scene{}, next_scene{}, attack{}, wrapper{}, wrapper_vt{};
    Memory<0x20> aggregate{};
    Memory<0xc40> cannon_ext{};
    std::array<Address, 3> observations{}, entities{};
    std::array<int, 18 * 18> diplomacy{};
    Address ext{};
    unsigned cleared_locations{};
    su::World w{};
    rt::BuildTable::Authority saved_auth = rt::table().auth;
    bool remote{}, worker{}, native_accept{true};
    static inline Fixture* current{};
    Fixture() {
        current = this;
        cfg::reset_defaults();
        const auto base = image.base();
        tank.setup(base, 7, 12, 1);
        enemy.setup(base, 8, 13, 2);
        second.setup(base, 9, 14, 2);
        rifle.setup(base, 10, 15, 2, 0);
        rifle.actor.ename(0xde8, "human");
        for (auto* target : {&enemy, &second})
            target->actor.put(0xdc0, scene.ptr());
        tank.actor.put(0xdd0, sensor.ptr());
        tank.actor.put(0xdd8, weaponry.ptr());
        sensor.put(0x260, tank.actor.ptr());
        observations = {armor.ptr(), other.ptr(), soldier.ptr()};
        entities = {enemy.actor.ptr(), second.actor.ptr(), rifle.actor.ptr()};
        scene.put(0x48, reinterpret_cast<Address>(entities.data()));
        scene.put(0x50, reinterpret_cast<Address>(entities.data() + entities.size()));
        sensor.put(0x280, reinterpret_cast<Address>(observations.data()));
        sensor.put(0x288, reinterpret_cast<Address>(observations.data() + observations.size()));
        for (auto* s : {&armor, &other, &soldier}) {
            s->put(0x14, 3u);
            s->put(0x10, 3u << 28);
        }
        armor.put(0x18, enemy.actor.ptr());
        other.put(0x18, second.actor.ptr());
        soldier.put(0x18, rifle.actor.ptr());
        armor.put(0x28, pr::Vec3{80, 0, 0});
        other.put(0x28, pr::Vec3{0, -80, 0});
        soldier.put(0x28, pr::Vec3{0, 60, 0});
        other.put(0x10, 1u << 28);
        w = {base, scene.ptr(), 1000, 1, 100};
        image.put(0x15c9668, w.scene);
        image.put(0x15c7d58, w.scene);
        image.put(0x1584568, w.now);
        image.put(0x149e750, w.length);
        image.put(0x149e758, w.ticks);
        image.put(0x15c9700, reinterpret_cast<Address>(diplomacy.data()));
        image.put(0x15c9710, 18);
        attack.put(0, base + rt::table().vt.attack_order);
        wrapper_vt.put(0x18, base + 0x919f60);
        wrapper_vt.put(0x20, base + 0x91a0e0);
        wrapper.put(0, wrapper_vt.ptr());
        wrapper.put(0x30, attack.ptr());
        ext = cannon_ext.ptr();
        aggregate.put(0, reinterpret_cast<Address>(&ext));
        aggregate.put(8, reinterpret_cast<Address>(&ext + 1));
        cannon_ext.put(0, base + rt::table().vt.cannon_ext);
        cannon_ext.put(0x18, tank.actor.ptr());
        pr::base = ef::image = su::base = base;
        rt::table().auth.is_remote_client = reinterpret_cast<Address>(+[]() -> Byte { return current->remote; }) - base;
        rt::table().auth.is_quant_worker = reinterpret_cast<Address>(+[]() -> Byte { return current->worker; }) - base;
        ef::ops.serial_actor = +[]() { return current->tank.actor.ptr(); };
        ef::detail::original.eligible = +[](Address, Address) -> Byte { return current->native_accept; };
        ef::ops.resolve = +[](Address, Address rva) {
            if (rva == rt::table().auth.clock_read) return reinterpret_cast<Address>(+[]() { return current->w.now; });
            assert(rva == 0x66e7c0);
            return reinterpret_cast<Address>(+[](int ticks) { return ticks / current->w.ticks; });
        };
        pr::resolve_selection_call = +[](Address rva) -> Address {
            switch (rva) {
                case 0x92b320:
                    return reinterpret_cast<Address>(+[](Address, Address) -> Byte { return current->native_accept; });
                case 0x7e18f0: return reinterpret_cast<Address>(+[](Address temporary) { return temporary; });
                case 0x7e35e0:
                    return reinterpret_cast<Address>(+[](Address destination, Address empty) {
                        assert(!rt::read<Address>(empty));
                        rt::write(destination, 0, Address{});
                        return destination;
                    });
                case 0x7e1ec0: return reinterpret_cast<Address>(+[](Address) { ++current->cleared_locations; });
                default: std::abort();
            }
        };
        rt::global_accepting = true;
        ef::gate.activate();
        pr::hook_gate.activate();
        goh::target_prio::enter_actor_scope(tank.actor.ptr());
    }
    ~Fixture() {
        goh::target_prio::exit_actor_scope(tank.actor.ptr());
        ef::after_hooks_removed();
        at::after_hooks_removed();
        goh::target_prio::after_hooks_removed();
        rt::table().auth = saved_auth;
        su::base = 0;
    }
    // Each gate is removed in isolation. Index six is a nested explicit Attack.
    void gate(int which) {
        remote = which == 1;
        worker = which == 2;
        tank.brain.put<unsigned>(0x39c, which == 3 ? 1 : 0);
        tank.chassis.put<unsigned>(0x18, which == 4);
        weaponry.put<Byte>(0x70, which == 5 ? 2 : 0);
        tank.brain.put(0xf0, which == 6 ? wrapper.ptr() : which == 7 ? attack.ptr() : Address{});
        tank.brain.put(0, image.base() + (which == 8 ? 0x1020020 : rt::table().vt.vehicle_brain));
    }
    const pr::ArmorMemory* memory() {
        return pr::update_armor_memory(w, tank.actor.ptr(), {});
    }
    void visible(bool on) {
        armor.put(0x10, on ? 3u << 28 : 1u << 28);
    }
};

void allegiance_and_towing() {
    Fixture f;
    const auto check = [&](bool expected) {
        assert(bool(ef::detail::eligible(f.tank.brain.ptr(), f.armor.ptr())) == expected);
        assert(pr::eligible_subject(f.tank.brain.ptr(), f.armor.ptr()) == expected);
    };
    check(true);
    f.enemy.actor.put<Byte>(0xda8, 1);
    f.enemy.actor.put(0xdb4, 0u); // Empty captured vehicle still owns the live actor.
    check(false);
    f.enemy.actor.put<Byte>(0xda8, 3);
    f.diplomacy[3 * 18 + 1] = 2;
    check(false);
    cfg::active().modules.engine_fixes = false;
    assert(!pr::eligible_subject(f.tank.brain.ptr(), f.armor.ptr()));
    cfg::active().modules.engine_fixes = true;
    pr::Assignment assignment;
    assignment.brain = f.tank.brain.ptr();
    pr::scope.assignment = &assignment;
    Memory<0x58> result{};
    auto rejected = [&] {
        result.data.fill(std::byte{1});
        assert(!pr::vehicle_select(assignment.brain, f.armor.ptr(), 0, result.ptr(), 0));
        for (unsigned i = 0; i < 4; ++i)
            assert(!result.get<Address>(i * sizeof(Address)));
    };
    cfg::active().modules.engine_fixes = false;
    rejected(); // The native row selector must also reject an already-ranked captured ally.
    cfg::active().modules.engine_fixes = true;
    for (int i = 1; i <= 8; ++i) {
        f.gate(i);
        check(true);
    }
    f.gate(0);
    cfg::active().modules.allegiance_guard = false;
    check(true);
    cfg::active().modules.allegiance_guard = true;
    f.native_accept = false;
    f.w.now = 2000;
    f.armor.put(0x64, 900u);
    f.armor.put(0x70, f.w.now);
    f.armor.put(0x14, 1u << 10);
    assert(!ef::detail::eligible(f.tank.brain.ptr(), f.armor.ptr()));
    assert(ef::stats.allegiance_veto == 3);
    f.enemy.actor.put<Byte>(0xda8, 2);
    assert(ef::detail::eligible(f.tank.brain.ptr(), f.armor.ptr()));
    f.tank.actor.put(0x578, f.aggregate.ptr());
    f.cannon_ext.put(0xc30, 1);
    assert(!ef::detail::eligible(f.tank.brain.ptr(), f.armor.ptr()));
    f.native_accept = true;
    f.armor.put(0x14, 3u);
    for (int state : {1, 2, 3}) {
        f.cannon_ext.put(0xc30, state);
        check(false);
    }
    f.cannon_ext.put(0xc30, 0);
    check(true);
    f.tank.actor.put(0x1c0, 1u << 28);
    check(false);
    cfg::active().modules.engine_fixes = false;
    rejected();
    assert(f.cleared_locations == 2);
    cfg::active().modules.engine_fixes = true;
    assert(ef::stats.towed_gun_suppress == 5);
    f.cannon_ext.put(0x18, f.enemy.actor.ptr()); // Foreign extender cannot suppress a linked vehicle.
    check(true);
    ef::after_hooks_removed();
    assert(!ef::stats.allegiance_veto && !ef::stats.towed_gun_suppress);
}

void armor_lifetime() {
    Fixture f;
    assert(!f.memory());
    f.visible(false);
    f.enemy.actor.put(0x74, pr::Vec3{-90, 90, 0}); // Hidden entity motion must never update the bearing.
    auto m = f.memory();
    assert(m && m->threat_entity.ptr == f.enemy.actor.ptr() && m->bearing_rad == 0 && m->threat_pos.x == 80);
    f.w.now += 500;
    assert(f.memory());
    ++f.w.now;
    assert(!f.memory());
    f.visible(true);
    f.other.put(0x10, 3u << 28);
    f.other.put(0x14, 0x103u);
    assert(!f.memory());
    f.visible(false);
    f.other.put(0x10, 1u << 28);
    assert(f.memory()->threat_entity.ptr == f.second.actor.ptr());
    f.visible(true);
    assert(!f.memory());
    f.visible(false);
    assert(f.memory()->threat_entity.ptr == f.enemy.actor.ptr());
    for (int reason = 0; reason < 8; ++reason) {
        f.visible(true);
        assert(!f.memory());
        f.visible(false);
        if (reason == 0) f.enemy.actor.put<Byte>(0xda8, 1);
        if (reason == 1) f.enemy.actor.put(0xdb4, 0u);
        if (reason == 2) f.enemy.actor.put(0xc0, 999u);
        if (reason == 3) {
            f.enemy.track_name.ename(0, "gun");
            f.enemy.comp_rows.put<Byte>(0x4c, 1);
        }
        if (reason == 4) f.entities[0] = 0;
        if (reason == 5) f.w.scene = f.next_scene.ptr();
        if (reason == 6) --f.w.now;
        if (reason == 7) f.enemy.def_tag.ename(0, "destroyed");
        assert(!f.memory());
        f.enemy.actor.put<Byte>(0xda8, 2);
        f.enemy.actor.put(0xdb4, 0x100u);
        f.enemy.actor.put(0xc0, 8u);
        f.enemy.comp_rows.put<Byte>(0x4c, 0);
        f.enemy.def_tag.ename(0, "tank");
        f.entities[0] = f.enemy.actor.ptr();
        f.w.scene = f.scene.ptr();
        ++f.w.now;
        assert(!f.memory());
    }
}

void turret_and_hull() {
    Fixture f;
    Memory<0x1700> cannon_def{}, mg_def{}, shell{};
    Memory<0x60> mg_tag{}, shell_tag{};
    mg_tag.ename(0, "mgun");
    shell_tag.ename(0, "shell");
    mg_def.put(0x11e0, mg_tag.ptr());
    mg_def.put(0x11e8, mg_tag.ptr() + 0x60);
    shell.put(0x11e0, shell_tag.ptr());
    shell.put(0x11e8, shell_tag.ptr() + 0x60);
    cannon_def.put(0x1628, 75.f);
    std::array<Memory<0x230>, 4> weapons{};
    std::array<Memory<0x90>, 4> aimers{};
    std::array<Memory<0x150>, 4> bones{};
    Memory<0x150> turret{}, bow{};
    Memory<0x600> place{};
    Memory<0x60> gunner{}, out{};
    Memory<4 * pr::row_size> rows{};
    Memory<0xb8> manager{};
    Memory<0x60> box{}, item{};
    Memory<0x400> hand{};
    Memory<0x1200> item_def{};
    Memory<0xc0> item_tags{};
    Address held = hand.ptr(), stack = item.ptr();
    f.rifle.agg.put(0x28, manager.ptr());
    manager.put(0xa0, box.ptr());
    manager.put(0x88, reinterpret_cast<Address>(&held));
    manager.put(0x90, reinterpret_cast<Address>(&held + 1));
    hand.put(0, f.image.base() + rt::table().vt.holder_hand_right);
    hand.put(0x3d0, f.rifle.actor.ptr());
    box.put(0x50, f.rifle.actor.ptr());
    item.put(0x40, item_def.ptr());
    item.put(0x48, 1);
    item_def.put(0x11e0, item_tags.ptr());
    item_def.put(0x11e8, item_tags.ptr() + item_tags.data.size());
    const auto brain = f.tank.brain.ptr();
    Address candidate = f.soldier.ptr();
    f.tank.brain.put(0x3a0, reinterpret_cast<Address>(&candidate));
    f.tank.brain.put(0x3a8, reinterpret_cast<Address>(&candidate + 1));
    f.tank.brain.put(0x3b0, reinterpret_cast<Address>(&candidate + 1));
    f.tank.brain.put(0x3b8, rows.ptr());
    f.tank.brain.put(0x3c0, rows.ptr() + rows.data.size());
    for (unsigned i = 0; i < 4; ++i) {
        weapons[i].put(0x98, i == 0 || i == 3 ? cannon_def.ptr() : mg_def.ptr());
        rows.put(i * pr::row_size + 0x98, shell.ptr());
        rows.put(i * pr::row_size + 8, weapons[i].ptr());
        weapons[i].put(0x90, f.tank.actor.ptr());
        weapons[i].put(0xa8, aimers[i].ptr());
        aimers[i].put(0x20, bones[i].ptr());
        bones[i].put(0x138, i < 2 ? turret.ptr() : bow.ptr());
    }
    weapons[0].put(0xc0, place.ptr());
    place.put(0x10, f.tank.actor.ptr());
    gunner.ename(0, "gunner");
    place.put(0x5e0, gunner.ptr());
    place.put(0x5e8, gunner.ptr() + 0x60);
    turret.put<Byte>(0x12, 1);
    bow.put<Byte>(0x12, 1);
    static unsigned expected;
    pr::orig_select = +[](Address owner_brain, Address, std::int16_t, Address output, int) {
        auto live = pr::span(owner_brain, 0x3b8);
        unsigned seen = 0;
        for (unsigned i = 0; i < 4; ++i) {
            if (rt::read<Byte>(live.first + i * pr::row_size)) seen |= 1u << i;
        }
        assert(seen == expected);
        rt::write(output, 0, live.first + ((seen & 1) ? 2u : 0u) * pr::row_size);
        return true;
    };
    static Address result;
    result = out.ptr();
    pr::orig_assign = +[](Address b) { assert(pr::vehicle_select(b, Fixture::current->soldier.ptr(), 0, result, 0)); };
    auto select = [&](unsigned mask) {
        expected = mask;
        pr::vehicle_assign(brain);
        assert(!pr::scope.assignment && !pr::assignment_memory);
        for (unsigned i = 0; i < 4; ++i)
            assert(!rows.get<Byte>(i * pr::row_size));
    };
    f.visible(true);
    select(0);
    f.visible(false);
    select(3);
    assert(rt::read<Address>(out.ptr()) == rows.ptr() + 2 * pr::row_size && goh::target_prio::stat_memory_holds == 1);
    hand.put(0x3d8, item.ptr());
    item_tags.ename(0, "weapon");
    item_tags.ename(0x60, "rocketlauncher");
    select(0);
    hand.put(0x3d8, Address{});
    box.put(0x10, reinterpret_cast<Address>(&stack));
    box.put(0x18, reinterpret_cast<Address>(&stack + 1));
    item_tags.ename(0x60, "at_rifle");
    select(0);
    item_tags.ename(0, "grenade");
    item_tags.ename(0x60, "at");
    select(0);
    manager.put(0xa0, Address{});
    select(0); // Unknown equipment.
    manager.put(0xa0, box.ptr());
    item.put(0x48, 0); // An empty stack is not a carried AT weapon.
    select(3);
    f.soldier.put(0x28, pr::Vec3{0, 30, 0});
    select(0);
    f.soldier.put(0x28, pr::Vec3{60, 16, 0});
    select(0);                         // 15 degrees: inside a turreted tank's cone.
    f.tank.track_name.ename(0, "gun"); // Hull and primary gun, no rotating turret component.
    f.visible(true);
    assert(!f.memory());
    f.visible(false);
    assert(f.memory() && f.memory()->casemate);
    select(3);
    cfg::active().modules.tank_target_memory = false;
    select(0);
    cfg::active().modules.tank_target_memory = true;
    for (int i : {1, 2, 3, 4, 5, 6}) {
        f.gate(i);
        select(0);
    }
    f.gate(0);
    f.other.put(0x10, 3u << 28);
    select(0);
    f.other.put(0x10, 1u << 28);
    f.visible(true);
    assert(!f.memory());
    f.visible(false);
    f.observations[0] = 0; // Smoke removed the previous subject from the sensor altogether.
    f.soldier.put(0x14, 0x103u);
    f.soldier.put(0x28, pr::Vec3{0, 12, 0});
    tf::detail::base = f.image.base();
    const auto gather = [&] { return tf::detail::gather(f.w, f.tank.actor.ptr(), brain, {}, {}); };
    auto contacts = gather();
    assert(contacts.count == 1 && contacts.rows[0].entity == f.enemy.actor.ptr() && !contacts.rows[0].threat.visible);
    auto threat = contacts.rows[0].threat;
    assert(!tf::policy::evaluate_facing(0, &threat, 1).should_rotate);
    f.soldier.put(0x58, 2.f); // A stock anti-tank rating alone cannot turn the hull toward a known rifleman.
    contacts = gather();
    assert(contacts.count == 1 && contacts.rows[0].entity == f.enemy.actor.ptr());
    f.w.now += 501;
    contacts = gather();
    assert(contacts.count == 1 && contacts.rows[0].entity == f.rifle.actor.ptr());
    threat = contacts.rows[0].threat;
    assert(tf::policy::evaluate_facing(0, &threat, 1).should_rotate);
    tf::detail::base = 0;
    using ef::policy::suppress_coax_slew;
    assert(!suppress_coax_slew(179 * degree, -179 * degree, false, false, false, 90, true));
    assert(!suppress_coax_slew(0, 1, true, true, false, 90, true));
    assert(!suppress_coax_slew(0, 1, true, false, true, 90, true));
    assert(!suppress_coax_slew(0, 1, true, false, false, 90, false));
    assert(!suppress_coax_slew(0, 20 * degree, false, false, false, 90, true));
    assert(suppress_coax_slew(0, 20.1f * degree, false, false, false, 90, true));
}

void cannon_settle() {
    Fixture f;
    Memory<0x280> weapon{};
    Memory<0x1700> def{};
    Memory<0x90> aimer{}, target{}, charger{};
    Memory<0x160> bone{};
    Memory<0x60> tag{};
    f.enemy.actor.ename(0xde8, "car");
    weapon.put(0x90, f.tank.actor.ptr());
    weapon.put(0x98, def.ptr());
    weapon.put(0xa8, aimer.ptr());
    weapon.put(0xb0, charger.ptr());
    aimer.put(0x20, bone.ptr());
    def.put(0x1628, 75.f);
    def.put(0x470, 1);
    target.put(8, 1);
    target.put(0x18, f.enemy.actor.ptr());
    pr::Assignment assignment;
    assignment.brain = f.tank.brain.ptr();
    pr::scope.assignment = &assignment;
    at::detail::accepting = true;
    su::main_tid = GetCurrentThreadId();
    su::actor_tls = TlsAlloc();
    assert(su::actor_tls != TLS_OUT_OF_INDEXES);
    assert(TlsSetValue(su::actor_tls, reinterpret_cast<void*>(f.tank.actor.ptr())));
    const auto caller = f.image.base() + 0x9a85d1;
    auto fire = [&](unsigned now) {
        f.image.put(0x1584568, now);
        return at::detail::option_for(weapon.ptr(), target.ptr(), 3, 2, caller);
    };
    bone.put(0xa8, rt::XY{0, 1});
    for (int i = 1; i <= 8; ++i) {
        f.gate(i);
        assert(fire(1000) == 3);
    }
    f.gate(0);
    cfg::active().modules.penetration_at = false;
    assert(fire(1000) == 3);
    cfg::active().modules.penetration_at = true;
    cfg::active().modules.cannon_aim_settle = false;
    assert(fire(1000) == 3);
    cfg::active().modules.cannon_aim_settle = true;
    assert(at::detail::option_for(weapon.ptr(), target.ptr(), 3, 2, caller - 1) == 3);
    assert(at::detail::option_for(weapon.ptr(), target.ptr(), 3, 3, caller) == 3);
    assert(at::detail::option_for(weapon.ptr(), target.ptr(), 1, 2, caller) == 1);
    for (auto key : {"mgun", "flame", "autocannon"}) {
        tag.ename(0, key);
        def.put(0x11e0, tag.ptr());
        def.put(0x11e8, tag.ptr() + 0x60);
        assert(fire(1000) == 3);
    }
    def.put(0x11e8, tag.ptr());
    def.put(0x470, 3);
    assert(fire(1000) == 3);
    def.put(0x470, 2);
    def.put(0x1628, 36.9f);
    assert(fire(1000) == 3);
    def.put(0x1628, 75.f);
    assert(at::detail::settle_tracks.empty());
    assert(fire(1000) == 0);
    aimer.put(0x18, 3);
    bone.put(0xa8, rt::XY{1, 0});
    assert(fire(1010) == 0);
    bone.put(0xa8, rt::XY{1, .06f});
    assert(fire(1040) == 0); // Break alignment above 2.5 degrees.
    bone.put(0xa8, rt::XY{1, 0});
    aimer.put(0x18, 4);
    assert(fire(1050) == 0 && fire(1099) == 0 && fire(1100) == 3);
    assert(fire(1120) == 3);
    const auto hits = at::detail::settle_holds.load();
    assert(hits == 5);
    auto acquire = [&](unsigned id, pr::Vec3 pos) {
        f.enemy.actor.put(0xc0, id);
        f.armor.put(0x28, pos);
    };
    acquire(19, {0, 80, 0});
    aimer.put(0x18, 1);
    assert(fire(1200) == 0 && fire(1379) == 0 && fire(1380) == 3 && fire(1381) == 3);
    acquire(20, {0, -35, 0});
    assert(fire(1400) == 3);
    f.armor.put(0x28, pr::Vec3{0, -60, 0});
    assert(fire(1401) == 3);
    acquire(21, {-60, 0, 0});
    assert(fire(1500) == 0);
    f.weaponry.put(0x90, 1500u);
    assert(fire(1501) == 3);
    f.weaponry.put(0x90, 0u);
    assert(fire(1502) == 3);
    // Clock rewind and a scene replacement must each require a fresh acquisition.
    assert(fire(1100) == 0);
    f.weaponry.put(0x90, 1100u);
    assert(fire(1101) == 3);
    f.weaponry.put(0x90, 0u);
    f.image.put(0x15c7d58, f.next_scene.ptr());
    assert(fire(1102) == 0);
    // Ground-target rules can return a native point for a selected visible entity.
    target.put(8, 2);
    target.put(0x18, Address{});
    target.put(0xc, pr::Vec3{0, -80, 0});
    assert(fire(1103) == 3);
    assignment.proposed_weapon = weapon.ptr();
    assignment.proposed_target = f.enemy.actor.ptr();
    assert(fire(1104) == 0);
    target.put(8, 3);
    target.put(0x18, f.enemy.actor.ptr());
    assert(fire(1105) == 0);
    assert(fire(1282) == 3);
    // A previously released gun left idle can make another large traverse to the same entity.
    assert(fire(1500) == 0);
    at::after_hooks_removed();
    assert(!at::detail::settle_holds && at::detail::settle_tracks.empty());
    assert(TlsSetValue(su::actor_tls, nullptr));
    TlsFree(su::actor_tls);
    su::actor_tls = TLS_OUT_OF_INDEXES;
    su::main_tid = 0;
}

void medium_cannon_and_dirty_bone() {
    at::p::AimSettleState st{};
    const float unknown = std::numeric_limits<float>::quiet_NaN();
    auto hold = [&](bool changed, float yaw, unsigned now, int aimer) {
        return at::p::should_hold_for_settle(st, changed, yaw * degree, unknown, aimer, true, 70, 57, now, 100, 1.8f);
    };
    assert(!hold(true, 179, 0, 1) && !hold(true, -179, 1, 1));
    assert(hold(true, 90, 10, 1));
    assert(hold(false, 90, 15, 3) && hold(false, 90, 49, 4) && !hold(false, 90, 50, 4));
    assert(!hold(false, 91, 51, 1));
}
} // namespace

int main() {
    allegiance_and_towing();
    armor_lifetime();
    turret_and_hull();
    cannon_settle();
    medium_cannon_and_dirty_bone();
    std::puts("Stream B tank combat: 5 fixture groups passed.");
}
