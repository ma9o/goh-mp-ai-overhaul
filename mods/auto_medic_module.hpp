#pragma once

#include "ai_support_26734796.hpp"

namespace goh::auto_medic {
using namespace ai_support;
inline constexpr bool runtime_allowed = false;
inline constexpr float patient_metres = 36.f, donor_metres = 24.f;
inline std::uint32_t stat_claims = 0;
inline std::uint32_t stat_fetches = 0;
inline std::uint32_t stat_smokes = 0;
inline std::uint32_t stat_revives = 0;
inline std::uint32_t stat_returns = 0;
inline std::uint32_t stat_patient_backoffs = 0;
enum class Phase {
    scan,
    fetch,
    exchange,
    approach,
    screen,
    revive,
    returning,
    cooldown
};
// The stock manager owns scheduling and deletion on the host and vanilla peers.
// Observe destruction to distinguish operation identities reused at the same address.
struct Transfer {
    std::atomic<Address> operation{};
    std::uint64_t generation{};
};
inline store::StableTable<Transfer, 32, 4096> transfers{};
inline std::atomic<unsigned> transfer_count{};
inline std::uint64_t transfer_generation{};
inline Destroy original_transfer_destroy{};
inline Transfer* tracked_transfer(Address operation) {
    if (transfer_count.load())
        for (auto& t : transfers)
            if (t.operation.load() == operation) return &t;
    return nullptr;
}
inline Address transfer_destroy(Address operation, unsigned flags) {
    runtime::HookGuard guard(smoke::hook_gate);
    if (auto* t = tracked_transfer(operation)) {
        if (t->operation.exchange(0) == operation) --transfer_count;
    }
    return original_transfer_destroy(operation, flags);
}
struct ExchangeRef {
    Address operation{};
    int action{-1};
    Transfer* transfer{};
    std::uint64_t transfer_serial{};
};
struct Job : ExchangeRef {
    Identity medic{}, patient{}, donor{};
    OrderRef order{};
    Phase phase{Phase::scan};
    unsigned start{}, since{}, wait{}, player{}, donor_player{};
    Address scene{}, brain{}, smoke_def{};
    std::uint64_t ticket{};
    Vec3 endpoint{}, screened_at{}, home{}, previous{};
    float remaining{}, travelled{};
    unsigned progress_at{};
    bool released{}, return_home{}, return_issued{};
    std::array<Identity, 16> previous_clouds{};
    unsigned previous_count{};
};
inline store::GrowableTable<Job, 32, 4096> jobs{};
inline Address scene{};
inline unsigned last_tick{};
inline bool busy{}, release_installed{};
inline bool expired(const World& w, const Job& j) {
    return (j.phase != Phase::returning && w.now - j.start >= w.duration(70)) || w.now - j.since >= j.wait;
}
inline void phase(Job& j, const World& w, Phase p, float seconds) {
    j.phase = p;
    j.since = w.now;
    j.wait = w.duration(seconds);
}
inline void revoke(Job& j) {
    j.order.abandon();
    for (auto& r : smoke::reservations)
        if (j.ticket && r.ticket == j.ticket && !r.released) r = {};
}
inline Address queued(const ExchangeRef& j, Address donor) {
    if (!donor || !j.transfer || j.transfer->generation != j.transfer_serial ||
        j.transfer->operation.load() != j.operation || !j.operation)
        return 0;
    const auto ops = span(manager(donor), 0xA8);
    if (ops.valid(8, 512))
        for (auto p = ops.first; p != ops.end; p += 8) {
            auto op = read<Address>(p);
            if (op == j.operation && read<Address>(op) == base + 0x10FF790 && read<int>(op, 0xF0) == j.action &&
                read<Address>(op, 0x88) == donor)
                return op;
        }
    return 0;
}
inline void cancel(ExchangeRef& j, Address donor) {
    if (j.action >= 0 && queued(j, donor)) native<void (*)(Address, const int*)>(0x7DDDC0)(manager(donor), &j.action);
    j = {};
}
inline void cancel_exchange(const World& w, Job& j) {
    if (j.action >= 0) cancel(j, resolve(w, j.donor));
}
// Reserve a destructor watch before stock submission. A reused operation address
// or action ID alone cannot identify a transfer after this call returns.
template <class Submit> inline bool begin_owned_exchange(ExchangeRef& j, Address owner_entity, Submit submit) {
    if (!release_installed || !original_transfer_destroy || j.action >= 0) return false;
    Transfer* mark = nullptr;
    for (auto& t : transfers)
        if (!t.operation.load()) {
            mark = &t;
            break;
        }
    if (!mark) mark = transfers.append_slot();
    if (!mark) return false;
    mark->operation.store(1); // Reserved across synchronous native callbacks.
    const int action = submit();
    if (action >= 0) {
        const auto ops = span(manager(owner_entity), 0xA8);
        if (ops.valid(8, 512))
            for (auto p = ops.first; p != ops.end; p += 8) {
                const auto op = read<Address>(p);
                if (!op || read<Address>(op) != base + 0x10FF790 || read<int>(op, 0xF0) != action ||
                    read<Address>(op, 0x88) != owner_entity)
                    continue;
                if (!++transfer_generation) ++transfer_generation;
                j = {op, action, mark, transfer_generation};
                mark->generation = transfer_generation;
                ++transfer_count;
                mark->operation.store(op);
                return true;
            }
        // This ID came directly from the submitter during this serial call.
        native<void (*)(Address, const int*)>(0x7DDDC0)(manager(owner_entity), &action);
    }
    mark->operation.store(0);
    return false;
}
inline bool begin_exchange(ExchangeRef& j, Address donor, Address receiver, Address item, int amount) {
    return begin_owned_exchange(j, donor, [&] { return exchange_amount(donor, receiver, item, amount); });
}
inline bool exclusive_exchange(Address op, Address donor, Address receiver) {
    const auto src = manager(donor), dst = manager(receiver);
    const auto queue = span(src, 0xA8), dest = span(dst, 0xA8);
    return src && dst && queue.valid(8, 1) && queue.end - queue.first == 8 && dest.first == dest.end &&
           (!read<Address>(src, 0xC8) || read<Address>(src, 0xC8) == op) &&
           (!read<Address>(dst, 0xC8) || read<Address>(dst, 0xC8) == op);
}
inline void finish(const World& w, Job& j, float seconds = 12) {
    revoke(j);
    phase(j, w, Phase::cooldown, seconds);
    cancel_exchange(w, j);
}
struct PatientBackoff {
    Identity medic{}, patient{};
    runtime::Tick until{};
    unsigned attempts{};
};
inline store::GrowableTable<PatientBackoff, 32, 2048> patient_backoffs{};
inline Address patient_scene{};

inline bool patient_in_backoff(const World& w, Address medic, Address patient) {
    for (const auto& row : patient_backoffs)
        if (row.medic.matches(medic) && row.patient.matches(patient))
            return static_cast<std::int32_t>(row.until - w.now) > 0;
    return false;
}

inline bool valid_patient(const World& w, Address actor, Address p);
inline bool record_patient_failure(const World& w, Address medic, Identity patient_id, float seconds) {
    const auto patient = resolve(w, patient_id);
    if (!medic || !valid_patient(w, medic, patient)) return false;
    PatientBackoff *slot = nullptr, *expired_slot = nullptr;
    for (auto& row : patient_backoffs) {
        if (row.medic.matches(medic) && row.patient.matches(patient)) {
            slot = &row;
            break;
        }
        if (!row.medic.ptr)
            slot = &row;
        else if (!expired_slot && static_cast<std::int32_t>(row.until - w.now) <= 0)
            expired_slot = &row;
    }
    if (!slot) slot = patient_backoffs.append_slot();
    if (!slot) slot = expired_slot;
    if (!slot) return false;
    if (!slot->medic.matches(medic) || !slot->patient.matches(patient)) *slot = {identity(medic), patient_id};
    slot->attempts = (std::min)(3u, slot->attempts + 1);
    const float delay = slot->attempts == 1 ? seconds : (slot->attempts == 2 ? 25.f : 45.f);
    slot->until = w.now + w.duration(delay);
    ++stat_patient_backoffs;
    return true;
}

inline void refresh(const World& w) {
    const auto current_scene = entity_scene(w);
    const bool reset = scene != w.scene || patient_scene != current_scene || w.now - last_tick > 0x7fffffffu;
    if (reset || w.now < last_tick) patient_backoffs.clear();
    patient_scene = current_scene;
    for (auto& j : jobs) {
        if (!j.medic.ptr) continue;
        if (reset) {
            cancel_exchange(w, j);
            revoke(j);
            j = {};
            continue;
        }
        if (j.phase == Phase::cooldown) {
            if (w.now - j.since >= j.wait) j = {};
        } else if (expired(w, j)) {
            bool failed = false;
            if (j.scene == w.scene && j.patient.ptr) {
                failed = record_patient_failure(w, resolve(w, j.medic), j.patient, 12);
                j.patient = {};
            }
            finish(w, j, failed ? 4.f : 12.f);
        }
    }
    scene = w.scene;
    last_tick = w.now;
}
inline Job* find(Address actor) {
    for (auto& j : jobs)
        if (j.medic.matches(actor)) return &j;
    return nullptr;
}
inline bool patient_claimed(Address a) {
    for (const auto& j : jobs)
        if (j.patient.matches(a)) return true;
    return false;
}
inline bool occupied(Address a) {
    for (const auto& j : jobs)
        if (j.phase != Phase::cooldown && (j.medic.matches(a) || j.donor.matches(a))) return true;
    return false;
}
inline bool native_claim(const World& w, Address target, Address self) {
    bool assigned = false;
    actors(w, [&](Address a) {
        if (assigned || a == self || !(read<Byte>(a, 0x30) & 4)) return;
        auto b = read<Address>(a, 0xDC8);
        if (!b || read<Address>(b) != base + 0x110FF88) return;
        auto o = read<Address>(b, 0xF0);
        for (int depth = 0; o && depth < 32; ++depth) {
            if (read<Address>(o, 8) != a + 0xAF0) break;
            if (read<Address>(o) == base + 0x1080638 && read<Address>(o, 0x48) == target) {
                assigned = true;
                break;
            }
            o = smoke::orders::active_child(base, o);
        }
    });
    return assigned;
}
inline bool valid_patient(const World& w, Address actor, Address p) {
    return p && p != actor && smoke::patient(w, p) && read<std::uint16_t>(p, 0xDAA) &&
           smoke::allies(base, owner(actor), owner(p)) && within(pos(actor), pos(p), 42 * w.length);
}
inline bool valid(const World& w, const Job& j, Address actor, Address& patient) {
    if (j.scene != w.scene || !j.medic.matches(actor) || j.phase == Phase::cooldown || expired(w, j) ||
        !autonomous(actor, true) || owner(actor) != j.player || read<Address>(actor, 0xDC8) != j.brain)
        return false;
    patient = resolve(w, j.patient);
    if (!valid_patient(w, actor, patient)) return false;
    const auto order = j.order.get();
    if (!read<Byte>(patient, 0x12BA)) return !native_claim(w, patient, actor);
    return j.phase == Phase::revive && order && current_order(actor, order) && read<Address>(order, 0x48) == patient &&
           read<int>(order, 0x44) == 6;
}
inline Address spare(Address actor, bool require_extra = false) {
    const auto b = box(actor);
    const Span items = span(b, 0x10);
    if (!b || !items.valid(8, 2048)) return 0;
    Address first{};
    for (auto p = items.first; p != items.end; p += 8) {
        const auto i = read<Address>(p);
        if (!i || read<Address>(i, 0x30) != b || read<Address>(i, 0x38) ||
            read<Address>(i, 0x10) != read<Address>(i, 0x18) || read<Address>(i, 0x50) || read<int>(i, 0x48) <= 0 ||
            !smoke::IsPureSmokeDefinition(read<Address>(i, 0x40)))
            continue;
        if (read<int>(i, 0x48) >= 2) return i;
        if (first && first != i) return first;
        first = i;
    }
    return require_extra ? 0 : first;
}
inline bool is_medic_actor(Address a) {
    if (!a || !(read<Byte>(a, 0x30) & 4)) return false;
    const auto b = read<Address>(a, 0xDC8);
    if (!b || read<Address>(b) != base + 0x110FF88) return false;
    const auto root = read<Address>(b, 0xE0);
    return root && read<Address>(root) == base + 0x1110CD0 && read<Address>(root, 0x30) == b &&
           native<Test>(0x96AAF0)(root) != 0;
}
inline bool donor_available(Address d) {
    if (!autonomous(d, false, true) || !free_slot(d)) return false;
    auto brain = read<Address>(d, 0xDC8);
    if (read<Address>(brain) == base + 0x110FF88) return true;
    auto crew = span(read<Address>(d, 0x568), 0x10);
    return (read<Byte>(d, 0x30) & 8) && crew.first && crew.end > crew.first &&
           !(read<unsigned>(read<Address>(d, 0xDE0), 0x48) & 0x10) && working_hull(d);
}
inline bool donor_ok(const World& w, Address medic, Address d) {
    return d && medic != d && donor_available(d) && !(supply_assignment && supply_assignment(d)) && inventory_free(d) &&
           smoke::allies(base, owner(medic), owner(d)) && within(pos(medic), pos(d), donor_metres * w.length);
}
inline bool can_handoff(const World& w, Address medic, Address donor) {
    auto m = pos(medic), d = pos(donor);
    if (!within(m, d, 2 * w.length)) return false;
    Vec3 middle{(m.x + d.x) * .5f, (m.y + d.y) * .5f, (m.z + d.z) * .5f};
    if (!ground(w, middle)) return false;
    PathContext from_medic(medic), from_donor(donor);
    return corridor(w, from_medic, m, middle) && corridor(w, from_donor, d, middle);
}
inline bool cloud(const World&, Job&, bool snapshot);
inline bool screens(const World&, Vec3 patient, Vec3 point, const Danger&);
inline bool permit(const World& w, Address actor, Address order) {
    auto* j = find(actor);
    if (!j || j->order.get() != order || j->scene != w.scene || j->phase == Phase::cooldown || expired(w, *j) ||
        !autonomous(actor, true) || owner(actor) != j->player || read<Address>(actor, 0xDC8) != j->brain ||
        !current_order(actor, order))
        return false;
    if (j->phase == Phase::returning) {
        return j->return_home && hold_position(j->brain) && w.now - j->progress_at < w.duration(9) &&
               j->travelled <= 55.f * w.length && !post_overrun(w, actor, j->home, 10.f);
    }
    Address patient{};
    if (!valid(w, *j, actor, patient)) return false;
    if (j->phase == Phase::fetch) {
        const auto d = resolve(w, j->donor);
        return donor_ok(w, actor, d) && j->donor_player == owner(d) && spare(d, is_medic_actor(d)) &&
               within(j->endpoint, pos(d), 2 * w.length);
    }
    if (j->phase == Phase::revive) {
        if (read<Address>(order, 0x48) != patient) return false;
        auto d = danger(w, actor, pos(patient), 25);
        if (!j->released) return !d.unsafe;
        return within(pos(patient), j->screened_at, 2 * w.length) && cloud(w, *j, false) &&
               screens(w, pos(patient), j->endpoint, d);
    }
    return j->phase == Phase::approach;
}

inline Address nearest(const World& w, Job& j, Address medic, bool donor, const Vec3* home_anchor = nullptr) {
    Address best = 0;
    const float search_m = config::active().tuning.medic_search_radius_m;
    float cap = (donor ? donor_metres : search_m) * w.length, best2 = cap * cap;
    actors(w, [&](Address a) {
        if (a == medic) return;
        float d2 = smoke::distance2(pos(medic), pos(a));
        if (!(d2 <= best2) || (best && d2 == best2 && read<unsigned>(a, 0xC0) >= read<unsigned>(best, 0xC0))) return;
        if (donor) {
            if (!donor_ok(w, medic, a) || occupied(a) || !spare(a, is_medic_actor(a))) return;
            if (within(pos(medic), pos(a), 2 * w.length) && !can_handoff(w, medic, a)) return;
        } else {
            if (!valid_patient(w, medic, a) || read<Byte>(a, 0x12BA) || patient_in_backoff(w, medic, a) ||
                patient_claimed(a) || native_claim(w, a, medic))
                return;
            if (home_anchor && !within(*home_anchor, pos(a), search_m * w.length)) return;
        }
        Vec3 goal{};
        if (!approach(w, medic, pos(a), (donor ? 1.8f : 2.5f) * w.length, goal)) return;
        best = a;
        best2 = d2;
        j.endpoint = goal;
    });
    return best;
}
inline void complete_or_return(const World& w, Job& j, Address actor, bool can_chain, float cooldown_sec = 12) {
    // Re-resolve before releasing or chaining. A completed revive, death or removed entity incurs no penalty.
    if (j.patient.ptr && record_patient_failure(w, actor, j.patient, 12)) cooldown_sec = (std::min)(4.f, cooldown_sec);
    revoke(j);
    cancel_exchange(w, j);
    j.patient = {};
    j.donor = {};
    j.smoke_def = 0;
    j.ticket = 0;
    j.released = false;
    j.previous_count = 0;
    if (can_chain && autonomous(actor, true) && free_slot(actor) && inventory_free(actor)) {
        const Vec3* anchor = j.return_home ? &j.home : nullptr;
        if (auto next_p = nearest(w, j, actor, false, anchor)) {
            j.patient = identity(next_p);
            j.start = w.now;
            phase(j, w, Phase::scan, 3);
            ++stat_claims;
            if (log_sink) {
                log_sink("AUTO-MEDIC CLAIM #%u: medic=0x%llx patient=0x%llx", stat_claims, (unsigned long long)actor,
                         (unsigned long long)next_p);
            }
            return;
        }
    }
    if (j.return_home && hold_position(j.brain) && autonomous(actor, true) &&
        !within(pos(actor), j.home, 2.f * w.length) && !post_overrun(w, actor, j.home, 10.f)) {
        phase(j, w, Phase::returning, 35);
        j.endpoint = {};
        j.previous = pos(actor);
        j.travelled = 0.f;
        j.progress_at = w.now;
        j.remaining = std::sqrt(smoke::distance2(pos(actor), j.home));
        j.return_issued = false;
        return;
    }
    finish(w, j, cooldown_sec);
}
inline void revive(const World& w, Job& j, Address medic, Address patient) {
    if (!free_slot(medic) || native_claim(w, patient, medic)) {
        complete_or_return(w, j, medic, false);
        return;
    }
    phase(j, w, Phase::revive, 35);
    if (!install(medic, native<Unary>(0x378670)(patient), permit, j.order)) {
        complete_or_return(w, j, medic, false);
        return;
    }
    ++stat_revives;
    if (log_sink) {
        log_sink("AUTO-MEDIC REVIVE #%u: medic=0x%llx patient=0x%llx screened=%d", stat_revives,
                 (unsigned long long)medic, (unsigned long long)patient, j.released ? 1 : 0);
    }
}
inline bool aim(const World& w, Address actor, Vec3 anchor, const Danger& d, float range, Vec3& point) {
    if (d.direction && smoke::screen_point(w, pos(actor), anchor, d.towards, range, point)) return true;
    point = anchor;
    return ground(w, point) && within(pos(actor), point, range);
}
inline void screen(const World& w, Job& j, Address actor, Address patient, Address item) {
    const auto def = read<Address>(item, 0x40), squad = read<Address>(j.brain, 0x100);
    const auto d = danger(w, actor, pos(patient), 25);
    Vec3 point{};
    const auto range = smoke::nominal_throw_range(base, def);
    if (!(range > 0)) {
        complete_or_return(w, j, actor, false);
        return;
    }
    if (!aim(w, actor, pos(patient), d, range, point)) {
        if (j.phase == Phase::approach || !approach(w, actor, pos(patient), 5 * w.length, j.endpoint) ||
            within(pos(actor), j.endpoint, w.length)) {
            complete_or_return(w, j, actor, false);
            return;
        }
        phase(j, w, Phase::approach, 16);
        j.progress_at = w.now;
        j.remaining = std::sqrt(smoke::distance2(pos(actor), j.endpoint));
        if (!move(actor, j.endpoint, permit, j.order)) complete_or_return(w, j, actor, false);
        return;
    }
    smoke::reset_scene_cache(w);
    auto* r = smoke::available(w, actor, squad, pos(actor), false);
    if (!r || !release_installed || smoke::image_base != base) {
        complete_or_return(w, j, actor, false);
        return;
    }
    const auto path = smoke::orders::inspect(base, j.brain);
    if (!path.allowed || !free_slot(actor)) {
        complete_or_return(w, j, actor, false);
        return;
    }
    smoke::reserve(*r, w, actor, squad, pos(actor), false);
    r->medical = true;
    r->protected_actor = patient;
    r->protected_id = j.patient.id;
    r->known_threat = d.towards;
    j.ticket = r->ticket;
    j.screened_at = pos(patient);
    j.endpoint = point;
    phase(j, w, Phase::screen, 9);
    auto shot = smoke::make_smoke_order(base, def, point);
    if (shot && smoke::track_smoke_order(w, actor, shot, def, point, j.ticket) &&
        smoke::orders::install(base, j.brain, path, shot)) {
        ++stat_smokes;
        if (log_sink) {
            log_sink("AUTO-MEDIC SMOKE #%u: medic=0x%llx patient=0x%llx screen=(%.1f,%.1f)", stat_smokes,
                     (unsigned long long)actor, (unsigned long long)patient, point.x, point.y);
        }
        return;
    }
    if (shot) smoke::forget_smoke_order(shot);
    discard(shot);
    complete_or_return(w, j, actor, false);
}
// A paired send/spawn proves a throw, not that the grenade landed or
// activated. Wait for a newly observed stock occlusion volume near the screen
// (allowing for grenade bounce/roll during the activation fuse).
inline bool cloud(const World& w, Job& j, bool snapshot) {
    const Span entries = span(entity_scene(w), 0x48);
    if (!entries.valid(8, 131072)) return false;
    if (snapshot) j.previous_count = 0;
    for (Address p = entries.first; p != entries.end; p += 8) {
        auto e = read<Address>(p);
        if (!e || !(read<Byte>(e, 0x30) & 1) || (read<unsigned>(e, 0x38) & 0xA20) ||
            !within(pos(e), j.endpoint, 7 * w.length))
            continue;
        auto name = native_string(e + 0xF0);
        if (name != "smoke_volume_cylinder" && name != "smoke_volume_cylinder_sm") continue;
        if (snapshot) {
            if (j.previous_count == j.previous_clouds.size()) return false;
            j.previous_clouds[j.previous_count++] = identity(e);
        } else {
            bool old = false;
            for (unsigned i = 0; i < j.previous_count; ++i)
                old |= j.previous_clouds[i].matches(e);
            if (!old) return true;
        }
    }
    return snapshot;
}
inline bool screens(const World& w, Vec3 patient, Vec3 point, const Danger& d) {
    if (within(patient, point, 2 * w.length)) return true;
    if (!d.direction) return !d.unsafe;
    const float x = d.towards.x - patient.x, y = d.towards.y - patient.y, len = std::hypot(x, y);
    const float sx = point.x - patient.x, sy = point.y - patient.y;
    return len > 0 && sx * x + sy * y > 0 && sx * x + sy * y < len * len &&
           std::fabs(sx * y - sy * x) <= 5 * w.length * len;
}
// Decorate the single existing throw detour at creation. Stock smoke retains
// its original trampoline and handles every other firing context.
inline void release(Address shooter) noexcept {
    runtime::HookGuard guard(smoke::hook_gate);
    if (!smoke::serial_thread()) {
        smoke::smoke_release(shooter);
        return;
    }
    const auto e = smoke::lookup_smoke_intent(&smoke::SmokeIntent::shooter, shooter);
    Job* job = nullptr;
    if (e.generation && e.lease_ticket)
        for (auto& j : jobs)
            if (j.ticket == e.lease_ticket) {
                job = &j;
                break;
            }
    if (!job) {
        smoke::smoke_release(shooter);
        return;
    }
    const auto frame = smoke::quant_frame();
    auto f = frame ? frame->firing : nullptr;
    const bool synchronous = f && f->shooter == shooter;
    if (!synchronous && !e.owns_latch) {
        smoke::smoke_release(shooter);
        return;
    }
    if ((synchronous && (!f->permit || f->generation != e.generation)) || e.spent) return;
    World w{};
    Address p{};
    const auto weapon = read<Address>(shooter, 8);
    auto actor = weapon ? read<Address>(weapon, 0x90) : 0;
    // Animation-marker releases can arrive outside Hook_ActorSlot23 (where
    // current_actor() == 0); check serial host + world rather than callback().
    if (job->phase != Phase::screen || !accepting.load(std::memory_order_acquire) || !actor || !host() ||
        !serial_thread() || !smoke::world(base, w) || !valid(w, *job, actor, p) ||
        !smoke::matching_smoke_target(weapon, e.screen) || !smoke::smoke_may_continue(e, actor, w) || !e.child ||
        read<Address>(e.root, 0x30) != e.child || !within(pos(p), job->screened_at, 2 * w.length))
        return;
    auto d = danger(w, actor, pos(p), 25);
    if (!screens(w, pos(p), e.screen, d) || !cloud(w, *job, true)) return;
    if (!smoke::change_smoke_intent(e.generation, [](auto& entry) { entry.spent = true; })) return;
    if (!smoke::scoped_smoke_release(base, smoke::original_smoke_release, shooter, e.definition, e.screen, true))
        return;
    smoke::change_smoke_intent(e.generation, [](auto& entry) { entry.emitted = true; });
    const auto live = smoke::lookup_smoke_intent(&smoke::SmokeIntent::shooter, shooter);
    if (live.generation != e.generation || read<Address>(shooter, 8) != weapon || read<Address>(weapon, 0x90) != actor)
        return;
    smoke::receipt(actor, w.now, e.lease_ticket);
    if (job->ticket == e.lease_ticket && job->phase == Phase::screen) {
        job->released = true;
        phase(*job, w, Phase::screen, 12);
    }
}
inline std::uint32_t shot_exec(Address shot, std::uint32_t phase) {
    runtime::HookGuard guard(smoke::hook_gate);
    if (phase != 2 && smoke::serial_thread()) {
        auto intent = smoke::lookup_smoke_intent(&smoke::SmokeIntent::root, shot);
        if (intent.generation && intent.lease_ticket)
            for (auto& j : jobs)
                if (j.ticket == intent.lease_ticket) {
                    World w{};
                    Address p{};
                    auto actor = smoke::bound_smoke_actor(shot);
                    if (j.phase != Phase::screen || !accepting.load(std::memory_order_acquire) || !callback(actor, w) ||
                        !valid(w, j, actor, p))
                        return 2;
                    break;
                }
    }
    return smoke::smoke_shot_exec(shot, phase);
}
template <class Create, class Remove>
inline bool prepare_smoke_hooks(Address image, const char* digest, bool stock, Create create, Remove remove) {
    if (!image || !stock || !digest || std::strcmp(digest, executable_sha256) != 0 || smoke::image_base) return false;
    void* original_dtor = nullptr;
    auto dtor = reinterpret_cast<void*>(image + 0x7E94F0);
    if (create(dtor, reinterpret_cast<void*>(&transfer_destroy), &original_dtor)) return false;
    original_transfer_destroy = reinterpret_cast<Destroy>(original_dtor);
    auto dispatch = [&](void* entry, void* hook, void** original) {
        if (entry == reinterpret_cast<void*>(image + 0x8313B0)) hook = reinterpret_cast<void*>(&release);
        if (entry == reinterpret_cast<void*>(image + 0x903FE0)) hook = reinterpret_cast<void*>(&shot_exec);
        return create(entry, hook, original);
    };
    if (!smoke::prepare_hooks(image, digest, stock, dispatch, remove)) {
        remove(dtor);
        original_transfer_destroy = nullptr;
        return false;
    }
    release_installed = true;
    coord::instance().register_provider(coord::Module::auto_medic, coord::Priority::auto_medic, &occupied);
    return true;
}
inline void update_medic(const World& w, Address actor) {
    medical_assignment = &occupied;
    refresh(w);
    // A donor can receive a player command between the medic's actor updates.
    // Cancel only our still-queued transfer by its replicated action ID.
    for (auto& borrowed : jobs) {
        if (borrowed.phase != Phase::exchange || !borrowed.donor.matches(actor)) continue;
        if (!resolve(w, borrowed.patient) || !donor_available(actor) || borrowed.donor_player != owner(actor)) {
            const bool failed = record_patient_failure(w, resolve(w, borrowed.medic), borrowed.patient, 12);
            borrowed.patient = {};
            finish(w, borrowed, failed ? 4.f : 12.f);
        }
    }
    auto* j = find(actor);
    if (j && j->phase == Phase::cooldown) return;
    if (!autonomous(actor, true)) {
        if (j) finish(w, *j);
        return;
    }
    const Address b = read<Address>(actor, 0xDC8);
    if (!j) {
        if (occupied(actor) || (supply_assignment && supply_assignment(actor)) ||
            !coord::instance().can_act(actor, coord::Module::auto_medic, coord::Priority::auto_medic,
                                       read<std::uint32_t>(actor, 0xC0), w.now) ||
            !free_slot(actor) || !inventory_free(actor) || !box(actor) ||
            !native<Test>(0x96AAF0)(read<Address>(b, 0xE0)))
            return;
        for (auto& slot : jobs)
            if (!slot.medic.ptr) {
                j = &slot;
                break;
            }
        if (!j) j = jobs.append_slot();
        if (!j) return;
        j->medic = identity(actor);
        j->scene = w.scene;
        j->brain = b;
        j->start = w.now;
        j->player = owner(actor);
        j->home = pos(actor);
        j->return_home = hold_position(b);
        auto p = nearest(w, *j, actor, false);
        if (!p) {
            finish(w, *j, 2);
            return;
        }
        j->patient = identity(p);
        phase(*j, w, Phase::scan, 3);
        ++stat_claims;
        if (log_sink) {
            log_sink("AUTO-MEDIC CLAIM #%u: medic=0x%llx patient=0x%llx", stat_claims, (unsigned long long)actor,
                     (unsigned long long)p);
        }
    }
    if (j->phase == Phase::returning) {
        if (j->scene != w.scene || owner(actor) != j->player || read<Address>(actor, 0xDC8) != j->brain ||
            !j->return_home || !hold_position(j->brain) || post_overrun(w, actor, j->home, 10.f)) {
            finish(w, *j, 4);
            return;
        }
        if (within(pos(actor), j->home, 2.f * w.length)) {
            finish(w, *j, 3);
            return;
        }
        auto owned = j->order.get();
        if (!owned) {
            if (j->return_issued) {
                finish(w, *j, 3);
                return;
            }
            if (!free_slot(actor)) {
                if (w.now - j->since >= w.duration(2)) finish(w, *j, 4);
                return;
            }
            Vec3 goal{};
            if (!return_goal(w, actor, j->home, goal) || !move(actor, goal, permit, j->order)) {
                finish(w, *j, 4);
                return;
            }
            j->return_issued = true;
            j->endpoint = goal;
            j->previous = pos(actor);
            j->progress_at = w.now;
            j->remaining = std::sqrt(smoke::distance2(pos(actor), goal));
            ++stat_returns;
            if (log_sink) {
                log_sink("AUTO-MEDIC RETURN #%u: medic=0x%llx dist=%.1fm", stat_returns, (unsigned long long)actor,
                         j->remaining / w.length);
            }
            return;
        }
        if (!current_order(actor, owned)) {
            finish(w, *j, 4);
            return;
        }
        j->travelled += std::sqrt(smoke::distance2(j->previous, pos(actor)));
        j->previous = pos(actor);
        float rem = std::sqrt(smoke::distance2(pos(actor), j->endpoint));
        if (j->remaining - rem >= .5f * w.length) {
            j->progress_at = w.now;
            j->remaining = rem;
        }
        if (j->travelled > 55.f * w.length || w.now - j->progress_at >= w.duration(9)) finish(w, *j, 4);
        return;
    }
    Address p{};
    if (!valid(w, *j, actor, p)) {
        if (j->scene != w.scene || owner(actor) != j->player || read<Address>(actor, 0xDC8) != j->brain)
            finish(w, *j);
        else
            complete_or_return(w, *j, actor, true, 4);
        return;
    }
    if (j->phase == Phase::revive) {
        if (!j->order.get() || !current_order(actor, j->order.get())) complete_or_return(w, *j, actor, true, 4);
        return;
    }
    if (j->phase == Phase::screen) {
        if (j->released && free_slot(actor) && within(pos(p), j->screened_at, 2 * w.length) && cloud(w, *j, false) &&
            screens(w, pos(p), j->endpoint, danger(w, actor, pos(p), 25)))
            revive(w, *j, actor, p);
        return;
    }
    if (j->order.get()) {
        if (j->phase == Phase::approach) {
            const float remaining = std::sqrt(smoke::distance2(pos(actor), j->endpoint));
            if (j->remaining - remaining >= .5f * w.length) {
                j->remaining = remaining;
                j->progress_at = w.now;
            } else if (w.now - j->progress_at >= w.duration(9)) {
                complete_or_return(w, *j, actor, false);
                return;
            }
        }
        if (!permit(w, actor, j->order.get())) complete_or_return(w, *j, actor, false);
        return;
    }
    if (!free_slot(actor)) {
        complete_or_return(w, *j, actor, false);
        return;
    }
    auto item = smoke::FindCarriedPureSmokeItem(base, actor);
    if (j->phase == Phase::exchange) {
        auto d = resolve(w, j->donor);
        if (!d || !donor_available(d) || j->donor_player != owner(d) || !smoke::allies(base, owner(actor), owner(d)) ||
            !within(pos(actor), pos(d), 2 * w.length)) {
            complete_or_return(w, *j, actor, false);
            return;
        }
        if (auto op = queued(*j, d)) {
            if (!exclusive_exchange(op, d, actor)) {
                complete_or_return(w, *j, actor, false);
                return;
            }
            return;
        }
        if (!item || !inventory_free(actor)) {
            complete_or_return(w, *j, actor, false);
            return;
        }
    }
    if (item) {
        if (j->phase == Phase::exchange && read<Address>(item, 0x40) != j->smoke_def) {
            complete_or_return(w, *j, actor, false);
            return;
        }
        j->action = -1;
        j->operation = 0;
        j->donor = {};
        j->transfer = nullptr;
        screen(w, *j, actor, p, item);
        return;
    }
    if (j->phase == Phase::approach || !inventory_free(actor)) {
        complete_or_return(w, *j, actor, false);
        return;
    }
    auto d = resolve(w, j->donor);
    if (!j->donor.ptr) {
        d = nearest(w, *j, actor, true);
        if (d) {
            j->donor = identity(d);
            j->donor_player = owner(d);
        }
    }
    if (!donor_ok(w, actor, d) || j->donor_player != owner(d)) {
        auto threat = danger(w, actor, pos(p), 25);
        if (!threat.unsafe)
            revive(w, *j, actor, p);
        else
            complete_or_return(w, *j, actor, false);
        return;
    }
    if (!within(pos(actor), pos(d), 2 * w.length)) {
        if (j->phase == Phase::fetch) {
            complete_or_return(w, *j, actor, false);
            return;
        }
        phase(*j, w, Phase::fetch, 14);
        if (!move(actor, j->endpoint, permit, j->order)) complete_or_return(w, *j, actor, false);
        return;
    }
    const auto offered = spare(d, is_medic_actor(d));
    if (!offered || !box(actor) || !can_handoff(w, actor, d)) {
        complete_or_return(w, *j, actor, false);
        return;
    }
    j->smoke_def = read<Address>(offered, 0x40);
    phase(*j, w, Phase::exchange, 8);
    if (!begin_exchange(*j, d, actor, offered, 1)) {
        complete_or_return(w, *j, actor, false);
        return;
    }
    ++stat_fetches;
    if (log_sink) {
        log_sink("AUTO-MEDIC FETCH #%u: medic=0x%llx donor=0x%llx action=%d", stat_fetches, (unsigned long long)actor,
                 (unsigned long long)d, j->action);
    }
}
inline void tick(Address actor) {
    World w{};
    if (!config::active().modules.auto_medic || busy || !release_installed || smoke::image_base != base ||
        !accepting.load(std::memory_order_acquire) || !callback(actor, w))
        return;
    struct Scope {
        Scope() {
            busy = true;
        }
        ~Scope() {
            busy = false;
        }
    } scope;
    update_medic(w, actor);
}
inline void reset() {
    patient_backoffs.clear();
    patient_scene = 0;
    for (auto& j : jobs) {
        revoke(j);
        j = {};
    }
    scene = 0;
    last_tick = 0;
}
inline bool quiescent() {
    return transfer_count.load() == 0 && smoke::active_intents.load() == 0;
}
inline void after_hooks_removed() {
    reset();
    jobs.release();
    patient_backoffs.release();
    stat_patient_backoffs = 0;
    transfers.release();
    transfer_count.store(0, std::memory_order_relaxed);
    release_installed = false;
    original_transfer_destroy = nullptr;
    smoke::after_hooks_removed();
}
} // namespace goh::auto_medic
