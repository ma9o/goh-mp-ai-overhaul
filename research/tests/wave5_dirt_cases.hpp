#pragma once
#include "../../mods/engine_fixes_module.hpp"
#include "../../mods/grenade_dodge_module.hpp"
#include "vehicle_fixture_26734796.hpp"

namespace goh::test::wave5_dirt {
namespace d = hit_the_dirt::detail;
namespace gd = grenade_dodge::detail;
namespace ef = engine_fixes;
namespace s = ai_support;
using runtime::Address;
using runtime::Byte;
using runtime::Tick;
using vehicle::Memory;

struct Fixture {
    vehicle::FakeImage image;
    vehicle::FakeHuman human;
    Memory<0x580> root;
    Memory<0x300> sensor;
    Memory<0x90> subject;
    Memory<0x180> chassis_slots;
    Memory<0x40> cover, charger;
    Memory<0xc0> weapon;
    Memory<0x70> order;
    Memory<0x200> grenade;
    std::array<Address, 2> entities{};
    Address observation{}, current{};
    Tick now = 100;
    bool client{}, worker{}, armed{}, stock_plan_ok{};
    int signals{}, postures{}, hides{}, ticks{}, plans{};
    std::uint64_t stock_result = 2;
    int expected_native_state = 0;
    d::SignalDesc desc{5, 400.f};
    s::Vec3 point{180.f, 0, 0};
    s::World world() const {
        return {image.base(), root.ptr(), now, 20.f, 20.f};
    }
    void time(Tick tick) {
        now = tick;
        image.put(0x1584568, now);
    }
    void quant() {
        hit_the_dirt::on_human_post_quant(world(), human.actor.ptr());
    }
    void publish(Address source = 0) {
        const auto before = signals;
        d::publish(&desc, source, &point, 199);
        assert(signals == before + 1);
    }
};
inline Fixture* f{};

template <class Fn> Address address(Fn fn) {
    return reinterpret_cast<Address>(fn);
}

inline void blasts() {
    auto& h = f->human;
    f->desc.type = 2;
    f->publish();
    f->desc.type = 5;
    f->desc.radius = 299;
    f->publish();
    f->desc.radius = 400;
    f->point.z = std::numeric_limits<float>::quiet_NaN();
    f->publish();
    f->point.z = 0;
    h.actor.put<Byte>(0x3a, 1);
    f->publish(h.actor.ptr());
    h.actor.put<Byte>(0x3a, 0);
    f->client = true;
    f->publish();
    f->client = false;
    f->worker = true;
    f->publish();
    f->worker = false;
    s::main_tid = 0;
    f->publish();
    s::main_tid = GetCurrentThreadId();
    assert(d::count == 0 && !f->postures);
    f->publish(h.actor.ptr());
    assert(d::count == 1 && d::blasts[d::head].source_player == 1 && !f->postures);

    h.brain.put<unsigned>(0x398, 1);
    f->quant();
    h.brain.put<unsigned>(0x398, 0x1001);
    for (auto blocked : {1u, 1u << 6, 1u << 9, 1u << 28}) {
        h.brain.put(0x39c, blocked);
        f->quant();
    }
    h.brain.put<unsigned>(0x39c, 0);
    h.chassis.put<int>(0x18, 1);
    f->quant();
    h.chassis.put<int>(0x18, 0);
    h.chassis.put<int>(0x2c, 3);
    f->quant();
    h.chassis.put<int>(0x2c, 0);
    h.chassis.put<int>(0x30, 1);
    f->quant();
    h.chassis.put<int>(0x30, 0);
    f->charger.put<Byte>(0x14, 4);
    f->quant();
    f->charger.put<Byte>(0x14, 0);
    h.brain.put(0xf0, f->order.ptr());
    f->quant();
    h.brain.put<Address>(0xf0, 0);
    f->current = 0;
    f->quant();
    f->current = h.actor.ptr();
    coord::instance().claim(f->current, 10, coord::Module::auto_resupply, coord::Priority::auto_resupply, f->now);
    f->quant();
    coord::instance().release(f->current, coord::Module::auto_resupply);
    f->observation = f->subject.ptr();
    f->sensor.put(0x280, address(&f->observation));
    f->sensor.put(0x288, address(&f->observation + 1));
    f->subject.put<unsigned>(0x10, 3u << 28);
    f->subject.put<unsigned>(0x14, 3u);
    f->subject.put(0x28, s::Vec3{199.f, 0, 0});
    f->quant();
    assert(!f->postures);
    f->subject.put(0x28, s::Vec3{201.f, 0, 0});
    f->quant();
    assert(f->postures == 1 && hit_the_dirt::stat_hit_dirt == 1);
    assert(hit_the_dirt::is_hitting_dirt(f->current, f->now));

    // Exercise Wave 3's actual stance adapter. At 7.5 m it must leave the stock
    // decision alone. At 7.45 m a standing request is allowed through immediately.
    ef::image = f->image.base();
    ef::gate.activate();
    ef::ops.serial_actor = d::serial_actor;
    ef::ops.resolve = +[](Address, Address rva) {
        assert(rva == 0x91b200 || rva == 0x7e05b0);
        return address(+[](Address) -> Byte { return 0; });
    };
    const auto probe = ef::detail::queries.probe_stance;
    ef::detail::queries.probe_stance = +[](const ef::ti::Context&, int pose) {
        return pose == 1 ? ef::policy::Sight::blocked : ef::policy::Sight::clear;
    };
    h.chassis.put<int>(0x2c, 1);
    h.brain.put(0x3b8, f->subject.ptr());
    f->subject.put(0x28, s::Vec3{150, 0, 0});
    assert(ef::detail::stance_choice(h.brain.ptr(), 0) == -1);
    f->subject.put(0x28, s::Vec3{149, 0, 0});
    assert(ef::detail::stance_choice(h.brain.ptr(), 0) == 0);
    f->time(190);
    assert(!hit_the_dirt::is_hitting_dirt(f->current, f->now));
    f->subject.put(0x28, s::Vec3{230, 0, 0});
    assert(ef::detail::stance_choice(h.brain.ptr(), 0) == 0);
    ef::detail::queries.probe_stance = probe;
    ef::ops = {};
    ef::gate.deactivate();
    ef::image = 0;
    h.chassis.put<int>(0x2c, 0);

    f->time(299);
    f->publish();
    f->quant();
    assert(f->postures == 1);
    f->time(300);
    f->quant();
    assert(f->postures == 2);
    // Recycled entities have separate cooldowns. A clock rewind drops all old blasts.
    h.actor.put<unsigned>(0xc0, 11);
    f->quant();
    assert(f->postures == 3);
    f->time(99);
    f->quant();
    assert(!d::count && !hit_the_dirt::is_hitting_dirt(f->current, f->now));
    for (int i = 0; i < 65; ++i) {
        f->point.x = 500.f + i;
        f->publish();
    }
    assert(d::count == 64 && d::blasts[d::head].pos.x == 501.f);
    // Cap blast reaction reach even for very large radius.
    f->desc.radius = 10000;
    f->publish();
    f->quant();
    assert(f->postures == 3);
    f->point = {100, 0, 0};
    f->publish();
    f->time(120); // Still retained but older than the reaction window.
    f->quant();
    assert(d::count == 64 && f->postures == 3);
    f->time(140);
    f->quant();
    assert(!d::count);
    f->publish();
    auto new_scene = f->world();
    new_scene.scene = f->sensor.ptr();
    hit_the_dirt::on_human_post_quant(new_scene, f->current);
    assert(!d::count && f->postures == 3);
}

inline void grenades() {
    auto& h = f->human;
    auto& gd_state = gd::state();
    gd::base = f->image.base();
    gd::gate.activate();
    gd::orig_avoid_inner_tick = +[](Address order) -> std::uint64_t {
        ++f->ticks;
        assert(order == f->order.ptr() && runtime::read<int>(order, 0x4c) == f->expected_native_state);
        return f->stock_result;
    };
    gd::orig_avoid_plan_escape = +[](Address order) -> Byte {
        assert(order == f->order.ptr());
        ++f->plans;
        return f->stock_plan_ok;
    };
    f->order.put(0, gd::base + runtime::table().vt.avoid_grenade);
    f->order.put(8, h.actor.ptr() + 0xaf0);
    f->order.put(0x38, f->grenade.ptr());
    f->order.put<float>(0x48, 4.f);
    h.brain.put(0xf0, f->order.ptr());
    f->grenade.put<Byte>(0x30, 1);
    f->grenade.put<unsigned>(0xc0, 9);
    f->grenade.put<unsigned>(0x1c0, 1u << 13);
    // Untracked stock projectile, bit 13 live despite the fuse query returning false.
    config::active().modules.grenade_dodge = false;
    f->stock_result = 0;
    assert(!gd::hook_avoid_inner_tick(f->order.ptr()) && f->ticks == 1 && !f->hides);
    f->stock_result = 2;
    h.chassis.put<int>(0x18, 1);
    assert(gd::hook_avoid_inner_tick(f->order.ptr()) == 2 && !f->hides);
    h.chassis.put<int>(0x18, 0);
    f->grenade.put<unsigned>(0x1c0, 0);
    assert(gd::hook_avoid_inner_tick(f->order.ptr()) == 2 && !f->hides);
    f->grenade.put<unsigned>(0x1c0, 1u << 13);
    const auto before = f->postures;
    assert(!gd::hook_avoid_inner_tick(f->order.ptr()) && f->hides == 1 && f->postures == before + 1);

    // Turn tracking back on. The formerly unconditional state reset must leave
    // our hidden soldier in native state 1 even in resident cover.
    config::active().modules.grenade_dodge = true;
    f->armed = true;
    gd_state.scene = runtime::read<Address>(gd::base, runtime::table().glob.scene_ptr);
    gd_state.last_tick = f->now;
    gd::TrackedGrenade track{};
    track.entity = f->grenade.ptr();
    track.last_tick = f->now;
    gd_state.grenades.push_back(track);
    f->cover.put<int>(0x24, 1);
    f->expected_native_state = 1;
    f->stock_result = 0;
    assert(!gd::hook_avoid_inner_tick(f->order.ptr()) && f->hides == 1 && !f->plans);

    // New hazard at the same address. Stock gets one last escape attempt after
    // the sprint retry budget. Hide only if that attempt also fails.
    f->grenade.put<unsigned>(0xc0, 10);
    gd_state.retries[f->order.ptr()] = {f->now - f->world().duration(.8f), 6, h.actor.ptr() + 0xaf0, f->grenade.ptr()};
    f->stock_plan_ok = true;
    assert(!gd::hook_avoid_inner_tick(f->order.ptr()) && f->hides == 1 && f->plans == 1);
    f->time(f->now + f->world().duration(.8f));
    f->stock_plan_ok = false;
    assert(!gd::hook_avoid_inner_tick(f->order.ptr()) && f->hides == 2 && f->plans == 2);
    assert(hit_the_dirt::stat_trapped_hides == 2);
    grenade_dodge::after_hooks_removed();
}
inline void run() {
    Fixture fixture;
    f = &fixture;
    auto& table = runtime::table();
    const auto saved_table = table;
    const auto saved_config = config::active();
    const bool accepting = runtime::global_accepting.exchange(true);
    const auto previous_tid = s::main_tid.exchange(GetCurrentThreadId());
    const auto previous_base = s::base;
    config::active() = {};
    const Address base = f->image.base();
    auto& h = f->human;
    h.setup(base, 10, 23, 1, 0);
    s::base = base;
    f->current = h.actor.ptr();
    f->time(f->now);
    f->image.put(0x15c9668, f->root.ptr());
    f->image.put(0x149e750, 20.f);
    f->image.put(0x149e758, 20.f);
    f->root.put(0, base + 0x1110cd0);
    f->root.put(0x30, h.brain.ptr());
    h.brain.put(0xe0, f->root.ptr());
    f->entities = {h.actor.ptr(), f->grenade.ptr()};
    f->root.put(0x48, address(f->entities.data()));
    f->root.put(0x50, address(f->entities.data() + f->entities.size()));
    h.actor.put(0xdd0, f->sensor.ptr());
    h.actor.put(0xdc0, f->cover.ptr());
    f->sensor.put(0x260, h.actor.ptr());
    h.chassis.put(0, f->chassis_slots.ptr());
    f->chassis_slots.put(0x170, address(+[](Address ch, int type, Byte force) {
                             assert(ch == f->human.chassis.ptr() && type == 1 && !force);
                             ++f->postures;
                         }));
    f->weapon.put(0xb0, f->charger.ptr());
    table.auth.is_remote_client = address(+[]() -> Byte { return f->client; }) - base;
    table.auth.is_quant_worker = address(+[]() -> Byte { return f->worker; }) - base;
    table.auth.clock_read = address(+[]() -> Tick { return f->now; }) - base;
    table.fn.weapon_select = address(+[](Address wp) {
                                 assert(wp == f->human.weaponry.ptr());
                                 return f->weapon.ptr();
                             }) -
                             base;
    table.fn.armed_grenade_query = address(+[](Address grenade) -> Byte {
                                       assert(grenade == f->grenade.ptr());
                                       return f->armed;
                                   }) -
                                   base;
    table.fn.avoid_grenade_hide = address(+[](Address order) {
                                      assert(order == f->order.ptr() && f->postures == 4 + f->hides);
                                      ++f->hides;
                                      f->order.put<Byte>(0x50, 1);
                                      f->order.put<int>(0x4c, 1);
                                  }) -
                                  base;

    const auto create = [base](void* target, void* detour, void** trampoline) {
        assert(address(target) == base + runtime::table().hook.world_publish_signal &&
               address(detour) == address(&d::publish));
        *trampoline =
            reinterpret_cast<void*>(+[](const d::SignalDesc* desc, Address, const s::Vec3* pos, Address payload) {
                assert(desc == &f->desc && pos == &f->point && payload == 199);
                ++f->signals;
            });
        return 0;
    };
    auto remove = [](void*) { assert(false); };
    assert(!hit_the_dirt::prepare_hooks(base, "wrong image", create, remove));
    assert(hit_the_dirt::prepare_hooks(base, runtime::kBuild26734796Sha256, create, remove));
    d::serial_actor = +[] { return f->current; };
    blasts();
    grenades();
    hit_the_dirt::deactivate();
    assert(hit_the_dirt::inflight_count() == 0);
    f->publish();
    hit_the_dirt::after_hooks_removed();
    assert(!d::original && !d::count && d::traps.empty() && d::reactions.empty());
    config::active() = saved_config;
    table = saved_table;
    runtime::global_accepting = accepting;
    s::main_tid = previous_tid;
    s::base = previous_base;
    f = nullptr;
}
} // namespace goh::test::wave5_dirt
