// Offline sanitized GEM fixtures for Tank Exterior-Passenger Crew Refill (build 26734796).
#include "../../mods/tank_crew_fill_module.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

namespace cf = goh::tank_crew_fill;
namespace d = cf::detail;
namespace p = cf::policy;
using cf::Address;

namespace {

template <std::size_t N> struct Memory {
    alignas(16) std::array<std::byte, N> data{};
    Address ptr() const {
        return reinterpret_cast<Address>(data.data());
    }
    template <class T> void put(std::size_t off, T v) {
        assert(off + sizeof(v) <= N);
        std::memcpy(data.data() + off, &v, sizeof(v));
    }
    template <class T> T get(std::size_t off) const {
        assert(off + sizeof(v_dummy<T>()) <= N);
        T v{};
        std::memcpy(&v, data.data() + off, sizeof(v));
        return v;
    }
    void ename(std::size_t off, std::string_view s) {
        assert(s.size() <= 15);
        std::memset(data.data() + off + 0x40, 0, 0x20);
        if (!s.empty()) std::memcpy(data.data() + off + 0x40, s.data(), s.size());
        put(off + 0x50, s.size());
        put(off + 0x58, std::size_t{15});
    }

private:
    template <class T> static T v_dummy() {
        return T{};
    }
};

// Fake image buffer covering vtables and dynamic ability indices (burn=0x15C6A58, sink=0x15C6A5C).
struct FakeImage {
    std::vector<std::byte> bytes;
    FakeImage() : bytes(0x1600000) {
        put<unsigned>(0x15C6A58, 1u); // burning ability bit index
        put<unsigned>(0x15C6A5C, 2u); // sinking ability bit index
    }
    Address base() const {
        return reinterpret_cast<Address>(bytes.data());
    }
    template <class T> void put(std::size_t off, T v) {
        std::memcpy(bytes.data() + off, &v, sizeof(v));
    }
};

struct MoveCall {
    Address boarder{}, src_ent{}, dst_ent{};
    short src_place{-1}, dst_place{-1};
};

struct Harness {
    FakeImage img{};
    d::World world{};
    Memory<0x100> scene{};
    std::vector<Address> scene_entities{};
    std::vector<MoveCall> moves{};
    std::vector<Address> returning_list{};
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
};

struct FakeHuman {
    Memory<0x1400> actor{};
    Memory<0x400> brain{};
    Memory<0x80> chassis{};
    Memory<0x80> weaponry{};
    Memory<0x40> agg{};
    Memory<0x180> vit{};
    Memory<0x40> link{};
    Memory<0x600> breed{};
    Memory<0x60> breed_tag{};
    Memory<0x30> mech_info{};
    Memory<0x30> medic_info{};
    std::array<Address, 2> ability_ptrs{};

    void setup(Address base, std::uint32_t id, std::uint16_t route, std::uint8_t player, Address parent_tank,
               unsigned ctrl = 1, unsigned move_mode = 1) {
        actor.put<d::Byte>(0x30, 0x07); // entity + actor + human
        actor.put<unsigned>(0x38, 0);
        actor.put<Address>(0xB0, actor.ptr() + 0x10);
        actor.put<std::uint32_t>(0xC0, id);
        actor.put<unsigned>(0x1C0, parent_tank ? (1u << 28) : 0u);
        actor.put<Address>(0x560, link.ptr());
        actor.put<Address>(0x578, agg.ptr());
        actor.put<d::Byte>(0xDA8, player);
        actor.put<std::uint16_t>(0xDAA, route);
        actor.put<unsigned>(0xDB4, 0x100u);
        actor.put<Address>(0xDC8, brain.ptr());
        actor.put<Address>(0xDD8, weaponry.ptr());
        actor.put<Address>(0xDE0, chassis.ptr());
        actor.put<Address>(0x1390, breed.ptr());

        link.put<Address>(0x08, parent_tank);
        agg.put<Address>(0x18, vit.ptr());
        vit.put<d::Byte>(0x174, 0);

        chassis.put<Address>(0x10, actor.ptr());
        chassis.put<unsigned>(0x18, 0);
        weaponry.put<d::Byte>(0x70, 0);

        brain.put<Address>(0x00, base + d::human_vt);
        brain.put<Address>(0x38, actor.ptr());
        brain.put<unsigned>(0x398, (ctrl & 3u) | ((move_mode & 3u) << 12));
        // Native seated inhibition: count = 1, flag 19 set!
        brain.put<unsigned>(0x39C, parent_tank ? ((1u << 19) | 1u) : 0u);
    }

    void set_breed_tag(std::string_view tag) {
        breed_tag.ename(0, tag);
        breed.put<Address>(0x440, breed_tag.ptr());
        breed.put<Address>(0x448, breed_tag.ptr() + 0x60);
    }

    void set_mechanic(Address base, float rate) {
        mech_info.put<Address>(0x00, base + d::mechanic_info_vt);
        mech_info.put<unsigned>(0x18, 0);
        mech_info.put<float>(0x1C, rate);
        ability_ptrs[0] = mech_info.ptr();
        breed.put<Address>(0x528 + 0x20, reinterpret_cast<Address>(ability_ptrs.data()));
        breed.put<Address>(0x528 + 0x28, reinterpret_cast<Address>(ability_ptrs.data() + 1));
    }

    void set_medic(Address base) {
        medic_info.put<Address>(0x00, base + d::medic_info_vt);
        ability_ptrs[0] = medic_info.ptr();
        breed.put<Address>(0x528 + 0x20, reinterpret_cast<Address>(ability_ptrs.data()));
        breed.put<Address>(0x528 + 0x28, reinterpret_cast<Address>(ability_ptrs.data() + 1));
    }
};

struct FakeTank {
    Memory<0xF00> actor{};
    Memory<0x400> brain{};
    Memory<0x80> chassis{};
    Memory<0x80> weaponry{};
    Memory<0x400> def{};
    Memory<0x60> def_tag{};
    Memory<0x80> placer{};
    Memory<0xD0> boarder{};
    Memory<0x298 * 8> slots{};
    std::array<short, 8> occ_ids{};
    Memory<0x60> body_name{}, track_name{};
    Memory<0x360> body_def{}, track_def{};
    Memory<0x28 * 2> comp_rows{};
    Memory<0x90 * 2> walkers{};
    Memory<0x40> door_node{};
    Address bone_stub = 0xDEADBEEF;

    void setup(Address base, std::uint32_t id, std::uint16_t route, std::uint8_t player, unsigned ctrl = 1,
               unsigned move_mode = 1) {
        actor.put<d::Byte>(0x30, 0x0B); // entity + actor + vehicle
        actor.put<unsigned>(0x38, 0);
        actor.put<float>(0x70, 1.0f); // upright
        actor.put<Address>(0xB0, actor.ptr() + 0x10);
        actor.put<std::uint32_t>(0xC0, id);
        actor.put<Address>(0x548, def.ptr());
        actor.put<Address>(0x568, placer.ptr());
        actor.put<Address>(0x570, boarder.ptr());
        actor.put<d::Byte>(0xDA8, player);
        actor.put<std::uint16_t>(0xDAA, route);
        actor.put<unsigned>(0xDB4, 0x100u);
        actor.put<Address>(0xDC8, brain.ptr());
        actor.put<Address>(0xDD8, weaponry.ptr());
        actor.put<Address>(0xDE0, chassis.ptr());
        actor.ename(0xDE8, "tank_medium");

        def_tag.ename(0, "tank");
        def.put<Address>(0x330, def_tag.ptr());
        def.put<Address>(0x338, def_tag.ptr() + 0x60);

        chassis.put<Address>(0x10, actor.ptr());
        chassis.put<unsigned>(0x18, 0);
        weaponry.put<d::Byte>(0x70, 0);

        brain.put<Address>(0x00, base + d::vehicle_vt);
        brain.put<Address>(0x38, actor.ptr());
        brain.put<unsigned>(0x398, (ctrl & 3u) | ((move_mode & 3u) << 12));
        brain.put<unsigned>(0x39C, 0);

        body_name.ename(0, "body");
        body_def.put<Address>(0x328, body_name.ptr());
        body_def.put<Address>(0x330, body_name.ptr() + 0x60);
        comp_rows.put<Address>(0x00, body_def.ptr());
        comp_rows.put<Address>(0x08, actor.ptr());
        comp_rows.put<d::Byte>(0x24, 0); // working body

        track_name.ename(0, "trackleft");
        track_def.put<Address>(0x328, track_name.ptr());
        track_def.put<Address>(0x330, track_name.ptr() + 0x60);
        comp_rows.put<Address>(0x28 + 0x00, track_def.ptr());
        comp_rows.put<Address>(0x28 + 0x08, actor.ptr());
        comp_rows.put<d::Byte>(0x28 + 0x24, 0);

        actor.put<Address>(0xB90, comp_rows.ptr());
        actor.put<Address>(0xB98, comp_rows.ptr() + 2 * 0x28);

        placer.put<Address>(0x28, slots.ptr());
        placer.put<Address>(0x30, slots.ptr() + 8 * 0x298);
        placer.put<Address>(0x40, actor.ptr());
        placer.put<d::Byte>(0x48, 0);
        placer.put<d::Byte>(0x49, 1);

        boarder.put<Address>(0x58, actor.ptr());
        door_node.put<short>(0x00, -1);
        door_node.put<int>(0x04, 1); // Door node
    }

    void set_slot_count(std::size_t n) {
        assert(n <= 8);
        placer.put<Address>(0x28, slots.ptr());
        placer.put<Address>(0x30, slots.ptr() + n * 0x298);
    }

    void set_slot(std::size_t idx, std::string_view name, std::string_view group, unsigned flags,
                  std::string_view visor, std::string_view link_bone, Address occupant, std::int32_t occ_id = -1) {
        const std::size_t off = idx * 0x298;
        std::memset(slots.data.data() + off, 0, 0x298);
        slots.ename(off + 0x00, name);
        slots.ename(off + 0x60, group);
        if (!visor.empty()) slots.ename(off + 0xC0, visor);
        if (!link_bone.empty()) {
            slots.ename(off + 0x120, link_bone);
            slots.put<Address>(off + 0x240, reinterpret_cast<Address>(&bone_stub));
        }
        slots.put<unsigned>(off + 0x248, flags);
        slots.put<std::int32_t>(off + 0x250, occupant ? occ_id : -1);
        slots.put<Address>(off + 0x280, occupant);
    }

    void set_slot_flags(std::size_t idx, unsigned flags) {
        slots.put<unsigned>(idx * 0x298 + 0x248, flags);
    }

    void set_slot_reserved(std::size_t idx, Address reserved) {
        slots.put<Address>(idx * 0x298 + 0x290, reserved);
    }

    void set_body_state(d::Byte state) {
        comp_rows.put<d::Byte>(0x24, state);
    }

    void set_exit_walker(Address human) {
        std::memset(walkers.data.data(), 0, 0x90);
        walkers.put<Address>(0x08, human);
        walkers.put<Address>(0x30, door_node.ptr());
        boarder.put<Address>(0x60, walkers.ptr());
        boarder.put<Address>(0x68, walkers.ptr() + 0x90);
    }
    void clear_walkers() {
        boarder.put<Address>(0x60, 0);
        boarder.put<Address>(0x68, 0);
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

    // Tick 2: rifleman (slot 3) -> driver (slot 0), leaving medic on seat3!
    h.world.now += 50;
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.size() == 2);
    assert(h.moves[1].src_place == 3 && h.moves[1].dst_place == 0);
    assert(cf::stat_transfers.load() == 2);
    assert(cf::stat_transfers_infantry.load() == 1);

    // Tick 3: all crew slots full -> no move
    h.world.now += 50;
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
    h.world.now += 50;
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
    h.world.now += 50;
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.size() == 2);
    assert(h.moves[1].src_place == 3 && h.moves[1].dst_place == 0);

    // Free gunner again -> rifle (slot 2, tier 4) beats bazooka (slot 1, tier 5)!
    t.set_slot(0, "gunner", "crew", 0x1000u, "main", "", 0);
    h.world.now += 50;
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.size() == 3);
    assert(h.moves[2].src_place == 2 && h.moves[2].dst_place == 0);

    // Free gunner again -> bazooka (slot 1, tier 5) fills last!
    t.set_slot(0, "gunner", "crew", 0x1000u, "main", "", 0);
    h.world.now += 50;
    cf::on_actor_post_quant(t.actor.ptr());
    assert(h.moves.size() == 4);
    assert(h.moves[3].src_place == 1 && h.moves[3].dst_place == 0);
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
    std::printf("test_tank_crew_fill_26734796: ALL PASSED\n");
    return 0;
}
