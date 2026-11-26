#pragma once

#include "../../mods/safe_disembark_module.hpp"
#include "vehicle_fixture_26734796.hpp"

namespace goh::test::wave4_vehicle {
namespace sd = safe_disembark;
namespace d = sd::detail;
using namespace vehicle;
using runtime::Byte;
using runtime::Tick;
using sd::Vec3;

struct Fixture {
    FakeImage image{};
    FakeTank tank{}, hull{};
    FakeHuman human{}, driver{}, ally{};
    Memory<0x300> sensor{};
    Memory<0x80> subject{}, scene{}, scene2{};
    Memory<0xF8 * 4> doors{};
    std::array<Memory<0xE0>, 4> bones{};
    Memory<0x80> callback{}, emit{};
    std::array<Address, 1> subjects{};
    std::array<Address, 2> actors{};
    std::array<unsigned, 4> updates{};
    std::vector<unsigned> probes;
    unsigned stock_calls{}, blocks{}, other_node{}, discard_count{}, created{}, installs{}, callbacks{}, velocities{};
    Address discarded{}, current{};
    bool remote{}, worker{}, fail_create{}, fail_cb{}, interrupt{}, nested{};
    sd::World w{};
    Vec3 vel{};
    Address a() const {
        return tank.actor.ptr();
    }
    Address h() const {
        return human.actor.ptr();
    }
};
inline Fixture* f{};

inline Address stock(Address, const short*, const Vec3*, unsigned) {
    ++f->stock_calls;
    return 0x5150;
}
inline Address slot(Address boarder, short i) {
    assert(boarder == f->tank.boarder.ptr() && i >= 0 && i < 2);
    return f->tank.slots.ptr() + i * 0x298;
}
inline Address place_node(Address boarder, const short* i) {
    assert(boarder == f->tank.boarder.ptr() && !*i);
    return f->tank.door_node.ptr();
}
inline Address root(Address boarder) {
    return f->nested ? f->hull.boarder.ptr() : boarder;
}
inline Address door_node(Address boarder, Address door) {
    assert(boarder == root(f->tank.boarder.ptr()));
    return door;
}
inline Byte connected(Address boarder, Address from, Address door) {
    assert(boarder == root(f->tank.boarder.ptr()) && from == f->tank.door_node.ptr());
    return !(f->other_node & (1u << ((door - f->doors.ptr()) / 0xF8)));
}
inline Tick clock() {
    return f->w.now;
}
inline Vec3* velocity(Address actor, Vec3* out) {
    assert(actor == f->a());
    ++f->velocities;
    *out = f->vel;
    return out;
}
inline Address callback(Address actor) {
    assert(actor == f->a());
    ++f->callbacks;
    return f->fail_cb ? 0 : f->callback.ptr();
}
inline Address factory(int count, int mode, Address cb) {
    assert(count == -1 && mode == 1 && cb == f->callback.ptr());
    ++f->created;
    if (f->interrupt) f->tank.chassis.put<unsigned>(0x18, 1);
    return f->fail_create ? 0 : f->emit.ptr();
}
inline void setter(Address b, Address emit) {
    assert(b == f->tank.brain.ptr() && emit == f->emit.ptr());
    ++f->installs;
    f->tank.brain.put<Address>(0xF0, emit);
    sd::on_actor_post_quant(f->a()); // Native callbacks may reenter the coordinator.
}
inline Address resolve(Address base, Address rva) {
    assert(base == f->image.base());
    switch (rva) {
        case 0x79D790: return reinterpret_cast<Address>(root);
        case 0x79E930: return reinterpret_cast<Address>(place_node);
        case 0x799150: return reinterpret_cast<Address>(slot);
        case 0x79E8A0: return reinterpret_cast<Address>(door_node);
        case 0x79EDE0: return reinterpret_cast<Address>(connected);
        case 0x66E7B0: return reinterpret_cast<Address>(clock);
        case 0x73A2A0: return reinterpret_cast<Address>(velocity);
        case 0x9AC4E0: return reinterpret_cast<Address>(callback);
        case 0x8FC550: return reinterpret_cast<Address>(factory);
        default: assert(false); return 0;
    }
}
inline void set_threat(Address observer, bool visible = true) {
    f->sensor.put(0x260, observer);
    f->sensor.put(0x280, reinterpret_cast<Address>(f->subjects.data()));
    f->sensor.put(0x288, reinterpret_cast<Address>(f->subjects.data() + 1));
    f->subjects[0] = f->subject.ptr();
    runtime::write(observer, 0xDD0, f->sensor.ptr());
    f->subject.put<unsigned>(0x10, visible ? (1u << 29) : (1u << 28));
    f->subject.put<unsigned>(0x14, 2);
    f->subject.put<Vec3>(0x28, {30, 0, 0});
    f->subject.put<Tick>(0x64, f->w.now);
    f->subject.put<Tick>(0x68, f->w.now);
}
inline void setup(Fixture& x) {
    f = &x;
    const auto image = x.image.base();
    x.tank.setup(image, 1, 1, 0);
    x.tank.set_wheeled("truck");
    x.hull.setup(image, 5, 5, 0);
    x.human.setup(image, 2, 2, 0, x.a());
    x.driver.setup(image, 3, 3, 0, x.a());
    x.ally.setup(image, 4, 4, 0, 0);
    x.tank.set_slot_count(2);
    x.tank.set_slot(0, "seat", "passenger", 0, "", "", x.h());
    x.tank.set_slot(1, "driver", "crew", 0x1000, "", "", x.driver.actor.ptr());
    x.tank.occ_ids[0] = 0;
    x.tank.occ_ids[1] = 1;
    x.tank.placer.put(0x10, reinterpret_cast<Address>(x.tank.occ_ids.data()));
    x.tank.placer.put(0x18, reinterpret_cast<Address>(x.tank.occ_ids.data() + 2));
    x.w = {image, x.scene.ptr(), 2000, 1.f, 100.f};
    x.current = x.a();
    x.tank.boarder.put(0xA8, x.doors.ptr());
    x.tank.boarder.put(0xB0, x.doors.ptr() + 4 * 0xF8);
    for (unsigned i = 0; i < 4; ++i) {
        x.doors.put(i * 0xF8 + 0x60, x.bones[i].ptr());
        x.bones[i].put<Byte>(0x10, 1);
    }
    x.bones[0].put<Vec3>(0xCC, {2, 0, 0});
    x.bones[1].put<Vec3>(0xCC, {-2, 0, 0});
    x.bones[2].put<Vec3>(0xCC, {0, 2, 0});
    x.bones[3].put<Vec3>(0xCC, {0, -2, 0});
    x.image.put<float>(0x1099C40, std::numeric_limits<float>::max());
    x.image.put(runtime::table().vt.vehicle_brain + 0x68, &setter);
    auto create = [&](void* target, void* hook, void** orig) {
        assert(target == reinterpret_cast<void*>(image + 0x79D3C0));
        assert(hook == reinterpret_cast<void*>(&d::select_door));
        *orig = reinterpret_cast<void*>(&stock);
        return 0;
    };
    auto remove = [](void*) {
        assert(false);
        return 0;
    };
    assert(!sd::prepare_hooks(image, "", create, remove));
    assert(sd::prepare_hooks(image, runtime::kBuild26734796Sha256, create, remove));
    runtime::global_accepting = true;
    sd::s::base = image;
    d::ops.resolve = &resolve;
    d::ops.current = [] { return f->current; };
    d::ops.host = [](Address) { return !f->remote && !f->worker; };
    d::ops.world = [](Address, sd::World& w) {
        w = f->w;
        return true;
    };
    d::ops.blocked = [](Address occupant, Address door, Address bone) {
        const unsigned i = static_cast<unsigned>((door - f->doors.ptr()) / 0xF8);
        assert(occupant == f->h() && i < 4 && bone == f->bones[i].ptr());
        f->probes.push_back(i);
        return (f->blocks & (1u << i)) != 0;
    };
    d::ops.update = [](Address bone) {
        for (unsigned i = 0; i < 4; ++i)
            if (f->bones[i].ptr() == bone) {
                ++f->updates[i];
                return;
            }
        assert(false);
    };
    d::ops.discard = [](Address o) {
        ++f->discard_count;
        f->discarded = o;
    };
    x.actors[0] = x.ally.actor.ptr();
    x.actors[1] = x.a();
    x.scene.put(0x48, reinterpret_cast<Address>(x.actors.data()));
    x.scene.put(0x50, reinterpret_cast<Address>(x.actors.data() + 2));
    x.scene2 = x.scene;
}
inline void native_probe_arguments(Fixture& x) {
    Memory<0xF0> ctx{};
    Memory<0x30> params{};
    ctx.put<Address>(0xD0, params.ptr());
    ctx.put<unsigned>(0xE8, 7);
    params.put<float>(0x20, 4.25f);
    x.bones[0].put<float>(0xA8, 3.5f);
    x.bones[0].put<float>(0xAC, -2.f);
    auto update = [](Address b) { assert(b == f->bones[0].ptr()); };
    auto probe = [](void* path, const Vec3* pos, const runtime::XY* normal, float range, int attempts) -> Byte {
        assert(runtime::read<unsigned>(reinterpret_cast<Address>(path), 0xE8) == 6);
        assert(reinterpret_cast<Address>(pos) == f->bones[0].ptr() + 0xCC);
        assert(normal->x == -3.5f && normal->y == 2.f && range == 4.25f && attempts == 3);
        return 1;
    };
    assert(d::probe_exit(ctx.data.data(), x.doors.ptr(), x.bones[0].ptr(), update, probe));
}
inline void door_cases(Fixture& x) {
    const short place = 0;
    const Vec3 pivot{0, 0, 0};
    const auto select = [&](unsigned flags = 0) { return d::select_door(x.tank.boarder.ptr(), &place, &pivot, flags); };
    constexpr Address vanilla = 0x5150;
    assert(select() == vanilla && x.probes.empty());
    set_threat(x.a());
    assert(select() == x.doors.ptr() + 0xF8 && x.stock_calls == 1);
    assert((x.probes == std::vector<unsigned>{0, 1, 2, 3}) && sd::stat_safe_doors == 1);
    x.probes.clear();
    x.blocks = 2;
    x.other_node = 4;
    x.doors.put<Address>(3 * 0xF8 + 0x68, 1);
    assert(select() == x.doors.ptr() && x.stock_calls == 1 && sd::stat_safe_doors == 1);
    assert((x.probes == std::vector<unsigned>{0, 1}));
    assert(x.updates[3] == 2 && x.updates[2] == 1);
    x.probes.clear();
    x.doors.put<Tick>(0x74, x.w.now + 1);
    assert(!select() && x.stock_calls == 1 && (x.probes == std::vector<unsigned>{1}));
    x.doors.put<Tick>(0x74, x.w.now);
    x.blocks = x.other_node = 0;
    x.doors.put<Address>(3 * 0xF8 + 0x68, 0);
    x.nested = true;
    x.hull.boarder.put(0xA8, x.doors.ptr());
    x.hull.boarder.put(0xB0, x.doors.ptr() + 4 * 0xF8);
    x.tank.actor.put<unsigned>(0x1C0, 1u << 28);
    x.current = x.hull.actor.ptr();
    set_threat(x.current);
    assert(select() == x.doors.ptr() + 0xF8);
    x.nested = false;
    x.tank.actor.put<unsigned>(0x1C0, 0);
    set_threat(x.a());
    x.current = x.h(); // Passenger can own the enclosing serial update.
    assert(select(4) == x.doors.ptr() + 0xF8);
    x.probes.clear();
    x.current = x.driver.actor.ptr();
    assert(select() == vanilla);
    x.current = x.a();
    x.remote = true;
    assert(select() == vanilla);
    x.remote = false;
    x.worker = true;
    assert(select() == vanilla);
    x.worker = false;
    x.tank.brain.put<unsigned>(0x39C, 1);
    assert(select() == vanilla);
    x.tank.brain.put<unsigned>(0x39C, 0);
    x.tank.weaponry.put<Byte>(0x70, 2);
    assert(select() == vanilla);
    x.tank.weaponry.put<Byte>(0x70, 0);
    assert(select(1) == vanilla);
    config::active().modules.safe_disembark = false;
    assert(select() == vanilla && x.probes.empty());
    config::active().modules.safe_disembark = true;
    set_threat(x.ally.actor.ptr(), false);
    x.tank.actor.put<Address>(0xDD0, 0);
    x.ally.actor.put<Vec3>(0x74, {45, 0, 0});
    x.subject.put<Tick>(0x64, x.w.now - x.w.duration(10));
    assert(select() == x.doors.ptr() + 0xF8);
    x.ally.actor.put<Vec3>(0x74, {46, 0, 0});
    assert(select() == vanilla);
    x.ally.actor.put<Vec3>(0x74, {45, 0, 0});
    x.subject.put<Tick>(0x64, x.w.now - x.w.duration(10) - 1);
    assert(select() == vanilla);
    x.subject.put<Tick>(0x64, x.w.now);
    x.subject.put<Vec3>(0x28, {121, 0, 0});
    assert(select() == vanilla);
    x.subject.put<Vec3>(0x28, {120, 0, 0});
    x.subject.put<unsigned>(0x14, 0);
    assert(select() == vanilla);
    x.subject.put<unsigned>(0x14, 2);
    for (auto& b : x.bones)
        b.put<Vec3>(0xCC, {-1, 0, 0});
    assert(select() == x.doors.ptr() + 3 * 0xF8); // Identical directions retain native tie rules.
    assert(select(4) == x.doors.ptr());
    x.tank.brain.put<Address>(0, x.image.base() + runtime::table().vt.human_brain);
    assert(select() == vanilla);
    x.tank.brain.put<Address>(0, x.image.base() + runtime::table().vt.vehicle_brain);
}
inline void unload_cases(Fixture& x) {
    auto tick = [&] { sd::on_actor_post_quant(x.a()); };
    auto next = [&] {
        x.w.now += x.w.duration(1);
        tick();
    };
    x.tank.brain.put<unsigned>(0x398, 1u | (1u << 10) | (1u << 12)); // Hold Fire and Hold Position.
    x.tank.set_body_state(1);
    x.remote = true;
    tick();
    x.remote = false;
    x.worker = true;
    tick();
    x.worker = false;
    x.current = x.h();
    tick();
    x.current = x.a();
    config::active().modules.transport_unload = false;
    tick();
    config::active().modules.transport_unload = true;
    x.tank.brain.put<unsigned>(0x39C, 1);
    tick();
    x.tank.brain.put<unsigned>(0x39C, 1u << 28);
    tick();
    x.tank.brain.put<unsigned>(0x39C, 0);
    x.tank.chassis.put<unsigned>(0x18, 1);
    tick();
    x.tank.chassis.put<unsigned>(0x18, 0);
    x.tank.actor.put<unsigned>(0x1C0, 1u << 28);
    tick();
    x.tank.actor.put<unsigned>(0x1C0, 0);
    x.tank.placer.put<Byte>(0x48, 1);
    next();
    x.tank.placer.put<Byte>(0x48, 0);
    x.human.weaponry.put<Byte>(0x70, 2);
    next();
    x.human.weaponry.put<Byte>(0x70, 0);
    x.tank.set_exit_walker(x.h());
    next();
    x.tank.clear_walkers();
    x.tank.set_slot_flags(1, 0);
    next();
    x.tank.set_slot_flags(1, 0x1000); // Native mode 1 would take driver too.
    x.tank.brain.put<Address>(0xF0, x.emit.ptr());
    next();
    x.tank.brain.put<Address>(0xF0, 0);
    x.tank.set_slot_reserved(0, x.driver.actor.ptr());
    next();
    x.tank.set_slot_reserved(0, 0);
    x.tank.def_tag.ename(0, "destroyed");
    next();
    x.tank.def_tag.ename(0, "truck");
    x.tank.track_name.ename(0, "trackleft");
    x.tank.actor.ename(0xDE8, "tank_medium");
    next();
    x.tank.set_wheeled("truck");
    assert(!x.callbacks && !x.installs);
    next();
    assert(x.created == 1 && x.installs == 1 && sd::stat_transport_unloads == 1);
    x.tank.brain.put<Address>(0xF0, 0);
    x.w.now += x.w.duration(15) - 1;
    tick();
    assert(x.created == 1);
    ++x.w.now;
    tick();
    assert(x.installs == 2 && sd::stat_transport_unloads == 2);
    x.tank.brain.put<Address>(0xF0, 0);
    x.tank.set_body_state(0);
    x.tank.actor.put<Byte>(0xDAD, 2);
    x.w.scene = x.scene2.ptr();
    tick();
    assert(x.installs == 3); // Burning alone, world change clears previous deadline.
    x.tank.brain.put<Address>(0xF0, 0);
    x.tank.actor.put<Byte>(0xDAD, 0);
    x.w.now = 500;
    x.tank.brain.put<Tick>(0x3F8, 0);
    tick();
    assert(x.installs == 3);
    x.tank.brain.put<Tick>(0x3F8, x.w.now + x.w.duration(4));
    x.vel = {1, 0, 0};
    next();
    assert(x.installs == 3);
    x.vel = {};
    next();
    assert(x.installs == 4); // Threat deadline and stationary, after clock rewind.
    x.tank.brain.put<Address>(0xF0, 0);
    x.w.now = 100;
    x.tank.brain.put<Tick>(0x3F8, 0);
    x.vel = {2, 0, 0};
    x.tank.comp_rows.put<Byte>(0x28 + 0x24, 1);
    set_threat(x.a());
    x.subject.put<unsigned>(0x14, 2u | 0x180u);
    x.subject.put<float>(0x58, 1.f);
    x.subject.put<Vec3>(0x28, {81, 0, 0});
    tick();
    assert(x.installs == 4);
    x.subject.put<Vec3>(0x28, {80, 0, 0});
    x.subject.put<unsigned>(0x14, 2);
    next();
    assert(x.installs == 4);
    x.subject.put<unsigned>(0x14, 2u | 0x180u);
    ++x.w.now;
    next();
    assert(x.installs == 4); // Historical hit no longer marks an active threat.
    x.subject.put<Tick>(0x68, x.w.now);
    next();
    assert(x.installs == 5); // Broken wheel allows a moving vehicle to issue the native order.
    x.tank.brain.put<Address>(0xF0, 0);
    x.w.now = 5;
    x.fail_cb = true;
    x.tank.set_body_state(1);
    tick();
    assert(x.discard_count == 0);
    x.fail_cb = false;
    x.fail_create = true;
    next();
    assert(x.discard_count == 1 && x.discarded == x.callback.ptr());
    x.fail_create = false;
    x.interrupt = true;
    next();
    assert(x.discard_count == 2 && x.discarded == x.emit.ptr() && x.installs == 5);
    x.interrupt = false;
    x.tank.chassis.put<unsigned>(0x18, 0);
    sd::deactivate();
    next();
    assert(x.installs == 5);
}
inline void run() {
    const auto saved_base = sd::s::base;
    const bool accepting = runtime::global_accepting;
    Fixture fixture;
    setup(fixture);
    native_probe_arguments(fixture);
    door_cases(fixture);
    unload_cases(fixture);
    sd::after_hooks_removed();
    sd::s::base = saved_base;
    runtime::global_accepting = accepting;
    f = nullptr;
}
} // namespace goh::test::wave4_vehicle
