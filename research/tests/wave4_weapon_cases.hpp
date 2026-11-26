#pragma once
#include "../../mods/combat_logistics_module.hpp"
#include "vehicle_fixture_26734796.hpp"

namespace goh::test::wave4_weapon {
namespace cl = combat_logistics;
namespace d = cl::detail;
namespace s = ai_support;
using runtime::Address;
using runtime::Byte;
using runtime::Tick;
using vehicle::Memory;

struct World {
    vehicle::FakeImage image;
    vehicle::FakeHuman human, infantry;
    Memory<0x500> brain;
    Memory<0x580> root;
    Memory<0x300> sensor, weapon;
    Memory<0xe0> item, pistol;
    Memory<0x300> manager, box;
    Memory<0x1200> launcher, rifle, sidearm;
    Memory<0xc0> launcher_tags, rifle_tags, smoke_key, sidearm_tags;
    Memory<0x100> subject, charger, ctx, order, take;
    Memory<0xe00> tank;
    std::array<Address, 2> items{}, subjects{};
    auto_smoke::World w{};
    bool remote{}, worker{}, serial{true}, selected{}, managing{}, missile{}, dry{}, dummy{};
    bool stock_result{true}, mount{true};
    int reserve{}, calls{}, native_reads{};
    Address hand{};
    inline static World* self{};

    World() {
        self = this;
        s::base = image.base();
        w = {s::base, 100, 1000, 1.f, 100.f};
        human.setup(s::base, 11, 11, 0, 0);
        infantry.setup(s::base, 22, 22, 1, 0);
        std::memcpy(brain.data.data(), human.brain.data.data(), human.brain.data.size());
        human.actor.put(0xdc8, brain.ptr());
        human.actor.put(0xdd0, sensor.ptr());
        brain.put(0xe0, root.ptr());
        root.put(0, s::base + 0x1110cd0);
        root.put(0x30, brain.ptr());
        brain.put(0x3b8, subject.ptr());
        sensor.put(0x260, human.actor.ptr());
        subjects = {subject.ptr(), 0};
        sensor.put(0x280,
                   s::Span{reinterpret_cast<Address>(subjects.data()), reinterpret_cast<Address>(subjects.data() + 1)});
        subject.put(0x10, 3u << 28);
        subject.put(0x14, 3u);
        subject.put(0x18, tank.ptr());
        subject.put(0x28, auto_smoke::Vec3{12, 0, 0});
        subject.put(0x64, w.now);
        tank.put(0x30, Byte(0xb));
        tank.put(0xdb4, 0x100u);
        tank.put(0xda8, Byte(1));
        human.agg.put(0x28, manager.ptr());
        manager.put(0xa0, box.ptr());
        box.put(0x50, human.actor.ptr());
        items = {item.ptr(), pistol.ptr()};
        box.put(0x10, s::Span{reinterpret_cast<Address>(items.data()), reinterpret_cast<Address>(items.data() + 2)});
        rifle_tags.ename(0, "weapon");
        rifle_tags.ename(0x60, "rifle");
        rifle.put(0x11e0, s::Span{rifle_tags.ptr(), rifle_tags.ptr() + 0xc0});
        rifle.put(0x438, s::Span{rifle_tags.ptr(), rifle_tags.ptr() + 0x60});
        launcher_tags.ename(0, "weapon");
        launcher_tags.ename(0x60, "rocketlauncher");
        launcher.put(0x11e0, s::Span{launcher_tags.ptr(), launcher_tags.ptr() + 0xc0});
        sidearm_tags.ename(0, "weapon");
        sidearm_tags.ename(0x60, "shortsmg");
        sidearm.put(0x11e0, s::Span{sidearm_tags.ptr(), sidearm_tags.ptr() + 0xc0});
        item.put(0x40, launcher.ptr());
        item.put(0x48, 1);
        item.put(0x30, box.ptr());
        pistol.put(0x40, sidearm.ptr());
        pistol.put(0x48, 1);
        pistol.put(0x30, box.ptr());
        pistol.put(0x50, Address(1));
        pistol.put(0xdc, 1);
        weapon.put(0x90, human.actor.ptr());
        weapon.put(0x98, rifle.ptr());
        weapon.put(0xb0, charger.ptr());
        take.put(0, s::base + 0x110aa28);
        hand = rifle.ptr();
    }
    static Byte stock(Address brain, Address ctx) {
        ++self->calls;
        if (self->mount && self->stock_result) runtime::write(ctx, 8, self->take.ptr());
        // Model the stock chooser's new lockout even when it finds no replacement.
        if (runtime::read<Tick>(brain, 0x47c) == self->w.now) runtime::write(brain, 0x47c, self->w.now + 500);
        return self->stock_result;
    }
    static Address resolve(Address, Address rva) {
        ++self->native_reads;
        switch (rva) {
            case 0x7e05b0: return reinterpret_cast<Address>(+[](Address) -> Byte { return self->managing; });
            case 0x91b190: return reinterpret_cast<Address>(+[](Address) -> Byte { return self->selected; });
            case 0x95e590: return reinterpret_cast<Address>(+[](Address) -> Byte { return self->missile; });
            case 0x7e04e0: return reinterpret_cast<Address>(+[](Address) -> Address { return self->hand; });
            case 0x81b080: return reinterpret_cast<Address>(+[](Address) -> Address { return self->weapon.ptr(); });
            case 0x820ce0: return reinterpret_cast<Address>(+[](Address) -> Byte { return self->dummy; });
            case 0x820c50: return reinterpret_cast<Address>(+[](Address) -> Byte { return self->dry; });
            default: assert(false); return 0;
        }
    }
    bool run(Tick lock = 2000) {
        const auto fast = cl::stat_fast_swaps.load();
        const auto calls_before = calls;
        ctx.put(8, Address(0));
        brain.put(0x47c, lock);
        assert(d::swap(brain.ptr(), ctx.ptr()) == stock_result);
        assert(calls == calls_before + 1);
        return cl::stat_fast_swaps == fast + 1;
    }
};
inline void run() {
    World f;
    auto& auth = runtime::table().auth;
    const auto saved = auth;
    auth.is_remote_client = reinterpret_cast<Address>(+[]() -> Byte { return World::self->remote; }) - s::base;
    auth.is_quant_worker = reinterpret_cast<Address>(+[]() -> Byte { return World::self->worker; }) - s::base;
    auto create = [&](void* target, void* hook, void** orig) {
        assert(reinterpret_cast<Address>(target) == s::base + 0x95cea0 && hook == reinterpret_cast<void*>(d::swap));
        *orig = reinterpret_cast<void*>(World::stock);
        return 0;
    };
    auto remove = [](void*) {
        assert(false);
        return 1;
    };
    assert(!cl::prepare_hooks(s::base, "mismatch", create, remove));
    assert(!cl::prepare_hooks(s::base, runtime::kBuild26734796Sha256, [](void*, void*, void**) { return 1; }, remove));
    assert(cl::prepare_hooks(s::base, runtime::kBuild26734796Sha256, create, remove));
    assert(!cl::prepare_hooks(s::base, runtime::kBuild26734796Sha256, create, remove));
    d::ops.context = [](auto_smoke::World& w) -> Address {
        const auto& fixture = *World::self;
        w = fixture.w;
        return fixture.serial && runtime::is_host_main_thread(w.base) ? fixture.human.actor.ptr() : 0;
    };
    d::ops.resolve = World::resolve;
    d::ops.reserve = [](Address, Address) { return World::self->reserve; };
    runtime::global_accepting = true;

    // A passive callback must delegate without touching any native helper or timer.
    for (bool* field : {&f.remote, &f.worker}) {
        *field = true;
        assert(!f.run() && !f.native_reads && f.brain.get<Tick>(0x47c) == 2000);
        *field = false;
    }
    f.serial = false;
    assert(!f.run() && !f.native_reads);
    f.serial = true;
    config::active().modules.fast_weapon_swap = false;
    assert(!f.run() && !f.native_reads);
    config::active().modules.fast_weapon_swap = true;
    for (unsigned flags : {1u, 1u << 28}) {
        f.brain.put(0x39c, flags);
        assert(!f.run() && !f.native_reads);
    }
    f.brain.put(0x39c, 0u);
    f.human.chassis.put(0x18, 1);
    assert(!f.run());
    f.human.chassis.put(0x18, 0);
    f.human.weaponry.put(0x70, Byte(2));
    assert(!f.run());
    f.human.weaponry.put(0x70, Byte(0));
    for (unsigned pol : {1u << 10, 2u << 10, 2u}) {
        f.brain.put(0x398, pol);
        assert(!f.run());
    }
    f.brain.put(0x398, 1u | (1u << 12)); // player, Hold Position, Fire At Will
    f.brain.put(0xf0, f.order.ptr());
    f.order.put(0, s::base + runtime::table().vt.attack_order);
    f.order.put(8, f.human.actor.ptr() + 0xaf0);
    assert(!f.run());
    f.brain.put(0xf0, Address(0));
    f.human.vit.put(0x174, 1u);
    assert(!f.run());
    f.human.vit.put(0x174, 0u);
    f.brain.put(0, s::base + 0x110ff80);
    assert(!f.run());
    f.brain.put(0, s::base + runtime::table().vt.human_brain);
    for (bool* field : {&f.selected, &f.managing}) {
        *field = true;
        assert(!f.run());
        *field = false;
    }
    assert(!f.run(0) && !f.run(f.w.now)); // zero = unscheduled, elapsed = stock
    f.subject.put(0x14, 0u);
    assert(!f.run());
    f.subject.put(0x14, 3u);
    f.subject.put(0x6c, Byte(1));
    assert(!f.run());
    f.subject.put(0x6c, Byte(0));
    f.subject.put(0x10, 1u << 28);
    f.subject.put(0x64, f.w.now - 501);
    assert(!f.run()); // 5.01 s
    f.subject.put(0x64, f.w.now - 500);
    f.sensor.put(0x260, Address(0));
    assert(!f.run());
    f.sensor.put(0x260, f.human.actor.ptr());
    Memory<0x60> destroyed_tag;
    destroyed_tag.ename(0, "abandoned");
    f.tank.put(0x4e8, s::Span{destroyed_tag.ptr(), destroyed_tag.ptr() + 0x60});
    assert(!f.run());
    f.tank.put(0x4e8, s::Span{});
    f.item.put(0x48, 0);
    assert(!f.run());
    f.item.put(0x48, 1);
    f.tank.put(0xdb4, 0u);
    assert(!f.run());
    f.tank.put(0xdb4, 0x100u);
    f.hand = f.launcher.ptr();
    assert(!f.run());
    f.hand = f.rifle.ptr();
    assert(f.run()); // equipped rifle + active vehicle + carried AT weapon
    f.subject.put(0x10, 3u << 28);
    ++f.w.now;
    assert(!f.run()); // private cooldown
    f.w.now += 99;
    f.mount = false;
    assert(!f.run() && f.brain.get<Tick>(0x47c) == f.w.now + 500); // return 1 from a different stock branch
    f.mount = true;
    assert(!f.run()); // no repeat even after the unsuccessful chooser
    f.human.actor.put(0xc0, 12u);
    assert(f.run()); // recycled native address with a different entity ID
    ++f.w.scene;
    assert(f.run()); // same tick, new scene
    f.w.now -= 50;
    assert(f.run()); // rewound clock
    f.subject.put(0x18, f.infantry.actor.ptr());
    f.weapon.put(0x98, f.launcher.ptr());
    f.w.now += 100;
    f.stock_result = false;
    assert(!f.run());
    f.stock_result = true;
    f.w.now += 100;
    assert(f.run()); // launcher against infantry
    f.weapon.put(0x98, f.rifle.ptr());
    f.w.now += 100;
    assert(!f.run()); // loaded
    f.dry = true;
    f.charger.put(0x14, Byte(4));
    assert(!f.run()); // reloading
    f.charger.put(0x14, Byte(0));
    f.dummy = true;
    assert(!f.run()); // infinite/dummy charger
    f.dummy = false;
    f.reserve = -1;
    assert(!f.run()); // unknown inventory
    f.reserve = 0;
    assert(f.run()); // no reserve
    f.w.now += 100;
    f.reserve = 8;
    f.subject.put(0x28, auto_smoke::Vec3{15, 0, 0});
    assert(!f.run());
    f.subject.put(0x28, auto_smoke::Vec3{14.99f, 0, 0});
    f.pistol.put(0xdc, 0);
    assert(!f.run());
    f.pistol.put(0xdc, 1);
    assert(f.run()); // loaded backup with reserve for the rifle

    f.subject.put(0x18, f.tank.ptr());
    f.subject.put(0x10, 1u << 28);
    f.subject.put(0x64, f.w.now - 1000);
    f.subject.put(0x28, auto_smoke::Vec3{60, 0, 0});
    f.brain.put(0x3a4, f.w.now + 51);
    f.missile = true;
    assert(!f.run(0) && !cl::stat_missile_holds);
    f.brain.put(0x3a4, f.w.now + 50);
    assert(!f.run(0) && cl::stat_missile_holds == 1 && f.brain.get<Tick>(0x3a4) == f.w.now + 250);
    f.brain.put(0x3a4, f.w.now);
    f.subject.put(0x64, f.w.now - 1001);
    f.run(0);
    assert(cl::stat_missile_holds == 1);
    f.subject.put(0x10, 3u << 28);
    f.subject.put(0x28, auto_smoke::Vec3{60.01f, 0, 0});
    f.run(0);
    assert(cl::stat_missile_holds == 1);
    f.subject.put(0x28, auto_smoke::Vec3{60.f, 0, 0});
    f.smoke_key.ename(0, "grenade");
    f.smoke_key.ename(0x60, "anm8_smoke");
    f.rifle.put(0xb80, s::Span{f.smoke_key.ptr(), f.smoke_key.ptr() + 0xc0});
    f.run(0);
    assert(cl::stat_missile_holds == 1 && f.brain.get<Tick>(0x3a4) == f.w.now);
    cl::deactivate();
    assert(!f.run() && !cl::inflight_count());
    cl::after_hooks_removed();
    assert(!d::original && !d::scene && d::attempts.empty());
    runtime::global_accepting = false;
    s::base = 0;
    World::self = nullptr;
    auth = saved;
}
} // namespace goh::test::wave4_weapon
