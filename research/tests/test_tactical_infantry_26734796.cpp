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
    assert(claims.has_outbound_claim(1, 5, 10, {0, 0}, 16, 9));
    assert(!claims.has_outbound_claim(1, 5, 10, {8, 0}, 16, 9));
    assert(!claims.has_outbound_claim(1, 5, 10, {0, 0}, 16, 20));
    assert(!claims.has_outbound_claim(1, 6, 10, {0, 0}, 16, 9));
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
    // Anti-HE/MG crowding curve: full clearance at >= 8 m, smooth taper across 5.5-8 m and 2.5-5.5 m, finite fallback.
    assert(p::crowding_penalty(std::numeric_limits<float>::infinity()) == 0.f);
    assert(p::crowding_penalty(8.f) == 0.f && p::crowding_penalty(12.f) == 0.f);
    assert(std::abs(p::crowding_penalty(7.f) - 1.6f) < .01f);
    assert(std::abs(p::crowding_penalty(6.f) - 3.2f) < .01f);
    assert(std::abs(p::crowding_penalty(5.5f) - 4.f) < .01f);
    assert(std::abs(p::crowding_penalty(4.f) - 13.f) < .01f);
    assert(std::abs(p::crowding_penalty(3.f) - 19.f) < .01f);
    assert(p::crowding_penalty(2.5f) == 22.f && p::crowding_penalty(0.f) == 22.f);
    assert(p::crowding_penalty(std::numeric_limits<float>::quiet_NaN()) == 22.f);
    assert(p::cover_score(10, V::unknown, true, 0, 8, true) == 18);
    assert(p::cover_score(10, V::open, false, 0, 8, true) == 30);
    assert(p::cover_score(10, V::blocked, true, 0, 8, true) == 30);
    assert(p::cover_score(40, V::blocked, false, 0, 8, false) > p::cover_score(10, V::open, false, 0, 8, true));
    // 6-8 m spaced cover strongly beats 2.5-3.5 m clumped cover, while scarce cover stays finite and usable.
    assert(p::cover_score(10, V::open, false, 0, 7, true) > p::cover_score(10, V::open, false, 0, 3, true) + 15.f);
    assert(std::isfinite(p::cover_score(10, V::open, false, 0, 2, true)));
    assert(p::cover_score(25, V::open, false, 0, 2.5f, true) > p::cover_score(0, V::blocked, false, 0, 8, false));
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
    assert(p::cover_score(10, V::open, false, 1, 8) == 28);
    assert(p::cover_score(10, V::open, false, 1, 2.5) == 6);
    assert(p::cover_score(10, V::unknown, true, 0, 8) == 10);
    assert(p::cover_score(10, V::blocked, true, 0, 8) == 22);
    // Assistant cohesion favors the 4.5-8 m anti-HE sweet spot while keeping 2.5-4.5 m viable when scarce.
    assert(p::assistant_anchor_bonus({120, 0}, {}, 20) == 14.f);
    assert(p::assistant_anchor_bonus({60, 0}, {}, 20) == 12.f);
    assert(p::assistant_anchor_bonus({40, 0}, {}, 20) == 0.f);
    assert(p::assistant_anchor_bonus({180, 0}, {}, 20) == 0.f);
    // Overflow cover prefers 6-7 m spacing over 2 m clumping while honoring Hold Position < 7 m and scarce fallback.
    assert(p::score_overflow_spot(20, {130, 0}, {}, 20, 8, false, 6.5f) >
           p::score_overflow_spot(20, {40, 0}, {}, 20, 8, false, 2.f));
    assert(std::isfinite(p::score_overflow_spot(20, {40, 0}, {}, 20, 8, true, 2.f)));
    assert(!std::isfinite(p::score_overflow_spot(20, {140, 0}, {}, 20, 8, true, 7.f)));
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
void anti_clump_and_march_cover_policy() {
    using R = p::Role;
    using V = p::Sight;
    // Anchor priority: covered > paired gunner > heavy > lower entity ID.
    assert(p::anchor_beats(true, false, R::rifle, 99, false, R::heavy, 1));
    assert(!p::anchor_beats(false, false, R::heavy, 1, true, R::rifle, 99));
    assert(p::anchor_beats(false, true, R::heavy, 50, false, R::heavy, 2));
    assert(p::anchor_beats(false, false, R::heavy, 50, false, R::rifle, 2));
    assert(!p::anchor_beats(false, false, R::rifle, 2, false, R::heavy, 50));
    assert(p::anchor_beats(false, false, R::rifle, 2, false, R::rifle, 5));
    assert(!p::anchor_beats(false, false, R::rifle, 5, false, R::rifle, 2));

    // Anti-clump trigger, cooldown, and retry hysteresis.
    p::Soldier s;
    assert(p::should_disperse_clump(s, 10, true, 3.5f, true));
    assert(p::should_disperse_clump(s, 10, true, 4.0f, true));
    assert(!p::should_disperse_clump(s, 10, true, 4.1f, true));
    assert(!p::should_disperse_clump(s, 10, false, 2.0f, true));
    assert(!p::should_disperse_clump(s, 10, true, 2.0f, false));
    s.covered = true;
    assert(!p::should_disperse_clump(s, 10, true, 2.0f, true));
    s.covered = false;
    s.dispersed_at = 10;
    assert(!p::should_disperse_clump(s, 17.9, true, 2.0f, true));
    assert(p::should_disperse_clump(s, 18.0, true, 2.0f, true));
    s.disperse_retry = 20.5;
    assert(!p::should_disperse_clump(s, 20.4, true, 2.0f, true));
    assert(p::should_disperse_clump(s, 20.5, true, 2.0f, true));

    // Never step toward enemies, and enforce spacing gain hysteresis.
    assert(p::dispersion_step_safe({0, 0}, {0, 120}, {400, 0}));   // pure lateral left
    assert(p::dispersion_step_safe({0, 0}, {0, -120}, {400, 0}));  // pure lateral right
    assert(p::dispersion_step_safe({0, 0}, {-40, 120}, {400, 0})); // oblique back
    assert(!p::dispersion_step_safe({0, 0}, {10, 120}, {400, 0})); // stepping forward rejected
    assert(!p::dispersion_step_safe({0, 0}, {100, 0}, {400, 0}));  // straight toward enemy rejected
    assert(!p::dispersion_step_safe({0, 0}, {0, 120}, {0, 0}));    // degenerate threat rejected

    assert(p::spacing_improved(1.5f, 4.5f, false));
    assert(!p::spacing_improved(1.5f, 4.4f, false));
    assert(!p::spacing_improved(3.5f, 5.4f, false));
    assert(p::spacing_improved(3.5f, 5.5f, false));
    assert(p::spacing_improved(1.5f, 3.5f, true));
    assert(!p::spacing_improved(2.5f, 3.9f, true));
    assert(p::spacing_improved(2.5f, 4.0f, true));
    assert(p::spacing_improved(3.5f, std::numeric_limits<float>::infinity(), false));

    // Lateral dispersion fan: all 10 points are 6-12.5 m away, never step toward enemies, split clumps both ways,
    // and let a 6-man clump fan out >= 5 m pairwise without colliding on only two side slots.
    const auto even_cands = p::lateral_disperse_candidates({0, 0}, {400, 0}, {0, 0}, 2, 20);
    const auto odd_cands = p::lateral_disperse_candidates({0, 0}, {400, 0}, {0, 0}, 3, 20);
    assert(even_cands[0].y < 0.f && odd_cands[0].y > 0.f); // opposite initial lateral directions on tie
    for (const auto& pt : even_cands) {
        const float d = p::meters_between(pt, {0, 0}, 20);
        assert(d >= 5.9f && d <= 12.6f);
        assert(p::dispersion_step_safe({0, 0}, pt, {400, 0}));
    }
    std::array<p::XY, 6> placed{};
    placed[0] = {0, 0}; // anchor
    for (unsigned idx = 1; idx < 6; ++idx) {
        const auto cands = p::lateral_disperse_candidates({0, 0}, {400, 0}, {0, 0}, idx + 1, 20);
        bool found = false;
        for (const auto& pt : cands) {
            float nearest = std::numeric_limits<float>::infinity();
            for (unsigned j = 0; j < idx; ++j)
                nearest = (std::min)(nearest, p::meters_between(pt, placed[j], 20));
            if (nearest >= 5.f) {
                placed[idx] = pt;
                found = true;
                break;
            }
        }
        assert(found);
    }
    assert(p::score_disperse_cover(15, V::open, 6.5f, 5.f, true) >
           p::score_disperse_cover(15, V::blocked, 6.5f, 5.f, true));
    assert(!std::isfinite(p::score_disperse_cover(15, V::open, 6.5f, 5.f, false))); // unscreened rejected
    assert(p::score_disperse_offset(6.5f, .2f, V::open, 6.f, 1.f) >
           p::score_disperse_offset(6.5f, 1.5f, V::open, 6.f, 1.f));
    assert(p::score_disperse_offset(6.5f, .2f, V::open, 6.f, 1.f) >
           p::score_disperse_offset(6.5f, .2f, V::blocked, 6.f, 1.f));

    // Goal 3: AI march cover authority, under-fire detection, cooldown, and screened cover scoring.
    assert(p::ai_march_allows(0, 0));
    assert(p::ai_march_allows(0, (1u << 6) | (1u << 9) | (1u << 28))); // AI direct goal / follow / move bits allowed
    assert(p::ai_march_allows(2u << 10, 1u << 18));                    // alerted return fire allowed
    assert(!p::ai_march_allows(1, 0));                                 // player control=1 strictly rejected
    assert(!p::ai_march_allows(1, 1u << 28));                          // player active_user_move strictly rejected
    assert(!p::ai_march_allows(0x1000u, 0));                           // Hold Position strictly rejected
    assert(!p::ai_march_allows(0x2000u, 0));                           // Move in Zone strictly rejected
    assert(!p::ai_march_allows(1u << 10, 0));                          // Hold Fire rejected
    assert(!p::ai_march_allows(2u << 10, 0));                          // unalerted Return Fire rejected
    for (unsigned flag : {1u, 63u, 1u << 20, 1u << 24})
        assert(!p::ai_march_allows(0, flag));

    assert(!p::ai_march_under_fire(100.f, false, false, 0, false));
    assert(p::ai_march_under_fire(70.f, false, false, 0, false));
    assert(p::ai_march_under_fire(100.f, true, false, 0, false));
    assert(p::ai_march_under_fire(100.f, false, true, 0, false));
    assert(p::ai_march_under_fire(100.f, false, false, 1u << 14, false));
    assert(p::ai_march_under_fire(100.f, false, false, 0, true));

    p::Soldier m;
    assert(p::should_seek_march_cover(m, 10, true, true));
    assert(!p::should_seek_march_cover(m, 10, false, true));
    assert(!p::should_seek_march_cover(m, 10, true, false));
    m.cover_break_until = 20;
    assert(!p::should_seek_march_cover(m, 19.9, true, true));
    assert(p::should_seek_march_cover(m, 20.0, true, true));
    m.march_cover_retry = 22.5;
    assert(!p::should_seek_march_cover(m, 22.4, true, true));
    assert(p::should_seek_march_cover(m, 22.5, true, true));
    m.covered = true;
    assert(!p::should_seek_march_cover(m, 30.0, true, true));

    assert(!std::isfinite(p::score_march_cover(20, false, 7.f, 10.f))); // unscreened rejected
    assert(!std::isfinite(p::score_march_cover(20, true, 1.4f, 10.f))); // occupied/stacked rejected
    assert(!std::isfinite(p::score_march_cover(20, true, 7.f, 18.5f))); // beyond 18 m rejected
    assert(p::score_march_cover(20, true, 7.5f, 14.f) > p::score_march_cover(20, true, 2.5f, 8.f));
    assert(std::isfinite(p::score_march_cover(20, true, 2.0f, 10.f))); // scarce cover fallback
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

    // AI march roster permits active squad waypoints (squad+0x1C8) ONLY for AI Move-at-Will squads.
    d::put(b, 0x398, 0u);
    d::put(addr(squad), 0x1c8, std::uintptr_t{0x1234});
    assert(!d::roster_permits(addr(squad), a));
    assert(d::ai_march_roster_permits(addr(squad), a));
    d::put(b, 0x398, 1u); // player control=1 rejected even with waypoint
    assert(!d::ai_march_roster_permits(addr(squad), a));
    d::put(b, 0x398, 0x1000u); // Hold Position rejected
    assert(!d::ai_march_roster_permits(addr(squad), a));
    d::put(b, 0x398, 0u);
    fixture_selected = true;
    assert(!d::ai_march_roster_permits(addr(squad), a));
    fixture_selected = false;
    d::put(addr(squad), 0x1c8, std::uintptr_t{0});

    // march_order_state distinguishes safe march legs (eMove/eMoveTask/eAdvance) from cover/combat/listened orders.
    const auto& vt = goh::runtime::table().vt;
    bool active_m = false, repl = false;
    d::march_order_state(a, b, active_m, repl);
    assert(!active_m && repl); // free slot
    d::put(b, 0xf0, o);
    d::put(o, 8, a + 0xaf0);
    d::put(o, 0, goh::ai_support::base + vt.move_order);
    d::march_order_state(a, b, active_m, repl);
    assert(active_m && repl);
    d::put(o, 0x10, std::uintptr_t{0x99}); // external listener vetoes replacement
    d::march_order_state(a, b, active_m, repl);
    assert(!active_m && !repl);
    d::put(o, 0x10, std::uintptr_t{0});
    d::put(o, 0, goh::ai_support::base + vt.move_to_cover); // already moving to cover: never interrupt
    d::march_order_state(a, b, active_m, repl);
    assert(!active_m && !repl);
    d::put(o, 0, goh::ai_support::base + vt.attack_order); // explicit attack: never interrupt
    d::march_order_state(a, b, active_m, repl);
    assert(!active_m && !repl);
    d::put(o, 0, goh::ai_support::base + vt.advance_order);
    d::put(b, 0x39c, 1u << 24); // inside Exec callback: never touch brain+0xF0
    d::march_order_state(a, b, active_m, repl);
    assert(!active_m && !repl);
    d::put(b, 0x39c, 0u);
    d::march_order_state(a, b, active_m, repl);
    assert(active_m && repl);
    // Single-item eComplex wrapping eAdvance is replaceable; multi-item eComplex must never drop tail orders.
    alignas(16) std::array<std::byte, 0x60> complex_ord{};
    std::array<std::uintptr_t, 2> block{o, 0};
    std::array<std::uintptr_t, 1> map{reinterpret_cast<std::uintptr_t>(block.data())};
    const auto cx = addr(complex_ord);
    d::put(cx, 0, goh::ai_support::base + 0x110D490);
    d::put(cx, 8, a + 0xaf0);
    d::put(cx, 0x30, reinterpret_cast<std::uintptr_t>(map.data()));
    d::put(cx, 0x38, std::size_t{1});
    d::put(cx, 0x40, std::size_t{0});
    d::put(cx, 0x48, std::size_t{1});
    d::put(b, 0xf0, cx);
    d::march_order_state(a, b, active_m, repl);
    assert(active_m && repl);
    d::put(cx, 0x48, std::size_t{2}); // queued tail order -> veto replacement
    d::march_order_state(a, b, active_m, repl);
    assert(!active_m && !repl);
    d::put(b, 0xf0, std::uintptr_t{});
    d::put(b, 0x398, 1u);

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
    t::stat_clump_spread.store(4);
    t::stat_march_cover.store(5);
    t::stat_wedge_moves.store(3);
    assert(t::stat_issued() == 9);
    t::after_hooks_removed();
    assert(t::stat_issued() == 0 && t::stat_clump_spread == 0 && t::stat_march_cover == 0 && t::stat_wedge_moves == 0);
    d::soldiers[0].key = 1;
    d::squads[0].key = 2;
    d::claims.entries[0].actor = 3;
    d::reset();
    assert(!d::soldiers[0].key && !d::squads[0].key && !d::claims.entries[0].actor);
}
#endif
void flanking_wedge_policy() {
    // Trigger gates: >=4 squad, >=2 covered, >=1 open fire, no armor, fire advantage/blocked front, morale >=62, 16..55m, >=5s.
    assert(p::should_wedge(6, 2, 1, false, 70.f, 40.f, 5.5, true));
    assert(!p::should_wedge(6, 2, 1, false, 70.f, 40.f, 5.5, false)); // no fire advantage & unblocked front
    assert(!p::should_wedge(3, 2, 1, false, 70.f, 40.f, 5.5, true));  // too small
    assert(!p::should_wedge(6, 1, 1, false, 70.f, 40.f, 5.5, true));  // insufficient base of fire cover
    assert(!p::should_wedge(6, 2, 1, true, 70.f, 40.f, 5.5, true));   // armor near blocks rifle wedge
    assert(!p::should_wedge(6, 2, 1, false, 58.f, 40.f, 5.5, true));  // low morale
    assert(!p::should_wedge(6, 2, 1, false, 70.f, 12.f, 5.5, true));  // too close
    assert(!p::should_wedge(6, 2, 1, false, 70.f, 55.f, 5.5, true));  // too far (avoid crossing long MG killing zone)
    assert(!p::should_wedge(6, 2, 1, false, 70.f, 40.f, 3.0, true));  // not stalled yet
    // Tight 38 m cohesion leash keeps flankers mutually supported by base of fire.
    assert(!p::wedge_cohesive({0, 0}, {900, 0}, {0, 0}, {0, 0}, {1600, 0}, 20.f));
    assert(p::wedge_cohesive({0, 0}, {600, 0}, {0, 0}, {0, 0}, {1600, 0}, 20.f));

    // Element selection: prefers assault/SMG furthest toward chosen flank; never picks heavy/medic.
    p::Roster roster{};
    roster[0] = {10, 1, p::Role::heavy, {0, 0}};      // MG
    roster[1] = {11, 2, p::Role::heavy, {0, 200}};    // medic on left edge
    roster[2] = {12, 3, p::Role::assault, {0, -100}}; // SMG right
    roster[3] = {13, 4, p::Role::assault, {0, 120}};  // SMG left
    roster[4] = {14, 5, p::Role::rifle, {0, 160}};    // rifle far left
    roster[5] = {15, 6, p::Role::rifle, {0, -160}};   // rifle far right
    // Threat is at +X (front = {1,0}, left = {0,1}).
    const auto el = p::wedge_elements(roster, 6, {0, 0}, {800, 0}, 1.f);
    assert(!el[0] && !el[1]);
    assert(el[3] && el[2]); // Both assault/SMGs picked (2-man wedge); rifles + heavy stay base of fire.

    // When only 1 SMG exists, pick the SMG + the rifleman already furthest toward the chosen flank.
    roster[2].role = p::Role::rifle;
    const auto el_left = p::wedge_elements(roster, 6, {0, 0}, {800, 0}, 1.f);
    assert(el_left[3] && el_left[4] && !el_left[5] && !el_left[0] && !el_left[1]);

    // Flank angle & two-phase direction: lateral-oblique hook (<38 deg), then curve inward to enfilade/frag range.
    const float a0 = p::wedge_flank_angle_deg({0, 100}, {0, 0}, {800, 0}, 1.f);
    assert(a0 > 5.f && a0 < 15.f);
    assert(p::wedge_flank_angle_deg({0, -100}, {0, 0}, {800, 0}, 1.f) == 0.f); // wrong side
    const auto d_hook = p::wedge_direction({0, 100}, {0, 0}, {800, 0}, 1.f, 20.f);
    assert(d_hook.y > 0.85f && d_hook.x > 0.35f); // lateral-oblique hook
    const float a_flanked = p::wedge_flank_angle_deg({600, 500}, {0, 0}, {800, 0}, 1.f);
    assert(a_flanked >= 38.f);
    assert(p::wedge_in_enfilade(a_flanked, 26.f, true, true));
    assert(!p::wedge_in_enfilade(a_flanked, 26.f, false, true)); // must be covered
    const auto d_close = p::wedge_direction({600, 700}, {0, 0}, {800, 0}, 1.f, 20.f);
    assert(d_close.y < -0.4f); // curves inward toward threat at (800, 0)

    // Cover-to-cover safety: unscreened cover, charging inside 11 m, or pure sideways back-wall shuffle (>45 m) rejected.
    assert(!std::isfinite(p::score_wedge_cover(5.f, p::Sight::open, 6.f, 10.f, 10.f, 30.f, 40.f, 30.f, 8.f, false)));
    assert(!std::isfinite(p::score_wedge_cover(5.f, p::Sight::open, 6.f, 10.f, 10.f, 30.f, 20.f, 9.f, 8.f, true)));
    assert(!std::isfinite(p::score_wedge_cover(5.f, p::Sight::open, 6.f, 10.f, 10.f, 20.f, 80.f, 79.f, 8.f, true)));
    const float s_open = p::score_wedge_cover(5.f, p::Sight::open, 6.f, 10.f, 10.f, 45.f, 40.f, 28.f, 8.f, true);
    const float s_blk = p::score_wedge_cover(5.f, p::Sight::blocked, 6.f, 10.f, 10.f, 45.f, 40.f, 28.f, 8.f, true);
    assert(std::isfinite(s_open) && s_open > s_blk + 10.f);

    // Team state: wedge keeps phase=0 (does not flip base of fire), and immediately aborts on fallback.
    p::Team tm{};
    tm.observe_contact(10.0, true);
    assert(tm.contact_duration(16.0) == 6.0);
    tm.update(16.0, p::Mode::wedge_left, 10.f, 2.f, false);
    assert(tm.mode == p::Mode::wedge_left && tm.phase == 0);
    tm.update(23.0, p::Mode::wedge_left, 10.f, 2.f, true);
    assert(tm.mode == p::Mode::wedge_left && tm.phase == 0 && tm.cycle == 1);
    tm.update(24.0, p::Mode::fallback, 10.f, 2.f, false);
    assert(tm.mode == p::Mode::fallback); // immediate abort despite <6s commit
}
int main() {
    posture();
    emergency_and_squad_posture();
    elements();
    spacing_and_movement();
    cover_and_authority();
    anti_clump_and_march_cover_policy();
    flanking_wedge_policy();
#if defined(_WIN32) || defined(GOH_TACTICAL_ADAPTER_FIXTURES)
    defensive_line();
    support_and_orders();
    lifecycle();
#endif
    std::puts("Tactical infantry fixtures passed.");
}
