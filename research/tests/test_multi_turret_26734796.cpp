// Offline multi-turret fixtures for build 26734796. Links no game code and installs no hooks.
#include "../../mods/auto_resupply_module.hpp"
#include "../../mods/auto_smoke_module.hpp"
#include "../../mods/tank_auto_repair_module.hpp"
#include "../../mods/target_prioritization_module.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

namespace pr = goh::target_prio::detail;
namespace sm = goh::auto_smoke;
namespace rp = goh::tank_auto_repair::detail;
namespace su = goh::auto_resupply;
using goh::ai_support::Address;
using goh::ai_support::Byte;

template <std::size_t N> struct Mem {
    alignas(16) std::array<std::byte, N> bytes{};
    Address addr() const {
        return reinterpret_cast<Address>(bytes.data());
    }
    template <class T> void put(std::size_t off, const T& value) {
        assert(off + sizeof(T) <= N);
        std::memcpy(bytes.data() + off, &value, sizeof(T));
    }
    void str(std::size_t off, std::string_view text) {
        const auto len = text.size();
        if (len <= 15)
            std::memcpy(bytes.data() + off, text.data(), text.size());
        else
            put(off, reinterpret_cast<Address>(text.data()));
        put(off + 0x10, len);
        put(off + 0x18, std::max<std::size_t>(15, len));
    }
    void tag_row(std::size_t off, std::string_view text) {
        str(off + 0x40, text);
    }
    template <class T, std::size_t M> void range(std::size_t off, const std::array<T, M>& arr, std::size_t count = M) {
        assert(count <= M);
        put(off, reinterpret_cast<Address>(arr.data()));
        put(off + 8, reinterpret_cast<Address>(arr.data() + count));
    }
};

void test_target_prio_rows_and_masks() {
    // Build T-35 weapon definitions: 76mm main, 45mm gun1, 45mm gun2, DT mgun, plus a 37mm AA autocannon.
    Mem<0x1700> def76, def45, defMg, defAuto;
    Mem<0x60> tagMg;
    tagMg.tag_row(0, "mgun");
    def76.put(0x1628, 76.2f);
    def45.put(0x1628, 45.0f);
    defAuto.put(0x1628, 37.0f);
    defMg.put(0x11E0, tagMg.addr());
    defMg.put(0x11E8, tagMg.addr() + 0x60);

    Mem<0x1300> ammoShell, ammoAuto;
    Mem<0x60> tagShell;
    tagShell.tag_row(0, "shell");
    ammoShell.put(0x11E0, tagShell.addr());
    ammoShell.put(0x11E8, tagShell.addr() + 0x60);
    Mem<0xC0> tagAutoShell;
    tagAutoShell.tag_row(0, "shell");
    tagAutoShell.tag_row(0x60, "auto");
    ammoAuto.put(0x11E0, tagAutoShell.addr());
    ammoAuto.put(0x11E8, tagAutoShell.addr() + 0xC0);

    Mem<0x220> w76, w45a, w45b, wMg, wAuto;
    w76.put(0x98, def76.addr());
    w45a.put(0x98, def45.addr());
    w45b.put(0x98, def45.addr());
    wMg.put(0x98, defMg.addr());
    wAuto.put(0x98, defAuto.addr());

    assert(pr::row_kind(w76.addr()) == pr::RowKind::main_gun);
    assert(pr::row_kind(w45a.addr()) == pr::RowKind::main_gun);
    assert(pr::row_kind(wMg.addr()) == pr::RowKind::mg);
    assert(pr::row_kind(wAuto.addr()) == pr::RowKind::main_gun);

    // Populate brain+0x3B8 with 5 rows (76mm, 45mm x2, MG, 37mm auto).
    Mem<5 * pr::row_size> rows;
    auto init_row = [&](std::size_t idx, Address weapon, Address ammo) {
        const std::size_t off = idx * pr::row_size;
        rows.put(off + 8, weapon);
        rows.put(off + 0x98, ammo);
    };
    init_row(0, w76.addr(), ammoShell.addr());
    init_row(1, w45a.addr(), ammoShell.addr());
    init_row(2, w45b.addr(), ammoShell.addr());
    init_row(3, wMg.addr(), Address{0});
    init_row(4, wAuto.addr(), ammoAuto.addr());

    Mem<0x430> brain;
    brain.put(0x3B8, rows.addr());
    brain.put(0x3C0, rows.addr() + 5 * pr::row_size);

    pr::Assignment a;
    a.brain = brain.addr();
    pr::capture_rows(a);
    assert(a.rows.size() == 5);
    assert(a.rows[0].kind == pr::RowKind::main_gun);
    assert(a.rows[1].kind == pr::RowKind::main_gun);
    assert(a.rows[2].kind == pr::RowKind::main_gun);
    assert(a.rows[3].kind == pr::RowKind::mg);
    assert(a.rows[4].kind == pr::RowKind::other); // autocannon excluded!
    assert(a.economy);
    assert(a.multi_cannon);
    assert(pr::has_secondary_row(brain.addr(), w76.addr()));

    // Single-weapon brain has no secondary row.
    brain.put(0x3C0, rows.addr() + pr::row_size);
    assert(!pr::has_secondary_row(brain.addr(), w76.addr()));

    // Verify Pass::covered masks ALL three shell cannons (76mm + both 45mm AP turrets) while leaving MG open.
    std::vector<pr::PartitionEntry> part;
    part.push_back({a.rows[0].row, 0, false, a.rows[0].kind});
    part.push_back({a.rows[1].row, 0, true, a.rows[1].kind});
    part.push_back({a.rows[2].row, 0, true, a.rows[2].kind});
    part.push_back({a.rows[3].row, 0, false, a.rows[3].kind});
    {
        pr::RowMask mask(part, pr::Pass::covered);
        assert(pr::read<Byte>(a.rows[0].row) == 1);
        assert(pr::read<Byte>(a.rows[1].row) == 1);
        assert(pr::read<Byte>(a.rows[2].row) == 1);
        assert(pr::read<Byte>(a.rows[3].row) == 0);
    }
    assert(pr::read<Byte>(a.rows[0].row) == 0 && pr::read<Byte>(a.rows[1].row) == 0);

    // Verify Pass::cannon_prefer hides ONLY non-preferred cannons (76mm) while leaving 45mm AP turrets & MG unmasked.
    {
        pr::RowMask mask(part, pr::Pass::cannon_prefer);
        assert(pr::read<Byte>(a.rows[0].row) == 1);
        assert(pr::read<Byte>(a.rows[1].row) == 0);
        assert(pr::read<Byte>(a.rows[2].row) == 0);
        assert(pr::read<Byte>(a.rows[3].row) == 0);
    }
    // Verify Pass::cannon_fallback hides preferred cannons & MGs, exposing the remaining cannon (76mm).
    {
        pr::RowMask mask(part, pr::Pass::cannon_fallback);
        assert(pr::read<Byte>(a.rows[0].row) == 0);
        assert(pr::read<Byte>(a.rows[1].row) == 1);
        assert(pr::read<Byte>(a.rows[2].row) == 1);
        assert(pr::read<Byte>(a.rows[3].row) == 1);
    }
    assert(pr::read<Byte>(a.rows[0].row) == 0 && pr::read<Byte>(a.rows[3].row) == 0);
}

void test_multi_cannon_role_selection() {
    bool pref[4]{};

    // T-35 vs tank: 76mm KT-26 (19mm pen), 45mm #1 (45mm pen), 45mm #2 (45mm pen), MG (-1)
    {
        const float m[] = {19.f, 45.f, 45.f, -1.f};
        assert(pr::select_preferred_cannons(m, 4, true, pref));
        assert(!pref[0] && pref[1] && pref[2] && !pref[3]);
    }
    // T-35 vs infantry cluster: 76mm HE (6.5m), 45mm #1 (2.5m), 45mm #2 (2.5m), MG (-1)
    {
        const float m[] = {6.5f, 2.5f, 2.5f, -1.f};
        assert(pr::select_preferred_cannons(m, 4, false, pref));
        assert(pref[0] && !pref[1] && !pref[2] && !pref[3]);
    }
    // M3 Lee / Grant vs tank: 37mm M5 (50mm pen), 75mm M2 (66mm pen)
    {
        const float m[] = {50.f, 66.f, -1.f};
        assert(pr::select_preferred_cannons(m, 3, true, pref));
        assert(!pref[0] && pref[1]);
    }
    // M3 Lee / Grant vs infantry cluster: 37mm HE (2.0m), 75mm HE (6.2m)
    {
        const float m[] = {2.0f, 6.2f, -1.f};
        assert(pr::select_preferred_cannons(m, 3, false, pref));
        assert(!pref[0] && pref[1]);
    }
    // Panzer B2 vs tank: 47mm AP (40mm pen), 75mm hull (19mm pen)
    {
        const float m[] = {40.f, 19.f};
        assert(pr::select_preferred_cannons(m, 2, true, pref));
        assert(pref[0] && !pref[1]);
    }
    // Panzer B2 vs infantry cluster: 47mm HE (2.8m), 75mm HE (6.0m)
    {
        const float m[] = {2.8f, 6.0f};
        assert(pr::select_preferred_cannons(m, 2, false, pref));
        assert(!pref[0] && pref[1]);
    }
    // SMK vs tank: 76mm L-11 (53mm pen), 45mm 20-K (35mm pen)
    {
        const float m[] = {53.f, 35.f};
        assert(pr::select_preferred_cannons(m, 2, true, pref));
        assert(pref[0] && !pref[1]);
    }
    // Only 1 remaining cannon -> no split
    {
        const float m[] = {-1.f, 45.f, -1.f};
        assert(!pr::select_preferred_cannons(m, 3, true, pref));
    }
    // Tied cannons (twin 45mm turrets left) -> no split
    {
        const float m[] = {-1.f, 45.f, 45.f};
        assert(!pr::select_preferred_cannons(m, 3, true, pref));
    }
}

void test_auto_smoke_gun1_gun2() {
    Mem<0xE00> tank;
    Mem<0x400> defBody, defGun, defGun1;
    Mem<0x60> tagBody, tagGun, tagGun1;
    tagBody.tag_row(0, "body");
    tagGun.tag_row(0, "gun");
    tagGun1.tag_row(0, "gun1");
    defBody.put(0x328, tagBody.addr());
    defBody.put(0x330, tagBody.addr() + 0x60);
    defGun.put(0x328, tagGun.addr());
    defGun.put(0x330, tagGun.addr() + 0x60);
    defGun1.put(0x328, tagGun1.addr());
    defGun1.put(0x330, tagGun1.addr() + 0x60);

    Mem<3 * 0x28> comps;
    auto set_comp = [&](std::size_t idx, Address def, float hp, float max_hp, Byte st) {
        const std::size_t o = idx * 0x28;
        comps.put(o + 0, def);
        comps.put(o + 8, tank.addr());
        comps.put(o + 0x14, hp);
        comps.put(o + 0x1C, max_hp);
        comps.put(o + 0x24, st);
    };
    set_comp(0, defBody.addr(), 100.f, 100.f, 0);
    set_comp(1, defGun.addr(), 100.f, 100.f, 0);
    set_comp(2, defGun1.addr(), 100.f, 100.f, 0);
    tank.put(0xB90, comps.addr());
    tank.put(0xB98, comps.addr() + 3 * 0x28);

    sm::vehicle::Observation obs{};
    assert(sm::vehicle::usableBody(tank.addr(), obs));
    assert(!obs.reactiveDamage);

    // Grant / Churchill Mk I loses 75mm / 76mm gun1 while 37mm/40mm gun is intact -> must trigger reactive smoke!
    set_comp(2, defGun1.addr(), 0.f, 100.f, 1);
    obs = {};
    assert(sm::vehicle::usableBody(tank.addr(), obs));
    assert(obs.reactiveDamage);

    // T-35 gun2 damaged -> must also trigger reactive smoke.
    tagGun1.tag_row(0, "gun2");
    obs = {};
    assert(sm::vehicle::usableBody(tank.addr(), obs));
    assert(obs.reactiveDamage);
}

void test_tank_auto_repair_multi_turret() {
    assert(rp::gun("gun") && rp::gun("gun1") && rp::gun("gun2") && !rp::gun("gun7"));
    assert(rp::turret("turret") && rp::turret("aux_turret1") && rp::turret("aux_turret4") &&
           !rp::turret("aux_turret5") && !rp::turret("engine"));
    assert(rp::tier(rp::Scenario::turret_track, "gun1", 1) == 0);
    assert(rp::tier(rp::Scenario::turret_track, "aux_turret1", 1) == 1);
    assert(rp::tier(rp::Scenario::turret_track, "mantlet", 1) == 2);
    assert(rp::tier(rp::Scenario::turret_track, "engine", 1) == 3);
    assert(rp::tier(rp::Scenario::turret_track, "trackleft", 1) == 4);

    // Cannon gunner seat protection: M3 Lee (gunner=37mm, gunner3=75mm) vs ordinary tank (gunner3=bow MG).
    Mem<0xE00> lee, sherman;
    Mem<0x80> wpnryLee, wpnrySherman;
    Mem<0x220> w37, w75, wBowMg;
    Mem<0x1700> d37, d75, dMg;
    Mem<0x60> tMg;
    tMg.tag_row(0, "mgun");
    d37.put(0x1628, 37.f);
    d75.put(0x1628, 75.f);
    dMg.put(0x1628, 7.62f);
    dMg.put(0x11E0, tMg.addr());
    dMg.put(0x11E8, tMg.addr() + 0x60);

    Mem<0x640> p37, p75, pBowMg;
    Mem<0x60> sGunner, sGunner3;
    sGunner.tag_row(0, "gunner");
    sGunner3.tag_row(0, "gunner3");

    p37.put(0x10, lee.addr());
    p37.put(0x5E0, sGunner.addr());
    p37.put(0x5E8, sGunner.addr() + 0x60);
    p75.put(0x10, lee.addr());
    p75.put(0x5E0, sGunner3.addr());
    p75.put(0x5E8, sGunner3.addr() + 0x60);

    w37.put(0x90, lee.addr());
    w37.put(0x98, d37.addr());
    w37.put(0xC0, p37.addr());
    w75.put(0x90, lee.addr());
    w75.put(0x98, d75.addr());
    w75.put(0xC0, p75.addr());
    std::array<Address, 2> leeWeapons{w37.addr(), w75.addr()};
    wpnryLee.range(0x30, leeWeapons);
    lee.put(0xDD8, wpnryLee.addr());

    assert(rp::cannon_gunner_seat(lee.addr(), "gunner"));
    assert(rp::cannon_gunner_seat(lee.addr(), "gunner3")); // 75mm main AP gunner protected!
    assert(!rp::cannon_gunner_seat(lee.addr(), "driver"));
    assert(!rp::cannon_gunner_seat(lee.addr(), "charger2"));

    pBowMg.put(0x10, sherman.addr());
    pBowMg.put(0x5E0, sGunner3.addr());
    pBowMg.put(0x5E8, sGunner3.addr() + 0x60);
    wBowMg.put(0x90, sherman.addr());
    wBowMg.put(0x98, dMg.addr());
    wBowMg.put(0xC0, pBowMg.addr());
    std::array<Address, 1> shermanWeapons{wBowMg.addr()};
    wpnrySherman.range(0x30, shermanWeapons);
    sherman.put(0xDD8, wpnrySherman.addr());

    assert(rp::cannon_gunner_seat(sherman.addr(), "gunner"));
    assert(!rp::cannon_gunner_seat(sherman.addr(), "gunner3")); // bow MG gunner NOT protected!

    // Rocket rack (tag "mrl", e.g. Calliope / Tulip commander) is NOT treated as a cannon turret gunner.
    Mem<0x60> tMrl;
    tMrl.tag_row(0, "mrl");
    d75.put(0x11E0, tMrl.addr());
    d75.put(0x11E8, tMrl.addr() + 0x60);
    assert(!rp::cannon_gunner_seat(lee.addr(), "gunner3"));
}

void test_auto_resupply_multi_cannon() {
    // T-35 twin 45 mm turrets share one weapon def: keep loaded chamber if either turret still holds a shell.
    // Live chamber count lives at eItem+0xDC (stock 0x7DC450); eItem+0x58 is fill capacity (stays 1 when empty).
    Mem<0xE8> emptyChamber, loadedChamber;
    emptyChamber.put(0x50, Address{0x1234});
    emptyChamber.put(0x58, 1);
    emptyChamber.put(0xDC, 0);
    loadedChamber.put(0x50, Address{0x1234});
    loadedChamber.put(0x58, 1);
    loadedChamber.put(0xDC, 1);

    std::array<su::VehicleGun, 16> found{};
    std::size_t count = 0;
    const Address def76 = 0x7600, def45 = 0x4500, defMg = 0x0762;
    su::merge_vehicle_gun(found, count, {def76, loadedChamber.addr(), 0x100, 76.2f, false}, true);
    su::merge_vehicle_gun(found, count, {def45, emptyChamber.addr(), 0x200, 45.f, false}, true);
    su::merge_vehicle_gun(found, count, {def45, loadedChamber.addr(), 0x300, 45.f, false}, true);
    su::merge_vehicle_gun(found, count, {defMg, loadedChamber.addr(), 0x400, 7.62f, false}, false);
    assert(count == 3);
    assert(found[1].gun.def == def45);
    assert(found[1].gun.item == loadedChamber.addr());
    assert(found[1].gun.place == 0x300);

    // T-35 / Churchill Mk I / Panzer B2: 76 mm HE has ammo, 45 mm AP is dry -> select dry 45 mm AP!
    {
        const bool empty[] = {false, true, false};
        auto picked = su::select_vehicle_gun(found.data(), empty, count);
        assert(picked.def == def45);
    }
    // Both cannons dry -> pick highest-calibre dry cannon (76 mm).
    {
        const bool empty[] = {true, true, false};
        auto picked = su::select_vehicle_gun(found.data(), empty, count);
        assert(picked.def == def76);
    }
    // Only MG dry while cannons have ammo -> stay on cannon (do not pull tank off front for MG ammo).
    {
        const bool empty[] = {false, false, true};
        auto picked = su::select_vehicle_gun(found.data(), empty, count);
        assert(picked.def == def76);
    }
    // Active trip tracking: wanted_def selects the exact tracked dry cannon until refilled.
    {
        auto tracked = su::select_vehicle_gun(found.data(), nullptr, count, def45);
        assert(tracked.def == def45 && tracked.place == 0x300);
        auto missing = su::select_vehicle_gun(found.data(), nullptr, count, 0x9999);
        assert(missing.def == 0);
    }
    // MG-only vehicle (e.g. T-26-31 twin-MG turret) falls back to dry MG.
    {
        std::array<su::VehicleGun, 16> mgOnly{};
        std::size_t mgCount = 0;
        su::merge_vehicle_gun(mgOnly, mgCount, {defMg, emptyChamber.addr(), 0x500, 7.62f, false}, false);
        const bool empty[] = {true};
        auto picked = su::select_vehicle_gun(mgOnly.data(), empty, mgCount);
        assert(picked.def == defMg);
    }
}

int main() {
    test_target_prio_rows_and_masks();
    test_multi_cannon_role_selection();
    test_auto_smoke_gun1_gun2();
    test_tank_auto_repair_multi_turret();
    test_auto_resupply_multi_cannon();
    std::puts("Multi-turret offline fixtures passed");
    return 0;
}
