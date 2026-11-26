#pragma once

#include "ai_support_26734796.hpp"
#include <mutex>

namespace goh::hit_the_dirt {
namespace s = ai_support;
namespace sim = auto_smoke;
using runtime::Address;
using runtime::Byte;
using runtime::Tick;
using s::read;
inline constexpr bool runtime_allowed = false;
inline constexpr unsigned hook_count = 1;
inline std::atomic<unsigned> stat_hit_dirt{}, stat_trapped_hides{};

namespace detail {
struct SignalDesc {
    int type;
    float radius;
};
using Publish = void (*)(const SignalDesc*, Address, const s::Vec3*, Address);
inline Publish original{};
inline runtime::HookGate gate{};
inline Address image{};
inline Address (*serial_actor)() = s::current_actor;

struct Blast {
    s::Vec3 pos{};
    float radius{};
    Tick tick{};
    unsigned source_player{};
};
struct Reaction {
    s::Identity actor{};
    Address brain{};
    Tick tick{};
};
struct Trap {
    Address order{};
    s::Identity actor{}, grenade{};
    Tick tick{};
};

inline std::mutex mutex;
inline std::array<Blast, 64> blasts{};
inline std::size_t head{}, count{};
inline Address scene{};
inline Tick clock{};
inline store::GrowableTable<Reaction, 32, 2048> reactions;
inline store::GrowableTable<Trap, 16, 512> traps;

inline bool sync_locked(const s::World& w) {
    const auto current = s::entity_scene(w);
    if (scene != current || clock > w.now) {
        head = count = 0;
        reactions.clear();
        traps.clear();
    }
    scene = current;
    clock = w.now;
    while (count && w.now - blasts[head].tick > w.duration(2.f)) {
        head = (head + 1) % blasts.size();
        --count;
    }
    return current != 0;
}
inline bool enabled() {
    return gate.active() && config::active().modules.hit_the_dirt;
}
inline bool authorized_thread() {
    return enabled() && s::serial_thread() && runtime::is_host_main_thread(image);
}
inline void publish(const SignalDesc* desc, Address source, const s::Vec3* pos, Address payload) {
    runtime::HookGuard frame(gate);
    s::World w{};
    if (authorized_thread() && desc && desc->type == 5 && pos && sim::finite(*pos) && std::isfinite(desc->radius) &&
        (!source || !(read<Byte>(source, 0x3a) & 1)) && sim::world(image, w) && desc->radius >= 15.f * w.length) {
        std::lock_guard<std::mutex> lock(mutex);
        if (sync_locked(w)) {
            blasts[(head + count) % blasts.size()] = {*pos, desc->radius, w.now,
                                                      sim::has_actor(source) ? s::owner(source) : 0xffu};
            if (count == blasts.size())
                head = (head + 1) % blasts.size();
            else
                ++count;
        }
    }
    original(desc, source, pos, payload);
}
inline bool near_blast(const s::World& w, s::Vec3 actor) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!sync_locked(w)) return false;
    for (std::size_t i = count; i; --i) {
        const auto& blast = blasts[(head + i - 1) % blasts.size()];
        if (w.now - blast.tick > w.duration(1.f)) break;
        const float d2 = sim::distance2(actor, blast.pos);
        const float radius = (std::min)(blast.radius * .75f, 22.f * w.length);
        if (d2 > (.1f * w.length) * (.1f * w.length) && d2 <= radius * radius) return true;
    }
    return false;
}
// Reserve private state before calling the replicating setter (which can re-enter).
inline bool claim_reaction(const s::World& w, Address actor, Address brain) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!sync_locked(w)) return false;
    Reaction* slot = nullptr;
    for (auto& row : reactions) {
        if (row.actor.matches(actor) && row.brain == brain) {
            if (w.now - row.tick < w.duration(10.f)) return false;
            slot = &row;
            break;
        }
        if (!slot && (!row.actor.ptr || w.now - row.tick >= w.duration(10.f))) slot = &row;
    }
    if (!slot) slot = store::try_append_slot(reactions);
    if (slot) *slot = {s::identity(actor), brain, w.now};
    return slot != nullptr;
}
using SetType = void (*)(Address, int, Byte);
inline SetType posture_setter(Address chassis) {
    return reinterpret_cast<SetType>(read<Address>(read<Address>(chassis), 0x170));
}
inline bool trap_context(const s::World& w, Address order, Address actor, Address grenade) {
    if (!authorized_thread() || w.base != image || serial_actor() != actor ||
        read<Address>(order) != image + runtime::table().vt.avoid_grenade || read<Address>(order, 8) != actor + 0xaf0 ||
        read<Address>(order, 0x38) != grenade || !s::live_entity(actor) || !s::live_entity(grenade))
        return false;
    const auto brain = read<Address>(actor, 0xdc8), ch = read<Address>(actor, 0xde0);
    const auto vitality = read<Address>(read<Address>(actor, 0x578), 0x18);
    return (read<Byte>(actor, 0x30) & 6) == 6 && (read<unsigned>(actor, 0xdb4) & 0x100) &&
           !(read<unsigned>(actor, 0x1c0) & (1u << 28)) && s::owner(actor) < 18 &&
           read<Address>(brain) == image + runtime::table().vt.human_brain && read<Address>(brain, 0x38) == actor &&
           (read<unsigned>(brain, 0x398) & 3) <= 1 && vitality && !(read<unsigned>(vitality, 0x174) & 3) && ch &&
           read<Address>(ch, 0x10) == actor && read<Address>(actor, 0xdd8) &&
           !runtime::is_inhibited_or_direct_control(actor, brain);
}
inline bool trapped_hide(const s::World& w, Address order, Address actor, Address grenade, bool reserve) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!sync_locked(w)) return false;
    Trap* slot = nullptr;
    for (auto& row : traps) {
        if (row.order == order) {
            if (!reserve && row.actor.matches(actor) && row.grenade.matches(grenade)) {
                row.tick = w.now;
                return true;
            }
            slot = &row;
            break;
        }
        if (!slot && (!row.order || w.now - row.tick > w.duration(10.f))) slot = &row;
    }
    if (!reserve) return false;
    if (!slot) slot = store::try_append_slot(traps);
    if (slot) *slot = {order, s::identity(actor), s::identity(grenade), w.now};
    return slot != nullptr;
}
} // namespace detail

inline bool is_hitting_dirt(Address actor, Tick now) {
    s::World w{};
    if (!detail::authorized_thread() || detail::serial_actor() != actor || !sim::world(detail::image, w) ||
        w.now != now)
        return false;
    std::lock_guard<std::mutex> lock(detail::mutex);
    if (!detail::sync_locked(w)) return false;
    for (const auto& row : detail::reactions)
        if (row.actor.matches(actor) && row.brain == read<Address>(actor, 0xdc8))
            return now - row.tick < w.duration(4.5f);
    return false;
}
inline void on_human_post_quant(const s::World& w, Address actor) {
    runtime::HookGuard frame(detail::gate);
    if (!detail::authorized_thread() || !actor || detail::serial_actor() != actor || w.base != detail::image ||
        !detail::near_blast(w, s::pos(actor)) || !s::autonomous_if(actor, true, s::donor_policy_allows) ||
        !s::free_slot(actor))
        return;
    const auto brain = read<Address>(actor, 0xdc8), ch = read<Address>(actor, 0xde0);
    if (!s::hold_position(brain) || (read<unsigned>(brain, 0x39c) & ((1u << 6) | (1u << 9))) ||
        read<Address>(ch, 0x10) != actor || read<int>(ch, 0x2c) == 1 || read<int>(ch, 0x2c) == 3 ||
        read<int>(ch, 0x30) == 1 || read<int>(ch, 0x30) == 3 || s::visible_near(actor, 10.f * w.length) ||
        coord::instance().active_for(actor, read<unsigned>(actor, 0xc0), w.now).owner != coord::Module::none)
        return;
    const auto weapon =
        runtime::native<s::Unary>(w.base, runtime::table().fn.weapon_select)(read<Address>(actor, 0xdd8));
    const auto charger = read<Address>(weapon, 0xb0);
    const auto setter = detail::posture_setter(ch);
    if ((charger && (read<Byte>(charger, 0x14) & 0x14)) || !setter || !detail::claim_reaction(w, actor, brain)) return;
    setter(ch, 1, 0);
    ++stat_hit_dirt;
}
template <class Create, class Remove> bool prepare_hooks(Address base, const char* sha, Create create, Remove remove) {
    if (!base || !sha || std::strcmp(sha, runtime::kBuild26734796Sha256) != 0 || detail::original) return false;
    auto target = reinterpret_cast<void*>(base + runtime::table().hook.world_publish_signal);
    void* trampoline = nullptr;
    if (create(target, reinterpret_cast<void*>(detail::publish), &trampoline)) return false;
    if (!trampoline) {
        remove(target);
        return false;
    }
    detail::original = reinterpret_cast<detail::Publish>(trampoline);
    detail::image = base;
    detail::gate.activate();
    return true;
}
inline void deactivate() {
    detail::gate.deactivate();
}
inline unsigned inflight_count() {
    return detail::gate.count();
}
inline void after_hooks_removed() {
    deactivate();
    std::lock_guard<std::mutex> lock(detail::mutex);
    detail::head = detail::count = detail::scene = detail::clock = detail::image = 0;
    detail::reactions.release();
    detail::traps.release();
    detail::original = nullptr;
    detail::serial_actor = s::current_actor;
    stat_hit_dirt = stat_trapped_hides = 0;
}
} // namespace goh::hit_the_dirt
