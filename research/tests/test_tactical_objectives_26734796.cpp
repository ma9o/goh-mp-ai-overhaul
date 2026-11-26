// C++17 offline fixtures for the build-26734796 Move-at-Will objectives.
#include "../../mods/tactical_objectives_policy.hpp"
#if defined(_WIN32) || defined(GOH_TACTICAL_OBJECTIVES_ADAPTER_TEST)
#include "../../mods/tactical_objectives_module.hpp"
#endif
#include <cstdlib>
#include <cstdio>

// Keep fixtures active for release-mode cross-compiles as well.
void check(bool passed, const char* expression, int line) {
    if (!passed) {
        std::fprintf(stderr, "fixture line %d failed: %s\n", line, expression);
        std::abort();
    }
}
#define CHECK(expression) check(static_cast<bool>(expression), #expression, __LINE__)

namespace p = goh::tactical_objectives::policy;
using K = p::Kind;
using R = p::Result;

void leashes() {
    p::Anchor anchor;
    anchor.observe({}, false, true);
    anchor.observe({23, 0}, false, true);
    CHECK(anchor.initialized && !anchor.pending && anchor.xy.x == 0 && anchor.revision == 1);
    CHECK(p::in_leash(K::pursue, {55, 0}, {5, 0}, {5, 0}, anchor.xy));
    CHECK(!p::in_leash(K::pursue, {55.1f, 0}, {10, 0}, {10, 0}, anchor.xy));
    CHECK(!p::in_leash(K::pursue, {51, 0}, {}, {5, 0}, anchor.xy));
    CHECK(!p::in_leash(K::pursue, {51, 0}, {5, 0}, {}, anchor.xy));
    CHECK(!p::in_leash(K::pursue, {11.99f, 0}, {}, {}, anchor.xy));
    CHECK(p::in_leash(K::cap, {35, 0}, {}, {}, anchor.xy));
    CHECK(!p::in_leash(K::cap, {35.1f, 0}, {1, 0}, {1, 0}, anchor.xy));
    CHECK(!p::in_leash(K::cap, {0, 0}, {35.1f, 0}, {}, anchor.xy));
    anchor.observe({32, 0}, true, true);
    anchor.observe({85, 0}, false, false);
    CHECK(anchor.pending && anchor.xy.x == 0);
    anchor.observe({90, 0}, false, true);
    anchor.observe({110, 0}, false, true);
    CHECK(anchor.xy.x == 90 && anchor.revision == 2);
    CHECK(p::player_intent(1, 0, true) && p::player_intent(1, 1u << 28, false));
    CHECK(p::player_intent(1 | 0x2000, 0, false) && !p::player_intent(0, 1u << 28, false));
}

void local_targets() {
    p::Contacts c;
    c.add({11, {25, 0}, 10, .5f, false, false, true});
    c.add({0, {27, 0}, 9, 1, false, false, false});
    CHECK(c.size == 1 && c.rows[0].enemy == 11 && c.rows[0].xy.x == 25 && c.rows[0].power == 1);
    auto pursue = [&](double now, unsigned n = 1, bool at = false) { return c.pursue({}, {}, {}, n, at, now); };
    auto cap = [&](float x, double now, bool has_target = false) {
        return p::cap_allowed(c, {x, 0}, {}, {}, {}, 1, has_target, now);
    };
    CHECK(pursue(22) && !pursue(22.01));
    CHECK(!cap(18, 22, pursue(22) != nullptr) && cap(18, 22.01));
    CHECK(!cap(6.5, 23) && cap(6.51f, 23));
    c.add({12, {30, 0}, 10, .5f, true, true, false});
    CHECK(!pursue(12) && pursue(12, 1, true));
    CHECK(!cap(35, 12) && !cap(-20, 12)); // Threatening the squad even across the flag safety boundary.
    c.rows[1].threatens = false;
    CHECK(!cap(5, 12) && !cap(7, 12) && cap(-20, 12));
    c.add({13, {36, 0}, 10, 2, false, false, true});
    CHECK(!pursue(12, 2, true) && pursue(12, 3, true));
    CHECK(!p::manageable(K::pursue, {12.1f, false}, 100, true));
    CHECK(!p::manageable(K::cap, {.5f, true}, 8, true));
    CHECK(p::morale_ok(55, 58, true) && !p::morale_ok(54.99f, 100, true) && !p::morale_ok(100, 57.99f, true));
    CHECK(p::morale_ok(55, 0, false) && !p::odds_ok(10.1f, 5, 2) && p::odds_ok(10, 5, 2) && p::odds_ok(10, 0, 2));
    c = {};
    c.add({1, {25, 0}, 0, 1, false, false, false});
    c.add({2, {32, 0}, 9, 1, false, true, true});
    CHECK(pursue(10, 2)->enemy == 2);
    c.incomplete = true;
    CHECK(!pursue(10, 8, true) && !cap(35, 10));
}

void spreads() {
    CHECK(!p::reaches_cap({10, 0}, {4.5f, 0}, {}));
    CHECK(p::reaches_cap({6.51f, 0}, {3.5f, 0}, {}));
    CHECK(!p::reaches_cap({6.51f, 0}, {4.5f, 0}, {}));
    CHECK(p::standoff(0) == 15 && p::standoff(10) == 12 && p::standoff(100) == 20);
    for (auto kind : {K::pursue, K::cap}) {
        const p::XY target{35, 0};
        std::array<p::XY, 7> points{};
        unsigned count = kind == K::cap ? 6 : 7;
        for (unsigned i = 0; i < count; ++i) {
            CHECK(p::destination(kind, target, {}, {}, 30, i, [&](p::XY q) {
                points[i] = q;
                return true;
            }));
            CHECK(std::fabs(p::distance(points[i], target) - (kind == K::cap ? 4.5f : 18.f)) < .001f);
            for (unsigned j = 0; j < i; ++j)
                CHECK(p::distance(points[i], points[j]) >= 2.5f);
        }
        CHECK(points[0].x < target.x && points[0].y == 0);
        if (kind == K::cap) CHECK(std::fabs(points[3].x - (target.x + 4.5f)) < .001f);
        unsigned attempts = 0;
        CHECK(p::destination(kind, {30, 0}, {}, {}, 30, 0, [&](p::XY) { return ++attempts == 3; }));
        attempts = 0;
        CHECK(!p::destination(kind, {30, 0}, {}, {}, 30, 0, [&](p::XY) {
            ++attempts;
            return false;
        }));
        CHECK(attempts > 3 && attempts <= 18);
    }
}
void lifecycle() {
    p::Idle idle;
    CHECK(!idle.ready(5, false) && !idle.ready(6.49, false) && idle.ready(6.5, false));
    CHECK(!idle.ready(7.99, false) && idle.ready(8, false));
    idle.interrupt();
    CHECK(!idle.ready(12, true) && !idle.ready(13.99, true) && idle.ready(14, true));
    p::Progress path{0, 0, 20};
    CHECK(path.update(5.9, 18.51f, false, true) == R::moving && path.update(6, 18.51f, false, true) == R::failed);
    path = {0, 0, 20};
    CHECK(path.update(6, 18.5f, false, true) == R::moving && path.update(12, 17, false, true) == R::moving);
    CHECK(path.update(18, 15.5f, false, true) == R::moving && path.update(18.01, 14, false, true) == R::failed);
    CHECK(path.update(19, 3, true, false) == R::arrived);
    CHECK((p::Progress{0, 0, 10}).update(3, 8, false, false) == R::arrived);
    CHECK(p::cooldown(R::failed) == 12 && p::cooldown(R::arrived) == 8);
    p::Epoch e{1, 0xffffffff};
    CHECK(e.changed(1, 0) && e.changed(2, 0xffffffff) && !e.changed(1, 0xffffffff));
}

#if defined(_WIN32) || defined(GOH_TACTICAL_OBJECTIVES_ADAPTER_TEST)
namespace obj = goh::tactical_objectives;
namespace d = obj::detail;
namespace s = goh::ai_support;
namespace sim = goh::auto_smoke;
using s::Address;
template <std::size_t N> struct Buffer {
    alignas(16) std::array<std::byte, N> bytes{};
    Address addr() const {
        return reinterpret_cast<Address>(bytes.data());
    }
    template <class T> void set(std::size_t offset, T v) {
        CHECK(offset + sizeof(T) <= N);
        std::memcpy(bytes.data() + offset, &v, sizeof(v));
    }
};
Address selected_brain{};
std::uint8_t selected(Address b) {
    return b == selected_brain;
}
int team_code(Address) {
    return 'a';
}
struct Soldier {
    Buffer<0xe60> entity;
    Buffer<0x580> brain, root;
    Buffer<0x300> sensor;
    Buffer<0x180> vitals;
    Buffer<0x90> aggregate, chassis, weapon;
    Soldier(unsigned id) {
        entity.set(0x30, std::uint8_t{7});
        entity.set(0xc0, id);
        entity.set(0xdaa, static_cast<std::uint16_t>(id));
        entity.set(0xdb4, 0x100u);
        entity.set(0x578, aggregate.addr());
        aggregate.set(0x18, vitals.addr());
        entity.set(0xdd0, sensor.addr());
        sensor.set(0x260, entity.addr());
        entity.set(0xdc8, brain.addr());
        entity.set(0xde0, chassis.addr());
        entity.set(0xdd8, weapon.addr());
        brain.set(0, s::base + 0x110ff88);
        brain.set(0x38, entity.addr());
        brain.set(0xe0, root.addr());
        brain.set(0x398, 1u);
        brain.set(0x474, 75.f);
        root.set(0, s::base + 0x1110cd0);
        root.set(0x30, brain.addr());
    }
};
void admission_and_orders() {
    s::base = reinterpret_cast<Address>(&selected) - 0x91b190;
    Soldier soldier(17), mate(18);
    Address a = soldier.entity.addr(), b = soldier.brain.addr();
    Buffer<0x100> scene, sub, order;
    s::World w{0, scene.addr(), 100, 1, 10};
    auto admit = [&] { return d::admit(w, a, false); };
    CHECK(admit() && !d::admit(w, a, true));
    for (unsigned modes : {0x1001u, 0x2001u, 0x401u, 0x801u}) {
        soldier.brain.set(0x398, modes);
        CHECK(!admit());
    }
    soldier.brain.set(0x39c, 1u << 18);
    CHECK(admit());
    soldier.brain.set(0x398, 1u);
    for (unsigned bits : {1u, 1u << 6, 1u << 20, 1u << 28}) {
        soldier.brain.set(0x39c, bits);
        CHECK(!admit());
    }
    soldier.brain.set(0x39c, 1u << 11);
    CHECK(admit());
    soldier.brain.set(0x398, 0u);
    CHECK(!admit());
    soldier.brain.set(0x39c, 1u << 28);
    CHECK(admit());
    soldier.brain.set(0x39c, 0u);
    soldier.brain.set(0x398, 1u);
    soldier.chassis.set(0x18, 1);
    CHECK(!admit());
    soldier.chassis.set(0x18, 0);
    soldier.weapon.set(0x70, std::uint8_t{2});
    CHECK(!admit());
    soldier.weapon.set(0x70, std::uint8_t{});
    soldier.sensor.set(0x260, Address{});
    CHECK(!admit());
    soldier.sensor.set(0x260, a);
    selected_brain = b;
    CHECK(!admit());
    selected_brain = 0;
    soldier.brain.set(0xf0, order.addr());
    order.set(8, a + 0xaf0);
    for (auto vt : {0x110c018u, 0x110d278u}) {
        order.set(0, s::base + vt);
        CHECK(!admit());
    }
    soldier.brain.set(0xf0, Address{});
    Buffer<0x1d0> squad;
    std::array<Address, 2> members{a, mate.entity.addr()};
    auto list = reinterpret_cast<Address>(members.data());
    squad.set(0, s::base + 0x110f3a0);
    squad.set(0x80, s::Span{list, list + sizeof(members)});
    soldier.brain.set(0x100, squad.addr());
    mate.brain.set(0x100, squad.addr());
    auto blocked = [&] {
        d::Group g;
        CHECK(d::roster(w, a, g) && g.count == 2);
        return g.blocked;
    };
    CHECK(!blocked());
    squad.set(0x1c8, Address{1});
    CHECK(blocked());
    squad.set(0x1c8, Address{});
    selected_brain = mate.brain.addr();
    CHECK(blocked());
    selected_brain = 0;
    mate.brain.set(0x398, 0x1001u);
    CHECK(blocked());
    mate.brain.set(0x398, 1u);
    mate.brain.set(0x39c, 1u << 28);
    CHECK(blocked());
    mate.brain.set(0x39c, 0u);
    soldier.brain.set(0x100, Address{});
    auto id = s::identity(a);
    goh::auto_medic::jobs[0].medic = id;
    CHECK(!admit());
    goh::auto_medic::jobs[0] = {};
    goh::auto_resupply::trips[0].actor = id;
    goh::auto_resupply::trips[0].phase = goh::auto_resupply::Phase::travel;
    CHECK(!admit());
    goh::auto_resupply::trips[0] = {};
    sim::cache_scene = w.scene;
    auto& r = sim::reservations[0];
    r.carrier = a;
    r.entity = id.id;
    r.start = w.now;
    CHECK(!admit());
    r = {};
    namespace repair = goh::tank_auto_repair::detail;
    auto& tank = repair::tanks[0];
    tank.key = tank.crew[0].key = repair::key(a);
    repair::members[17] = {1, 0};
    tank.crew[0].boarding = true;
    CHECK(!admit());
    tank = {};
    repair::members[17] = {};
    CHECK(admit());

    obj::reset();
    d::advance(w.scene, w);
    d::evaluate(w, a, false);
    auto& u = d::units[0];
    auto& group = d::solos[0];
    CHECK(u.idle.since == 10 && group.anchor.initialized);
    // Set up the same generation and ownership as a dispatched move. A subsequent
    // visible subject withdraws only this permit and leaves the native root intact.
    s::Watched watch;
    watch.order = order.addr();
    watch.generation = 5;
    watch.permit = d::permit;
    auto& t = d::trips[0];
    t.actor = u.key;
    t.group = u.group;
    t.revision = u.revision;
    t.brain = b;
    t.order = {&watch, 5};
    t.unit = &u;
    t.state = &group;
    Buffer<0x40> vt;
    order.set(0, vt.addr());
    soldier.brain.set(0xf0, order.addr());
    CHECK(d::permit(w, a, order.addr()));
    Buffer<8> subjects;
    subjects.set(0, sub.addr());
    soldier.sensor.set(0x280, s::Span{subjects.addr(), subjects.addr() + 8});
    soldier.brain.set(0x3b8, sub.addr());
    sub.set(0x10, 3u << 28);
    CHECK(!admit() && !d::permit(w, a, order.addr()));
    d::evaluate(w, a, false);
    CHECK(!t.actor.ptr && !watch.permit && watch.order == order.addr() && s::read<Address>(b, 0xf0) == order.addr());
    sub.set(0x10, 1u << 28);
    soldier.brain.set(0xf0, Address{});
    CHECK(admit());
    w.now = 110;
    d::evaluate(w, a, false);
    w.now = 129;
    d::evaluate(w, a, false);
    CHECK(u.idle.next == 0);
    w.now = 130;
    d::evaluate(w, a, false);
    CHECK(u.idle.next == 14.5);
    soldier.brain.set(0x39c, 1u << 28);
    d::evaluate(w, a, false);
    CHECK(group.anchor.pending);
    soldier.entity.set(0x74, p::XY{37, 0});
    soldier.brain.set(0x39c, 0u);
    d::evaluate(w, a, false);
    CHECK(group.anchor.xy.x == 37 && !group.anchor.pending);
    obj::clear_state();
}

void roles_and_priority() {
    s::base = reinterpret_cast<Address>(&selected) - 0x91b190;
    obj::pr::base = s::base;
    obj::reset();
    Soldier mobile(51), support(52);
    Buffer<0x180> scene;
    s::World w{0, scene.addr(), 100, 1, 10};
    d::Group g;
    g.squad = true;
    g.key = {10};
    g.count = 2;
    g.members[0] = support.entity.addr();
    g.members[1] = mobile.entity.addr();
    Buffer<0x80> item;
    Buffer<0x1210> def;
    Buffer<0x120> tags;
    auto set_tag = [&](unsigned slot, const char* name) {
        auto length = std::strlen(name);
        std::memcpy(tags.bytes.data() + slot * 0x60 + 0x40, name, length + 1);
        tags.set(slot * 0x60 + 0x50, length);
        tags.set(slot * 0x60 + 0x58, std::size_t{15});
    };
    def.set(0x11e0, s::Span{tags.addr(), tags.addr() + 0x60});
    item.set(0x40, def.addr());
    item.set(0x48, 1);
    set_tag(0, "rocketlauncher");
    d::Role kit;
    d::item_role(item.addr(), kit);
    CHECK(kit.support && kit.at);
    set_tag(0, "sniper");
    kit = {};
    d::item_role(item.addr(), kit);
    CHECK(kit.support && !kit.at);
    set_tag(0, "missile");
    set_tag(1, "grenade");
    set_tag(2, "ic");
    def.set(0x11e0, s::Span{tags.addr(), tags.addr() + 3 * 0x60});
    kit = {};
    d::item_role(item.addr(), kit);
    CHECK(!kit.support && kit.at);
    item.set(0x48, 0);
    kit = {};
    d::item_role(item.addr(), kit);
    CHECK(!kit.at);
    for (unsigned i = 0; i < g.count; ++i) {
        auto* member = d::row(d::units, d::actor_key(g.members[i]), 10);
        CHECK(member);
        member->role_sample = 10;
        member->role = {i == 0, false, i == 0};
    }
    d::GroupState state;
    state.anchor.center({});
    state.contacts.add({100, {30, 0}, 10, 1, true, false, true});
    d::Force f;
    d::Target target;
    CHECK(d::strength(w, g.members[1], g, f) && f.eligible == 2 && f.mobile == 1 && f.slot == 0 && f.at);
    CHECK(d::choose(w, g.members[1], g, state, f, target) && target.enemy == 100 && target.kind == K::pursue);
    auto* cooldown = d::reserve(d::actor_key(g.members[1]), target, 10);
    CHECK(cooldown);
    cooldown->until = 22;
    state.contacts.add({101, {-40, 0}, 10, 1, false, false, false});
    CHECK(d::choose(w, g.members[1], g, state, f, target) && target.enemy == 101);
    CHECK(d::reserve(d::actor_key(g.members[1]), target, 10));
    CHECK(!d::choose(w, g.members[1], g, state, f, target));
    f = {};
    CHECK(d::strength(w, g.members[0], g, f) && f.self.support && f.slot == 1);
    CHECK(!d::choose(w, g.members[0], g, state, f, target));
    auto* u = d::row(d::units, d::actor_key(g.members[1]), 10);
    u->role.support = true;
    f = {};
    CHECK(d::strength(w, g.members[1], g, f) && !f.mobile);
    CHECK(!d::choose(w, g.members[1], g, state, f, target)); // All-heavy group and no flags.
    CHECK(!p::cap_strength(0, 0) && p::cap_strength(2, 8) == 2 && p::cap_strength(1, 8) == 4);
    mobile.brain.set(0x518, 10.f);
    mobile.brain.set(0x51c, 2.f);
    mobile.brain.set(0x528, 2.f);
    f = {};
    CHECK(!d::strength(w, g.members[1], g, f));
    mobile.brain.set(0x518, 1.f);
    support.brain.set(0x474, 30.f);
    f = {};
    CHECK(!d::strength(w, g.members[1], g, f));
    obj::pr::base = 0;
    obj::clear_state();
}
void capture_flags() {
    s::base = reinterpret_cast<Address>(&team_code) - 0xd0dfd0;
    Soldier soldier(21);
    Buffer<0xe00> flag;
    Buffer<0xb60> point;
    Buffer<0x140> engine;
    Buffer<0x50> aggregate;
    Buffer<8> ext;
    Buffer<0x90> scene;
    Address a = soldier.entity.addr(), f = flag.addr();
    s::World w{0, scene.addr(), 100, 1, 10};
    Buffer<16> entities;
    entities.set(0, a);
    entities.set(8, f);
    scene.set(0x48, s::Span{entities.addr(), entities.addr() + 16});
    flag.set(0x30, std::uint8_t{3});
    flag.set(0xc0, 300u);
    flag.set(0x74, p::XY{30, 0});
    flag.set(0x578, aggregate.addr());
    ext.set(0, point.addr());
    aggregate.set(0, s::Span{ext.addr(), ext.addr() + 8});
    point.set(0, s::base + 0x10fbff0);
    point.set(0x18, f);
    point.set(0xb58, engine.addr());
    engine.set(0, s::base + 0x116c550);
    engine.set(8, point.addr());
    engine.set(0x98, 1.f);
    using F = d::FlagState;
    auto status = [&] { return d::flag_state(f, a, {false, 0}); };
    CHECK(status() == F::captured && d::flag_state(f, a, {true, 0}) == F::invalid);
    flag.set(0xda8, std::uint8_t{255});
    CHECK(status() == F::unfinished);
    flag.set(0xda8, std::uint8_t{});
    engine.set(0x9c, .2f);
    CHECK(status() == F::unfinished);
    engine.set(0x9c, 0.f);
    engine.set(0x10c, 1.f);
    CHECK(status() == F::unfinished);
    engine.set(0x10c, 0.f);
    engine.set(0x108, 1.f);
    CHECK(status() == F::captured);
    engine.set(8, Address{});
    CHECK(status() == F::invalid);
    engine.set(8, point.addr());
    engine.set(0, Address{});
    CHECK(status() == F::invalid);
    engine.set(0, s::base + 0x116c550);
    point.set(0x18, a);
    CHECK(status() == F::invalid);
    point.set(0x18, f);
    point.set(0, Address{});
    CHECK(status() == F::invalid);
    point.set(0, s::base + 0x10fbff0);
    engine.set(0x98, 1.1f);
    CHECK(status() == F::invalid);
    engine.set(0x98, .7f);
    CHECK(status() == F::unfinished);
    obj::reset();
    d::advance(w.scene, w);
    d::sample_flags(w);
    CHECK(d::flag_count == 1);
    d::Group g;
    g.key = d::actor_key(a);
    g.count = 1;
    g.members[0] = a;
    auto& state = d::solos[0];
    state.key = g.key;
    state.anchor.center({});
    d::Force force;
    force.mobile = 1;
    force.eligible = 2;
    d::Target goal;
    auto choose = [&] { return d::choose_flag(w, a, g, state, force, {false, 0}, goal); };
    CHECK(choose() && goal.flag.matches(f) && goal.xy.x == 30);
    force.self.support = true;
    for (unsigned i = 0; i < 3; ++i) {
        auto& t = d::trips[i];
        t.actor = {100 + i};
        t.group = g.key;
        t.target = goal;
        t.support = true;
    }
    CHECK(!choose());
    force.self.support = false;
    CHECK(choose());
    d::trips[3] = d::trips[2];
    CHECK(!choose());
    d::trips.fill({});
    state.contacts.add({99, {49, 0}, 10, 3, true, false, false});
    CHECK(!choose());
    state.contacts = {};
    soldier.entity.set(0x74, p::XY{24, 0});
    CHECK(!choose());
    auto& u = d::units[0];
    u.group = state.key;
    u.revision = state.anchor.revision;
    u.arrived_flag = goal.flag;
    d::await_capture(w, a, state, u, {false, 0});
    CHECK(u.arrived_flag.matches(f) && state.anchor.xy.x == 0);
    engine.set(0x98, 1.f);
    engine.set(0x10c, .1f);
    d::await_capture(w, a, state, u, {false, 0});
    CHECK(u.arrived_flag.matches(f) && state.anchor.xy.x == 0);
    engine.set(0x10c, 0.f);
    d::await_capture(w, a, state, u, {false, 0});
    CHECK(!u.arrived_flag.ptr && state.anchor.xy.x == 30 && state.anchor.revision == 2 && !choose());
    CHECK(!d::finish_capture(state, u, F::unfinished, {45, 0}));
    state.anchor.observe({60, 0}, true, false);
    CHECK(!d::finish_capture(state, u, F::captured, {45, 0}) && state.anchor.xy.x == 30);
    // Flag snapshots expire as a group; resolution still rejects recycled identities immediately.
    flag.set(0xc0, 301u);
    CHECK(!s::resolve(w, goal.flag));
    w.now = 109;
    d::sample_flags(w);
    CHECK(d::flags[0].entity.id == 300);
    w.now = 110;
    d::sample_flags(w);
    CHECK(d::flags[0].entity.id == 301);
    obj::clear_state();
}
void sensors_and_limits() {
    s::base = reinterpret_cast<Address>(&selected) - 0x91b190;
    Soldier soldier(40), ally(41);
    Buffer<0x90> scene, subject;
    Buffer<8> list;
    Buffer<16> entities;
    Address a = soldier.entity.addr();
    list.set(0, subject.addr());
    ally.sensor.set(0x280, s::Span{list.addr(), list.addr() + 8});
    entities.set(0, a);
    entities.set(8, ally.entity.addr());
    scene.set(0x48, s::Span{entities.addr(), entities.addr() + 16});
    subject.set(0x10, 1u << 28);
    subject.set(0x14, 2u);
    subject.set(0x28, p::XY{30, 0});
    subject.set(0x18, Address{1}); // An unseen enemy pointer is only a deduplication key.
    subject.set(0x58, 1.f);
    subject.set(0x64, 80u);
    obj::reset();
    s::World w{0, scene.addr(), 200, 1, 10};
    d::Group g;
    g.count = 1;
    g.members[0] = a;
    g.key = d::actor_key(a);
    d::GroupState state;
    ally.entity.set(0x74, p::XY{45, 0});
    d::sample(w, g, state);
    CHECK(state.contacts.size == 1 && state.contacts.rows[0].armor);
    subject.set(0x6c, std::uint8_t{1});
    w.now = 204;
    d::sample(w, g, state);
    CHECK(state.contacts.size == 1);
    w.now = 205;
    d::sample(w, g, state);
    CHECK(state.contacts.size == 0);
    subject.set(0x6c, std::uint8_t{});
    w.now = 210;
    d::sample(w, g, state);
    CHECK(state.contacts.size == 0);
    subject.set(0x64, 210u);
    ally.entity.set(0x74, p::XY{45.1f, 0});
    w.now = 215;
    d::sample(w, g, state);
    CHECK(state.contacts.size == 0);
    g.members[g.count++] = ally.entity.addr();
    w.now = 220;
    d::sample(w, g, state);
    CHECK(state.contacts.size == 1);
    subject.set(0x28, p::XY{80, 0});
    subject.set(0x14, 0x182u);
    w.now = 225;
    d::sample(w, g, state);
    CHECK(!state.contacts.size && state.contacts.threatened({}, {}, 22.5));
    subject.set(0x28, p::XY{30, 0});
    subject.set(0x14, 2u);
    // Visible contacts use the live actor class and last observed location.
    Buffer<0xe60> enemy;
    enemy.set(0x30, std::uint8_t{3});
    constexpr char tag[] = "tank_custom";
    std::memcpy(enemy.bytes.data() + 0xde8 + 0x40, tag, sizeof(tag));
    enemy.set(0xde8 + 0x50, std::size_t{sizeof(tag) - 1});
    enemy.set(0xde8 + 0x58, std::size_t{15});
    subject.set(0x18, enemy.addr());
    subject.set(0x58, 0.f);
    subject.set(0x10, 3u << 28);
    w.now = 500;
    d::sample(w, g, state);
    CHECK(state.contacts.size == 1 && state.contacts.rows[0].armor);
    subject.set(0x14, 0u);
    w.now = 505;
    d::sample(w, g, state);
    CHECK(state.contacts.size == 0);

    std::array<d::GroupState, 2> rows{};
    auto* first = d::row(rows, {1}, 1);
    CHECK(first);
    first->anchor.center({12, 0});
    CHECK(d::row(rows, {2}, 2) && !d::row(rows, {3}, 60));
    CHECK(d::row(rows, {1}, 61) == first && first->anchor.xy.x == 12);
    auto* recycled = d::row(rows, {3}, 62.01);
    CHECK(recycled == &rows[1] && !recycled->anchor.initialized);
    auto key = d::actor_key(a);
    d::Target target{K::pursue, 89, {}, {30, 0}};
    auto* backoff = d::reserve(key, target, 5);
    CHECK(backoff && !d::reserve(key, target, 100)); // Active leases do not expire under another trip.
    d::Trip t;
    t.backoff = backoff;
    d::stop(t, 7, R::failed);
    CHECK(!d::reserve(key, target, 18.99) && d::reserve(key, target, 19));
    d::advance(w.scene, w);
    d::units[0].key = key;
    d::flag_count = 4;
    d::epoch.tick = 0xffffffff;
    s::Watched watch;
    watch.order = 68;
    watch.generation = 1;
    watch.permit = d::permit;
    d::trips[0].order = {&watch, 1};
    w.now = 0;
    d::advance(w.scene, w);
    CHECK(!d::units[0].key.ptr && !d::flag_count && !watch.permit && watch.order == 68);
    d::units[0].key = key;
    d::advance(w.scene + 1, w);
    CHECK(!d::units[0].key.ptr);
    d::record_issue(K::pursue);
    d::record_issue(K::cap);
    CHECK(obj::stat_pursues() == 1 && obj::stat_caps() == 1);
    d::advance(w.scene, w);
    CHECK(d::scope_ok(w));
    obj::shutdown();
    CHECK(!d::scope_ok(w));
    obj::clear_state();
    CHECK(!d::accepting.load() && obj::stat_pursues() == 1 && obj::stat_caps() == 1);
    obj::reset();
    CHECK(d::accepting.load() && !obj::stat_pursues() && !obj::stat_caps());
    s::base = 0;
    obj::tick(0, true);
    CHECK(!obj::is_busy(0));
}
#endif

int main() {
    leashes();
    local_targets();
    spreads();
    lifecycle();
#if defined(_WIN32) || defined(GOH_TACTICAL_OBJECTIVES_ADAPTER_TEST)
    CHECK(!d::accepting.load());
    admission_and_orders();
    roles_and_priority();
    capture_flags();
    sensors_and_limits();
    std::puts("Tactical objectives: policy and build-26734796 adapter fixtures passed.");
#else
    std::puts("Tactical objectives: portable policy fixtures passed.");
#endif
}
