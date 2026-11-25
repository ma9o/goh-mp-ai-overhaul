#pragma once

// Steam 26734796 x64. See research/notes/tank-crusher-26734796.md.
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#ifdef _WIN32
#include <windows.h>
#endif
#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace goh::tank_crusher {

using Address = std::uintptr_t;
using Byte = std::uint8_t;
inline constexpr char executable_sha256[] = "81a13de787c1869b766004cdcadb9ff99012f0b1b58b89db9a7741b10b9f059a";
inline constexpr bool runtime_allowed = false; // Static qualification status.
inline void (*log_sink)(const char*, ...) = nullptr;
inline std::atomic<unsigned> admitted_contexts{}, expanded_contexts{}, discounted_obstacles{}, biased_estimates{};

namespace detail {

static_assert(sizeof(Address) == 8);
template<class T> inline T read(Address a, std::size_t offset = 0) noexcept {
    T value;
    std::memcpy(&value, reinterpret_cast<const void*>(a + offset), sizeof value);
    return value;
}

// MSVC x64 basic_string<char>, borrowed while the native owner is live.
inline std::string_view name_at(Address s) noexcept {
    const auto size = read<std::size_t>(s, 0x10), capacity = read<std::size_t>(s, 0x18);
    if (!size || size > 32 || size > capacity) return {};
    const auto data = capacity > 15 ? read<Address>(s) : s;
    return data ? std::string_view(reinterpret_cast<const char*>(data), size) : std::string_view{};
}

enum class Obstacle : Byte { other, stuff, construction, small_building, tree_post, building };

inline Obstacle classify(std::string_view name, unsigned flags) noexcept {
    if (flags & 3) return Obstacle::other;
    if (name == "stuff") return Obstacle::stuff;
    if (name == "construction" || name == "construction_vaultable") return Obstacle::construction;
    if (name == "small_building" || name == "small_building_vaultable") return Obstacle::small_building;
    if (name == "tree" || name == "post") return Obstacle::tree_post;
    if (name == "building" || name == "building_vaultable") return Obstacle::building;
    return Obstacle::other;
}
inline bool passable(float cost) noexcept { return cost > 0.f && cost < 100000.f; }
inline float obstacle_cost(float original, Obstacle type, bool battle) noexcept {
    if (!passable(original)) return original;
    float cap;
    switch (type) {
    case Obstacle::stuff:          cap = battle ? 0.5f : 2.f; break;
    case Obstacle::construction:   cap = battle ? 1.f : 4.f; break;
    case Obstacle::small_building: cap = battle ? 2.f : 8.f; break;
    case Obstacle::tree_post:      cap = battle ? 3.f : 12.f; break;
    case Obstacle::building:
        if (!battle) return original;
        cap = 25.f;
        break;
    default: return original;
    }
    return original < cap ? original : cap;
}

struct XY { float x, y; };
struct PathStats {
    Address compiled_path;
    float forward, reverse; // game length units
    std::int32_t switches;
    float turn_rad;
};
static_assert(sizeof(PathStats) == 0x18 && offsetof(PathStats, turn_rad) == 0x14);

inline bool frontal(XY hull, XY self, XY seen, float metres_per_unit) noexcept {
    const double dx = double(seen.x) - self.x, dy = double(seen.y) - self.y;
    const double distance = std::hypot(dx, dy), norm = std::hypot(double(hull.x), double(hull.y));
    if (!(metres_per_unit > 0 && distance > 0 && distance <= 180.f / metres_per_unit && norm > 0))
        return false;
    return (hull.x * dx + hull.y * dy) > .35 * norm * distance;
}
inline float reverse_estimate(float original, const PathStats& path, float metres_per_unit) noexcept {
    if (!(original > 0 && std::isfinite(original) && metres_per_unit > 0 &&
          path.forward >= 0 && path.reverse > path.forward && path.switches >= 0 &&
          path.turn_rad >= 0 && path.turn_rad <= 2.3561945f)) return original;
    if (!((path.forward + path.reverse) * metres_per_unit <= 45.f)) return original;
    const float biased = original - .08f * (path.reverse * metres_per_unit);
    return biased > 0 ? biased : original;
}

using Role = Byte (*)();
using ContextCtor = Address (*)(Address, Address);
using ObjectPolicy = float (*)(Address, Address, Address);
using Estimate = float (*)(Address, const PathStats*);
inline ContextCtor orig_context{};
inline ObjectPolicy orig_policy{};
inline Estimate orig_estimate{};
inline Address base{};
inline bool rollback_pending{};
inline std::atomic<bool> active{};
inline std::atomic<bool> host{};
inline std::atomic<int> inflight{};
inline std::atomic<unsigned long> serial_tid{};
inline thread_local Address updating_actor{};
inline Address scene{}, obstacle_begin{}, obstacle_end{}; // serial writer only
inline bool can_expand{};
// The worker sees scalar classifications and actor/area IDs copied by the engine.
// No actor, sensor, registry string, or module-owned dynamic storage crosses threads.
inline std::array<std::atomic<Obstacle>, 256> obstacles{};
inline std::array<std::atomic<std::uint64_t>, 4096> actors{};
static_assert(std::atomic<Obstacle>::is_always_lock_free && std::atomic<bool>::is_always_lock_free &&
              std::atomic<int>::is_always_lock_free && std::atomic<std::uint64_t>::is_always_lock_free);

struct InflightGuard {
    InflightGuard() noexcept { inflight.fetch_add(1, std::memory_order_acq_rel); }
    ~InflightGuard() noexcept { inflight.fetch_sub(1, std::memory_order_acq_rel); }
    InflightGuard(const InflightGuard&) = delete;
    InflightGuard& operator=(const InflightGuard&) = delete;
};

inline std::uint64_t actor_key(unsigned actor, unsigned area) noexcept {
    // The engine reserves UINT32_MAX as the absent entity ID. Zero is our empty slot.
    return actor == UINT32_MAX ? 0 : ((std::uint64_t(actor) + 1) << 32) | area;
}
inline auto& actor_slot(std::uint64_t key) noexcept {
    return actors[(key >> 32) % actors.size()];
}
inline void revoke(std::uint64_t key) noexcept {
    if (key) actor_slot(key).compare_exchange_strong(key, 0, std::memory_order_relaxed);
}
inline bool actor_admitted(std::uint64_t key) noexcept {
    return key && actor_slot(key).load(std::memory_order_acquire) == key;
}
inline std::uint64_t previous_admission(unsigned id) noexcept {
    const auto key = actor_key(id, 0);
    const auto previous = actor_slot(key).load(std::memory_order_acquire);
    return (previous >> 32) == (key >> 32) ? previous : 0;
}
inline void reset_admission() noexcept {
    host.store(false, std::memory_order_release);
    for (auto& slot : actors) slot.store(0, std::memory_order_relaxed);
    obstacle_begin = obstacle_end = scene = 0;
    can_expand = false;
}
inline bool refresh_authority() noexcept {
    // Only called from serial actor dispatch or its identified simulation thread.
    const bool remote = reinterpret_cast<Role>(base + 0xC2B7A0)() != 0;
    const Address current = remote ? 0 : read<Address>(base, 0x15C9668);
    if (scene != current) {
        reset_admission();
        scene = current;
    }
    host.store(current != 0, std::memory_order_release);
    return current != 0;
}
inline bool refresh_obstacles() noexcept {
    const Address first = read<Address>(base, 0x15B3110), last = read<Address>(base, 0x15B3118);
    if (first && first == obstacle_begin && last == obstacle_end) return true;
    reset_admission();
    scene = read<Address>(base, 0x15C9668);
    if (!scene || !first || last <= first || (last-first) % 0x40 || (last-first)/0x40 > 255) return false;
    can_expand = true;
    for (unsigned i = 0; i < obstacles.size(); ++i) {
        Obstacle kind = Obstacle::other;
        if (i < (last-first)/0x40) {
            const Address row = first + 0x40*i;
            const unsigned flags = read<unsigned>(row, 0x20);
            can_expand &= !(flags & 4); // Widening bit 4 must never bypass the stock hard-cost gate.
            kind = classify(name_at(row), flags);
        }
        obstacles[i].store(kind, std::memory_order_relaxed);
    }
    obstacle_begin = first;
    obstacle_end = last;
    host.store(true, std::memory_order_release);
    return true;
}

inline Address vehicle_brain(Address actor, Address chassis) noexcept {
    if (!actor || !chassis || !(read<Byte>(actor, 0x30) & 2) ||
        (read<unsigned>(actor, 0x38) & 0xA20) || !(read<unsigned>(actor, 0xDB4) & 0x100) ||
        (read<unsigned>(actor, 0x1C0) & (1u << 28)) ||
        read<Address>(actor, 0xDE0) != chassis || read<Address>(chassis, 0x10) != actor ||
        read<unsigned>(chassis, 0x18)) return 0;
    const Address brain = read<Address>(actor, 0xDC8), weaponry = read<Address>(actor, 0xDD8);
    if (!brain || read<Address>(brain) != base + 0x101FD68 || read<Address>(brain, 0x38) != actor ||
        !weaponry || (read<Byte>(weaponry, 0x70) & 2)) return 0;
    const unsigned modes = read<unsigned>(brain, 0x398), flags = read<unsigned>(brain, 0x39C);
    // Path cost & gear scoring never creates or hijacks orders: allow active user moves (bit 28)
    // and Hold Position (move_mode == 1) while always bailing on Direct Control / inhibition.
    if ((flags & 0x3Fu) || (modes & 3) > 1) return 0;
    return brain;
}

inline bool fresh_contact(Address subject, std::uint32_t now, float ticks_per_sec) noexcept {
    const auto flags10 = read<unsigned>(subject, 0x10);
    if (flags10 & (1u << 29)) return true; // currently visible
    if (!(ticks_per_sec >= 1.f && ticks_per_sec <= 1000000.f) || !now) return true;
    const auto obs_time = read<std::uint32_t>(subject, 0x64);
    if (!obs_time) return false;
    const auto max_age = static_cast<std::uint32_t>(ticks_per_sec * 20.f);
    return (now - obs_time) <= max_age;
}

template<class Accept>
inline bool known_enemy(Address actor, std::uint32_t now, float ticks_per_sec, Accept accept) noexcept {
    const Address sensor = read<Address>(actor, 0xDD0);
    if (!sensor || read<Address>(sensor, 0x260) != actor) return false;
    const Address first = read<Address>(sensor, 0x280), last = read<Address>(sensor, 0x288);
    if (!first || last < first || (last-first) % 8 || (last-first)/8 > 4096) return false;
    for (Address p = first; p != last; p += 8) {
        const Address s = read<Address>(p);
        // Enemy bit 1 set, apparent/actual ally bits 2..3 and unusable bit 4 clear (recognition bit 0 optional).
        if (!s || read<Byte>(s, 0x6C) || (read<unsigned>(s, 0x14) & 0x1Eu) != 2u) continue;
        const auto flags = read<unsigned>(s, 0x10);
        if (!(flags & (1u << 28)) || (flags & (1u << 16)) || !read<Address>(s, 0x18)) continue;
        if (!fresh_contact(s, now, ticks_per_sec)) continue;
        const XY seen = read<XY>(s, 0x28);
        if (std::isfinite(seen.x) && std::isfinite(seen.y) && accept(seen)) return true;
    }
    return false;
}

inline bool battle_profile(Address profile) noexcept {
    return profile && (read<Byte>(profile, 0xF0) & 0x10);
}
inline bool light_tank_profile(Address profile) noexcept {
    // Stock PATHER.SET assigns Priority == 4 exclusively to the light_tank template.
    return profile && read<std::int32_t>(profile, 0xF4) == 4;
}
inline bool tank_worker_profile(Address profile) noexcept {
    if (!profile) return false;
    if (read<Byte>(profile, 0xF0) & 0x10) return true; // heavy_tank / medium_tank
    const auto prio = read<std::int32_t>(profile, 0xF4);
    // Priority 4 = light_tank; Priority 3 = amphibious tracked tanks (only admitted via actor_admitted).
    return prio == 4 || prio == 3;
}
inline bool is_tank_class(Address actor) noexcept {
    const auto type = name_at(actor + 0xDE8 + 0x40);
    return type == "tank_light" || type == "tank_medium" || type == "tank_heavy" || type == "tank";
}
inline bool is_tank_actor(Address actor, Address chassis, Address profile) noexcept {
    if (battle_profile(profile) || light_tank_profile(profile)) return true;
    return chassis && read<Address>(chassis) == base + 0x1105E80 && is_tank_class(actor);
}

inline float Hook_ObjectPolicy(Address ctx, Address dir, Address area) noexcept {
    InflightGuard guard;
    const float native = orig_policy(ctx, dir, area);
    if (!active.load(std::memory_order_acquire) || !passable(native) ||
        !host.load(std::memory_order_acquire) || !read<Byte>(ctx) ||
        read<Byte>(area, 0x1F) || !tank_worker_profile(read<Address>(ctx, 0xD0))) return native;
    const auto key = actor_key(read<unsigned>(ctx, 0x7C), read<unsigned>(ctx, 0x78));
    if (!actor_admitted(key)) return native;
    const auto type = obstacles[read<Byte>(area, 0x3F)].load(std::memory_order_relaxed);
    const float adjusted = obstacle_cost(native, type, (read<Byte>(ctx, 0xE8) & 0x10) != 0);
    if (adjusted < native) discounted_obstacles.fetch_add(1, std::memory_order_relaxed);
    return adjusted;
}

#ifdef _WIN32
inline bool serial_host() noexcept {
    return serial_tid.load(std::memory_order_acquire) == GetCurrentThreadId() &&
           !reinterpret_cast<Role>(base + 0xA7A80)() && refresh_authority();
}
#ifdef _MSC_VER
#define GOH_TANK_CALLER reinterpret_cast<Address>(_ReturnAddress())
#else
#define GOH_TANK_CALLER reinterpret_cast<Address>(__builtin_return_address(0))
#endif

inline Address Hook_ContextCtor(Address ctx, Address actor) noexcept {
    InflightGuard guard;
    const Address result = orig_context(ctx, actor);
    if (!active.load(std::memory_order_acquire) || !result || !actor) return result;
    const Address caller = GOH_TANK_CALLER - base;
    const Address profile = read<Address>(result, 0xD0);
    if (!tank_worker_profile(profile)) return result;
    const unsigned id = read<unsigned>(result, 0x7C);
    const auto key = actor_key(id, read<unsigned>(result, 0x78));
    if (!serial_host()) {
        // A new client or off-thread request can reuse IDs from an older scene.
        // Withdraw any matching admission using only the constructed context.
        revoke(previous_admission(id));
        return result;
    }
    const Address chassis = read<Address>(actor, 0xDE0);
    const Address brain = vehicle_brain(actor, chassis);
    if (!brain || !is_tank_actor(actor, chassis, profile)) {
        revoke(previous_admission(id));
        return result;
    }
    // The physics can_crush predicate (0x76AC6C) constructs its own context. Leave it entirely native.
    if (!key || caller == 0x76AC71 ||
        (updating_actor != actor && caller != 0x90E511 && caller != 0x90ED5D &&
         caller != 0x86F6CB && caller != 0x8557DF && caller != 0x90B286 && caller != 0x90BD51))
        return result;
    if (!refresh_obstacles()) return result;
    actor_slot(key).store(key, std::memory_order_release);
    admitted_contexts.fetch_add(1, std::memory_order_relaxed);
    auto& flags = *reinterpret_cast<Byte*>(result + 0xE8);
    const auto now = read<std::uint32_t>(base, 0x1584568);
    const float ticks_per_sec = read<float>(base, 0x149E758);
    if (can_expand && !(flags & 0x10) &&
        ((read<unsigned>(brain, 0x39C) & 0x1E000u) ||
         read<unsigned>(brain, 0x3F8) > now ||
         known_enemy(actor, now, ticks_per_sec, [](XY) { return true; }))) {
        flags |= 0x10;
        expanded_contexts.fetch_add(1, std::memory_order_relaxed);
    }
    return result;
}

inline float Hook_EstimatePathTime(Address chassis, const PathStats* stats) noexcept {
    InflightGuard guard;
    const float original = orig_estimate(chassis, stats);
    if (!active.load(std::memory_order_acquire) || !chassis || !stats) return original;
    const Address caller = GOH_TANK_CALLER - base;
    if ((caller != 0x86F78B && caller != 0x86F7B9 && caller != 0x855706) || !serial_host() ||
        (read<Byte>(chassis, 0x3C0) & 0xC0)) return original;
    const float scale = read<float>(base, 0x15754A0);
    const auto biased = reverse_estimate(original, read<PathStats>(reinterpret_cast<Address>(stats)), scale);
    if (!(biased < original)) return original;
    const Address actor = read<Address>(chassis, 0x10);
    if (!vehicle_brain(actor, chassis)) return original;
    const Byte profile_id = read<Byte>(actor, 0xDB4);
    const Address first = read<Address>(base, 0x15B2FF0), last = read<Address>(base, 0x15B2FF8);
    if (!first || last <= first || profile_id >= (last-first)/0xF8) return original;
    if (!is_tank_actor(actor, chassis, first + profile_id * 0xF8)) return original;
    const auto hull = read<XY>(chassis, 0x5C), self = read<XY>(actor, 0x74);
    const auto now = read<std::uint32_t>(base, 0x1584568);
    const float ticks_per_sec = read<float>(base, 0x149E758);
    if (!known_enemy(actor, now, ticks_per_sec,
                     [&](XY seen) { return frontal(hull, self, seen, scale); })) return original;
    biased_estimates.fetch_add(1, std::memory_order_relaxed);
    return biased;
}
#undef GOH_TANK_CALLER
#endif
} // namespace detail

#ifdef _WIN32
// Slot 23 is a positive serial-thread witness. The async pather is not necessarily
// a quant worker, so a negative worker test alone cannot establish authority.
class ActorScope {
    Address previous;
public:
    explicit ActorScope(Address actor) noexcept : previous(detail::updating_actor) {
        using namespace detail;
        updating_actor = 0;
        if (!base || !active.load(std::memory_order_acquire) ||
            reinterpret_cast<Role>(base + 0xA7A80)()) return;
        serial_tid.store(GetCurrentThreadId(), std::memory_order_release);
        if (!refresh_authority()) return;
        updating_actor = actor;
        const auto previous_key = previous_admission(read<unsigned>(actor, 0xC0));
        if (previous_key && !vehicle_brain(actor, read<Address>(actor, 0xDE0))) revoke(previous_key);
    }
    ~ActorScope() { detail::updating_actor = previous; }
    ActorScope(const ActorScope&) = delete;
    ActorScope& operator=(const ActorScope&) = delete;
};

template<class Create, class Remove>
inline bool prepare_hooks(Address image, const char* measured_sha256, Create create, Remove remove) {
    using namespace detail;
    if (base || rollback_pending || !image || !measured_sha256 ||
        std::strcmp(measured_sha256, executable_sha256)) return false;
    std::array<void*, 3> created{};
    std::size_t count{};
    auto add = [&](Address rva, auto hook, auto& original) {
        void* target = reinterpret_cast<void*>(image + rva);
        void* trampoline = nullptr;
        if (create(target, reinterpret_cast<void*>(hook), &trampoline) != 0) return false;
        original = reinterpret_cast<decltype(hook)>(trampoline);
        created[count++] = target;
        return true;
    };
    if (!add(0x8DF010, Hook_ContextCtor, orig_context) || !add(0x8C3650, Hook_ObjectPolicy, orig_policy) ||
        !add(0x86F430, Hook_EstimatePathTime, orig_estimate)) {
        while (count) {
            const auto target = created[--count];
            if (remove(target) != 0) {
                rollback_pending = true;
                if (log_sink) log_sink("Tank pathing rollback failed for RVA 0x%llx",
                                       (unsigned long long)(reinterpret_cast<Address>(target) - image));
            }
        }
        if (!rollback_pending) {
            orig_context = nullptr;
            orig_policy = nullptr;
            orig_estimate = nullptr;
        }
        return false;
    }
    base = image;
    admitted_contexts = expanded_contexts = discounted_obstacles = biased_estimates = 0;
    active.store(true, std::memory_order_release);
    if (log_sink) log_sink("Tank BattleCrusher and front-armor reverse pathing staged (3 hooks)");
    return true;
}
#endif

inline void deactivate() noexcept {
    detail::active.store(false, std::memory_order_release);
    detail::host.store(false, std::memory_order_release);
}
inline int inflight_count() noexcept {
    return detail::inflight.load(std::memory_order_acquire);
}

// An async callback can still be inside its native trampoline after hook disable.
// The scene drops before pather teardown. The async grid reference drops only
// after the worker acknowledges the flush (0x8C849A..0x8C853D).
inline bool scene_torn_down() noexcept {
    return !detail::base || (!detail::read<Address>(detail::base, 0x15C9668) &&
                             !detail::read<Address>(detail::base, 0x15B2F80));
}

// Call after detours are disabled and their native callers have quiesced.
inline void after_hooks_removed() noexcept {
    using namespace detail;
    active.store(false, std::memory_order_release);
    reset_admission();
    serial_tid = 0;
    base = 0;
    rollback_pending = false;
    orig_context = nullptr;
    orig_policy = nullptr;
    orig_estimate = nullptr;
}
} // namespace goh::tank_crusher
