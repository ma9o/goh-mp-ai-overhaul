#pragma once

#include "game_runtime.hpp"

#include <array>
#include <cstdint>
#include <unordered_map>

namespace goh::coord {

using runtime::Address;
using runtime::Tick;

enum class Module : std::uint8_t {
    none = 0,
    tactical_objectives = 1,
    tactical_infantry = 2,
    auto_resupply = 3,
    tank_auto_repair = 4,
    auto_medic = 5,
    auto_smoke = 6,
    close_grenade = 7,
    grenade_dodge = 8,
    count = 9
};

enum class Priority : std::uint8_t {
    none = 0,
    tactical_objectives = 10,
    tactical_infantry = 20,
    auto_resupply = 30,
    tank_auto_repair = 40,
    auto_medic = 50,
    auto_smoke = 60,
    close_grenade = 70,
    active_revive = 75,
    grenade_dodge = 100
};

// Unified actor-level priority arbiter replacing N*(N-1) pairwise is_busy checks.
// Serial main-thread owned; heap-backed lease map with zero PE .data bloat.
class Coordinator {
public:
    using LiveBusyFn = bool (*)(Address actor);
    using PreemptFn = void (*)(Address actor);

    struct Lease {
        std::uint32_t entity_id{};
        Module owner{Module::none};
        Priority priority{Priority::none};
        Tick expires{};
        bool has_expiry{};
    };

    void register_provider(Module mod, Priority prio, LiveBusyFn busy_fn, PreemptFn preempt_fn = nullptr) noexcept {
        const auto idx = static_cast<std::size_t>(mod);
        if (idx == 0 || idx >= providers_.size()) return;
        providers_[idx] = {prio, busy_fn, preempt_fn};
    }

    void clear_providers() noexcept {
        providers_ = {};
    }

    // Automatically invalidates leases on scene change or backwards clock jump.
    void sync_world(Address scene, Tick now) {
        if ((scene && last_scene_ && scene != last_scene_) || (last_clock_ && now < last_clock_)) {
            leases_.clear();
        }
        if (scene) last_scene_ = scene;
        if (now) last_clock_ = now;
    }

    void reset() {
        leases_.clear();
        last_scene_ = 0;
        last_clock_ = 0;
    }

    void release_all() {
        std::unordered_map<Address, Lease>{}.swap(leases_);
        last_scene_ = 0;
        last_clock_ = 0;
    }

    // Returns the highest active (owner, priority) currently controlling `actor`,
    // considering both explicit leases and registered live-state providers.
    struct ActiveClaim {
        Module owner{Module::none};
        Priority priority{Priority::none};
    };

    ActiveClaim active_for(Address actor, std::uint32_t entity_id = 0, Tick now = 0,
                           Module ignore = Module::none) const noexcept {
        ActiveClaim best{};
        if (!actor) return best;

        auto it = leases_.find(actor);
        if (it != leases_.end()) {
            const Lease& l = it->second;
            const bool id_ok = !entity_id || !l.entity_id || l.entity_id == entity_id;
            const bool alive = !l.has_expiry || !now || now <= l.expires;
            if (id_ok && alive && l.owner != ignore &&
                static_cast<std::uint8_t>(l.priority) > static_cast<std::uint8_t>(best.priority)) {
                best = {l.owner, l.priority};
            }
        }

        for (std::size_t i = 1; i < providers_.size(); ++i) {
            const auto mod = static_cast<Module>(i);
            if (mod == ignore) continue;
            const auto& p = providers_[i];
            if (!p.busy_fn) continue;
            if (static_cast<std::uint8_t>(p.priority) <= static_cast<std::uint8_t>(best.priority)) continue;
            if (p.busy_fn(actor)) best = {mod, p.priority};
        }
        return best;
    }

    // True if `requester` with `priority` is permitted to act on `actor`:
    // blocked whenever any *other* module holds a lease/live job at >= priority.
    bool can_act(Address actor, Module requester, Priority priority, std::uint32_t entity_id = 0,
                 Tick now = 0) const noexcept {
        if (!actor) return false;
        const auto cur = active_for(actor, entity_id, now, requester);
        return static_cast<std::uint8_t>(cur.priority) < static_cast<std::uint8_t>(priority);
    }

    // Claims `actor` for `owner` at `priority`, preempting any lower-priority
    // holder (invoking its registered preempt callback if present).
    bool claim(Address actor, std::uint32_t entity_id, Module owner, Priority priority, Tick now = 0, Tick ttl = 0) {
        if (!actor || owner == Module::none || priority == Priority::none) return false;
        const auto cur = active_for(actor, entity_id, now, owner);
        if (static_cast<std::uint8_t>(cur.priority) >= static_cast<std::uint8_t>(priority)) return false;

        if (cur.owner != Module::none) {
            const auto idx = static_cast<std::size_t>(cur.owner);
            if (idx < providers_.size() && providers_[idx].preempt_fn) providers_[idx].preempt_fn(actor);
        }
        Lease lease{};
        lease.entity_id = entity_id;
        lease.owner = owner;
        lease.priority = priority;
        lease.has_expiry = (ttl != 0 && now != 0);
        lease.expires = lease.has_expiry ? (now + ttl) : 0;
        leases_[actor] = lease;
        return true;
    }

    void release(Address actor, Module owner) {
        auto it = leases_.find(actor);
        if (it != leases_.end() && it->second.owner == owner) leases_.erase(it);
    }

    std::size_t lease_count() const noexcept {
        return leases_.size();
    }

private:
    struct Provider {
        Priority priority{Priority::none};
        LiveBusyFn busy_fn{};
        PreemptFn preempt_fn{};
    };

    std::array<Provider, static_cast<std::size_t>(Module::count)> providers_{};
    std::unordered_map<Address, Lease> leases_;
    Address last_scene_{};
    Tick last_clock_{};
};

inline Coordinator& instance() noexcept {
    static Coordinator coord{};
    return coord;
}

} // namespace goh::coord
