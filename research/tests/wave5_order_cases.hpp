#pragma once
#include "../../mods/lof_detour_module.hpp"
#include "vehicle_fixture_26734796.hpp"

namespace goh::test::wave5_order {
namespace co = combat_orders;
namespace d = co::detail;
namespace lof = lof_detour::detail;
using runtime::Address;
using runtime::Byte;
using runtime::Tick;
using vehicle::Memory;

struct Fixture {
    vehicle::FakeImage pe;
    vehicle::FakeHuman human, enemy;
    Memory<0x108> order;
    Memory<0x290> sensor;
    Memory<0x80> subject;
    Memory<0x200> weapon;
    Memory<0x30> aimer, charger;
    Memory<0x1200> weapon_def;
    Memory<0xc0> tags;
    std::array<Address, 1> subjects{}, places{};
    auto_smoke::World w{};
    bool serial{true}, remote{}, worker{}, visible{true}, fog{true}, blocked{}, cannon{};
    float weapon_range{20};
    bool engaged{}, restricted{}, empty{}, dummy{}, swap_weapon{};
    int advances{}, attacks{}, fires{}, stock_level{2}, pose_checks{};
    Tick stock_melee{};
    static inline Fixture* f{};
    Fixture() {
        f = this;
        const auto base = pe.base();
        w = {base, 0x1234, 1000, 2, 100};
        human.setup(base, 100, 200, 1, 0);
        enemy.setup(base, 101, 201, 2, 0);
        human.brain.put(0xf0, order.ptr());
        human.brain.put(0x14c, 20.f);
        human.actor.put(0xdd0, sensor.ptr());
        sensor.put(0x260, human.actor.ptr());
        enemy.actor.put(0x74, auto_smoke::Vec3{3, 0, 0});
        subject.put(0x10, 3u << 28);
        subject.put(0x14, 2u);
        subject.put(0x18, enemy.actor.ptr());
        subject.put(0x28, auto_smoke::Vec3{3, 0, 0});
        subjects[0] = subject.ptr();
        places[0] = weapon.ptr();
        weapon.put(0x90, human.actor.ptr());
        weapon.put(0xa8, aimer.ptr());
        weapon.put(0x98, weapon_def.ptr());
        weapon.put(0xa0, weapon_def.ptr());
        weapon.put(0xb0, charger.ptr());
        tags.ename(0, "rifle");
        weapon_def.put(0x11e0, ai_support::Span{tags.ptr(), tags.ptr() + 0x60});
        order.put(8, human.actor.ptr() + 0xaf0);
        order.put(0x58, enemy.actor.ptr());
    }
    static Byte is_remote() {
        return f->remote;
    }
    static Byte is_worker() {
        return f->worker;
    }
    static Address context(auto_smoke::World& world) {
        world = f->w;
        return f->serial ? f->human.actor.ptr() : 0;
    }
    static Address resolve(Address base, Address rva) {
        assert(base == f->pe.base());
        switch (rva) {
            case 0x945b60:
                return reinterpret_cast<Address>(+[](Address a) -> Address {
                    assert(a == f->human.actor.ptr());
                    return f->cannon;
                });
            case 0x8e7090:
                return reinterpret_cast<Address>(+[](Address o) -> Byte {
                    assert(f->cannon && o == f->order.ptr());
                    return f->engaged;
                });
            case 0x95d710:
                return reinterpret_cast<Address>(+[](Address brain, Address sub) -> Byte {
                    assert(brain == f->human.brain.ptr() && sub == f->subject.ptr());
                    return f->restricted;
                });
            case 0x81f710:
                return reinterpret_cast<Address>(+[](Address weapon, Address target) -> float {
                    assert(weapon == f->weapon.ptr() && target == f->enemy.actor.ptr());
                    return f->weapon_range;
                });
            case 0xc2b670: return reinterpret_cast<Address>(+[]() -> Byte { return f->fog; });
            case 0x809b30:
                return reinterpret_cast<Address>(+[](Address entity_sub, const Byte* player) -> Byte {
                    assert(entity_sub == f->enemy.actor.ptr() + 0xbe0 && *player == 1);
                    return f->visible;
                });
            case 0x81b080:
                return reinterpret_cast<Address>(+[](Address wp) -> Address {
                    assert(wp == f->human.weaponry.ptr());
                    return f->swap_weapon ? 0 : f->weapon.ptr();
                });
            case 0x820ce0:
                return reinterpret_cast<Address>(+[](Address ch) -> Byte {
                    assert(ch == f->charger.ptr());
                    return f->dummy;
                });
            case 0x820c50:
                return reinterpret_cast<Address>(+[](Address ch) -> Byte {
                    assert(ch == f->charger.ptr());
                    return f->empty;
                });
            default: assert(false); return 0;
        }
    }
    static bool hard_cover(const auto_smoke::World&, Address actor, Address sub) {
        assert(actor == f->human.actor.ptr() && sub == f->subject.ptr());
        ++f->pose_checks;
        return f->blocked;
    }
    static void advance(Address o) {
        assert(o == f->order.ptr() && d::gate.count() == 1);
        ++f->advances;
        f->order.put(0x40, f->stock_level);
    }
    static std::uint64_t attack(Address o) {
        assert(o == f->order.ptr() && d::gate.count() == 1);
        ++f->attacks;
        f->stock_melee = f->order.get<Tick>(0xf8);
        return 17;
    }
    static std::uint64_t fire(Address o) {
        assert(o == f->order.ptr() && lof::gate.count() == 1);
        ++f->fires;
        if (!f->fog) return 0;
        if (f->visible) f->order.put(0xec, f->w.now);
        return f->w.now - f->order.get<Tick>(0xec) > f->w.duration(6) ? 2 : 0;
    }
    void vehicle(bool on) {
        human.actor.put<Byte>(0x30, on ? 11 : 7);
        human.brain.put(0, pe.base() + (on ? runtime::table().vt.vehicle_brain : runtime::table().vt.human_brain));
    }
    void expect_level(int level) {
        const int before = advances;
        order.put(0x40, -1);
        d::hook_advance(order.ptr());
        assert(advances == before + 1 && order.get<int>(0x40) == level);
    }
    void expect_fire(Tick now, std::uint64_t result) {
        w.now = now;
        const auto before = fires;
        assert(lof::hook_attack_check_and_fire(order.ptr()) == result && fires == before + 1);
    }
    void expect_melee(bool defer) {
        const auto before = attacks;
        const auto timer = order.get<Tick>(0xf8);
        assert(d::hook_attack(order.ptr()) == 17 && attacks == before + 1);
        assert(stock_melee == (defer ? w.now + w.duration(1) : timer));
        assert(order.get<Tick>(0xf8) == timer);
    }
};

inline void advance_cases(Fixture& f) {
    const auto& vt = runtime::table().vt;
    const auto a = f.human.actor.ptr();
    f.order.put(0, f.pe.base() + vt.advance_order);
    f.order.put(0x3c, 3);
    f.order.put(0x58, Tick(850));
    f.human.brain.put(0x18, Tick(800));
    f.w.now = 999;
    f.expect_level(2);
    f.w.now = 1000;
    f.expect_level(1);
    f.human.brain.put(0x18, Tick(801));
    f.expect_level(2);
    f.human.brain.put(0x18, Tick(800));
    f.order.put(0x30, Address(1));
    f.expect_level(2);
    f.order.put(0x30, Address(0));
    f.order.put(0x3c, 4);
    f.expect_level(1);
    f.human.brain.put(0x3b8, f.subject.ptr());
    f.expect_level(2);
    f.blocked = true;
    f.expect_level(1);
    assert(f.pose_checks >= 2);
    f.blocked = false;
    f.restricted = true;
    f.expect_level(1);
    f.restricted = false;
    f.subject.put(0x10, 1u << 28);
    f.subject.put(0x64, Tick(850));
    f.expect_level(2);
    f.subject.put(0x64, Tick(849));
    f.expect_level(1);
    f.subject.put(0x10, 3u << 28);
    f.enemy.vit.put<Byte>(0x174, 1);
    f.expect_level(1);
    f.enemy.vit.put<Byte>(0x174, 0);
    f.subject.put(0x28, auto_smoke::Vec3{21, 0, 0});
    f.expect_level(1);
    f.subject.put(0x28, auto_smoke::Vec3{3, 0, 0});
    f.swap_weapon = true;
    f.expect_level(1);
    f.swap_weapon = false;
    f.vehicle(true);
    f.order.put(0x3c, 2);
    f.human.brain.put(0x18, Tick(701));
    f.expect_level(2);
    f.human.brain.put(0x18, Tick(700));
    f.expect_level(1);
    const auto subjects = reinterpret_cast<Address>(f.subjects.data());
    f.sensor.put(0x280, ai_support::Span{subjects, subjects + 8});
    f.expect_level(2);
    f.subject.put(0x10, 1u << 28);
    f.expect_level(1);
    const auto places = reinterpret_cast<Address>(f.places.data());
    f.human.weaponry.put(0x30, ai_support::Span{places, places + 8});
    f.weapon.put(0xd0, 1);
    f.weapon.put(0xe0, f.enemy.actor.ptr());
    f.expect_level(2);
    f.weapon_range = 2;
    f.expect_level(1);
    f.weapon_range = 20;
    f.aimer.put(0x18, 7);
    f.expect_level(1);
    f.aimer.put(0x18, 1);
    f.visible = false;
    f.expect_level(1);
    f.visible = true;
    f.stock_level = 1;
    f.cannon = true;
    f.order.put(0x6c, Tick(1000));
    f.human.brain.put(0x18, Tick(601));
    f.expect_level(1);
    f.human.brain.put(0x18, Tick(600));
    f.expect_level(0);
    f.engaged = true;
    f.expect_level(0);
    f.human.brain.put(0x3a0, ai_support::Span{subjects, subjects + 8});
    f.expect_level(1);
    f.subject.put(0x28, auto_smoke::Vec3{15, 0, 0});
    f.expect_level(0);
    f.engaged = false;
    f.order.put(0x6c, Tick(0));
    f.expect_level(1);
    f.order.put(0x6c, Tick(1001));
    f.expect_level(1);
    f.order.put(0x6c, Tick(1000));
    f.expect_level(0);
    for (bool* flag : {&f.remote, &f.worker}) {
        *flag = true;
        f.expect_level(1);
        *flag = false;
    }
    f.serial = false;
    f.expect_level(1);
    f.serial = true;
    f.human.brain.put(0x39c, 1u);
    f.expect_level(1);
    f.human.brain.put(0x39c, 0u);
    f.human.chassis.put(0x18, 1);
    f.expect_level(1);
    f.human.chassis.put(0x18, 0);
    f.human.weaponry.put<Byte>(0x70, 2);
    f.expect_level(1);
    f.human.weaponry.put<Byte>(0x70, 0);
    f.human.brain.put(0xf0, Address(0));
    f.expect_level(1);
    f.human.brain.put(0xf0, f.order.ptr());
    f.order.put(8, a);
    f.expect_level(1);
    f.order.put(8, a + 0xaf0);
    f.human.brain.put(0, f.pe.base() + 0x100);
    f.expect_level(1);
    f.vehicle(false);
    f.cannon = false;
    f.stock_level = 2;
    f.order.put(0x3c, 4);
    f.human.brain.put(0x3b8, Address(0));
    config::active().modules.advance_responsive = false;
    f.expect_level(2);
    config::active().modules.advance_responsive = true;
    f.expect_level(1);
}

inline void attack_cases(Fixture& f) {
    auto& m = config::active().modules;
    m.soft_cover_lof = m.attack_detour = false;
    f.order.put(0, f.pe.base() + runtime::table().vt.attack_order);
    f.order.put(0x58, f.enemy.actor.ptr());
    f.expect_fire(1000, 0);
    const auto count = co::stat_attack_persists.load();
    f.visible = false;
    f.expect_fire(1449, 0);
    assert(co::stat_attack_persists == count);
    f.expect_fire(1450, 0);
    assert(co::stat_attack_persists == count + 1 && f.order.get<Tick>(0xec) == 1000);
    f.expect_fire(1601, 0);
    f.expect_fire(2399, 0);
    f.expect_fire(2400, 2);
    f.expect_fire(2450, 2);
    f.visible = true;
    f.expect_fire(3000, 0);
    f.visible = false;
    f.expect_fire(3780, 0);
    m.attack_order_polish = false;
    f.expect_fire(3781, 2);
    m.attack_order_polish = true;
    f.enemy.vit.put<Byte>(0x174, 2);
    f.expect_fire(3782, 2);
    f.enemy.vit.put<Byte>(0x174, 0);
    f.expect_fire(3783, 2);
    f.visible = true;
    f.expect_fire(4000, 0);
    f.visible = false;
    f.enemy.actor.put(0xc0, 999u);
    f.expect_fire(4700, 2);
    f.visible = true;
    f.expect_fire(5000, 0);
    f.visible = false;
    ++f.w.scene;
    f.expect_fire(5700, 2);
    f.visible = true;
    f.expect_fire(6000, 0);
    f.visible = false;
    f.order.put(0xec, Tick(1000));
    f.expect_fire(5900, 2);
    f.visible = true;
    f.enemy.actor.put<Byte>(0x30, 11);
    f.expect_fire(7000, 0);
    f.visible = false;
    f.expect_fire(7700, 0);
    Memory<0x60> destruction;
    destruction.ename(0, "destroyed");
    f.enemy.actor.put(0x4e8, ai_support::Span{destruction.ptr(), destruction.ptr() + 0x60});
    f.expect_fire(7701, 2);
    f.enemy.actor.put(0x4e8, ai_support::Span{});
    f.enemy.actor.put<Byte>(0x30, 7);
    f.visible = true;
    f.expect_fire(8000, 0);
    f.visible = false;
    for (bool* b : {&f.remote, &f.worker}) {
        *b = true;
        f.expect_fire(8700, 2);
        *b = false;
    }
    f.human.brain.put(0x39c, 7u);
    f.expect_fire(8700, 2);
    f.human.brain.put(0x39c, 0u);
    f.expect_fire(8700, 0);
    f.visible = true;
    f.w.now = 9000;
    f.order.put(0xf8, Tick(9000));
    for (auto tag : {"rifle", "smg", "shortsmg", "pistol", "revolver", "mgun"}) {
        f.tags.ename(0, tag);
        f.expect_melee(true);
    }
    f.weapon_def.put(0x11e0, ai_support::Span{f.tags.ptr(), f.tags.ptr() + 0xc0});
    for (auto tag : {"rocketlauncher", "grenade", "missile", "melee", "knife"}) {
        f.tags.ename(0x60, tag);
        f.expect_melee(false);
    }
    f.weapon_def.put(0x11e0, ai_support::Span{f.tags.ptr(), f.tags.ptr() + 0x60});
    for (bool* b : {&f.empty, &f.dummy, &f.swap_weapon, &f.remote, &f.worker}) {
        *b = true;
        f.expect_melee(false);
        *b = false;
    }
    f.charger.put<Byte>(0x14, 4);
    f.expect_melee(false);
    f.charger.put<Byte>(0x14, 0);
    f.visible = false;
    f.expect_melee(false);
    f.visible = true;
    f.enemy.actor.put(0x74, auto_smoke::Vec3{12, 0, 0});
    f.expect_melee(false);
    f.enemy.actor.put(0x74, auto_smoke::Vec3{11, 0, 0});
    f.expect_melee(true);
    f.order.put(0xf8, Tick(0));
    f.expect_melee(false);
    f.order.put(0xf8, Tick(9001));
    f.expect_melee(false);
    f.order.put(0xf8, Tick(9000));
    m.attack_order_polish = false;
    f.expect_melee(false);
    m.attack_order_polish = true;
}

inline void run() {
    Fixture f;
    const auto image = f.pe.base();
    auto& auth = runtime::table().auth;
    const auto saved = auth;
    const auto modules = config::active().modules;
    const bool accepting = runtime::global_accepting.exchange(true);
    auth.is_remote_client = reinterpret_cast<Address>(Fixture::is_remote) - image;
    auth.is_quant_worker = reinterpret_cast<Address>(Fixture::is_worker) - image;
    const auto create = [&](void* target, void* detour, void** orig) {
        if (reinterpret_cast<Address>(target) == image + 0x8e79f0) {
            assert(detour == reinterpret_cast<void*>(d::hook_advance));
            *orig = reinterpret_cast<void*>(Fixture::advance);
        } else {
            assert(reinterpret_cast<Address>(target) == image + 0x904930 &&
                   detour == reinterpret_cast<void*>(d::hook_attack));
            *orig = reinterpret_cast<void*>(Fixture::attack);
        }
        return 0;
    };
    int removed = 0;
    const auto remove = [&](void* target) {
        assert(reinterpret_cast<Address>(target) == image + 0x8e79f0);
        ++removed;
        return 0;
    };
    assert(!co::prepare_hooks(image, "wrong", create, remove));
    const auto fail_second = [&](void* t, void* h, void** o) {
        return reinterpret_cast<Address>(t) == image + 0x904930 ? 1 : create(t, h, o);
    };
    assert(!co::prepare_hooks(image, runtime::kBuild26734796Sha256, fail_second, remove) && removed == 1);
    assert(!co::prepare_hooks(image, runtime::kBuild26734796Sha256, fail_second, [](void*) { return 1; }));
    assert(!co::prepare_hooks(image, runtime::kBuild26734796Sha256, create, remove));
    co::after_hooks_removed();
    assert(co::prepare_hooks(image, runtime::kBuild26734796Sha256, create, remove));
    assert(!co::prepare_hooks(image, runtime::kBuild26734796Sha256, create, remove));
    d::ops = {Fixture::context, Fixture::resolve, Fixture::hard_cover};
    lof::base = image;
    lof::orig_attack_check_fire = Fixture::fire;
    lof::gate.activate();
    advance_cases(f);
    attack_cases(f);
    co::deactivate();
    f.expect_melee(false);
    assert(co::inflight_count() == 0 && runtime::global_inflight == 0);
    co::after_hooks_removed();
    assert(d::watches.empty() && !d::original_attack && !d::original_advance);
    lof::gate.deactivate();
    lof::base = 0;
    lof::orig_attack_check_fire = nullptr;
    auth = saved;
    config::active().modules = modules;
    runtime::global_accepting = accepting;
    Fixture::f = nullptr;
}
} // namespace goh::test::wave5_order
