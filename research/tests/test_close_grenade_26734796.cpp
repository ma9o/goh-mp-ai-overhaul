// Offline fixtures for build 26734796. No game image is loaded.
#include "../../mods/close_grenade_module.hpp"
#include <cassert>

namespace cg = goh::close_grenade;
namespace pr = goh::target_prio::detail;
namespace sim = goh::auto_smoke;

template <class T> void put(cg::Address addr, std::size_t offset, const T& value) {
    std::memcpy(reinterpret_cast<void*>(addr + offset), &value, sizeof(value));
}

void target_priority() {
    pr::Candidate rifle{}, vehicle{};
    rifle.score = 1.32f;
    rifle.sight = pr::Sight::open;
    rifle.subject = 1;
    vehicle.score = 2.01f;
    vehicle.ordinal = 1;
    vehicle.subject = 2;
    vehicle.grenade = true;
    vehicle.sight = pr::Sight::blocked;
    vehicle.has_origin = true; // Calling a bullet probe in this fixture would access an unmapped image.
    pr::Plan p{{rifle, vehicle}, true, false};
    pr::rank_candidates(p);
    assert(p.should_rewrite && p.order.front().subject == 2 && pr::scope.used == 0);
    assert(!cg::supported_armour(false, "t3476_41"));
    assert(!cg::supported_armour(false, "panzer4h"));
    assert(!cg::supported_armour(false, "tiger1e"));
    assert(!cg::supported_armour(false, "sdkfz251c_tarp"));
    assert(cg::supported_armour(true, "marder2"));
    assert(cg::supported_armour(false, "sdkfz251c"));
    assert(cg::supported_armour(false, "su26"));
}

template <std::size_t N> struct Object {
    std::array<std::byte, N> bytes{};
    cg::Address addr() {
        return reinterpret_cast<cg::Address>(bytes.data());
    }
    template <class T> void set(std::size_t offset, T value) {
        put(addr(), offset, value);
    }
};
void engine_volume() {
    Object<0x530> target;
    Object<0x100> body, engine, bodyTags, engineTags, part;
    Object<0x3b0> bodyDef, engineDef;
    std::array<cg::Address, 2> list{body.addr(), engine.addr()};
    auto begin = reinterpret_cast<cg::Address>(list.data());
    target.set(0x510, pr::Span{begin, begin + sizeof(list)});
    auto volume = [](auto& v, auto& def, auto& tags, const char* name) {
        v.set(0x30, def.addr());
        v.set(0x48, v.addr());
        def.set(0x398, pr::Span{tags.addr(), tags.addr() + 0x60});
        std::memcpy(tags.bytes.data() + 0x40, name, std::strlen(name));
        tags.set(0x50, std::strlen(name));
        tags.set(0x58, std::size_t(15));
    };
    volume(body, bodyDef, bodyTags, "body");
    volume(engine, engineDef, engineTags, "engine");
    engine.set(0x38, part.addr());
    bool resolvable = true;
    auto locate = [&](cg::Address v, cg::Address, pr::Vec3& pos) {
        pos = {1, 2, 3};
        return v != engine.addr() || resolvable;
    };
    auto select = [&] { return cg::damage_volume(target.addr(), cg::Payload::incendiary, locate).volume; };
    assert(select() == engine.addr()); // stock car body precedes engine
    resolvable = false;
    assert(select() == 0); // intact ambiguous engine must never downgrade to body
    part.set(0x24, std::uint8_t(1));
    assert(select() == body.addr());
    list[1] = 0;
    assert(select() == body.addr());
}

void friendly_radius() {
    std::vector<std::byte> image(0x15C9720);
    pr::base = reinterpret_cast<cg::Address>(image.data());
    Object<0xdb0> self, ally;
    Object<0xa0> scene, vt;
    cg::Address entities[]{self.addr(), ally.addr()};
    auto start = reinterpret_cast<cg::Address>(entities);
    scene.set(0x48, pr::Span{start, start + sizeof(entities)});
    sim::World w{pr::base, scene.addr(), 100, 1, 100};
    int sides[]{0, 2, 2, 0};
    put(pr::base, 0x15C9700, reinterpret_cast<cg::Address>(sides));
    put(pr::base, 0x15C9710, 2);
    self.set(0x30, std::uint8_t(2));
    ally.set(0x30, std::uint8_t(2));
    ally.set(0xDA8, std::uint8_t(1));
    ally.set(0, vt.addr());
    vt.set(0x80, +[](cg::Address) -> float { return 1.f; });
    pr::Vec3 impact{10, 0, 0};
    auto safe = [&] { return cg::clear_area(w, self.addr(), impact, 6); };
    ally.set(0x74, pr::Vec3{17.1f, 0, 0});
    assert(safe());
    ally.set(0x74, pr::Vec3{17, 0, 0});
    assert(!safe()); // allied hull touches blast edge
    ally.set(0x74, pr::Vec3{40, 0, 0});
    self.set(0x74, pr::Vec3{4, 0, 0});
    assert(!safe());
    // When eScene (0x15C7D58) is populated, clear_area must scan it instead of w.scene (eEnvironment).
    self.set(0x74, pr::Vec3{0, 0, 0});
    ally.set(0x74, pr::Vec3{17, 0, 0});
    Object<0xa0> empty_env, real_scene;
    cg::Address dummy_env[]{self.addr()};
    empty_env.set(0x48, pr::Span{reinterpret_cast<cg::Address>(dummy_env),
                                 reinterpret_cast<cg::Address>(dummy_env) + sizeof(dummy_env)});
    real_scene.set(0x48, pr::Span{start, start + sizeof(entities)});
    sim::World w_live{pr::base, empty_env.addr(), 100, 1, 100};
    assert(cg::clear_area(w_live, self.addr(), impact, 6)); // without 0x15C7D58, empty env misses ally
    put(pr::base, 0x15C7D58, real_scene.addr());
    assert(!cg::clear_area(w_live, self.addr(), impact, 6)); // with 0x15C7D58, real eScene catches ally
    // Closed heavy armor (e.g., friendly IS-2) gets a tight 1.5m buffer for AT/Molotov,
    // full immunity for frag, and keeps full blast radius for satchels/dynamite and infantry.
    assert(cg::ally_clearance(4.5f, 1.f, cg::Payload::at, true) == 1.5f);
    assert(cg::ally_clearance(2.5f, 1.f, cg::Payload::incendiary, true) == 1.5f);
    assert(cg::ally_clearance(1.0f, 1.f, cg::Payload::at, true) == 1.0f);
    assert(cg::ally_clearance(5.5f, 1.f, cg::Payload::frag, true) == 0.f);
    assert(cg::ally_clearance(6.0f, 1.f, cg::Payload::charge, true) == 6.0f);
    assert(cg::ally_clearance(4.5f, 1.f, cg::Payload::at, false) == 4.5f);

    Object<0x1000> tank_ally;
    Object<0x350> tank_props;
    tank_ally.set(0x30, std::uint8_t(2));
    tank_ally.set(0xDA8, std::uint8_t(1));
    tank_ally.set(0, vt.addr()); // hull = 1.0
    tank_ally.set(0x548, tank_props.addr());
    std::memcpy(tank_ally.bytes.data() + 0xDE8 + 0x40, "tank_heavy", 10);
    tank_ally.set(0xDE8 + 0x50, std::size_t(10));
    tank_ally.set(0xDE8 + 0x58, std::size_t(15));
    cg::Address tank_scene_ents[]{self.addr(), tank_ally.addr()};
    Object<0xa0> tank_scene;
    auto tstart = reinterpret_cast<cg::Address>(tank_scene_ents);
    tank_scene.set(0x48, pr::Span{tstart, tstart + sizeof(tank_scene_ents)});
    put(pr::base, 0x15C7D58, tank_scene.addr());
    // Tank ally at distance 3.0 from impact (> hull 1.0 + 1.5 = 2.5, < hull 1.0 + 4.5 = 5.5):
    tank_ally.set(0x74, pr::Vec3{13.0f, 0, 0});
    assert(cg::clear_area(w_live, self.addr(), impact, 4.5f, cg::Payload::at));
    assert(cg::clear_area(w_live, self.addr(), impact, 2.5f, cg::Payload::incendiary));
    assert(!cg::clear_area(w_live, self.addr(), impact, 6.0f, cg::Payload::charge)); // satchel still blocked
    // Tank ally too close (distance 2.4 <= hull 1.0 + 1.5): AT/Molotov still vetoed (not zeroed!)
    tank_ally.set(0x74, pr::Vec3{12.4f, 0, 0});
    assert(!cg::clear_area(w_live, self.addr(), impact, 4.5f, cg::Payload::at));
    assert(!cg::clear_area(w_live, self.addr(), impact, 2.5f, cg::Payload::incendiary));
    assert(cg::clear_area(w_live, self.addr(), impact, 5.5f, cg::Payload::frag));
    pr::base = 0;
}
void flight_contact() {
    cg::Choice c{};
    c.target = 10;
    c.aim = {11, 0, {0, 0, 2}, false, false};
    sim::ThrowContact hit{3, {}, 10, 100};
    assert(!cg::frag_contact(c, hit, c.aim.point, 1));
    hit.entity = 11;
    assert(cg::frag_contact(c, hit, c.aim.point, 1));
    hit.entity = 10;
    c.aim.open_deck = true;
    assert(cg::frag_contact(c, hit, {0.2f, 0, 1.5f}, 1));
    assert(cg::frag_contact(c, hit, {0.9f, 0, 2.5f}, 1)); // open compartment rim / shield
    assert(!cg::frag_contact(c, hit, {1.5f, 0, 2}, 1));   // far front plate
    assert(!cg::frag_contact(c, hit, {0, 0, 3.5f}, 1));   // high roof above seat
    hit.volume = 0;
    assert(!cg::frag_contact(c, hit, {0, 0, 2}, 1)); // ground / water
    std::array<std::byte, 0x110> place{};
    auto weapon = reinterpret_cast<cg::Address>(place.data());
    cg::Aim seat{10, 0, {3, 2, 1}, true, true};
    put(weapon, 0xD0, 2);
    put(weapon, 0xD4, seat.point);
    assert(cg::matches_aim(weapon, seat, 0));
    put(weapon, 0xD4, pr::Vec3{4, 2, 1});
    assert(!cg::matches_aim(weapon, seat, .75f));
}

void reservations() {
    std::array<std::byte, 0xD0> src{}, other{}, vehicle{};
    auto a = reinterpret_cast<cg::Address>(src.data()), b = reinterpret_cast<cg::Address>(other.data());
    auto t = reinterpret_cast<cg::Address>(vehicle.data());
    put(a, 0xC0, 1u);
    put(b, 0xC0, 2u);
    put(t, 0xC0, 3u);
    sim::World w{0, 1, 100, 1, 100};
    auto* c = cg::available(w, t, a);
    assert(c);
    cg::reserve(*c, w, t, a, false);
    assert(cg::available(w, t, a) == c && !cg::available(w, t, b));
    w.now += w.duration(1);
    cg::reserve(*c, w, t, a, true);
    w.now += w.duration(8.99f);
    assert(!cg::available(w, t, a) && !cg::available(w, t, b));
    ++w.now;
    assert(cg::available(w, t, b));
    cg::reserve(*c, w, t, b, true);
    put(t, 0xC0, 4u);
    assert(cg::available(w, t, a)); // new entity at reused address
    cg::clear_state();

    // Rush claims: cap to 2 concurrent rushers, track rush order, distinguish is_throwing vs is_busy,
    // and block rushing after a released throw.
    std::array<std::byte, 0xD0> third{};
    auto d = reinterpret_cast<cg::Address>(third.data());
    put(d, 0xC0, 5u);
    put(t, 0xC0, 3u);
    sim::World rw{0, 7, 200, 1, 100};
    assert(cg::rush_available(rw, t, a));
    cg::note_rush(rw, t, a, 0x1111);
    assert(cg::is_rushing(rw, a) && cg::is_busy(rw, a) && !cg::is_throwing(rw, a));
    assert(cg::is_rush_order(rw, a, 0x1111) && !cg::is_rush_order(rw, a, 0x2222));
    // Preserving existing rush order when dispatch refreshes with order==0:
    cg::note_rush(rw, t, a, 0);
    assert(cg::is_rush_order(rw, a, 0x1111));
    assert(cg::rush_available(rw, t, b));
    cg::note_rush(rw, t, b, 0x3333);
    assert(!cg::rush_available(rw, t, d)); // 3rd carrier capped
    assert(cg::rush_available(rw, t, a) && cg::rush_available(rw, t, b));
    cg::clear_rush(a);
    assert(!cg::is_rushing(rw, a) && cg::rush_available(rw, t, d));
    auto* tc = cg::available(rw, t, b, false);
    assert(tc);
    cg::reserve(*tc, rw, t, b, false, true); // approaching claim: busy for smoke, not throwing for tactical_infantry
    assert(cg::is_busy(rw, b) && !cg::is_throwing(rw, b));
    assert(!cg::available(rw, t, d, false));     // another out-of-range rusher cannot steal pre-equip claim
    assert(cg::available(rw, t, d, true) == tc); // an in-range thrower CAN immediately take over approaching claim
    cg::reserve(*tc, rw, t, b, false, false);    // active throw claim: blocks both and cannot be preempted
    assert(cg::is_busy(rw, b) && cg::is_throwing(rw, b));
    assert(!cg::available(rw, t, d, true));
    cg::reserve(*tc, rw, t, b, true, false); // released throw blocks new rushes
    assert(!cg::rush_available(rw, t, d));
    cg::clear_state();
}

void vehicle_target_ranking() {
    // Live armored car gets strong class + close-in priority over ordinary infantry;
    // defanged or known-impenetrable armor is properly demoted.
    assert(pr::vehicle_class_bonus(true, false, false, false, 20.f) > .60f);
    assert(pr::vehicle_class_bonus(true, false, false, false, 20.f) >
           pr::vehicle_class_bonus(true, false, false, true, 20.f));
    assert(pr::vehicle_class_bonus(false, true, false, false, 30.f) >
           pr::vehicle_class_bonus(false, false, true, false, 30.f));
    assert(pr::vehicle_class_bonus(false, false, false, false, 10.f) == 0.f);

    // Multi-volume penetration exposure marks armored target open without needing a coarse ray,
    // beating an incumbent infantryman cleanly.
    pr::Candidate inf{}, car{}, heavy_impen{};
    inf.subject = 10;
    inf.ordinal = 0;
    inf.score = 0.85f;
    inf.sight = pr::Sight::open;
    car.subject = 20;
    car.ordinal = 1;
    car.score = 0.45f;
    car.class_bonus = pr::vehicle_class_bonus(true, false, false, false, 20.f);
    car.armored = true;
    car.armor.observe(goh::penetration_at::policy::Part::body, {150.f, 15.f, 0.9f});
    car.armor_complete = true;
    heavy_impen.subject = 30;
    heavy_impen.ordinal = 2;
    heavy_impen.score = 0.70f;
    heavy_impen.class_bonus = pr::vehicle_class_bonus(true, false, false, false, 20.f);
    heavy_impen.armored = true;
    heavy_impen.armor.observe(goh::penetration_at::policy::Part::body, {40.f, 150.f, 0.9f});
    heavy_impen.armor_complete = true;
    pr::Plan plan{{inf, car, heavy_impen}, true, false};
    pr::rank_candidates(plan);
    assert(plan.should_rewrite && plan.order[0].subject == 20 && plan.order[0].sight == pr::Sight::open);
    assert(plan.order[1].subject == 10 && plan.order[2].subject == 30); // impenetrable heavy loses class bonus

    // Vehicle-vs-armor chord allows passing transient human or soft wood (steelEq <= 0.06),
    // while still blocking on masonry (0.15) or foreign vehicle hull.
    Object<0x40> human_ent, wall_ent, veh_ent;
    human_ent.set(0x30, std::uint8_t(6)); // human actor
    wall_ent.set(0x30, std::uint8_t(0));
    veh_ent.set(0x30, std::uint8_t(2)); // non-human vehicle actor
    Object<0x40> vol_human, vol_wood, vol_brick, vol_veh;
    Object<0x420> def_wood, def_brick;
    Object<0x100> mat_wood, mat_brick;
    mat_wood.set(0xE0, 0.05f);
    mat_brick.set(0xE0, 0.15f);
    def_wood.set(0x408, mat_wood.addr());
    def_brick.set(0x408, mat_brick.addr());
    for (auto* v : {&vol_human, &vol_wood, &vol_brick, &vol_veh})
        v->set(0x0C, 2u);
    vol_wood.set(0x30, def_wood.addr());
    vol_brick.set(0x30, def_brick.addr());
    pr::Ray ray{1, 999, 0, 0, {0, 0, 0}, {100, 0, 0}, false};
    pr::SceneHit h{};
    h.kind = 4;
    h.distance = 20.f;
    pr::HitVec hits{&h, &h + 1, &h + 1};
    // Human crossing ray: blocked by default, passed for vehicle-vs-armor.
    h.entity = human_ent.addr();
    h.volume = vol_human.addr();
    assert(!pr::clear_segment(ray, hits, 20.f, nullptr, false));
    assert(pr::clear_segment(ray, hits, 20.f, nullptr, true));
    // Soft wood fence: blocked by default, passed for vehicle-vs-armor.
    h.entity = wall_ent.addr();
    h.volume = vol_wood.addr();
    assert(!pr::clear_segment(ray, hits, 20.f, nullptr, false));
    assert(pr::clear_segment(ray, hits, 20.f, nullptr, true));
    // Masonry wall: still blocked even with pass_soft_or_human.
    h.volume = vol_brick.addr();
    assert(!pr::clear_segment(ray, hits, 20.f, nullptr, true));
    // Foreign vehicle hull: still blocked even with pass_soft_or_human.
    h.entity = veh_ent.addr();
    h.volume = vol_veh.addr();
    assert(!pr::clear_segment(ray, hits, 20.f, nullptr, true));
}

void wire_pair() {
    cg::Shot record{};
    sim::World w{};
    cg::Launch l{record, 20, 30, {}, w};
    l.origin = {1, 2, 3};
    l.velocity = {4, 5, 6};
    l.spin = {7, 8, 9};
    cg::launch = &l;
    const sim::Vec3 scratch{9, 9, 9}, *o = &scratch, *v = &scratch, *spin = &scratch;
    l.saw_send = true;
    assert(!cg::spawn_allowed(20, o, v, spin)); // vetoed command suppresses local consumption
    l.sent = true;
    assert(!cg::spawn_allowed(99, o, v, spin));
    assert(cg::spawn_allowed(20, o, v, spin));
    assert(o == &l.origin && v == &l.velocity && spin == &l.spin);
    assert(!cg::spawn_allowed(20, o, v, spin));
    cg::launch = nullptr;
    assert(cg::spawn_allowed(20, o, v, spin));
}

int main() {
    target_priority();
    engine_volume();
    friendly_radius();
    flight_contact();
    reservations();
    vehicle_target_ranking();
    wire_pair();
}
