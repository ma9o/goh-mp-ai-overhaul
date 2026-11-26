// Offline build-26734796 actor, order, sensor and event-dispatch fixtures.
#include "../../mods/engine_fixes_module.hpp"
#include "vehicle_fixture_26734796.hpp"
#include <cstdio>

namespace ef = goh::engine_fixes;
namespace d = ef::detail;
namespace p = ef::policy;
namespace rt = goh::runtime;
namespace sim = goh::auto_smoke;
namespace s = goh::ai_support;
namespace pr = goh::target_prio::detail;
namespace rp = goh::tank_auto_repair::detail;
using goh::test::vehicle::Memory;
using rt::Address;
using rt::Byte;

namespace {
goh::test::vehicle::FakeImage& image() {
    static goh::test::vehicle::FakeImage pe;
    return pe;
}
goh::test::vehicle::FakeHuman h;
Memory<0x580> state;
Memory<0x300> sensor;
Memory<0x98> subject, other;
Memory<0x50> cover;
Memory<0x300> weapon, charger, order, suborder;
Memory<0x900> def, scene;
Memory<0x100> vt, child_vt, hand, box;
Memory<0x1100> target;
Memory<0x78> rows;
std::array<Address, 2> observations{};
Address hand_ptr{}, move_result{}, take_result{}, seq_result{}, scoped_actor{};
bool host = true, worker = false, empty_hand = true, inventory_busy = false, reject_interrupt = false;
bool child_finished = false, board_cancel = false, boarded = false, dry = false, reloading = false, selected = false;
int stock_calls{}, advances{}, finish_calls{}, deletes{}, attaches{}, events{}, names{}, names_freed{}, event_dtors{};
int remaining = 3, reload_calls{}, probes{}, rank_calls{}, rejected_link{}, seconds_calls{};
unsigned now = 300;
bool stock_true{}, reload_accepted{};
p::Point target_xy{}, moved_to{};
p::Sight rays[3]{};
struct Hooks {
    template <class Fn> static Address addr(Fn fn) {
        return reinterpret_cast<Address>(fn);
    }
    static Address resolve(Address, Address rva) {
        switch (rva) {
            case 0x7e05b0:
                return addr(+[](Address a) -> Byte {
                    assert(a == h.actor.ptr());
                    return inventory_busy;
                });
            case 0x7e04b0: return addr(+[](Address) { return hand_ptr; });
            case 0x92aa40:
                return addr(+[](Address st, Byte yes) -> Byte {
                    assert(st == state.ptr() && yes);
                    return !reject_interrupt;
                });
            case 0x901420:
                return addr(+[](Address name) {
                    assert(name == def.ptr() + 0x868);
                    return take_result;
                });
            case 0x92abb0: return addr(+[](Address) { return seq_result; });
            case 0x919270:
                return addr(+[](Address seq, Address item, int layer, Byte start) -> Address {
                    assert(seq == seq_result && item == take_result && !layer && start);
                    ++attaches;
                    return item;
                });
            case 0x90a8c0:
                return addr(+[](const p::Point* pt, unsigned mode) {
                    assert(!mode);
                    moved_to = *pt;
                    return move_result;
                });
            case 0x91b200: return addr(+[](Address) -> Byte { return selected; });
            case 0x564e40:
                return addr(+[](void*, const char* name) {
                    assert(std::string_view(name) == "break_window");
                    ++names;
                });
            case 0x565010: return addr(+[](void*) { ++names_freed; });
            case 0x780e60:
                return addr(+[](void* event, Address ent) {
                    assert(ent == box.ptr());
                    return reinterpret_cast<Address>(event);
                });
            case 0x780f80:
                return addr(+[](Address event, Address vol) {
                    assert(vol == hand.ptr() || vol == cover.ptr());
                    return event;
                });
            case 0x781240:
                return addr(+[](Address, const void*) {
                    ++events;
                    image().put<Address>(0x15ae8d8, 0);
                });
            case 0x780ef0: return addr(+[](void*) { ++event_dtors; });
            case 0x81b080:
                return addr(+[](Address w) {
                    assert(w == h.weaponry.ptr());
                    return weapon.ptr();
                });
            case 0x820ce0: return addr(+[](Address) -> Byte { return reloading; });
            case 0x820c50: return addr(+[](Address) -> Byte { return dry; });
            case 0x7dc450:
                return addr(+[](Address m) {
                    assert(m == box.ptr());
                    return remaining;
                });
            case 0x95c530:
                return addr(+[](Address b, float ratio) -> Byte {
                    assert(b == h.brain.ptr() && ratio == goh::config::active().tuning.lull_reload_ratio);
                    ++reload_calls;
                    return reload_accepted;
                });
            case 0x8f3980:
                return addr(+[](Address) -> std::uint64_t {
                    ++advances;
                    return 6;
                });
            case 0x8f3910: return addr(+[](Address) -> Byte { return boarded; });
            case 0x7a70d0:
                return addr(+[](Address ref) {
                    assert(ref == order.ptr() + 0x40);
                    return reinterpret_cast<Address>(&target_xy);
                });
            case 0x66e7b0: return addr(+[]() { return now; });
            case 0x66e7c0:
                return addr(+[](int dt) {
                    ++seconds_calls;
                    return dt / 10.f;
                });
            case 0x4c1cb0:
                return addr(+[](Address parent, Address tag) -> Byte {
                    assert(parent == box.ptr() && tag == image().base() + 0x159d880);
                    return rejected_link;
                });
            default: std::abort();
        }
    }
};
Byte stock_test(Address) {
    ++stock_calls;
    return stock_true;
}
std::uint64_t stock_exec(Address, int) {
    ++stock_calls;
    return 7;
}
void setup() {
    h.setup(image().base(), 8, 45, 1, 0);
    scoped_actor = h.actor.ptr();
    h.brain.put(0xe0, state.ptr());
    state.put(0, image().base() + 0x1110cd0);
    state.put(0x30, h.brain.ptr());
    h.actor.put(0xdc0, cover.ptr());
    h.actor.put(0xdd0, sensor.ptr());
    sensor.put(0x260, h.actor.ptr());
    observations = {subject.ptr(), other.ptr()};
    sensor.put(0x280, reinterpret_cast<Address>(observations.data()));
    sensor.put(0x288, reinterpret_cast<Address>(observations.data() + 2));
    image().put(0x15c9668, scene.ptr());
    image().put<float>(0x149e750, 20);
    image().put<float>(0x149e758, 10);
    image().put<float>(0x1042c90, 32);
    image().put<float>(0x1023908, .5f);
    image().put<float>(0x102af14, 3);
    ef::image = s::base = rp::base = image().base();
    ef::ops.serial_actor = +[]() { return scoped_actor; };
    ef::ops.host_main = +[](Address) { return host && !worker; };
    ef::ops.resolve = &Hooks::resolve;
    ef::gate.activate();
    rt::global_accepting = true;
    d::original = {stock_test,
                   +[](Address) -> Address {
                       ++stock_calls;
                       return 0;
                   },
                   stock_exec,
                   +[](Address, Address, Address, Byte) -> Byte {
                       ++stock_calls;
                       return stock_true;
                   },
                   +[](Address, int) -> Byte {
                       ++stock_calls;
                       return stock_true;
                   },
                   +[](Address, int* out) -> Byte {
                       ++stock_calls;
                       if (out) *out = stock_true ? 1 : -1;
                       return stock_true;
                   },
                   stock_exec,
                   +[](Address, Address) -> Byte {
                       ++stock_calls;
                       return stock_true;
                   }};
    order.put(8, h.actor.ptr() + 0xaf0);
    order.put(0, vt.ptr());
    vt.put(0, Hooks::addr(+[](Address, unsigned f) -> Address {
               assert(f == 1);
               ++deletes;
               return 0;
           }));
    vt.put(0xc0, Hooks::addr(+[](Address) -> Byte { return board_cancel; }));
}
void lifecycle() {
    std::vector<void*> created;
    int fail_at = 0;
    auto create = [&](void* at, void* hook, void** trampoline) {
        assert(std::find(created.begin(), created.end(), at) == created.end());
        if (static_cast<int>(created.size()) == fail_at) return 1;
        created.push_back(at);
        *trampoline = hook;
        return 0;
    };
    auto remove = [&](void* at) {
        assert(created.back() == at);
        created.pop_back();
        return 0;
    };
    assert(!ef::prepare_hooks(image().base(), "other build", create, remove) && created.empty());
    for (; fail_at < 8; ++fail_at) {
        assert(!ef::prepare_hooks(image().base(), rt::kBuild26734796Sha256, create, remove));
        assert(created.empty() && !d::installed && !ef::gate.active());
    }
    assert(ef::prepare_hooks(image().base(), rt::kBuild26734796Sha256, create, remove));
    assert(created.size() == 8 && !ef::prepare_hooks(image().base(), rt::kBuild26734796Sha256, create, remove));
    ef::after_hooks_removed();
    created.clear();
    fail_at = 3;
    assert(!ef::prepare_hooks(image().base(), rt::kBuild26734796Sha256, create, [](void*) { return 1; }));
    assert(d::rollback_pending && !ef::prepare_hooks(image().base(), rt::kBuild26734796Sha256, create, remove));
    ef::after_hooks_removed();
}
void healing() {
    hand_ptr = hand.ptr();
    hand.put(0, child_vt.ptr());
    child_vt.put(0x30, Hooks::addr(+[](Address) -> Byte { return empty_hand; }));
    h.actor.put(0x12b0, def.ptr());
    seq_result = box.ptr();
    take_result = order.ptr();
    for (int disabled = 0; disabled < 9; ++disabled) {
        auto& cfg = goh::config::active().modules;
        cfg.engine_fixes = disabled != 0;
        host = disabled != 1;
        worker = disabled == 2;
        h.brain.put<unsigned>(0x39c, disabled == 3 ? 1 : 0);
        h.chassis.put<int>(0x18, disabled == 4);
        h.weaponry.put<unsigned>(0x70, disabled == 5 ? 2 : 0);
        scoped_actor = disabled == 6 ? 0 : h.actor.ptr();
        h.actor.put<Byte>(0x12b8, disabled == 7);
        inventory_busy = disabled == 8;
        const int before = stock_calls;
        assert(!d::heal(state.ptr()) && stock_calls == before + 1 && !attaches &&
               h.actor.get<Address>(0x12b0) == def.ptr());
    }
    inventory_busy = false;
    h.brain.put(0, image().base() + rt::table().vt.vehicle_brain);
    assert(!d::heal(state.ptr()) && !attaches);
    h.brain.put(0, image().base() + rt::table().vt.human_brain);
    h.brain.put<unsigned>(0x39c, 1u << 28);
    assert(!d::heal(state.ptr()) && !attaches);
    h.brain.put<unsigned>(0x39c, 0);
    reject_interrupt = true;
    assert(!d::heal(state.ptr()) && !attaches);
    reject_interrupt = false;
    empty_hand = false;
    assert(!d::heal(state.ptr()) && !h.actor.get<Address>(0x12b0));
    empty_hand = true;
    h.actor.put(0x12b0, def.ptr());
    assert(d::heal(state.ptr()) && attaches == 1 && ef::stats.heal == 1 && !h.actor.get<Address>(0x12b0));
    h.actor.put(0x12b0, def.ptr());
    seq_result = 0;
    assert(!d::heal(state.ptr()) && deletes == 1 && attaches == 1);
    h.actor.put(0x12b0, def.ptr());
    take_result = 0;
    assert(!d::heal(state.ptr()) && !h.actor.get<Address>(0x12b0));
}
void burn_escape() {
    p::Circle circles[] = {{{1, 0}, 2}, {{-3, 0}, 1}};
    p::Point goal{}, enemy{0, 10};
    auto clear = [](p::Point) { return true; };
    assert(p::choose_burn_escape({}, circles, 1, nullptr, 1, clear, goal) && goal.x < -1.49f);
    assert(p::choose_burn_escape({}, circles, 2, &enemy, 1, clear, goal) && goal.y < 0 &&
           p::squared(goal, circles[1].center) > 1.35f * 1.35f);
    assert(!p::choose_burn_escape({8, 8}, circles, 2, nullptr, 1, clear, goal));
    assert(!p::choose_burn_escape({}, circles, 1, nullptr, 1, [](p::Point) { return false; }, goal));
    circles[0] = {{0, 0}, 2};
    assert(p::choose_burn_escape(
        {}, circles, 1, nullptr, 1,
        [](p::Point pt) { return std::fabs(pt.x - 3.25f) < .001f && std::fabs(pt.y) < .001f; }, goal));
    circles[0] = {{0, 0}, 11};
    assert(p::choose_burn_escape({}, circles, 1, nullptr, 1, clear, goal) && std::fabs(goal.x - 12.25f) < .001f);
    Memory<0x40> scanner, fire;
    scanner.put(8, h.brain.ptr());
    scanner.put<Byte>(0x10, 1);
    scanner.put(0x18, fire.ptr());
    scanner.put(0x20, fire.ptr() + 0x18);
    fire.put(8, p::Point{1, 2});
    fire.put(0x10, 4.f);
    d::queries.choose_route =
        +[](const sim::World& w, Address a, const d::Circles& cs, std::size_t n, const p::Point*, p::Point& dest) {
            assert(w.length == 20 && a == h.actor.ptr() && n == 1 && cs[0].radius == 4 && cs[0].center.y == 2);
            dest = {30, 40};
            return true;
        };
    assert(!d::burn(scanner.ptr()));
    move_result = order.ptr();
    assert(d::burn(scanner.ptr()) == move_result && moved_to.y == 40 && ef::stats.burn == 1);
    scanner.put(0x20, fire.ptr() + 0x19);
    assert(!d::burn(scanner.ptr()) && ef::stats.burn == 1);
    d::queries.choose_route = &d::route;
}
void repairs() {
    target.put(0xb90, rows.ptr());
    target.put(0xb98, rows.ptr() + 0x78);
    order.put(0x68, target.ptr());
    order.put(0xd0, rows.ptr());
    order.put(0x118, box.ptr());
    order.put(0x38, 5);
    rows.put<int>(0x10, 8);
    assert(d::repair(order.ptr(), 1) == 2 && ef::stats.kit == 1);
    assert(d::repair(order.ptr(), 2) == 7);
    rows.put<int>(0x10, 0);
    assert(d::repair(order.ptr(), 1) == 7);
    rows.put<Byte>(0x24, 1);
    assert(d::repair(order.ptr(), 1) == 2);
    order.put<Byte>(0x131, 1);
    assert(d::repair(order.ptr(), 1) == 7);
    order.put<Byte>(0x131, 0);
    order.put(0xd0, rows.ptr() + 1);
    assert(d::repair(order.ptr(), 1) == 7);
    assert(order.get<Address>(0x118) == box.ptr());
    rp::Claims claims{};
    claims.used[0] = true;
    const auto span = s::span(target.ptr(), 0xb90);
    auto pick = [&](s::Span rejected = s::Span{}, Address stock = 0) {
        return rp::unclaimed_component(stock ? stock : rows.ptr(), span, rejected, claims,
                                       [](Address) { return true; });
    };
    rows.put(0, def.ptr());
    rows.put(0x28, def.ptr());
    rows.put(0x50, def.ptr() + 8);
    rows.put<Byte>(0x28 + 0x24, 1);
    def.put<int>(0x344, 10);
    def.put<int>(0x34c, -3);
    assert(pick() == rows.ptr() + 0x28);
    rows.put<Byte>(0x50 + 0x24, 1);
    assert(pick() == rows.ptr() + 0x50);
    Address rejected = rows.ptr() + 0x50;
    assert(pick({reinterpret_cast<Address>(&rejected), reinterpret_cast<Address>(&rejected + 1)}) == rows.ptr() + 0x28);
    rows.put<Byte>(0x28 + 0x25, 1);
    claims.used[2] = true;
    assert(pick() == rows.ptr() && pick({}, rows.ptr() + 1) == rows.ptr() + 1);

    // A second actor is repairing the stock row through its own live order.
    goh::test::vehicle::FakeHuman helper;
    helper.setup(image().base(), 9, 46, 1, 0);
    Memory<0x140> repair_order;
    helper.brain.put(0xf0, repair_order.ptr());
    repair_order.put(0, image().base() + rp::repair_vt);
    repair_order.put(8, helper.actor.ptr() + 0xaf0);
    repair_order.put(0x68, target.ptr());
    repair_order.put(0xd0, rows.ptr());
    target.put<Byte>(0x30, 3);
    std::array<Address, 2> actors{h.actor.ptr(), helper.actor.ptr()};
    scene.put(0x48, reinterpret_cast<Address>(actors.data()));
    scene.put(0x50, reinterpret_cast<Address>(actors.data() + actors.size()));
    const Address arg = order.ptr();
    rp::original_picker = +[](const Address*) {
        ++stock_calls;
        return rows.ptr();
    };
    const auto calls = stock_calls;
    const auto good = +[](Address) { return true; };
    assert(rp::global_pick(&arg, good) == rows.ptr() + 0x50);
    assert(stock_calls == calls + 1 && goh::tank_auto_repair::stat_picker_overrides == 1);
    repair_order.put<Address>(0xd0, 0);
    assert(rp::global_pick(&arg, good) == rows.ptr() && stock_calls == calls + 2);
    worker = true;
    assert(rp::pick(&arg) == rows.ptr() && stock_calls == calls + 3);
    worker = false;
    scene.put<Address>(0x48, 0);
    scene.put<Address>(0x50, 0);
}
void windows() {
    Memory<0x90> hits;
    Memory<0x30> aimer;
    aimer.put(0x10, weapon.ptr());
    weapon.put(0x90, h.actor.ptr());
    for (unsigned off : {0u, 0x48u}) {
        hits.put<int>(off, 4);
        hits.put(off + 0x18, box.ptr());
        hits.put(off + 0x20, off ? cover.ptr() : hand.ptr());
    }
    hand.put<unsigned>(0xc, 1 << 13);
    cover.put<unsigned>(0xc, 1 << 13);
    auto run = [&](unsigned size, Byte may = 1) {
        image().put(0x15ae8d8, hits.ptr());
        image().put(0x15ae8e0, hits.ptr() + size);
        return d::window(aimer.ptr(), 0, 0, may);
    };
    stock_true = true;
    assert(run(0x48) && run(0x8f) && run(0x90, 0));
    hits.put<int>(0x48, 3);
    assert(run(0x90) && events == 0);
    hits.put<int>(0x48, 4);
    cover.put<Byte>(0x28, 0x20);
    assert(run(0x90) && !events);
    cover.put<Byte>(0x28, 0);
    assert(!run(0x90) && events == 2 && event_dtors == 2 && names == 1 && names_freed == 1 && ef::stats.window == 2);
    stock_true = false;
    assert(!run(0x90) && events == 2);
}
void stances() {
    h.brain.put(0x3b8, subject.ptr());
    subject.put<unsigned>(0x10, 1u << 29);
    h.chassis.put<int>(0x2c, 1);
    d::queries.probe_stance = +[](const ef::ti::Context&, int pose) {
        ++probes;
        return rays[pose];
    };
    int out = -1;
    assert(d::select(h.brain.ptr(), &out) && out == 0 && !probes);
    assert(d::allow(h.brain.ptr(), 0));
    subject.put(0x28, pr::Vec3{150, 0, 0}); // Exactly 7.5 m uses the ray gates.
    rays[0] = rays[2] = p::Sight::clear;
    rays[1] = p::Sight::blocked;
    assert(d::select(h.brain.ptr(), &out) && out == 2);
    assert(d::allow(h.brain.ptr(), 2) && !d::allow(h.brain.ptr(), 1));
    rays[2] = p::Sight::blocked;
    assert(d::select(h.brain.ptr(), &out) && out == 0);
    rays[1] = p::Sight::unknown;
    assert(!d::select(h.brain.ptr(), &out) && out == -1);
    selected = true;
    assert(!d::allow(h.brain.ptr(), 0));
    selected = false;
    stock_true = true;
    int before = probes;
    assert(d::select(h.brain.ptr(), &out) && out == 1 && probes == before);
    stock_true = false;
    d::queries.probe_stance = &d::stance_ray;
}
void reloads() {
    h.brain.put<Address>(0x3b8, 0);
    subject.put<unsigned>(0x10, 0);
    weapon.put(0, child_vt.ptr());
    weapon.put(0x98, def.ptr());
    weapon.put(0xb0, charger.ptr());
    child_vt.put(0x18, Hooks::addr(+[](Address) -> Address { return box.ptr(); }));
    def.put<int>(0x470, 10);
    sim::World w{image().base(), scene.ptr(), 100, 20, 10};
    auto tick = [&]() { ef::on_human_post_quant(w, h.actor.ptr()); };
    auto& cfg = goh::config::active();
    cfg.modules.lull_reload = false;
    tick();
    cfg.modules.lull_reload = true;
    cfg.modules.engine_fixes = false;
    tick();
    cfg.modules.engine_fixes = true;
    assert(reload_calls == 0);
    other.put<unsigned>(0x14, 3);
    other.put<unsigned>(0x10, 1u << 29);
    other.put(0x28, pr::Vec3{700, 0, 0});
    tick();
    assert(!reload_calls);
    other.put(0x28, pr::Vec3{701, 0, 0});
    remaining = 0;
    tick();
    remaining = 4;
    tick();
    remaining = 3;
    reloading = true;
    tick();
    reloading = false;
    charger.put<Byte>(0x14, 4);
    tick();
    charger.put<Byte>(0x14, 0);
    def.put<int>(0x470, 3);
    tick();
    def.put<int>(0x470, 10);
    h.brain.put(0x3b8, subject.ptr());
    subject.put<unsigned>(0x64, 81);
    tick();
    assert(!reload_calls);
    cover.put<int>(0x24, 1);
    assert(goh::coord::instance().claim(h.actor.ptr(), 8, goh::coord::Module::grenade_dodge,
                                        goh::coord::Priority::grenade_dodge));
    tick();
    assert(!reload_calls);
    goh::coord::instance().release(h.actor.ptr(), goh::coord::Module::grenade_dodge);
    tick();
    assert(reload_calls == 1 && ef::stats.reload == 0);
    cover.put<int>(0x24, 0);
    w.now = 139;
    tick();
    assert(reload_calls == 1);
    w.now = 140;
    reload_accepted = true;
    tick();
    assert(reload_calls == 2 && ef::stats.reload == 1);
    w.now = 110;
    tick();
    assert(reload_calls == 3);
    w.scene = def.ptr();
    tick();
    assert(reload_calls == 4);
    w.now = 200;
    h.brain.put<unsigned>(0x39c, 1u << 28);
    tick();
    assert(reload_calls == 4);
    h.brain.put<unsigned>(0x39c, 0);
    h.brain.put(0xf0, order.ptr());
    tick();
    assert(reload_calls == 4);
    h.brain.put<Address>(0xf0, 0);
    h.brain.put<Address>(0x3b8, 0);
}
void order_updates() {
    sim::original_smoke_fire_exec = +[](Address, unsigned) -> unsigned {
        ++stock_calls;
        return 9u;
    };
    auto fire = [&](unsigned phase) {
        order.put<int>(0xc4, 16);
        assert(sim::smoke_fire_exec(order.ptr(), phase) == 9);
        return order.get<int>(0xc4);
    };
    dry = true;
    assert(fire(1) == 16);
    charger.put<Byte>(0x14, 4);
    assert(fire(1) == 0);
    charger.put<Byte>(0x14, 0);
    dry = false;
    assert(fire(1) == 0 && fire(2) == 16);
    h.brain.put<unsigned>(0x398, 0);
    assert(fire(1) == 16);
    h.brain.put<unsigned>(0x398, 0x1001);
    ef::ti::original.replan = +[](Address) -> Byte {
        ++stock_calls;
        return 1;
    };
    state.put(0x3cc, p::Point{});
    h.brain.put(0x138, p::Point{12, 0});
    assert(!ef::ti::replan(state.ptr()));
    h.brain.put(0x138, p::Point{41, 0});
    assert(ef::ti::replan(state.ptr()));
    host = false;
    assert(ef::ti::replan(state.ptr()));
    host = true;
    assert(d::board(order.ptr(), 0) == 7 && d::board(order.ptr(), 2) == 7);
    order.put(0x38, 3);
    order.put(0x30, suborder.ptr());
    suborder.put(0, child_vt.ptr());
    child_vt.put(0x18, Hooks::addr(+[](Address) -> int { return child_finished; }));
    child_vt.put(0x20, Hooks::addr(+[](Address) { ++finish_calls; }));
    child_vt.put(0, Hooks::addr(+[](Address, int flag) {
                     assert(flag == 1);
                     ++deletes;
                 }));
    order.put(0xf4, p::Point{});
    target_xy = {32, 0};
    assert(!d::board(order.ptr(), 1) && ef::stats.board == 1);
    target_xy = {33, 0};
    assert(d::board(order.ptr(), 1) == 6 && advances == 1);
    order.put(0x38, 1);
    order.put<float>(0xe8, 40);
    assert(!d::board(order.ptr(), 1) && ef::stats.board == 2);
    h.actor.put(0x74, p::Point{150, 0});
    assert(!d::board(order.ptr(), 1) && ef::stats.board == 2);
    h.actor.put(0x74, p::Point{});
    boarded = true;
    assert(d::board(order.ptr(), 1) == 6);
    boarded = false;
    board_cancel = true;
    assert(d::board(order.ptr(), 1) == 2);
    board_cancel = false;
    child_finished = true;
    const int before = deletes;
    assert(d::board(order.ptr(), 1) == 6 && finish_calls == 1 && deletes == before + 1 && !order.get<Address>(0x30));
}
void eligibility() {
    auto check = [&]() { return d::eligible(h.brain.ptr(), subject.ptr()); };
    subject.put(0x18, target.ptr());
    subject.put<unsigned>(0x14, 3);
    subject.put<unsigned>(0x70, now);
    subject.put<unsigned>(0x64, now - 200);
    assert(check());
    subject.put<unsigned>(0x64, now - 201);
    assert(!check());
    subject.put<unsigned>(0x64, now - 101);
    subject.put<Byte>(0x6c, 1);
    assert(!check());
    subject.put<Byte>(0x6c, 0);
    subject.put<unsigned>(0x70, now + 1);
    assert(!check());
    subject.put<unsigned>(0x70, now);
    h.brain.put<unsigned>(0x398, 0);
    assert(!check());
    target.put<Byte>(0x30, 0xb);
    target.put<Byte>(0xdad, 2);
    target.put<unsigned>(0xdb4, 0x100);
    assert(check());
    target.put<unsigned>(0xdb4, 0);
    assert(!check());
    target.put<unsigned>(0xdb4, 0x100);
    target.put<unsigned>(0x38, 0x20);
    assert(!check());
    target.put<unsigned>(0x38, 0);
    Memory<0x60> tags;
    tags.ename(0, "abandoned");
    target.put(0x4e8, tags.ptr());
    target.put(0x4f0, tags.ptr() + 0x60);
    assert(!check());
    target.put(0x4f0, tags.ptr());
    target.put<unsigned>(0x1c0, 1u << 28);
    target.put(0x560, h.link.ptr());
    h.link.put(8, box.ptr());
    rejected_link = 1;
    assert(!check());
    rejected_link = 0;
    assert(check());
    subject.put<unsigned>(0x14, 1);
    assert(!check());
    subject.put<unsigned>(0x14, 1u << 10);
    subject.put<unsigned>(0x10, 1u << 28);
    assert(check());
    subject.put<unsigned>(0x14, 0);
    subject.put(0x40, box.ptr());
    subject.put<unsigned>(0x5c, now - 30);
    assert(!check());
    subject.put<unsigned>(0x5c, now - 29);
    assert(check());
    stock_true = true;
    auto before = seconds_calls;
    assert(check() && before == seconds_calls);
    stock_true = false;
    h.brain.put<unsigned>(0x398, 0x1001);
}
void cutoff() {
    subject.put(0x28, pr::Vec3{2, 0, 0});
    other.put(0x28, pr::Vec3{32, 0, 0});
    h.brain.put<float>(0x14c, 32);
    std::array<Address, 4> storage{subject.ptr(), other.ptr(), other.ptr(), subject.ptr()};
    pr::SubjectPtrVec v{storage.data(), storage.data() + 3, storage.data() + 4};
    pr::CutoffCandidates before{};
    before.capture(v, h.actor.ptr(), h.brain.ptr());
    v.end = v.begin + 1;
    before.restore(v);
    assert(v.end == v.begin + 2 && v.begin[1] == other.ptr());
    before.restore(v);
    assert(v.end == v.begin + 2);
    --v.end;
    --v.cap;
    before.restore(v);
    assert(v.end == v.begin + 1);
    v.cap = v.begin + 4;
    pr::CutoffCandidates none{};
    none.capture(v, h.actor.ptr(), h.brain.ptr());
    assert(!none.count);
    pr::orig_rank = +[](pr::SubjectPtrVec* vec, Address) {
        ++rank_calls;
        vec->end = vec->begin + 1;
    };
    pr::depth = 0;
    v.end = v.begin + 2;
    pr::ranked(&v, h.actor.ptr());
    assert(rank_calls == 1 && v.end == v.begin + 1);

    // Route the two shared role queries to real, ABI-matched fixture functions.
    // An empty stock result also covers the native profile rejecting the near candidate.
    const auto saved_auth = rt::table().auth;
    pr::base = image().base();
    rt::table().auth.is_remote_client = Hooks::addr(+[]() -> Byte { return !host; }) - pr::base;
    rt::table().auth.is_quant_worker = Hooks::addr(+[]() -> Byte { return worker; }) - pr::base;
    pr::scope.actor = h.actor.ptr();
    pr::depth = 1;
    pr::hook_gate.activate();
    pr::orig_rank = +[](pr::SubjectPtrVec* vec, Address) {
        ++rank_calls;
        vec->end = vec->begin;
    };
    auto run = [&]() {
        storage = {subject.ptr(), other.ptr(), other.ptr(), 0};
        v.end = v.begin + 3;
        const int calls = rank_calls;
        pr::ranked(&v, h.actor.ptr());
        assert(rank_calls == calls + 1);
    };
    run();
    assert(v.end == v.begin + 1 && v.begin[0] == other.ptr());
    worker = true;
    run();
    assert(v.end == v.begin);
    worker = false;
    auto& modules = goh::config::active().modules;
    modules.engine_fixes = false;
    run();
    assert(v.end == v.begin);
    modules.engine_fixes = true;
    modules.target_prioritization = false;
    run();
    assert(v.end == v.begin);
    modules.target_prioritization = true;
    pr::depth = 0;
    pr::scope.actor = 0;
    pr::hook_gate.deactivate();
    rt::table().auth = saved_auth;
}
} // namespace
int main() {
    lifecycle();
    setup();
    healing();
    burn_escape();
    repairs();
    windows();
    stances();
    reloads();
    order_updates();
    eligibility();
    cutoff();
    assert(!ef::inflight_count() && !rt::global_inflight && !ef::runtime_allowed);
    ef::after_hooks_removed();
    assert(!ef::gate.active() && d::attempts.empty() && !ef::stats.heal);
    std::puts("Engine fixes: all build-26734796 fixtures passed.");
}
