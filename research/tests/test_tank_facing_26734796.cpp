// Offline turn decisions and build-26734796 actor/brain/order fixtures.
#include "../../mods/tank_facing_module.hpp"
#include "vehicle_fixture_26734796.hpp"
#include <cassert>
#include <cstdio>
#include <limits>

namespace tf = goh::tank_facing;
namespace d = tf::detail;
namespace p = tf::policy;
using namespace goh::test::vehicle;
using d::Vec3;
using goh::runtime::XY;

namespace {
bool near_value(float a, float b) {
    return std::fabs(a - b) < 0.001f;
}

void policy_math() {
    using T = p::ThreatTier;
    assert(near_value(p::wrap_angle(-p::pi), p::pi));
    assert(near_value(p::wrap_angle(p::radians(350)), p::radians(-10)));
    assert(near_value(p::armor_exposure(0), .15f));
    assert(near_value(p::armor_exposure(p::radians(35)), .5f));
    assert(near_value(p::armor_exposure(p::radians(125)), 1.6f));
    assert(near_value(p::armor_exposure(p::pi), 2.4f));
    p::ThreatSample t{p::pi, 80, T::heavy_at, true, true, true};
    assert(near_value(p::threat_weight(t), 4.f * 2.f * 1.5f * 1.6f * 1.35f));
    for (float degrees : {90.f, 180.f, -75.f}) {
        t.bearing_rad = p::radians(degrees);
        auto result = p::evaluate_facing(0, &t, 1);
        assert(result.should_rotate && near_value(result.desired_yaw_rad, t.bearing_rad));
        assert(near_value(result.best_cost, .15f * p::threat_weight(t)));
    }
    t.bearing_rad = p::radians(23);
    assert(!p::evaluate_facing(0, &t, 1).should_rotate);
    t.bearing_rad = p::radians(26);
    assert(p::evaluate_facing(0, &t, 1).should_rotate);
    p::FacingConfig stealth{};
    stealth.hold_fire_mode = true;
    t.attacking_or_threatening = false;
    assert(!p::evaluate_facing(0, &t, 1, stealth).should_rotate);
    stealth.recently_hit = true;
    assert(p::evaluate_facing(0, &t, 1, stealth).should_rotate);
    stealth.recently_hit = false;
    t.attacking_or_threatening = true;
    assert(p::evaluate_facing(0, &t, 1, stealth).should_rotate);

    const p::ThreatSample pair[] = {{p::radians(55), 80, T::heavy_at, true}, {p::radians(100), 80, T::heavy_at, true}};
    auto between = p::evaluate_facing(0, pair, 2);
    assert(between.should_rotate);
    for (const auto& threat : pair)
        assert(std::fabs(p::wrap_angle(between.desired_yaw_rad - threat.bearing_rad)) <= p::radians(35) + .0001f);
    const p::ThreatSample mixed[] = {{0, 20, T::heavy_at, true, true, true},
                                     {p::pi, 17, T::close_infantry, true, true}};
    assert(!p::evaluate_facing(0, mixed, 2).should_rotate);

    // Opposing threats leave little gain from turning toward either one.
    const p::ThreatSample opposed[] = {{0, 80, T::heavy_at, true}, {p::pi, 80, T::heavy_at, true}};
    const auto marginal = p::evaluate_facing(p::radians(70), opposed, 2);
    assert(marginal.angle_error_rad >= p::radians(24) && marginal.improvement_ratio > 0 &&
           marginal.improvement_ratio < .25f);
    assert(!marginal.should_rotate);
    t = {p::pi, 18, T::close_infantry, true, false};
    assert(!p::threat_weight(t));
    t.attacking_or_threatening = true;
    assert(p::threat_weight(t) > 0);
    t.dist_m = 19;
    assert(!p::threat_weight(t));
    t.dist_m = std::numeric_limits<float>::quiet_NaN();
    assert(!p::evaluate_facing(0, &t, 1).should_rotate);
    assert(!p::evaluate_facing(0, nullptr, 0).should_rotate);

    p::FacingDecision turn{};
    turn.should_rotate = true;
    turn.angle_error_rad = p::radians(50);
    turn.desired_yaw_rad = p::radians(28);
    p::RotationGate gate{true, false, false, false, 0, 1.499f};
    assert(!p::can_issue_rotation(turn, gate));
    gate.seconds_since_issue = 1.5f;
    assert(p::can_issue_rotation(turn, gate));
    turn.desired_yaw_rad = p::radians(27.9f);
    assert(!p::can_issue_rotation(turn, gate));
    gate = {false, false, true, true};
    assert(p::can_issue_rotation(turn, gate));
    turn.angle_error_rad = p::radians(49.9f);
    assert(!p::can_issue_rotation(turn, gate));
    gate.backoff = true;
    turn.angle_error_rad = p::pi;
    assert(!p::can_issue_rotation(turn, gate));
}

struct Sensor {
    Memory<0x300> mem{};
    Memory<0x80> subject{};
    Address subject_ptr{};
    void setup(Address observer, Address enemy, Vec3 position) {
        mem.put<Address>(0x260, observer);
        subject_ptr = subject.ptr();
        const auto start = reinterpret_cast<Address>(&subject_ptr);
        mem.put<Address>(0x280, start);
        mem.put<Address>(0x288, start + sizeof(Address));
        subject.put<Address>(0x18, enemy);
        subject.put<Vec3>(0x28, position);
        subject.put<unsigned>(0x10, 1u << 29);
        subject.put<unsigned>(0x14, 2);
    }
};

struct Fixture {
    FakeImage image;
    FakeTank tank{}, enemy{};
    FakeHuman driver{}, rifleman{}, ally{};
    Sensor local{}, shared{};
    Memory<0x100> scene{}, scene2{};
    Memory<0xd0> other_order{};
    std::array<Memory<0xd0>, 16> orders{};
    std::array<Address, 2> entities{};
    d::World world{};
    unsigned made{}, installed{}, discarded{};
    bool remote{}, worker{}, repairing{}, hit{}, fail_factory{}, interrupt_factory{};
    Vec3 point{};
    static inline Fixture* current{};
    Fixture() {
        current = this;
        tf::after_hooks_removed();
        goh::config::reset_defaults();
        goh::coord::instance().reset();
        goh::runtime::global_accepting = true;
        assert(!tf::init(image.base(), "wrong"));
        assert(tf::init(image.base(), tf::executable_sha256));
        d::ops.is_worker = [](Address) { return current->worker; };
        d::ops.is_remote = [](Address) { return current->remote; };
        d::ops.world = [](Address, d::World& w) {
            w = current->world;
            return true;
        };
        d::ops.allies = [](Address, unsigned a, unsigned b) { return a == b || (a == 1 && b == 3); };
        d::ops.repairing = [](Address) { return current->repairing; };
        d::ops.recently_hit = [](const d::World&, Address) { return current->hit; };
        d::ops.make_rotate = [](const Vec3& target_point) -> Address {
            auto& f = *current;
            ++f.made;
            if (f.fail_factory) return 0;
            f.point = target_point;
            auto& order = f.orders.at(f.made - 1);
            order.put<Address>(0, f.image.base() + goh::runtime::table().vt.rotate_order);
            order.put<int>(0x48, 2);
            order.put<Vec3>(0x4C, target_point);
            if (f.interrupt_factory) f.tank.brain.put<Address>(0xF0, f.other_order.ptr());
            return order.ptr();
        };
        d::ops.discard = [](Address order) {
            assert(order == current->orders.at(current->made - 1).ptr());
            ++current->discarded;
        };
        d::ops.set_order = [](Address brain, Address order) {
            assert(brain == current->tank.brain.ptr() && order);
            current->tank.brain.put<Address>(0xF0, order);
            current->orders.at(current->made - 1).put<Address>(8, current->tank.actor.ptr() + 0xAF0);
            ++current->installed;
            tf::on_actor_post_quant(current->tank.actor.ptr()); // Native callback re-entry.
        };
        tank.setup(image.base(), 1, 1, 1);
        enemy.setup(image.base(), 2, 2, 2);
        driver.setup(image.base(), 3, 3, 1, tank.actor.ptr());
        rifleman.setup(image.base(), 4, 4, 2, 0);
        ally.setup(image.base(), 5, 5, 3, 0);
        tank.set_slot_count(1);
        tank.set_slot(0, "driver", "crew", 0x1000u, "", "", driver.actor.ptr(), 3);
        tank.brain.put<float>(0x3D4, -1.f);
        tank.chassis.put<Address>(0, image.base() + goh::runtime::table().vt.chassis_track);
        tank.chassis.put<XY>(0x5C, {1, 0});
        tank.actor.put<Vec3>(0x74, {10, 20, 30});
        world = {image.base(), scene.ptr(), 10000, 20, 1000};
        local.setup(tank.actor.ptr(), enemy.actor.ptr(), {10, 2020, 30});
        tank.actor.put<Address>(0xDD0, local.mem.ptr());
        shared.setup(ally.actor.ptr(), enemy.actor.ptr(), {10, 2020, 30});
        ally.actor.put<Address>(0xDD0, shared.mem.ptr());
    }
    ~Fixture() {
        assert(tf::inflight_count() == 0);
        tf::after_hooks_removed();
        assert(d::states.empty());
        goh::runtime::global_accepting = false;
        current = nullptr;
    }
    void tick(unsigned elapsed = 400) {
        world.now += elapsed;
        tf::on_actor_post_quant(tank.actor.ptr());
    }
    void clear_order() {
        tank.brain.put<Address>(0xF0, 0);
    }
    void ally_scene(float metres) {
        ally.actor.put<Vec3>(0x74, {10 + metres * world.length, 20, 30});
        entities = {tank.actor.ptr(), ally.actor.ptr()};
        scene.put<Address>(0x48, reinterpret_cast<Address>(entities.data()));
        scene.put<Address>(0x50, reinterpret_cast<Address>(entities.data() + entities.size()));
    }
    void expect_point(float x, float y) const {
        assert(near_value(point.x, x) && near_value(point.y, y) && point.z == 30);
    }
};

void admission_and_guards() {
    Fixture f;
    auto blocked = [&] {
        f.tick();
        assert(!f.made);
    };
    for (auto bit : {0u, 6u, 9u, 28u, 29u}) {
        f.tank.brain.put<unsigned>(0x39C, 1u << bit);
        blocked();
    }
    f.tank.brain.put<unsigned>(0x39C, 0);
    f.tank.brain.put<int>(0x88, 1);
    blocked();
    f.tank.brain.put<int>(0x88, 0);
    f.tank.brain.put<Address>(0x98, 16);
    blocked();
    f.tank.brain.put<Address>(0x98, 0);
    f.tank.brain.put<float>(0x3D4, 0);
    blocked();
    f.tank.brain.put<float>(0x3D4, -1.f);
    f.tank.chassis.put<unsigned>(0x18, 1);
    blocked();
    f.tank.chassis.put<unsigned>(0x18, 0);
    f.tank.weaponry.put<d::Byte>(0x70, 2);
    blocked();
    f.tank.weaponry.put<d::Byte>(0x70, 0);
    f.tank.actor.put<unsigned>(0x1C0, 1u << 28);
    blocked();
    f.tank.actor.put<unsigned>(0x1C0, 1u << 4);
    blocked();
    f.tank.actor.put<unsigned>(0x1C0, 0);
    f.tank.actor.put<d::Byte>(0xBAA, 1);
    blocked();
    f.tank.actor.put<d::Byte>(0xBAA, 0);
    f.tank.set_body_state(1);
    blocked();
    f.tank.set_body_state(0);
    f.tank.comp_rows.put<d::Byte>(0x28 + 0x24, 2);
    blocked();
    f.tank.track_name.ename(0, "engine");
    blocked();
    f.tank.comp_rows.put<d::Byte>(0x28 + 0x24, 0);
    f.tank.track_name.ename(0, "trackleft");
    f.driver.vit.put<d::Byte>(0x174, 1);
    blocked();
    f.driver.vit.put<d::Byte>(0x174, 0);
    f.tank.chassis.put<Address>(0, f.image.base());
    blocked();
    f.tank.chassis.put<Address>(0, f.image.base() + goh::runtime::table().vt.chassis_track);
    f.tank.placer.put<d::Byte>(0x48, 1);
    blocked();
    f.tank.placer.put<d::Byte>(0x48, 0);
    f.repairing = true;
    blocked();
    f.repairing = false;
    auto& coordinator = goh::coord::instance();
    assert(coordinator.claim(f.tank.actor.ptr(), 1, goh::coord::Module::auto_resupply,
                             goh::coord::Priority::auto_resupply, f.world.now, 1000));
    blocked();
    coordinator.release(f.tank.actor.ptr(), goh::coord::Module::auto_resupply);
    f.other_order.put<Address>(0, f.image.base() + goh::runtime::table().vt.attack_order);
    f.tank.brain.put<Address>(0xF0, f.other_order.ptr());
    blocked();
    assert(f.tank.brain.get<Address>(0xF0) == f.other_order.ptr());
    f.clear_order();
    goh::config::active().modules.tank_facing = false;
    blocked();
    goh::config::active().modules.tank_facing = true;
    f.remote = true;
    blocked();
    f.remote = false;
    f.worker = true;
    blocked();
    f.worker = false;
    f.tick();
    assert(f.made == 1 && f.installed == 1 && tf::stat_rotations == 1);
    f.expect_point(10, 1020); // 50 game metres from (10,20,30), on Hold Position.
    assert(f.tank.actor.get<Vec3>(0x74).y == 20 && f.tank.weaponry.get<d::Byte>(0x70) == 0);
}

void perception_and_stealth() {
    Fixture f;
    f.tank.brain.put<unsigned>(0x398, 1u | (1u << 10)); // Move at Will, Hold Fire.
    f.local.subject.put<Address>(0x18, f.rifleman.actor.ptr());
    f.local.subject.put<unsigned>(0x14, 0x182u);
    f.tick();
    assert(!f.made); // Distant ordinary rifleman.
    f.local.subject.put<Address>(0x18, f.enemy.actor.ptr());
    f.local.subject.put<unsigned>(0x14, 2);
    f.tick();
    assert(!f.made); // Unaware tank.
    f.local.subject.put<unsigned>(0x14, 0x82u);
    f.tick();
    assert(f.installed == 1); // Now threatening.
    f.clear_order();
    f.local.subject.put<unsigned>(0x14, 2);
    f.hit = true;
    f.tick();
    assert(f.installed == 2); // Recent hit, urgent heavy flank bypasses cooldown.
    f.clear_order();
    f.hit = false;
    f.tank.brain.put<unsigned>(0x398, 1);
    f.local.subject.put<unsigned>(0x10, 1u << 28);
    f.local.subject.put<unsigned>(0x64, f.world.now - 10000);
    f.tick(1);
    assert(f.installed == 2);
    f.local.subject.put<unsigned>(0x64, f.world.now);
    f.tank.brain.put<unsigned>(0x3F8, f.world.now + 5000);
    f.tick(1);
    assert(f.installed == 3); // Fresh deadline bypasses the scan interval.
}

void vehicle_type_and_angle_setting() {
    Fixture f;
    Memory<0x40> aggregate{}, cannon{};
    const Address ext = cannon.ptr();
    aggregate.put<Address>(0, reinterpret_cast<Address>(&ext));
    aggregate.put<Address>(8, reinterpret_cast<Address>(&ext + 1));
    cannon.put<Address>(0, f.image.base() + goh::runtime::table().vt.cannon_ext);
    cannon.put<Address>(0x18, f.tank.actor.ptr());
    f.tank.actor.put<Address>(0x578, aggregate.ptr());
    f.tick();
    assert(!f.made);
    f.tank.actor.put<Address>(0x578, 0);
    f.tank.actor.put<d::Byte>(0xDAD, 1u << 1);
    f.tick();
    assert(!f.made);
    f.tank.actor.put<d::Byte>(0xDAD, 0);
    f.tank.actor.put<unsigned>(0xDB4, 0);
    f.tick();
    assert(!f.made);
    f.tank.actor.put<unsigned>(0xDB4, 0x100);
    f.local.subject.put<Vec3>(0x28, {1010, 1020, 30}); // 45 degrees off the nose.
    goh::config::active().tuning.tank_facing_min_angle_deg = 60;
    f.tick();
    assert(!f.made);
    goh::config::active().tuning.tank_facing_min_angle_deg = 24;
    f.tick();
    assert(f.installed == 1);
}

void lifecycle_and_hysteresis() {
    Fixture f;
    f.tick(0);
    assert(f.installed == 1);
    f.local.subject.put<Vec3>(0x28, {-1990, 20, 30});
    f.tick(1499);
    assert(f.installed == 1);
    f.tank.brain.put<unsigned>(0x3F8, f.world.now + 5000);
    f.tick(1);
    assert(f.installed == 2 && tf::stat_retargets == 1);
    f.expect_point(-990, 20);
    f.local.subject.put<Vec3>(0x28, {-1990, 100, 30});
    f.tick(1500);
    assert(f.installed == 2);
    f.tick(6499);
    assert(tf::stat_timeouts == 0);
    const auto owned = f.tank.brain.get<Address>(0xF0);
    f.tick(1);
    assert(tf::stat_timeouts == 1 && f.tank.brain.get<Address>(0xF0) == owned);
    f.tick(500);
    assert(tf::stat_timeouts == 1 && f.installed == 2);
    f.clear_order();
    f.tick(5499);
    assert(f.installed == 2);
    f.tick(1);
    assert(f.installed == 3); // Backoff wins even for a heavy threat.
    f.other_order.put<Address>(0, f.image.base() + goh::runtime::table().vt.rotate_order);
    f.tank.brain.put<Address>(0xF0, f.other_order.ptr());
    f.tick(2000);
    assert(f.installed == 3 && f.tank.brain.get<Address>(0xF0) == f.other_order.ptr());
    f.local.subject.put<Address>(0x18, 0);
    f.local.subject.put<float>(0x58, .5f);
    f.clear_order();
    f.tick(2499);
    assert(f.installed == 3);
    f.tank.brain.put<unsigned>(0x3F8, f.world.now + 5000);
    f.tick(1);
    assert(f.installed == 4); // Light AT waits through cooldown.
    f.world.scene = f.scene2.ptr();
    f.tick();
    assert(f.installed == 4); // Surviving old order is foreign in the new scene.
    f.clear_order();
    f.tick();
    assert(f.installed == 5);
    f.world.now = 100;
    f.tick(0);
    assert(f.installed == 5); // Rewind also forgets ownership.
    f.clear_order();
    f.tick();
    assert(f.installed == 6);
    tf::deactivate();
    f.clear_order();
    f.tick();
    assert(f.installed == 6);
}

void allied_sensors_and_candidates() {
    Fixture f;
    f.tank.actor.put<Address>(0xDD0, 0);
    f.ally_scene(45.1f);
    f.tick();
    assert(!f.made);
    f.ally_scene(45);
    f.shared.subject.put<d::Byte>(0x6C, 1);
    f.tick();
    assert(!f.made);
    f.shared.subject.put<d::Byte>(0x6C, 0);
    f.shared.subject.put<unsigned>(0x14, 6);
    f.tick();
    assert(!f.made);
    f.shared.subject.put<unsigned>(0x14, 2);
    f.tick();
    assert(f.installed == 1);
    const auto p0 = f.tank.actor.get<Vec3>(0x74);
    f.tank.actor.put<Address>(0xDD0, f.local.mem.ptr());
    f.tank.brain.put<Address>(0x3A0, reinterpret_cast<Address>(&f.local.subject_ptr));
    f.tank.brain.put<Address>(0x3A8, reinterpret_cast<Address>(&f.local.subject_ptr + 1));
    auto contacts = d::gather(f.world, f.tank.actor.ptr(), f.tank.brain.ptr(), p0, {});
    assert(contacts.count == 1 && contacts.heavy(true)); // Same entity in own sensor and brain.
    f.tank.actor.put<Address>(0xDD0, 0);
    f.ally_scene(46);
    contacts = d::gather(f.world, f.tank.actor.ptr(), f.tank.brain.ptr(), p0, {});
    assert(contacts.count == 1 && contacts.heavy(true)); // Candidate vector alone.
    f.enemy.actor.put<unsigned>(0xDB4, 0);
    assert(!d::gather(f.world, f.tank.actor.ptr(), f.tank.brain.ptr(), p0, {}).count);
}

void native_order_ownership() {
    Fixture f;
    f.fail_factory = true;
    f.tick();
    assert(f.made == 1 && !f.installed && !tf::stat_rotations);
    f.fail_factory = false;
    f.interrupt_factory = true;
    f.tick();
    assert(f.made == 2 && !f.installed && f.discarded == 1 && f.tank.brain.get<Address>(0xF0) == f.other_order.ptr());
    f.interrupt_factory = false;
    f.clear_order();
    f.tick();
    assert(f.installed == 1);
    // Same address, actor and concrete order class, but the player picked a different point.
    f.orders[2].put<Vec3>(0x4C, {100, 100, 30});
    f.local.subject.put<Vec3>(0x28, {-1990, 20, 30});
    f.tick(2000);
    assert(f.made == 3 && !tf::stat_retargets);
    f.clear_order();
    f.tank.actor.put<Vec3>(0x74, {11, 20, 30});
    f.tick();
    assert(f.made == 3); // Coasting after the order finished.
    f.tank.chassis.put<XY>(0x5C, {0, 0});
    f.tank.actor.put<XY>(0x50, {1, 0});
    f.tick();
    assert(f.installed == 2); // Stationary with a valid entity-forward fallback.
}

void contact_classes_and_bounds() {
    using T = p::ThreatTier;
    Fixture f;
    assert(d::classify_contact(0, 0.f, 50, false, 1) == T::none);
    assert(d::classify_contact(0, .2f, 50, false, 1) == T::light_at);
    assert(d::classify_contact(0, 1.f, 50, false, 1) == T::heavy_at);
    const auto human = f.rifleman.actor.ptr();
    assert(d::classify_contact(human, 0.f, 18, true, 1) == T::close_infantry);
    assert(d::classify_contact(human, .2f, 90, false, 1) == T::light_at);
    f.rifleman.vit.put<d::Byte>(0x174, 1);
    assert(d::classify_contact(human, 2.f, 10, true, 1) == T::none);

    const auto e = f.enemy.actor.ptr();
    f.enemy.set_wheeled("halftrack");
    assert(d::classify_contact(e, 0.f, 50, false, 1) == T::none);
    f.enemy.track_name.ename(0, "gun");
    assert(d::classify_contact(e, 0.f, 50, false, 1) == T::light_at);
    f.enemy.comp_rows.put<d::Byte>(0x28 + 0x24, 2);
    assert(d::classify_contact(e, .3f, 50, false, 1) == T::none);
    f.enemy.comp_rows.put<d::Byte>(0x28 + 0x24, 0);
    f.enemy.def_tag.ename(0, "cannon");
    Memory<0x1680> gun_def{};
    Memory<0x100> weapon{};
    const Address mounted = weapon.ptr();
    f.enemy.weaponry.put<Address>(0x30, reinterpret_cast<Address>(&mounted));
    f.enemy.weaponry.put<Address>(0x38, reinterpret_cast<Address>(&mounted + 1));
    weapon.put<Address>(0x90, e);
    weapon.put<Address>(0x98, gun_def.ptr());
    gun_def.put<float>(0x1628, 25.f);
    assert(d::classify_contact(e, .3f, 50, false, 1) == T::heavy_at);
    gun_def.put<float>(0x1628, 20.f);
    assert(d::classify_contact(e, .3f, 50, false, 1) == T::light_at);
    f.enemy.def_tag.ename(0, "abandoned");
    assert(d::classify_contact(e, 2.f, 10, true, 1) == T::none);
    f.enemy.def_tag.ename(0, "tank");
    Memory<0x1B8> row{};
    row.put<d::Byte>(1, 1);
    row.put<int>(0x20, 1);
    row.put<Address>(0x30, e);
    f.tank.brain.put<Address>(0x3B8, row.ptr());
    f.tank.brain.put<Address>(0x3C0, row.ptr() + 0x1B8);
    auto gather = [&] {
        return d::gather(f.world, f.tank.actor.ptr(), f.tank.brain.ptr(), f.tank.actor.get<Vec3>(0x74), {});
    };
    assert(gather().rows[0].threat.is_main_target);
    f.local.subject.put<Vec3>(0x28, {10, 4021, 30});
    assert(!gather().count); // More than 200 metres from this tank.
    d::Contacts contacts{};
    for (unsigned i = 0; i < 18; ++i) {
        d::Observation o{};
        o.entity = i + 1;
        o.weight = static_cast<float>(i + 1);
        contacts.add(o, 20);
    }
    assert(contacts.count == 16);
    for (const auto& o : contacts.rows)
        assert(o.entity > 2);
    d::Contacts memory{};
    d::Observation o{};
    o.point = {100, 200, 0};
    memory.add(o, 20);
    o.point.x += 79;
    memory.add(o, 20);
    assert(memory.count == 1);
    o.point.x += 2;
    memory.add(o, 20);
    assert(memory.count == 2);
}
} // namespace

int main() {
    policy_math();
    admission_and_guards();
    perception_and_stealth();
    vehicle_type_and_angle_setting();
    {
        Fixture f;
        const auto tls = goh::ai_support::actor_tls;
        goh::ai_support::actor_tls = 0; // Live layout has distinct environment and entity scene pointers.
        f.image.put<Address>(0x15C7D58, f.scene2.ptr());
        auto& coordinator = goh::coord::instance();
        const auto human = f.rifleman.actor.ptr();
        coordinator.sync_world(f.scene2.ptr(), f.world.now);
        assert(coordinator.claim(human, 4, goh::coord::Module::recrew_weapons, goh::coord::Priority::recrew_weapons));
        tf::on_actor_post_quant(human);
        assert(coordinator.active_for(human, 4, f.world.now).owner == goh::coord::Module::recrew_weapons);
        f.image.put<Address>(0x15C7D58, f.scene.ptr());
        tf::on_actor_post_quant(human);
        assert(coordinator.lease_count() == 0);
        goh::ai_support::actor_tls = tls;
    }
    lifecycle_and_hysteresis();
    allied_sensors_and_candidates();
    native_order_ownership();
    contact_classes_and_bounds();
    std::puts("Tank facing policy and adapter fixtures passed.");
}
