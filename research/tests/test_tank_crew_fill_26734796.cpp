// Offline sanitized GEM fixtures for vehicle crew redistribution and refill (build 26734796).
#include "../../mods/tank_crew_fill_module.hpp"
#include "vehicle_fixture_26734796.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

namespace cf = goh::tank_crew_fill;
namespace d = cf::detail;
namespace p = cf::policy;
using cf::Address;

namespace {

using goh::test::vehicle::FakeHuman;
using goh::test::vehicle::FakeImage;
using goh::test::vehicle::FakeTank;
using goh::test::vehicle::Memory;

struct MoveCall {
    Address boarder{}, src_ent{}, dst_ent{};
    short src_place{-1}, dst_place{-1};
};

struct Harness {
    FakeImage img;
    d::World world{};
    Memory<0x100> scene{};
    std::vector<Address> scene_entities;
    std::vector<MoveCall> moves;
    std::vector<Address> returning_list;
    std::array<Memory<0x40>, 16> nodes{};
    bool worker{};
    bool remote{};
    bool allies_flag{true};
    bool access_flag{true};
    bool move_result{true};
    bool apply_move_effect{true};
    int missing_node_slot{-1};

    static inline Harness* current{};

    Harness() {
        current = this;
        world.base = img.base();
        world.scene = scene.ptr();
        world.length = 20.f;
        world.ticks = 1000.f;
        world.now = 10000;
        for (int i = 0; i < 16; ++i) {
            nodes[i].put<short>(0x00, static_cast<short>(i));
            nodes[i].put<int>(0x04, 2); // Place node
        }
    }
    ~Harness() {
        cf::after_hooks_removed();
        current = nullptr;
    }

    void sync_scene() {
        if (scene_entities.empty()) {
            scene.put<Address>(0x48, 0);
            scene.put<Address>(0x50, 0);
        } else {
            const auto first = reinterpret_cast<Address>(scene_entities.data());
            scene.put<Address>(0x48, first);
            scene.put<Address>(0x50, first + scene_entities.size() * sizeof(Address));
        }
    }

    void install() {
        cf::after_hooks_removed();
        goh::config::reset_defaults();
        assert(cf::init(img.base(), cf::executable_sha256));
        d::ops.is_worker = []() -> d::Byte { return current->worker ? 1 : 0; };
        d::ops.is_remote_client = []() -> d::Byte { return current->remote ? 1 : 0; };
        d::ops.read_world = [](Address, d::World& out) -> bool {
            out = current->world;
            return true;
        };
        d::ops.allies_fn = [](Address, unsigned a, unsigned b) -> bool { return a == b || current->allies_flag; };
        d::ops.boarder_root = [](Address b) -> Address { return b; };
        d::ops.find_node = [](Address, const short* id) -> Address {
            if (!id || *id < 0 || *id >= 16 || *id == current->missing_node_slot) return 0;
            return current->nodes[*id].ptr();
        };
        d::ops.boarder_move = [](Address b, Address se, const short* sp, Address de, const short* dp) -> d::Byte {
            current->moves.push_back({b, se, de, *sp, *dp});
            if (!current->move_result) return 0;
            if (current->apply_move_effect) {
                const auto placer = d::read<Address>(se, 0x568);
                const auto first = d::read<Address>(placer, 0x28);
                const auto s_slot = first + static_cast<Address>(*sp) * 0x298;
                const auto d_slot = first + static_cast<Address>(*dp) * 0x298;
                const auto human = d::read<Address>(s_slot, 0x280);
                const auto hid = d::read<std::int32_t>(s_slot, 0x250);
                std::memset(reinterpret_cast<void*>(s_slot + 0x280), 0, sizeof(Address));
                const std::int32_t neg1 = -1;
                std::memcpy(reinterpret_cast<void*>(s_slot + 0x250), &neg1, sizeof(neg1));
                std::memcpy(reinterpret_cast<void*>(d_slot + 0x280), &human, sizeof(human));
                std::memcpy(reinterpret_cast<void*>(d_slot + 0x250), &hid, sizeof(hid));
            }
            return 1;
        };
        d::ops.can_access = [](Address, const char*) -> d::Byte { return current->access_flag ? 1 : 0; };
        d::ops.vet_level = [](Address) -> unsigned { return 0; };
        d::ops.returning_crew = [](Address, Address* out, std::size_t cap) -> std::size_t {
            std::size_t n = 0;
            for (Address a : current->returning_list) {
                if (n >= cap) break;
                out[n++] = a;
            }
            return n;
        };
    }

    void tick(Address vehicle, unsigned elapsed = 50) {
        world.now += elapsed;
        cf::on_actor_post_quant(vehicle);
    }

    void expect_move(std::size_t count, short src, short dst) const {
        assert(moves.size() == count);
        assert(moves.back().src_place == src && moves.back().dst_place == dst);
        assert(moves.back().src_ent == moves.back().dst_ent);
    }
};

void test_policy_rules() {
    // 1. Crew destination classification
    p::SlotFacts gunner{"gunner", "crew", 0x1000u, true, false, false, false, false, true, 1};
    assert(p::is_crew_destination(gunner));

    auto disabled = gunner;
    disabled.flags |= 0x10u; // State.Disable (8s kill_crewman lockout)
    assert(!p::is_crew_destination(disabled));

    auto sealed = gunner;
    sealed.flags |= 0x100000u;
    assert(!p::is_crew_destination(sealed));

    auto occ = gunner;
    occ.occupied = true;
    assert(!p::is_crew_destination(occ));

    auto res = gunner;
    res.reserved = true;
    assert(!p::is_crew_destination(res));

    auto missing_bone = gunner;
    missing_bone.has_link_bone_name = true;
    missing_bone.has_resolved_bone = false;
    assert(!p::is_crew_destination(missing_bone));

    auto open_top_crew = gunner;
    open_top_crew.has_link_bone_name = true;
    open_top_crew.has_resolved_bone = true;
    assert(p::is_crew_destination(open_top_crew));

    // Exposed roof MG gunner2 (group "passenger") must NOT be a crew destination OR hull passenger source!
    p::SlotFacts gunner2_empty{"gunner2", "passenger", 0x1000u, true, true, true, false, false, false, 5};
    assert(!p::is_crew_destination(gunner2_empty));
    auto gunner2_occ = gunner2_empty;
    gunner2_occ.occupied = true;
    assert(!p::is_hull_passenger_source(gunner2_occ));

    // Ram Kangaroo passenger-group driver (bit 12 set) must NOT be a hull passenger source!
    p::SlotFacts ram_driver{"driver", "passenger", 0x1000u, false, true, true, true, false, false, 0};
    assert(!p::is_hull_passenger_source(ram_driver));

    // Standard hull passenger seat1
    p::SlotFacts seat1{"seat1", "passenger", 0u, false, true, true, true, false, false, 6};
    assert(p::is_hull_passenger_source(seat1));
    auto cabin = seat1;
    cabin.has_link_bone_name = cabin.has_resolved_bone = false;
    assert(!p::is_hull_passenger_source(cabin) && p::is_passenger_source(cabin, false));
    for (unsigned flag : {0x200u, 0x1000u, 0x8000u}) {
        cabin.flags = flag;
        assert(!p::is_passenger_source(cabin, false));
    }
    auto seat1_empty = seat1;
    seat1_empty.occupied = false;
    assert(!p::is_hull_passenger_source(seat1_empty));

    // 2. Crew destination priority
    p::SlotFacts driver{"driver", "crew", 0x1000u, true, false, false, false, false, false, 0};
    p::SlotFacts charger{"charger", "crew", 0x1000u, false, false, false, false, false, false, 2};
    p::SlotFacts commander{"commander", "crew", 0x1000u, true, false, false, false, false, false, 3};
    p::SlotFacts sec_cannon{"gunner4", "crew", 0x1000u, true, false, false, false, false, true, 4};
    p::SlotFacts charger2{"charger2", "crew", 0x1000u, false, false, false, false, false, false, 5};
    p::SlotFacts bow_mg{"gunner3", "crew", 0x1000u, true, false, false, false, false, false, 6};
    assert(p::better_crew_slot(gunner, driver));
    assert(p::better_crew_slot(driver, charger));
    assert(p::better_crew_slot(charger, commander));
    assert(p::better_crew_slot(commander, sec_cannon));
    assert(p::better_crew_slot(sec_cannon, charger2));
    assert(p::better_crew_slot(charger2, bow_mg));

    // 3. Passenger priority (tankman > tankman_ace > crew > driver > rifleman > medic/AT)
    p::PassengerFacts tankman{true, true, 1.0f, false, false, true, 4, 10};
    p::PassengerFacts ace{false, true, 1.0f, false, false, true, 3, 9};
    p::PassengerFacts art_crew{true, false, 0.f, false, false, true, 2, 8};
    p::PassengerFacts veh_driver{false, true, 0.5f, false, false, true, 1, 7};
    p::PassengerFacts rifleman{false, false, 0.f, false, false, true, 0, 6};
    p::PassengerFacts medic{false, false, 0.f, true, false, true, 0, 5};
    p::PassengerFacts bazooka{false, false, 0.f, false, true, true, 0, 4};
    assert(p::better_passenger(tankman, ace));
    assert(p::better_passenger(ace, art_crew));
    assert(p::better_passenger(art_crew, veh_driver));
    assert(p::better_passenger(veh_driver, rifleman));
    assert(p::better_passenger(rifleman, medic));
    assert(p::better_passenger(rifleman, bazooka));

    // Same-player beats allied-player within same tier
    p::PassengerFacts ally_tankman = tankman;
    ally_tankman.same_player = false;
    ally_tankman.slot_index = 0;
    assert(p::better_passenger(tankman, ally_tankman));

    // 4. Transfer gate
    assert(p::can_transfer({1, 0, true, false, false, false, false}));
    assert(!p::can_transfer({1, 1, true, false, false, false, false}));
    assert(p::can_transfer({2, 1, true, false, false, false, false}));
    assert(!p::can_transfer({2, 0, true, true, false, false, false}));
    assert(!p::can_transfer({2, 0, true, false, true, false, false}));
    assert(!p::can_transfer({2, 0, true, false, false, true, false}));
    assert(!p::can_transfer({2, 0, true, false, false, false, true}));
    assert(!p::can_transfer({2, 0, false, false, false, false, false}));
}

void test_priority_and_stepwise_fill() {
    Harness h;
    h.install();
    goh::config::active().modules.internal_crew_swap = false; // Original passenger-only policy.
    FakeTank t;
    t.setup(h.img.base(), 100, 10, 1, 1, 1); // Player-owned Hold Position tank
    t.set_slot_count(6);

    FakeHuman cmd, rifle, tankman, medic;
    cmd.setup(h.img.base(), 200, 20, 1, t.actor.ptr());
    rifle.setup(h.img.base(), 201, 21, 1, t.actor.ptr());
    rifle.set_breed_tag("soldier");
    tankman.setup(h.img.base(), 202, 22, 1, t.actor.ptr());
    tankman.set_breed_tag("crew");
    tankman.set_mechanic(h.img.base(), 1.0f);
    medic.setup(h.img.base(), 203, 23, 1, t.actor.ptr());
    medic.set_breed_tag("soldier");
    medic.set_medic(h.img.base());

    t.set_slot(0, "driver", "crew", 0x1000u, "driver", "", 0);
    t.set_slot(1, "gunner", "crew", 0x1000u, "main", "", 0);
    t.set_slot(2, "commander", "crew", 0x1000u, "around", "", cmd.actor.ptr(), 200);
    t.set_slot(3, "seat1", "passenger", 0u, "", "seat01", rifle.actor.ptr(), 201);
    t.set_slot(4, "seat2", "passenger", 0u, "", "seat02", tankman.actor.ptr(), 202);
    t.set_slot(5, "seat3", "passenger", 0u, "", "seat03", medic.actor.ptr(), 203);

    // Tick 1: tankman (slot 4) -> gunner (slot 1), 1 transfer per tick!
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.size() == 1);
    assert(h.moves[0].src_place == 4 && h.moves[0].dst_place == 1);
    assert(cf::stat_transfers.load() == 1);
    assert(cf::stat_transfers_crew_breed.load() == 1);

    // Same tick again is a no-op
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.size() == 1);

    // After the transfer settles, rifleman (slot 3) -> driver (slot 0), leaving medic on seat3.
    h.world.now += 1500;
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.size() == 2);
    assert(h.moves[1].src_place == 3 && h.moves[1].dst_place == 0);
    assert(cf::stat_transfers.load() == 2);
    assert(cf::stat_transfers_infantry.load() == 1);

    // All crew slots full -> no move.
    h.world.now += 1500;
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.size() == 2);
}

void test_lockout_and_gunner2_exclusion() {
    Harness h;
    h.install();
    FakeTank t;
    t.setup(h.img.base(), 101, 11, 1);
    t.set_slot_count(4);

    FakeHuman roof_mg, rider;
    roof_mg.setup(h.img.base(), 210, 30, 1, t.actor.ptr());
    rider.setup(h.img.base(), 211, 31, 1, t.actor.ptr());

    // Gunner disabled (8s kill_crewman lockout), gunner2 occupied, seat1 occupied
    t.set_slot(0, "gunner", "crew", 0x1010u, "main", "", 0);
    t.set_slot(1, "driver", "crew", 0x1000u, "driver", "", 0);
    t.set_slot(2, "gunner2", "passenger", 0x1000u, "top", "gunner2", roof_mg.actor.ptr(), 210);
    t.set_slot(3, "seat1", "passenger", 0u, "", "seat01", 0);

    // Only roof_mg (gunner2) is in passenger group -> must NOT be pulled off roof MG into driver!
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.empty());

    // Now put rider on seat1, leave driver occupied and gunner disabled -> still 0 moves (8s lockout respected)!
    t.set_slot(1, "driver", "crew", 0x1000u, "driver", "", roof_mg.actor.ptr(), 210);
    t.set_slot(2, "gunner2", "passenger", 0x1000u, "top", "gunner2", 0);
    t.set_slot(3, "seat1", "passenger", 0u, "", "seat01", rider.actor.ptr(), 211);
    h.world.now += 50;
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.empty()); // Also verifies seat1 is NOT moved to empty gunner2!

    // Clear 8s lockout on gunner -> rider moves into gunner!
    t.set_slot_flags(0, 0x1000u);
    h.world.now += 50;
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.size() == 1);
    assert(h.moves[0].src_place == 3 && h.moves[0].dst_place == 0);
}

void test_returning_repairer_and_dedup() {
    Harness h;
    h.install();
    FakeTank t;
    t.setup(h.img.base(), 102, 12, 1);
    t.set_slot_count(3);

    FakeHuman rider, repairer;
    rider.setup(h.img.base(), 220, 40, 1, t.actor.ptr());
    repairer.setup(h.img.base(), 221, 41, 1, 0); // on ground outside tank

    // Attach Order::eSmartRepair targeting t.actor to repairer
    Memory<0x80> smart_ord{};
    smart_ord.put<Address>(0x00, h.img.base() + d::smart_vt);
    smart_ord.put<Address>(0x08, repairer.actor.ptr() + 0xAF0);
    smart_ord.put<Address>(0x58, t.actor.ptr());
    repairer.brain.put<Address>(0xF0, smart_ord.ptr());

    h.scene_entities = {t.actor.ptr(), repairer.actor.ptr()};
    h.sync_scene();
    // Also report the exact same repairer via returning_crew callback to verify deduplication!
    h.returning_list = {repairer.actor.ptr()};

    t.set_slot(0, "driver", "crew", 0x1000u, "driver", "", 0);
    t.set_slot(1, "gunner", "crew", 0x1000u, "main", "", 0);
    t.set_slot(2, "seat1", "passenger", 0u, "", "seat01", rider.actor.ptr(), 220);

    // 2 empty crew slots, 1 deduplicated returning repairer -> 2 > 1 -> rider fills gunner!
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.size() == 1);
    assert(h.moves[0].src_place == 2 && h.moves[0].dst_place == 1);

    // Put another rider on seat1: now 1 empty crew slot (driver) and 1 returning repairer -> DEFERRED!
    FakeHuman rider2;
    rider2.setup(h.img.base(), 222, 42, 1, t.actor.ptr());
    t.set_slot(2, "seat1", "passenger", 0u, "", "seat01", rider2.actor.ptr(), 222);
    h.world.now += 1500;
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.size() == 1);
    assert(cf::stat_deferred_repair.load() == 1);

    // If repairer dies (vitality = 1) and is cleared from returning_list -> rider2 immediately fills driver!
    repairer.vit.put<d::Byte>(0x174, 1);
    h.returning_list.clear();
    h.world.now += 50;
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.size() == 2);
    assert(h.moves[1].src_place == 2 && h.moves[1].dst_place == 0);
}

void test_emit_backoff_and_guards() {
    Harness h;
    h.install();
    FakeTank t;
    t.setup(h.img.base(), 103, 13, 1);
    t.set_slot_count(2);

    FakeHuman rider;
    rider.setup(h.img.base(), 230, 50, 1, t.actor.ptr());
    t.set_slot(0, "driver", "crew", 0x1000u, "driver", "", 0);
    t.set_slot(1, "seat1", "passenger", 0u, "", "seat01", rider.actor.ptr(), 230);

    // 1. Active Order::eEmit (mode 2 = EMIT_CREW) triggers 10s backoff
    Memory<0x68> emit_ord{};
    emit_ord.put<Address>(0x00, h.img.base() + d::emit_vt);
    emit_ord.put<Address>(0x08, t.actor.ptr() + 0xAF0);
    emit_ord.put<int>(0x5C, 2);
    t.brain.put<Address>(0xF0, emit_ord.ptr());
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.empty());
    assert(cf::stat_deferred_emit.load() == 1);

    // Clear Order::eEmit; still within 10s backoff -> no transfer
    t.brain.put<Address>(0xF0, 0);
    h.world.now += 5000;
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.empty());

    // After 10s backoff expires -> check Direct Control blocks it
    h.world.now += 6000;
    t.chassis.put<unsigned>(0x18, 1);
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.empty());
    t.chassis.put<unsigned>(0x18, 0);

    // Broken body blocks it
    t.set_body_state(1);
    h.world.now += 50;
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.empty());
    t.set_body_state(0);

    // Exit walker blocks it
    t.set_exit_walker(rider.actor.ptr());
    h.world.now += 50;
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.empty());
    t.clear_walkers();

    // Placer blocking byte (+0x48) blocks it
    t.placer.put<d::Byte>(0x48, 1);
    h.world.now += 50;
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.empty());
    t.placer.put<d::Byte>(0x48, 0);

    // Passenger manual order (bit 28) blocks it
    rider.brain.put<unsigned>(0x39C, (1u << 28) | (1u << 19) | 1u);
    h.world.now += 50;
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.empty());
    rider.brain.put<unsigned>(0x39C, (1u << 19) | 1u);

    // Stale reservation for rider on another slot blocks it
    t.set_slot_reserved(0, rider.actor.ptr());
    h.world.now += 50;
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.empty());
    t.set_slot_reserved(0, 0);

    // Now clear -> succeeds!
    h.world.now += 50;
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.size() == 1);
}

void test_ownership_remote_retry_and_rewind() {
    Harness h;
    h.install();
    goh::config::active().modules.internal_crew_swap = false; // Isolate allied passenger admission.
    FakeTank t;
    t.setup(h.img.base(), 104, 14, 1);
    t.set_slot_count(3);

    FakeHuman ally_rider, own_cmd;
    ally_rider.setup(h.img.base(), 240, 60, 2, t.actor.ptr()); // different allied player 2!
    own_cmd.setup(h.img.base(), 241, 61, 1, t.actor.ptr());

    // 0 same-player crew inside -> allied passenger must NOT hijack tank ownership!
    t.set_slot(0, "driver", "crew", 0x1000u, "driver", "", 0);
    t.set_slot(1, "commander", "crew", 0x1000u, "around", "", 0);
    t.set_slot(2, "seat1", "passenger", 0u, "", "seat01", ally_rider.actor.ptr(), 240);
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.empty());

    // With same-player commander inside, but CanAccess=0 -> still rejected!
    t.set_slot(1, "commander", "crew", 0x1000u, "around", "", own_cmd.actor.ptr(), 241);
    h.access_flag = false;
    h.world.now += 50;
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.empty());

    // With CanAccess=1, but remote client or worker -> passive!
    h.access_flag = true;
    h.remote = true;
    h.world.now += 50;
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.empty());
    h.remote = false;
    h.worker = true;
    h.world.now += 50;
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.empty());
    h.worker = false;

    // Simulate failed eBoarder::Move -> sets 1s retry cooldown!
    h.move_result = false;
    h.world.now += 50;
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.size() == 1);
    assert(cf::stat_transfer_failures.load() == 1);

    // Within 1s retry cooldown -> skipped
    h.move_result = true;
    h.world.now += 500;
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.size() == 1);

    // Clock rewind resets retry cooldown -> succeeds immediately!
    h.world.now = 100;
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.size() == 2);
    assert(h.moves[1].src_place == 2 && h.moves[1].dst_place == 0);

    // Wrong SHA-256 rejected
    cf::after_hooks_removed();
    assert(!cf::init(h.img.base(), "0000000000000000000000000000000000000000000000000000000000000000"));
}

void test_breed_traits_and_additional_safety() {
    Harness h;
    h.install();
    FakeTank t;
    t.setup(h.img.base(), 105, 15, 1);
    t.set_slot_count(5);

    // Crewless tank (0 crew inside) with 4 same-player hull passengers:
    // bazooka (tier 5), rifle (tier 4), basic_repair driver (tier 3), tankman_ace (tier 1)
    FakeHuman bazooka, rifle, drv, ace;
    bazooka.setup(h.img.base(), 250, 70, 1, t.actor.ptr());
    bazooka.set_breed_tag("soldier_bazooka");
    rifle.setup(h.img.base(), 251, 71, 1, t.actor.ptr());
    rifle.set_breed_tag("soldier");
    drv.setup(h.img.base(), 252, 72, 1, t.actor.ptr());
    drv.set_breed_tag("soldier");
    drv.set_mechanic(h.img.base(), 0.5f);
    ace.setup(h.img.base(), 253, 73, 1, t.actor.ptr());
    ace.set_breed_tag("soldier");
    ace.set_mechanic(h.img.base(), 1.0f);

    t.set_slot(0, "gunner", "crew", 0x1000u, "main", "", 0);
    t.set_slot(1, "seat1", "passenger", 0u, "", "seat01", bazooka.actor.ptr(), 250);
    t.set_slot(2, "seat2", "passenger", 0u, "", "seat02", rifle.actor.ptr(), 251);
    t.set_slot(3, "seat3", "passenger", 0u, "", "seat03", drv.actor.ptr(), 252);
    t.set_slot(4, "seat4", "passenger", 0u, "", "seat04", ace.actor.ptr(), 253);

    // Burning, sinking, flipped, missing node, EMIT_PASSENGERS active -> blocked
    t.actor.put<d::Byte>(0xDAD, 1u << 1); // burning bit 1
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.empty());
    t.actor.put<d::Byte>(0xDAD, 1u << 2); // sinking bit 2
    h.world.now += 50;
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.empty());
    t.actor.put<d::Byte>(0xDAD, 0);

    t.actor.put<float>(0x70, 0.2f); // tilted > 70 deg
    h.world.now += 50;
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.empty());
    t.actor.put<float>(0x70, 1.0f);

    h.missing_node_slot = 0; // gunner has no boarder node
    h.world.now += 50;
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.empty());
    h.missing_node_slot = -1;

    // EMIT_PASSENGERS (mode 1) defers while active, but imposes NO 10s crew-emit backoff once done!
    Memory<0x68> emit_pax{};
    emit_pax.put<Address>(0x00, h.img.base() + d::emit_vt);
    emit_pax.put<Address>(0x08, t.actor.ptr() + 0xAF0);
    emit_pax.put<int>(0x5C, 1);
    t.brain.put<Address>(0xF0, emit_pax.ptr());
    h.world.now += 50;
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.empty());
    t.brain.put<Address>(0xF0, 0);

    // Next tick: crewless tank immediately pulls tankman_ace (slot 4, tier 1) into gunner (slot 0)!
    h.world.now += 50;
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.size() == 1);
    assert(h.moves[0].src_place == 4 && h.moves[0].dst_place == 0);

    // Free gunner again -> basic_repair driver (slot 3, tier 3) beats rifle (tier 4) & bazooka (tier 5)!
    t.set_slot(0, "gunner", "crew", 0x1000u, "main", "", 0);
    h.world.now += 1500;
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.size() == 2);
    assert(h.moves[1].src_place == 3 && h.moves[1].dst_place == 0);

    // Free gunner again -> rifle (slot 2, tier 4) beats bazooka (slot 1, tier 5)!
    t.set_slot(0, "gunner", "crew", 0x1000u, "main", "", 0);
    h.world.now += 1500;
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.size() == 3);
    assert(h.moves[2].src_place == 2 && h.moves[2].dst_place == 0);

    // Free gunner again -> bazooka (slot 1, tier 5) fills last!
    t.set_slot(0, "gunner", "crew", 0x1000u, "main", "", 0);
    h.world.now += 1500;
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.size() == 4);
    assert(h.moves[3].src_place == 1 && h.moves[3].dst_place == 0);
}

void test_internal_policy() {
    const auto seat = [](std::string_view name, short index, bool occupied) {
        p::SlotFacts s{};
        s.group = "crew";
        s.name = name;
        s.slot_index = index;
        s.occupied = occupied;
        return s;
    };
    const auto gunner = seat("gunner", 0, false), driver = seat("driver", 1, false);
    auto commander = seat("commander", 2, true), charger = seat("charger", 3, true);
    auto mg = seat("gunner3", 4, true);
    mg.flags = 0x1000;
    p::VehicleTacticalContext ctx{};
    ctx.has_eligible_passenger = true;
    ctx.swap_cooldown_active = true;
    assert(p::can_promote_internal(mg, gunner, ctx));
    assert(p::can_promote_internal(commander, gunner, ctx));
    assert(p::can_promote_internal(charger, driver, ctx));
    assert(!p::can_promote_internal(charger, seat("commander", 5, false), ctx));
    assert(!p::can_promote_internal(commander, seat("commander", 5, false), ctx));
    assert(!p::can_promote_internal(commander, seat("gunner", 2, false), ctx));
    auto spare = seat("spare", 6, true), empty_mg = mg;
    empty_mg.occupied = false;
    assert(!p::can_promote_internal(spare, empty_mg, ctx));
    assert(!p::can_promote_internal(spare, seat("bench", 7, false), ctx));
    for (unsigned flag : {0x200u, 0x8000u}) {
        mg.flags = 0x1000u | flag;
        assert(!p::is_internal_crew_source(mg));
    }
    mg.flags = 0x1000u | (1u << 13); // The stock placer's temporary donor mark.
    assert(p::is_internal_crew_source(mg));
    auto secondary = mg;
    secondary.is_cannon_gunner = true;
    const p::PassengerFacts trained{true, true, 1.f}, infantry{};
    assert(p::better_internal_source({spare, infantry}, {mg, trained}));
    assert(p::better_internal_source({mg, infantry}, {secondary, trained}));
    assert(p::better_internal_source({secondary, infantry}, {commander, trained}));
    assert(p::better_internal_source({commander, trained}, {commander, infantry}));
    assert(p::better_internal_source({commander, trained}, {seat("commander", 7, true), trained}));

    const auto driving = seat("driver", 1, true), gunning = seat("gunner", 0, true);
    for (unsigned flags = 0; flags < 64; ++flags) {
        ctx.immobilized = flags & 1u;
        ctx.has_move_order = flags & 2u;
        ctx.has_visible_enemy = flags & 4u;
        ctx.swap_cooldown_active = flags & 8u;
        ctx.has_eligible_passenger = flags & 16u;
        ctx.occupied_crew_count = (flags & 32u) ? 2u : 1u;
        const bool driver_to_gunner = p::can_promote_internal(driving, gunner, ctx);
        const bool gunner_to_driver = p::can_promote_internal(gunning, driver, ctx);
        const bool last_resort = !ctx.has_eligible_passenger && !ctx.swap_cooldown_active;
        assert(driver_to_gunner == (last_resort && !ctx.has_move_order && (ctx.has_visible_enemy || ctx.immobilized)));
        assert(gunner_to_driver ==
               (last_resort && ctx.occupied_crew_count == 1 && ctx.has_move_order && !ctx.immobilized));
        assert(!(driver_to_gunner && gunner_to_driver));
    }
}

void test_internal_then_passenger(bool returning_repairer) {
    Harness h;
    h.install();
    FakeTank t;
    t.setup(h.img.base(), 106, 16, 1);
    FakeHuman commander, rider;
    commander.setup(h.img.base(), 260, 80, 1, t.actor.ptr());
    commander.set_breed_tag("crew");
    rider.setup(h.img.base(), 261, 81, 1, t.actor.ptr());
    t.set_slot_count(3);
    t.set_slot(0, "gunner", "crew", 0x1000u, "main", "", 0);
    t.set_slot(1, "commander", "crew", 0x3000u, "around", "", commander.actor.ptr(), 260);
    t.set_slot(2, "seat1", "passenger", 0, "", "seat01", rider.actor.ptr(), 261);
    FakeHuman repairer;
    if (returning_repairer) h.returning_list = {repairer.actor.ptr()};

    t.set_slot_reserved(0, repairer.actor.ptr());
    h.tick(t.actor.ptr());
    assert(h.moves.empty());
    t.set_slot_reserved(0, 0);
    h.tick(t.actor.ptr());
    h.expect_move(1, 1, 0);
    h.tick(t.actor.ptr(), 0);
    assert(h.moves.size() == 1);
    h.tick(t.actor.ptr(), 1500);
    if (returning_repairer) {
        assert(h.moves.size() == 1 && cf::stat_deferred_repair == 1);
        h.returning_list.clear();
        h.tick(t.actor.ptr());
    }
    h.expect_move(2, 2, 1);
    assert(cf::stat_transfers_internal == 1 && cf::stat_transfers_nontank == 0);
}

void test_emergency_swap_timing() {
    Harness h;
    h.install();
    FakeTank t;
    t.setup(h.img.base(), 107, 17, 1);
    FakeHuman driver, rider;
    driver.setup(h.img.base(), 270, 82, 1, t.actor.ptr());
    rider.setup(h.img.base(), 271, 83, 1, t.actor.ptr());
    t.set_slot_count(3);
    t.set_slot(0, "gunner", "crew", 0x1000u, "main", "", 0);
    t.set_slot(1, "driver", "crew", 0x1000u, "driver", "", driver.actor.ptr(), 270);
    Memory<0x20> subject{};
    std::array<Address, 2> subjects{0, subject.ptr()};
    t.brain.put<Address>(0x3A0, reinterpret_cast<Address>(subjects.data()));
    t.brain.put<Address>(0x3A8, reinterpret_cast<Address>(subjects.data() + subjects.size()));
    subject.put<unsigned>(0x10, 1u << 28);
    h.tick(t.actor.ptr());
    assert(h.moves.empty());

    subject.put<unsigned>(0x10, 1u << 29);
    for (unsigned bit : {6u, 9u, 28u}) {
        t.brain.put<unsigned>(0x39C, 1u << bit);
        h.tick(t.actor.ptr());
        assert(h.moves.empty());
    }
    t.brain.put<unsigned>(0x39C, 0);
    Memory<0x68> move_task{};
    move_task.put<Address>(0, h.img.base() + goh::runtime::table().vt.move_task_order);
    move_task.put<Address>(8, t.actor.ptr() + 0xAF0);
    t.brain.put<Address>(0xF0, move_task.ptr());
    h.tick(t.actor.ptr());
    assert(h.moves.empty());
    t.brain.put<Address>(0xF0, 0);

    // A passenger awaiting a repairer's return still protects the driver's role.
    t.set_slot(2, "seat1", "passenger", 0, "", "seat01", rider.actor.ptr(), 271);
    FakeHuman repairer;
    h.returning_list = {repairer.actor.ptr()};
    h.tick(t.actor.ptr());
    assert(h.moves.empty());
    t.set_slot(2, "seat1", "passenger", 0, "", "seat01", 0);
    h.tick(t.actor.ptr());
    h.expect_move(1, 1, 0);
    t.brain.put<Address>(0xF0, move_task.ptr());
    h.tick(t.actor.ptr(), 2999);
    assert(h.moves.size() == 1);
    h.tick(t.actor.ptr(), 1);
    h.expect_move(2, 0, 1);
    t.brain.put<Address>(0xF0, 0);
    h.tick(t.actor.ptr(), 2999);
    assert(h.moves.size() == 2);
    subject.put<unsigned>(0x10, 0);
    t.comp_rows.put<d::Byte>(0x28 + 0x24, 2); // Damaged track, clear field of view.
    h.tick(t.actor.ptr(), 1);
    h.expect_move(3, 1, 0);
    t.brain.put<unsigned>(0x39C, 1u << 28);
    h.tick(t.actor.ptr(), 3000);
    assert(h.moves.size() == 3); // A move order alone cannot make this tank mobile.
    assert(cf::stat_transfers_internal == 3);
}

void test_internal_donor_selection() {
    Harness h;
    h.install();
    goh::config::active().modules.nontank_crew_fill = false;
    FakeTank t;
    t.setup(h.img.base(), 110, 20, 1);
    FakeHuman commander, bow_gunner, loader;
    commander.setup(h.img.base(), 300, 89, 1, t.actor.ptr());
    commander.set_breed_tag("crew");
    bow_gunner.setup(h.img.base(), 301, 90, 1, t.actor.ptr());
    loader.setup(h.img.base(), 302, 91, 1, t.actor.ptr());
    t.set_slot_count(5);
    t.set_slot(0, "gunner", "crew", 0x1000u, "main", "", 0);
    t.set_slot(1, "driver", "crew", 0x1000u, "driver", "", 0);
    t.set_slot(2, "commander", "crew", 0x1000u, "around", "", commander.actor.ptr(), 300);
    t.set_slot(3, "gunner3", "crew", 0x1000u, "bow", "", bow_gunner.actor.ptr(), 301);
    t.set_slot(4, "charger", "crew", 0x1000u, "", "", loader.actor.ptr(), 302);
    h.tick(t.actor.ptr());
    h.expect_move(1, 3, 0); // Preserve the trained commander and the main loader.
    commander.chassis.put<unsigned>(0x18, 1);
    h.tick(t.actor.ptr(), 1500);
    h.expect_move(2, 4, 1); // Charger covers driver while commander is in Direct Control.
    h.tick(t.actor.ptr());
    assert(h.moves.size() == 2);
}

void test_damaged_tank_keeps_bow_gunner() {
    Harness h;
    h.install();
    FakeTank t;
    t.setup(h.img.base(), 120, 23, 1);
    Memory<0x28 * 3> parts;
    Memory<0x360> gun_def;
    Memory<0x60> gun_name;
    std::memcpy(parts.data.data(), t.comp_rows.data.data(), 2 * 0x28);
    parts.put<d::Byte>(0x28 + 0x24, 1); // Broken track.
    gun_name.ename(0, "gun");
    gun_def.put<Address>(0x328, gun_name.ptr());
    gun_def.put<Address>(0x330, gun_name.ptr() + 0x60);
    parts.put<Address>(2 * 0x28, gun_def.ptr());
    parts.put<Address>(2 * 0x28 + 8, t.actor.ptr());
    parts.put<d::Byte>(2 * 0x28 + 0x24, 1);
    t.actor.put<Address>(0xB90, parts.ptr());
    t.actor.put<Address>(0xB98, parts.ptr() + 3 * 0x28);
    FakeHuman first, second, repairer1, repairer2;
    first.setup(h.img.base(), 202, 32, 1, t.actor.ptr());
    second.setup(h.img.base(), 203, 33, 1, t.actor.ptr());
    h.returning_list = {repairer1.actor.ptr(), repairer2.actor.ptr()};
    t.set_slot_count(5);
    t.set_slot(0, "driver", "crew", 0x1000u, "driver", "", 0);
    t.set_slot(1, "gunner", "crew", 0x1000u, "main", "", 0);
    t.set_slot(2, "charger", "crew", 0x1000u, "", "", 0);
    t.set_slot(3, "gunner3", "crew", 0x1000u, "bow", "", first.actor.ptr(), 202);
    t.set_slot(4, "commander", "crew", 0x1000u, "around", "", 0);
    h.tick(t.actor.ptr());
    assert(h.moves.empty());
    // The track is still broken. A repaired main gun is useful immediately.
    parts.put<d::Byte>(2 * 0x28 + 0x24, 0);
    h.tick(t.actor.ptr());
    h.expect_move(1, 3, 1);
    // Vanilla staffs the same bow MG with someone else. The best vacancy has
    // changed too. Neither change should reset the weapon-seat backoff.
    t.set_slot(3, "gunner3", "crew", 0x1000u, "bow", "", second.actor.ptr(), 203);
    parts.put<d::Byte>(0x28 + 0x24, 0);
    h.tick(t.actor.ptr(), 9999);
    assert(h.moves.size() == 1);
    h.tick(t.actor.ptr(), 1);
    h.expect_move(2, 3, 0);
}

void test_wheeled_mobility_damage(std::size_t damaged_row) {
    Harness h;
    h.install();
    FakeTank t;
    t.setup(h.img.base(), 111, 21, 1);
    t.set_wheeled("armoredcar");
    FakeHuman driver;
    driver.setup(h.img.base(), 303, 92, 1, t.actor.ptr());
    t.set_slot_count(2);
    t.set_slot(0, "gunner", "crew", 0x1000u, "main", "", 0);
    t.set_slot(1, "driver", "crew", 0x1000u, "driver", "", driver.actor.ptr(), 303);
    h.tick(t.actor.ptr());
    assert(h.moves.empty());

    t.comp_rows.put<d::Byte>(damaged_row * 0x28 + 0x24, 1);
    h.move_result = false;
    h.tick(t.actor.ptr());
    h.expect_move(1, 1, 0);
    assert(cf::stat_transfer_failures == 1 && cf::stat_transfers_internal == 0 && cf::stat_transfers_nontank == 0);
    h.move_result = true;
    h.tick(t.actor.ptr(), 999);
    assert(h.moves.size() == 1);
    h.tick(t.actor.ptr(), 1); // Failed swap armed only the retry delay.
    h.expect_move(2, 1, 0);
    assert(cf::stat_transfers_internal == 1 && cf::stat_transfers_nontank == 1);
}

void test_nontank_passengers(std::string_view type, std::string_view destination, std::string_view bone) {
    Harness h;
    h.install();
    FakeTank t;
    t.setup(h.img.base(), 108, 18, 1);
    t.set_wheeled(type);
    FakeHuman rider, roof_mg, apc_driver, apc_commander;
    rider.setup(h.img.base(), 280, 84, 1, t.actor.ptr());
    roof_mg.setup(h.img.base(), 281, 85, 1, t.actor.ptr());
    apc_driver.setup(h.img.base(), 282, 86, 1, t.actor.ptr());
    apc_commander.setup(h.img.base(), 283, 87, 1, t.actor.ptr());
    t.set_slot_count(5);
    t.set_slot(0, destination, "crew", 0x1000u, "front", "", 0);
    t.set_slot(1, "gunner2", "passenger", 0x1000u, "top", "roof", roof_mg.actor.ptr(), 281);
    t.set_slot(2, "driver", "passenger", 0x1000u, "", "", apc_driver.actor.ptr(), 282);
    t.set_slot(3, "commander", "passenger", 0, "around", "", apc_commander.actor.ptr(), 283);
    t.set_slot(4, "seat1", "passenger", 0, "", "unresolved", rider.actor.ptr(), 280);
    t.slots.put<Address>(4 * 0x298 + 0x240, 0);
    h.tick(t.actor.ptr());
    assert(h.moves.empty());
    t.set_slot(4, "seat1", "passenger", 0, "", bone, rider.actor.ptr(), 280);
    auto& modules = goh::config::active().modules;
    modules.nontank_crew_fill = false;
    h.tick(t.actor.ptr());
    assert(h.moves.empty());
    modules.nontank_crew_fill = true;
    modules.tank_crew_fill = false;
    h.tick(t.actor.ptr());
    assert(h.moves.empty());
    modules.tank_crew_fill = true;
    modules.internal_crew_swap = false;
    h.tick(t.actor.ptr());
    h.expect_move(1, 4, 0);
    assert(cf::stat_transfers_internal == 0 && cf::stat_transfers_nontank == 1);
}

void test_cannon_guards() {
    Harness h;
    h.install();
    FakeTank t;
    t.setup(h.img.base(), 109, 19, 1);
    t.actor.ename(0xDE8, "cannon_small");
    t.actor.put<Address>(0xB98, t.comp_rows.ptr()); // No body or tracks.
    FakeHuman commander;
    commander.setup(h.img.base(), 290, 88, 1, t.actor.ptr());
    t.set_slot_count(2);
    t.set_slot(0, "gunner", "crew", 0x1000u, "main", "", 0);
    t.set_slot(1, "commander", "crew", 0x1000u, "around", "", commander.actor.ptr(), 290);
    Memory<0x40> aggregate{};
    Memory<0xC38> cannon{};
    const Address extender = cannon.ptr();
    aggregate.put<Address>(0, reinterpret_cast<Address>(&extender));
    aggregate.put<Address>(8, reinterpret_cast<Address>(&extender + 1));
    t.actor.put<Address>(0x578, aggregate.ptr());
    cannon.put<Address>(0, h.img.base() + goh::runtime::table().vt.cannon_ext);
    cannon.put<Address>(0x18, commander.actor.ptr());
    assert(!cf::support::extender(t.actor.ptr(), goh::runtime::table().vt.cannon_ext, h.img.base()));
    cannon.put<Address>(0x18, t.actor.ptr());
    for (int state : {1, 2, 3}) {
        cannon.put<int>(0xC30, state);
        h.tick(t.actor.ptr());
        assert(h.moves.empty());
    }
    cannon.put<int>(0xC30, 0);
    t.actor.put<Address>(0xB98, t.comp_rows.ptr() + 1);
    h.tick(t.actor.ptr());
    assert(h.moves.empty());
    t.actor.put<Address>(0xB98, t.comp_rows.ptr());
    t.actor.put<unsigned>(0x1C0, 1u << 28);
    h.tick(t.actor.ptr());
    assert(h.moves.empty());
    t.actor.put<unsigned>(0x1C0, 0);
    goh::config::active().modules.internal_crew_swap = false;
    h.tick(t.actor.ptr());
    assert(h.moves.empty());
    goh::config::active().modules.internal_crew_swap = true;
    h.tick(t.actor.ptr());
    h.expect_move(1, 1, 0);
    assert(cf::stat_transfers_internal == 1 && cf::stat_transfers_nontank == 1);
}

void test_accepted_cannon_transfer_stalls() {
    Harness h;
    h.install();
    h.apply_move_effect = false; // The order was accepted, but the charger is still in place.
    FakeTank gun;
    gun.setup(h.img.base(), 112, 22, 1);
    gun.actor.ename(0xDE8, "cannon_small");
    gun.actor.put<Address>(0xB98, gun.comp_rows.ptr());
    FakeHuman charger, replacement;
    charger.setup(h.img.base(), 304, 93, 1, gun.actor.ptr());
    replacement.setup(h.img.base(), 305, 94, 1, gun.actor.ptr());
    gun.set_slot_count(2);
    gun.set_slot(0, "gunner", "crew", 0x1000u, "main", "", 0);
    gun.set_slot(1, "charger", "crew", 0x1000u, "", "", charger.actor.ptr(), 304);

    h.tick(gun.actor.ptr());
    h.expect_move(1, 1, 0);
    h.tick(gun.actor.ptr(), 7);
    assert(h.moves.size() == 1);
    unsigned calls = 1;
    for (unsigned remaining : {1493u, 5000u, 10000u, 10000u}) {
        h.tick(gun.actor.ptr(), remaining - 1);
        assert(h.moves.size() == calls);
        h.tick(gun.actor.ptr(), 1);
        h.expect_move(++calls, 1, 0);
    }

    // The new charger waits for the per-vehicle cooldown and starts a fresh retry sequence.
    gun.set_slot(1, "charger", "crew", 0x1000u, "", "", replacement.actor.ptr(), 305);
    h.tick(gun.actor.ptr(), 1499);
    assert(h.moves.size() == calls);
    h.tick(gun.actor.ptr(), 1);
    h.expect_move(++calls, 1, 0);
    h.apply_move_effect = true;
    h.tick(gun.actor.ptr(), 1500);
    h.expect_move(++calls, 1, 0);
    h.tick(gun.actor.ptr(), 10000);
    assert(h.moves.size() == calls && cf::stat_transfer_failures == 0);
}

} // namespace

int main() {
    test_policy_rules();
    test_priority_and_stepwise_fill();
    test_lockout_and_gunner2_exclusion();
    test_returning_repairer_and_dedup();
    test_emit_backoff_and_guards();
    test_ownership_remote_retry_and_rewind();
    test_breed_traits_and_additional_safety();
    test_internal_policy();
    test_internal_then_passenger(false);
    test_internal_then_passenger(true);
    test_emergency_swap_timing();
    test_internal_donor_selection();
    test_damaged_tank_keeps_bow_gunner();
    test_wheeled_mobility_damage(0); // Engine.
    test_wheeled_mobility_damage(1); // Wheel.
    test_nontank_passengers("halftrack", "gunner", "seat01");
    test_nontank_passengers("truck", "driver", "");
    test_nontank_passengers("armoredcar", "gunner", "");
    test_cannon_guards();
    test_accepted_cannon_transfer_stalls();
    std::printf("test_tank_crew_fill_26734796: ALL PASSED\n");
    return 0;
}
