#pragma once
#include "engine_fixes_shared.hpp"
#include "engine_fixes_policy.hpp"
#include "hit_the_dirt_module.hpp"
#include "tactical_infantry_module.hpp"

namespace goh::engine_fixes {
namespace s = ai_support;
namespace sim = auto_smoke;
namespace ti = tactical_infantry::detail;
namespace pr = target_prio::detail;
namespace p = policy;
inline constexpr bool runtime_allowed = false;
inline constexpr unsigned hook_count = 8;
struct Counters {
    std::atomic<unsigned> heal{}, burn{}, kit{}, window{}, stance{}, reload{}, board{}, eligible{};
    std::atomic<unsigned> allegiance_veto{}, towed_gun_suppress{};
};
inline Counters stats{};
namespace detail {
using Unary = Address (*)(Address);
using Test = Byte (*)(Address);
using Exec = std::uint64_t (*)(Address, int);
struct Originals {
    Test heal{};
    Unary burn{};
    Exec repair{};
    Byte (*window)(Address, Address, Address, Byte){};
    Byte (*allow)(Address, int){};
    Byte (*select)(Address, int*){};
    Exec board{};
    Byte (*eligible)(Address, Address){};
};
inline Originals original{};
inline bool installed{}, rollback_pending{};

template <class Fn> inline Fn slot(Address object, unsigned off) {
    return reinterpret_cast<Fn>(read<Address>(read<Address>(object), off));
}
inline Address owner(Address brain, bool human_only = true) {
    const auto actor = current();
    return authorized(actor, brain, human_only) ? actor : 0;
}
inline Address order_owner(Address order) {
    const auto a = current();
    return a && read<Address>(order, 8) == a + 0xaf0 && authorized(a, read<Address>(a, 0xdc8)) ? a : 0;
}
inline Byte heal(Address state) {
    runtime::HookGuard guard(gate);
    const auto brain = read<Address>(state, 0x30), actor = owner(brain);
    const auto saved = read<Address>(actor, 0x12b0);
    if (!actor || (read<unsigned>(brain, 0x39c) & (1u << 28)) || read<Byte>(state, 0x3c0) ||
        read<Byte>(actor, 0x12b8) || !runtime::is_plausible_ptr(saved) || !read<Address>(actor, 0x578) ||
        ti::protected_order(brain, actor) || function<Test>(0x7e05b0)(actor))
        return original.heal(state);
    const auto holder = function<Unary>(0x7e04b0)(actor);
    const auto empty = slot<Test>(holder, 0x30);
    if (!empty) return original.heal(state);
    if (!empty(holder)) {
        runtime::write<Address>(actor, 0x12b0, 0);
        return 0;
    }
    if (!function<Byte (*)(Address, Byte)>(0x92aa40)(state, 1)) return original.heal(state);
    const auto order = function<Unary>(0x901420)(saved + 0x868);
    runtime::write<Address>(actor, 0x12b0, 0);
    if (!order) return 0;
    const auto seq = function<Unary>(0x92abb0)(state);
    if (!seq) {
        s::discard(order);
        return 0;
    }
    function<Address (*)(Address, Address, int, Byte)>(0x919270)(seq, order, 0, 1);
    ++stats.heal;
    return 1;
}
using Circles = std::array<p::Circle, 64>;
inline bool route(const sim::World& w, Address actor, const Circles& circles, std::size_t n, const p::Point* enemy,
                  p::Point& goal) {
    const auto start = s::pos(actor);
    s::PathContext path(actor);
    return p::choose_burn_escape(
        {start.x, start.y}, circles.data(), n, enemy, w.length,
        [&](p::Point pt) {
            sim::Vec3 dest{pt.x, pt.y, start.z};
            return s::ground(w, dest) && s::corridor(w, path, start, dest);
        },
        goal);
}
struct QueryOps {
    decltype(&route) choose_route{&route};
    p::Sight (*probe_stance)(const ti::Context&, int);
};
inline p::Sight stance_ray(const ti::Context&, int);
inline QueryOps queries{&route, &stance_ray};
inline Address burn(Address scanner) {
    runtime::HookGuard guard(gate);
    const auto brain = read<Address>(scanner, 8), actor = owner(brain);
    const auto rows = s::span(scanner, 0x18);
    sim::World w{};
    if (!actor || !read<Byte>(scanner, 0x10) || !rows.valid(0x18, 64) || rows.first == rows.end ||
        ti::protected_order(brain, actor) || !sim::world(image, w))
        return original.burn(scanner);
    Circles circles{};
    std::size_t n = 0;
    for (auto row = rows.first; row != rows.end; row += 0x18)
        circles[n++] = {read<p::Point>(row, 8), read<float>(row, 0x10)};
    const auto subject = read<Address>(brain, 0x3b8);
    p::Point target{}, goal{};
    const bool visible = pr::is_visible(subject);
    if (visible) target = read<p::Point>(subject, 0x28);
    if (!queries.choose_route(w, actor, circles, n, visible ? &target : nullptr, goal)) return 0;
    const auto order = function<Address (*)(const p::Point*, unsigned)>(runtime::table().fn.move_task_point)(&goal, 0);
    if (order) ++stats.burn;
    return order;
}
inline std::uint64_t repair(Address order, int mode) {
    runtime::HookGuard guard(gate);
    if (mode == 1 && order_owner(order) && !read<Byte>(order, 0x131) && read<int>(order, 0x38) == 5 &&
        read<Address>(order, 0x118)) {
        const auto comp = read<Address>(order, 0xd0);
        const auto rows = s::span(read<Address>(order, 0x68), 0xb90);
        if (rows.valid(0x28, 1024) && comp >= rows.first && comp < rows.end && (comp - rows.first) % 0x28 == 0 &&
            p::unfinished(read<int>(comp, 0x10), read<int>(image, 0x1584508), read<Byte>(comp, 0x24))) {
            ++stats.kit;
            return 2;
        }
    }
    return original.repair(order, mode);
}
struct Pane {
    Address entity, volume;
};
inline Byte window(Address aimer, Address from, Address to, Byte may_break) {
    runtime::HookGuard guard(gate);
    const Byte blocked = original.window(aimer, from, to, may_break);
    if (!blocked || !may_break) return blocked;
    const auto actor = current();
    if (!actor || read<Address>(read<Address>(aimer, 0x10), 0x90) != actor) return blocked;
    const auto brain = read<Address>(actor, 0xdc8);
    if (!authorized(actor, brain, false) || !firing_permitted(brain)) return blocked;
    const auto hits = s::span(image, 0x15ae8d8);
    if (!hits.valid(0x48, 8) || hits.end - hits.first < 2 * 0x48) return blocked;
    std::array<Pane, 8> panes{};
    std::size_t n = 0;
    for (auto h = hits.first; h != hits.end; h += 0x48) {
        const Pane pane{read<Address>(h, 0x18), read<Address>(h, 0x20)};
        if (read<int>(h) != 4 || !pane.entity || !pane.volume || (read<Byte>(pane.volume, 0x28) & 0x20) ||
            !(read<unsigned>(pane.volume, 0xc) & (1u << 13)))
            return blocked;
        panes[n++] = pane;
    }
    alignas(16) std::array<std::byte, 0x60> name{};
    function<void (*)(void*, const char*)>(0x564e40)(name.data(), "break_window");
    for (std::size_t i = 0; i < n; ++i) {
        alignas(16) std::array<std::byte, 0x190> event{};
        auto dispatch = function<Address (*)(void*, Address)>(0x780e60)(event.data(), panes[i].entity);
        dispatch = function<Address (*)(Address, Address)>(0x780f80)(dispatch, panes[i].volume);
        function<void (*)(Address, const void*)>(0x781240)(dispatch, name.data());
        function<void (*)(void*)>(0x780ef0)(event.data());
    }
    function<void (*)(void*)>(0x565010)(name.data());
    stats.window += static_cast<unsigned>(n);
    return 0;
}
inline p::Sight stance_ray(const ti::Context& c, int stance) {
    if (pr::scope.actor != c.actor || pr::depth != 1) return p::Sight::unknown;
    const auto result = ti::sight(c, c.self, ti::height(stance));
    using Sight = tactical_infantry::policy::Sight;
    return result == Sight::open ? p::Sight::clear : result == Sight::blocked ? p::Sight::blocked : p::Sight::unknown;
}

inline int stance_choice(Address brain, int requested) {
    ti::Context c{};
    c.brain = brain;
    c.actor = owner(brain);
    if (!c.actor || (read<unsigned>(brain, 0x398) & 0x3000u) != 0x1000u || !firing_permitted(brain) ||
        (read<unsigned>(brain, 0x39c) & (1u << 9)))
        return -1;
    c.sub = read<Address>(brain, 0x3b8);
    const auto cover = read<Address>(c.actor, 0xdc0);
    const auto action = read<Byte>(c.actor, 0xa38);
    if (!pr::is_visible(c.sub) || !cover || read<int>(cover, 0x24) == 4 || action == 4 || action == 5 ||
        function<Test>(0x91b200)(brain) || function<Test>(0x7e05b0)(c.actor) || !sim::world(image, c.w))
        return -1;
    c.self = read<tactical_infantry::XY>(c.actor, 0x74);
    const float metres = pr::distance(read<pr::Vec3>(c.actor, 0x74), read<pr::Vec3>(c.sub, 0x28)) / c.w.length;
    if (metres >= 7.5f && hit_the_dirt::is_hitting_dirt(c.actor, c.w.now)) return -1;
    return p::hold_stance(ti::stance(c), requested, metres, [&](int pose) { return queries.probe_stance(c, pose); });
}
inline Byte allow(Address brain, int requested) {
    runtime::HookGuard guard(gate);
    const Byte stock = original.allow(brain, requested);
    if (stock || (requested != 0 && requested != 2) || stance_choice(brain, requested) < 0) return stock;
    ++stats.stance;
    return 1;
}
inline Byte select(Address brain, int* out) {
    runtime::HookGuard guard(gate);
    const Byte stock = original.select(brain, out);
    if (stock || !out) return stock;
    const int chosen = stance_choice(brain, -1);
    if (chosen < 0) return stock;
    *out = chosen;
    ++stats.stance;
    return 1;
}
inline std::uint64_t board(Address order, int mode) {
    runtime::HookGuard guard(gate);
    const auto actor = mode == 1 ? order_owner(order) : 0;
    if (!actor) return original.board(order, mode);
    if (slot<Test>(order, 0xc0)(order)) return 2;
    using Advance = std::uint64_t (*)(Address);
    const auto advance = function<Advance>(0x8f3980);
    if (read<int>(order, 0x38) == 1 && function<Test>(0x8f3910)(order)) return advance(order);
    auto child = read<Address>(order, 0x30);
    if (!child) return advance(order);
    if (slot<int (*)(Address)>(child, 0x18)(child)) {
        child = read<Address>(order, 0x30);
        if (child) {
            slot<void (*)(Address)>(child, 0x20)(child);
            child = read<Address>(order, 0x30);
            if (child) slot<void (*)(Address, int)>(child, 0)(child, 1);
            runtime::write<Address>(order, 0x30, 0);
        }
        return advance(order);
    }
    const int state = read<int>(order, 0x38);
    if (state != 1 && state != 3) return 0;
    const auto position = function<Unary>(0x7a70d0);
    const float move2 = p::squared(read<p::Point>(position(order + 0x40)), read<p::Point>(order, 0xf4));
    const float tolerance = read<int>(order, 0x38) == 1 ? read<float>(order, 0xe8) : read<float>(image, 0x1042c90);
    const float relative =
        p::squared(read<p::Point>(position(order + 0x40)), read<p::Point>(actor, 0x74)) * read<float>(image, 0x1023908);
    if (p::board_replan(move2, tolerance, relative)) return advance(order);
    if (move2 > tolerance && move2 > relative) ++stats.board;
    return 0;
}
inline Byte eligible(Address brain, Address sub) {
    runtime::HookGuard guard(gate);
    const auto stock = original.eligible(brain, sub);
    if (!sub || (stock && !config::active().modules.allegiance_guard)) return stock;
    const auto actor = owner(brain, false);
    if (!actor || !pr::autonomous_fire(image, brain)) return stock;
    bool extended = false;
    if (!stock) {
        const auto now = function<runtime::Tick (*)()>(runtime::table().auth.clock_read)();
        const auto seconds = function<float (*)(int)>(0x66e7c0);
        const auto age = seconds(static_cast<int>(now - read<unsigned>(sub, 0x64)));
        const auto deadline = read<unsigned>(sub, 0x70);
        if (!(age <= 20.f) || !deadline || now < deadline || read<Byte>(sub, 0x6c)) return 0;
        const auto entity = read<Address>(sub, 0x18);
        const auto bit = read<unsigned>(image, 0x15c6a58);
        if (bit >= 32) return 0;
        const bool burning = entity && (read<Byte>(entity, 0xdad + bit / 8) & (1u << (bit % 8)));
        if (!burning && !((read<unsigned>(brain, 0x398) & 3) == 1 && age > 10.f)) return 0;
        if (burning && (!sim::has_actor(entity) || !(read<Byte>(entity, 0x30) & 8) ||
                        !(read<unsigned>(entity, 0xdb4) & 0x100) || s::has_destruction_tag(entity)))
            return 0;
        if ((read<Byte>(actor, 0x30) & 4) && (read<unsigned>(entity, 0x1c0) & (1u << 28)) &&
            !(read<unsigned>(brain, 0x39c) & (1u << 17))) {
            const auto parent = read<Address>(read<Address>(entity, 0x560), 8);
            if (!parent || function<Byte (*)(Address, Address)>(0x4c1cb0)(parent, image + 0x159d880)) return 0;
        }
        const auto flags = read<unsigned>(sub, 0x14);
        const bool recent = !(flags & (1 | (1u << 10))) && read<Address>(sub, 0x40) &&
                            seconds(static_cast<int>(now - read<unsigned>(sub, 0x5c))) < read<float>(image, 0x102af14);
        if (!p::target_flags(flags, read<unsigned>(sub, 0x10), entity != 0, recent)) return 0;
        extended = true;
    }
    if (config::active().modules.allegiance_guard) {
        switch (pr::target_veto(image, actor, sub)) {
            case pr::TargetVeto::towing: ++stats.towed_gun_suppress; return 0;
            case pr::TargetVeto::ally: ++stats.allegiance_veto; return 0;
            case pr::TargetVeto::none: break;
        }
    }
    if (extended) ++stats.eligible;
    return stock ? stock : 1;
}
struct ReloadAttempt {
    s::Identity actor{};
    runtime::Tick at{};
};
inline store::GrowableTable<ReloadAttempt, 64, 8192> attempts{};
inline Address reload_scene{};
inline runtime::Tick reload_clock{};
inline bool quiet(const sim::World& w, Address a, Address b) {
    const auto sensor = read<Address>(a, 0xdd0);
    const auto subjects = s::span(sensor, 0x280);
    if (!sensor || read<Address>(sensor, 0x260) != a || !subjects.valid(8, 4096)) return false;
    const auto selected = read<Address>(b, 0x3b8);
    bool found = !selected;
    const bool covered = read<int>(read<Address>(a, 0xdc0), 0x24) != 0;
    for (auto it = subjects.first; it != subjects.end; it += 8) {
        const auto sub = read<Address>(it);
        if (!sub) continue;
        if (sub == selected) {
            found = true;
            const auto seen = read<unsigned>(sub, 0x64);
            if (!pr::is_visible(sub) && !covered && (seen > w.now || w.now - seen < w.duration(2))) return false;
        }
        if (!read<Byte>(sub, 0x6c) && (sub == selected || (read<unsigned>(sub, 0x14) & 0x1e) == 2) &&
            pr::is_visible(sub) && s::within(s::pos(a), read<sim::Vec3>(sub, 0x28), 35.f * w.length))
            return false;
    }
    return found;
}
inline void reload(const sim::World& w, Address a) {
    const auto b = read<Address>(a, 0xdc8);
    if (!authorized(a, b) || !config::active().modules.lull_reload) return;
    const auto scene = s::entity_scene(w);
    if (!scene) return;
    if (scene != reload_scene || w.now < reload_clock) attempts.clear();
    reload_scene = scene;
    reload_clock = w.now;
    if (!s::autonomous_if(a, true, s::donor_policy_allows) || !s::free_slot(a) ||
        coord::instance().active_for(a, read<unsigned>(a, 0xc0), w.now).owner != coord::Module::none ||
        auto_medic::occupied(a) || auto_resupply::occupied(a) || close_grenade::is_busy(w, a) || !quiet(w, a, b))
        return;
    ReloadAttempt* row = nullptr;
    for (auto& item : attempts) {
        if (item.actor.matches(a)) {
            if (w.now - item.at < w.duration(4)) return;
            row = &item;
            break;
        }
        if (!row && (!item.actor.ptr || w.now - item.at >= w.duration(4))) row = &item;
    }
    const auto weapon = function<Unary>(runtime::table().fn.weapon_select)(read<Address>(a, 0xdd8));
    const auto charger = read<Address>(weapon, 0xb0), def = read<Address>(weapon, 0x98);
    if (!charger || !def || (read<Byte>(charger, 0x14) & 0x14) || function<Test>(0x820ce0)(charger)) return;
    const int cap = read<int>(def, 0x470);
    const auto magazine = slot<Unary>(weapon, 0x18);
    if (cap < 4 || !magazine) return;
    const float ratio = config::active().tuning.lull_reload_ratio;
    if (!p::low_magazine(function<int (*)(Address)>(0x7dc450)(magazine(weapon)), cap, ratio)) return;
    if (!row) row = store::try_append_slot(attempts);
    if (!row) return;
    *row = {s::identity(a), w.now};
    if (function<Byte (*)(Address, float)>(0x95c530)(b, ratio)) ++stats.reload;
}
} // namespace detail
inline void on_human_post_quant(const sim::World& w, Address a) {
    runtime::HookGuard guard(gate);
    detail::reload(w, a);
}
template <class Create, class Remove> bool prepare_hooks(Address base, const char* sha, Create create, Remove remove) {
    using namespace detail;
    if (!base || !sha || std::strcmp(sha, runtime::kBuild26734796Sha256) != 0 || installed || rollback_pending)
        return false;
    std::array<void*, hook_count> staged{};
    unsigned n{};
    auto add = [&](Address rva, auto detour, auto& orig) {
        void *target = reinterpret_cast<void*>(base + rva), *trampoline = nullptr;
        if (create(target, reinterpret_cast<void*>(detour), &trampoline)) return false;
        staged[n++] = target;
        orig = reinterpret_cast<decltype(detour)>(trampoline);
        return true;
    };
    if (!add(0x96aa30, heal, original.heal) || !add(0x91f990, burn, original.burn) ||
        !add(0x910d50, repair, original.repair) || !add(0x834ea0, window, original.window) ||
        !add(0x95b940, allow, original.allow) || !add(0x95bad0, select, original.select) ||
        !add(0x8f3610, board, original.board) || !add(0x92b320, eligible, original.eligible)) {
        while (n)
            if (remove(staged[--n])) rollback_pending = true;
        if (!rollback_pending) original = {};
        return false;
    }
    image = base;
    ops.serial_actor = []() -> Address { return s::serial_thread() ? s::current_actor() : 0; };
    installed = true;
    gate.activate();
    return true;
}
inline void deactivate() {
    gate.deactivate();
}
inline unsigned inflight_count() {
    return gate.count();
}
inline void after_hooks_removed() {
    deactivate();
    detail::original = {};
    detail::installed = detail::rollback_pending = false;
    detail::attempts.release();
    detail::reload_scene = detail::reload_clock = 0;
    image = 0;
    ops = {};
    stats.allegiance_veto = stats.towed_gun_suppress = 0;
    stats.heal = stats.burn = stats.kit = stats.window = stats.stance = stats.reload = stats.board = stats.eligible = 0;
}
} // namespace goh::engine_fixes
