// Standalone C++17 test. No game or MinHook dependency.
#include "../../mods/tank_crusher_module.hpp"
#include <cassert>
#include <limits>
#include <thread>

namespace tc = goh::tank_crusher;
using namespace tc::detail;

namespace {
constexpr float nan = std::numeric_limits<float>::quiet_NaN();
constexpr float inf = std::numeric_limits<float>::infinity();

void cost_policy() {
    struct Case {
        const char* name;
        Obstacle type;
        float march, battle;
    };
    for (const auto& c :
         {Case{"stuff", Obstacle::stuff, 2, .5f}, Case{"construction", Obstacle::construction, 4, 1},
          Case{"construction_vaultable", Obstacle::construction, 4, 1},
          Case{"small_building", Obstacle::small_building, 8, 2},
          Case{"small_building_vaultable", Obstacle::small_building, 8, 2}, Case{"tree", Obstacle::tree_post, 12, 3},
          Case{"post", Obstacle::tree_post, 12, 3}, Case{"building", Obstacle::building, 250, 25},
          Case{"building_vaultable", Obstacle::building, 250, 25}}) {
        assert(classify(c.name, 0) == c.type);
        assert(obstacle_cost(250, c.type, false) == c.march);
        assert(obstacle_cost(250, c.type, true) == c.battle);
        for (auto flags : {1u, 2u, 3u, 0x11u})
            assert(classify(c.name, flags) == Obstacle::other);
        for (auto original : {-1.f, 0.f, .25f, 100000.f, 100001.f, inf})
            for (bool battle : {true, false})
                assert(obstacle_cost(original, c.type, battle) == original);
        assert(std::isnan(obstacle_cost(nan, c.type, true)));
    }
    for (auto name : {"fire", "marine", "human", "human_impassable", "car", "tank", "cliff", "unbreakable",
                      "trench_wall", "wire", "stuff_extra", "construction_site", "Tree", ""}) {
        assert(classify(name, 0) == Obstacle::other);
        assert(obstacle_cost(250.f, classify(name, 0), true) == 250.f);
    }
    assert(obstacle_cost(75, Obstacle::building, false) == 75);
}

void reverse_policy() {
    constexpr float scale = .05f;
    const XY hull{2, 0}, self{50, 50};
    assert(frontal(hull, self, {3650, 50}, scale));
    assert(!frontal(hull, self, {3651, 50}, scale));
    assert(frontal(hull, self, {51, 52}, scale));
    assert(!frontal(hull, self, {51, 53}, scale));
    assert(!frontal(hull, self, self, scale));
    assert(!frontal(hull, self, {-50, 50}, scale));
    assert(!frontal(hull, self, {50, 150}, scale));
    assert(!frontal({0, 0}, self, {150, 50}, scale));
    assert(!frontal(hull, self, {nan, 50}, scale));

    // At 30 m a native forward pivot beats reverse. The score discount flips the comparison.
    PathStats back{0, 0, 600, 0, 0};
    const float native_back = 30.f * .19f, native_pivot = 30.f * .15f + 1.f;
    assert(native_pivot < native_back && reverse_estimate(native_back, back, scale) < native_pivot);
    assert(std::abs(reverse_estimate(native_back, back, scale) - 3.3f) < 1e-5f);
    PathStats mixed{0, 200, 700, 1, 1};
    const float native_mixed = 10 * .15f + 35 * .19f + 2 + 2.7f / 6.2831853f;
    assert(std::abs(reverse_estimate(native_mixed, mixed, scale) - (native_mixed - 35 * .08f)) < 1e-5f);
    mixed.forward += 1;
    assert(reverse_estimate(native_mixed, mixed, scale) == native_mixed);
    // Backing around a 90-degree cover corner (~115 deg total turn) still gets reverse bias.
    PathStats corner{0, 0, 400, 0, 2.0f};
    assert(reverse_estimate(4.5f, corner, scale) < 4.5f);
    for (auto path : {PathStats{0, 600, 0, 0, 3.14f}, PathStats{0, 40, 20, 0, 0}, PathStats{0, 0, 600, 0, 3.14f},
                      PathStats{0, -1, 600, 0, 0}, PathStats{0, 0, 600, -1, 0}, PathStats{0, 0, nan, 0, 0}})
        assert(reverse_estimate(8, path, scale) == 8);
    assert(reverse_estimate(8, back, 0) == 8);
    assert(std::isnan(reverse_estimate(nan, back, scale)));
}

// Exercise the actual worker detour with only a copied context, an area and a stock result.
template <class Buffer, class T> void put(Buffer& b, std::size_t off, T value) {
    assert(off + sizeof value <= b.size());
    std::memcpy(b.data() + off, &value, sizeof value);
}
float native_cost;
unsigned calls;
float stock(tc::Address, tc::Address, tc::Address) {
    ++calls;
    return native_cost;
}

void worker_policy() {
    std::array<tc::Byte, 0xF0> ctx{};
    std::array<tc::Byte, 0xF8> profile{};
    std::array<tc::Byte, 0x40> area{};
    const unsigned id = 11, area_id = 20;
    const auto key = actor_key(id, area_id);
    put(ctx, 0x78, area_id);
    put(ctx, 0x7C, id);
    put(ctx, 0xD0, reinterpret_cast<tc::Address>(profile.data()));
    ctx[0] = 1;
    profile[0xF0] = 0x10;
    area[0x3F] = 8;
    obstacles[8] = Obstacle::tree_post;
    orig_policy = stock;
    native_cost = 250;
    auto eval = [&] {
        return Hook_ObjectPolicy(reinterpret_cast<tc::Address>(ctx.data()), 0,
                                 reinterpret_cast<tc::Address>(area.data()));
    };
    active = true;
    actor_slot(key) = key;
    assert(eval() == 250 && calls == 1); // Initial and remote authority.
    host = true;
    std::thread worker([&] { assert(eval() == 12); });
    worker.join();
    ctx[0xE8] = 0x10;
    assert(eval() == 3);
    // Stock light_tank lacks BattleCrusher bit 0x10 but has unique Priority 4 at profile+0xF4.
    profile[0xF0] = 0;
    put(profile, 0xF4, std::int32_t{4});
    assert(eval() == 3);
    // Unrelated profile (e.g. big_vehicle Priority 2) never gets discounts.
    put(profile, 0xF4, std::int32_t{2});
    assert(eval() == 250);
    put(profile, 0xF4, std::int32_t{0});
    profile[0xF0] = 0x10;
    area[0x1F] = 1;
    assert(eval() == 250);
    area[0x1F] = 0;
    const auto old_ctx = ctx;
    ctx[0] = 0;
    assert(eval() == 250);
    ctx = old_ctx;
    profile[0xF0] = 0;
    assert(eval() == 250);
    profile[0xF0] = 0x10;
    native_cost = 100000;
    assert(eval() == 100000);
    native_cost = 0;
    assert(eval() == 0);
    native_cost = 250;
    const auto collision = actor_key(id + unsigned(actors.size()), area_id);
    actor_slot(collision) = collision;
    assert(eval() == 250);
    revoke(key);
    assert(actor_admitted(collision));
    actor_slot(key) = key;
    revoke(key);
    assert(eval() == 250);
    actor_slot(key) = key;
    tc::deactivate();
    assert(eval() == 250 && tc::inflight_count() == 0);
    active = true;
    host = true;
    reset_admission();
    assert(!actor_admitted(key) && eval() == 250);
    assert(ctx == old_ctx && profile[0xF0] == 0x10);
    active = false;
}

void native_names() {
    std::array<tc::Byte, 32> s{};
    std::memcpy(s.data(), "post", 4);
    put(s, 0x10, std::size_t{4});
    put(s, 0x18, std::size_t{15});
    assert(name_at(reinterpret_cast<tc::Address>(s.data())) == "post");
    constexpr char name[] = "small_building_vaultable";
    put(s, 0, reinterpret_cast<tc::Address>(name));
    put(s, 0x10, sizeof name - 1);
    put(s, 0x18, std::size_t{31});
    assert(classify(name_at(reinterpret_cast<tc::Address>(s.data())), 0) == Obstacle::small_building);
    put(s, 0x18, std::size_t{3});
    assert(name_at(reinterpret_cast<tc::Address>(s.data())).empty());
}

void vehicle_and_observation_guards() {
    std::array<tc::Byte, 0xE00> actor{};
    std::array<tc::Byte, 0x400> brain{};
    std::array<tc::Byte, 0x80> chassis{}, weaponry{}, subject{};
    std::array<tc::Byte, 0x300> sensor{};
    auto address = [](auto& b) { return reinterpret_cast<tc::Address>(b.data()); };
    base = 0x140000000;
    actor[0x30] = 2;
    put(actor, 0xDB4, 0x100u);
    put(actor, 0xDC8, address(brain));
    put(actor, 0xDD0, address(sensor));
    put(actor, 0xDD8, address(weaponry));
    put(actor, 0xDE0, address(chassis));
    put(chassis, 0x10, address(actor));
    put(brain, 0, base + 0x101FD68);
    put(brain, 0x38, address(actor));
    put(brain, 0x398, 1u); // Player-owned.
    auto eligible = [&] { return vehicle_brain(address(actor), address(chassis)); };
    assert(eligible() == address(brain));
    // Active player right-click move order (bit 28) and Hold Position (move_mode == 1) MUST remain eligible!
    put(brain, 0x39C, 1u << 28);
    put(brain, 0x398, 1u | (1u << 12));
    assert(eligible() == address(brain));
    // Direct Control / inhibition (low 6 bits of brain+0x39C) must still bail out.
    for (unsigned flags : {1u, 63u, (1u << 28) | 1u}) {
        put(brain, 0x39C, flags);
        assert(!eligible());
    }
    put(brain, 0x39C, 0u);
    put(brain, 0x398, 0u);
    chassis[0x18] = 1;
    assert(!eligible());
    chassis[0x18] = 0;
    weaponry[0x70] = 2;
    assert(!eligible());
    weaponry[0x70] = 0;
    put(brain, 0, base + 0x110FF88);
    assert(!eligible());

    std::array<tc::Address, 1> contacts{address(subject)};
    put(sensor, 0x260, address(actor));
    put(sensor, 0x280, address(contacts));
    put(sensor, 0x288, address(contacts) + sizeof contacts);
    put(subject, 0x10, (1u << 28) | (1u << 29)); // known pos + visible
    put(subject, 0x14, 3u);
    put(subject, 0x18, tc::Address{1}); // Deliberately opaque target link.
    put(subject, 0x28, XY{20, 10});
    put(subject, 0x64, std::uint32_t{1000});
    auto ahead = [&](std::uint32_t now = 1050) {
        return known_enemy(address(actor), now, 10.f, [](XY seen) { return frontal({1, 0}, {0, 0}, seen, .05f); });
    };
    assert(ahead());
    // Unrecognized enemy (bit 0 clear, bit 1 enemy set) also counts immediately.
    put(subject, 0x14, 2u);
    assert(ahead());
    // Unseen contact within 20s is fresh; >20s (200 ticks @ 10/s) is stale.
    put(subject, 0x10, 1u << 28);
    assert(ahead(1150));
    assert(!ahead(1250));
    put(subject, 0x10, (1u << 28) | (1u << 29));
    for (unsigned relation : {0u, 1u, 6u, 7u, 10u, 11u, 18u, 19u}) {
        put(subject, 0x14, relation);
        assert(!ahead());
    }
    put(subject, 0x14, 3u);
    put(subject, 0x10, (1u << 28) | (1u << 16));
    assert(!ahead());
    put(subject, 0x10, (1u << 28) | (1u << 29));
    subject[0x6C] = 1;
    assert(!ahead());
    subject[0x6C] = 0;
    put(subject, 0x18, tc::Address{});
    assert(!ahead());
    base = 0;
}
} // namespace

int main() {
    cost_policy();
    reverse_policy();
    worker_policy();
    native_names();
    vehicle_and_observation_guards();
}
