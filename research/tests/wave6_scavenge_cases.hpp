#pragma once
#include "../../mods/scavenge_module.hpp"
#include "wave4_share_cases.hpp"

namespace goh::test::wave6_scavenge {
namespace sc = scavenge;
namespace d = sc::detail;
namespace s = ai_support;
namespace med = auto_medic;
using runtime::Address;
using runtime::Byte;
using vehicle::Memory;
using wave4_share::Boxer;

struct Actions {
    Address hand{};
    bool bag_free{true}, inv_free{true}, stow_receipt{}, current{true}, nearby{}, moving{}, home{}, got{},
        target_ok{true};
    d::Receipt result{d::Receipt::pending};
    unsigned puts{}, collects{}, walks{}, returns{}, sends{};
    bool idle() const {
        return inv_free;
    }
    Address held() const {
        return hand;
    }
    bool stow(Address item) {
        ++puts;
        return bag_free && item == hand;
    }
    bool stowed() const {
        return stow_receipt;
    }
    bool valid() const {
        return target_ok;
    }
    bool collect() {
        ++collects;
        return hand == 0;
    }
    bool collecting() const {
        return current;
    }
    bool picked_up() const {
        return got;
    }
    bool in_reach() const {
        return nearby;
    }
    bool walking() const {
        return moving;
    }
    bool at_home() const {
        return home;
    }
    bool move(bool returning) {
        if (returning)
            ++returns;
        else
            ++walks;
        return true;
    }
    bool send() {
        ++sends;
        return true;
    }
    d::Receipt receipt() const {
        return result;
    }
};
inline void transitions(const s::World& w) {
    for (auto need : {sc::Need::rearm, sc::Need::heavy_weapon}) {
        d::Job j{};
        j.target.need = need;
        j.phase = d::Phase::start;
        Actions a{};
        a.hand = 1234;
        assert(!d::advance(j, w, a) && a.puts == 1 && !a.collects && j.phase == d::Phase::stowing);
        a.inv_free = false;
        assert(!d::advance(j, w, a) && a.puts == 1 && !a.collects);
        a.inv_free = true;
        a.hand = 0;
        a.stow_receipt = true;
        assert(!d::advance(j, w, a) && j.phase == d::Phase::collect && a.collects == 1);
        assert(!d::advance(j, w, a) && !j.success && a.collects == 1);
        a.got = true;
        a.target_ok = false;
        assert(!d::advance(j, w, a) && !j.success);
        a.current = false;
        assert(d::advance(j, w, a) && j.success);
        j.phase = d::Phase::returning;
        assert(!d::advance(j, w, a) && a.returns == 1);
        a.moving = true;
        assert(!d::advance(j, w, a) && a.returns == 1);
        a.moving = false;
        a.home = true;
        assert(d::advance(j, w, a));
    }
    for (bool reject : {true, false}) {
        d::Job j{};
        j.phase = d::Phase::start;
        Actions a{};
        a.hand = 46;
        a.bag_free = !reject;
        if (!reject) assert(!d::advance(j, w, a));
        assert(d::advance(j, w, a) && a.puts == 1 && a.hand == 46 && !a.collects && !j.success);
    }
    for (auto need : {sc::Need::corpse_ammo, sc::Need::corpse_bandage}) {
        d::Job j{};
        j.phase = d::Phase::start;
        j.target.need = need;
        Actions a{};
        assert(!d::advance(j, w, a) && j.phase == d::Phase::walking && a.walks == 1);
        a.moving = true;
        assert(!d::advance(j, w, a) && a.walks == 1);
        a.nearby = true;
        a.moving = false;
        assert(!d::advance(j, w, a) && a.sends == 1);
        assert(!d::advance(j, w, a) && !j.success);
        a.result = d::Receipt::success;
        assert(d::advance(j, w, a) && j.success);
    }
}

struct StowCalls {
    inline static std::vector<Address> trace;
    inline static Address actor{};
    template <class Fn> static Fn get(Address rva) {
        trace.push_back(rva);
        Address fn{};
        switch (rva) {
            case 0x7e1a40:
                fn = reinterpret_cast<Address>(+[](void* loc, Address owner) -> void* {
                    runtime::write<Address>(reinterpret_cast<Address>(loc), 8, owner);
                    return loc;
                });
                break;
            case 0x7df910:
                fn = reinterpret_cast<Address>(+[](void* b, const char* n) -> void* {
                    assert(std::string_view(n) == "PutFromRight");
                    return b;
                });
                break;
            case 0x7dfee0:
                fn = reinterpret_cast<Address>(+[](void* b, void* loc) -> void* {
                    assert(runtime::read<Address>(reinterpret_cast<Address>(loc), 8) == actor);
                    return b;
                });
                break;
            case 0x7dfbf0:
                fn = reinterpret_cast<Address>(+[](void* b, int* id) -> int* {
                    assert(runtime::read<Address>(reinterpret_cast<Address>(b), 0x80) == actor);
                    *id = 17;
                    return id;
                });
                break;
            default: fn = reinterpret_cast<Address>(+[](void*) {});
        }
        return reinterpret_cast<Fn>(fn);
    }
};
struct TransferCalls {
    inline static Address receiver{}, op{};
    inline static Address* queued{};
    template <class Fn> static Fn get(Address rva) {
        if (rva != 0x7dfbf0) return wave4_share::ExchangeCalls::get<Fn>(rva);
        auto submit = +[](void* b, int* id) -> int* {
            assert(runtime::read<Address>(reinterpret_cast<Address>(b), 0x80) == receiver);
            *queued = op;
            *id = 29;
            return id;
        };
        return reinterpret_cast<Fn>(reinterpret_cast<Address>(submit));
    }
};
inline void exchange(Boxer& soldier, Boxer& body) {
    using E = wave4_share::ExchangeCalls;
    E::from = body.box.ptr();
    E::to = soldier.box.ptr();
    E::found = body.item.ptr();
    Memory<0x170> operation{};
    Address row{};
    TransferCalls::op = operation.ptr();
    TransferCalls::receiver = soldier.ptr();
    TransferCalls::queued = &row;
    operation.put(0, s::base + 0x10ff790);
    operation.put<int>(0xf0, 29);
    operation.put(0x88, soldier.ptr());
    soldier.manager.put(0xa8, reinterpret_cast<Address>(&row));
    soldier.manager.put(0xb0, reinterpret_cast<Address>(&row + 1));
    body.item.put<std::uint16_t>(0x29, 0x2233);
    const auto release = med::release_installed;
    const auto destroy = med::original_transfer_destroy;
    med::release_installed = true;
    med::original_transfer_destroy = +[](Address, unsigned) -> Address { return 0; };
    for (auto n : {sc::Need::corpse_bandage, sc::Need::corpse_ammo}) {
        d::Job j{};
        j.target.need = n;
        j.target.gun = soldier.definition.ptr();
        j.before = 1;
        j.amount = 2;
        row = 0;
        soldier.manager.put(0xb0, reinterpret_cast<Address>(&row + 1));
        assert(med::begin_exchange<TransferCalls>(j.transfer, body.ptr(), soldier.ptr(), body.item.ptr(), 2,
                                                  soldier.ptr()));
        assert(E::quantity == 2 && med::queued(j.transfer, soldier.ptr()) == operation.ptr() &&
               !med::queued(j.transfer, body.ptr()));
        assert(d::exchange_receipt(j, soldier.ptr(), body.ptr()) == d::Receipt::pending);
        soldier.manager.put(0xb0, reinterpret_cast<Address>(&row));
        assert(d::exchange_receipt(j, soldier.ptr(), body.ptr()) == d::Receipt::failure);
        med::transfer_destroy(operation.ptr(), 1);
        soldier.item.put<int>(0x48, 1);
        assert(d::exchange_receipt(j, soldier.ptr(), body.ptr()) == d::Receipt::failure);
        soldier.item.put<int>(0x48, 3);
        assert(d::exchange_receipt(j, soldier.ptr(), body.ptr()) == d::Receipt::success);
        assert(d::exchange_receipt(j, soldier.ptr(), 0) == d::Receipt::success);
    }
    med::release_installed = release;
    med::original_transfer_destroy = destroy;
}
struct CollectFixture {
    inline static Address actor{}, brain{}, source{}, order{}, replacement{};
    inline static bool replace{}, removed{};
    inline static unsigned issued{}, discarded{};
    static Address make(Address target, Byte mode) {
        assert(target == source && mode == 0);
        if (replace) runtime::write(brain, 0xf0, replacement);
        if (removed) runtime::write<Byte>(source, 0x30, 0);
        return order;
    }
    static void set(Address b, Address fresh) {
        assert(b == brain);
        runtime::write(brain, 0xf0, fresh);
        ++issued;
    }
    static Address destroy(Address o, unsigned flags) {
        assert(o == order && flags == 1);
        ++discarded;
        return 0;
    }
};
inline void collect_adapter() {
    using F = CollectFixture;
    const auto old = s::base;
    vehicle::FakeImage image;
    s::base = image.base();
    Boxer person;
    person.setup(s::base, 12);
    Memory<0x60> order{}, other{};
    Memory<0xb00> target{};
    Memory<0x38> agg{};
    Memory<0xa0> ext{};
    Memory<0x100> item{};
    Memory<0x80> env{};
    s::World w{s::base, env.ptr(), 90, 1, 10};
    F::actor = person.ptr();
    F::brain = person.human.brain.ptr();
    F::source = target.ptr();
    F::order = order.ptr();
    F::replacement = other.ptr();
    F::issued = F::discarded = 0;
    image.put(runtime::table().vt.human_brain + 0x68, reinterpret_cast<Address>(F::set));
    image.put(runtime::table().vt.collect_order, reinterpret_cast<Address>(F::destroy));
    order.put(0, s::base + runtime::table().vt.collect_order);
    order.put(8, F::actor + 0xaf0);
    order.put(0x38, F::source);
    order.put<int>(0x48, -1);
    target.put<Byte>(0x30, 1);
    target.put(0x578, agg.ptr());
    agg.put(0x20, ext.ptr());
    ext.put(0, s::base + runtime::table().vt.stuff_ext);
    ext.put(0x18, F::source);
    ext.put(0x88, item.ptr());
    item.put(0x40, person.definition.ptr());
    item.put<int>(0xdc, 9);
    item.put<Address>(0x50, 1);
    std::array<Address, 2> array{F::actor, F::source};
    env.put(0x48, reinterpret_cast<Address>(array.data()));
    env.put(0x50, reinterpret_cast<Address>(array.data() + 2));
    d::Job j{};
    j.person = squad_logistics::Person::from(F::actor);
    j.hold = true;
    j.phase = d::Phase::collect;
    j.target = {sc::Need::rearm, s::identity(F::source), item.ptr(), person.definition.ptr(), 0, 0, 0};
    d::NativeWork adapter{j, w, F::actor, F::source};
    auto empty = +[](Address) -> Address { return 0; };
    F::replace = true;
    assert(!adapter.collect(F::make, empty) && F::discarded == 1 && F::issued == 0);
    assert(person.human.brain.get<Address>(0xf0) == F::replacement);
    person.human.brain.put<Address>(0xf0, 0);
    F::replace = false;
    F::removed = true;
    assert(!adapter.collect(F::make, empty) && F::discarded == 2 && !F::issued);
    F::removed = false;
    target.put<Byte>(0x30, 1);
    assert(adapter.collect(F::make, empty) && F::issued == 1 && d::is_collect(j, F::actor));
    // Native reservation and target unlinking during PickupToRight are expected.
    order.put<int>(0x48, 32);
    order.put<int>(0x50, 1);
    ext.put<Address>(0x98, 1);
    order.put<Address>(0x38, 0);
    assert(d::pickup_started(j, F::actor) && adapter.collecting() && adapter.valid());
    // A new player order supersedes the expired pickup. No clearing or Hold return.
    person.human.brain.put(0xf0, F::replacement);
    d::finish(j, w, true);
    assert(j.phase == d::Phase::cooldown && F::issued == 1 && person.human.brain.get<Address>(0xf0) == F::replacement);
    s::base = old;
}
inline void run() {
    using sc::policy::rearm;
    assert(rearm(false, -1, false) && rearm(true, 0, false) && !rearm(true, 0, true) && !rearm(true, 2, false) &&
           !rearm(true, -1, false));
    assert(sc::policy::upgrade(true, false, false) && !sc::policy::upgrade(false, false, false) &&
           !sc::policy::upgrade(true, true, false) && !sc::policy::upgrade(true, false, true));
    assert(sc::policy::amount(sc::Need::corpse_bandage, 3, 0) == 2 &&
           sc::policy::amount(sc::Need::corpse_ammo, 400, 30) == 60 &&
           sc::policy::amount(sc::Need::corpse_ammo, 2, 30) == 2);
    const auto prev = s::base;
    auto compat = +[](Address, Address) -> Byte { return 1; };
    s::base = reinterpret_cast<Address>(compat) - 0x565a50;
    Boxer soldier{}, body{};
    soldier.setup(s::base, 15);
    body.setup(s::base, 26);
    Memory<0x88> env{};
    s::World w{s::base, env.ptr(), 200, 1, 10};
    const std::array<Address, 2> actors{soldier.ptr(), body.ptr()};
    env.put(0x48, reinterpret_cast<Address>(actors.data()));
    env.put(0x50, reinterpret_cast<Address>(actors.data() + actors.size()));
    transitions(w);
    collect_adapter();
    StowCalls::actor = soldier.ptr();
    StowCalls::trace.clear();
    assert(d::put_from_right<StowCalls>(soldier.ptr()) == 17);
    assert((StowCalls::trace == std::vector<Address>{0x7e1a40, 0x7df910, 0x7dfee0, 0x7dfbf0, 0x7dfe00, 0x7e1ec0}));
    Memory<0x60> fsm{};
    soldier.definition.put(0x118, fsm.ptr());
    fsm.ename(0, "rifle");
    assert(d::can_stow(soldier.ptr(), soldier.item.ptr()));
    fsm.ename(0, "hand-only");
    assert(!d::can_stow(soldier.ptr(), soldier.item.ptr()));
    fsm.ename(0, "rifle");
    soldier.item.put<Address>(0x30, 0);
    assert(!d::can_stow(soldier.ptr(), soldier.item.ptr()));
    soldier.item.put(0x30, soldier.box.ptr());
    assert(!d::corpse(body.ptr()));
    for (unsigned flags : {2u, 6u}) {
        body.human.vit.put(0x174, flags);
        assert(!d::corpse(body.ptr()));
    }
    body.human.vit.put<unsigned>(0x174, 1);
    assert(d::corpse(body.ptr()));
    assert(!d::specialist(soldier.ptr()));
    soldier.human.set_medic(s::base);
    assert(d::specialist(soldier.ptr()));
    soldier.human.set_mechanic(s::base, 1);
    assert(d::specialist(soldier.ptr()));
    d::Demand demand{};
    demand.bandage = true;
    auto target = d::choose(w, soldier.ptr(), demand);
    assert(target.source.ptr == body.ptr() && target.need == sc::Need::corpse_bandage && !target.source.mid);
    Memory<0xb00> ground{};
    Memory<0x38> group{};
    Memory<0xa0> ext{};
    Memory<0x100> firearm{};
    Memory<0x1210> def{};
    Memory<0xc0> names{};
    ground.put<Byte>(0x30, 1);
    ground.put<unsigned>(0xc0, 78);
    ground.put(0x578, group.ptr());
    group.put(0x20, ext.ptr());
    ext.put(0, s::base + runtime::table().vt.stuff_ext);
    ext.put(0x18, ground.ptr());
    ext.put(0x88, firearm.ptr());
    firearm.put(0x40, def.ptr());
    firearm.put<int>(0x48, 1);
    firearm.put<int>(0xdc, 2);
    firearm.put<Address>(0x50, 1);
    names.ename(0, "weapon");
    def.put(0x11e0, names.ptr());
    def.put(0x11e8, names.ptr() + 0xc0);
    const std::array<Address, 3> all{actors[0], actors[1], ground.ptr()};
    env.put(0x48, reinterpret_cast<Address>(all.data()));
    env.put(0x50, reinterpret_cast<Address>(all.data() + all.size()));
    demand.upgrade = true;
    for (const char* tag : {"mgun", "rocketlauncher", "at_rifle"}) {
        names.ename(0x60, tag);
        target = d::choose(w, soldier.ptr(), demand);
        assert(target.need == sc::Need::heavy_weapon && target.item == firearm.ptr());
    }
    names.ename(0x60, "rifle");
    demand.rearm = true;
    assert(d::choose(w, soldier.ptr(), demand).need == sc::Need::rearm);
    ground.put<Byte>(0xa38, 5);
    assert(d::choose(w, soldier.ptr(), demand).need == sc::Need::corpse_bandage);
    ground.put<Byte>(0xa38, 0);
    ext.put<Address>(0x98, 1);
    assert(!d::ground_item(ground.ptr()));
    ext.put<Address>(0x98, 0);
    ground.put<unsigned>(0x1c0, 1u << 28);
    assert(!d::ground_item(ground.ptr()));
    ground.put<unsigned>(0x1c0, 0);
    ground.put(0x74, s::Vec3{10, 0, 0});
    body.human.actor.put(0x74, s::Vec3{10, 0, 0});
    soldier.observation = soldier.sub.ptr();
    soldier.sensor.put(0x280, reinterpret_cast<Address>(&soldier.observation));
    soldier.sensor.put(0x288, reinterpret_cast<Address>(&soldier.observation + 1));
    soldier.sub.put<unsigned>(0x10, 3u << 28);
    soldier.sub.put<unsigned>(0x14, 2);
    soldier.sub.put(0x28, s::Vec3{28, 0, 0});
    assert(!d::choose(w, soldier.ptr(), demand).source.ptr);
    soldier.sub.put(0x28, s::Vec3{28.1f, 0, 0});
    assert(d::choose(w, soldier.ptr(), demand).source.ptr == ground.ptr());
    soldier.sub.put<unsigned>(0x10, 1u << 29);
    assert(!d::choose(w, soldier.ptr(), demand).source.ptr);
    exchange(soldier, body);
    assert(sc::quiescent());
    sc::after_hooks_removed();
    s::base = prev;
}
} // namespace goh::test::wave6_scavenge
