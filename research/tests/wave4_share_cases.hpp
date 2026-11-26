#pragma once

#include "../../mods/squad_logistics_module.hpp"
#include "vehicle_fixture_26734796.hpp"

namespace goh::test::wave4_share {
namespace sh = squad_logistics;
namespace p = sh::policy;
namespace s = ai_support;
using runtime::Address;
using runtime::Byte;
using vehicle::Memory;

struct Boxer {
    vehicle::FakeHuman human{};
    Memory<0x580> root{};
    Memory<0xD0> manager{};
    Memory<0x80> box{};
    Memory<0x300> sensor{};
    Memory<0x80> sub{};
    Memory<0x100> item{};
    Memory<0x1210> definition{};
    Memory<0x60> tag{};
    std::array<Address, 2> rows{};
    Address observation{};
    Address ptr() const {
        return human.actor.ptr();
    }
    void setup(Address base, unsigned id) {
        human.setup(base, id, static_cast<std::uint16_t>(id), 3, 0);
        root.put(0, base + 0x1110CD0);
        root.put(0x30, human.brain.ptr());
        human.brain.put(0xE0, root.ptr());
        human.agg.put(0x28, manager.ptr());
        manager.put(0xA0, box.ptr());
        box.put(0x50, ptr());
        rows[0] = item.ptr();
        box.put(0x10, reinterpret_cast<Address>(rows.data()));
        box.put(0x18, reinterpret_cast<Address>(rows.data() + 1));
        sensor.put(0x260, ptr());
        human.actor.put(0xDD0, sensor.ptr());
        tag.ename(0, "bandage");
        definition.put(0x11E0, tag.ptr());
        definition.put(0x11E8, tag.ptr() + 0x60);
        item.put(0x30, box.ptr());
        item.put(0x40, definition.ptr());
        item.put<std::uint16_t>(0x29, 0x2233);
        item.put<int>(0x48, 2);
    }
};
struct ExchangeCalls {
    inline static std::vector<Address> trace;
    inline static Address from{}, to{}, found{};
    inline static int quantity{};
    template <class Fn> static Fn get(Address rva) {
        trace.push_back(rva);
        Address fn{};
        switch (rva) {
            case 0x7E1D50:
                fn = reinterpret_cast<Address>(
                    +[](void* dst, Address box, const std::uint16_t* cell, int amount) -> void* {
                        assert(box == from && *cell == 0x2233);
                        quantity = amount;
                        return dst;
                    });
                break;
            case 0x7E1CE0:
                fn = reinterpret_cast<Address>(+[](void* dst, Address box) -> void* {
                    assert(box == to);
                    return dst;
                });
                break;
            case 0x7E1F40: fn = reinterpret_cast<Address>(+[](void*) -> Address { return found; }); break;
            case 0x7DF910:
                fn = reinterpret_cast<Address>(+[](void* dst, const char* name) -> void* {
                    assert(std::string_view(name) == "Xchange");
                    return dst;
                });
                break;
            case 0x7DFEE0:
            case 0x7E35E0: fn = reinterpret_cast<Address>(+[](void* dst, void*) -> void* { return dst; }); break;
            case 0x7DFBF0:
                fn = reinterpret_cast<Address>(+[](void*, int* id) -> int* {
                    *id = 53;
                    return id;
                });
                break;
            case 0x7DFE00:
            case 0x7E1EC0: fn = reinterpret_cast<Address>(+[](void*) {}); break;
            default: assert(false);
        }
        return reinterpret_cast<Fn>(fn);
    }
};
inline void amounts_and_ranks() {
    assert(p::needs_ammo(15, 30, false) && !p::needs_ammo(16, 30, false));
    assert(p::needs_ammo(0, 4, true) && !p::needs_ammo(1, 4, true) && !p::needs_ammo(-1, 30, false));
    assert(p::needs_bandage(69.f, 100.f, 0) && !p::needs_bandage(70.f, 100.f, 0) && !p::needs_bandage(30.f, 100.f, 1));
    assert(!p::needs_bandage(0, 100, 0) && !p::needs_bandage(1, 0, 0));
    assert(p::ammo_amount(90, 90, 30, false, false) == 60 && p::ammo_amount(12, 12, 30, false, false) == 12);
    assert(p::ammo_amount(45, 59, 30, false, true) == 0 && p::ammo_amount(45, 60, 30, false, true) == 30);
    assert(p::ammo_amount(90, 130, 30, false, true) == 65 && p::ammo_amount(8, 80, 30, false, true) == 8);
    assert(p::ammo_amount(6, 6, 4, true, false) == 2 && p::ammo_amount(2, 2, 4, true, true) == 1 &&
           p::ammo_amount(1, 1, 4, true, true) == 0);
    const auto high = (std::numeric_limits<int>::max)();
    assert(p::ammo_amount(high, high, high, false, false) == high &&
           p::ammo_amount(high, high, high, false, true) == 0);
    assert(p::bandage_amount(1, 2, false) == 1 && !p::bandage_amount(1, 1, false));
    assert(!p::bandage_amount(2, 2, true) && p::bandage_amount(2, 3, true) == 1);
    assert(p::rank_share_donor({true, false, 30, 3}, {false, true, 1, 1}));
    assert(p::rank_share_donor({true, true, 30, 3}, {true, false, 1, 1}));
    assert(p::rank_share_donor({true, true, 1, 9}, {true, true, 2, 2}));
    assert(p::rank_share_donor({true, true, 2, 1}, {true, true, 2, 2}));
}
inline void inventory_and_authority(const s::World& w, Boxer& d, Boxer& r) {
    const sh::Need bandage{sh::Kind::bandage};
    assert(sh::offer(d.ptr(), bandage).amount == 1);
    d.human.set_medic(s::base);
    assert(sh::offer(d.ptr(), bandage).amount == 0);
    d.item.put<int>(0x48, 3);
    assert(sh::offer(d.ptr(), bandage).amount == 1);
    for (auto off : {0x10, 0x38, 0x50}) {
        d.item.put<Address>(off, 1);
        assert(sh::contents(d.ptr(), bandage).total == 0);
        d.item.put<Address>(off, 0);
    }
    d.item.put(0x30, r.box.ptr());
    assert(sh::contents(d.ptr(), bandage).total == 0);
    d.item.put(0x30, d.box.ptr());
    using Calls = ExchangeCalls;
    Calls::from = d.box.ptr();
    Calls::to = r.box.ptr();
    Calls::found = d.item.ptr();
    Calls::trace.clear();
    assert(s::exchange_amount<Calls>(d.ptr(), r.ptr(), d.item.ptr(), 2) == 53);
    assert(Calls::quantity == 2 &&
           Calls::trace == std::vector<Address>({0x7E1D50, 0x7E1CE0, 0x7E1F40, 0x7DF910, 0x7DFEE0, 0x7E35E0, 0x7DFBF0,
                                                 0x7DFE00, 0x7E1EC0, 0x7E1EC0}));
    Calls::trace.clear();
    assert(s::exchange_amount<Calls>(d.ptr(), r.ptr(), d.item.ptr(), 4) < 0 && Calls::trace.empty());
    Calls::found = 0;
    assert(s::exchange_amount<Calls>(d.ptr(), r.ptr(), d.item.ptr(), 1) < 0);
    assert(Calls::trace == std::vector<Address>({0x7E1D50, 0x7E1CE0, 0x7E1F40, 0x7E1EC0, 0x7E1EC0}));
    auto no = +[](Address) -> Byte { return 0; };
    auto okay = [&] { return sh::free_actor(w, d.ptr(), 0, no); };
    d.human.brain.put<unsigned>(0x398, 1 | (1u << 10) | (1u << 12)); // Hold Fire, Hold Position
    assert(okay() && s::hold_position(d.human.brain.ptr()));
    assert(!sh::free_actor(w, d.ptr(), 0, +[](Address) -> Byte { return 1; }));
    for (unsigned flags : {1u, 1u << 20, 1u << 28}) {
        d.human.brain.put(0x39C, flags);
        assert(!okay());
    }
    d.human.brain.put<unsigned>(0x39C, 0);
    d.human.chassis.put<int>(0x18, 1);
    assert(!okay());
    d.human.chassis.put<int>(0x18, 0);
    d.human.weaponry.put<Byte>(0x70, 2);
    assert(!okay());
    d.human.weaponry.put<Byte>(0x70, 0);
    d.human.actor.put<unsigned>(0x1C0, 1u << 28);
    assert(!okay());
    d.human.actor.put<unsigned>(0x1C0, 0);
    Memory<0x70> active{};
    d.human.brain.put(0xF0, active.ptr());
    for (Address vt : {Address{0x110C018}, runtime::table().vt.avoid_grenade}) {
        active.put(0, s::base + vt);
        assert(!okay());
    }
    d.human.brain.put<Address>(0xF0, 0);
    d.observation = d.sub.ptr();
    d.sensor.put(0x280, reinterpret_cast<Address>(&d.observation));
    d.sensor.put(0x288, reinterpret_cast<Address>(&d.observation + 1));
    d.sub.put<unsigned>(0x14, 2);
    d.sub.put<unsigned>(0x10, 3u << 28);
    d.sub.put(0x28, s::Vec3{18.f, 0, 0});
    assert(sh::visible_near(d.ptr(), 18.f) && !sh::visible_near(d.ptr(), 12.f));
    d.sub.put(0x28, s::Vec3{12.f, 0, 0});
    assert(sh::visible_near(d.ptr(), 12.f));
    d.sub.put<unsigned>(0x10, 1u << 28);
    assert(!sh::visible_near(d.ptr(), 18.f));
}
struct MockDelivery {
    bool close{}, home{}, authorized_return{}, valid{true}, sent{true}, movement{true};
    int outbound{}, returns{}, exchanges{};
    sh::NativeDelivery::Receipt observed{sh::NativeDelivery::Receipt::pending};
    bool safe_return() const {
        return authorized_return;
    }
    bool at_post() const {
        return home;
    }
    bool ready() const {
        return valid;
    }
    bool in_range() const {
        return close;
    }
    bool move_to(bool back) {
        if (back)
            ++returns;
        else
            ++outbound;
        return movement;
    }
    bool send() {
        ++exchanges;
        return sent;
    }
    sh::NativeDelivery::Receipt receipt() const {
        return observed;
    }
};
inline void observed_receipt(const s::World& w, Boxer& d, Boxer& r) {
    // The real adapter distinguishes a pending marked action, a changed queue, an active
    // operation that disappeared from the queue, and the net stack increase after destruction.
    sh::Job j{};
    j.need = {sh::Kind::bandage};
    j.before = 2;
    j.amount = 1;
    sh::NativeDelivery adapter{w, j, d.ptr(), r.ptr()};
    using Receipt = sh::NativeDelivery::Receipt;
    assert(adapter.receipt() == Receipt::failure);
    Memory<0x100> op{};
    op.put(0, s::base + 0x10FF790);
    op.put<int>(0xF0, 11);
    op.put(0x88, d.ptr());
    auto_medic::Transfer mark{};
    mark.generation = 2;
    mark.operation = op.ptr();
    j.transfer = {op.ptr(), 11, &mark, 2};
    const std::array<Address, 2> queue{op.ptr(), op.ptr()};
    d.manager.put(0xA8, reinterpret_cast<Address>(queue.data()));
    d.manager.put(0xB0, reinterpret_cast<Address>(queue.data() + 1));
    assert(adapter.receipt() == Receipt::pending);
    d.manager.put(0xB0, reinterpret_cast<Address>(queue.data() + 2));
    assert(adapter.receipt() == Receipt::failure);
    d.manager.put<Address>(0xA8, 0);
    d.manager.put<Address>(0xB0, 0);
    assert(adapter.receipt() == Receipt::failure);
    mark.operation = 0;
    assert(adapter.receipt() == Receipt::failure);
    r.item.put<int>(0x48, 3);
    assert(adapter.receipt() == Receipt::success);
    r.item.put<int>(0x48, 2);
    static Address expected_item{}, expected_gun{};
    expected_item = d.definition.ptr() + 0xEC8;
    expected_gun = r.definition.ptr() + 0x120;
    const auto ammo = s::loose_ammo(
        d.ptr(), r.definition.ptr(),
        +[](Address item, Address gun) -> Byte { return item == expected_item && gun == expected_gun; });
    assert(ammo.item == d.item.ptr() && ammo.count == 3 && ammo.total == 3);
}
inline void jobs_and_time(s::World w, Boxer& donor, Boxer& receiver) {
    sh::refresh(w);
    auto& j = sh::jobs[0];
    j.donor = sh::Person::from(donor.ptr());
    j.receiver = sh::Person::from(receiver.ptr());
    j.since = w.now;
    j.hold = false; // Switching a free-moving donor to Hold Position cancels an outbound errand.
    assert(!sh::pair(w, j, donor.ptr(), receiver.ptr()));
    j.hold = true;
    coord::instance().register_provider(coord::Module::squad_logistics, coord::Priority::squad_logistics, sh::occupied,
                                        sh::preempt);
    for (Address a : {donor.ptr(), receiver.ptr()}) {
        assert(sh::occupied(a));
        assert(!coord::instance().can_act(a, coord::Module::auto_resupply, coord::Priority::auto_resupply));
        assert(coord::instance().can_act(a, coord::Module::auto_medic, coord::Priority::auto_medic));
    }
    assert(sh::due(w, receiver.ptr()) && !sh::due(w, receiver.ptr()));
    w.now += w.duration(.75f);
    assert(sh::due(w, receiver.ptr()));
    MockDelivery d{};
    sh::progress(w, j, d);
    assert(j.phase == sh::Phase::walking && d.outbound == 1 && d.exchanges == 0);
    s::Watched watch{};
    watch.order = 0x2000;
    watch.generation = 1;
    j.move = {&watch, 1};
    d.close = true;
    sh::progress(w, j, d);
    assert(j.phase == sh::Phase::walking && j.move.get() == 0x2000 && d.exchanges == 0);
    watch.order = 0;
    sh::progress(w, j, d);
    assert(j.phase == sh::Phase::exchange && d.exchanges == 1);
    sh::progress(w, j, d);
    assert(j.phase == sh::Phase::exchange && d.exchanges == 1);
    d.authorized_return = true;
    d.observed = sh::NativeDelivery::Receipt::success;
    const auto before = sh::stat_ammo.load();
    sh::progress(w, j, d);
    assert(j.phase == sh::Phase::returning && sh::stat_ammo == before + 1);
    assert(!sh::occupied(receiver.ptr()) && sh::occupied(donor.ptr()));
    sh::progress(w, j, d);
    assert(j.issued_return && d.returns == 1);
    d.home = true;
    sh::progress(w, j, d);
    assert(j.phase == sh::Phase::cooldown && !sh::occupied(donor.ptr()));
    w.now += w.duration(10) - 1;
    sh::refresh(w);
    assert(j.receiver.key.ptr);
    ++w.now;
    sh::refresh(w);
    assert(!j.receiver.key.ptr);
    j.receiver = sh::Person::from(receiver.ptr());
    j.donor = sh::Person::from(donor.ptr());
    j.since = w.now;
    j.need.kind = sh::Kind::bandage;
    d = {};
    d.close = true;
    sh::progress(w, j, d);
    assert(d.exchanges == 1 && d.outbound == 0);
    d.observed = sh::NativeDelivery::Receipt::failure;
    const auto bandages = sh::stat_bandages.load();
    sh::progress(w, j, d);
    assert(!j.delivered && j.phase == sh::Phase::cooldown && sh::stat_bandages == bandages);
    w.now += w.duration(5);
    sh::refresh(w);
    assert(!j.receiver.key.ptr);
    j.receiver = sh::Person::from(receiver.ptr());
    j.donor = sh::Person::from(donor.ptr());
    j.since = w.now;
    j.need.kind = sh::Kind::bandage;
    d = {};
    d.close = true;
    d.observed = sh::NativeDelivery::Receipt::success;
    sh::progress(w, j, d);
    sh::progress(w, j, d);
    assert(j.delivered && sh::stat_bandages == bandages + 1);
    j.phase = sh::Phase::walking;
    j.delivered = false;
    d = {};
    d.valid = false;
    d.authorized_return = true;
    sh::progress(w, j, d);
    assert(j.phase == sh::Phase::returning && !j.delivered);
    sh::preempt(donor.ptr());
    assert(!sh::occupied(donor.ptr()));
    ++w.now;
    sh::refresh(w);
    assert(j.phase == sh::Phase::cooldown);
    --w.now;
    sh::refresh(w);
    assert(!j.receiver.key.ptr);
    j.receiver = sh::Person::from(receiver.ptr());
    j.donor = sh::Person::from(donor.ptr());
    j.since = w.now;
    Memory<0x90> next_scene{};
    w.scene = next_scene.ptr();
    sh::refresh(w);
    assert(!j.receiver.key.ptr);
    sh::begin_shutdown();
    assert(sh::stopping && sh::quiescent());
    ++sh::admissions;
    assert(!sh::quiescent());
    --sh::admissions;
    ++s::watched_count;
    assert(!sh::quiescent());
    --s::watched_count;
    ++auto_medic::transfer_count;
    assert(!sh::quiescent());
    --auto_medic::transfer_count;
    sh::after_hooks_removed();
    assert(!sh::stopping);
}
inline void run() {
    amounts_and_ranks();
    vehicle::FakeImage image{};
    const auto prior = s::base;
    s::base = image.base();
    Boxer donor{}, receiver{};
    donor.setup(s::base, 1);
    receiver.setup(s::base, 2);
    Memory<0x90> scene{};
    const std::array<Address, 2> actors{donor.ptr(), receiver.ptr()};
    scene.put(0x48, reinterpret_cast<Address>(actors.data()));
    scene.put(0x50, reinterpret_cast<Address>(actors.data() + actors.size()));
    s::World w{s::base, scene.ptr(), 300, 1, 20};
    inventory_and_authority(w, donor, receiver);
    observed_receipt(w, donor, receiver);
    const auto before_ammo = sh::stat_ammo.load(), before_bandages = sh::stat_bandages.load();
    auto previous_coord = coord::instance();
    jobs_and_time(w, donor, receiver);
    coord::instance() = previous_coord;
    sh::stat_ammo = before_ammo;
    sh::stat_bandages = before_bandages;
    auto saved = runtime::table().auth;
    runtime::table().auth.is_remote_client = reinterpret_cast<Address>(+[]() -> Byte { return 1; }) - s::base;
    sh::tick(donor.ptr());
    assert(sh::scene == 0);
    runtime::table().auth.is_remote_client = reinterpret_cast<Address>(+[]() -> Byte { return 0; }) - s::base;
    runtime::table().auth.is_quant_worker = reinterpret_cast<Address>(+[]() -> Byte { return 1; }) - s::base;
    sh::tick(donor.ptr());
    assert(sh::scene == 0);
    runtime::table().auth = saved;
    s::base = prior;
}
} // namespace goh::test::wave4_share
