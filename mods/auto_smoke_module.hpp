#pragma once

// Steam 26734796, Windows x64. Install only via the verified-build integration
// described in research/notes/auto-smoke-26734796.md.
#include <windows.h>
#include "actor_coordinator.hpp"
#include "config_and_log.hpp"
#include "dynamic_store.hpp"
#include "game_classifiers.hpp"
#include "game_runtime.hpp"
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace goh::auto_smoke {

using Address = runtime::Address;
#if defined(_MSC_VER)
#define GOH_AUTO_SMOKE_RETURN_ADDRESS() reinterpret_cast<Address>(_ReturnAddress())
#else
#define GOH_AUTO_SMOKE_RETURN_ADDRESS() reinterpret_cast<Address>(__builtin_return_address(0))
#endif

static_assert(sizeof(Address) == 8, "Auto-Smoke requires a 64-bit target");
inline constexpr const char* executable_sha256 = runtime::kBuild26734796Sha256;
inline runtime::HookGate hook_gate{};

using runtime::read;

struct Vec3 {
    float x, y, z;
};
static_assert(sizeof(Vec3) == 12);
inline bool finite(Vec3 v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
inline float length2_xy(Vec3 a, Vec3 b) noexcept {
    const float dx = a.x - b.x, dy = a.y - b.y;
    return dx * dx + dy * dy;
}
inline float distance2(Vec3 a, Vec3 b) noexcept {
    const float dz = a.z - b.z;
    return length2_xy(a, b) + dz * dz;
}
inline void (*log_sink)(const char* fmt, ...) = nullptr;
inline std::uint32_t stat_veh_smoke = 0;
inline std::uint32_t stat_inf_smoke = 0;
inline std::uint32_t stat_med_smoke = 0;
inline std::uint32_t stat_inf_released = 0;

using Role = std::uint8_t(__fastcall*)();
// Active host simulation authority: true in SP/Skirmish/Conquest AND MP host; false on MP clients & workers.
inline bool emitting_host(Address base) noexcept {
    return runtime::is_host_main_thread(base);
}
inline bool is_mp_host_emit(Address base) noexcept {
    return emitting_host(base) && runtime::is_host_emit(base);
}

inline bool has_actor(Address a) noexcept {
    return a && (read<std::uint8_t>(a, 0x30) & 2) &&
           !(read<std::uint32_t>(a, 0x38) & 0xA20u); // retired, inactive, pending deletion
}
inline bool eligible(Address base, Address brain, Address actor) noexcept {
    if (!has_actor(actor) || !brain || read<Address>(actor, 0xDC8) != brain || read<Address>(brain, 0x38) != actor ||
        !(read<std::uint32_t>(actor, 0xDB4) & 0x100) || (read<std::uint32_t>(actor, 0x1C0) & (1u << 28)))
        return false;
    const auto vt = read<Address>(brain);
    const bool is_human = (vt == base + 0x110FF88);
    const bool is_vehicle = (vt == base + 0x101FD68);
    if (!is_human && !is_vehicle) return false;
    const auto policy = read<std::uint32_t>(brain, 0x398);
    const auto flags = read<std::uint32_t>(brain, 0x39C);
    const auto fire = (policy >> 10) & 3u;
    const auto move = (policy >> 12) & 3u;
    const auto ctrl = policy & 3u;
    // Allow both AI (0) and player-owned (1) units! Always bail on Direct Control/inhibit (flags & 0x3F) or Hold Fire (fire == 1).
    if (ctrl > 1u || (flags & 0x3Fu) || (fire != 0 && fire != 2)) return false;
    // Throwing smoke does not move the soldier off Hold Position (move == 1);
    // still bail on active player movement orders (bit 28) or invalid move modes (> 2).
    // For vehicles, defensive smoke launchers do not move the vehicle or cancel orders.
    if (is_human && ((flags & (1u << 28)) || move > 2u)) return false;
    const Address chassis = read<Address>(actor, 0xDE0), weaponry = read<Address>(actor, 0xDD8);
    return chassis && weaponry && !read<std::uint32_t>(chassis, 0x18) && !(read<std::uint8_t>(weaponry, 0x70) & 2);
}

struct World {
    Address base, scene;
    std::uint32_t now;
    float length, ticks;
    std::uint32_t duration(float seconds) const noexcept {
        return static_cast<std::uint32_t>(ticks * seconds);
    }
};
inline bool world(Address base, World& w) noexcept {
    w = {base, read<Address>(base, 0x15C9668), read<std::uint32_t>(base, 0x1584568), read<float>(base, 0x149E750),
         read<float>(base, 0x149E758)};
    return w.scene && w.length > 0 && w.length < 10000 && w.ticks >= 1 && w.ticks <= 1000000;
}
inline bool recent(const World& w, std::uint32_t stamp, float seconds) noexcept {
    return stamp && w.now - stamp <= w.duration(seconds);
}
inline bool allies(Address base, unsigned a, unsigned b) noexcept {
    if (a >= 18 || b >= 18) return false;
    if (a == b) return true;
    const auto stride = read<int>(base, 0x15C9710);
    const auto table = read<Address>(base, 0x15C9700);
    return table && stride > static_cast<int>(a) && stride > static_cast<int>(b) && stride <= 256 &&
           read<int>(table, (b * static_cast<unsigned>(stride) + a) * sizeof(int)) == 2;
}

// One system TLS index. A frame lives on the actor callback's stack, including
// during nested native notifications. No per-thread C++ TLS runtime or heap.
struct SmokeReleaseScope;
struct SmokeFiringScope;
struct QuantFrame {
    Address actor{};
    SmokeReleaseScope* release{};
    SmokeFiringScope* firing{};
};
inline DWORD frame_index = TLS_OUT_OF_INDEXES;
inline QuantFrame* quant_frame() noexcept {
    if (frame_index == TLS_OUT_OF_INDEXES) return nullptr;
    const DWORD saved = GetLastError();
    auto* frame = static_cast<QuantFrame*>(TlsGetValue(frame_index));
    SetLastError(saved);
    return frame;
}
class ActorQuantScope {
    QuantFrame current_;
    QuantFrame* previous_;
    bool installed_;

public:
    explicit ActorQuantScope(Address actor) noexcept
        : current_{actor}, previous_(quant_frame()),
          installed_(frame_index != TLS_OUT_OF_INDEXES && TlsSetValue(frame_index, &current_)) {}
    ~ActorQuantScope() {
        if (installed_) TlsSetValue(frame_index, previous_);
    }
    ActorQuantScope(const ActorQuantScope&) = delete;
    ActorQuantScope& operator=(const ActorQuantScope&) = delete;
};

// The key in eStuff+0x868 is distinct from the inherited selector at +0xEC8.
// Native strings use the MSVC layout. Borrow their bytes during this serial call.
inline bool SmokeTagEquals(Address row, const char* text) noexcept {
    if (!row || !text) return false;
    const auto length = std::strlen(text);
    const auto capacity = read<std::size_t>(row, 0x58);
    if (read<std::size_t>(row, 0x50) != length || capacity < length) return false;
    const Address data = capacity > 15 ? read<Address>(row, 0x40) : row + 0x40;
    return data && std::memcmp(reinterpret_cast<const void*>(data), text, length) == 0;
}

inline bool IsGrenadeDefinition(Address def, const char* name) noexcept {
    if (!def || !name) return false;
    const Address first = read<Address>(def, 0xB80);
    const Address end = read<Address>(def, 0xB88);
    if (!first || end < first || end - first != 2 * 0x60) return false;
    return (SmokeTagEquals(first, "grenade") && SmokeTagEquals(first + 0x60, name)) ||
           (SmokeTagEquals(first + 0x60, "grenade") && SmokeTagEquals(first, name));
}

inline bool IsPureSmokeDefinition(Address def) noexcept {
    // The stock WP items also have 'smoke' and have no item blastwave.
    // The exact key binds the item to audited stock smoke or configured non-WP mod keys.
    if (!def || read<Address>(def, 0x480) != read<Address>(def, 0x488)) return false;
    const Address first = read<Address>(def, 0xB80);
    const Address end = read<Address>(def, 0xB88);
    if (!first || end < first || end - first != 2 * 0x60) return false;
    const auto k0 = classify::gem_string(first + 0x40, 128);
    const auto k1 = classify::gem_string(first + 0x60 + 0x40, 128);
    if (k0 == "grenade") return classify::is_allowed_smoke_grenade_key(k1);
    if (k1 == "grenade") return classify::is_allowed_smoke_grenade_key(k0);
    return false;
}

// Returns an immediate borrow. The native box search uses these same slots.
inline Address FindPureSmokeItem(Address actor, const char* name = nullptr) noexcept {
    if (!actor) return 0;
    const Address agg = read<Address>(actor, 0x578);
    const Address manager = agg ? read<Address>(agg, 0x28) : 0;
    const Address box = manager ? read<Address>(manager, 0xA0) : 0;
    if (!box || read<Address>(box, 0x50) != actor) return 0;
    const Address begin = read<Address>(box, 0x10);
    const Address end = read<Address>(box, 0x18);
    if (!begin || end < begin || (end - begin) % sizeof(Address)) return 0;
    for (Address slot = begin; slot < end; slot += sizeof(Address)) {
        const Address item = read<Address>(slot);
        if (!item || read<Address>(item, 0x30) != box || read<std::int32_t>(item, 0x48) <= 0 ||
            read<Address>(item, 0x50))
            continue;
        const Address def = read<Address>(item, 0x40);
        if (IsPureSmokeDefinition(def) && (!name || IsGrenadeDefinition(def, name))) return item;
    }
    return 0;
}

// WeaponShot first compares the right-hand item and otherwise searches its box.
// A mounted hand-only item need not have a box back-pointer.
inline Address FindCarriedPureSmokeItem(Address base, Address actor) noexcept {
    if (!base || !actor) return 0;
    const Address aggregate = read<Address>(actor, 0x578);
    const Address manager = aggregate ? read<Address>(aggregate, 0x28) : 0;
    if (!manager) return 0;
    const Address first = read<Address>(manager, 0x88);
    const Address end = read<Address>(manager, 0x90);
    if (first && end >= first && (end - first) % sizeof(Address) == 0) {
        for (Address slot = first; slot < end; slot += sizeof(Address)) {
            const Address hand = read<Address>(slot);
            if (!hand || read<Address>(hand) != base + 0x10FF210 || read<Address>(hand, 0x3D0) != actor) continue;
            const Address item = read<Address>(hand, 0x3D8);
            if (item && read<Address>(item, 0x38) == hand && read<std::int32_t>(item, 0x48) > 0 &&
                !read<Address>(item, 0x50) && IsPureSmokeDefinition(read<Address>(item, 0x40)))
                return item;
        }
    }
    return FindPureSmokeItem(actor);
}

namespace vehicle {

// The master registers service ID 0x1D and retains it at +0x278. Resolve
// each bot from the service's live vector for this serial callback.
inline Address active_bound_bot(Address base, Address master, std::uint8_t owner) noexcept {
    if (!master || owner >= 18 || read<Address>(base, 0x15D2BE0) != master ||
        read<Address>(master) != base + 0x1093938 || read<std::uint8_t>(master, 0x11))
        return 0;
    const Address service = read<Address>(base, 0x15D2C80);
    if (!service || read<Address>(master, 0x278) != service || read<Address>(service) != base + 0x10D86E8 ||
        read<std::uint8_t>(service, 0x11) || read<Address>(service, 0x60) != master)
        return 0;
    const Address first = read<Address>(service, 0x48), end = read<Address>(service, 0x50);
    const Address capacity = read<Address>(service, 0x58);
    if (!first || end < first || end > capacity || (end - first) % sizeof(Address)) return 0;
    Address result{};
    for (Address p = first; p != end; p += sizeof(Address)) {
        const Address bot = read<Address>(p);
        if (!bot || read<Address>(bot) != base + 0x10D87C0 || read<Address>(bot, 0x48) != service ||
            !read<std::uint8_t>(bot, 0x58))
            continue;
        const Address player = read<Address>(bot, 0x50);
        if (!player || read<std::uint8_t>(player, 4) != owner) continue;
        const Address router = read<Address>(bot, 0x68);
        if (result || !router || read<Address>(router) != base + 0x10D86D0 ||
            read<std::uint8_t>(router, 0x100) != owner)
            return 0;
        result = bot;
    }
    return result;
}

struct ControlRoute {
    Address context{}, router{}, control_vtable{}, callback_vtable{}, native_send{};
};

inline ControlRoute vehicle_control_route(Address base, Address master, std::uint8_t owner) noexcept {
    if (!master || owner >= 18 || read<Address>(base, 0x15D2BE0) != master ||
        read<Address>(master) != base + 0x1093938 || read<std::uint8_t>(master, 0x11) ||
        read<std::uint8_t>(master, 0x2C9))
        return {};
    const Address server = read<Address>(master, 0x18);
    if (!server || (read<Address>(server) != base + 0x1161690 && read<Address>(server) != base + 0x10D96C8) ||
        !read<Address>(server, 0x98) || read<Address>(read<Address>(server, 0x98), 0x238) ||
        !read<Address>(server, 0xB0) || read<std::uint8_t>(server, 0x11) || read<Address>(server, 0xB8) != master)
        return {};
    if (owner == read<std::uint8_t>(base, 0x14AEF38)) {
        if (read<Address>(base, 0x14B3520) != base + 0x1146FF8) return {};
        return {master, base + 0x14B3520, base + 0x10461B0, base + 0x1161620, base + 0xC94280};
    }
    const Address bot = active_bound_bot(base, master, owner);
    if (!bot) return {};
    return {bot, read<Address>(bot, 0x68), base + 0x1071548, base + 0x10D8DB0, base + 0x534150};
}

// Call the same small-object callback as the stock router drain. Its capture
// is the session for a local player and the registered client for a bot.
inline bool send_authorized_control(Address base, Address master, std::uint8_t owner, Address control,
                                    Address envelope) noexcept {
    const auto route = vehicle_control_route(base, master, owner);
    if (!route.context || !control || !envelope || read<std::uint8_t>(envelope) != owner ||
        read<std::uint8_t>(control, 0x43) != owner || read<Address>(control) != route.control_vtable ||
        read<Address>(control, 0x10) != route.router)
        return false;
    const Address first = read<Address>(control, 0x20), end = read<Address>(control, 0x28);
    if (!first || end < first || end - first != sizeof(Address)) return false;
    const Address actor = read<Address>(first);
    if (!actor || read<std::uint8_t>(actor, 0xDA8) != owner) return false;
    struct Capture {
        Address vtable, context;
    };
    const Capture capture{route.callback_vtable, route.context};
    using Send = void(__fastcall*)(const Capture*, const void*);
    reinterpret_cast<Send>(route.native_send)(&capture, reinterpret_cast<const void*>(envelope));
    return true;
}

} // namespace vehicle

namespace vehicle {

struct Identity {
    Address actor{}, brain{}, definition{}, scene{};
    std::uint32_t entity{}, ticket{}, submitted_at{};
    std::uint16_t mid{};
    std::uint8_t owner{};
};
inline store::GrowableTable<Identity, 64, 4096> submissions{};
inline std::uint32_t next_ticket{};
inline Address vehicle_scene{};
inline std::uint32_t vehicle_tick{};
inline void clear_timer_vetoes() noexcept;
inline void clear_scene(Address scene) noexcept {
    clear_timer_vetoes();
    vehicle_scene = scene;
    vehicle_tick = 0;
    submissions.clear();
}
inline Identity* available_submission(const World& w) {
    if (vehicle_scene != w.scene || w.now - vehicle_tick > 0x7FFFFFFFu) clear_scene(w.scene);
    vehicle_tick = w.now;
    if (next_ticket == 0x7fffff) return nullptr;
    for (auto& record : submissions) {
        if (!record.ticket || w.now - record.submitted_at >= w.duration(25)) return &record;
    }
    return submissions.append_slot();
}
inline Identity take_submission(std::uint32_t ticket) noexcept {
    for (auto& record : submissions) {
        if (record.ticket && record.ticket == ticket) {
            const auto captured = record;
            record = {};
            return captured;
        }
    }
    return {};
}
struct Observation {
    Address master{};
    Address extender{};
    Address charge{};
    bool reactiveDamage{};
    bool penetratingHit{};
    bool directSp{};
    // Ratio for comparison between serial updates of the same live identity.
    float hullHealth{-1.0f};

    bool eligible() const noexcept {
        return (master || directSp) && extender && charge;
    }
};

inline bool validRows(Address begin, Address end, std::size_t stride) noexcept {
    return begin && end >= begin && (end - begin) % stride == 0;
}

inline bool containsTag(Address begin, Address end, const char* name) noexcept {
    if (!validRows(begin, end, 0x60)) return false;
    for (; begin != end; begin += 0x60)
        if (SmokeTagEquals(begin, name)) return true;
    return false;
}

inline bool isGunComponent(Address tag) noexcept {
    if (SmokeTagEquals(tag, "gun")) return true;
    for (const char* name : {"gun1", "gun2", "gun3", "gun4", "gun5", "gun6"})
        if (SmokeTagEquals(tag, name)) return true;
    return false;
}

inline bool usableBody(Address actor, Observation& obs) noexcept {
    const Address begin = read<Address>(actor, 0xB90), end = read<Address>(actor, 0xB98);
    if (!validRows(begin, end, 0x28)) return false;
    bool bodyWorks = false;
    for (Address row = begin; row != end; row += 0x28) {
        const Address def = read<Address>(row);
        if (!def || read<Address>(row, 8) != actor) return false;
        const Address tag = read<Address>(def, 0x328);
        if (!tag || read<Address>(def, 0x330) <= tag) continue;
        const auto state = read<std::uint8_t>(row, 0x24);
        const float health = read<float>(row, 0x14), max = read<float>(row, 0x1C);
        if (SmokeTagEquals(tag, "body")) {
            bodyWorks = state == 0;
            if (max > 0.0f && health >= 0.0f && health <= max) obs.hullHealth = health / max;
        } else if (SmokeTagEquals(tag, "trackleft") || SmokeTagEquals(tag, "trackright") ||
                   SmokeTagEquals(tag, "engine") || isGunComponent(tag)) {
            if (state == 1 || state == 2 || (state == 0 && health >= 0.0f && health < max)) obs.reactiveDamage = true;
        }
    }
    return bodyWorks;
}

inline bool hasWeaponAndPlaces(Address actor) noexcept {
    const Address crew = read<Address>(actor, 0x568), weapons = read<Address>(actor, 0xDD8);
    if (!crew || !weapons || read<Address>(crew, 0x10) == read<Address>(crew, 0x18)) return false;
    const Address places = read<Address>(weapons, 0x18);
    if (places && places != read<Address>(weapons, 0x20)) {
        const Address place = read<Address>(places);
        if (place && read<Address>(place, 0x2B8)) return true;
    }
    const Address first = read<Address>(weapons, 0x30);
    return first && first != read<Address>(weapons, 0x38) && read<Address>(first);
}

enum class Payload {
    none,
    german,
    british,
    mortar
};

inline Payload stockPayload(Address actor) noexcept {
    const Address def = read<Address>(actor, 0x548);
    if (!def) return Payload::none;
    const Address first = read<Address>(def, 0x330), end = read<Address>(def, 0x338);
    const Address dynamic = read<Address>(actor, 0x4E8), dynamicEnd = read<Address>(actor, 0x4F0);
    const auto has = [&](const char* tag) { return containsTag(first, end, tag); };
    const auto dynamicHas = [&](const char* tag) { return containsTag(dynamic, dynamicEnd, tag); };
    if (has("2in_wp") || dynamicHas("2in_wp")) return Payload::none;
    struct Program {
        const char* name;
        Payload payload;
    };
    static constexpr Program programs[] = {{"nkav", Payload::german},   {"nwg", Payload::german},
                                           {"nwg_2x", Payload::german}, {"nvw", Payload::german},
                                           {"4in", Payload::british},   {"4in_2x", Payload::british},
                                           {"2in", Payload::mortar},    {"morris_cs9", Payload::british}};
    Payload result = Payload::none;
    const bool smokeProperty = has("smoke");
    for (const Program& p : programs) {
        if (dynamicHas(p.name) && !has(p.name)) return Payload::none;
        if (!has(p.name)) continue;
        const bool specific = std::strcmp(p.name, "morris_cs9") == 0;
        if (result != Payload::none || (!specific && !smokeProperty)) return Payload::none;
        result = p.payload;
    }
    return result;
}

// Follow the engine's box slot order. A different first matching item vetoes
// the action even when a qualifying charge exists farther along the vector.
inline Address firstCharge(Address actor, Address ext, Payload payload) noexcept {
    const Address mask = read<Address>(ext, 0x470), maskEnd = read<Address>(ext, 0x478);
    if (!mask || maskEnd < mask || maskEnd - mask != 2 * 0x60) return 0;
    const bool mortar = payload == Payload::mortar;
    const char* kind = mortar ? "mortareng_50" : "grenade";
    const char* name = mortar ? "sm" : (payload == Payload::german ? "nbks" : "no8_smoke");
    if (!containsTag(mask, maskEnd, kind) || !containsTag(mask, maskEnd, name)) return 0;
    const Address agg = read<Address>(actor, 0x578);
    const Address manager = agg ? read<Address>(agg, 0x28) : 0;
    const Address box = manager ? read<Address>(manager, 0xA0) : 0;
    if (!box || read<Address>(box, 0x50) != actor) return 0;
    const Address first = read<Address>(box, 0x10), end = read<Address>(box, 0x18);
    if (!validRows(first, end, sizeof(Address))) return 0;
    for (Address p = first; p != end; p += sizeof(Address)) {
        const Address item = read<Address>(p);
        const Address def = item ? read<Address>(item, 0x40) : 0;
        if (!def) continue;
        const Address tags = read<Address>(def, 0x11E0), tagsEnd = read<Address>(def, 0x11E8);
        if (!containsTag(tags, tagsEnd, kind) || !containsTag(tags, tagsEnd, name)) continue;
        if (read<Address>(item, 0x30) != box || read<std::int32_t>(item, 0x48) <= 0 || read<Address>(item, 0x50) ||
            read<Address>(def, 0x480) != read<Address>(def, 0x488))
            return 0;
        if (!mortar) return IsGrenadeDefinition(def, name) ? item : 0;
        const Address key = read<Address>(def, 0xB80), keyEnd = read<Address>(def, 0xB88);
        if (key && keyEnd >= key && keyEnd - key == 3 * 0x60 && containsTag(key, keyEnd, "ammo") &&
            containsTag(key, keyEnd, kind) && containsTag(key, keyEnd, name))
            return item;
        return 0;
    }
    return 0;
}

// Borrowed addresses are valid only for the admitted actor-update invocation.
inline Observation inspect(Address base, Address actor, std::uint32_t now) noexcept {
    Observation obs{};
    // Zero terminates the actor list in the stock eControl codec.
    if (!actor || (read<std::uint8_t>(actor, 0x3A) & 1) || !read<std::uint16_t>(actor, 0xDAA) ||
        !(read<std::uint8_t>(actor, 0x30) & 2) || !(read<std::uint32_t>(actor, 0xDB4) & 0x100) ||
        !usableBody(actor, obs))
        return obs;
    const Address master = read<Address>(base, 0x15D2BE0); // service manager +0x50
    const auto owner = read<std::uint8_t>(actor, 0xDA8);
    if (vehicle_control_route(base, master, owner).context) {
        obs.master = master;
    } else if (!is_mp_host_emit(base)) {
        // Local SP / Skirmish / Conquest without MP eControl route: use direct slot 10 under WP guard
        obs.directSp = true;
    } else {
        return obs;
    }
    obs.penetratingHit = containsTag(read<Address>(actor, 0x4E8), read<Address>(actor, 0x4F0), "pen_fx");
    obs.reactiveDamage |= obs.penetratingHit;
    const Payload payload = stockPayload(actor);
    if (payload == Payload::none || !hasWeaponAndPlaces(actor)) return obs;
    const Address agg = read<Address>(actor, 0x578);
    if (!agg) return obs;
    const Address first = read<Address>(agg), end = read<Address>(agg, 8);
    if (!validRows(first, end, sizeof(Address))) return obs;
    for (Address p = first; p != end; p += sizeof(Address)) {
        const Address ext = read<Address>(p);
        if (!ext || !SmokeTagEquals(ext + 0x20, "smokescreen")) continue;
        if (read<Address>(ext) != base + 0x1031C48 || read<Address>(ext, 0x18) != actor) return obs;
        const auto deadline = read<std::uint32_t>(ext, 0x154);
        if (deadline && static_cast<std::int32_t>(deadline - now) > read<std::int32_t>(base, 0x1584508)) return obs;
        obs.extender = ext;
        obs.charge = firstCharge(actor, ext, payload);
        return obs;
    }
    return obs;
}

inline void deploy_direct_sp(Address base, Address actor, Address extender) noexcept;

// eControl's serializer copies the one borrowed actor ID before it returns.
// No game destructor sees the borrowed vector. The ordinary input receiver
// executes slot 10, charging inventory and setting the extender deadline.
inline void deploy(Address base, const Observation& obs) {
    if (!obs.eligible()) return;
    Address actor = read<Address>(obs.extender, 0x18);
    if (!actor) return;
    if (obs.directSp && !obs.master) {
        deploy_direct_sp(base, actor, obs.extender);
        return;
    }
    const auto owner = read<std::uint8_t>(actor, 0xDA8);
    const auto route = vehicle_control_route(base, obs.master, owner);
    if (!route.context) return;
    struct alignas(16) Control {
        Address vtable;
        std::uint16_t action;
        std::uint8_t pad0A[6];
        Address router;
        std::uint16_t flags;
        std::uint8_t pad1A[6];
        Address* first;
        Address* end;
        Address* capacity;
        std::uint8_t remaining[0x120 - 0x38];
    } packet{};
    static_assert(offsetof(Control, router) == 0x10 && offsetof(Control, flags) == 0x18 &&
                      offsetof(Control, first) == 0x20 && offsetof(Control, remaining) == 0x38 &&
                      sizeof(Control) == 0x120,
                  "eControl serializer layout");
    packet.vtable = route.control_vtable;
    packet.action = 0x11;
    packet.router = route.router;
    using Initialize = void(__fastcall*)(Control*);
    reinterpret_cast<Initialize>(base + 0xB98C50)(&packet);
    reinterpret_cast<std::uint8_t*>(&packet)[0x43] = owner;
    // Both are ordinary optional eControl fields. The stock smoke handler uses
    // +42 only for its "==1" UI acknowledgement and ignores +FC.
    packet.flags = 0x140;
    World w{};
    if (!world(base, w)) return;
    auto* receipt = available_submission(w);
    if (!receipt) return;
    const std::uint32_t ticket = 0x3F000000u | ++next_ticket;
    reinterpret_cast<std::uint8_t*>(&packet)[0x42] = 0xE3;
    std::memcpy(reinterpret_cast<std::uint8_t*>(&packet) + 0xFC, &ticket, sizeof(ticket));
    packet.first = &actor;
    packet.end = packet.capacity = &actor + 1;
    // The master session's generic UI-router drain skips outgoing records.
    // The stock input sender's master slot 22 explicitly forwards message
    // 0x31 to its bound server. The server schedules it for native replay.
    struct alignas(8) Envelope {
        std::uint8_t player, category, action, pad[5];
        alignas(8) std::uint8_t stream[0x50];
    } envelope{};
    static_assert(offsetof(Envelope, stream) == 8 && sizeof(Envelope) == 0x58,
                  "eInterfaceCommand memory-stream layout");
    envelope.player = owner;
    envelope.category = 1;
    envelope.action = 0x11;
    using InitStream = void*(__fastcall*)(void*, std::uint32_t, std::uint32_t);
    using CloseStream = void(__fastcall*)(void*);
    using Encode = void(__fastcall*)(const Control*, void*);
    *receipt = {actor,   read<Address>(actor, 0xDC8),       read<Address>(actor, 0x548),
                w.scene, read<std::uint32_t>(actor, 0xC0),  ticket,
                w.now,   read<std::uint16_t>(actor, 0xDAA), owner};
    reinterpret_cast<InitStream>(base + 0x68C630)(envelope.stream, 0x100, 0);
    reinterpret_cast<Encode>(base + 0xB98F20)(&packet, envelope.stream);
    if (!send_authorized_control(base, obs.master, owner, reinterpret_cast<Address>(&packet),
                                 reinterpret_cast<Address>(&envelope)) &&
        receipt->ticket == ticket)
        *receipt = {};
    reinterpret_cast<CloseStream>(base + 0x68C6A0)(envelope.stream);
}

} // namespace vehicle

inline bool has_tag(Address first, Address end, const char* label) noexcept {
    if (!first || end < first || (end - first) % 0x60 || (end - first) / 0x60 > 2048) return false;
    for (Address p = first; p != end; p += 0x60)
        if (SmokeTagEquals(p, label)) return true;
    return false;
}

struct Threat {
    Vec3 position{};
    int rank = 0;
    float distance = 0;
};

inline Threat primary_threat(const World& w, Address observer, bool vehicle, bool medical) noexcept {
    Threat best{};
    const auto sensor = read<Address>(observer, 0xDD0), brain = read<Address>(observer, 0xDC8);
    if (!sensor || !brain || read<Address>(sensor, 0x260) != observer) return best;
    const auto begin = read<Address>(sensor, 0x280), end = read<Address>(sensor, 0x288);
    if (!begin || end < begin || (end - begin) % 8 || (end - begin) / 8 > 4096) return best;
    const Vec3 self = read<Vec3>(observer, 0x74);
    if (!finite(self)) return best;
    const bool human = read<Address>(brain) == w.base + 0x110FF88;
    const float morale = human ? read<float>(brain, 0x474) : 100.f;
    const auto recovery = human ? read<std::uint32_t>(brain, 0x478) : 0;
    const auto remaining = recovery - w.now;
    // A negative morale delta arms 10 s. Ordinary recovery rearms only 0.5 s.
    const bool losing_morale = recovery && remaining > w.duration(7) && remaining <= w.duration(10);
    const bool under_pressure =
        (morale >= 0 && morale < 50) || losing_morale || recent(w, read<std::uint32_t>(sensor, 0x2F4), 3);
    for (Address p = begin; p != end; p += 8) {
        const auto sub = read<Address>(p);
        if (!sub || read<std::uint8_t>(sub, 0x6C)) continue;
        const auto relation = read<std::uint32_t>(sub, 0x14), flags = read<std::uint32_t>(sub, 0x10);
        if ((relation & 0x1Eu) != 2 || !(flags & (1u << 28))) continue;
        const bool visible = (flags & (1u << 29)) != 0;
        if (!visible && !recent(w, read<std::uint32_t>(sub, 0x64), 4)) continue;
        const bool attack = (relation & 0x100) && recent(w, read<std::uint32_t>(sub, 0x68), 1);
        const bool threat = (relation & 0x080) && recent(w, read<std::uint32_t>(sub, 0x68), 2);
        const auto enemy = read<Address>(sub, 0x18);
        if (!(relation & 1) || !has_actor(enemy)) continue;
        bool combat_vehicle = false;
        for (const char* name : {"tank_heavy", "tank_medium", "tank_light", "tank_bullet", "armoredcar", "armoredcar_l",
                                 "car_bullet", "cannon_big", "cannon_small", "cannon_bullet", "cannon_zenite",
                                 "cannon_aa", "tank_spg", "tank_zenite", "halftrack", "howitzer", "cannon_howitzer"}) {
            if (SmokeTagEquals(enemy + 0xDE8, name)) {
                combat_vehicle = true;
                break;
            }
        }
        const auto weapon = read<Address>(sub, 0x40);
        const auto first = weapon ? read<Address>(weapon, 0x11E0) : 0;
        const auto last = weapon ? read<Address>(weapon, 0x11E8) : 0;
        const bool antitank =
            has_tag(first, last, "rocketlauncher") || has_tag(first, last, "at_rifle") || has_tag(first, last, "ptr");
        const bool human_enemy = (read<std::uint8_t>(enemy, 0x30) & 4) != 0;
        // A recent attack signal itself identifies the source of fire. For a
        // merely sighted human, require an armed enemy squad or an MG.
        const auto enemy_brain = human_enemy ? read<Address>(enemy, 0xDC8) : 0;
        const Address enemy_squad = enemy_brain ? read<Address>(enemy_brain, 0x100) : 0;
        const bool group = enemy_squad && read<Address>(enemy_squad) == w.base + 0x110F3A0;
        const bool mg = has_tag(first, last, "mgun") || has_tag(first, last, "machinegun");
        if (!vehicle && !combat_vehicle && !(human_enemy && (group || mg))) continue;
        if (!attack && !threat) {
            if (vehicle ? !(combat_vehicle || antitank) : (!(combat_vehicle || group || mg) || !under_pressure))
                continue;
            if (medical && !recent(w, read<std::uint32_t>(sensor, 0x2F4), 3)) continue;
        }
        const Vec3 pos = read<Vec3>(sub, 0x28);
        const float d2 = distance2(self, pos);
        if (!finite(pos) || !std::isfinite(d2) || d2 <= 0) continue;
        const int rank = (attack   ? 16
                          : threat ? 8
                                   : 0) +
                         (visible ? 4 : 0) + (combat_vehicle || antitank || mg ? 2 : 0) + 1;
        if (rank > best.rank || (rank == best.rank && d2 < best.distance)) best = {pos, rank, d2};
    }
    return best;
}

inline bool patient(const World& w, Address actor) noexcept {
    if (!has_actor(actor) || !(read<std::uint8_t>(actor, 0x30) & 4)) return false;
    const auto agg = read<Address>(actor, 0x578);
    const auto vit = agg ? read<Address>(agg, 0x18) : 0;
    if (!vit || (read<std::uint32_t>(vit, 0x174) & 1)) return false;
    using Patient = std::uint8_t(__fastcall*)(Address);
    return reinterpret_cast<Patient>(w.base + 0x379260)(actor) != 0;
}

struct Situation {
    Vec3 anchor{};
    Threat threat{};
    bool medical = false;
    Address protected_actor{};
};

inline bool better(const Threat& candidate, const Threat& best) noexcept {
    return candidate.rank > best.rank ||
           (candidate.rank && candidate.rank == best.rank && candidate.distance < best.distance);
}

inline bool dry_ground(Address base, Address terrain, const float* xy) noexcept {
    const Address levels = terrain ? read<Address>(terrain, 0x58) : 0;
    if (!levels || levels == read<Address>(terrain, 0x60)) return false;
    const Address level = read<Address>(levels);
    if (!level || !read<Address>(level, 8)) return false;
    const int cell = read<int>(level);
    if (cell <= 0) return false;
    // Both native samplers clamp coordinates to the map edge. Reject outside
    // positions before their float-to-int conversion or index clamping.
    const float x = xy[0] + read<float>(terrain, 0x34);
    const float y = xy[1] + read<float>(terrain, 0x38);
    if (!(x >= 0 && y >= 0 && x < 2147483648.f && y < 2147483648.f && x / cell < read<int>(level, 0x18) &&
          y / cell < read<int>(level, 0x1C)))
        return false;
    using Env = std::uint16_t*(__fastcall*)(std::uint16_t*, const float*);
    std::uint16_t env{};
    reinterpret_cast<Env>(base + 0xA62620)(&env, xy);
    const unsigned kind = env & 0xffu;
    return kind == 1 || kind == 6 || kind == 7 || (kind >= 9 && kind <= 11);
}

struct ScreenRay {
    float ux{}, uy{}, first{}, last{};
};
inline bool reachable_screen_ray(float scale, Vec3 origin, Vec3 anchor, Vec3 threat, float range,
                                 ScreenRay& ray) noexcept {
    if (!finite(origin) || !finite(anchor) || !finite(threat) || !std::isfinite(range) || !(scale > 0) || range <= 0)
        return false;
    const float separation = std::sqrt(length2_xy(anchor, threat));
    if (!std::isfinite(separation) || separation < 3 * scale) return false;
    const float ux = (threat.x - anchor.x) / separation, uy = (threat.y - anchor.y) / separation;
    const float dx = anchor.x - origin.x, dy = anchor.y - origin.y;
    const float along = dx * ux + dy * uy, side = dx * uy - dy * ux;
    const float disk = range * range - side * side;
    if (!(disk >= 0) || !std::isfinite(disk)) return false;
    const float half_chord = std::sqrt(disk);
    ray = {ux, uy, (std::fmax)(scale, -along - half_chord),
           (std::fmin)((std::fmin)(12 * scale, separation * 0.5f), -along + half_chord)};
    return ray.first <= ray.last;
}

// origin is the thrower's current position. anchor may be a different soldier.
// Try only the part of the threat ray between its endpoints and inside the
// thrower's range circle. Walking the anchor toward the thrower could put smoke
// on the wrong side of a patient.
inline bool screen_point(const World& w, Vec3 origin, Vec3 anchor, Vec3 threat, float range, Vec3& out) noexcept {
    ScreenRay ray{};
    if (!reachable_screen_ray(w.length, origin, anchor, threat, range, ray)) return false;
    const auto terrain = read<Address>(w.scene, 0x18);
    using Height = float(__fastcall*)(Address, const float*, int);
    float previous = -1;
    for (float metres : {9.f, 8.f, 10.f, 7.f, 11.f, 6.f, 12.f, 5.f, 4.f, 3.f, 2.f}) {
        const float step = (std::fmax)(ray.first, (std::fmin)(metres * w.length, ray.last));
        if (step == previous) continue;
        previous = step;
        float xy[2] = {anchor.x + step * ray.ux, anchor.y + step * ray.uy};
        if (!dry_ground(w.base, terrain, xy)) continue;
        Vec3 point{xy[0], xy[1], reinterpret_cast<Height>(w.base + 0xA5D440)(terrain, xy, 0)};
        if (finite(point) && distance2(origin, point) <= range * range) {
            out = point;
            return true;
        }
    }
    return false;
}

namespace orders {

struct Path {
    Address queue{};     // Innermost interruptible native sequence.
    Address suspended{}; // Sole current Medic/Heal eligible for native snapshot.
    Address patient{};   // Medic's borrowed receiver at +0x48.
    bool healing{}, allowed{};
};

inline bool is_single(Address base, Address table) noexcept {
    return table && read<Address>(table, 0x18) == base + 0x919F60 && read<Address>(table, 0x20) == base + 0x91A0E0;
}

inline Address active_child(Address base, Address order) noexcept {
    if (!order) return 0;
    const Address table = read<Address>(order);
    if (is_single(base, table)) return read<Address>(order, 0x30);
    if (table != base + 0x110D490) return 0;
    const auto count = read<std::size_t>(order, 0x48);
    const Address map = read<Address>(order, 0x30);
    const auto blocks = read<std::size_t>(order, 0x38);
    const auto start = read<std::size_t>(order, 0x40);
    if (!count || count > 0x7fff || !map || !blocks || (blocks & (blocks - 1)) || (count + 1) / 2 > blocks) return 0;
    const Address block = read<Address>(map, ((start >> 1) & (blocks - 1)) * sizeof(Address));
    return block ? read<Address>(block, (start & 1) * sizeof(Address)) : 0;
}

// Read only the active front/child. Tail entries remain owned by the sequence.
// Default/user orders and their timed queue have precedence over a smoke action.
inline Path inspect(Address base, Address brain) noexcept {
    Path path{};
    if (!brain || read<Address>(brain) != base + 0x110FF88) return path;
    const Address actor = read<Address>(brain, 0x38);
    const Address root = read<Address>(brain, 0xE0);
    if (!actor || !root || read<Address>(root) != base + 0x1110CD0 || read<Address>(root, 0x30) != brain ||
        read<std::uint32_t>(root, 0x4C0) || read<Address>(root, 0x568) || read<std::int32_t>(brain, 0x88) != 0 ||
        read<Address>(brain, 0x98) != read<Address>(brain, 0xA0) || (read<std::uint32_t>(brain, 0x39C) & 0x1110003fu))
        return path;
    Address order = read<Address>(brain, 0xF0);
    if (!order) {
        path.allowed = true;
        return path;
    }
    Address standalone{};
    for (unsigned depth = 0; depth < 32; ++depth) {
        const Address table = read<Address>(order);
        if (!table || table == base + 0x110D278 || read<Address>(order, 8) != actor + 0xAF0) return path;
        if (table == base + 0x1080638) {
            path.healing = true;
            path.patient = read<Address>(order, 0x48);
            // End releases a live patient lock and breaks the treatment FSM.
            // During approach native restart can re-evaluate range/retry from
            // states 1/2 even if End disposed the old movement child.
            const int phase = read<int>(order, 0x44);
            if (!path.patient || read<std::uint8_t>(path.patient, 0x12BA) || read<std::uint8_t>(order, 0x38) ||
                phase < 1 || phase > 2)
                return path;
            if (!depth) standalone = order;
        } else if (table == base + 0x10223F0) {
            path.healing = true; // eHeal treats the carrier.
            path.patient = actor;
            // Restarting an active Heal resets its bandage budget.
            if ((read<std::uint32_t>(order, 0x18) & 0x10000u) && read<int>(order, 0x3C) != 0) return path;
            if (!depth) standalone = order;
        }
        if (table == base + 0x110D490) {
            // Deque has two pointer elements per block. During an executing or
            // deferred End, and with a pending child End, insertion must wait.
            if ((read<std::uint32_t>(order, 0x20) & 7) || read<Address>(order, 0x50)) return path;
            const auto count = read<std::size_t>(order, 0x48);
            if (count > 0x7fff) return path;
            path.queue = order;
            if (!count) {
                path.allowed = true;
                return path;
            }
            order = active_child(base, order);
            if (!order) return path;
        } else {
            // These two exact implementations operate on eSingleOrder+0x30.
            if (!is_single(base, table)) return path;
            const Address child = read<Address>(order, 0x30);
            if (!child) {
                path.suspended = path.queue ? 0 : standalone;
                path.allowed = path.queue || path.suspended;
                return path;
            }
            order = child;
        }
    }
    return path;
}

// Ownership of a freshly constructed, unstarted order transfers to native code
// on success. The caller owns it on failure. Call immediately after a fresh
// serial admission, outside order callbacks. Actor/squad authority is additional.
inline bool install(Address base, Address brain, const Path& prepared, Address fresh) noexcept {
    if (!fresh || !prepared.allowed || (read<std::uint32_t>(fresh, 0x18) & 0x30000u)) return false;
    const Path live = inspect(base, brain);
    if (!live.allowed || live.queue != prepared.queue || live.suspended != prepared.suspended) return false;
    if (live.queue) {
        using Insert = Address(__fastcall*)(Address, Address, int, std::uint8_t);
        const int count = static_cast<int>(read<std::size_t>(live.queue, 0x48));
        reinterpret_cast<Insert>(base + 0x919270)(live.queue, fresh, count, 1);
    } else if (live.suspended) {
        // Native restore_order can resume the original before smoke finishes.
        // The smoke lease must detect that preemption before allowing release.
        using Interrupt = void(__fastcall*)(Address, Address, std::uint8_t);
        reinterpret_cast<Interrupt>(base + 0x96F2F0)(read<Address>(brain, 0xE0), fresh, 0);
    } else {
        using Set = void(__fastcall*)(Address, Address);
        reinterpret_cast<Set>(base + 0x91DB30)(brain, fresh);
    }
    return true;
}
} // namespace orders

inline Situation situation(const World& w, Address carrier, Address squad, bool healing,
                           Address assigned_patient) noexcept {
    const Vec3 self = read<Vec3>(carrier, 0x74);
    const auto owner = read<std::uint8_t>(carrier, 0xDA8);
    Situation result{self, primary_threat(w, carrier, false, false), false, carrier};
    Threat medical_threat = primary_threat(w, carrier, false, true);
    const float nearby = 400 * w.length * w.length;
    Vec3 casualty{};
    Address casualty_actor{};
    float nearest = nearby;
    bool found_patient = false;
    const auto consider = [&](Address member, bool assigned) {
        if (!has_actor(member) || !allies(w.base, owner, read<std::uint8_t>(member, 0xDA8))) return;
        const Vec3 position = read<Vec3>(member, 0x74);
        const float dist = distance2(self, position);
        if (!finite(position) || !std::isfinite(dist) || dist > nearby) return;
        if (patient(w, member)) {
            if (assigned || (!assigned_patient && (!found_patient || dist < nearest))) {
                casualty_actor = member;
                casualty = position;
                nearest = dist;
                found_patient = true;
            }
        } else if (member != carrier && !assigned) {
            const auto other = read<Address>(member, 0xDC8);
            if (!other || read<Address>(other) != w.base + 0x110FF88 || read<Address>(other, 0x38) != member ||
                read<Address>(other, 0x100) != squad)
                return;
            auto observed = primary_threat(w, member, false, false);
            if (better(observed, result.threat)) result = {position, observed, false, member};
            observed = primary_threat(w, member, false, true);
            if (better(observed, medical_threat)) medical_threat = observed;
        }
    };
    if (assigned_patient && assigned_patient != carrier) consider(assigned_patient, true);
    if (squad && read<Address>(squad) == w.base + 0x110F3A0) {
        const auto begin = read<Address>(squad, 0x80), end = read<Address>(squad, 0x88);
        if (begin && end >= begin && (end - begin) % 8 == 0 && (end - begin) / 8 <= 256) {
            for (Address p = begin; p != end; p += 8) {
                const Address mate = read<Address>(p);
                if (!has_actor(mate) || !(read<std::uint8_t>(mate, 0x30) & 4)) continue;
                // The live native member list also covers a casualty whose
                // brain no longer supplies observations.
                consider(mate, false);
            }
        }
    }
    if (medical_threat.rank && (found_patient || (healing && (!assigned_patient || assigned_patient == carrier))))
        return {found_patient ? casualty : self, medical_threat, true, found_patient ? casualty_actor : carrier};
    if (healing) return {};
    return result;
}

// The point arm of eTarget's native copy constructor reads only kind and XYZ
// (0x85292E, 0x852B75..8F, 0x852CBB..D4). The factory deep-copies both
// this point and the immutable definition's exact key using the game heap.
inline Address make_smoke_order(Address base, Address definition, Vec3 screen) noexcept {
    if (!IsPureSmokeDefinition(definition) || !finite(screen)) return 0;
    struct PointArgument {
        Address reserved;
        std::uint32_t kind;
        Vec3 point;
    };
    static_assert(offsetof(PointArgument, point) == 0xC);
    const PointArgument argument{0, 2, screen};
    using Factory = Address (*)(Address, std::uint32_t, const PointArgument*);
    return reinterpret_cast<Factory>(base + 0x901600)(definition + 0x868, 1, &argument);
}

inline float nominal_throw_range(Address base, Address definition) noexcept {
    if (!IsPureSmokeDefinition(definition)) return 0.0f;
    const Address properties = read<Address>(definition, 0x7D0);
    if (!properties || read<Address>(properties) != base + 0x1100908) return 0.0f;
    const float range = read<float>(properties, 0x10);
    return std::isfinite(range) && range > 0.0f ? range : 0.0f;
}

// Held-item and selected-weapon checks are repeated at release. These are
// synchronous borrows on the admitted simulation thread.
inline Address smoke_shooter_for_actor(Address base, Address actor, Address definition) noexcept {
    if (!actor || !definition) return 0;
    using Getter = Address (*)(Address);
    const Address item = FindCarriedPureSmokeItem(base, actor);
    if (!item || read<Address>(item, 0x40) != definition) return 0;
    const Address hand = read<Address>(item, 0x38);
    if (!hand || read<Address>(hand) != base + 0x10FF210 || read<Address>(hand, 0x3D0) != actor ||
        read<Address>(hand, 0x3D8) != item)
        return 0;

    const Address weaponry = read<Address>(actor, 0xDD8);
    if (!weaponry) return 0;
    const auto selected = reinterpret_cast<Getter>(base + 0x81B080);
    const Address weapon = selected(weaponry);
    if (!weapon || read<Address>(weapon, 0x90) != actor) return 0;
    const Address shooter = read<Address>(weapon, 0xB8);
    return shooter && read<Address>(shooter) == base + 0x1102F20 && read<Address>(shooter, 8) == weapon ? shooter : 0;
}

struct SmokeHits {
    Address begin{}, end{}, capacity{};
};

// Return the first contact of the stock projectile query on the sampled vectors.
// 0x84F280 uses the game's hit-vector growth, then the same deallocation
// contract as its temporary vector in 0x84FBA2..F8.
struct ThrowContact {
    unsigned kind;
    Vec3 position;
    Address entity, volume;
};
inline bool first_throw_contact(Address base, Address actor, Address weapon, const Vec3& origin, const Vec3& velocity,
                                ThrowContact& contact) noexcept {
    if (!actor || !weapon || !finite(origin) || !finite(velocity)) return false;
    const float scale = read<float>(base, 0x149E750);
    const float gravity = read<float>(base, 0x15754E0);
    if (!std::isfinite(scale) || scale <= 0 || !std::isfinite(gravity) || gravity <= 0) return false;
    alignas(16) unsigned char trajectory[0x60]{};
    SmokeHits hits{};
    using Init = void* (*)(void*, Address, SmokeHits*, const Vec3*, const Vec3*);
    using Flags = void* (*)(void*, std::uint32_t);
    using Run = void (*)(void*);
    reinterpret_cast<Init>(base + 0x84F1B0)(trajectory, actor, &hits, &origin, &velocity);
    // ThrowingShooter::Try ORs 0x900000 into caller flags 0x80800.
    std::uint32_t flags = 0x980800;
    if (read<std::uint32_t>(weapon, 0x208) == 1) flags |= 0x40000;
    reinterpret_cast<Flags>(base + 0x84F260)(trajectory, flags);
    std::memcpy(trajectory + 0xC, &gravity, sizeof(gravity));
    reinterpret_cast<Run>(base + 0x84F280)(trajectory);
    const bool found =
        hits.begin && hits.end >= hits.begin && hits.end <= hits.capacity && hits.end - hits.begin >= 0x48u;
    if (found)
        contact = {read<unsigned>(hits.begin), read<Vec3>(hits.begin, 8), read<Address>(hits.begin, 0x18),
                   read<Address>(hits.begin, 0x20)};
    if (hits.begin) {
        const Address allocation =
            hits.capacity - hits.begin >= 0x1000 ? read<Address>(hits.begin - sizeof(Address)) : hits.begin;
        reinterpret_cast<void (*)(Address)>(base + 0xDA1E38)(allocation);
    }
    return found;
}

inline bool flight_reaches_screen(Address base, Address actor, Address weapon, const Vec3& origin, const Vec3& velocity,
                                  const Vec3& screen) noexcept {
    ThrowContact first{};
    if (!finite(screen) || !first_throw_contact(base, actor, weapon, origin, velocity, first)) return false;
    const float scale = read<float>(base, 0x149E750);
    const float tolerance = scale * 4.5f;
    const float minThrow = scale * 1.5f;
    if (first.kind == 2 || !finite(first.position) || length2_xy(first.position, origin) < minThrow * minThrow ||
        length2_xy(first.position, screen) > tolerance * tolerance)
        return false;
    const Address scene = read<Address>(base, 0x15C9668);
    const float xy[]{first.position.x, first.position.y};
    return scene && dry_ground(base, read<Address>(scene, 0x18), xy);
}

using SmokeRelease = void (*)(Address shooter);
using SmokeSend = void (*)(Address command);
using SmokeSpawn = void (*)(Address shooter, const Vec3* origin, const Vec3* velocity, const Vec3* spin);

struct GrenadeCallbacks {
    int (*fire_status)(Address, unsigned){};
    void (*release)(SmokeRelease, Address){};
    bool (*send_allowed)(Address){};
    bool (*spawn_allowed)(Address, const Vec3*&, const Vec3*&, const Vec3*&){};
    void (*shooter_destroyed)(Address){};
    void (*new_firing)(Address, std::uint8_t){};
    bool (*is_busy)(const World&, Address){};
};
inline GrenadeCallbacks grenades{};

struct SmokeReleaseScope {
    Address base, actor, weapon, shooter, definition;
    Vec3 screen, origin{}, velocity{}, spin{};
    bool sent{}, spawned{};
};

inline SmokeReleaseScope* active_release_scope = nullptr;

// Bind a single native sampled launch to its matching wire command and local
// consumption. The lease owner must consume its one-shot allowance before entry.
inline bool scoped_smoke_release(Address base, SmokeRelease original, Address shooter, Address definition, Vec3 screen,
                                 bool permit) noexcept {
    if (!permit || !shooter || !finite(screen)) return false;
    const Address weapon = read<Address>(shooter, 8);
    const Address actor = weapon ? read<Address>(weapon, 0x90) : 0;
    if (!actor || smoke_shooter_for_actor(base, actor, definition) != shooter) return false;
    if (read<std::uint32_t>(weapon, 0xD0) != 2) return false;
    const Vec3 current_target = read<Vec3>(weapon, 0xD4);
    if (current_target.x != screen.x || current_target.y != screen.y || current_target.z != screen.z) return false;
    SmokeReleaseScope scope{base, actor, weapon, shooter, definition, screen};
    auto* frame = quant_frame();
    SmokeReleaseScope* prev_frame = frame ? frame->release : nullptr;
    SmokeReleaseScope* prev_global = active_release_scope;
    if (frame) frame->release = &scope;
    active_release_scope = &scope;
    original(shooter);
    active_release_scope = prev_global;
    if (frame) frame->release = prev_frame;
    return scope.spawned;
}

// Unrelated networking uses the original sender immediately. C2B740 was
// required at admission and checked again by the stock shooter at 0x8319F2.
inline void smoke_send_adapter(SmokeSend original, Address command) noexcept {
    auto* frame = quant_frame();
    auto* scope = (frame && frame->release) ? frame->release : active_release_scope;
    if (!scope) {
        original(command);
        return;
    }
    if (!command || read<Address>(command) != scope->base + 0x10231C0 || read<std::uint32_t>(command, 8) != 0x0D ||
        read<std::uint32_t>(command, 0x20) != 2 || read<Address>(command, 0x10) != scope->actor ||
        read<Address>(command, 0x18) != scope->weapon)
        return;
    if (scope->sent) return;
    const Vec3 origin = read<Vec3>(command, 0x24);
    const Vec3 velocity = read<Vec3>(command, 0x30);
    const Vec3 spin = read<Vec3>(command, 0x3C);
    if (smoke_shooter_for_actor(scope->base, scope->actor, scope->definition) != scope->shooter) return;
    const float range = nominal_throw_range(scope->base, scope->definition);
    const float dz = origin.z - scope->screen.z;
    if (!(range > 0) || !(length2_xy(origin, scope->screen) + dz * dz <= (range * 1.15f) * (range * 1.15f)) ||
        !finite(spin) ||
        !flight_reaches_screen(scope->base, scope->actor, scope->weapon, origin, velocity, scope->screen) ||
        smoke_shooter_for_actor(scope->base, scope->actor, scope->definition) != scope->shooter)
        return;
    scope->origin = origin;
    scope->velocity = velocity;
    scope->spin = spin;
    scope->sent = true;
    original(command);
}

// Slot11 proceeds directly from send to SpawnBullet, with the same saved local
// vectors (0x831A90..AAE). In MP host (0xC2B740==1), require smoke_send to have
// validated and transmitted the packet first. In SP/Skirmish (0xC2B740==0),
// native Slot11 skips 0x151340 and jumps straight here, so validate directly.
inline void smoke_spawn_adapter(SmokeSpawn original, Address shooter, const Vec3* origin, const Vec3* velocity,
                                const Vec3* spin) noexcept {
    auto* frame = quant_frame();
    auto* scope = (frame && frame->release) ? frame->release : active_release_scope;
    if (!scope) {
        original(shooter, origin, velocity, spin);
        return;
    }
    if (shooter != scope->shooter || scope->spawned) return;
    if (is_mp_host_emit(scope->base)) {
        if (!scope->sent) return;
    } else {
        if (!origin || !velocity || !spin) return;
        if (smoke_shooter_for_actor(scope->base, scope->actor, scope->definition) != scope->shooter) return;
        const float range = nominal_throw_range(scope->base, scope->definition);
        const float dz = origin->z - scope->screen.z;
        if (!(range > 0) || !(length2_xy(*origin, scope->screen) + dz * dz <= (range * 1.15f) * (range * 1.15f)) ||
            !finite(*spin) ||
            !flight_reaches_screen(scope->base, scope->actor, scope->weapon, *origin, *velocity, scope->screen) ||
            smoke_shooter_for_actor(scope->base, scope->actor, scope->definition) != scope->shooter) {
            if (log_sink) {
                log_sink("AUTO-SMOKE RELEASE VETO (flight): actor=0x%llx screen=(%.1f,%.1f)",
                         (unsigned long long)scope->actor, scope->screen.x, scope->screen.y);
            }
            return;
        }
        scope->origin = *origin;
        scope->velocity = *velocity;
        scope->spin = *spin;
    }
    scope->spawned = true;
    ++stat_inf_released;
    if (log_sink) {
        log_sink("AUTO-SMOKE RELEASED #%u: actor=0x%llx screen=(%.1f,%.1f) mpEmit=%u", stat_inf_released,
                 (unsigned long long)scope->actor, scope->screen.x, scope->screen.y,
                 is_mp_host_emit(scope->base) ? 1u : 0u);
    }
    if (frame) frame->release = nullptr;
    active_release_scope = nullptr;
    original(shooter, &scope->origin, &scope->velocity, &scope->spin);
    active_release_scope = scope;
    if (frame) frame->release = scope;
}

// Entries retain numeric identities and copied coordinates. Never dereference a
// cached actor, squad, or scene address. Serial callbacks own this storage.
struct Reservation {
    Address carrier{}, squad{}, protected_actor{};
    std::uint64_t ticket{};
    std::uint32_t entity{}, start{}, last{}, protected_id{};
    Vec3 anchor{}, known_threat{};
    std::uint8_t player{};
    bool vehicle{}, released{}, medical{};
};
inline store::GrowableTable<Reservation, 64, 4096> reservations{};
inline std::uint64_t next_reservation{};
struct HullSample {
    Address actor{};
    std::uint32_t entity{}, time{}, last_hit{};
    float health{};
    bool hit{};
};
inline store::GrowableTable<HullSample, 64, 4096> hulls{};
inline Address cache_scene{};
inline std::uint32_t cache_tick{};
inline bool have_tick{};

inline void reset_scene_cache(const World& w) noexcept {
    // Unsigned forward deltas include natural wraparound. A rewind during
    // save/load must clear prior mission ownership and suppress stale receipts.
    if (!have_tick || cache_scene != w.scene || w.now - cache_tick > 0x7fffffffu) {
        reservations.clear();
        hulls.clear();
        vehicle::clear_scene(w.scene);
        cache_scene = w.scene;
        have_tick = true;
    }
    cache_tick = w.now;
}
inline bool heavy_hit(const World& w, Address actor, float hp, bool penetrating) {
    if (!(hp >= 0 && hp <= 1)) return penetrating;
    const auto id = read<std::uint32_t>(actor, 0xC0);
    HullSample* oldest = nullptr;
    for (auto& s : hulls) {
        if (s.actor == actor && s.entity == id) {
            if (penetrating || (w.now - s.time <= w.duration(3) && s.health - hp >= 0.20f)) {
                s.hit = true;
                s.last_hit = w.now;
            }
            s.health = hp;
            s.time = w.now;
            if (s.hit && w.now - s.last_hit > w.duration(3)) s.hit = false;
            return s.hit;
        }
        if (!s.actor || !oldest || (oldest->actor && w.now - s.time > w.now - oldest->time)) {
            oldest = &s;
        }
    }
    if (!oldest || oldest->actor) {
        if (auto* grown = hulls.append_slot()) oldest = grown;
    }
    if (oldest) *oldest = {actor, id, w.now, w.now, hp, penetrating};
    return penetrating;
}
inline bool recently_hit(const World& w, Address actor) noexcept {
    if (w.scene != cache_scene) return false;
    const auto id = read<std::uint32_t>(actor, 0xC0);
    for (const auto& sample : hulls)
        if (sample.actor == actor && sample.entity == id) return sample.hit && w.now - sample.last_hit <= w.duration(3);
    return false;
}
inline Reservation* available(const World& w, Address actor, Address squad, Vec3 anchor, bool for_vehicle) {
    Reservation* free_slot = nullptr;
    const auto owner = read<std::uint8_t>(actor, 0xDA8);
    if (owner >= 18) return nullptr;
    const float cooldown_s = for_vehicle ? config::active().tuning.smoke_vehicle_cooldown_s
                                         : config::active().tuning.smoke_infantry_cooldown_s;
    for (auto& r : reservations) {
        if (!r.carrier || w.now - r.last >= w.duration(cooldown_s)) {
            if (!free_slot) free_slot = &r;
            continue;
        }
        if (r.vehicle != for_vehicle) continue;
        if (r.carrier == actor && r.entity == read<std::uint32_t>(actor, 0xC0)) return nullptr;
        if (for_vehicle) continue;
        if (squad && r.squad == squad) return nullptr;
        if ((!squad || !r.squad) && allies(w.base, owner, r.player) &&
            distance2(r.anchor, anchor) <= 400 * w.length * w.length)
            return nullptr;
    }
    return free_slot ? free_slot : reservations.append_slot();
}
inline void reserve(Reservation& r, const World& w, Address actor, Address squad, Vec3 anchor,
                    bool for_vehicle) noexcept {
    if (!++next_reservation) ++next_reservation;
    r = {};
    r.carrier = actor;
    r.squad = squad;
    r.ticket = next_reservation;
    r.entity = read<std::uint32_t>(actor, 0xC0);
    r.start = w.now;
    r.last = w.now;
    r.anchor = anchor;
    r.player = read<std::uint8_t>(actor, 0xDA8);
    r.vehicle = for_vehicle;
}
inline Address image_base{};
inline bool valid_lease_time(Address actor, std::uint32_t now, std::uint64_t ticket) noexcept {
    World w{};
    if (!image_base || !world(image_base, w) || w.scene != cache_scene) return false;
    for (const auto& r : reservations)
        if (r.ticket == ticket && !r.vehicle && !r.released && r.carrier == actor &&
            r.entity == read<std::uint32_t>(actor, 0xC0) && now - r.start <= w.duration(8))
            return true;
    return false;
}
inline void receipt(Address actor, std::uint32_t now, std::uint64_t ticket) noexcept {
    World w{};
    if (!world(image_base, w) || w.scene != cache_scene) return;
    for (auto& r : reservations) {
        if (r.ticket == ticket && !r.vehicle && !r.released && r.carrier == actor &&
            r.entity == read<std::uint32_t>(actor, 0xC0) && now - r.start <= w.duration(8)) {
            r.released = true;
            r.last = now;
            return;
        }
    }
}

// Re-acquire the protected actor from a live owner before release. A medic
// approaching someone outside the squad keeps that patient in its saved order.
inline Address protected_member(const World& w, const Reservation& r, Address actor, Address brain) noexcept {
    if (r.protected_actor == actor) return actor;
    const Address root = read<Address>(brain, 0xE0);
    if (root && read<Address>(root) == w.base + 0x1110CD0 && read<Address>(root, 0x30) == brain) {
        const Address saved = read<Address>(root, 0x568);
        if (saved && read<Address>(saved) == w.base + 0x1080638 && read<Address>(saved, 0x48) == r.protected_actor)
            return read<Address>(saved, 0x48);
    }
    const Address squad = read<Address>(brain, 0x100);
    if (!squad || squad != r.squad || read<Address>(squad) != w.base + 0x110F3A0) return 0;
    const Address first = read<Address>(squad, 0x80), end = read<Address>(squad, 0x88);
    if (!first || end < first || (end - first) % 8 || (end - first) / 8 > 256) return 0;
    for (Address p = first; p != end; p += 8)
        if (read<Address>(p) == r.protected_actor) return read<Address>(p);
    return 0;
}
inline bool still_screens(const World& w, Address actor, Vec3 screen, std::uint64_t ticket) noexcept {
    const Address brain = read<Address>(actor, 0xDC8);
    if (!brain) return false;
    for (const auto& r : reservations) {
        if (r.ticket != ticket || r.vehicle || r.released || r.carrier != actor ||
            r.entity != read<std::uint32_t>(actor, 0xC0) || w.now - r.start > w.duration(8))
            continue;
        if (read<Address>(brain, 0x100) != r.squad) return false;
        const Address member = protected_member(w, r, actor, brain);
        if (!has_actor(member) || !(read<std::uint8_t>(member, 0x30) & 4) ||
            read<std::uint32_t>(member, 0xC0) != r.protected_id ||
            !allies(w.base, r.player, read<std::uint8_t>(member, 0xDA8)))
            return false;
        if (r.medical && member != actor && !patient(w, member)) return false;
        const auto fresh = situation(w, actor, r.squad, r.medical, r.medical ? member : 0);
        if (!fresh.threat.rank && w.now - r.start > w.duration(4)) return false;
        const Vec3 threat = fresh.threat.rank ? fresh.threat.position : r.known_threat;
        const Vec3 anchor = read<Vec3>(member, 0x74);
        const float tx = threat.x - anchor.x, ty = threat.y - anchor.y;
        const float sx = screen.x - anchor.x, sy = screen.y - anchor.y;
        const float len = std::sqrt(tx * tx + ty * ty);
        if (!(len > 0) || !std::isfinite(len)) return false;
        const float forward = (sx * tx + sy * ty) / len;
        return forward >= w.length && forward <= 13 * w.length && forward < len &&
               std::fabs(sx * ty - sy * tx) <= 3 * w.length * len;
    }
    return false;
}

using Decision = void(__fastcall*)(Address);
inline Decision original_decision{};
inline SmokeSpawn original_smoke_spawn{};
inline SmokeSend original_smoke_send{};
inline std::atomic<DWORD> simulation_tid{};
static_assert(std::atomic<DWORD>::is_always_lock_free);
inline bool serial_thread() noexcept {
    return simulation_tid.load(std::memory_order_relaxed) == GetCurrentThreadId();
}

inline bool serial_actor(Address actor) noexcept {
    const auto* frame = quant_frame();
    return actor && frame && frame->actor == actor && serial_thread();
}

namespace vehicle {

using ControlReplay = void(__fastcall*)(Address);
using EventAction = void(__fastcall*)(Address, Address);
inline ControlReplay original_control{};
inline EventAction original_event{}, original_delay{}, original_delay_due{}, original_spawn{};
struct MarkedControl {
    Address owner{};
    Payload admitted{};
};
inline MarkedControl marked_control{};
inline Address screen_event_owner{};

inline void deploy_direct_sp(Address base, Address actor, Address extender) noexcept {
    if (!base || !actor || !extender || is_mp_host_emit(base)) return;
    const Payload p = stockPayload(actor);
    if (p == Payload::none) return;
    const auto previous = marked_control;
    const auto previous_owner = screen_event_owner;
    marked_control = {actor, p};
    screen_event_owner = 0;
    using Slot10 = void(__fastcall*)(Address);
    reinterpret_cast<Slot10>(base + 0x1274C0)(extender);
    screen_event_owner = previous_owner;
    marked_control = previous;
}

// Native 0x128520 first resolves "smokescreen", falls back to "flare".
// Never invoke an unrelated extender from a stale marked input on the host.
inline Address decoded_smoke(Address base, Address actor) noexcept {
    const Address list = read<Address>(actor, 0x578);
    if (!list) return 0;
    const Address first = read<Address>(list), end = read<Address>(list, 8);
    if (!validRows(first, end, sizeof(Address))) return 0;
    for (Address p = first; p != end; p += sizeof(Address)) {
        const Address ext = read<Address>(p);
        if (!ext || !SmokeTagEquals(ext + 0x20, "smokescreen")) continue;
        return read<Address>(ext) == base + 0x1031C48 && read<Address>(ext, 0x18) == actor ? ext : 0;
    }
    return 0;
}

inline void __fastcall replay_control(Address control) {
    runtime::HookGuard guard(hook_gate);
    const auto base = image_base;
    const auto flags = read<std::uint16_t>(control, 0x18);
    const auto ticket = read<std::uint32_t>(control, 0xFC);
    const bool marked =
        flags == 0x140 && read<std::uint8_t>(control, 0x42) == 0xE3 && (ticket & 0xFF800000u) == 0x3F000000u;
    if (!base || reinterpret_cast<Role>(base + 0xC2B7A0)()) {
        original_control(control);
        return;
    }
    if (!marked) {
        if (!serial_thread() || !marked_control.owner) {
            original_control(control);
            return;
        }
        const auto previous = marked_control;
        const auto previous_owner = screen_event_owner;
        marked_control = {};
        screen_event_owner = 0;
        original_control(control);
        marked_control = previous;
        screen_event_owner = previous_owner;
        return;
    }
    if (!serial_thread() || !emitting_host(base)) return;
    const Identity expected = take_submission(ticket);
    const Address first = read<Address>(control, 0x20), end = read<Address>(control, 0x28);
    if (!first || end < first || end - first != sizeof(Address)) return;
    const Address actor = read<Address>(first);
    if (!actor || !(read<std::uint8_t>(actor, 0x30) & 2) || !decoded_smoke(base, actor)) return;
    World w{};
    const auto brain = read<Address>(actor, 0xDC8);
    const bool same =
        expected.ticket && world(base, w) && expected.scene == w.scene &&
        w.now - expected.submitted_at < w.duration(25.0f) && read<std::uint8_t>(control, 9) == expected.owner &&
        read<std::uint8_t>(control, 0x43) == expected.owner && read<std::uint8_t>(actor, 0xDA8) == expected.owner &&
        actor == expected.actor && brain == expected.brain && read<Address>(actor, 0x548) == expected.definition &&
        read<std::uint32_t>(actor, 0xC0) == expected.entity && read<std::uint16_t>(actor, 0xDAA) == expected.mid;
    bool permitted = false;
    if (same && eligible(base, brain, actor) && read<Address>(brain) == base + 0x101FD68) {
        const auto current = inspect(base, actor, w.now);
        permitted = current.eligible() && (current.reactiveDamage || recently_hit(w, actor)) &&
                    primary_threat(w, actor, true, false).rank;
    }
    const auto previous = marked_control;
    const auto previous_owner = screen_event_owner;
    marked_control = {actor, permitted ? stockPayload(actor) : Payload::none};
    screen_event_owner = 0;
    original_control(control); // Native slot 10 owns the debit and deadline.
    screen_event_owner = previous_owner;
    marked_control = previous;
}

inline bool single_wp_program(Address actor) noexcept {
    const Address def = read<Address>(actor, 0x548);
    if (!def) return false;
    const Address first = read<Address>(def, 0x330), end = read<Address>(def, 0x338);
    if (!containsTag(first, end, "smoke") || !containsTag(first, end, "2in_wp")) return false;
    const Address dynamic = read<Address>(actor, 0x4E8), dynamicEnd = read<Address>(actor, 0x4F0);
    for (const char* name : {"nkav", "nwg", "nwg_2x", "nvw", "4in", "4in_2x", "2in", "morris_cs9"})
        if (containsTag(first, end, name) || containsTag(dynamic, dynamicEnd, name)) return false;
    return true;
}

inline void __fastcall replay_event(Address context, Address eventName) {
    runtime::HookGuard guard(hook_gate);
    if (GOH_AUTO_SMOKE_RETURN_ADDRESS() == image_base + 0x127554 && serial_thread() && marked_control.owner &&
        context && read<Address>(context) == marked_control.owner && SmokeTagEquals(eventName, "deploy_smoke")) {
        if (stockPayload(marked_control.owner) == Payload::none && !single_wp_program(marked_control.owner)) return;
        const auto previous = screen_event_owner;
        screen_event_owner = marked_control.owner;
        original_event(context, eventName);
        screen_event_owner = previous;
        return;
    }
    original_event(context, eventName);
}

struct PureTimerVeto {
    Address owner{}, action{}, projectile{}, definition{}, scene{};
    std::uint32_t entity{}, at{};
    std::uint16_t mid{};
};
inline std::vector<PureTimerVeto>& timer_vetoes() noexcept {
    static std::vector<PureTimerVeto> v;
    return v;
}
inline Address due_owner{}, due_projectile{};
inline void clear_timer_vetoes() noexcept {
    timer_vetoes().clear();
    due_owner = due_projectile = 0;
}
inline void remove_veto(std::size_t index) noexcept {
    auto& tv = timer_vetoes();
    if (index < tv.size()) {
        tv[index] = tv.back();
        tv.pop_back();
    }
}

struct ShotTemplate {
    Address physical{};
    Payload payload{};
    bool cap_only{};
};
inline ShotTemplate read_shot(Address action) noexcept {
    const Address first = read<Address>(action, 0x28), end = read<Address>(action, 0x30);
    if (!validRows(first, end, sizeof(Address)) || first == end) return {};
    const Address physical = read<Address>(first);
    if (!physical) return {};
    // The sole sibling of the separate cap-restoration timer is ProcessBone.
    if (end - first == sizeof(Address) && read<Address>(physical) == image_base + 0x10F8AE8)
        return {0, Payload::none, true};
    if (end - first != 3 * sizeof(Address) || read<Address>(physical) != image_base + 0x10F7558 ||
        read<std::uint8_t>(physical, 0x48))
        return {};
    const Address flash = read<Address>(first, sizeof(Address)), bone = read<Address>(first, 2 * sizeof(Address));
    if (!flash || read<Address>(flash) != image_base + 0x10F7558 ||
        !SmokeTagEquals(flash + 0x10, "flash_grenade_launcher") || !bone ||
        read<Address>(bone) != image_base + 0x10F8AE8)
        return {};
    // eAction_Spawn+0x50 is the exact MSVC string passed to the entity resolver.
    if (SmokeTagEquals(physical + 0x10, "nbk39")) return {physical, Payload::german, false};
    if (SmokeTagEquals(physical + 0x10, "no8_smoke")) return {physical, Payload::british, false};
    if (SmokeTagEquals(physical + 0x10, "51mm_eng_mortar_sm")) return {physical, Payload::mortar, false};
    return {}; // Includes the physically distinct 51mm_eng_mortar_wp.
}

inline void __fastcall replay_delay(Address action, Address context) {
    runtime::HookGuard guard(hook_gate);
    auto& tv = timer_vetoes();
    if (!serial_thread() || (!screen_event_owner && tv.empty()) || !context) {
        original_delay(action, context);
        return;
    }
    const Address owner = read<Address>(context);
    const bool marked = owner && owner == screen_event_owner;
    World w{};
    const bool live = world(image_base, w);
    // Re-scheduling the same stock non-uniq template replaces its native row.
    // Manual schedules always take precedence, including after cancellation.
    for (std::size_t i = 0; i < tv.size();) {
        const auto& v = tv[i];
        if ((v.owner == owner && v.action == action) || !live || v.scene != w.scene || w.now - v.at > w.duration(25.0f))
            remove_veto(i);
        else
            ++i;
    }
    if (marked) {
        const auto shot = read_shot(action);
        // A hazardous/unknown timer never reaches a host save. The independent
        // cap-restoration timer remains native even for a stale WP substitution.
        if (!shot.cap_only) {
            if (!shot.physical || !live) return;
            if (shot.payload != marked_control.admitted || shot.payload != stockPayload(owner)) {
                if (tv.size() >= 8192 || read<std::uint8_t>(action, 0xA8) || read<std::uint8_t>(action, 0xA9)) return;
                tv.push_back({owner, action, shot.physical, read<Address>(owner, 0x548), w.scene,
                              read<std::uint32_t>(owner, 0xC0), w.now, read<std::uint16_t>(owner, 0xDAA)});
            }
        }
    }
    original_delay(action, context);
}

inline void __fastcall replay_delay_due(Address action, Address context) {
    runtime::HookGuard guard(hook_gate);
    auto& tv = timer_vetoes();
    if (!serial_thread() || tv.empty() || !context) {
        original_delay_due(action, context);
        return;
    }
    const Address owner = read<Address>(context);
    if (!owner) {
        original_delay_due(action, context);
        return;
    }
    for (std::size_t i = 0; i < tv.size(); ++i) {
        const auto v = tv[i];
        if (v.owner != owner || v.action != action) continue;
        remove_veto(i);
        World w{};
        if (!world(image_base, w) || w.scene != v.scene || w.now - v.at > w.duration(25.0f) ||
            read<Address>(owner, 0x548) != v.definition || read<std::uint32_t>(owner, 0xC0) != v.entity ||
            read<std::uint16_t>(owner, 0xDAA) != v.mid)
            break;
        const auto previous_owner = due_owner, previous_projectile = due_projectile;
        due_owner = owner;
        due_projectile = v.projectile;
        original_delay_due(action, context); // native projectile, flash, and bone dispatch order
        due_owner = previous_owner;
        due_projectile = previous_projectile;
        return;
    }
    original_delay_due(action, context);
}

inline void __fastcall replay_spawn(Address action, Address context) {
    runtime::HookGuard guard(hook_gate);
    if (serial_thread() && due_projectile && action == due_projectile && context &&
        read<Address>(context) == due_owner) {
        due_projectile = 0;
        return;
    }
    original_spawn(action, context);
}

} // namespace vehicle

// The main-thread decision registers a game-owned order before transferring it.
// Destructors can retire keys during teardown. Never hold this lock across a
// game callback, and only dereference an actor obtained from a live receiver.
using SmokeExec = std::uint32_t (*)(Address, std::uint32_t);
using SmokeDestroy = Address (*)(Address, std::uint32_t);
using SmokeFiring = void (*)(Address, std::uint8_t);
inline SmokeExec original_smoke_fire_exec{}, original_smoke_shot_exec{};
inline SmokeDestroy original_smoke_shot_destroy{}, original_smoke_shooter_destroy{};
inline SmokeFiring original_smoke_firing{};
inline SmokeRelease original_smoke_release{};

struct SmokeIntent {
    Address root{}, child{}, shooter{}, actor{}, scene{}, definition{};
    Vec3 screen{};
    std::uint32_t entity{};
    std::uint64_t generation{};
    bool started{}, spent{}, emitted{}, ending{}, owns_latch{};
    std::uint64_t lease_ticket{};
};
inline store::GrowableTable<SmokeIntent, 64, 4096> smoke_intents{};
inline std::uint64_t next_smoke_generation{};
inline std::atomic<unsigned> active_intents{};
static_assert(std::atomic<unsigned>::is_always_lock_free);
inline SRWLOCK smoke_intent_lock = SRWLOCK_INIT;
struct SmokeIntentLock {
    SmokeIntentLock() noexcept {
        AcquireSRWLockExclusive(&smoke_intent_lock);
    }
    ~SmokeIntentLock() {
        ReleaseSRWLockExclusive(&smoke_intent_lock);
    }
    SmokeIntentLock(const SmokeIntentLock&) = delete;
    SmokeIntentLock& operator=(const SmokeIntentLock&) = delete;
};

inline SmokeIntent lookup_smoke_intent(Address SmokeIntent::* field, Address value) noexcept {
    if (value && active_intents.load(std::memory_order_acquire)) {
        SmokeIntentLock lock;
        for (const auto& entry : smoke_intents)
            if (entry.generation && entry.*field == value) return entry;
    }
    return {};
}
inline bool is_busy(const World& w, Address actor) noexcept {
    const auto id = read<std::uint32_t>(actor, 0xC0);
    const auto order = lookup_smoke_intent(&SmokeIntent::actor, actor);
    if (order.scene == w.scene && order.entity == id && order.generation) return true;
    if (cache_scene == w.scene)
        for (const auto& r : reservations)
            if (r.carrier == actor && r.entity == id && !r.released && w.now - r.start <= w.duration(8)) return true;
    return false;
}
template <class Edit> inline bool change_smoke_intent(std::uint64_t generation, Edit edit) noexcept {
    SmokeIntentLock lock;
    for (auto& entry : smoke_intents) {
        if (generation && entry.generation == generation) {
            edit(entry);
            if (!entry.generation) active_intents.fetch_sub(1, std::memory_order_release);
            return true;
        }
    }
    return false;
}
inline bool track_smoke_order(const World& w, Address actor, Address order, Address definition, Vec3 screen,
                              std::uint64_t lease_ticket) {
    if (!serial_thread() || !actor || !order || !definition || !finite(screen) ||
        read<Address>(order) != w.base + 0x110BF68)
        return false;
    if (!lease_ticket) return false;
    const auto id = read<std::uint32_t>(actor, 0xC0);
    SmokeIntentLock lock;
    SmokeIntent* free_slot = nullptr;
    for (auto& entry : smoke_intents) {
        if (entry.generation && entry.scene == w.scene && entry.actor == actor && entry.entity == id) return false;
        if (!entry.generation && !free_slot) free_slot = &entry;
    }
    if (!free_slot) free_slot = smoke_intents.append_slot();
    if (!free_slot) return false;
    if (!++next_smoke_generation) ++next_smoke_generation;
    *free_slot = {order, 0, 0, actor, w.scene, definition, screen, id, next_smoke_generation};
    free_slot->lease_ticket = lease_ticket;
    active_intents.fetch_add(1, std::memory_order_release);
    return true;
}
inline void forget_smoke_order(Address order) noexcept {
    if (!active_intents.load(std::memory_order_acquire)) return;
    SmokeIntentLock lock;
    for (auto& e : smoke_intents) {
        if (e.generation && e.root == order) {
            // A pending native reservation is identified by its still-live
            // shooter. It may survive order End when the selected weapon changes.
            if (e.shooter && e.owns_latch) {
                e.root = e.child = e.definition = 0;
                e.ending = true;
            } else {
                e = {};
                active_intents.fetch_sub(1, std::memory_order_release);
            }
        }
    }
}
inline Address smoke_shot_destroy(Address order, std::uint32_t flags) noexcept {
    runtime::HookGuard guard(hook_gate);
    forget_smoke_order(order);
    return original_smoke_shot_destroy(order, flags);
}
inline Address smoke_shooter_destroy(Address shooter, std::uint32_t flags) noexcept {
    runtime::HookGuard guard(hook_gate);
    if (grenades.shooter_destroyed) grenades.shooter_destroyed(shooter);
    if (active_intents.load(std::memory_order_acquire)) {
        SmokeIntentLock lock;
        for (auto& e : smoke_intents) {
            if (!e.generation || e.shooter != shooter) continue;
            if (!e.root) {
                e = {};
                active_intents.fetch_sub(1, std::memory_order_release);
                continue;
            }
            e.shooter = 0;
            e.owns_latch = false;
            e.spent = true;
        }
    }
    return original_smoke_shooter_destroy(shooter, flags);
}
inline Address bound_smoke_actor(Address order) noexcept {
    const Address owner = read<Address>(order, 8);
    return owner >= 0xAF0 ? owner - 0xAF0 : 0;
}
inline bool live_smoke_carrier(const SmokeIntent& e, Address actor, const World& w) noexcept {
    return actor && actor == e.actor && e.scene == w.scene && read<std::uint32_t>(actor, 0xC0) == e.entity;
}
inline Address active_smoke_shot(Address actor, Address wanted, Address direct_child) noexcept {
    const Address brain = read<Address>(actor, 0xDC8);
    if (!brain || read<Address>(brain, 0x38) != actor || read<Address>(brain) != image_base + 0x110FF88) return 0;
    Address order = read<Address>(brain, 0xF0);
    for (unsigned depth = 0; order && depth < 32; ++depth) {
        const Address table = read<Address>(order);
        if (!table || table == image_base + 0x110D278 || read<Address>(order, 8) != actor + 0xAF0) return 0;
        if (table == image_base + 0x110BF68 &&
            (order == wanted || (direct_child && read<Address>(order, 0x30) == direct_child)))
            return order;
        order = orders::active_child(image_base, order);
    }
    return 0;
}
inline bool current_smoke_order(const SmokeIntent& e, Address actor) noexcept {
    return e.root && active_smoke_shot(actor, e.root, 0) == e.root;
}
inline bool matching_smoke_target(Address weapon, Vec3 point) noexcept {
    if (!weapon || read<std::uint32_t>(weapon, 0xD0) != 2) return false;
    const auto actual = read<Vec3>(weapon, 0xD4);
    return actual.x == point.x && actual.y == point.y && actual.z == point.z;
}
inline bool smoke_may_continue(const SmokeIntent& e, Address actor, const World& w) noexcept {
    if (!serial_thread() || !live_smoke_carrier(e, actor, w)) return false;
    const Address brain = read<Address>(actor, 0xDC8);
    return brain && read<std::int32_t>(brain, 0x88) == 0 && read<Address>(brain, 0x98) == read<Address>(brain, 0xA0) &&
           !(read<std::uint32_t>(brain, 0x39C) & 0x1000003fu) && current_smoke_order(e, actor) && !e.ending &&
           !e.spent && valid_lease_time(actor, w.now, e.lease_ticket) &&
           eligible(w.base, read<Address>(actor, 0xDC8), actor);
}
inline std::uint32_t smoke_shot_exec(Address order, std::uint32_t phase) noexcept {
    runtime::HookGuard guard(hook_gate);
    const auto e = lookup_smoke_intent(&SmokeIntent::root, order);
    if (!e.generation) return original_smoke_shot_exec(order, phase);
    if (phase == 2) {
        change_smoke_intent(e.generation, [](auto& entry) { entry.ending = true; });
        return original_smoke_shot_exec(order, phase);
    }
    World w{};
    if (!serial_thread() || !emitting_host(image_base) || !world(image_base, w)) return 2;
    if (e.spent) return e.emitted ? 1u : 2u;
    if (!smoke_may_continue(e, bound_smoke_actor(order), w)) return 2;
    return original_smoke_shot_exec(order, phase);
}

inline std::uint32_t smoke_fire_exec(Address child, std::uint32_t phase) noexcept {
    runtime::HookGuard guard(hook_gate);
    if (grenades.fire_status) {
        int status = grenades.fire_status(child, phase);
        if (status >= 0) return static_cast<std::uint32_t>(status);
    }
    if (!active_intents.load(std::memory_order_acquire)) return original_smoke_fire_exec(child, phase);
    if (!serial_thread()) {
        if (lookup_smoke_intent(&SmokeIntent::child, child).generation) return phase == 2 ? 0u : 2u;
        return original_smoke_fire_exec(child, phase);
    }
    const Address actor = bound_smoke_actor(child);
    auto e = lookup_smoke_intent(&SmokeIntent::child, child);
    if (!e.generation && actor && lookup_smoke_intent(&SmokeIntent::actor, actor).generation) {
        const Address shot = active_smoke_shot(actor, 0, child);
        e = lookup_smoke_intent(&SmokeIntent::root, shot);
        if (e.generation && (e.actor != actor || read<Address>(shot, 0x30) != child)) e = {};
    }
    if (!e.generation) return original_smoke_fire_exec(child, phase);
    World w{};
    if (phase == 2) {
        // Native End is called before deleting/reusing the child. Keep it visible
        // during synchronous Firing(false), then retire its identity on all exits.
        struct RetireChild {
            const SmokeIntent& intent;
            Address child;
            ~RetireChild() {
                change_smoke_intent(intent.generation, [&](auto& entry) {
                    if (entry.child == child) entry.child = 0;
                    entry.ending = true;
                });
            }
        } retire{e, child};
        if (!e.started || e.actor != actor || !actor || !e.shooter || !emitting_host(image_base) ||
            !world(image_base, w) || !live_smoke_carrier(e, actor, w))
            return 0;
        // Native End operates on the current selected weapon. Allow its cleanup
        // only for our live point on our original weapon.
        const Address weaponry = read<Address>(actor, 0xDD8);
        if (!weaponry) return 0;
        using Select = Address (*)(Address);
        const Address weapon = reinterpret_cast<Select>(image_base + 0x81B080)(weaponry);
        if (!weapon || read<Address>(weapon, 0xB8) != e.shooter ||
            (read<std::uint32_t>(weapon, 0xD0) && !matching_smoke_target(weapon, e.screen)))
            return 0;
        change_smoke_intent(e.generation, [](auto& entry) { entry.ending = true; });
        return original_smoke_fire_exec(child, phase);
    }
    if (!emitting_host(image_base) || !world(image_base, w)) return 2;
    if (e.spent) return e.emitted ? 1u : 2u;
    if (!smoke_may_continue(e, actor, w)) return 2;
    const Address shooter = smoke_shooter_for_actor(image_base, actor, e.definition);
    if (phase != 0) {
        if (phase != 1 || !e.started || !shooter || shooter != e.shooter ||
            !matching_smoke_target(read<Address>(shooter, 8), e.screen))
            return 2;
        return original_smoke_fire_exec(child, phase);
    }
    if (!shooter || e.started || read<std::uint8_t>(shooter, 0x14) || read<std::uint8_t>(shooter, 0x15)) return 2;
    const Address weapon = read<Address>(shooter, 8);
    if (read<std::uint32_t>(weapon, 0xD0) && !matching_smoke_target(weapon, e.screen)) return 2;
    if (!change_smoke_intent(e.generation, [&](auto& entry) {
            entry.child = child;
            entry.shooter = shooter;
            entry.started = true;
        }))
        return 2;
    return original_smoke_fire_exec(child, phase);
}

struct SmokeFiringScope {
    std::uint64_t generation;
    Address shooter;
    bool permit;
};

inline void smoke_firing(Address shooter, std::uint8_t active) noexcept {
    runtime::HookGuard guard(hook_gate);
    if (grenades.new_firing) grenades.new_firing(shooter, active);
    auto e = lookup_smoke_intent(&SmokeIntent::shooter, shooter);
    if (!e.generation || !serial_thread() || !emitting_host(image_base)) {
        original_smoke_firing(shooter, active);
        return;
    }
    const Address weapon = read<Address>(shooter, 8);
    const Address actor = weapon ? read<Address>(weapon, 0x90) : 0;
    World w{};
    if (!world(image_base, w) || !live_smoke_carrier(e, actor, w)) {
        original_smoke_firing(shooter, active);
        return;
    }
    const bool target = current_smoke_order(e, actor) && e.child && read<Address>(e.root, 0x30) == e.child &&
                        matching_smoke_target(weapon, e.screen);
    const bool old_latch = e.owns_latch && read<std::uint8_t>(shooter, 0x15);
    if (!target && !old_latch) {
        if (e.owns_latch)
            change_smoke_intent(e.generation, [](auto& entry) {
                entry.owns_latch = false;
                if (!entry.root) entry = {};
            });
        original_smoke_firing(shooter, active);
        return;
    }
    const bool permit = target && smoke_may_continue(e, actor, w) &&
                        smoke_shooter_for_actor(image_base, actor, e.definition) == shooter;
    if (!permit && old_latch) {
        // 0x831DB0 removes only this shooter's tagged hand reservations. Remote
        // mode 0/1 carries +14 alone. Native Firing below owns its wire toggle.
        using ReserveHands = void (*)(Address, std::uint8_t);
        reinterpret_cast<ReserveHands>(image_base + 0x831DB0)(shooter, 0);
        change_smoke_intent(e.generation, [](auto& entry) {
            entry.owns_latch = false;
            if (!entry.root) entry = {};
        });
    }
    if (!target) {
        original_smoke_firing(shooter, active);
        return;
    }
    if (!permit && active) return;
    // A nonzero latch on entry that we did not set belongs to native work.
    if (!old_latch && read<std::uint8_t>(shooter, 0x15)) {
        original_smoke_firing(shooter, active);
        return;
    }
    SmokeFiringScope context{e.generation, shooter, permit};
    auto* frame = quant_frame();
    if (!frame) {
        if (!active && !read<std::uint8_t>(shooter, 0x15)) original_smoke_firing(shooter, 0);
        return;
    }
    auto* previous = frame->firing;
    frame->firing = &context;
    if (active && permit) change_smoke_intent(e.generation, [](auto& entry) { entry.owns_latch = true; });
    original_smoke_firing(shooter, active);
    frame->firing = previous;
    // A synchronous native event may have deleted the shooter.
    e = lookup_smoke_intent(&SmokeIntent::shooter, shooter);
    if (e.generation == context.generation) {
        const bool pending = read<std::uint8_t>(shooter, 0x15) != 0;
        change_smoke_intent(e.generation, [&](auto& entry) {
            entry.owns_latch = entry.owns_latch && pending;
            if (!entry.root && !entry.owns_latch) entry = {};
        });
    }
}
inline void release_smoke_intent(const SmokeIntent& e, Address shooter) noexcept {
    if (!e.generation || e.spent) return;
    World w{};
    const auto weapon = read<Address>(shooter, 8);
    const auto actor = weapon ? read<Address>(weapon, 0x90) : 0;
    if (!serial_thread() || !emitting_host(image_base) || !world(image_base, w) || !smoke_may_continue(e, actor, w) ||
        !e.child || read<Address>(e.root, 0x30) != e.child || !matching_smoke_target(weapon, e.screen) ||
        !still_screens(w, actor, e.screen, e.lease_ticket))
        return;
    if (!change_smoke_intent(e.generation, [](auto& entry) { entry.spent = true; })) return;
    const bool emitted =
        scoped_smoke_release(image_base, original_smoke_release, shooter, e.definition, e.screen, true);
    if (!emitted) return;
    change_smoke_intent(e.generation, [](auto& entry) { entry.emitted = true; });
    const auto live = lookup_smoke_intent(&SmokeIntent::shooter, shooter);
    if (live.generation == e.generation && read<Address>(shooter, 8) == weapon && read<Address>(weapon, 0x90) == actor)
        receipt(actor, w.now, e.lease_ticket);
}
inline void smoke_release(Address shooter) noexcept {
    runtime::HookGuard guard(hook_gate);
    const auto* frame = quant_frame();
    const auto* context = frame ? frame->firing : nullptr;
    if (!context || context->shooter != shooter) {
        const auto owner = lookup_smoke_intent(&SmokeIntent::shooter, shooter);
        if (owner.generation) {
            if (owner.owns_latch) {
                // The animation may complete after the initiating Firing call.
                // Only the same live child and actor quant can release its latch.
                if (serial_thread()) release_smoke_intent(owner, shooter);
                return;
            }
            if (owner.started && !serial_thread()) return;
            if (serial_thread() && emitting_host(image_base)) {
                World w{};
                const auto weapon = read<Address>(shooter, 8);
                const auto actor = weapon ? read<Address>(weapon, 0x90) : 0;
                if (world(image_base, w) && live_smoke_carrier(owner, actor, w) && current_smoke_order(owner, actor) &&
                    owner.child && read<Address>(owner.root, 0x30) == owner.child &&
                    matching_smoke_target(weapon, owner.screen))
                    return;
            }
        }
        // A different native launch may reenter from an animation notification.
        // Keep its wire and local spawn independent of the outer smoke sample.
        auto* vanilla_frame = quant_frame();
        auto* outer_release = vanilla_frame ? vanilla_frame->release : nullptr;
        auto* outer_global = active_release_scope;
        if (vanilla_frame) vanilla_frame->release = nullptr;
        active_release_scope = nullptr;
        if (grenades.release)
            grenades.release(original_smoke_release, shooter);
        else
            original_smoke_release(shooter);
        active_release_scope = outer_global;
        if (vanilla_frame) vanilla_frame->release = outer_release;
        return;
    }
    const auto e = lookup_smoke_intent(&SmokeIntent::shooter, shooter);
    if (context->permit && e.generation == context->generation) release_smoke_intent(e, shooter);
}

inline void decide(Address brain) {
    if (!config::active().modules.auto_smoke) return;
    const Address base = image_base;
    const Address actor = read<Address>(brain, 0x38);
    if (!eligible(base, brain, actor)) return;
    World w{};
    if (!world(base, w)) return;
    reset_scene_cache(w);
    const Vec3 self = read<Vec3>(actor, 0x74);
    if (!finite(self)) return;
    if (read<Address>(brain) == base + 0x101FD68) {
        const auto observation = vehicle::inspect(base, actor, w.now);
        const bool hit = heavy_hit(w, actor, observation.hullHealth, observation.penetratingHit);
        if (!observation.eligible() || !(hit || observation.reactiveDamage) || !vehicle::available_submission(w))
            return;
        auto* slot = available(w, actor, 0, self, true);
        const auto thr = primary_threat(w, actor, true, false);
        if (!slot || !thr.rank) return;
        reserve(*slot, w, actor, 0, self, true); // Reentrant native events see the reservation.
        ++stat_veh_smoke;
        if (log_sink) {
            log_sink("AUTO-SMOKE VEH #%u: actor=0x%llx pos=(%.1f,%.1f) dmg=%d hit=%d threatRank=%d", stat_veh_smoke,
                     (unsigned long long)actor, self.x, self.y, observation.reactiveDamage ? 1 : 0, hit ? 1 : 0,
                     thr.rank);
        }
        vehicle::deploy(base, observation);
        return;
    }

    const Address squad = read<Address>(brain, 0x100);
    if (squad && read<Address>(squad) != base + 0x110F3A0) return;
    if (grenades.is_busy && grenades.is_busy(w, actor)) return;
    if (!coord::instance().can_act(actor, coord::Module::auto_smoke, coord::Priority::auto_smoke,
                                   read<std::uint32_t>(actor, 0xC0), w.now))
        return;
    auto* slot = available(w, actor, squad, self, false);
    if (!slot) return;
    const auto path = orders::inspect(base, brain);
    if (!path.allowed) return;
    const Address item = FindCarriedPureSmokeItem(base, actor);
    if (!item) return;
    const Address definition = read<Address>(item, 0x40);
    const float range = nominal_throw_range(base, definition);
    if (!(range > 0)) return;
    const auto need = situation(w, actor, squad, path.healing, path.patient);
    if (!need.threat.rank) return;
    Vec3 screen{};
    if (!screen_point(w, self, need.anchor, need.threat.position, range, screen)) return;

    reserve(*slot, w, actor, squad, self, false);
    slot->protected_actor = need.protected_actor;
    slot->protected_id = read<std::uint32_t>(need.protected_actor, 0xC0);
    slot->known_threat = need.threat.position;
    slot->medical = need.medical;
    const auto ticket = slot->ticket;
    const Address shot = make_smoke_order(base, definition, screen);
    if (shot && slot->ticket == ticket && eligible(base, brain, actor) &&
        track_smoke_order(w, actor, shot, definition, screen, ticket) && orders::install(base, brain, path, shot)) {
        if (need.medical) {
            ++stat_med_smoke;
            if (log_sink) {
                log_sink("AUTO-SMOKE MED #%u: actor=0x%llx screen=(%.1f,%.1f) threat=(%.1f,%.1f) rank=%d",
                         stat_med_smoke, (unsigned long long)actor, screen.x, screen.y, need.threat.position.x,
                         need.threat.position.y, need.threat.rank);
            }
        } else {
            ++stat_inf_smoke;
            if (log_sink) {
                log_sink("AUTO-SMOKE INF #%u: actor=0x%llx squad=0x%llx screen=(%.1f,%.1f) threat=(%.1f,%.1f) rank=%d",
                         stat_inf_smoke, (unsigned long long)actor, (unsigned long long)squad, screen.x, screen.y,
                         need.threat.position.x, need.threat.position.y, need.threat.rank);
            }
        }
        return;
    }
    if (slot->ticket == ticket) *slot = {};
    if (!shot) return;
    forget_smoke_order(shot);
    // Native factory returns unique game ownership. An uninstalled child goes
    // through its real scalar deleting destructor, RCX=this, EDX=1.
    using Destroy = Address(__fastcall*)(Address, unsigned);
    reinterpret_cast<Destroy>(read<Address>(read<Address>(shot)))(shot, 1);
}

// The sole direct game caller of 0x91BE00 is actor slot 23 at 0x804C57.
// Run after vanilla slot 35 so a due AvoidGrenade reaction wins this quant.
inline void __fastcall smoke_decision(Address brain) {
    runtime::HookGuard guard(hook_gate);
    const auto base = image_base;
    if (!base || !brain || !hook_gate.accepting.load(std::memory_order_acquire) || !emitting_host(base)) {
        original_decision(brain);
        return;
    }
    const auto table = read<Address>(brain);
    const Address actor = read<Address>(brain, 0x38);
    const bool concrete = table == base + 0x110FF88 || table == base + 0x101FD68;
    original_decision(brain);
    const auto* frame = quant_frame();
    if (!concrete || !frame || frame->actor != actor || !has_actor(actor) || read<Address>(actor, 0xDC8) != brain ||
        !emitting_host(base))
        return;
    DWORD unset{};
    simulation_tid.compare_exchange_strong(unset, GetCurrentThreadId(), std::memory_order_relaxed);
    if (serial_thread()) decide(brain);
}

// Attribute only the verified stock slot-11 call sites. Other senders and
// receive-side spawns may run synchronously while a module release is pending.
inline void __fastcall smoke_send(Address command) {
    runtime::HookGuard guard(hook_gate);
    if (GOH_AUTO_SMOKE_RETURN_ADDRESS() == image_base + 0x831A9C) {
        if (!grenades.send_allowed || grenades.send_allowed(command)) smoke_send_adapter(original_smoke_send, command);
    } else {
        original_smoke_send(command);
    }
}
inline void __fastcall smoke_spawn(Address shooter, const Vec3* origin, const Vec3* velocity, const Vec3* spin) {
    runtime::HookGuard guard(hook_gate);
    if (GOH_AUTO_SMOKE_RETURN_ADDRESS() == image_base + 0x831AB3) {
        if (!grenades.spawn_allowed || grenades.spawn_allowed(shooter, origin, velocity, spin))
            smoke_spawn_adapter(original_smoke_spawn, shooter, origin, velocity, spin);
    } else {
        original_smoke_spawn(shooter, origin, velocity, spin);
    }
}
#undef GOH_AUTO_SMOKE_RETURN_ADDRESS

inline bool is_actor_busy_live(Address actor) noexcept {
    if (!image_base || !actor) return false;
    World w{};
    return world(image_base, w) && is_busy(w, actor);
}

inline void deactivate() noexcept {
    hook_gate.deactivate();
}

inline std::uint32_t inflight_count() noexcept {
    return hook_gate.count();
}

// Create disabled hooks in the host's existing MinHook instance. The caller
// supplies its independently measured on-disk hash and attestation that only
// the audited stock gameplay definitions are loaded. Batch-enable at the same
// point as the host's other reviewed hooks, before any mission starts.
template <class Create, class Remove>
inline bool prepare_hooks(Address base, const char* measured_sha256, bool stock_content, Create create, Remove remove) {
    if (image_base || !base || !stock_content || !measured_sha256 ||
        std::strcmp(measured_sha256, executable_sha256) != 0)
        return false;
    frame_index = TlsAlloc();
    if (frame_index == TLS_OUT_OF_INDEXES) return false;
    Address created[14]{};
    std::size_t count{};
    const auto add = [&](Address offset, auto detour, auto& original) {
        if (count == sizeof(created) / sizeof(*created)) return false;
        void* trampoline = nullptr;
        void* target = reinterpret_cast<void*>(base + offset);
        if (create(target, reinterpret_cast<void*>(detour), &trampoline) != 0) return false;
        original = reinterpret_cast<decltype(detour)>(trampoline);
        created[count++] = base + offset;
        return true;
    };
    if (!add(0x91BE00, &smoke_decision, original_decision) ||
        !add(0x903FE0, &smoke_shot_exec, original_smoke_shot_exec) ||
        !add(0x8ECBF0, &smoke_fire_exec, original_smoke_fire_exec) ||
        !add(0x906480, &smoke_shot_destroy, original_smoke_shot_destroy) ||
        !add(0x832550, &smoke_shooter_destroy, original_smoke_shooter_destroy) ||
        !add(0x831C40, &smoke_firing, original_smoke_firing) ||
        !add(0x8313B0, &smoke_release, original_smoke_release) || !add(0x151340, &smoke_send, original_smoke_send) ||
        !add(0x831FD0, &smoke_spawn, original_smoke_spawn) ||
        !add(0xB9A260, &vehicle::replay_control, vehicle::original_control) ||
        !add(0x781240, &vehicle::replay_event, vehicle::original_event) ||
        !add(0x765690, &vehicle::replay_delay, vehicle::original_delay) ||
        !add(0x7657D0, &vehicle::replay_delay_due, vehicle::original_delay_due) ||
        !add(0x765FB0, &vehicle::replay_spawn, vehicle::original_spawn)) {
        while (count)
            remove(reinterpret_cast<void*>(created[--count]));
        TlsFree(frame_index);
        frame_index = TLS_OUT_OF_INDEXES;
        return false;
    }
    image_base = base;
    hook_gate.activate();
    coord::instance().register_provider(coord::Module::auto_smoke, coord::Priority::auto_smoke, &is_actor_busy_live);
    return true;
}

// Call after disabling/removing the hooks and joining all callbacks at mission
// teardown. The engine must have finished destroying the tracked orders.
inline void after_hooks_removed() noexcept {
    hook_gate.deactivate();
    if (frame_index != TLS_OUT_OF_INDEXES) TlsFree(frame_index);
    frame_index = TLS_OUT_OF_INDEXES;
    image_base = 0;
    simulation_tid = 0;
    vehicle::clear_scene(0);
    vehicle::submissions.release();
    std::vector<vehicle::PureTimerVeto>{}.swap(vehicle::timer_vetoes());
    vehicle::marked_control = {};
    vehicle::screen_event_owner = 0;
    have_tick = false;
    reservations.release();
    hulls.release();
    smoke_intents.release();
    active_intents.store(0, std::memory_order_relaxed);
}

} // namespace goh::auto_smoke
