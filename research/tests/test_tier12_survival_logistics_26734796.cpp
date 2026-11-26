// Offline fixtures for the Stream A contracts on Steam build 26734796.
#include "../../mods/grenade_dodge_module.hpp"
#include "wave4_share_cases.hpp"

#include <cstdio>
#include <limits>

namespace {
using namespace goh;
namespace gd = grenade_dodge;
namespace g = gd::detail;
namespace s = ai_support;
namespace med = auto_medic;
namespace sh = squad_logistics;
using runtime::Address;
using runtime::Byte;
using test::vehicle::FakeImage;
using test::vehicle::Memory;
using test::wave4_share::Boxer;

template <class Fn> Address rva(Fn fn, Address base) {
    return reinterpret_cast<Address>(fn) - base;
}
Byte off() {
    return 0;
}
Byte on() {
    return 1;
}
Byte armed(Address) {
    return 1;
}
runtime::Tick sim_time = 100;
runtime::Tick now() {
    return sim_time;
}

void grenade_escape(FakeImage& image) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    assert(gd::player_escape_already_safe(1, 0, 6, 0, 0, 0, 0, 0, 6));
    assert(gd::player_escape_already_safe(1, 0, 1, 6, 0, 0, 0, 0, 6));
    assert(!gd::player_escape_already_safe(1, 0, -8, 0, 0, 0, 0, 0, 6));
    assert(!gd::player_escape_already_safe(8, 0, -8, 0, 0, 0, 0, 0, 6));
    assert(!gd::player_escape_already_safe(1, 0, 5.99f, 0, 0, 0, 0, 0, 6));
    assert(!gd::player_escape_already_safe(1, 0, 7, 0, 7, 1, 0, 0, 6));
    assert(!gd::player_escape_already_safe(7, 0, 7.5f, 0, 0, 0, 0, 0, 6));
    assert(!gd::player_escape_already_safe(1, 0, 7, 0, nan, 0, 0, 0, 6));

    auto& table = runtime::table();
    const auto saved = table;
    g::base = image.base();
    image.put(table.glob.spatial_scale_l, 10.f);
    table.auth.is_remote_client = rva(off, g::base);
    table.auth.is_quant_worker = rva(off, g::base);
    table.auth.clock_read = rva(now, g::base);
    table.fn.armed_grenade_query = rva(armed, g::base);
    Boxer unit{};
    unit.setup(g::base, 7);
    auto& brain = unit.human.brain;
    Memory<0x80> scanner{}, move{}, seq{}, task{}, avoid{}, grenade{}, second{};
    Memory<0x80> task_data{};
    std::array<runtime::Vec3, 2> path{{{-20, 0, 0}, {100, 0, 0}}};
    Address child = move.ptr(), block = reinterpret_cast<Address>(&child);
    seq.put(0, g::base + 0x110D490);
    seq.put(8, unit.ptr() + 0xAF0);
    seq.put(0x30, reinterpret_cast<Address>(&block));
    seq.put<std::size_t>(0x38, 1);
    seq.put<std::size_t>(0x48, 1);
    move.put(0, g::base + table.vt.move_order);
    move.put(8, unit.ptr() + 0xAF0);
    move.put(0x40, reinterpret_cast<Address>(path.data()));
    move.put(0x48, reinterpret_cast<Address>(path.data() + 2));
    task.put(0, g::base + table.vt.move_task_order);
    task.put(8, unit.ptr() + 0xAF0);
    task.put(0x40, task_data.ptr());
    brain.put(0x39C, 1u << 28);
    brain.put(0xF0, seq.ptr());
    unit.human.actor.put(0x74, s::Vec3{10, 0, 0});
    scanner.put(8, brain.ptr());
    scanner.put(0x18, 4.f);
    avoid.put(0, g::base + table.vt.avoid_grenade);
    avoid.put(8, unit.ptr() + 0xAF0);
    avoid.put(0x38, grenade.ptr());
    avoid.put(0x48, 4.f);
    avoid.put(0x4c, 1);
    g::TrackedGrenade hazard{};
    hazard.entity = grenade.ptr();
    hazard.last_tick = now();
    g::state().grenades.push_back(hazard);
    g::orig_scanner_upd = +[](Address a) { runtime::write<Byte>(a, 0x10, 0); };
    g::orig_avoid_inner_tick = +[](Address) -> std::uint64_t { return 17; };
    g::orig_avoid_plan_escape = +[](Address) -> Byte { return 19; };
    runtime::global_accepting = true;
    g::gate.activate();
    auto scanning = [&] {
        g::hook_scanner_upd(scanner.ptr());
        return scanner.get<Byte>(0x10);
    };
    gd::on_actor_pre_quant(unit.ptr());
    assert(!(brain.get<unsigned>(0x39c) & (1u << 25)));
    assert(!scanning() && gd::total_player_escapes_preserved() == 1);
    assert(g::hook_avoid_inner_tick(avoid.ptr()) == 1 && avoid.get<int>(0x4c) == 1);
    assert(g::hook_avoid_plan_escape(avoid.ptr()) == 0 && g::state().retries.empty());

    // The original scanner can already have selected this grenade, or a separate stock hazard.
    g::orig_scanner_upd = +[](Address a) { runtime::write<Byte>(a, 0x10, 1); };
    scanner.put(0x28, grenade.ptr());
    assert(!scanning());
    scanner.put(0x28, second.ptr());
    assert(scanning() && scanner.get<Address>(0x28) == second.ptr());
    g::orig_scanner_upd = +[](Address a) { runtime::write<Byte>(a, 0x10, 0); };
    hazard.entity = second.ptr();
    hazard.x = hazard.land_x = 30;
    g::state().grenades.push_back(hazard);
    assert(scanning() && scanner.get<Address>(0x28) == second.ptr());
    g::state().grenades.pop_back();
    path.back().x = -100;
    assert(scanning());
    assert(g::hook_avoid_inner_tick(avoid.ptr()) == 17 && avoid.get<int>(0x4c) == 0);
    path.back().x = 40;
    assert(scanning());
    path.back().x = 100;
    brain.put(0x39c, 0u); // Hold Position alone still allows an emergency dodge.
    assert(scanning());
    gd::on_actor_pre_quant(unit.ptr());
    assert(brain.get<unsigned>(0x39c) & (1u << 25));

    runtime::XY goal{};
    brain.put(0x39c, 1u << 28);
    brain.put(0x398, 0u);
    assert(!g::player_destination(brain.ptr(), goal));
    brain.put(0x398, 1u);
    move.put(0x48, move.get<Address>(0x40) + 13);
    assert(!g::player_destination(brain.ptr(), goal));
    move.put(0x48, move.get<Address>(0x40) + 4097 * 12);
    assert(!g::player_destination(brain.ptr(), goal));
    brain.put(0xF0, task.ptr());
    task_data.put(0x10, runtime::XY{100, 0});
    assert(g::player_destination(brain.ptr(), goal) && goal.x == 100);
    task_data.put(0x10, runtime::XY{nan, 0});
    assert(!g::player_destination(brain.ptr(), goal));
    task_data.put(0x10, runtime::XY{0, 0});
    brain.put(0x39c, (1u << 28) | (1u << 6));
    brain.put(0x130, runtime::XY{100, 20});
    assert(g::player_destination(brain.ptr(), goal) && goal.y == 20);
    // An AvoidGrenade child is an engine escape. Only an independent user goal can supersede it.
    brain.put(0xf0, avoid.ptr());
    avoid.put(0x30, task.ptr());
    task_data.put(0x10, runtime::XY{100, 0});
    assert(g::hook_avoid_plan_escape(avoid.ptr()) == 0);
    brain.put(0x39c, 1u << 28);
    assert(!g::player_destination(brain.ptr(), goal));
    // Emergency grenade dodge overrides an explicit Order::eAttack (never treated as a safe escape).
    Memory<0x80> atk{};
    atk.put(0, g::base + table.vt.attack_order);
    atk.put(8, unit.ptr() + 0xaf0);
    brain.put(0xf0, atk.ptr());
    assert(!g::player_destination(brain.ptr(), goal));
    assert(scanning());
    brain.put<Address>(0xf0, 0);
    for (auto gate : {&table.auth.is_remote_client, &table.auth.is_quant_worker}) {
        *gate = rva(on, g::base);
        assert(!scanning() && g::hook_avoid_inner_tick(avoid.ptr()) == 17 &&
               g::hook_avoid_plan_escape(avoid.ptr()) == 19);
        *gate = rva(off, g::base);
    }
    brain.put(0x39c, 1u);
    assert(!scanning());
    brain.put(0x39c, 0u);
    unit.human.chassis.put(0x18, 1u);
    assert(!scanning());
    unit.human.chassis.put(0x18, 0u);
    unit.human.weaponry.put<Byte>(0x70, 2);
    assert(!scanning());
    unit.human.weaponry.put<Byte>(0x70, 0);
    --sim_time;
    assert(!scanning() && g::state().grenades.empty());
    g::state().grenades.push_back(hazard);
    image.put(table.glob.scene_ptr, scanner.ptr());
    assert(!scanning() && g::state().grenades.empty());
    gd::after_hooks_removed();
    runtime::global_accepting = false;
    assert(!gd::total_player_escapes_preserved());
    table = saved;
    coord::instance().reset();
}

struct Scene {
    Memory<0x90> scene{};
    std::array<Address, 5> entities{};
    void set(std::initializer_list<Address> list) {
        std::copy(list.begin(), list.end(), entities.begin());
        scene.put(0x48, reinterpret_cast<Address>(entities.data()));
        scene.put(0x50, reinterpret_cast<Address>(entities.data() + list.size()));
    }
};
Byte downed(Address actor) {
    return (s::read<unsigned>(s::read<Address>(s::read<Address>(actor, 0x578), 0x18), 0x174) & 2) != 0;
}
Address canceled_manager{};
int canceled_id{};
void cancel_action(Address manager, const int* action) {
    canceled_manager = manager;
    canceled_id = *action;
}
void medic_failures() {
    s::base = rva(cancel_action, 0x7DDDC0);
    Boxer unit{}, casualty{}, alternate{}, donor{};
    unit.setup(s::base, 1);
    casualty.setup(s::base, 2);
    alternate.setup(s::base, 3);
    donor.setup(s::base, 4);
    casualty.human.vit.put(0x174, 2u);
    alternate.human.vit.put(0x174, 2u);
    alternate.human.actor.put(0x74, s::Vec3{1.f, 0.f, 0.f});
    Scene scene{};
    scene.set({unit.ptr(), casualty.ptr(), alternate.ptr(), donor.ptr()});
    s::World w{rva(downed, 0x379260), scene.scene.ptr(), 100, 1, 10};
    med::refresh(w);
    auto& j = med::jobs[0];
    auto claim = [&](med::Phase phase) {
        j = {};
        j.medic = s::identity(unit.ptr());
        j.patient = s::identity(casualty.ptr());
        j.scene = w.scene;
        j.brain = unit.human.brain.ptr();
        j.player = s::owner(unit.ptr());
        j.start = w.now;
        med::phase(j, w, phase, 35);
    };
    for (unsigned seconds : {12u, 25u, 45u, 45u}) {
        claim(med::Phase::revive); // Stock order ended, casualty is still down.
        med::update_medic(w, unit.ptr());
        assert(j.phase == med::Phase::scan && j.patient.matches(alternate.ptr()));
        assert(!med::patient_claimed(casualty.ptr()));
        assert(!med::patient_in_backoff(w, donor.ptr(), casualty.ptr()));
        w.now += w.duration(static_cast<float>(seconds)) - 1;
        assert(med::patient_in_backoff(w, unit.ptr(), casualty.ptr()));
        ++w.now;
        assert(!med::patient_in_backoff(w, unit.ptr(), casualty.ptr()));
    }
    assert(med::stat_patient_backoffs == 4);
    for (unsigned vitality : {0u, 1u}) {
        claim(med::Phase::revive);
        casualty.human.vit.put(0x174, vitality);
        med::update_medic(w, unit.ptr());
        assert(j.patient.matches(alternate.ptr()) && med::stat_patient_backoffs == 4);
    }
    casualty.human.vit.put(0x174, 2u);
    claim(med::Phase::approach);
    s::Watched watch{};
    watch.generation = 12;
    watch.order = 0x12340;
    unit.human.brain.put(0xf0, watch.order.load());
    j.order = {&watch, watch.generation};
    j.endpoint = {10, 0, 0};
    j.remaining = 10;
    j.progress_at = w.now - w.duration(9);
    med::update_medic(w, unit.ptr());
    assert(j.phase == med::Phase::cooldown && j.wait == w.duration(4) && !j.patient.ptr);
    unit.human.brain.put<Address>(0xf0, 0);
    claim(med::Phase::screen);
    w.now += j.wait;
    med::refresh(w);
    assert(med::stat_patient_backoffs == 6 && !med::patient_claimed(casualty.ptr()));

    scene.set({unit.ptr(), donor.ptr()});
    for (auto phase : {med::Phase::fetch, med::Phase::exchange, med::Phase::screen, med::Phase::approach}) {
        claim(phase);
        j.donor = s::identity(donor.ptr());
        med::Transfer mark{};
        Memory<0x180> op{};
        Address entry = op.ptr();
        if (phase == med::Phase::exchange) {
            donor.manager.put(0xa8, reinterpret_cast<Address>(&entry));
            donor.manager.put(0xb0, reinterpret_cast<Address>(&entry + 1));
            op.put(0, s::base + 0x10ff790);
            op.put(0x88, donor.ptr());
            op.put(0xf0, 41);
            mark.operation = op.ptr();
            mark.generation = 9;
            static_cast<med::ExchangeRef&>(j) = {op.ptr(), 41, &mark, 9};
        }
        if (phase == med::Phase::screen) {
            j.ticket = 42;
            s::smoke::reservations[0].ticket = 42;
        }
        med::update_medic(w, unit.ptr());
        assert(!j.patient.ptr && !j.donor.ptr && !med::patient_claimed(casualty.ptr()) && j.action < 0 &&
               !med::occupied(donor.ptr()) && !s::smoke::reservations[0].ticket && med::stat_patient_backoffs == 6);
        if (phase == med::Phase::exchange) {
            assert(canceled_id == 41 && canceled_manager == donor.manager.ptr());
            donor.manager.put<Address>(0xa8, 0);
            donor.manager.put<Address>(0xb0, 0);
        }
    }
    --w.now;
    med::refresh(w);
    assert(med::patient_backoffs.empty());
    scene.set({unit.ptr(), casualty.ptr()});
    assert(med::record_patient_failure(w, unit.ptr(), s::identity(casualty.ptr()), 12));
    casualty.human.actor.put(0xc0, 22u);
    assert(!med::patient_in_backoff(w, unit.ptr(), casualty.ptr()));
    Scene next{};
    w.scene = next.scene.ptr();
    med::refresh(w);
    assert(med::patient_backoffs.empty());
    med::after_hooks_removed();
    assert(med::stat_patient_backoffs == 0);
}

Address ammo_key{}, rifle_query{}, turret_query{};
bool turret_matches{};
Byte matches(Address ammo, Address query) {
    return ammo == ammo_key && (query == rifle_query || (turret_matches && query == turret_query));
}
s::Vec3* stationary(Address, s::Vec3* v) {
    *v = {};
    return v;
}
s::Vec3* moving(Address, s::Vec3* v) {
    *v = {1, 0, 0};
    return v;
}

void sources_and_selection(FakeImage& image) {
    s::base = rva(matches, 0x565A50);
    Boxer receiver{}, corpse{}, pack{};
    receiver.setup(s::base, 11);
    corpse.setup(s::base, 12);
    pack.setup(s::base, 13);
    Scene scene{};
    scene.set({receiver.ptr(), corpse.ptr(), pack.ptr()});
    s::World w{image.base(), scene.scene.ptr(), 200, 1, 10};
    sh::refresh(w);
    sh::Need need{sh::Kind::ammo, receiver.definition.ptr(), 6, false};
    ammo_key = corpse.definition.ptr() + 0xec8;
    rifle_query = need.gun + 0x120;
    pack.human.actor.put<Byte>(0x30, 1);
    pack.item.put(0x40, corpse.definition.ptr());
    pack.human.actor.put(0x74, s::Vec3{9, 0, 0});
    corpse.human.actor.put(0x74, s::Vec3{8, 0, 0});
    assert(!sh::scavenge_source(w, receiver.ptr(), corpse.ptr()));
    for (unsigned incapacitated : {2u, 6u}) {
        corpse.human.vit.put(0x174, incapacitated);
        assert(!sh::scavenge_source(w, receiver.ptr(), corpse.ptr()));
    }
    corpse.human.vit.put(0x174, 1u);
    assert(sh::scavenge_source(w, receiver.ptr(), corpse.ptr()));
    assert(sh::scavenge_source(w, receiver.ptr(), pack.ptr()));
    assert(sh::scavenge_offer(pack.ptr(), need).amount == 2);
    assert(!sh::scavenge_offer(pack.ptr(), {sh::Kind::bandage}).amount);
    pack.item.put(0x38, pack.ptr());
    assert(!sh::scavenge_offer(pack.ptr(), need).amount);
    pack.item.put<Address>(0x38, 0);
    auto select = [&] {
        sh::Job pick{};
        if (!sh::prefer_scavenge(w, receiver.ptr(), need, pick)) return Address{};
        assert(pick.scavenge && pick.hold && s::within(pick.home, s::pos(receiver.ptr()), 0));
        return pick.donor.key.ptr;
    };
    assert(select() == corpse.ptr());
    // The battlefield search owns nearby bodies while both features are active.
    sh::Job local_corpse{};
    assert(sh::prefer_scavenge(w, receiver.ptr(), need, local_corpse));
    assert(sh::battlefield_corpse(w, receiver.ptr(), local_corpse));
    goh::config::active().modules.battlefield_scavenge = false;
    assert(!sh::battlefield_corpse(w, receiver.ptr(), local_corpse));
    goh::config::active().modules.battlefield_scavenge = true;
    corpse.human.actor.put(0x74, s::Vec3{17, 0, 0});
    assert(!sh::battlefield_corpse(w, receiver.ptr(), local_corpse));
    corpse.human.actor.put(0x74, s::Vec3{8, 0, 0});
    static Address reserved = 0;
    reserved = corpse.ptr();
    auto& coordinator = goh::coord::instance();
    coordinator.register_provider(goh::coord::Module::scavenge, goh::coord::Priority::scavenge,
                                  [](Address source) { return source == reserved; });
    assert(select() == pack.ptr());
    coordinator.register_provider(goh::coord::Module::scavenge, goh::coord::Priority::scavenge, nullptr);

    auto& claim = sh::jobs[0];
    claim.scavenge = true;
    claim.donor.key = s::identity(corpse.ptr());
    claim.receiver = sh::Person::from(receiver.ptr());
    assert(sh::occupied(corpse.ptr()) && select() == pack.ptr());
    sh::fail_scavenge(w, claim);
    claim = {};
    assert(sh::source_blocked(w, receiver.ptr(), corpse.ptr()) && !sh::source_blocked(w, pack.ptr(), corpse.ptr()));
    w.now += w.duration(30) - 1;
    assert(select() == pack.ptr());
    ++w.now;
    assert(select() == corpse.ptr());
    corpse.human.actor.put(0x1c0, 1u << 28);
    assert(select() == pack.ptr());
    pack.manager.put(0xc8, pack.ptr());
    assert(!select());
    pack.manager.put<Address>(0xc8, 0);
    Memory<0xc0> ext{};
    Address ext_entry = ext.ptr();
    pack.human.agg.put(0, reinterpret_cast<Address>(&ext_entry));
    pack.human.agg.put(8, reinterpret_cast<Address>(&ext_entry + 1));
    ext.put(0x18, pack.ptr());
    ext.put(0, s::base + runtime::table().vt.supply_zone_ext);
    assert(!select());
    ext.put(0, s::base + runtime::table().vt.burning);
    ext.put(0xa0, 1.f);
    assert(!select());
    ext.put(0xa0, 0.f);
    pack.human.actor.put(0x74, s::Vec3{21, 0, 0});
    assert(!select());
    pack.human.actor.put(0x74, s::Vec3{20, 0, 0});
    assert(select() == pack.ptr());
    receiver.observation = receiver.sub.ptr();
    receiver.sensor.put(0x280, reinterpret_cast<Address>(&receiver.observation));
    receiver.sensor.put(0x288, reinterpret_cast<Address>(&receiver.observation + 1));
    receiver.sub.put(0x10, 3u << 28);
    receiver.sub.put(0x14, 2u);
    for (float enemy_x : {-18.f, 35.f}) {
        receiver.sub.put(0x28, s::Vec3{enemy_x, 0, 0});
        assert(!select());
    }
    receiver.sub.put(0x28, s::Vec3{35.1f, 0, 0});
    assert(select() == pack.ptr());
    receiver.human.brain.put(0x398, 1u | (1u << 10));
    assert(!select());
    receiver.human.brain.put(0x398, 1u | (1u << 12));
    for (unsigned flags : {1u, 1u << 28}) {
        receiver.human.brain.put(0x39c, flags);
        assert(!select());
    }
    receiver.human.brain.put(0x39c, 0u);
    config::active().modules.ammo_scavenge = false;
    assert(!select());
    config::active().modules.ammo_scavenge = true;
    config::active().modules.squad_logistics = false;
    assert(!select());
    config::active().modules.squad_logistics = true;

    // A vehicle may offer only ammo unused by every mounted gun, including a secondary gun.
    test::vehicle::FakeTank truck{};
    truck.setup(w.base, 17, 17, s::owner(receiver.ptr()));
    auto vehicle = truck.actor.ptr();
    truck.actor.put(0x578, pack.human.agg.ptr());
    pack.box.put(0x50, vehicle);
    assert(sh::scavenge_source(w, receiver.ptr(), vehicle, stationary));
    assert(!sh::scavenge_source(w, receiver.ptr(), vehicle, moving));
    truck.actor.put<Byte>(0xda8, 6);
    assert(!sh::scavenge_source(w, receiver.ptr(), vehicle, stationary));
    std::array<int, 18 * 18> diplomacy{};
    image.put(0x15c9700, reinterpret_cast<Address>(diplomacy.data()));
    image.put(0x15c9710, 18);
    assert(sh::scavenge_source(w, receiver.ptr(), vehicle, stationary)); // Confirmed neutral and unmanned.
    truck.set_slot(0, "driver", "crew", 0, "", "", receiver.ptr(), 0);
    assert(!sh::scavenge_source(w, receiver.ptr(), vehicle, stationary));
    truck.set_slot(0, "driver", "crew", 0, "", "", 0);
    diplomacy[6 * 18 + 3] = 1;
    assert(!sh::scavenge_source(w, receiver.ptr(), vehicle, stationary)); // Even an empty enemy truck is off limits.
    truck.actor.put<Byte>(0xda8, 3);
    corpse.human.actor.put<Byte>(0xda8, 6);
    truck.set_slot(0, "driver", "crew", 0, "", "", corpse.ptr(), 0);
    assert(!sh::scavenge_source(w, receiver.ptr(), vehicle, stationary));
    truck.set_slot(0, "driver", "crew", 0, "", "", 0);
    image.put<Address>(0x15c9700, 0);
    truck.actor.put<Byte>(0xdad, 2);
    assert(!sh::scavenge_source(w, receiver.ptr(), vehicle, stationary));
    truck.actor.put<Byte>(0xdad, 0);
    truck.brain.put(0x39c, 1u << 28);
    assert(!sh::scavenge_source(w, receiver.ptr(), vehicle, stationary));
    truck.brain.put(0x39c, 0u);
    Memory<0x230> mount{};
    Memory<0x60> mounted_item{}, weapon_tag{};
    Memory<0x1210> gun{};
    gun.put(0x11e0, weapon_tag.ptr());
    gun.put(0x11e8, weapon_tag.ptr() + 0x60);
    gun.put(0x438, weapon_tag.ptr());
    gun.put(0x440, weapon_tag.ptr() + 0x60);
    weapon_tag.ename(0, "weapon");
    mounted_item.put(0x40, gun.ptr());
    mount.put(0x90, vehicle);
    mount.put(0x218, mounted_item.ptr());
    Address gun_entry = mount.ptr();
    truck.weaponry.put(0x30, reinterpret_cast<Address>(&gun_entry));
    truck.weaponry.put(0x38, reinterpret_cast<Address>(&gun_entry + 1));
    turret_query = gun.ptr() + 0x120;
    turret_matches = true;
    assert(!sh::scavenge_offer(vehicle, need).amount);
    turret_matches = false;
    assert(sh::scavenge_offer(vehicle, need).amount == 2);
    mount.put<Address>(0x218, 0);
    assert(!sh::scavenge_offer(vehicle, need).amount);
    sh::after_hooks_removed();
}

struct InventoryCalls : test::wave4_share::ExchangeCalls {
    inline static Address owner{}, queue_entry{};
    inline static Memory<0x180> operation{};
    static void* bind_destination(void* dst, Address box) {
        assert(box == to);
        runtime::write<Address>(reinterpret_cast<Address>(dst), 8, s::read<Address>(box, 0x50));
        return dst;
    }
    static void* copy_location(void* dst, void* src) {
        return std::memcpy(dst, src, 0x40);
    }
    static int* submit(void* builder, int* action) {
        owner = s::read<Address>(reinterpret_cast<Address>(builder), 0x80);
        assert(owner && owner == s::read<Address>(to, 0x50));
        queue_entry = operation.ptr();
        operation.put(0, s::base + 0x10ff790);
        operation.put(0x88, owner);
        operation.put(0xf0, 59);
        auto manager = s::manager(owner);
        runtime::write<Address>(manager, 0xa8, reinterpret_cast<Address>(&queue_entry));
        runtime::write<Address>(manager, 0xb0, reinterpret_cast<Address>(&queue_entry + 1));
        *action = 59;
        return action;
    }
    template <class Fn> static Fn get(Address address) {
        if constexpr (std::is_same_v<Fn, decltype(&bind_destination)>)
            if (address == 0x7E1CE0) return &bind_destination;
        if constexpr (std::is_same_v<Fn, decltype(&copy_location)>)
            if (address == 0x7E35E0) return &copy_location;
        if constexpr (std::is_same_v<Fn, decltype(&submit)>)
            if (address == 0x7DFBF0) return &submit;
        return ExchangeCalls::get<Fn>(address);
    }
};

void scavenging_delivery(FakeImage& image) {
    auto unselected = +[](Address) -> Byte { return 0; };
    s::base = rva(unselected, 0x91B190);
    Boxer receiver{}, pack{};
    receiver.setup(s::base, 1);
    pack.setup(s::base, 2);
    pack.human.actor.put<Byte>(0x30, 1);
    Scene scene{};
    scene.set({receiver.ptr(), pack.ptr()});
    s::World w{image.base(), scene.scene.ptr(), 210, 1, 10};
    sh::refresh(w);
    auto& j = sh::jobs[0];
    j = {};
    j.receiver = sh::Person::from(receiver.ptr());
    j.donor.key = s::identity(pack.ptr());
    j.scavenge = j.hold = true;
    j.home = s::pos(receiver.ptr());
    j.need = {sh::Kind::ammo, receiver.definition.ptr(), 6, false};
    j.since = w.now;
    sh::NativeDelivery d{w, j, pack.ptr(), receiver.ptr()};
    pack.human.actor.put(0x74, s::Vec3{8, 0, 0});
    assert(d.mover() == receiver.ptr() && d.other() == pack.ptr() && !d.in_range() && d.at_post());
    receiver.human.actor.put(0x74, s::Vec3{4.99f, 0, 0});
    assert(!d.in_range());
    receiver.human.actor.put(0x74, s::Vec3{5, 0, 0});
    assert(d.in_range() && !d.at_post());

    Memory<0x90> order{};
    s::Watched move{};
    move.order = order.ptr();
    move.generation = 1;
    j.move = {&move, 1};
    j.endpoint = s::pos(pack.ptr());
    j.phase = sh::Phase::walking;
    j.moved = true;
    receiver.human.brain.put(0xf0, order.ptr());
    assert(sh::permit(w, receiver.ptr(), order.ptr()) && !sh::permit(w, pack.ptr(), order.ptr()));
    assert(d.safe_return());
    w.now += w.duration(20);
    assert(sh::permit(w, receiver.ptr(), order.ptr()));
    w.now += w.duration(25);
    assert(!sh::permit(w, receiver.ptr(), order.ptr()));
    ++w.now;
    j.since = w.now;
    scene.set({receiver.ptr()});
    assert(!sh::permit(w, receiver.ptr(), order.ptr()) && d.safe_return());
    j.phase = sh::Phase::returning;
    j.issued_return = true;
    assert(sh::permit(w, receiver.ptr(), order.ptr()));
    receiver.human.brain.put(0x39c, 1u << 28);
    assert(!sh::permit(w, receiver.ptr(), order.ptr()) && !d.safe_return());
    receiver.human.brain.put(0x39c, 0u);
    receiver.human.brain.put<Address>(0xf0, 0);
    j.move.abandon();
    j.phase = sh::Phase::assigned;
    j.moved = j.issued_return = false;
    scene.set({receiver.ptr(), pack.ptr()});

    s::base = rva(cancel_action, 0x7DDDC0);
    med::release_installed = true;
    med::original_transfer_destroy = +[](Address op, unsigned) -> Address { return op; };
    InventoryCalls::found = pack.item.ptr();
    InventoryCalls::from = pack.box.ptr();
    InventoryCalls::to = receiver.box.ptr();
    auto submit = [&] {
        return s::exchange_amount<InventoryCalls>(pack.ptr(), receiver.ptr(), pack.item.ptr(), 2, receiver.ptr());
    };
    assert(med::begin_owned_exchange(j.transfer, receiver.ptr(), submit));
    assert(InventoryCalls::owner == receiver.ptr() && InventoryCalls::quantity == 2);
    assert(!med::queued(j.transfer, pack.ptr()) && med::queued(j.transfer, receiver.ptr()) == j.transfer.operation);
    j.amount = 2;
    j.before = 0;
    assert(d.receipt() == sh::NativeDelivery::Receipt::pending);
    // The source disappears. The exact action is still cancellable on the live receiver.
    scene.set({receiver.ptr()});
    sh::cancel_transfer(w, j);
    assert(canceled_id == 59 && canceled_manager == receiver.manager.ptr());
    med::transfer_destroy(InventoryCalls::operation.ptr(), 0);
    receiver.manager.put<Address>(0xa8, 0);
    receiver.manager.put<Address>(0xb0, 0);
    scene.set({receiver.ptr(), pack.ptr()});
    assert(med::begin_owned_exchange(j.transfer, receiver.ptr(), submit));
    receiver.manager.put<Address>(0xa8, 0);
    receiver.manager.put<Address>(0xb0, 0);
    assert(d.receipt() == sh::NativeDelivery::Receipt::failure);
    med::transfer_destroy(InventoryCalls::operation.ptr(), 0);
    s::base = rva(matches, 0x565A50);
    ammo_key = receiver.definition.ptr() + 0xec8;
    rifle_query = j.need.gun + 0x120;
    receiver.item.put(0x48, 1);
    assert(d.receipt() == sh::NativeDelivery::Receipt::failure);
    receiver.item.put(0x48, 2);
    assert(d.receipt() == sh::NativeDelivery::Receipt::success);
    test::wave4_share::MockDelivery mock{};
    sh::progress(w, j, mock);
    assert(mock.outbound == 1 && j.phase == sh::Phase::walking);
    mock.close = true;
    sh::progress(w, j, mock);
    assert(mock.exchanges == 1 && j.phase == sh::Phase::exchange);
    mock.authorized_return = true;
    mock.observed = d.receipt();
    sh::progress(w, j, mock);
    assert(sh::stat_scavenge == 1 && j.delivered && j.phase == sh::Phase::returning);
    assert(sh::occupied(receiver.ptr()) && !sh::occupied(pack.ptr()));
    sh::progress(w, j, mock);
    assert(mock.returns == 1 && j.issued_return);
    mock.home = true;
    sh::progress(w, j, mock);
    assert(!sh::occupied(receiver.ptr()));
    j.delivered = false;
    j.phase = sh::Phase::assigned;
    mock = {};
    mock.movement = false;
    sh::progress(w, j, mock);
    assert(sh::source_blocked(w, receiver.ptr(), pack.ptr()));
    w.now = sh::last_tick - 1;
    sh::refresh(w);
    assert(sh::source_backoffs.empty() && !j.receiver.key.ptr);
    j.receiver = sh::Person::from(receiver.ptr());
    j.donor.key = s::identity(pack.ptr());
    sh::fail_scavenge(w, j);
    Scene next{};
    w.scene = next.scene.ptr();
    sh::refresh(w);
    assert(sh::source_backoffs.empty() && !j.receiver.key.ptr);
    sh::after_hooks_removed();
    med::after_hooks_removed();
    assert(sh::stat_scavenge == 0);
}

void passive_callbacks(FakeImage& image) {
    s::base = s::smoke::image_base = image.base();
    const auto saved = runtime::table().auth;
    auto& auth = runtime::table().auth;
    auth.is_remote_client = auth.is_quant_worker = rva(off, s::base);
    s::accepting = med::release_installed = true;
    Boxer unit{};
    unit.setup(s::base, 5);
    for (auto gate : {&auth.is_remote_client, &auth.is_quant_worker}) {
        *gate = rva(on, s::base);
        sh::tick(unit.ptr());
        med::tick(unit.ptr());
        assert(!sh::scene && !med::scene && sh::jobs.empty() && med::jobs.empty());
        *gate = rva(off, s::base);
    }
    s::World w{s::base, 0, 100, 1, 10};
    auto unselected = +[](Address) -> Byte { return 0; };
    for (unsigned flags : {1u, 1u << 28}) {
        unit.human.brain.put(0x39c, flags);
        assert(!sh::free_actor(w, unit.ptr(), 0, unselected) && !s::autonomous(unit.ptr(), true));
    }
    unit.human.brain.put(0x39c, 0u);
    unit.human.weaponry.put<Byte>(0x70, 2);
    assert(!sh::free_actor(w, unit.ptr(), 0, unselected) && !s::autonomous(unit.ptr(), true));
    unit.human.weaponry.put<Byte>(0x70, 0);
    unit.human.chassis.put(0x18, 1u);
    assert(!sh::free_actor(w, unit.ptr(), 0, unselected) && !s::autonomous(unit.ptr(), true));
    unit.human.chassis.put(0x18, 0u);
    unit.human.brain.put(0x398, 1u | (1u << 10));
    assert(!sh::scavenge_enabled(unit.human.brain.ptr()) && !s::autonomous(unit.ptr(), true));
    runtime::table().auth = saved;
    med::after_hooks_removed();
    s::after_hooks_removed();
}
} // namespace

int main() {
    FakeImage image{};
    grenade_escape(image);
    medic_failures();
    sources_and_selection(image);
    scavenging_delivery(image);
    passive_callbacks(image);
    std::puts("Stream A grenade escape, medic backoff and ammo scavenging fixtures passed.");
}
