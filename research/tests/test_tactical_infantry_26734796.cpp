// Native-host policy fixtures. The optional adapter fixtures use in-memory MinHook doubles.
#include "../../mods/tactical_infantry_policy.hpp"
#if defined(_WIN32) || defined(GOH_TACTICAL_ADAPTER_FIXTURES)
#include "../../mods/tactical_infantry_module.hpp"
#include "vehicle_fixture_26734796.hpp"
#endif
#include <cassert>
#include <cstdio>

namespace p = goh::tactical_infantry::policy;

void posture() {
    p::Balance b{101, 100, 51, 1, 1};
    for (bool cover : {false, true}) {
        p::Soldier s;
        s.observe_cover(cover, 0);
        auto expected = cover ? p::hold : p::fallback;
        assert(p::decide(s, b, p::flee, 0) == expected);
        for (int tick = 1; tick < 20; ++tick)
            assert(p::decide(s, b, p::advance, tick) < p::advance);
    }
    p::Soldier s;
    b = {170, 100, 60, 1, 1};
    s.observe_cover(true, 0);
    assert(p::decide(s, b, p::hold, 0) == p::hold);
    s.observe_cover(false, 8);
    assert(p::decide(s, b, p::hold, 8) == p::fallback);
    assert(p::decide(s, b, p::hold, 12) == p::fallback);
    assert(p::decide(s, b, p::hold, 13) == p::flee);
    b.a = 80;
    b.morale = 54;
    assert(p::decide(s, b, p::advance, 20) == p::flee);
    b.morale = 55;
    assert(p::decide(s, b, p::advance, 21) == p::hold);
    b.b = 160;
    b.morale = 75;
    assert(p::decide(s, b, p::advance, 25) == p::hold);
    assert(p::decide(s, b, p::hold, 26) == p::advance);
    b.b = 200;
    assert(p::decide(s, b, p::hold, 31) == p::assault);
    b.no_advance = true;
    assert(p::decide(s, b, p::assault, 32) == p::hold);
    b.a = 999;
    b.no_retreat = true;
    assert(p::decide(s, b, p::hold, 40) == p::hold);
    b.a = std::numeric_limits<float>::quiet_NaN();
    assert(!b.valid());
    p::Soldier cliff;
    b = {140, 100, 60, 1, 1};
    cliff.observe_cover(true, 0);
    assert(p::decide(cliff, b, p::hold, 0) == p::hold);
    cliff.observe_cover(false, 6);
    assert(p::decide(cliff, b, p::hold, 9.99) == p::hold);
    assert(p::decide(cliff, b, p::hold, 10) == p::fallback);
}
void emergency_and_squad_posture() {
    for (bool low_morale : {false, true}) {
        p::Soldier s;
        p::Balance b{100, 150, 75, 1, 1};
        assert(p::decide(s, b, p::hold, 10) == p::advance);
        b.a = low_morale ? 100.f : 300.f;
        b.morale = low_morale ? 30.f : 75.f;
        assert(p::decide(s, b, p::advance, 11) == p::advance);
        if (low_morale)
            b.morale = 29;
        else
            b.a = 301;
        b.no_retreat = true;
        assert(p::decide(s, b, p::advance, 12) == p::advance);
        b.no_retreat = false;
        assert(p::decide(s, b, p::advance, 12) == p::flee);
        assert(s.changed == 12);
    }
    for (auto mode : {p::Mode::bound, p::Mode::left, p::Mode::right}) {
        assert(p::sync_squad_posture(p::hold, mode, true, false, false, false) == p::advance);
        assert(p::sync_squad_posture(p::assault, mode, true, false, false, false) == p::assault);
        assert(p::sync_squad_posture(p::advance, mode, false, false, false, false) == p::hold);
        assert(p::sync_squad_posture(p::fallback, mode, false, true, false, false) == p::advance);
        assert(p::sync_squad_posture(p::assault, mode, true, true, true, false) == p::hold);
    }
    assert(p::sync_squad_posture(p::assault, p::Mode::hold, true, false, false, false) == p::hold);
    assert(p::sync_squad_posture(p::hold, p::Mode::hold, false, true, false, false) == p::advance);
    assert(p::sync_squad_posture(p::advance, p::Mode::hold, false, true, true, false) == p::hold);
    assert(p::sync_squad_posture(p::assault, p::Mode::fallback, true, true, false, false) == p::fallback);
    assert(p::sync_squad_posture(p::flee, p::Mode::fallback, false, false, false, false) == p::flee);
    assert(p::sync_squad_posture(p::advance, p::Mode::fallback, false, false, false, true) == p::advance);
}
void elements() {
    using R = p::Role;
    assert(p::classify(true, false, true, true) == R::heavy);
    assert(p::classify(false, true, false, false) == R::heavy);
    assert(p::classify(false, false, false, true) == R::assault);
    assert(p::classify(false, false, true, false) == R::assault);
    assert(p::classify(false, false, false, false) == R::rifle);
    p::Roster roster{};
    for (unsigned i = 0; i < 12; ++i)
        roster[i] = {i + 1, i, i == 0 ? R::heavy : (i == 1 ? R::assault : R::rifle), {}};
    p::Elements ever{};
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        auto a = p::bounders(roster, 12, 0, cycle), b = p::bounders(roster, 12, 1, cycle);
        assert(!a[0] && !b[0]);
        assert(std::count(a.begin(), a.end(), true) == 4 && std::count(b.begin(), b.end(), true) == 4);
        for (unsigned i = 1; i < 12; ++i) {
            assert(!(a[i] && b[i]));
            ever[i] = ever[i] || a[i] || b[i];
        }
    }
    assert(std::count(ever.begin(), ever.end(), true) == 11);
    for (unsigned phase : {0u, 1u})
        assert(p::bounders(roster, 2, phase)[1]);
    p::Team team;
    team.update(0, p::Mode::left, 5, 2, false);
    assert(team.mode == p::Mode::left);
    team.update(3, p::Mode::left, 0, 20, false);
    assert(team.mode == p::Mode::left);
    team.update(6, p::Mode::left, 2, 7, false);
    assert(team.mode == p::Mode::left);
    team.update(7, p::Mode::left, 2, 7, false);
    assert(team.phase == 1);
    team.update(8, p::Mode::left, 2, 7, true);
    team.update(11.49, p::Mode::left, 2, 7, true);
    assert(team.phase == 1);
    team.update(11.5, p::Mode::left, 2, 7, true);
    assert(team.phase == 0 && team.cycle == 1);
    team.update(12, p::Mode::left, 0, 20, false);
    assert(team.mode == p::Mode::right);
    team.update(13, p::Mode::fallback, 0, 0, false);
    assert(team.mode == p::Mode::right);
    team.update(18, p::Mode::fallback, 0, 0, false);
    assert(team.mode == p::Mode::fallback);
    auto axis = p::axis(p::Mode::left, {12, 0});
    assert(axis.x > .70 && axis.x < .71 && axis.y == axis.x);
    assert(p::axis(p::Mode::right, {12, 0}).y == -axis.y);
    assert(p::cohesive({0, 0}, {18, 0}, {0, 0}, {0, 0}, 1));
    assert(!p::cohesive({0, 0}, {19, 0}, {0, 0}, {0, 0}, 1));
    assert(!p::cohesive({25, 0}, {29, 0}, {0, 0}, {20, 0}, 1));
    assert(p::cohesive({40, 0}, {30, 0}, {0, 0}, {0, 0}, 1));
}
void spacing_and_movement() {
    p::Claims claims;
    claims.claim({1, 10, 5, 8, {0, 0}, 12}, 0);
    assert(claims.nearest(1, 5, 11, {2, 0}, 11) == 4);
    assert(std::isinf(claims.nearest(1, 5, 10, {2, 0}, 11)));
    assert(std::isinf(claims.nearest(1, 5, 11, {2, 0}, 12)));
    assert(std::isinf(claims.nearest(2, 5, 11, {2, 0}, 11)));
    assert(std::isinf(claims.nearest(1, 6, 11, {2, 0}, 11)));
    claims.claim({1, 10, 5, 8, {9, 0}, 20}, 8);
    assert(claims.nearest(1, 5, 11, {9, 0}, 9) == 0);
    const auto forward = p::bound_range(false), back = p::bound_range(true);
    assert(forward.contains(5) && forward.contains(18) && !forward.contains(4.9f) && !forward.contains(18.1f));
    assert(back.contains(8) && back.contains(20) && !back.contains(7.9f) && !back.contains(20.1f));
    assert(p::point_move_action(false, true, false, true) == 8);
    assert(p::point_move_action(false, true, true, false) == 0);
    assert(p::point_move_action(false, false, false, false) == 7);
    assert(p::point_move_action(false, false, false, true) == 0);
    assert(p::point_move_action(true, true, true, true) == 6);
    assert(p::bound_arrived({50, 0}, {}, 20, false, false));
    assert(!p::bound_arrived({50.1f, 0}, {}, 20, false, true));
    assert(p::bound_arrived({40, 0}, {}, 20, true, true));
    assert(!p::bound_arrived({40, 0}, {}, 20, true, false));
    assert(!p::bound_arrived({40.1f, 0}, {}, 20, true, true));
    using V = p::Sight;
    assert(p::cover_score(10, V::unknown, true, 0, 4, true) == 18);
    assert(p::cover_score(10, V::open, false, 0, 4, true) == 30);
    assert(p::cover_score(10, V::blocked, true, 0, 4, true) == 30);
    assert(p::cover_score(40, V::blocked, false, 0, 4, false) > p::cover_score(10, V::open, false, 0, 4, true));
    assert(p::cover_height(2, 3, false) == 1.35f && p::cover_height(2, 3, true) == .9f);
    assert(p::cover_height(3, 0, false) == 0 && p::cover_height(3, 0, true) == 1.35f);
    assert(p::cover_height(2, 7, false) == 0 && p::cover_height(1, 1, false) == .35f);
    p::CoverSelection ranked;
    unsigned calls = 0;
    for (unsigned i = 0; i < p::CoverSelection::limit; ++i) {
        auto sight = ranked.probe([&] {
            ++calls;
            return V::open;
        });
        assert(sight == (i < 6 ? V::open : V::unknown));
        ranked.consider(i, i == 3 || i == 4 ? 20.f : 10.f);
    }
    ranked.consider(1, std::numeric_limits<float>::quiet_NaN());
    ranked.consider(p::CoverSelection::limit, 30);
    assert(calls == 6 && ranked.winner == 3 && ranked.score == 20);
    assert(p::cover_score(10, V::open, false, 1, 3) == 28);
    assert(p::cover_score(10, V::open, false, 1, 2.5) == 13);
    assert(p::cover_score(10, V::unknown, true, 0, 4) == 10);
    assert(p::cover_score(10, V::blocked, true, 0, 4) == 22);
    assert(!p::replan({40, 0}, {0, 0}, {1, 1}, 20, false));
    assert(p::replan({41, 0}, {0, 0}, {1, 1}, 20, false));
    assert(!p::replan({120, 0}, {0, 0}, {1, 1}, 20, true));
    assert(p::replan({121, 0}, {0, 0}, {1, 1}, 20, true));
    assert(!p::replan({121, 0}, {0, 0}, {200, 0}, 20, true));
    p::XY away = p::threat_term({-40, 0}, 1, 0, false, false, 20);
    assert(away.x < 0 && away.y == 0);
    assert(p::threat_term({-40, 0}, 1, 0, true, false, 20).x < away.x);
    assert(p::threat_term({-40, 0}, 1, 2, false, false, 20).x > away.x);
    assert(p::threat_term({-40, 0}, 1, 15, false, false, 20).x == 0);
    for (p::XY ally : {p::XY{1, 0}, {0, 1}, {0, 0}}) {
        auto v = p::withdraw(away, ally);
        assert(v.x == -1 && v.y == 0);
    }
    auto v = p::withdraw(away, {-1, 1});
    assert(v.x < -.9f && v.y > 0 && v.y < .25f);
    assert(p::fire_first(78, 100, false, true, true, false, false));
    assert(!p::fire_first(78, 100, true, true, true, false, false));
    assert(p::fire_first(64, 100, true, true, false, true, false));
    assert(!p::fire_first(1, 100, false, false, true, true, true));
    assert(!p::fire_first(1, 100, false, true, false, false, false));
    assert(p::fire_first(1, 100, false, true, false, false, true));
    assert(!p::fire_first(20, 100, false, true, true, true, true, false)); // ineffective rifle vs armor never freezes
    assert(p::at_rush_allowed(75.f, false) && p::at_rush_allowed(30.f, false));
    assert(!p::at_rush_allowed(29.9f, false) && !p::at_rush_allowed(80.f, true));
    // Stand-off stays at ~0.75 * throw_range, strictly outside effect_radius + 2.5m and inside max_range - 1.5m.
    assert(std::abs(p::at_rush_standoff(320.f, 90.f, 20.f) - 240.f) < .01f);  // 16m AT grenade -> 12m
    assert(std::abs(p::at_rush_standoff(160.f, 50.f, 20.f) - 120.f) < .01f);  // 8m Molotov -> 6m
    assert(std::abs(p::at_rush_standoff(320.f, 120.f, 20.f) - 240.f) < .01f); // 16m / 6m satchel -> 12m
    assert(p::at_rush_standoff(124.f, 240.f, 20.f) == 0.f);                   // dynamite blast > range rejected
    auto rot25 = p::rotate({1.f, 0.f}, 25.f);
    assert(rot25.x > .90f && rot25.x < .91f && rot25.y > .42f && rot25.y < .43f);
    const auto no_shot = [] { return false; };
    const auto can_shoot = [] { return true; };
    assert(p::ineffective_posture(p::advance, false, false, 0, 28.f, no_shot) == p::fallback);
    assert(p::ineffective_posture(p::advance, false, false, 0, 28.1f, no_shot) == p::advance);
    for (unsigned threat : {0x80u, 0x100u}) {
        assert(p::ineffective_posture(p::hold, false, false, threat, 60.f, no_shot) == p::fallback);
        assert(p::ineffective_posture(p::hold, true, false, threat, 15.f, no_shot) == p::hold);
        assert(p::ineffective_posture(p::advance, false, true, threat, 15.f, no_shot) == p::advance);
        assert(p::ineffective_posture(p::hold, false, false, threat, 15.f, can_shoot) == p::hold);
    }
    assert(p::deadline(10, 50) == 60 && p::deadline(UINT32_MAX - 2, 50) == 1);
    assert(p::dwell(p::flee, false, false) == 2.5 && p::dwell(p::flee, false, true) == 5);
    assert(p::dwell(p::flee, true, true) == 4.5 && p::dwell(p::flee, true, false) == 2);
    assert(p::dwell(p::fallback, false, false) == 3 && p::dwell(p::fallback, true, true) == 5);
    assert(p::dwell(p::advance, false, false) == 4 && p::dwell(p::assault, true, false) == 5.5);
}
void cover_and_authority() {
    p::Soldier s;
    s.observe_cover(true, 10);
    assert(p::stable_cover(18.9, s, true, true, false));
    assert(!p::stable_cover(19, s, true, true, false));
    assert(!p::stable_cover(12, s, false, true, true));
    assert(!p::stable_cover(12, s, false, false, false));
    assert(p::rise(1, false, true, true) == 2 && p::rise(2, false, true, true) == 0);
    assert(p::rise(1, true, true, true) == -1 && p::rise(0, false, false, true) == -1);
    float clear = p::peek_score(true, true, .1, 4, 1, 2.5);
    assert(clear > p::peek_score(true, true, .5, 4, 0, 5));
    assert(clear > p::peek_score(true, true, .1, 2, 1, 2.5));
    assert(!std::isfinite(p::peek_score(false, true, 0, 9, 1, 1)));
    assert(!std::isfinite(p::peek_score(true, false, 0, 9, 1, 1)));
    assert(p::hold_anchor(0, 1u << 11, true, false));
    assert(p::hold_anchor(0, (1u << 11) | (1u << 18), false, false));
    assert(p::hold_anchor(0, 1u << 11, false, true));
    assert(!p::hold_anchor(1, 1u << 11, true, true));
    assert(!p::hold_anchor(0, 1u << 11, false, false));
    assert(!p::hold_anchor(0, 0, true, true));
    for (unsigned mode : {0u, 1u})
        assert(p::allows(mode, 0));
    for (unsigned flag : {1u, 63u, 1u << 6, 1u << 20, 1u << 28})
        assert(!p::allows(1, flag));
    for (unsigned mode : {2u, 1u << 12, 2u << 12, 1u << 10, 2u << 10})
        assert(!p::allows(mode, 0));
    assert(p::allows(1 | (2u << 10), 1u << 18));
    assert(!p::allows(1, 0, true, false, false));
    assert(!p::allows(1, 0, false, true, false));
    assert(!p::allows(1, 0, false, false, true));
    assert(p::allows(0, 1u << 28));
    assert(!p::allows(0, (1u << 28) | (1u << 20)));
}
#if defined(_WIN32) || defined(GOH_TACTICAL_ADAPTER_FIXTURES)
bool fixture_selected{};
goh::ai_support::Byte selection_fixture(goh::ai_support::Address) {
    return fixture_selected;
}
void defensive_line() {
    namespace d = goh::tactical_infantry::detail;
    using goh::runtime::Address;
    using goh::test::vehicle::Memory;
    Memory<0xe60> defender, armor, enemy;
    Memory<0x580> brain;
    Memory<0x300> sensor;
    Memory<0x80> main_contact, other;
    armor.put<unsigned char>(0x30, 11);
    armor.ename(0xde8, "armoredcar_l");
    enemy.put<unsigned char>(0x30, 7);
    enemy.ename(0xde8, "human");
    defender.put(0xdd0, sensor.ptr());
    std::array<Address, 2> observations{main_contact.ptr(), other.ptr()};
    sensor.put(0x280, reinterpret_cast<Address>(observations.data()));
    sensor.put(0x288, reinterpret_cast<Address>(observations.data() + observations.size()));
    d::Context c{};
    c.actor = defender.ptr();
    c.brain = brain.ptr();
    c.sub = main_contact.ptr();
    c.w.length = 20.f;
    c.now = 10;
    main_contact.put(0x18, armor.ptr());
    main_contact.put(0x14, 0x103u);
    other.put(0x14, 3u);
    other.put(0x10, 3u << 28);
    other.put(0x18, enemy.ptr());
    other.put(0x28, p::XY{900.f, 0});
    assert(d::visible_soft_target(c));
    other.put(0x28, p::XY{901.f, 0});
    assert(!d::visible_soft_target(c));
    other.put(0x28, p::XY{800.f, 0});
    other.put(0x10, 1u << 28);
    assert(!d::visible_soft_target(c));
    other.put(0x10, 3u << 28);
    other.put(0x14, 1u);
    assert(!d::visible_soft_target(c));
    other.put(0x14, 3u);
    other.put(0x18, armor.ptr());
    assert(!d::visible_soft_target(c));

    // The squad assigned the defender to bound while his current target is in clear rifle range.
    d::ActorState soldier{};
    d::SquadState squad{};
    c.soldier = &soldier;
    c.team = &squad;
    c.moving_element = true;
    c.threat = {800.f, 0};
    c.squad = squad.key = 42;
    squad.mode = p::Mode::bound;
    squad.phase_at = c.now;
    soldier.role_at = soldier.sight_at = c.now;
    soldier.covered = true;
    soldier.visibility = p::Sight::open;
    soldier.sight_target = c.sub;
    brain.put(0x52c, int(p::advance));
    brain.put(0x14c, 1200.f);
    main_contact.put(0x18, enemy.ptr());
    main_contact.put(0x28, c.threat);
    assert(d::hold_shot(c) && !d::movement(c, 0));
    assert(soldier.next_search == p::never && !squad.issued_bounds);
    soldier.visibility = p::Sight::blocked;
    assert(!d::hold_shot(c));
    soldier.visibility = p::Sight::open;
    main_contact.put(0x28, p::XY{1300.f, 0});
    assert(!d::hold_shot(c));
}
void support_and_orders() {
    namespace d = goh::tactical_infantry::detail;
    namespace pr = goh::target_prio::detail;
    pr::SceneHit hit{};
    pr::HitVec hits{&hit, &hit + 1, &hit + 1};
    pr::Ray ray{};
    ray.to = {100.f, 0, 0};
    bool evaluated{};
    hit.distance = std::numeric_limits<float>::quiet_NaN();
    assert(!pr::clear_segment(ray, hits, 1.f, &evaluated) && !evaluated);
    hit.kind = 1;
    hit.distance = 20.f;
    assert(!pr::clear_segment(ray, hits, 1.f, &evaluated) && evaluated);
    hits.end = hits.begin;
    assert(pr::clear_segment(ray, hits, 1.f, &evaluated) && evaluated);
    namespace smoke = goh::auto_smoke;
    namespace grenade = goh::close_grenade;
    namespace repair = goh::tank_auto_repair;
    namespace medic = goh::auto_medic;
    namespace supply = goh::auto_resupply;
    alignas(16) std::array<std::byte, 0xe00> actor{};
    alignas(16) std::array<std::byte, 0x580> brain{}, order{}, chassis{}, weapon{}, root{}, vitals{}, aggregate{};
    const auto addr = [](auto& storage) { return reinterpret_cast<std::uintptr_t>(storage.data()); };
    const auto a = addr(actor), b = addr(brain), o = addr(order);
    const auto saved_base = goh::ai_support::base;
    goh::ai_support::base = reinterpret_cast<std::uintptr_t>(&selection_fixture) - 0x91b190;
    d::put(a, 0xdc0, addr(aggregate));
    d::put(a, 0xdd0, addr(aggregate));
    d::put(a, 0xc0, 17u);
    d::put(a, 0x30, std::uint8_t{7});
    d::put(a, 0xdaa, std::uint16_t{21});
    d::put(a, 0xdb4, 0x100u);
    d::put(a, 0xdc8, b);
    d::put(a, 0xdd8, addr(weapon));
    d::put(a, 0xde0, addr(chassis));
    d::put(a, 0x578, addr(aggregate));
    d::put(addr(aggregate), 0x18, addr(vitals));
    d::put(b, 0, goh::ai_support::base + 0x110ff88);
    d::put(b, 0x38, a);
    d::put(b, 0xe0, addr(root));
    d::put(addr(root), 0, goh::ai_support::base + 0x1110cd0);
    d::put(addr(root), 0x30, b);
    smoke::World w{0, 42, 100, 20, 10};
    auto eligible = [&] { return d::available(w, a, b); };
    alignas(16) std::array<std::byte, 0x1d0> squad{};
    std::array<std::uintptr_t, 1> members{a};
    d::put(addr(squad), 0, goh::ai_support::base + 0x110f3a0);
    d::put(addr(squad), 0x80, addr(members));
    d::put(addr(squad), 0x88, addr(members) + sizeof(a));
    d::put(b, 0x39c, 1u << 28);
    assert(eligible() && d::roster_permits(addr(squad), a));
    assert(!goh::ai_support::autonomous(a, true));
    d::put(b, 0x398, 1u);
    assert(!eligible() && !d::roster_permits(addr(squad), a));
    d::put(b, 0x39c, 0u);
    fixture_selected = true;
    assert(!eligible() && !d::roster_permits(addr(squad), a));
    fixture_selected = false;
    assert(eligible());
    d::put(addr(chassis), 0x18, 1);
    assert(!eligible());
    d::put(addr(chassis), 0x18, 0);
    d::put(addr(weapon), 0x70, std::uint8_t{2});
    assert(!eligible());
    d::put(addr(weapon), 0x70, std::uint8_t{0});
    d::put(b, 0x39c, 1u);
    assert(!eligible());
    d::put(b, 0x39c, 0u);
    d::put(b, 0, goh::ai_support::base + 0x101fd68);
    assert(!eligible());
    d::put(b, 0, goh::ai_support::base + 0x110ff88);
    d::put(b, 0xf0, o);
    for (auto vtable : {0x110c018, 0x110d278}) {
        d::put(o, 0, goh::ai_support::base + vtable);
        d::put(o, 8, a + 0xaf0);
        assert(d::protected_order(b, a) && !eligible());
    }
    d::put(b, 0xf0, std::uintptr_t{});
    assert(!d::protected_order(b, a));

    auto& r = smoke::reservations[0];
    smoke::cache_scene = w.scene;
    r.carrier = a;
    r.entity = 17;
    r.start = 100;
    assert(smoke::is_busy(w, a) && !eligible());
    r.released = true;
    assert(!smoke::is_busy(w, a));
    r.released = false;
    w.now = 181;
    assert(!smoke::is_busy(w, a));
    r = {};
    grenade::scene = w.scene;
    grenade::last_tick = 100;
    grenade::claims[0] = {9, a, 3, 17, 170, 20, false, false};
    assert(grenade::is_busy(w, a) && grenade::is_throwing(w, a) && !eligible());
    grenade::claims[0].approaching = true;
    assert(grenade::is_busy(w, a) && !grenade::is_throwing(w, a) &&
           eligible()); // approaching carrier stays rush-eligible
    grenade::claims[0].approaching = false;
    w.now = 190;
    assert(!grenade::is_busy(w, a));
    w.now = 181;
    w.scene = 43;
    assert(!grenade::is_busy(w, a));
    w.scene = 42;
    d::put(a, 0xc0, 18u);
    assert(!grenade::is_busy(w, a));
    grenade::claims[0] = {};
    auto key = goh::ai_support::identity(a);
    medic::jobs[0].medic = key;
    assert(medic::occupied(a));
    medic::jobs[0].phase = medic::Phase::cooldown;
    assert(!medic::occupied(a));
    medic::jobs[0] = {};
    supply::trips[0].actor = key;
    supply::trips[0].phase = supply::Phase::travel;
    assert(supply::occupied(a));
    supply::trips[0] = {};
    assert(!supply::occupied(a));
    auto& tank = repair::detail::tanks[0];
    tank.key = tank.crew[0].key = repair::detail::key(a);
    repair::detail::members[21] = {1, 0};
    tank.crew[0].needs_board = true;
    assert(repair::is_busy(a));
    tank.crew[0].needs_board = false;
    assert(!repair::is_busy(a));
    tank.crew[0].started = true;
    d::put(a, 0xc0, 19u);
    assert(!repair::is_busy(a));
    tank = {};
    repair::detail::members[21] = {};
    goh::ai_support::base = saved_base;
}
void lifecycle() {
    namespace t = goh::tactical_infantry;
    namespace d = t::detail;
    static_assert(!t::runtime_allowed);
    constexpr std::uintptr_t image = 0x140000000;
    for (bool enable : {false, true}) {
        for (unsigned fail = 0; fail <= t::hook_count; ++fail) {
            std::array<void*, t::hook_count> staged{};
            unsigned count = 0;
            auto create = [&](void* addr, void*, void** orig) {
                if (count == fail) return 1;
                assert(std::find(staged.begin(), staged.end(), addr) == staged.end());
                staged[count++] = addr;
                *orig = addr;
                return 0;
            };
            auto remove = [&](void* addr) {
                assert(count);
                --count;
                assert(staged[count] == addr);
                return 0;
            };
            assert(!t::prepare_hooks(image, "mismatch", enable, create, remove));
            assert(!count);
            assert(t::prepare_hooks(image, t::executable_sha256, enable, create, remove) == (fail == t::hook_count));
            assert(count == (fail == t::hook_count ? fail : 0));
            assert(d::accepting == (enable && fail == t::hook_count));
            {
                d::Guard guard;
                assert(t::inflight_count() == 1);
            }
            assert(t::inflight_count() == 0);
            t::deactivate();
            assert(!d::accepting);
            t::after_hooks_removed();
        }
    }
    unsigned tries = 0;
    auto fail_second = [&](void* addr, void*, void** out) {
        *out = addr;
        return ++tries == 2 ? 1 : 0;
    };
    auto could_not_remove = [](void*) { return 1; };
    assert(!t::prepare_hooks(image, t::executable_sha256, true, fail_second, could_not_remove));
    assert(!t::prepare_hooks(image, t::executable_sha256, true, fail_second, could_not_remove) && tries == 2);
    t::after_hooks_removed();
    std::array<d::SquadState, 2> rows{};
    auto* first = d::record(rows, 16, 0);
    assert(first);
    first->phase = 1;
    assert(d::record(rows, 16, 1)->phase == 1);
    assert(d::record(rows, 32, 2));
    assert(!d::record(rows, 48, 5));
    assert(d::record(rows, 48, 40)->phase == 0);
    d::Context c{};
    c.squad = 77;
    c.team = &d::squads[0];
    c.team->phase_at = 5;
    c.soldier = &d::soldiers[0];
    p::XY point{7, 11};
    d::record_bound(c, nullptr, &point);
    assert(!c.soldier->issued_cover && c.soldier->goal.y == 11 && c.team->issued_bounds == 1);
    assert(c.soldier->issued_squad == c.squad && c.soldier->issued_phase == 5);
    d::Descriptor cover;
    d::put(cover.ptr(), 0x10, p::XY{18, 23});
    d::record_bound(c, &cover, nullptr);
    assert(c.soldier->issued_cover && c.soldier->goal.x == 18 && c.team->issued_bounds == 2);
    d::issued.store(9);
    assert(t::stat_issued() == 9);
    t::after_hooks_removed();
    assert(t::stat_issued() == 0);
    d::soldiers[0].key = 1;
    d::squads[0].key = 2;
    d::claims.entries[0].actor = 3;
    d::reset();
    assert(!d::soldiers[0].key && !d::squads[0].key && !d::claims.entries[0].actor);
}
#endif
int main() {
    posture();
    emergency_and_squad_posture();
    elements();
    spacing_and_movement();
    cover_and_authority();
#if defined(_WIN32) || defined(GOH_TACTICAL_ADAPTER_FIXTURES)
    defensive_line();
    support_and_orders();
    lifecycle();
#endif
    std::puts("Tactical infantry fixtures passed.");
}
