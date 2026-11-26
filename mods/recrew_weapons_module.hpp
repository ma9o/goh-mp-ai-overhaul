#pragma once

#include "squad_logistics_module.hpp"
#include "tank_crew_fill_module.hpp"

namespace goh::recrew_weapons {
namespace s = ai_support;
namespace repair = tank_auto_repair;
using runtime::Tick;
using s::Address;
using s::Byte;
using s::read;
using s::World;
inline constexpr bool runtime_allowed = false;
inline constexpr unsigned hook_count = 0;
inline std::atomic<unsigned> stat_recrews{};

namespace detail {
constexpr auto module = coord::Module::recrew_weapons;
constexpr auto priority = coord::Priority::recrew_weapons;
constexpr unsigned seat_size = 0x298;
using Crew = std::array<s::Identity, 8>;
struct GunWatch {
    s::Identity gun{};
    unsigned owner{};
    Crew previous{};
    Tick lost{}, manual_until{};
};
struct Job {
    s::Identity human{}, gun{};
    Address brain{}, order{};
    unsigned player{18};
    int seat{-1};
    Tick deadline{}, next_scan{};
    bool interrupted{};
};
inline store::GrowableTable<GunWatch, 64, 2048> guns{};
inline store::GrowableTable<Job, 32, 2048> jobs{};
inline Address scene{};
inline Tick clock{}, next_discovery{};
inline std::atomic<bool> stopping{};
inline std::atomic<unsigned> admissions{};
inline bool busy{};
struct Scope {
    Scope() {
        ++admissions;
        busy = true;
    }
    ~Scope() {
        busy = false;
        --admissions;
    }
};
inline bool pending(Tick until, Tick now) {
    return until && static_cast<std::int32_t>(until - now) > 0;
}
inline void refresh(const World& w) {
    const auto live = s::entity_scene(w);
    coord::instance().sync_world(live, w.now);
    if (scene != live || w.now < clock) {
        for (const auto& j : jobs)
            if (j.order) coord::instance().release(j.human.ptr, module);
        guns.clear();
        jobs.clear();
        next_discovery = 0;
    }
    scene = live;
    clock = w.now;
}
inline s::Span seat_rows(Address gun) {
    const Address placer = read<Address>(gun, 0x568);
    return placer && read<Address>(placer, 0x40) == gun ? s::span(placer, 0x28) : s::Span{};
}
struct AmmoCalls {
    static bool dummy(Address charger) {
        return s::native<s::Test>(0x820ce0)(charger) != 0;
    }
    static bool offers(const World& w, Address source, Address zone, Address gun, const auto_resupply::Gun& weapon) {
        return auto_resupply::source_owner(w, source, zone, gun) && auto_resupply::supplies(zone, gun, weapon);
    }
};
template <class Calls = AmmoCalls> inline bool armed_or_supplied(const World& w, Address gun) {
    const auto weapons = s::span(read<Address>(gun, 0xdd8), 0x30);
    if (!weapons.valid(sizeof(Address), 256)) return false;
    // auto_resupply::primary requires seated crew. Read the mounted weapon and
    // charger directly so an empty crew vector does not hide usable ammunition.
    for (Address p = weapons.first; p < weapons.end; p += sizeof(Address)) {
        const auto place = read<Address>(p), charger = read<Address>(place, 0xb0);
        const auto item = read<Address>(place, 0x218), def = read<Address>(item, 0x40);
        if (!place || read<Address>(place, 0x90) != gun || !charger || !read<Address>(place, 0x98) ||
            !read<Address>(place, 0xa0) || !auto_resupply::weapon_definition(def) || Calls::dummy(charger))
            continue;
        const auto charged = read<s::Unary>(read<Address>(charger), 8)(charger);
        const auto_resupply::Gun weapon{def, charged, place, 0.f, false};
        if (auto_resupply::ammo(gun, weapon) == auto_resupply::Ammo::available) return true;
        bool supplied = false;
        s::entities(w, [&](Address source) {
            if (supplied || source == gun) return;
            const auto zone = s::extender(source, runtime::table().vt.supply_zone_ext);
            if (!zone || !read<Byte>(zone, 0x430)) return;
            const float radius = read<float>(zone, 0x88), period = read<float>(zone, 0x8c);
            supplied = std::isfinite(radius) && radius > 0.f && period > 0.f && period <= 90.f &&
                       s::within(s::pos(gun), s::pos(source), radius * w.length) &&
                       auto_resupply::serviceable(source) && Calls::offers(w, source, zone, gun, weapon);
        });
        if (supplied) return true;
    }
    return false;
}
inline bool specialist(Address actor, Address hand_definition) {
    if (squad_logistics::medic_breed(actor)) return true;
    const auto pack = s::box(actor);
    const auto items = s::span(pack, 0x10);
    if (!pack || !items.valid(8, 2048)) return true;
    const auto heavy = [](Address definition) {
        return classify::def_any_tag(definition, [](std::string_view t) {
            return t == "rocketlauncher" || t == "mgun" || t == "flame_thrower";
        });
    };
    if (heavy(hand_definition)) return true;
    for (auto p = items.first; p < items.end; p += sizeof(Address)) {
        const auto item = read<Address>(p);
        if (read<Address>(item, 0x30) == pack && heavy(read<Address>(item, 0x40))) return true;
    }
    return false;
}
inline bool protected_soldier(Address actor) {
    return specialist(actor, s::native<s::Unary>(0x7e04e0)(actor));
}
inline Address make_board(Address gun, Address seat) {
    // 0x8FADC0 copies the eName from the first 0x60 bytes of the seat via
    // 0x565020. The installed order owns its name through its destructor.
    return s::native<Address (*)(Address, Address, Byte)>(runtime::table().fn.board_order)(gun, seat, 0);
}
struct Ops {
    decltype(&s::callback) callback = s::callback;
    bool (*ammunition)(const World&, Address) = armed_or_supplied<>;
    decltype(&protected_soldier) is_specialist = protected_soldier;
    decltype(&make_board) board = make_board;
    decltype(&s::set_brain_order) set = s::set_brain_order;
};
inline Ops ops{};
inline bool occupied(Address actor) {
    for (const auto& j : jobs)
        if (j.order && !j.interrupted && j.human.matches(actor) && j.player == s::owner(actor)) return true;
    return false;
}
inline void preempt(Address actor) {
    for (auto& j : jobs)
        if (j.order && j.human.matches(actor)) j.interrupted = true;
}
inline void finish(const World& w, Job& j, bool seated) {
    coord::instance().release(j.human.ptr, module);
    j.order = 0;
    j.gun = {};
    j.next_scan = w.now + w.duration(10.f);
    if (seated) ++stat_recrews;
}
inline bool boarding_receipt(const Job& j, Address human, Address gun) {
    if (j.interrupted || s::owner(human) != j.player || s::owner(gun) != j.player ||
        read<Address>(human, 0xdc8) != j.brain || repair::detail::vitality(human))
        return false;
    const auto current = read<Address>(j.brain, 0xf0);
    return !current || current == j.order || s::free_slot(human);
}
inline Address gun_brain(Address gun) {
    if (!s::live_entity(gun) || !(read<Byte>(gun, 0x30) & 8) || s::owner(gun) >= 18 || !read<std::uint16_t>(gun, 0xdaa))
        return 0;
    const auto b = read<Address>(gun, 0xdc8);
    return read<Address>(b) == s::base + runtime::table().vt.vehicle_brain && read<Address>(b, 0x38) == gun ? b : 0;
}
inline GunWatch* observe(const World& w, Address gun) {
    if (!gun_brain(gun) || !s::extender(gun, runtime::table().vt.cannon_ext)) return nullptr;
    const auto seats = seat_rows(gun);
    if (!seats.first || !seats.valid(seat_size, 256)) return nullptr;
    GunWatch* row = nullptr;
    for (auto& g : guns) {
        if (g.gun.ptr == gun) {
            row = &g;
            break;
        }
        if (!row && !g.gun.ptr) row = &g;
    }
    if (!row) row = store::try_append_slot(guns);
    if (!row) return nullptr;
    if (!row->gun.matches(gun) || row->owner != s::owner(gun)) *row = {s::identity(gun), s::owner(gun), {}, 0, 0};
    Crew crew{};
    std::size_t used = 0;
    for (auto seat = seats.first; seat < seats.end; seat += seat_size) {
        const auto human = read<Address>(seat, 0x280);
        if (!human || !repair::detail::seated_crew(seat)) continue;
        if (used == crew.size()) {
            // Never infer a loss from an incomplete crew observation.
            row->manual_until = w.now + w.duration(30.f);
            return row;
        }
        crew[used++] = s::identity(human);
        for (auto& j : jobs)
            if (j.order && j.gun.matches(gun) && j.human.matches(human)) finish(w, j, boarding_receipt(j, human, gun));
    }
    for (const auto& previous : row->previous) {
        if (!previous.ptr || std::any_of(crew.begin(), crew.end(), [&](const s::Identity& k) {
                return k.ptr == previous.ptr && k.id == previous.id && k.definition == previous.definition;
            }))
            continue;
        const auto departed = s::resolve(w, previous);
        if (!departed || repair::detail::vitality(departed))
            row->lost = w.now;
        else {
            repair::detail::Tank* assigned = nullptr;
            const bool repairing = repair::has_active_episode(gun) && repair::detail::member(departed, assigned) &&
                                   assigned->key.matches(gun) && repair::is_busy(departed);
            if (!repairing) row->manual_until = w.now + w.duration(30.f);
        }
    }
    row->previous = crew;
    return row;
}
inline void discover(const World& w) {
    if (pending(next_discovery, w.now)) return;
    next_discovery = w.now + w.duration(1.f);
    for (auto& g : guns)
        if (g.gun.ptr && !s::resolve(w, g.gun)) g = {};
    s::actors(w, [&](Address a) {
        if (read<Byte>(a, 0x30) & 8) observe(w, a);
    });
    for (auto& j : jobs) {
        if (!j.human.ptr || (j.order && pending(j.deadline, w.now))) continue;
        if (!s::resolve(w, j.human)) {
            coord::instance().release(j.human.ptr, module);
            j = {};
        }
    }
}
inline bool working_gun(Address gun) {
    const auto rows = s::span(gun, 0xb90);
    if (!rows.valid(0x28, 4096)) return false;
    bool working = false;
    for (auto p = rows.first; p < rows.end; p += 0x28) {
        if (!read<Address>(p) || read<Address>(p, 8) != gun) return false;
        const auto name = repair::detail::component_name(read<Address>(p));
        const auto state = read<Byte>(p, 0x24);
        if (name == "body" && state != 0) return false;
        if (repair::detail::gun(name) && !state) working = true;
    }
    return working;
}
inline bool eligible_gun(const World& w, Address gun, Address soldier, const GunWatch& watch) {
    const auto ext = s::extender(gun, runtime::table().vt.cannon_ext);
    if (s::owner(gun) != s::owner(soldier) || s::owner(soldier) >= 18 || !ext || read<int>(ext, 0xc30) ||
        !s::autonomous_vehicle_brain(w.base, gun) || !s::free_slot(gun) ||
        (read<unsigned>(read<Address>(gun, 0xdc8), 0x39c) & (1u << 28)) || pending(watch.manual_until, w.now) ||
        repair::has_active_episode(gun) || !working_gun(gun) ||
        (s::visible_near(gun, 14.f * w.length) || s::visible_near_point(soldier, s::pos(gun), 14.f * w.length)))
        return false;
    const auto placer = read<Address>(gun, 0x568), boarder = read<Address>(gun, 0x570);
    const auto walkers = s::span(boarder, 0x60);
    return placer && !read<Byte>(placer, 0x48) && boarder && read<Address>(boarder, 0x58) == gun &&
           walkers.valid(0x90, 256) && !tank_crew_fill::detail::has_exit_walker(walkers) && ops.ammunition(w, gun);
}
inline bool eligible_soldier(const World& w, Address actor) {
    return s::autonomous_if(actor, true,
                            [](unsigned policy, unsigned flags) {
                                return ((policy >> 12) & 3) <= 1 && s::donor_policy_allows(policy, flags);
                            }) &&
           s::inventory_free(actor) && !s::visible_near(actor, 14.f * w.length) && !ops.is_specialist(actor) &&
           !auto_medic::occupied(actor) && !squad_logistics::occupied(actor) && !auto_resupply::occupied(actor) &&
           !close_grenade::is_busy(w, actor) &&
           coord::instance().can_act(actor, module, priority, read<unsigned>(actor, 0xc0), w.now);
}
inline float reach(const World& w, Address actor) {
    return w.length *
           (s::hold_position(read<Address>(actor, 0xdc8)) ? 8.f : config::active().tuning.recrew_search_radius_m);
}
inline int seat_rank(Address gun, std::string_view name) {
    if (repair::detail::cannon_gunner_seat(gun, name)) return 0;
    if (name.substr(0, 7) == "charger") return 1;
    return name == "commander" ? 2 : 3;
}
inline int choose_seat(Address gun) {
    const auto seats = seat_rows(gun);
    if (!seats.first || !seats.valid(seat_size, 256)) return -1;
    int selected = -1, best_rank = 4, total = 0, seated = 0, claims = 0;
    bool gunner = false;
    for (const auto& j : jobs)
        if (j.order && j.gun.matches(gun)) ++claims;
    for (auto seat = seats.first; seat < seats.end; seat += seat_size) {
        if (!repair::detail::seated_crew(seat) || (read<unsigned>(seat, 0x248) & (0x10u | (1u << 20))) ||
            (read<std::size_t>(seat, 0x170) && !read<Address>(seat, 0x240)))
            continue;
        const auto name = s::native_string(seat + 0x40);
        if (name.empty()) continue;
        ++total;
        const int rank = seat_rank(gun, name);
        if (read<Address>(seat, 0x280)) {
            ++seated;
            gunner |= rank == 0;
            continue;
        }
        const int index = static_cast<int>((seat - seats.first) / seat_size);
        if (rank >= best_rank || read<Address>(seat, 0x290)) continue;
        if (std::any_of(jobs.begin(), jobs.end(),
                        [&](const Job& j) { return j.order && j.seat == index && j.gun.matches(gun); }))
            continue;
        selected = index;
        best_rank = rank;
    }
    return !gunner && claims + seated < (std::min)(total, 2) ? selected : -1;
}
inline Address seat_at(Address gun, int index) {
    const auto seats = seat_rows(gun);
    return seats.valid(seat_size, 256) && index >= 0 &&
                   static_cast<std::size_t>(index) < (seats.end - seats.first) / seat_size
               ? seats.first + static_cast<Address>(index) * seat_size
               : 0;
}
inline bool own_order(Address actor, const Job& j) {
    return read<Address>(actor, 0xdc8) == j.brain && read<Address>(j.brain, 0xf0) == j.order &&
           read<Address>(j.order) == s::base + runtime::table().vt.board_order &&
           read<Address>(j.order, 8) == actor + 0xaf0 && read<Address>(j.order, 0x58) == j.gun.ptr;
}
inline void progress(const World& w, Address human, Job& j) {
    const auto gun = s::resolve(w, j.gun);
    const auto seat = gun ? seat_at(gun, j.seat) : 0;
    if (seat && read<Address>(seat, 0x280) == human) {
        finish(w, j, boarding_receipt(j, human, gun));
        return;
    }
    if (!own_order(human, j)) {
        finish(w, j, false);
        return;
    }
    auto* watch = gun ? observe(w, gun) : nullptr;
    if (!j.order) return; // The vehicle observation can confirm a different crew seat.
    const auto reservation = read<Address>(seat, 0x290);
    if (!stopping.load() && s::accepting.load() && s::owner(human) == j.player &&
        config::active().modules.recrew_weapons && !j.interrupted && pending(j.deadline, w.now) && seat &&
        !read<Address>(seat, 0x280) && (!reservation || reservation == human) &&
        !(read<unsigned>(seat, 0x248) & (0x10u | (1u << 20))) && watch && eligible_gun(w, gun, human, *watch) &&
        eligible_soldier(w, human) && s::within(s::pos(human), s::pos(gun), reach(w, human)))
        return;
    // Slot 13 accepts null (0x91DB30); End and destruction remain native.
    // A replacement player order or a manually controlled brain is never touched.
    const bool cancel = s::owner(human) == j.player && s::autonomous_if(human, true, s::donor_policy_allows) &&
                        !(read<unsigned>(j.brain, 0x39c) & (1u << 24));
    const auto b = j.brain;
    finish(w, j, false);
    if (cancel) ops.set(b, 0);
}
inline void scan(const World& w, Address human, Job& j) {
    j.next_scan = w.now + w.duration(1.5f);
    if (!s::free_slot(human) || !eligible_soldier(w, human)) return;
    discover(w);
    const auto player = s::owner(human);
    s::Identity best{};
    int best_seat = -1;
    float distance = reach(w, human);
    float best2 = distance * distance;
    unsigned best_id = 0xffffffffu;
    for (auto& g : guns) {
        if (!g.gun.ptr || g.owner != s::owner(human)) continue;
        const auto gun = s::resolve(w, g.gun);
        if (!gun) continue;
        const float d2 = auto_smoke::distance2(s::pos(human), s::pos(gun));
        if (!(d2 <= best2) || (d2 == best2 && g.gun.id >= best_id)) continue;
        auto* current = observe(w, gun);
        const int seat = choose_seat(gun);
        if (current && seat >= 0 && eligible_gun(w, gun, human, *current)) {
            best = g.gun;
            best_seat = seat;
            best2 = d2;
            best_id = g.gun.id;
        }
    }
    if (!best.ptr || !coord::instance().claim(human, j.human.id, module, priority, w.now, w.duration(18.f))) return;
    // A lower-priority job may release a native order synchronously on preemption.
    const auto target = s::resolve(w, best);
    const auto row = target ? seat_at(target, best_seat) : 0;
    const auto order = row && s::resolve(w, j.human) && s::owner(human) == player && s::owner(target) == player
                           ? ops.board(target, row)
                           : 0;
    auto* current = s::resolve(w, best) ? observe(w, target) : nullptr;
    j.next_scan = w.now + w.duration(10.f);
    if (!order || !s::resolve(w, j.human) || !current || s::owner(human) != player ||
        !eligible_gun(w, target, human, *current) || !s::free_slot(human) || !eligible_soldier(w, human) ||
        choose_seat(target) != best_seat || seat_at(target, best_seat) != row) {
        s::discard(order);
        coord::instance().release(j.human.ptr, module);
        return;
    }
    j.gun = best;
    j.player = player;
    j.seat = best_seat;
    j.brain = read<Address>(human, 0xdc8);
    j.order = order;
    j.interrupted = false;
    j.deadline = w.now + w.duration(18.f);
    ops.set(j.brain, order);
    // Native setter consumes ownership, including when it completes synchronously.
    if (s::resolve(w, j.human)) progress(w, human, j);
}
inline void tick_serial(const World& w, Address actor) {
    refresh(w);
    coord::instance().register_provider(module, priority, occupied, preempt);
    Job* empty = nullptr;
    for (auto& j : jobs) {
        if (j.human.matches(actor)) {
            if (j.order) {
                progress(w, actor, j);
                return;
            }
            if (!pending(j.next_scan, w.now) && !stopping.load() && s::accepting.load() &&
                config::active().modules.recrew_weapons)
                scan(w, actor, j);
            return;
        }
        if (!empty && (!j.human.ptr || (!j.order && !pending(j.next_scan, w.now)))) empty = &j;
    }
    if (stopping.load() || !s::accepting.load() || !config::active().modules.recrew_weapons || !s::free_slot(actor))
        return;
    if (!empty) empty = store::try_append_slot(jobs);
    if (empty) {
        *empty = {s::identity(actor)};
        scan(w, actor, *empty);
    }
}
} // namespace detail

inline void on_vehicle_observe(const World& observed, Address gun) {
    World current{};
    if (detail::stopping.load() || !detail::ops.callback(gun, current) || observed.scene != current.scene ||
        detail::busy || !config::active().modules.recrew_weapons)
        return;
    detail::Scope scope;
    detail::refresh(current);
    detail::observe(current, gun);
}
inline void tick(Address actor) {
    World w{};
    if (!detail::ops.callback(actor, w) || !(read<Byte>(actor, 0x30) & 4) || detail::busy) return;
    detail::Scope scope;
    detail::tick_serial(w, actor);
}
inline void begin_shutdown() noexcept {
    detail::stopping.store(true);
}
inline bool quiescent() noexcept {
    return detail::admissions.load() == 0;
}
inline void after_hooks_removed() {
    for (const auto& j : detail::jobs)
        if (j.order) coord::instance().release(j.human.ptr, detail::module);
    coord::instance().register_provider(detail::module, detail::priority, nullptr, nullptr);
    detail::guns.release();
    detail::jobs.release();
    detail::scene = detail::clock = detail::next_discovery = 0;
    detail::ops = {};
    detail::stopping.store(false);
    stat_recrews = 0;
}
} // namespace goh::recrew_weapons
