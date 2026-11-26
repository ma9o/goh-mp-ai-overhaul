// Build 26734796. Real post-quant admission, order decoding, coordinator and watch
// generations run against an in-memory scene. Only native calls are substituted.
#include "../../mods/convoy_spacing_module.hpp"
#include "vehicle_fixture_26734796.hpp"
#include <cstdio>
#include <deque>

namespace m = goh::movement_orders;
namespace r = goh::movement_recovery;
namespace c = goh::convoy_spacing;
namespace s = goh::ai_support;
namespace rt = goh::runtime;
namespace co = goh::coord;
using namespace goh::test::vehicle;
using m::XY;
namespace {
constexpr XY destination{400, 100}, origin{100, 100};
constexpr float scale = 10;
struct Order {
    Memory<0x200> mem{};
    Memory<0x20> task{};
    std::vector<Address> children;
    std::array<Address, 4> slots{};
    std::array<Address, 2> blocks{};
    bool destroyed{};
    Address ptr() const {
        return mem.ptr();
    }
    void publish() {
        slots.fill(0);
        assert(children.size() <= slots.size());
        std::copy(children.begin(), children.end(), slots.begin());
        blocks = {reinterpret_cast<Address>(slots.data()), reinterpret_cast<Address>(slots.data() + 2)};
        mem.put<Address>(0x30, reinterpret_cast<Address>(blocks.data()));
        mem.put<std::size_t>(0x38, 2);
        mem.put<std::size_t>(0x48, children.size());
    }
};
struct Fixture;
Fixture* live{};
struct Fixture {
    FakeImage image;
    Memory<0x300> scene{}, second_scene{};
    Memory<0x600> human_root{};
    Memory<0xA0> weaponry{};
    Memory<0x40> extlist{};
    FakeHuman human{};
    FakeTank follower{}, leader{};
    std::array<Address, 4> roster{};
    std::deque<Order> orders;
    s::World w{};
    Address actor{}, brain{};
    bool vehicle{}, remote{}, worker{}, serial{true}, passable{true}, alive{true};
    XY speed{3.f, 0}, leader_speed{1.f, 0};
    int installs{}, probes{};
    Address replace_at_factory{};
    explicit Fixture(bool veh = false) : vehicle(veh) {
        live = this;
        rt::global_accepting = true;
        s::base = image.base();
        s::accepting = true;
        goh::config::active() = {};
        co::instance().reset();
        assert(!r::init(image.base(), "other") && !c::init(image.base(), nullptr));
        assert(r::init(image.base(), rt::kBuild26734796Sha256) && c::init(image.base(), rt::kBuild26734796Sha256));
        assert(!r::init(image.base(), rt::kBuild26734796Sha256));
        human.setup(image.base(), 1, 1, 0, 0, 1, 0);
        follower.setup(image.base(), 2, 2, 0, 1, 0);
        leader.setup(image.base(), 3, 3, 0, 1, 0);
        actor = veh ? follower.actor.ptr() : human.actor.ptr();
        brain = veh ? follower.brain.ptr() : human.brain.ptr();
        human_root.put<Address>(0, image.base() + 0x1110CD0);
        human_root.put<Address>(0x30, human.brain.ptr());
        human.brain.put<Address>(0xE0, human_root.ptr());
        follower.actor.put<Address>(0x578, extlist.ptr());
        follower.chassis.put<XY>(0x5C, {1, 0});
        leader.chassis.put<XY>(0x5C, {1, 0});
        rt::write<Address>(actor, 0xDD8, weaponry.ptr());
        pos(actor, origin);
        pos(leader.actor.ptr(), {145, 100});
        roster = {actor, leader.actor.ptr(), 0};
        for (auto* sc : {&scene, &second_scene}) {
            sc->put<Address>(0x48, reinterpret_cast<Address>(roster.data()));
            sc->put<Address>(0x50, reinterpret_cast<Address>(roster.data() + roster.size()));
        }
        w = {s::base, scene.ptr(), 0, scale, 1000};
        m::native.callback = [](Address a, s::World& wld) {
            if (!live->serial || live->worker || live->remote || a != live->actor) return false;
            wld = live->w;
            return true;
        };
        m::native.resolve = [](const s::World& wld, s::Identity id) {
            return live->alive ? s::resolve(wld, id) : Address{};
        };
        m::native.sequence = [] {
            auto& q = live->make(m::sequence_vt);
            q.publish();
            return q.ptr();
        };
        m::native.wait = [](float t) {
            assert(t == 2.f);
            return live->make(0x1109A58).ptr();
        };
        m::native.point = [](const XY& pt) {
            if (live->replace_at_factory) {
                live->replace(live->replace_at_factory);
                live->replace_at_factory = 0;
            }
            return live->point(pt);
        };
        m::native.push = [](Address q, Address o, int layer) {
            auto& seq = live->node(q);
            auto& children = seq.children;
            children.insert(children.end() - layer, o);
            live->node(o).mem.put<Address>(8, seq.mem.get<Address>(8));
            seq.publish();
        };
        m::native.keep_last = [](Address q) {
            auto& seq = live->node(q);
            assert(seq.children.size() > 1);
            while (seq.children.size() > 1)
                live->pop(seq);
        };
        m::native.set = [](Address b, Address q) {
            assert(b == live->brain && live->alive);
            ++live->installs;
            live->replace(q);
        };
        m::native.discard = [](Address o) { live->destroy(o); };
        r::detail::probe = [](const s::World& wld, Address a, XY dest, std::optional<XY> tried) {
            ++live->probes;
            return r::detail::nudge_route(wld, a, dest, tried, [](s::Vec3, s::Vec3&) { return live->passable; });
        };
        c::detail::probe = [](const s::World& wld, Address a, const c::Leader& ahead, XY fwd) {
            ++live->probes;
            return c::detail::passing_route(wld, a, ahead, fwd, [](s::Vec3, s::Vec3&) { return live->passable; });
        };
        c::detail::speed_vector = [](const s::World&, Address a) {
            return a == live->actor ? live->speed : live->leader_speed;
        };
        replace(point(destination));
    }
    ~Fixture() {
        replace(0);
        r::begin_shutdown();
        c::begin_shutdown();
        assert(r::quiescent() && c::quiescent());
        r::after_hooks_removed();
        c::after_hooks_removed();
        r::detail::probe = r::detail::nudge;
        c::detail::probe = c::detail::pass;
        c::detail::speed_vector = c::detail::velocity;
        m::native = {};
        s::watches.release();
        s::base = 0;
        live = nullptr;
    }
    static void pos(Address a, XY p) {
        rt::write(a, 0x74, m::at(p, 5));
    }
    Order& make(Address vt) {
        orders.emplace_back();
        auto& o = orders.back();
        o.mem.put<Address>(0, image.base() + vt);
        return o;
    }
    Order& node(Address p) {
        for (auto& o : orders)
            if (o.ptr() == p) {
                assert(!o.destroyed);
                return o;
            }
        std::abort();
    }
    Address point(XY pt) {
        auto& o = make(rt::table().vt.move_task_order);
        o.task.put<Address>(0, image.base() + m::point_vt);
        o.task.put<XY>(0x10, pt);
        o.mem.put<Address>(0x40, o.task.ptr());
        return o.ptr();
    }
    Address root() const {
        return rt::read<Address>(brain, 0xF0);
    }
    void destroy(Address o) {
        if (!o) return;
        auto& n = node(o);
        for (auto child : n.children)
            destroy(child);
        s::destroy([](Address p, unsigned) { return p; }, o, 1);
        n.destroyed = true;
    }
    void replace(Address o) {
        destroy(root());
        rt::write(brain, 0xF0, o);
        if (o) {
            auto& n = node(o);
            n.mem.put<Address>(8, actor + 0xAF0);
            for (auto child : n.children)
                node(child).mem.put<Address>(8, actor + 0xAF0);
        }
    }
    void pop(Order& n) {
        destroy(n.children.front());
        n.children.erase(n.children.begin());
        n.publish();
    }
    void tick(unsigned now) {
        w.now = now;
        if (vehicle)
            c::on_actor_post_quant(actor);
        else
            r::on_actor_post_quant(actor);
    }
    bool front_permitted() {
        const auto front = node(root()).children.front();
        const auto* watch = s::tracked(front);
        return watch && watch->permit && watch->permit(w, actor, front);
    }
    XY goal(Address order) const {
        const auto pt = m::task_point(order);
        assert(pt);
        return *pt;
    }
    void finish_waypoints() {
        auto& seq = node(root());
        while (seq.children.size() > 1) {
            assert(front_permitted());
            pos(actor, goal(seq.children.front()));
            pop(seq);
        }
    }
    void resume() {
        assert(node(root()).children.size() == 1 && front_permitted());
        const auto front = node(root()).children.front();
        assert(m::same(goal(front), destination));
        assert(!s::tracked(front));
    }
};

void stall_and_fan() {
    r::StallDetector st{};
    auto sample = [&](XY p, double time) { return st.stalled(p, destination, scale, time, 2.5f); };
    assert(!sample(origin, 0) && !sample(origin, 2.499));
    assert(!sample({104.5f, 100}, 2.5) && sample({104.4f, 100}, 2.5));
    assert(!sample({106, 100}, 2.5) && !sample({106, 100}, 4.99) && sample({106, 100}, 5));
    auto candidate = [&](auto grade, std::optional<XY> tried = {}) {
        return r::pick_nudge_point(origin, destination, scale, grade, tried);
    };
    auto pass = candidate([](XY p) { return p.y < 100 ? r::Passage::onward_clear : r::Passage::reachable; });
    assert(pass && pass->y == 80);
    pass = candidate([](XY p) { return p.x < 100 ? r::Passage::reachable : r::Passage::blocked; });
    assert(pass && pass->x == 80);
    const auto next = candidate([](XY p) { return p.x < 100 ? r::Passage::reachable : r::Passage::blocked; }, pass);
    assert(next && !m::same(*next, *pass));
    st.tried(5, pass);
    assert(!st.stalled(origin, {416, 100}, scale, 5., 2.5f) && st.attempts == 0);
    assert(!candidate([](XY) { return r::Passage::blocked; }));
}
void movement_resume_and_cap() {
    Fixture f;
    f.tick(100);
    f.tick(2599);
    assert(f.installs == 0);
    f.tick(2600);
    assert(f.installs == 1 && r::occupied(f.actor) && f.front_permitted());
    const auto first_goal = f.goal(f.node(f.root()).children.front());
    assert(m::same(first_goal, {100, 120}));
    Fixture::pos(f.actor, first_goal);
    f.tick(2700);
    f.resume();
    assert(r::snapshot_stats().resumed == 1 && !r::occupied(f.actor));
    f.tick(2750);
    f.tick(5250);
    assert(f.installs == 2 && !m::same(f.goal(f.node(f.root()).children.front()), first_goal));
    f.tick(11249);
    assert(f.node(f.root()).children.size() == 2);
    f.tick(11250);
    f.resume();
    f.tick(11500);
    f.tick(15000);
    f.tick(35000);
    assert(f.installs == 2 && r::snapshot_stats().resumed == 2);
}
void movement_abandon() {
    Fixture f;
    f.tick(0);
    f.tick(2500);
    auto player = f.point(destination); // Even the same coordinates are a new order.
    f.replace(player);
    f.tick(2600);
    f.tick(3100);
    assert(f.root() == player && f.installs == 1 && !r::occupied(f.actor) && r::snapshot_stats().aborted == 1);
    f.tick(5600);
    assert(f.installs == 2);
    auto temp = f.node(f.root()).children.front();
    f.node(temp).task.put<XY>(0x10, {130, 134});
    assert(!f.front_permitted());
    f.tick(5700);
    assert(!r::occupied(f.actor) && !f.front_permitted() && r::snapshot_stats().resumed == 0);
}
void traps_and_resets() {
    Fixture f;
    f.passable = false;
    f.tick(100);
    f.tick(2600);
    f.tick(2601);
    f.tick(5101);
    f.tick(30000);
    assert(f.probes == 2 && !f.installs);
    const auto* row = r::detail::control.find(f.actor);
    assert(row && row->history.attempts == 2 && row->history.cooldown_until > 25.);
    f.passable = true;
    f.tick(1200);
    f.tick(3699);
    assert(!f.installs);
    f.tick(3700);
    assert(f.installs == 1);
    f.w.scene = f.second_scene.ptr();
    f.tick(3800);
    assert(!r::occupied(f.actor) && !f.front_permitted());
    f.replace(f.point(destination));
    f.tick(3900);
    f.tick(6400);
    assert(f.installs == 2);
    f.pop(f.node(f.root())); // Natural completion in stock complex update.
    f.resume();
}
void movement_admission() {
    Fixture f;
    unsigned now = 1000;
    auto denied = [&] {
        f.tick(now);
        f.tick(now + 3000);
        now += 3100;
        assert(f.installs == 0);
    };
    f.worker = true;
    denied();
    f.worker = false;
    f.remote = true;
    denied();
    f.remote = false;
    f.serial = false;
    denied();
    f.serial = true;
    // Direct Control/inhibition (bit 0), dispatch block (bit 20), and inside-Exec (bit 24) stay denied.
    for (auto flag : {1u, 1u << 20, 1u << 24}) {
        f.human.brain.put<unsigned>(0x39C, flag);
        denied();
    }
    f.human.brain.put<unsigned>(0x39C, 0);
    // Idle Hold Position (no active move order) never triggers movement.
    const Address saved_root = f.root();
    f.human.brain.put<unsigned>(0x398, 1u | (1u << 12));
    f.human.brain.put<Address>(0xF0, 0);
    denied();
    f.human.brain.put<Address>(0xF0, saved_root);
    // Active player move (bit 28) on Hold Position IS eligible for doorway stall recovery and resumes destination.
    f.human.brain.put<unsigned>(0x39C, 1u << 28);
    f.tick(now);
    f.tick(now + 2600);
    now += 3100;
    assert(f.installs == 1 && r::occupied(f.actor));
    r::preempt(f.actor);
    f.installs = 0;
    f.replace(f.point(destination));
    f.human.brain.put<unsigned>(0x398, 1);
    f.human.brain.put<unsigned>(0x39C, 0);
    f.human.chassis.put<unsigned>(0x18, 1);
    denied();
    f.human.chassis.put<unsigned>(0x18, 0);
    f.weaponry.put<unsigned>(0x70, 2);
    denied();
    f.weaponry.put<unsigned>(0x70, 0);
    f.human.vit.put<unsigned>(0x174, 1);
    denied();
    f.human.vit.put<unsigned>(0x174, 0);
    Memory<0x220> gun{};
    Memory<0x20> charger{}, shooter{};
    gun.put<Address>(0x90, f.actor);
    gun.put<Address>(0xB0, charger.ptr());
    gun.put<Address>(0xB8, shooter.ptr());
    Address entry = gun.ptr();
    f.weaponry.put<Address>(0x30, reinterpret_cast<Address>(&entry));
    f.weaponry.put<Address>(0x38, reinterpret_cast<Address>(&entry + 1));
    gun.put<int>(0xD0, 1);
    denied();
    gun.put<int>(0xD0, 0);
    charger.put<m::Byte>(0x14, 4);
    denied();
    charger.put<m::Byte>(0x14, 0);
    shooter.put<m::Byte>(0x15, 1);
    denied();
    shooter.put<m::Byte>(0x15, 0);
    f.replace(0);
    denied();
    f.replace(f.point({129.99f, 100}));
    denied();
    const auto& vt = rt::table().vt;
    for (Address forbidden : {vt.attack_order, vt.medic_order, vt.smart_repair_order, vt.repair_order, vt.board_order,
                              vt.emit_order, vt.avoid_grenade, vt.move_to_cover}) {
        auto& parent = f.make(forbidden);
        parent.mem.put<Address>(0x30, f.point(destination));
        f.replace(parent.ptr());
        denied();
    }
    auto& seq = f.make(m::sequence_vt);
    m::native.push(seq.ptr(), f.point(destination), 0);
    m::native.push(seq.ptr(), f.point(destination), 0);
    f.replace(seq.ptr());
    denied();
    f.replace(f.point(destination));
    co::instance().claim(f.actor, 1, co::Module::auto_medic, co::Priority::auto_medic);
    denied();
    co::instance().release(f.actor, co::Module::auto_medic);
    goh::config::active().modules.movement_recovery = false;
    denied();
}
void source_and_claim_races() {
    Fixture f;
    Address route = f.make(rt::table().vt.move_order).ptr();
    std::array<s::Vec3, 2> pts{{{100, 100, 5}, {400, 100, 5}}};
    auto& n = f.node(route);
    n.mem.put<Address>(0x40, reinterpret_cast<Address>(pts.data()));
    n.mem.put<Address>(0x48, reinterpret_cast<Address>(pts.data() + pts.size()));
    f.replace(route);
    assert(m::same(m::active_move(f.actor).dest, destination));
    n.mem.put<Address>(0x48, reinterpret_cast<Address>(pts.data()) + 13);
    assert(!m::active_move(f.actor).order);
    f.replace(f.point(destination));
    f.node(f.root()).task.put<Address>(0, 0);
    assert(!m::active_move(f.actor).order);
    f.replace(f.point(destination));
    f.tick(0);
    const auto player = f.replace_at_factory = f.point({470, 171});
    f.tick(2500);
    assert(f.root() == player && !f.installs && s::quiescent());
    f.tick(2600);
    f.tick(5100);
    assert(r::occupied(f.actor));
    assert(co::instance().claim(f.actor, 1, co::Module::grenade_dodge, co::Priority::grenade_dodge));
    assert(!f.front_permitted() && !r::occupied(f.actor));
}
void cannon_push() {
    Fixture f(true);
    Memory<0xC40> cannon{};
    Address ext = cannon.ptr();
    f.extlist.put<Address>(0, reinterpret_cast<Address>(&ext));
    f.extlist.put<Address>(8, reinterpret_cast<Address>(&ext + 1));
    cannon.put<Address>(0, f.image.base() + rt::table().vt.cannon_ext);
    cannon.put<Address>(0x18, f.actor);
    auto tick = [&](unsigned now) {
        f.w.now = now;
        r::on_actor_post_quant(f.actor);
    };
    cannon.put<int>(0xC30, 2);
    tick(0);
    tick(3000);
    assert(!f.installs);
    cannon.put<int>(0xC30, 1);
    f.follower.actor.put<unsigned>(0x1C0, 1u << 28);
    tick(3100);
    tick(6100);
    assert(!f.installs);
    f.follower.actor.put<unsigned>(0x1C0, 0);
    tick(6200);
    tick(8700);
    assert(r::occupied(f.actor) && f.front_permitted() && !c::detail::allowed(f.w, f.actor));
}
void convoy_geometry() {
    assert(!c::blocking_distance(origin, {1, 0}, {105, 100}, scale, 8));
    assert(!c::blocking_distance(origin, {1, 0}, {180, 100}, scale, 8));
    assert(!c::blocking_distance(origin, {1, 0}, {160, 136}, scale, 8));
    assert(c::blocking_distance(origin, {1, 0}, {177, 90}, scale, 8));
    c::Following h{};
    c::Leader lead{1, {150, 100}, 5.f, 1.f, 1.f, 1.f, true};
    assert(h.sample(lead, .8f, .8f, 8, 0) == c::Action::none);
    assert(h.sample(lead, 2, 2, 8, 0) == c::Action::yield);
    lead.speed = .38f;
    lead.heading_dot = 0;
    assert(h.sample(lead, 0, 0, 8, 1) == c::Action::none);
    assert(h.sample(lead, 0, 0, 8, 3) == c::Action::try_pass);
    lead.id = 2;
    assert(h.sample(lead, 0, 0, 8, 3) == c::Action::none);
    h.destination(destination, scale);
    h.issued(true, 3);
    h.issued(true, 7);
    lead.speed = 0;
    h.sample(lead, 0, 0, 8, 8);
    assert(h.sample(lead, 0, 0, 8, 10.999) == c::Action::none && h.sample(lead, 0, 0, 8, 11) == c::Action::yield);
    h.destination({480, 100}, scale);
    assert(!h.passes && h.cooldown_until == 11);
    auto p = c::pick_pass_point({150, 100}, {1, 0}, scale, [](XY xy) { return xy.y < 40; });
    assert(p && m::same(*p, {190, 25}));
}
void yield_and_release() {
    Fixture f(true);
    f.tick(100);
    assert(f.installs == 1 && c::occupied(f.actor) && c::snapshot_stats().yields == 1);
    f.tick(400);
    f.tick(700);
    assert(f.node(f.root()).children.size() == 3);
    f.pop(f.node(f.root())); // A native wait completes while the gap remains closed.
    f.tick(1200);
    assert(f.node(f.root()).children.size() == 3 && !c::snapshot_stats().resumed);
    Fixture::pos(f.leader.actor.ptr(), {181, 100});
    f.tick(1450);
    assert(f.node(f.root()).children.size() == 3);
    f.tick(1700);
    f.resume();
    Fixture::pos(f.leader.actor.ptr(), {145, 100});
    f.tick(2000);
    f.tick(4099);
    assert(f.installs == 1 && !c::occupied(f.actor));
    f.tick(4350);
    assert(f.installs == 2 && c::snapshot_stats().resumed == 1);
}
void stopped_and_narrow() {
    Fixture f(true);
    f.leader_speed = {};
    f.speed = {};
    f.tick(0);
    f.tick(1499);
    assert(!f.installs);
    f.tick(1750);
    assert(c::snapshot_stats().passes == 1 && f.front_permitted());
    assert(f.node(f.root()).children.size() == 3);
    const auto shoulder = f.goal(f.node(f.root()).children.front());
    assert(shoulder.x == origin.x && shoulder.y != origin.y);
    Fixture::pos(f.actor, shoulder);
    f.pop(f.node(f.root()));
    assert(f.front_permitted());
    const auto pass_goal = f.goal(f.node(f.root()).children.front());
    assert(pass_goal.x == 185 && std::fabs(pass_goal.y - 100) >= 55);
    Fixture::pos(f.actor, pass_goal);
    f.tick(1800);
    f.resume();
    Fixture::pos(f.actor, origin);
    f.tick(2000);
    f.tick(3500);
    assert(f.installs == 1);
    f.tick(5750);
    assert(f.installs == 2 && c::snapshot_stats().passes == 2);
    f.finish_waypoints();
    f.resume();
    Fixture::pos(f.actor, origin);
    f.tick(6000);
    f.tick(9800);
    assert(f.installs == 3 && c::snapshot_stats().yields == 1);
}
void choke_and_override() {
    Fixture f(true);
    f.leader_speed = {};
    f.speed = {};
    f.passable = false;
    f.tick(0);
    f.tick(1500);
    assert(c::snapshot_stats().yields == 1 && !c::snapshot_stats().passes);
    f.tick(1750);
    f.tick(5500);
    assert(f.installs == 1);
    f.passable = true;
    f.tick(9500);
    assert(f.installs == 2 && c::snapshot_stats().passes == 1 && f.front_permitted());
    const auto player = f.point({177, 248});
    f.replace(player);
    f.tick(9700);
    assert(f.root() == player && !c::occupied(f.actor) && !c::snapshot_stats().resumed);
}
void convoy_vetoes_and_reset() {
    Fixture f(true);
    auto denied = [&](unsigned now) {
        f.tick(now);
        assert(!f.installs);
    };
    f.leader.actor.put<m::Byte>(0xDA8, 1);
    denied(0);
    f.leader.actor.put<m::Byte>(0xDA8, 0);
    f.speed = {-.3f, 0};
    denied(300);
    f.speed = {3, 0};
    f.worker = true;
    denied(600);
    f.worker = false;
    f.remote = true;
    denied(900);
    f.remote = false;
    f.weaponry.put<unsigned>(0x70, 2);
    denied(1200);
    f.weaponry.put<unsigned>(0x70, 0);
    f.follower.comp_rows.put<m::Byte>(0x28 + 0x24, 1);
    denied(1500);
    f.follower.comp_rows.put<m::Byte>(0x28 + 0x24, 0);
    f.follower.set_body_state(1);
    denied(1800);
    f.follower.set_body_state(0);
    f.follower.actor.put<m::Byte>(0xDAD, 2);
    denied(2100);
    f.follower.actor.put<m::Byte>(0xDAD, 0);
    f.replace(f.make(rt::table().vt.attack_order).ptr());
    denied(2400);
    f.replace(f.point({159, 100}));
    denied(2500);
    f.replace(f.point(destination));
    f.tick(2800);
    assert(c::occupied(f.actor));
    f.tick(100);
    assert(!c::occupied(f.actor) && c::snapshot_stats().resumed == 0);
    f.replace(f.point(destination));
    f.tick(200);
    assert(f.installs == 2);
    f.w.scene = f.second_scene.ptr();
    f.tick(600);
    assert(!c::occupied(f.actor));
    f.replace(f.point(destination));
    f.tick(700);
    assert(f.installs == 3);
    f.follower.brain.put<unsigned>(0x39C, 1);
    f.tick(850);
    assert(!c::occupied(f.actor));
    f.pop(f.node(f.root()));
    assert(!f.front_permitted());
}
void route_clearance() {
    Fixture f(true);
    FakeTank left{}, right{};
    left.setup(f.image.base(), 11, 11, 0);
    right.setup(f.image.base(), 12, 12, 1);
    const auto ahead = c::detail::leader(f.w, f.actor, {1, 0}, 8);
    assert(ahead.present);
    auto route = [&] { return c::detail::probe(f.w, f.actor, ahead, {1, 0}); };
    Fixture::pos(left.actor.ptr(), {100, 155});
    Fixture::pos(right.actor.ptr(), {100, 45});
    f.roster[2] = left.actor.ptr();
    auto single = route();
    assert(single && single->waypoint.y < 100 && single->shoulder);
    f.roster[3] = right.actor.ptr();
    assert(!route());
    f.roster[2] = f.roster[3] = 0;
    Fixture::pos(f.leader.actor.ptr(), {178, 132});
    const auto offcenter = c::detail::leader(f.w, f.actor, {1, 0}, 8);
    auto direct = c::detail::probe(f.w, f.actor, offcenter, {1, 0});
    assert(direct && !direct->shoulder);
    auto blocked = c::detail::passing_route(f.w, f.actor, offcenter, {1, 0}, [](s::Vec3, s::Vec3&) { return false; });
    assert(!blocked);
    // Force an occupied doorway's first lateral candidate to lose to its opposite.
    Fixture::pos(f.leader.actor.ptr(), {100, 120});
    auto alternative = r::detail::nudge_route(f.w, f.actor, destination, {}, [](s::Vec3, s::Vec3&) { return true; });
    assert(alternative && alternative->y < 100);
}
void transactional_failure_and_stop() {
    Fixture f;
    f.tick(0);
    const auto orig = f.root();
    m::native.point = [](const XY& p) { return m::same(p, destination) ? Address{} : live->point(p); };
    f.tick(2500);
    assert(!f.installs && orig == f.root() && s::quiescent());
    m::native.point = [](const XY& p) { return live->point(p); };
    f.tick(2600);
    f.tick(5100);
    assert(r::occupied(f.actor) && !r::quiescent());
    f.replace(0);
    f.tick(5200);
    f.tick(15000);
    assert(!f.root() && r::quiescent() && r::snapshot_stats().resumed == 0);
    f.replace(f.point({600, 100}));
    f.tick(16000);
    f.tick(20000);
    assert(f.installs == 1); // The two attempts also armed the per-actor 20 s cooldown.
    f.tick(26000);
    assert(f.installs == 2);
    r::begin_shutdown();
    assert(!f.front_permitted() && !r::occupied(f.actor));
}
void vehicle_combat_and_identity() {
    Fixture f(true);
    Memory<0x300> sensor{};
    Memory<0x80> enemy{};
    const Address sub = enemy.ptr();
    f.follower.actor.put<Address>(0xDD0, sensor.ptr());
    sensor.put<Address>(0x260, f.actor);
    sensor.put<Address>(0x280, reinterpret_cast<Address>(&sub));
    sensor.put<Address>(0x288, reinterpret_cast<Address>(&sub + 1));
    enemy.put<unsigned>(0x14, 2);
    enemy.put<unsigned>(0x10, (1u << 28) | (1u << 29));
    enemy.put<s::Vec3>(0x28, {150, 105, 5});
    f.speed = {-.1f, 0};
    assert(c::detail::reversing(f.w, f.actor, {1, 0}, f.speed));
    enemy.put<unsigned>(0x14, 0);
    assert(!c::detail::reversing(f.w, f.actor, {1, 0}, f.speed));
    f.speed = {3, 0};
    f.tick(100);
    assert(c::occupied(f.actor));
    f.follower.actor.put<unsigned>(0xC0, 17); // Same address and route, new live entity.
    assert(!c::occupied(f.actor));
    f.pop(f.node(f.root()));
    assert(!f.front_permitted());
    f.replace(f.point(destination));
    f.alive = false;
    f.tick(4500);
    assert(f.installs == 1);
}
} // namespace
int main() {
    stall_and_fan();
    movement_resume_and_cap();
    movement_abandon();
    traps_and_resets();
    movement_admission();
    source_and_claim_races();
    cannon_push();
    convoy_geometry();
    yield_and_release();
    stopped_and_narrow();
    choke_and_override();
    convoy_vetoes_and_reset();
    route_clearance();
    transactional_failure_and_stop();
    vehicle_combat_and_identity();
    std::puts("Stream D: 15 scenario groups passed");
}
