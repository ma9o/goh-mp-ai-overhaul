#pragma once

#include "../../mods/game_runtime.hpp"
#include <array>
#include <cassert>
#include <vector>

namespace goh::test::vehicle {
using runtime::Address;

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
        actor.put<runtime::Byte>(0x30, 0x07); // entity + actor + human
        actor.put<unsigned>(0x38, 0);
        actor.put<Address>(0xB0, actor.ptr() + 0x10);
        actor.put<std::uint32_t>(0xC0, id);
        actor.put<unsigned>(0x1C0, parent_tank ? (1u << 28) : 0u);
        actor.put<Address>(0x560, link.ptr());
        actor.put<Address>(0x578, agg.ptr());
        actor.put<runtime::Byte>(0xDA8, player);
        actor.put<std::uint16_t>(0xDAA, route);
        actor.put<unsigned>(0xDB4, 0x100u);
        actor.put<Address>(0xDC8, brain.ptr());
        actor.put<Address>(0xDD8, weaponry.ptr());
        actor.put<Address>(0xDE0, chassis.ptr());
        actor.put<Address>(0x1390, breed.ptr());

        link.put<Address>(0x08, parent_tank);
        agg.put<Address>(0x18, vit.ptr());
        vit.put<runtime::Byte>(0x174, 0);

        chassis.put<Address>(0x10, actor.ptr());
        chassis.put<unsigned>(0x18, 0);
        weaponry.put<runtime::Byte>(0x70, 0);

        brain.put<Address>(0x00, base + runtime::table().vt.human_brain);
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
        mech_info.put<Address>(0x00, base + runtime::table().vt.info_mechanic);
        mech_info.put<unsigned>(0x18, 0);
        mech_info.put<float>(0x1C, rate);
        ability_ptrs[0] = mech_info.ptr();
        breed.put<Address>(0x528 + 0x20, reinterpret_cast<Address>(ability_ptrs.data()));
        breed.put<Address>(0x528 + 0x28, reinterpret_cast<Address>(ability_ptrs.data() + 1));
    }

    void set_medic(Address base) {
        medic_info.put<Address>(0x00, base + runtime::table().vt.info_medic);
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
        actor.put<runtime::Byte>(0x30, 0x0B); // entity + actor + vehicle
        actor.put<unsigned>(0x38, 0);
        actor.put<float>(0x70, 1.0f); // upright
        actor.put<Address>(0xB0, actor.ptr() + 0x10);
        actor.put<std::uint32_t>(0xC0, id);
        actor.put<Address>(0x548, def.ptr());
        actor.put<Address>(0x568, placer.ptr());
        actor.put<Address>(0x570, boarder.ptr());
        actor.put<runtime::Byte>(0xDA8, player);
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
        weaponry.put<runtime::Byte>(0x70, 0);

        brain.put<Address>(0x00, base + runtime::table().vt.vehicle_brain);
        brain.put<Address>(0x38, actor.ptr());
        brain.put<unsigned>(0x398, (ctrl & 3u) | ((move_mode & 3u) << 12));
        brain.put<unsigned>(0x39C, 0);

        body_name.ename(0, "body");
        body_def.put<Address>(0x328, body_name.ptr());
        body_def.put<Address>(0x330, body_name.ptr() + 0x60);
        comp_rows.put<Address>(0x00, body_def.ptr());
        comp_rows.put<Address>(0x08, actor.ptr());
        comp_rows.put<runtime::Byte>(0x24, 0); // working body

        track_name.ename(0, "trackleft");
        track_def.put<Address>(0x328, track_name.ptr());
        track_def.put<Address>(0x330, track_name.ptr() + 0x60);
        comp_rows.put<Address>(0x28 + 0x00, track_def.ptr());
        comp_rows.put<Address>(0x28 + 0x08, actor.ptr());
        comp_rows.put<runtime::Byte>(0x28 + 0x24, 0);

        actor.put<Address>(0xB90, comp_rows.ptr());
        actor.put<Address>(0xB98, comp_rows.ptr() + 2 * 0x28);

        placer.put<Address>(0x28, slots.ptr());
        placer.put<Address>(0x30, slots.ptr() + 8 * 0x298);
        placer.put<Address>(0x40, actor.ptr());
        placer.put<runtime::Byte>(0x48, 0);
        placer.put<runtime::Byte>(0x49, 1);

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

    void set_body_state(runtime::Byte state) {
        comp_rows.put<runtime::Byte>(0x24, state);
    }

    void set_wheeled(std::string_view type) {
        actor.ename(0xDE8, type);
        def_tag.ename(0, type);
        body_name.ename(0, "engine");
        track_name.ename(0, "wheelright");
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

} // namespace goh::test::vehicle
