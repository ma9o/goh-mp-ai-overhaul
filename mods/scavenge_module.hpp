#pragma once

#include "squad_logistics_module.hpp"

namespace goh::scavenge {
namespace s = ai_support;
namespace med = auto_medic;
namespace supply = auto_resupply;
namespace squad = squad_logistics;
using s::Address;
using s::read;
using s::Vec3;
using s::World;
inline constexpr bool runtime_allowed = false;
inline constexpr auto module = coord::Module::scavenge;
inline constexpr auto priority = coord::Priority::scavenge;
inline std::atomic<unsigned> stat_rearm{}, stat_heavy_pickup{}, stat_corpse_loot{};

enum class Need {
    rearm,
    heavy_weapon,
    corpse_bandage,
    corpse_ammo
};
namespace policy {
inline bool rearm(bool gun, int ammo, bool loaded_backup) {
    return (!gun || ammo == 0) && !loaded_backup;
}
inline bool upgrade(bool rifle, bool specialist, bool carries_heavy) {
    return rifle && !specialist && !carries_heavy;
}
inline bool firearm(Need n) {
    return n == Need::rearm || n == Need::heavy_weapon;
}
inline int rank(Need n) {
    return n == Need::rearm ? 0 : n == Need::heavy_weapon ? 1 : 2;
}
inline int amount(Need n, int stack, int magazine) {
    if (stack <= 0 || firearm(n)) return 0;
    return static_cast<int>(
        (std::min<long long>)(stack, n == Need::corpse_bandage ? 2 : 2LL * (std::max)(0, magazine)));
}
} // namespace policy

namespace detail {
struct Tags {
    bool firearm{}, light{}, heavy{}, rifle{}, sidearm{}, specialist{}, weapon{};
};
inline Tags tags(Address def) {
    Tags t{};
    classify::def_any_tag(def, [&](std::string_view name) {
        const bool mg = name == "mgun", at = name == "at_rifle", launcher = name == "rocketlauncher";
        t.weapon |= name == "weapon";
        t.rifle |= name == "rifle";
        t.light |= name == "rifle" || name == "smg" || name == "shortsmg" || mg || at;
        t.heavy |= mg || at || launcher;
        t.sidearm |= name == "pistol" || name == "revolver";
        t.specialist |= mg || at || launcher || name == "sniper" || name == "flame_thrower";
        return false;
    });
    t.firearm = t.weapon && (t.light || t.heavy || t.sidearm || t.specialist);
    return t;
}
inline bool specialist(Address actor) {
    const auto breed = read<Address>(actor, 0x1390);
    const auto abilities = breed ? s::span(breed + 0x528, 0x20) : s::Span{};
    if (!breed || !abilities.valid(8, 1024)) return true;
    for (auto p = abilities.first; p != abilities.end; p += 8) {
        const auto vt = read<Address>(read<Address>(p));
        if (vt == s::base + runtime::table().vt.info_medic || vt == s::base + runtime::table().vt.info_mechanic)
            return true;
    }
    return false;
}
inline Address hand_item(Address actor) {
    const auto hand = s::native<s::Unary>(0x7E04B0)(actor);
    return hand ? read<s::Unary>(read<Address>(hand), 0x18)(hand) : 0;
}
inline Address boxed_item(Address actor, Address wanted) {
    const auto box = s::box(actor);
    const auto rows = s::span(box, 0x10);
    if (wanted && box && rows.valid(8, 2048))
        for (auto p = rows.first; p != rows.end; p += 8)
            if (read<Address>(p) == wanted && read<Address>(wanted, 0x30) == box) return wanted;
    return 0;
}
inline bool can_stow(Address actor, Address held) {
    // The stock event 0x7E9CC0 calls 0x7DCAB0, which returns immediately for an item without a box.
    // The alternate hand-only FSM branch at 0x7E99EC would issue DropFromHand.
    if (!boxed_item(actor, held)) return false;
    const auto def = read<Address>(held, 0x40), fsm = read<Address>(def, 0x118);
    const auto type = fsm ? s::native_string(fsm + 0x40) : std::string_view{};
    return !type.empty() && type != "hand-only";
}
template <class Calls = s::InventoryCalls> int put_from_right(Address actor) {
    alignas(16) std::byte loc[0x40]{}, builder[0x100]{};
    Calls::template get<void* (*)(void*, Address)>(0x7E1A40)(loc, actor);
    Calls::template get<void* (*)(void*, const char*)>(0x7DF910)(builder, "PutFromRight");
    Calls::template get<void* (*)(void*, void*)>(0x7DFEE0)(builder, loc);
    runtime::write<Address>(reinterpret_cast<Address>(builder), 0x80, actor);
    int action = -1;
    Calls::template get<int* (*)(void*, int*)>(0x7DFBF0)(builder, &action);
    Calls::template get<void (*)(void*)>(0x7DFE00)(builder);
    Calls::template get<void (*)(void*)>(0x7E1EC0)(loc);
    return action;
}
inline Address ground_item(Address source) {
    if (!s::live_entity(source) || (read<unsigned>(source, 0x1c0) & (1u << 28)) || read<s::Byte>(source, 0xa38) == 4 ||
        read<s::Byte>(source, 0xa38) == 5)
        return 0;
    const auto ext = read<Address>(read<Address>(source, 0x578), 0x20);
    if (!ext || read<Address>(ext) != s::base + runtime::table().vt.stuff_ext || read<Address>(ext, 0x18) != source ||
        read<Address>(ext, 0x98))
        return 0;
    return read<Address>(ext, 0x88);
}
inline bool corpse(Address target) {
    return s::dead_human(target) && !(read<unsigned>(target, 0x1c0) & (1u << 28)) && s::box(target);
}
inline s::LooseStack stock(Address a, Need n, Address gun) {
    return n == Need::corpse_bandage
               ? s::loose_stacks(a, [](Address def) { return classify::def_has_tag(def, "bandage"); })
               : s::loose_ammo(a, gun);
}
struct Demand {
    bool rearm{}, upgrade{}, bandage{}, ammo{};
    Address gun{};
    int load{};
};
inline Demand demand(Address actor) {
    const auto gun = supply::primary(actor);
    const auto primary_tags = tags(gun.def);
    const auto box = s::box(actor);
    const auto items = s::span(box, 0x10);
    if (!box || !items.valid(8, 2048)) return {};
    bool backup = false, heavy = primary_tags.heavy;
    for (auto p = items.first; p != items.end; p += 8) {
        const auto item = read<Address>(p);
        if (!item || item == gun.item || read<Address>(item, 0x30) != box || read<int>(item, 0x48) <= 0) continue;
        const auto t = tags(read<Address>(item, 0x40));
        backup |= t.firearm && supply::loaded_chamber(item);
        heavy |= t.weapon && t.heavy;
    }
    const int cap = read<int>(gun.def, 0x470);
    const bool single = cap == 1 || classify::def_has_tag(gun.def, "rocketlauncher");
    const auto vit = read<Address>(read<Address>(actor, 0x578), 0x18);
    using Health = float (*)(Address);
    return {
        policy::rearm(primary_tags.firearm, primary_tags.firearm ? supply::ammo_count(actor, gun) : 0, backup),
        policy::upgrade(primary_tags.weapon && primary_tags.rifle, primary_tags.specialist || specialist(actor), heavy),
        vit && squad::policy::needs_bandage(s::native<Health>(0x93D880)(vit), s::native<Health>(0x93D800)(vit),
                                            stock(actor, Need::corpse_bandage, 0).total),
        primary_tags.firearm && squad::policy::needs_ammo(s::loose_ammo(actor, gun.def).total, cap, single),
        gun.def,
        single ? 1 : cap};
}
struct Candidate {
    Need need{};
    s::Identity source{};
    Address item{}, def{}, gun{};
    int load{};
    float distance{};
};
inline bool better(const Candidate& candidate, const Candidate& best) {
    if (!best.source.ptr) return true;
    if (policy::rank(candidate.need) != policy::rank(best.need))
        return policy::rank(candidate.need) < policy::rank(best.need);
    if (candidate.distance != best.distance) return candidate.distance < best.distance;
    if (candidate.source.id != best.source.id) return candidate.source.id < best.source.id;
    return candidate.need == Need::corpse_bandage && best.need == Need::corpse_ammo;
}
enum class Phase {
    idle,
    start,
    stowing,
    collect,
    walking,
    exchange,
    returning,
    cooldown
};
struct Job {
    squad::Person person{};
    Candidate target{};
    Phase phase{};
    Vec3 home{}, endpoint{};
    Address stowed{}, stowed_def{}, collect{};
    s::OrderRef move{};
    med::ExchangeRef transfer{};
    unsigned since{}, wait{};
    int before{}, amount{};
    bool hold{}, success{}, return_issued{}, preempted{};
};
inline store::GrowableTable<Job, 32, 2048> jobs;
inline Address scene{};
inline unsigned clock{};
inline bool ticking{};
inline std::atomic<bool> stopping{};
inline std::atomic<unsigned> admissions{};
inline bool active(const Job& j) {
    return j.person.key.ptr && j.phase != Phase::idle && j.phase != Phase::cooldown;
}
inline Job* find(Address actor) {
    for (auto& j : jobs)
        if (j.person.key.matches(actor)) return &j;
    return nullptr;
}
inline bool reserved(Address source) {
    for (const auto& j : jobs)
        if (active(j) && j.phase != Phase::returning && j.target.source.matches(source)) return true;
    return false;
}
inline bool occupied(Address a) {
    const auto* j = find(a);
    return (j && active(*j) && !j->preempted) || reserved(a);
}
inline void preempt(Address actor) {
    if (auto* j = find(actor); j && active(*j)) {
        j->preempted = true;
        j->move.abandon();
    }
}
inline Candidate choose(const World& w, Address actor, const Demand& d) {
    Candidate best{};
    if (!d.rearm && !d.upgrade && !d.bandage && !d.ammo) return best;
    const auto self = s::pos(actor);
    const auto radius = config::active().tuning.scavenge_search_radius_m * w.length;
    s::entities(w, [&](Address source) {
        if (source == actor || !s::within(self, s::pos(source), radius)) return;
        auto consider = [&](Need need, Address item) {
            if (!item) return;
            Candidate c{need,
                        s::identity(source),
                        item,
                        read<Address>(item, 0x40),
                        d.gun,
                        d.load,
                        s::smoke::distance2(self, s::pos(source))};
            if (!policy::firearm(need)) c.source.mid = 0; // Corpse can outlive its active actor-map entry.
            if (better(c, best) && !reserved(source) && !squad::occupied(source) &&
                !s::visible_near_point(actor, s::pos(source), 18 * w.length))
                best = c;
        };
        if (auto item = ground_item(source)) {
            const auto def = read<Address>(item, 0x40);
            const auto t = tags(def);
            if (!t.weapon || read<int>(item, 0x48) <= 0) return;
            const bool loaded = supply::loaded_chamber(item);
            if (d.rearm && t.light && (loaded || s::loose_ammo(actor, def).total > 0)) consider(Need::rearm, item);
            if (d.upgrade && t.heavy && loaded) consider(Need::heavy_weapon, item);
        } else if ((d.bandage || d.ammo) && corpse(source) && s::inventory_free(source)) {
            if (d.bandage) consider(Need::corpse_bandage, stock(source, Need::corpse_bandage, 0).item);
            if (d.ammo) consider(Need::corpse_ammo, stock(source, Need::corpse_ammo, d.gun).item);
        }
    });
    return best;
}

inline bool authorized(Address actor) {
    return s::autonomous_if(actor, true, [](unsigned pol, unsigned flags) {
        return ((pol >> 12) & 3) <= 1 && s::policy_allows(pol, flags, false);
    });
}
inline bool current_collect(const Job& j, Address actor) {
    return j.collect && read<Address>(j.person.brain, 0xf0) == j.collect &&
           read<Address>(j.collect) == s::base + runtime::table().vt.collect_order &&
           read<Address>(j.collect, 8) == actor + 0xaf0;
}
inline bool is_collect(const Job& j, Address actor) {
    return current_collect(j, actor) && read<Address>(j.collect, 0x38) == j.target.source.ptr;
}
// Stock has submitted PickupToRight at 0x8E3980. Its own operation reserves
// extender+0x98 and can unlink the entity before the hand transfer is complete.
inline bool pickup_started(const Job& j, Address actor) {
    if (!current_collect(j, actor)) return false;
    const auto link = read<Address>(j.collect, 0x38);
    return (!link || link == j.target.source.ptr) && read<int>(j.collect, 0x48) >= 0 && read<int>(j.collect, 0x50) == 1;
}
inline bool received(const Job& j, Address actor) {
    const auto hand = hand_item(actor);
    return hand && hand == j.target.item && read<Address>(hand, 0x40) == j.target.def;
}
inline bool can_work(const Job& j, const World& w, Address actor) {
    return !j.preempted && j.person.matches(actor) && authorized(actor) && s::hold_position(j.person.brain) == j.hold &&
           coord::instance().can_act(actor, module, priority, j.person.key.id, w.now) &&
           !s::visible_near(actor, 18 * w.length) &&
           (s::free_slot(actor) || s::current_order(actor, j.move.get()) || is_collect(j, actor) ||
            (j.phase == Phase::collect && pickup_started(j, actor)));
}
inline bool safe_target(const Job& j, const World& w, Address actor, Address source) {
    return source && !squad::occupied(source) &&
           s::within(s::pos(actor), s::pos(source), config::active().tuning.scavenge_search_radius_m * w.length) &&
           !s::visible_near_point(actor, s::pos(source), 18 * w.length) &&
           (policy::firearm(j.target.need)
                ? ground_item(source) == j.target.item && read<Address>(j.target.item, 0x40) == j.target.def
                : corpse(source));
}
inline bool permit(const World& w, Address actor, Address order) {
    const auto* j = find(actor);
    if (!j || !active(*j) || stopping || !config::active().modules.battlefield_scavenge ||
        scene != s::entity_scene(w) || w.now < clock || w.now - j->since >= w.duration(14) || j->move.get() != order ||
        !can_work(*j, w, actor))
        return false;
    if (j->phase == Phase::returning) return j->return_issued && !s::visible_near_point(actor, j->home, 18 * w.length);
    const auto source = s::resolve(w, j->target.source);
    return j->phase == Phase::walking && safe_target(*j, w, actor, source) && s::inventory_free(actor) &&
           s::inventory_free(source) && s::within(j->endpoint, s::pos(source), 3.5f * w.length);
}
inline void cooldown(Job& j, const World& w) {
    j.move.abandon();
    coord::instance().release(j.person.key.ptr, module);
    j.phase = Phase::cooldown;
    j.since = w.now;
    j.wait = w.duration(j.success ? 15 : 8);
}
inline void finish(Job& j, const World& w, bool may_return) {
    auto actor = s::resolve(w, j.person.key);
    if (j.transfer.action >= 0) med::cancel(j.transfer, j.person.matches(actor) ? actor : 0);
    actor = s::resolve(w, j.person.key);
    // Never clear a replacement, new player command, or actor now under Direct Control.
    if (actor && j.person.matches(actor) && authorized(actor) && is_collect(j, actor))
        s::set_brain_order(j.person.brain, 0);
    j.collect = 0;
    if (may_return && j.hold && j.phase != Phase::returning && actor && s::resolve(w, j.person.key) == actor &&
        can_work(j, w, actor) && !s::within(s::pos(actor), j.home, 2 * w.length) &&
        !s::visible_near_point(actor, j.home, 18 * w.length)) {
        j.phase = Phase::returning;
        j.since = w.now;
    } else
        cooldown(j, w);
}
enum class Receipt {
    pending,
    success,
    failure
};
inline Receipt exchange_receipt(const Job& j, Address actor, Address source) {
    if (auto op = med::queued(j.transfer, actor))
        return med::exclusive_exchange(op, actor, source) ? Receipt::pending : Receipt::failure;
    if (!j.transfer.transfer ||
        (j.transfer.transfer->generation == j.transfer.transfer_serial &&
         j.transfer.transfer->operation.load() == j.transfer.operation) ||
        !s::inventory_free(actor) || (source && !s::inventory_free(source)))
        return Receipt::failure;
    return stock(actor, j.target.need, j.target.gun).total >= static_cast<long long>(j.before) + j.amount
               ? Receipt::success
               : Receipt::failure;
}
struct NativeWork {
    Job& j;
    const World& w;
    Address actor, source;
    bool idle() const {
        return s::inventory_free(actor);
    }
    Address held() const {
        return hand_item(actor);
    }
    bool stow(Address item) const {
        if (!can_stow(actor, item)) return false;
        j.stowed = item;
        j.stowed_def = read<Address>(item, 0x40);
        return put_from_right(actor) >= 0;
    }
    bool stowed() const {
        return boxed_item(actor, j.stowed) && read<Address>(j.stowed, 0x40) == j.stowed_def;
    }
    bool valid() const {
        if (j.phase == Phase::collect && pickup_started(j, actor)) return true;
        if (j.phase == Phase::exchange && !source) return true; // An already submitted receipt belongs to the receiver.
        if (!safe_target(j, w, actor, source)) return false;
        if (!policy::firearm(j.target.need) || j.phase == Phase::collect) return true;
        const auto d = demand(actor);
        if (j.target.need == Need::heavy_weapon) return d.upgrade && supply::loaded_chamber(j.target.item);
        return d.rearm && (supply::loaded_chamber(j.target.item) || s::loose_ammo(actor, j.target.def).total > 0);
    }
    using CollectFactory = Address (*)(Address, s::Byte);
    bool collect(CollectFactory make = s::native<CollectFactory>(runtime::table().fn.collect_order),
                 s::Unary right = hand_item) const {
        if (!s::free_slot(actor)) return false;
        const auto item = ground_item(source);
        if (!item || (!supply::loaded_chamber(item) &&
                      (j.target.need != Need::rearm || s::loose_ammo(actor, j.target.def).total <= 0)))
            return false;
        const auto fresh = make(source, 0);
        if (!fresh) return false;
        if (!s::resolve(w, j.person.key) || !can_work(j, w, actor) || !s::free_slot(actor) ||
            s::resolve(w, j.target.source) != source || !safe_target(j, w, actor, source) || !idle() || right(actor)) {
            s::discard(fresh);
            return false;
        }
        j.collect = fresh;
        s::set_brain_order(j.person.brain, j.collect);
        return true;
    }
    bool collecting() const {
        return is_collect(j, actor) || pickup_started(j, actor);
    }
    bool picked_up() const {
        return received(j, actor);
    }
    bool in_reach() const {
        return s::within(s::pos(actor), s::pos(source), 3.5f * w.length);
    }
    bool walking() const {
        return j.move.get() != 0;
    }
    bool at_home() const {
        return s::within(s::pos(actor), j.home, 2 * w.length);
    }
    bool move(bool home) const {
        const bool found = home ? s::return_goal(w, actor, j.home, j.endpoint)
                                : s::approach(w, actor, s::pos(source), 3.5f * w.length, j.endpoint);
        return found && s::move(actor, j.endpoint, permit, j.move);
    }
    bool send() const {
        if (!s::inventory_free(source)) return false;
        const auto d = demand(actor);
        if (j.target.need == Need::corpse_ammo ? !d.ammo || d.gun != j.target.gun : !d.bandage) return false;
        const auto offer = stock(source, j.target.need, j.target.gun);
        if (!offer.item || offer.item != j.target.item || read<Address>(offer.item, 0x40) != j.target.def) return false;
        j.before = stock(actor, j.target.need, j.target.gun).total;
        j.amount = policy::amount(j.target.need, offer.count, j.target.load);
        return j.before >= 0 && j.amount > 0 &&
               med::begin_exchange(j.transfer, source, actor, offer.item, j.amount, actor);
    }
    Receipt receipt() const {
        return exchange_receipt(j, actor, source);
    }
};
// True on completion or failure. The caller handles a safe return to the post.
template <class Actions> bool advance(Job& j, const World& w, Actions& do_it) {
    if (j.phase == Phase::returning) {
        if (do_it.walking()) return false;
        if (j.return_issued || do_it.at_home()) return true;
        j.return_issued = true;
        return !do_it.move(true);
    }
    if (j.phase == Phase::collect && do_it.picked_up()) {
        if (!do_it.idle() || do_it.collecting()) return false;
        j.success = true;
        if (j.target.need == Need::rearm)
            ++stat_rearm;
        else
            ++stat_heavy_pickup;
        return true;
    }
    if (!do_it.valid()) return true;
    if (policy::firearm(j.target.need)) {
        if (j.phase == Phase::collect) return !do_it.collecting();
        if (!do_it.idle()) return j.phase != Phase::stowing;
        const auto held = do_it.held();
        if (j.phase == Phase::stowing && (held || !do_it.stowed())) return true;
        j.since = w.now;
        if (held) {
            j.phase = Phase::stowing;
            return !do_it.stow(held);
        }
        j.phase = Phase::collect;
        return !do_it.collect();
    }
    if (j.phase == Phase::exchange) {
        const auto receipt = do_it.receipt();
        if (receipt == Receipt::pending) return false;
        if (receipt == Receipt::success) {
            j.success = true;
            ++stat_corpse_loot;
        }
        return true;
    }
    if (!do_it.idle()) return true;
    if (do_it.walking()) return false;
    if (!do_it.in_reach()) {
        if (j.phase == Phase::walking) return true;
        j.phase = Phase::walking;
        j.since = w.now;
        return !do_it.move(false);
    }
    j.phase = Phase::exchange;
    j.since = w.now;
    return !do_it.send();
}
inline void sync(const World& w) {
    const auto current = s::entity_scene(w);
    coord::instance().sync_world(current, w.now);
    if (scene == current && clock == w.now) return;
    const bool reset = scene != current || w.now < clock;
    for (auto& j : jobs) {
        if (!j.person.key.ptr) continue;
        if (reset) {
            j.move.abandon();
            // A scene boundary invalidates both entity and manager identities.
            coord::instance().release(j.person.key.ptr, module);
            j = {};
        } else if (!active(j)) {
            if (w.now - j.since >= j.wait) j = {};
        } else if (w.now - j.since >= w.duration(14)) {
            const auto actor = s::resolve(w, j.person.key);
            if (!actor || !j.person.matches(actor) || !authorized(actor)) cooldown(j, w);
        }
    }
    clock = w.now;
    scene = current;
}
inline Job* reserve() {
    for (auto& j : jobs)
        if (!j.person.key.ptr) return &j;
    return store::try_append_slot(jobs);
}
} // namespace detail

inline void tick(Address actor) {
    using namespace detail;
    World w{};
    if (ticking || !s::callback(actor, w) || !runtime::actor_ai_allowed(actor) ||
        !runtime::is_host_main_thread(w.base) || !(read<s::Byte>(actor, 0x30) & 4))
        return;
    struct Frame {
        Frame() {
            ticking = true;
            ++admissions;
        }
        ~Frame() {
            --admissions;
            ticking = false;
        }
    } frame;
    sync(w);
    coord::instance().register_provider(module, priority, occupied, preempt);
    Job* j = find(actor);
    const bool enabled = !stopping && s::accepting && config::active().modules.battlefield_scavenge;
    if (j && active(*j)) {
        if (!enabled || !can_work(*j, w, actor)) {
            finish(*j, w, false);
            return;
        }
        if (w.now - j->since >= w.duration(14)) {
            finish(*j, w, true);
            return;
        }
        const auto src = s::resolve(w, j->target.source);
        NativeWork actions{*j, w, actor, src};
        if (advance(*j, w, actions)) finish(*j, w, true);
        return;
    }
    if (j || !enabled || !authorized(actor) || !s::free_slot(actor) || !s::inventory_free(actor) ||
        !coord::instance().can_act(actor, module, priority, read<unsigned>(actor, 0xc0), w.now))
        return;
    j = reserve();
    if (!j) return;
    j->person = squad::Person::from(actor);
    j->since = w.now;
    j->wait = w.duration(1.5f);
    if (s::visible_near(actor, 18 * w.length)) return;
    const auto chosen = choose(w, actor, demand(actor));
    if (!chosen.source.ptr || !coord::instance().claim(actor, j->person.key.id, module, priority, w.now)) return;
    j->target = chosen;
    j->phase = Phase::start;
    j->home = s::pos(actor);
    j->hold = s::hold_position(j->person.brain);
}
inline void begin_shutdown() {
    detail::stopping = true;
}
inline bool quiescent() {
    return !detail::admissions && !s::watched_count && !med::transfer_count;
}
inline void after_hooks_removed() {
    for (auto& j : detail::jobs) {
        j.move.abandon();
        coord::instance().release(j.person.key.ptr, module);
    }
    detail::jobs.release();
    coord::instance().register_provider(module, priority, nullptr, nullptr);
    detail::scene = detail::clock = 0;
    detail::stopping = false;
    stat_rearm = stat_heavy_pickup = stat_corpse_loot = 0;
}
} // namespace goh::scavenge
