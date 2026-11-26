// Build 26734796. Native inventory matching, selection, ranges and scene hits have offline substitutes.
#include "../../mods/tactical_infantry_module.hpp"
#include "vehicle_fixture_26734796.hpp"

#include <cassert>
#include <cstdio>

namespace t = goh::tactical_infantry;
namespace d = t::detail;
namespace p = t::policy;
namespace s = goh::ai_support;
namespace cg = goh::close_grenade;
namespace pr = goh::target_prio::detail;
using goh::test::vehicle::Memory;
using s::Address;
using s::Byte;

namespace {
Address selected_brain{}, belt{}, support_gun{};
Byte selection(Address brain) {
    return brain == selected_brain;
}
Byte matches(Address tags, Address requirement) {
    return tags == belt + 0xec8 && requirement == support_gun + 0x120;
}
struct Gun {
    Memory<0x1200> def;
    Memory<0xe0> item;
    Memory<0xc0> tags;
    Gun(const char* kind) {
        tags.ename(0, "weapon");
        tags.ename(0x60, kind);
        def.put(0x11e0, tags.ptr());
        def.put(0x11e8, tags.ptr() + 0xc0);
        item.put(0x40, def.ptr());
        item.put(0x48, 1);
    }
};
struct Soldier {
    Memory<0x1400> actor;
    Memory<0x580> brain, root;
    Memory<0x40> aggregate, link;
    Memory<0x180> vit;
    Memory<0x88> cover, chassis, weaponry;
    Memory<0x300> sensor;
    Memory<0xc0> manager;
    Memory<0x58> box;
    void setup(Address squad, unsigned id) {
        actor.put<Byte>(0x30, 7);
        actor.put<std::uint16_t>(0xdaa, 1);
        actor.put(0xdb4, 0x100u);
        actor.put(0xc0, id);
        actor.put(0x560, link.ptr());
        actor.put(0x578, aggregate.ptr());
        actor.put(0xdc0, cover.ptr());
        actor.put(0xdc8, brain.ptr());
        actor.put(0xdd0, sensor.ptr());
        actor.put(0xdd8, weaponry.ptr());
        actor.put(0xde0, chassis.ptr());
        brain.put(0, s::base + goh::runtime::table().vt.human_brain);
        brain.put(0x38, actor.ptr());
        brain.put(0x100, squad);
        brain.put(0x398, 1u);
        brain.put(0xe0, root.ptr());
        root.put(0, s::base + 0x1110cd0);
        root.put(0x30, brain.ptr());
        aggregate.put(0x18, vit.ptr());
        aggregate.put(0x28, manager.ptr());
        manager.put(0xa0, box.ptr());
        box.put(0x50, actor.ptr());
    }
};
struct Squad {
    Memory<0x200> squad;
    std::array<Soldier, 4> soldiers;
    std::array<Address, 4> roster{};
    s::World world{0, 100, 100, 20.f, 10.f};
    unsigned attempts{};
    p::XY last{};
    d::Purpose purpose{};
    std::array<d::Descriptor, 16> spots{};
    Squad() {
        d::reset();
        goh::coord::instance().reset();
        const auto begin = reinterpret_cast<Address>(roster.data());
        squad.put(0, s::base + 0x110f3a0);
        squad.put(0x80, begin);
        squad.put(0x88, begin + sizeof(roster));
        d::Context c{};
        c.w = world;
        assert(d::observe_epoch(c));
        for (unsigned i = 0; i < soldiers.size(); ++i) {
            auto& h = soldiers[i];
            h.setup(squad.ptr(), i + 1);
            roster[i] = h.actor.ptr();
            d::actor_record(roster[i], h.brain.ptr(), c.now)->next_follow = 200.; // isolate the one-shot windows
        }
    }
    void covered(unsigned index, p::XY position, p::XY descriptor) {
        soldiers[index].cover.put(0x24, 1);
        soldiers[index].actor.put(0x74, position);
        soldiers[index].cover.put(0x30, descriptor);
    }
    void spot(unsigned index, float x, float y, float score) {
        d::put(spots[index].ptr(), 4, 1);
        d::put(spots[index].ptr(), 0x10, p::XY{x, y});
        d::put(spots[index].ptr(), 0x80, score);
    }
    bool tick(unsigned index, unsigned tick, bool collect = true) {
        d::Context c{};
        world.now = tick;
        c.w = world;
        c.actor = roster[index];
        return d::settle_squad(c, [&](d::Context& frame, d::Purpose why) {
            ++attempts;
            purpose = why;
            if (!collect) return false;
            d::Choice best{};
            const auto first = reinterpret_cast<Address>(spots.data());
            if (!d::select_cover(frame, {first, first + sizeof(spots)}, why, best)) return false;
            last = best.spot.xy();
            d::claim(frame, last);
            return true;
        });
    }
};
void pairs() {
    s::base = reinterpret_cast<Address>(&matches) - 0x565a50;
    for (const auto* support : {"mgun", "rocketlauncher", "flame"}) {
        Squad f;
        Gun weapon(support), personal("smg");
        Memory<0x60> stack;
        Memory<0x1200> ammo;
        auto& gunner = f.soldiers[0];
        auto& mate = f.soldiers[1];
        std::array<Address, 1> items{stack.ptr()};
        stack.put(0x30, mate.box.ptr());
        stack.put(0x40, ammo.ptr());
        stack.put(0x48, 8);
        mate.box.put(0x10, reinterpret_cast<Address>(items.data()));
        mate.box.put(0x18, reinterpret_cast<Address>(items.data() + 1));
        belt = ammo.ptr();
        support_gun = weapon.def.ptr();
        d::Context c{};
        c.actor = mate.actor.ptr();
        c.squad = f.squad.ptr();
        c.now = 10.;
        c.w = f.world;
        c.self = {300, 0};
        mate.actor.put(0x74, c.self);
        p::Roster guns{}, roster{};
        for (unsigned i = 0; i < f.roster.size(); ++i) {
            auto* row = d::actor_record(f.roster[i], f.soldiers[i].brain.ptr(), c.now);
            d::refresh_carried_role(*row, c.now, i == 0 ? weapon.item.ptr() : personal.item.ptr(), i == 2);
            if (i == 1) c.soldier = row;
            roster[i] = {f.roster[i], row->id, row->role, i == 1 ? c.self : p::XY{}};
        }
        const auto count = d::find_gunners(c, guns);
        assert(count == 1 && guns[0].actor == gunner.actor.ptr() && guns[0].gun_def == weapon.def.ptr());
        const auto base_mate = roster[1];
        assert(base_mate.role == p::Role::assault);
        // Incompatible and reserved stacks do not create a pair.
        belt = 0;
        d::assign_assistant(c, *c.soldier, roster[1], guns, count);
        assert(!c.gunner);
        belt = ammo.ptr();
        stack.put(0x38, Address{1});
        c.now += .1;
        d::refresh_carried_role(*c.soldier, c.now, personal.item.ptr(), false);
        d::assign_assistant(c, *c.soldier, roster[1], guns, count);
        assert(!c.gunner);
        stack.put(0x38, Address{});
        c.now += .1;
        d::refresh_carried_role(*c.soldier, c.now, personal.item.ptr(), false);
        d::assign_assistant(c, *c.soldier, roster[1], guns, count);
        assert(roster[1].paired_gunner == gunner.actor.ptr() && roster[1].role == p::Role::heavy);
        assert(d::lagging(c));
        for (unsigned phase : {0u, 1u}) {
            const auto moving = p::bounders(roster, 4, phase);
            assert(!moving[0] && !moving[1] && !moving[2] && moving[3]);
        }
        assert(!p::assistant_cohesive(c.self, {19, 0}, c.gunner_pos, 20));
        assert(!p::assistant_cohesive(c.self, {310, 0}, c.gunner_pos, 20));
        assert(p::assistant_cohesive(c.self, {120, 0}, c.gunner_pos, 20));
        assert(p::assistant_anchor_bonus({160, 0}, {}, 20) > p::assistant_anchor_bonus({190, 0}, {}, 20));
        c.placement_center = c.gunner_pos;
        assert(std::isfinite(d::placement_score(c, d::Purpose::assist, {100, 0}, 1)));
        assert(!std::isfinite(d::placement_score(c, d::Purpose::assist, {210, 0}, 1)));
        gunner.vit.put(0x174, 1u);
        // Even a cached assignment must find its live gunner and matching ID in the current roster.
        c.gunner = 0;
        assert(d::find_gunners(c, guns) == 0);
        roster[1] = base_mate;
        d::assign_assistant(c, *c.soldier, roster[1], guns, 0);
        assert(!c.gunner && !roster[1].paired_gunner && roster[1].role == p::Role::assault);
    }
}
void rejoin() {
    s::base = reinterpret_cast<Address>(&selection) - 0x91b190;
    Squad f;
    Gun mg("mgun"), rifle("rifle");
    auto& gunner = f.soldiers[1];
    gunner.actor.put(0x74, p::XY{400, 0});
    for (unsigned i = 0; i < f.roster.size(); ++i) {
        auto& soldier = f.soldiers[i];
        auto* row = d::actor_record(f.roster[i], soldier.brain.ptr(), 10.);
        d::refresh_carried_role(*row, 10., i == 1 ? mg.item.ptr() : rifle.item.ptr(), false);
        if (i == 0) {
            row->pair_squad = f.squad.ptr();
            row->paired_gunner = gunner.actor.ptr();
            row->paired_def = mg.def.ptr();
            row->gunner_id = gunner.actor.get<unsigned>(0xc0);
            row->pair_at = 10.;
            row->next_follow = 10.;
        }
    }
    f.spot(0, 398, 0, 100); // gunner occupies this slot
    f.spot(1, 220, 0, 50);  // outside the preferred ring
    f.spot(2, 300, 0, 10);
    assert(f.tick(0, 101) && f.purpose == d::Purpose::assist && f.last.x == 300);
    assert(!f.tick(0, 102) && f.attempts == 1);
}
void overflow() {
    s::base = reinterpret_cast<Address>(&selection) - 0x91b190;
    auto& modules = goh::config::active().modules;
    modules.weapon_team_cohesion = false;
    Squad f;
    auto& a = f.soldiers[0];
    auto& b = f.soldiers[1];
    a.brain.put(0x398, 0x1001u);
    b.brain.put(0x398, 0x1001u);
    for (auto index : {0u, 1u}) {
        f.soldiers[index].brain.put(0x39c, 1u << 28);
        assert(!f.tick(index, 100));
    }
    a.brain.put(0x39c, 0u);
    b.brain.put(0x39c, 0u);
    f.covered(2, {0, 100}, {60, 0});
    assert(!f.tick(0, 101) && !f.attempts);
    f.covered(3, {0, -100}, {0, -100});
    f.spot(0, 140, 0, 100); // exactly 7 m on Hold
    f.spot(1, 60, 0, 100);  // occupied descriptor, owner's feet elsewhere
    for (unsigned i = 2; i < 14; ++i)
        f.spot(i, 60, 0, 100);
    f.spot(14, -120, 0, 20); // first twelve candidates are occupied or out of bounds
    f.spot(15, 0, 60, 10);
    selected_brain = b.brain.ptr();
    assert(!f.tick(0, 102) && !f.attempts);
    selected_brain = 0;
    a.brain.put(0x39c, 1u);
    assert(!f.tick(0, 103));
    a.brain.put(0x39c, 0u);
    assert(goh::coord::instance().claim(a.actor.ptr(), 1, goh::coord::Module::squad_logistics,
                                        goh::coord::Priority::squad_logistics));
    assert(!f.tick(0, 104) && !f.attempts);
    goh::coord::instance().release(a.actor.ptr(), goh::coord::Module::squad_logistics);
    assert(f.tick(0, 105) && f.last.x == -120 && f.purpose == d::Purpose::overflow);
    assert(f.tick(1, 105) && f.last.y == 60 && f.attempts == 2);
    assert(!f.tick(0, 106) && !f.tick(1, 106));
    a.brain.put(0x39c, 1u << 28);
    assert(!f.tick(0, 110));
    a.brain.put(0x39c, 0u);
    assert(!f.tick(0, 111, false) && !f.tick(0, 112) && f.attempts == 3); // failed attempt also consumes window

    Squad free;
    free.soldiers[0].brain.put(0x39c, 1u << 28);
    assert(!free.tick(0, 100));
    free.soldiers[0].brain.put(0x39c, 0u);
    assert(!free.tick(0, 101));
    free.covered(1, {0, -60}, {0, -60});
    free.spot(0, 160, 0, 20);
    assert(free.tick(0, 102) && free.last.x == 160);
    assert(p::overflow_cover_eligible(1, 0, 0, true));
    assert(!p::overflow_cover_eligible(0x1001, 0, 0, true));
    assert(!p::overflow_cover_eligible(0x2001, 0, 4, true));
    assert(!std::isfinite(p::score_overflow_spot(10, {60, 0}, {}, 20, 3, true, 2)));
    free.soldiers[0].brain.put(0x39c, 1u << 28);
    assert(!free.tick(0, 120));
    free.soldiers[0].brain.put(0x39c, 0u);
    selected_brain = free.soldiers[0].brain.ptr();
    assert(!free.tick(0, 121));
    selected_brain = 0;
    assert(!free.tick(0, 152) && free.attempts == 1);
    modules.weapon_team_cohesion = true;
}
void dismount() {
    Squad f;
    Memory<0x40> truck;
    truck.put<Byte>(0x30, 8);
    auto& a = f.soldiers[0];
    auto& b = f.soldiers[1];
    for (unsigned i = 0; i < 4; ++i)
        f.spot(i, (i % 2 ? -100.f : 100.f), i < 2 ? 0.f : 50.f, 10.f - i);
    assert(!f.tick(0, 100)); // ordinary tight formation
    a.actor.put(0x1c0, 1u << 28);
    b.link.put(8, truck.ptr());
    assert(!f.tick(0, 101) && !f.tick(1, 101));
    a.actor.put(0x1c0, 0u);
    b.link.put(8, Address{});
    selected_brain = b.brain.ptr();
    assert(!f.tick(0, 102) && !f.tick(1, 102));
    selected_brain = 0;
    a.brain.put(0x398, 0x1001u);
    assert(!f.tick(0, 103) && !f.tick(1, 103) && !f.attempts);
    a.brain.put(0x398, 1u);
    b.brain.put(0x39c, 1u << 28);
    assert(!f.tick(0, 104));
    b.brain.put(0x39c, 0u);
    a.chassis.put(0x18, 1);
    assert(!f.tick(0, 105));
    a.chassis.put(0x18, 0);
    assert(f.tick(0, 106) && f.purpose == d::Purpose::spread);
    const auto first = f.last;
    assert(f.tick(1, 106) && p::meters_between(f.last, first, 20) >= 1.5f);
    assert(!f.tick(0, 107) && !f.tick(1, 107));
    assert(f.tick(2, 108)); // passenger first seen on foot beside a witnessed exit
    f.covered(3, {0, 20}, {0, 20});
    assert(!f.tick(3, 109)); // already settled in cover
    f.soldiers[3].cover.put(0x24, 0);
    f.soldiers[0].actor.put(0x74, p::XY{400, 0});
    f.soldiers[1].actor.put(0x74, p::XY{400, 0});
    assert(!f.tick(3, 110)); // only one nearby ally
    f.soldiers[0].actor.put(0x74, p::XY{});
    assert(!f.tick(3, 134)); // window expired
    const auto left = p::pick_dismount_spread_offset({}, {}, {}, 4, 0, 20);
    const auto right = p::pick_dismount_spread_offset({}, {}, {}, 5, 0, 20);
    assert(p::meters_between(left, {}, 20) >= 3.5f && p::meters_between(right, {}, 20) <= 7.f);
    assert(p::meters_between(left, right, 20) > 2.5f);
    selected_brain = b.brain.ptr();
    assert(!f.tick(2, 1)); // rewind clears actions, claims and dismount witnesses
    selected_brain = 0;
    assert(!std::isfinite(d::claims.nearest(d::scene, f.squad.ptr(), 0, {}, 0)));
}
void exposed_crews() {
    Soldier shooter, gunner;
    Memory<0x600> breed;
    shooter.actor.put(0x1390, breed.ptr());
    shooter.setup(0, 1);
    gunner.setup(0, 2);
    gunner.actor.put<Byte>(0x38, 0x40);
    gunner.brain.put(0x398, 1u << 15);
    Gun weapon("rifle");
    Memory<0x60> cartridge;
    Memory<0xf00> gun;
    Memory<0x500> gun_def;
    Memory<0x60> mount_tag, shield_tag;
    Memory<0x60> shield, crew_hit;
    Memory<0x500> shield_def;
    gun.put(0x548, gun_def.ptr());
    gun_def.put(0x330, mount_tag.ptr());
    gun_def.put(0x338, mount_tag.ptr() + 0x60);
    shield.put(0x30, shield_def.ptr());
    shield_def.put(0x398, shield_tag.ptr());
    shield_def.put(0x3a0, shield_tag.ptr() + 0x60);
    shield_tag.ename(0, "gun_shield");
    crew_hit.put(0xc, 2u);
    weapon.item.put(0x50, cartridge.ptr());
    weapon.item.put(0xdc, 1);
    cg::Aim aim{gunner.actor.ptr(), 0, {14, 0, 0}, false, true, true};
    s::World w{0, 22, 100, 1, 10};
    std::array<pr::SceneHit, 2> hits{};
    hits[0].distance = 12;
    hits[0].entity = gunner.actor.ptr();
    hits[0].volume = crew_hit.ptr();
    unsigned hit_count = 1;
    float range = 20;
    auto shot = [&](cg::Vehicle type) {
        return cg::gunner_shot(
            w, shooter.actor.ptr(), gun.ptr(), type, aim,
            [&](Address actor, float distance) {
                return cg::loaded_small_arm_reaches(actor, weapon.item.ptr(), distance, [&](Address def, Address ammo) {
                    assert(def == weapon.def.ptr() && ammo == cartridge.ptr());
                    return range;
                });
            },
            [&](const pr::Ray& ray, float distance) {
                assert(ray.target == gunner.actor.ptr() && ray.from.z > 0 && ray.to.z > 0);
                return cg::clear_gunner_ray({hits.data(), hits.data() + hit_count, hits.data() + hits.size()},
                                            gun.ptr(), ray.target, distance);
            });
    };
    assert(shot(cg::Vehicle::gun));
    assert(cg::defer_for_gunner(cg::Payload::at, false, false, 20));
    assert(cg::defer_for_gunner(cg::Payload::frag, true, true, 14));
    assert(!cg::defer_for_gunner(cg::Payload::frag, true, true, 9.99f));
    assert(cg::defer_for_gunner(cg::Payload::frag, false, true, 9.99f));
    assert(cg::defer_for_gunner(cg::Payload::frag, true, false, 9.99f));
    assert(cg::defer_for_gunner(cg::Payload::frag, true, true, 10));
    hits[1] = hits[0];
    hit_count = 2;
    hits[0].entity = gun.ptr();
    hits[0].distance = 11;
    hits[0].volume = shield.ptr();
    for (auto label : {"shield", "gun_shield", "armor"}) {
        shield_tag.ename(0, label);
        assert(!shot(cg::Vehicle::gun)); // intervening gun volume, including one without the generic hard flag
    }
    std::swap(hits[0], hits[1]);
    hits[1].distance = 13;
    assert(shot(cg::Vehicle::gun)); // gunner is the first impact from this flank
    hits[0].entity = 0;
    hits[0].kind = 1;
    hits[0].distance = 13.9f;
    assert(!shot(cg::Vehicle::gun)); // hard ground immediately before the chest
    hits[0].kind = 0;
    hits[0].volume = crew_hit.ptr();
    assert(!shot(cg::Vehicle::gun)); // foreign solid cover
    hit_count = 0;
    assert(shot(cg::Vehicle::gun) && !shot(cg::Vehicle::car));
    Gun mounted_mg("mgun");
    Memory<0x50> weapons;
    Memory<0xa0> place;
    std::array<Address, 1> mounts{place.ptr()};
    gun.put(0xdd8, weapons.ptr());
    weapons.put(0x30, reinterpret_cast<Address>(mounts.data()));
    weapons.put(0x38, reinterpret_cast<Address>(mounts.data() + 1));
    place.put(0x90, gun.ptr());
    place.put(0x98, mounted_mg.def.ptr());
    assert(shot(cg::Vehicle::car));
    for (auto type : {cg::Vehicle::none, cg::Vehicle::tank, cg::Vehicle::armored_car})
        assert(!shot(type));
    mount_tag.ename(0, "halftrack");
    assert(!shot(cg::Vehicle::car));
    mount_tag.ename(0, "apc");
    assert(!shot(cg::Vehicle::car));
    gunner.brain.put(0x398, 1u);
    assert(!shot(cg::Vehicle::gun));
    gunner.brain.put(0x398, 1u << 15);
    gunner.vit.put(0x174, 2u);
    assert(!shot(cg::Vehicle::gun));
    gunner.vit.put(0x174, 0u);
    aim.gunner = false;
    assert(!shot(cg::Vehicle::gun)); // driver or loader
    aim.gunner = true;
    aim.station_point = true;
    assert(!shot(cg::Vehicle::gun));
    aim.station_point = false;
    range = 13;
    assert(!shot(cg::Vehicle::gun));
    range = 20;
    weapon.item.put(0xdc, 0);
    assert(!shot(cg::Vehicle::gun));
    weapon.item.put(0xdc, 1);
    for (auto kind : {"pistol", "rocketlauncher", "grenade"}) {
        weapon.tags.ename(0x60, kind);
        assert(!shot(cg::Vehicle::gun));
    }
    for (auto kind : {"smg", "mgun", "rifle"}) {
        weapon.tags.ename(0x60, kind);
        assert(shot(cg::Vehicle::gun));
    }
    auto& modules = goh::config::active().modules;
    shooter.actor.put(0x1390, Address{});
    assert(!shot(cg::Vehicle::gun)); // missing breed ability information
    shooter.actor.put(0x1390, breed.ptr());
    modules.mg_crew_small_arms = false;
    assert(!shot(cg::Vehicle::gun));
    modules.mg_crew_small_arms = true;
    modules.close_grenade = false;
    assert(!shot(cg::Vehicle::gun));
    modules.close_grenade = true;
}
} // namespace
int main() {
    pairs();
    rejoin();
    overflow();
    dismount();
    exposed_crews();
    t::stat_overflow_cover = t::stat_dismount_spread = cg::stat_mg_small_arms_defer = 3;
    t::after_hooks_removed();
    cg::disconnect();
    assert(!t::stat_overflow_cover && !t::stat_dismount_spread && !cg::stat_mg_small_arms_defer);
    std::puts("Stream C squad tactics fixtures passed.");
}
