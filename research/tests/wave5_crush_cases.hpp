#pragma once

#include "../../mods/crush_avoidance_module.hpp"
#include "vehicle_fixture_26734796.hpp"

namespace goh::test::wave5_crush {
namespace ca = crush_avoidance;
namespace d = ca::detail;
namespace m = movement_orders;
namespace s = ai_support;
using runtime::Address;
using runtime::Byte;
using runtime::XY;
using s::Vec3;
using vehicle::Memory;

struct Fixture {
    vehicle::FakeImage image;
    vehicle::FakeHuman human{};
    vehicle::FakeTank tank{};
    Memory<0x590> root{};
    Memory<0xA0> scene{};
    Memory<0x200> grenade{};
    Memory<0x80> attack{};
    std::array<Address, 3> entities{};
    std::array<Address, 1> stock_vehicles{};
    s::World w{};
    bool host{true}, worker{}, native_path{}, blocked{}, retired{}, exclude_vehicle{}, exclude_brain{}, fail_move{};
    unsigned updates{}, builds{}, executes{}, probes{};
    Byte saw_burn{}, saw_crush{};
    Vec3 velocity{.1f, 0, 0};
    XY goal{};
    std::vector<Address> pushed{}, deleted{};
    Address scanner() const {
        return root.ptr() + 0x3e0;
    }
};
inline Fixture* fixture{};
inline Byte peek(Address obj, unsigned off) {
    return runtime::read<Byte>(obj, off);
}

inline void run() {
    Fixture f;
    fixture = &f;
    const Address base = f.image.base();
    f.human.setup(base, 11, 1, 1, 0);
    // A tank driven in Direct Control remains a threat. The observer is autonomous.
    f.tank.setup(base, 12, 2, 1);
    f.tank.chassis.put<unsigned>(0x18, 1);
    f.tank.brain.put<unsigned>(0x39c, 1);
    f.tank.actor.put<XY>(0x50, {-1, 0});
    f.human.actor.put<Vec3>(0x74, {25, 23, 1});
    f.tank.actor.put<Vec3>(0x74, {20, 20, 1});
    f.grenade.put<Byte>(0x30, 1);
    f.grenade.put<unsigned>(0xc0, 13);
    f.grenade.put<Vec3>(0x74, {25, 25, 1});
    f.human.brain.put<Address>(0xe0, f.root.ptr());
    f.root.put<Address>(0, base + runtime::table().vt.root_soldier);
    f.root.put<Address>(0x30, f.human.brain.ptr());
    f.root.put<Address>(0x3e0, base + runtime::table().vt.crush_scanner_vehicle);
    f.root.put<Address>(0x3e8, f.human.brain.ptr());
    f.root.put<Address>(0x490, f.grenade.ptr());
    f.entities = {f.human.actor.ptr(), f.tank.actor.ptr(), f.grenade.ptr()};
    f.scene.put<Address>(0x48, reinterpret_cast<Address>(f.entities.data()));
    f.scene.put<Address>(0x50, reinterpret_cast<Address>(f.entities.data() + f.entities.size()));
    f.stock_vehicles[0] = f.tank.actor.ptr();
    f.w = {base, f.scene.ptr(), 30, 1, 30};
    auto prior_native = m::native;
    const auto prior_global = runtime::global_accepting.exchange(true);
    const auto prior_config = config::active().modules.crush_avoidance;
    config::active().modules.crush_avoidance = true;
    d::image = base;
    d::gate.activate();
    d::ops.callback = [](Address a, s::World& w) {
        w = fixture->w;
        return a == fixture->human.actor.ptr() && fixture->host && !fixture->worker;
    };
    d::ops.hull = [](Address) { return 1.8f; };
    d::ops.excluded_vehicle = [](Address) -> Byte { return fixture->exclude_vehicle; };
    d::ops.excluded_brain = [](Address) -> Byte { return fixture->exclude_brain; };
    d::ops.motion = [](Address) { return fixture->velocity; };
    d::ops.escape = [](const s::World& w, Address a, const d::Threats& threats) {
        return d::choose_escape(threats, m::xy(s::pos(a)), w.length, {}, [](XY) {
            ++fixture->probes;
            return !fixture->blocked;
        });
    };
    m::native.sequence = []() -> Address { return 10; };
    m::native.point = [](const XY& xy) -> Address {
        fixture->goal = xy;
        return fixture->fail_move ? 0 : 30;
    };
    m::native.push = [](Address q, Address child, int layer) {
        assert(q == 10 && layer == 0);
        fixture->pushed.push_back(child);
    };
    m::native.discard = [](Address child) {
        if (child) fixture->deleted.push_back(child);
    };
    d::ops.movement_change = []() -> Address { return 20; };
    d::ops.brain_update = []() -> Address { return 40; };
    d::original.update = [](Address scanner) {
        assert(scanner == fixture->scanner());
        ++fixture->updates;
        runtime::write<Byte>(scanner, 0x10, fixture->native_path);
        const auto first = reinterpret_cast<Address>(fixture->stock_vehicles.data());
        runtime::write<Address>(scanner, 0x18, first);
        runtime::write<Address>(scanner, 0x20, first + (fixture->native_path ? 8 : 0));
    };
    d::original.build = [](Address) -> Address {
        ++fixture->builds;
        return 55;
    };
    d::original.execute = [](Address root) {
        ++fixture->executes;
        fixture->saw_burn = peek(root, 0x448);
        fixture->saw_crush = peek(root, 0x3f0);
        if (fixture->retired) fixture->human.brain.put<Address>(0xe0, 0);
    };
    auto update = [&] {
        d::update(f.scanner());
        return peek(f.scanner(), 0x10);
    };
    auto build = [&] { return d::build(f.scanner()); };
    auto execute = [&](Byte burn, Byte crush) {
        auto old_burn = peek(f.root.ptr(), 0x448), old_crush = peek(f.root.ptr(), 0x3f0);
        unsigned before = f.executes;
        d::execute(f.root.ptr());
        assert(f.executes == before + 1 && f.saw_burn == burn && f.saw_crush == crush);
        if (!f.retired) assert(peek(f.root.ptr(), 0x448) == old_burn && peek(f.root.ptr(), 0x3f0) == old_crush);
    };

    assert(update() == 1 && f.updates == 1);
    assert(f.root.get<Address>(0x3f8) == f.root.get<Address>(0x400));
    assert(build() == 10 && f.builds == 0 && ca::stat_crush_escapes == 1);
    assert((f.pushed == std::vector<Address>{20, 30, 40}) && f.goal.y > 23 && f.goal.x == 25);
    const XY first_goal = f.goal;
    assert(build() == 10 && f.goal.x == first_goal.x && f.goal.y == first_goal.y && f.builds == 0);
    f.blocked = true;
    assert(!build() && f.builds == 0);
    f.blocked = false;
    f.fail_move = true;
    assert(!build() && f.builds == 0 && (f.deleted == std::vector<Address>{20, 40, 10}));
    f.fail_move = false;
    // Losing authority, identity or freshness between Update and Build never enters stock's empty-vector RNG.
    f.human.chassis.put<unsigned>(0x18, 1);
    assert(!build() && !f.builds);
    f.human.chassis.put<unsigned>(0x18, 0);
    f.w.now++;
    assert(build() == 10);
    f.w.now++;
    assert(!build() && !f.builds);
    assert(update());
    f.tank.actor.put<unsigned>(0xc0, 14);
    assert(!build());
    f.tank.actor.put<unsigned>(0xc0, 12);
    f.human.actor.put<unsigned>(0xc0, 77);
    assert(!build());
    f.human.actor.put<unsigned>(0xc0, 11);
    f.velocity = {0, 0, 0};
    assert(!update());
    f.velocity = {-.1f, 0, 0};
    assert(!update());
    f.tank.actor.put<Vec3>(0x74, {29, 22, 1});
    assert(update() && build() == 10);
    f.tank.actor.put<Vec3>(0x74, {40, 22, 1});
    assert(!update());
    f.tank.actor.put<Vec3>(0x74, {29, 22, 4.01f});
    assert(!update());
    f.tank.actor.put<Vec3>(0x74, {29, 22, 1});
    f.exclude_vehicle = true;
    assert(!update());
    f.exclude_vehicle = false;
    f.worker = true;
    assert(!update());
    f.worker = false;
    f.human.weaponry.put<unsigned>(0x70, 2);
    assert(!update());
    f.human.weaponry.put<unsigned>(0x70, 0);
    f.human.brain.put<unsigned>(0x39c, 1);
    assert(!update());
    f.human.brain.put<unsigned>(0x39c, 0);
    f.exclude_brain = true;
    assert(!update());
    f.exclude_brain = false;
    f.attack.put<Address>(0, base + runtime::table().vt.attack_order);
    f.human.brain.put<Address>(0xf0, f.attack.ptr());
    assert(!update());
    f.human.brain.put<Address>(0xf0, 0);
    f.tank.actor.put<Vec3>(0x74, {29, 19.6f, 1});
    assert(!update()); // Allied 3.15 m corridor vs hostile 3.8 m.
    f.tank.actor.put<Byte>(0xda8, 2);
    assert(update());
    f.tank.actor.put<Byte>(0xda8, 1);
    f.tank.actor.put<Vec3>(0x74, {29, 22, 1});
    f.native_path = true;
    assert(update() && build() == 55 && f.builds == 1);
    f.root.put<Byte>(0x448, 5);
    f.root.put<Byte>(0x478, 1);
    execute(0, 0);
    assert(ca::stat_hazard_priorities == 1);
    f.grenade.put<Vec3>(0x74, {25, 30, 1}); // 7 m, nearby fire is still suppressed.
    execute(0, 1);
    f.grenade.put<Vec3>(0x74, {25, 25, 1});
    f.tank.actor.put<Vec3>(0x74, {27, 22, 1});
    assert(update());
    execute(0, 1); // Literal native under-tread crush.
    f.native_path = false;
    assert(update());
    execute(0, 1); // Same priority with only the custom Direct Control lane.
    f.root.put<Byte>(0x428, 1);
    execute(5, 1);
    f.root.put<Byte>(0x428, 0);
    f.human.actor.put<Byte>(0xdad, 2);
    execute(5, 1);
    f.human.actor.put<Byte>(0xdad, 0);
    f.grenade.put<unsigned>(0x38, 0x20);
    execute(5, 1);
    f.grenade.put<unsigned>(0x38, 0);
    f.worker = true;
    execute(5, 1);
    f.worker = false;
    f.human.brain.put<unsigned>(0x39c, 1);
    execute(5, 1);
    f.human.brain.put<unsigned>(0x39c, 0);
    f.root.put<Address>(0, base + 0x1000);
    execute(5, 1);
    f.root.put<Address>(0, base + runtime::table().vt.root_soldier);
    f.retired = true;
    execute(0, 1);
    assert(peek(f.root.ptr(), 0x448) == 0);

    f.human.brain.put<Address>(0xe0, f.root.ptr());
    f.retired = false;
    // A worker, disabled module or unowned empty scanner always calls stock.
    f.worker = true;
    assert(build() == 55 && f.builds == 2);
    f.worker = false;
    config::active().modules.crush_avoidance = false;
    assert(build() == 55 && f.builds == 3);
    config::active().modules.crush_avoidance = true;
    --f.w.now; // Scene clock rollback discards the old ownership and cached vehicles.
    assert(build() == 55 && f.builds == 4);
    assert(update() && build() == 10);
    Memory<0xA0> replacement_scene = f.scene;
    f.w.scene = replacement_scene.ptr();
    assert(build() == 55 && f.builds == 5);
    assert(update() && build() == 10);
    f.w.scene = f.scene.ptr();

    // An enemy breaks an exact left/right tie. Every selected escape clears every known lane.
    d::Threats lanes{};
    lanes.lanes[0] = {{}, {25, 23}, {1, 0}, 5, 3};
    lanes.count = 1;
    s::Danger enemy{true, true, {25, 30, 1}, true};
    auto goal = d::choose_escape(lanes, {25, 23}, 1, enemy, [](XY) { return true; });
    assert(goal && goal->y < 23);
    auto blocked = d::choose_escape(lanes, {25, 23}, 1, enemy, [](XY) { return false; });
    assert(!blocked);
    lanes.lanes[1] = {{}, {25, 18}, {0, 1}, 10, 3};
    lanes.count = 2;
    auto crossing = d::choose_escape(lanes, {25, 23}, 1, {}, [](XY) { return true; });
    assert(crossing && lanes.lanes[0].clearance(*crossing) > 0 && lanes.lanes[1].clearance(*crossing) > 0);
    lanes.overflow = true;
    assert(!d::choose_escape(lanes, {25, 23}, 1, {}, [](XY) { return true; }));
    m::native = prior_native;
    config::active().modules.crush_avoidance = prior_config;
    runtime::global_accepting = prior_global;
    ca::after_hooks_removed();
    fixture = nullptr;
}
} // namespace goh::test::wave5_crush
