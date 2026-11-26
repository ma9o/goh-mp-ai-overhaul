#pragma once

// Steam 26734796, Windows x64. See research/notes/target-prioritization-26734796.md.
#include "config_and_log.hpp"
#include "engine_fixes_shared.hpp"
#include "engine_fixes_policy.hpp"
#include "tank_auto_repair_module.hpp"
#include "game_classifiers.hpp"
#include "game_runtime.hpp"
#include "penetration_at_policy.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <unordered_set>
#include <vector>

namespace goh::target_prio {

using Address = runtime::Address;
static_assert(sizeof(Address) == 8, "Target prioritization requires Windows x64");
inline constexpr const char* executable_sha256 = runtime::kBuild26734796Sha256;

inline std::atomic<unsigned> stat_memory_holds{};

namespace detail {

inline runtime::HookGate hook_gate{};
inline Address base{};
inline bool rollback_pending{};
using LogFn = void (*)(const char*, ...);
inline LogFn logger{};
// Decision callbacks run in the serial actor scope; teardown quiesces them first.
inline unsigned long long promotion_count{}, mg_save_count{};
inline bool sample_event(unsigned long long& count) {
    ++count;
    return count <= 25 || count % 100 == 0;
}
using runtime::read;
template <class T> inline T native(Address rva) noexcept {
    return reinterpret_cast<T>(base + rva);
}
using Byte = std::uint8_t;
using Unary = Address(__fastcall*)(Address);
using Predicate = Byte(__fastcall*)(Address);
using Role = Byte(__fastcall*)();

struct SubjectPtrVec {
    Address* begin;
    Address* end;
    Address* cap;
};
static_assert(sizeof(SubjectPtrVec) == 0x18);
using Rank = void(__fastcall*)(SubjectPtrVec*, Address);
using Picker = Address(__fastcall*)(Address);
using Cover = int(__fastcall*)(Address, Address, unsigned, int);
using CoverList = int(__fastcall*)(Address, SubjectPtrVec*, const char*);
using BrainUpdate = void(__fastcall*)(Address);
using Select = bool(__fastcall*)(Address, Address, std::int16_t, Address, int);
inline Rank orig_rank{};
inline Picker orig_picker{};
inline Cover orig_cover{};
inline CoverList orig_list{};
inline BrainUpdate orig_assign{}, orig_rebuild{};
inline Select orig_select{};

struct Vec3 {
    float x, y, z;
};
inline bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
inline Vec3 diff(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline float distance(Vec3 a, Vec3 b) {
    Vec3 v = diff(a, b);
    return std::sqrt(dot(v, v));
}
inline float alignment(Vec3 forward, Vec3 delta) {
    float length = std::sqrt(dot(forward, forward) * dot(delta, delta));
    if (!(length > 0) || !std::isfinite(length)) return 0;
    return std::clamp(dot(forward, delta) / length, -1.f, 1.f);
}

struct Span {
    Address first{}, end{};
    bool valid(std::size_t stride) const {
        return end >= first && (first || !end) && (end - first) % stride == 0;
    }
    std::size_t size(std::size_t stride) const {
        return (end - first) / stride;
    }
};
inline Span span(Address owner, std::size_t offset) {
    return owner ? read<Span>(owner, offset) : Span{};
}
inline Span subjects(Address actor) {
    return span(read<Address>(actor, 0xDD0), 0x280);
}
inline bool contains(Span list, Address value) {
    if (!value || !list.valid(8)) return false;
    for (Address p = list.first; p != list.end; p += 8)
        if (read<Address>(p) == value) return true;
    return false;
}
inline Span entries(const SubjectPtrVec& v) {
    return {reinterpret_cast<Address>(v.begin), reinterpret_cast<Address>(v.end)};
}
inline bool is_human_brain(Address brain) {
    return read<Address>(brain) == base + 0x110FF88;
}
inline bool is_visible(Address subject) {
    return subject && (read<unsigned>(subject, 0x10) & (1u << 29));
}
// Only subject observations may be inspected before this check.
inline bool hostile_observation(Address s) {
    if (!is_visible(s)) return false;
    unsigned f = read<unsigned>(s, 0x10), relation = read<unsigned>(s, 0x14);
    return (f & (1u << 28)) && !(f & (1u << 16)) && (relation & 0x1fu) == 3;
}

struct Ray {
    Address subject{}, target{}, place{};
    std::uint16_t weaponry_revision{};
    Vec3 from{}, to{};
    bool open{};
};
struct VehicleRank {
    Address brain{};
    std::vector<Address> native_order;
    std::vector<Address> ranked_order;
};
struct Assignment;
struct ActorScope {
    Address actor{};
    std::array<Ray, 4> rays{};
    unsigned used{}, rank_calls{};
    Address* picker_grenade{};
    Assignment* assignment{};
    VehicleRank* vehicle_rank{};
};
inline thread_local ActorScope scope{};
inline thread_local unsigned depth{};

inline constexpr std::size_t row_size = 0x1B8;
inline std::uint16_t weaponry_revision() {
    return read<std::uint16_t>(read<Address>(scope.actor, 0xDD8), 0x84);
}
inline bool current_rows(Address brain) {
    return read<std::uint16_t>(brain, 0x3D0) == weaponry_revision();
}
inline bool has_secondary_row(Address brain, Address reserved_weapon) {
    Span rows = span(brain, 0x3B8);
    if (!rows.valid(row_size)) return false;
    for (Address r = rows.first; r != rows.end; r += row_size) {
        Address w = read<Address>(r, 8);
        if (w && w != reserved_weapon) return true;
    }
    return false;
}
inline bool authorized_actor(Address actor, Address brain) {
    if (!actor || !brain || !runtime::actor_ai_allowed(actor)) return false;
    if (read<Address>(actor, 0xDC8) != brain || read<Address>(brain, 0x38) != actor) return false;
    Address vt = read<Address>(brain);
    if (vt != base + 0x110FF88 && vt != base + 0x101FD68) return false;
    unsigned flags = read<unsigned>(brain, 0x39C);
    unsigned fire = (read<unsigned>(brain, 0x398) >> 10) & 3u;
    if ((flags & 0x3Fu) || fire == 1 || (fire == 2 && !(flags & (1u << 18)))) return false;
    Address chassis = read<Address>(actor, 0xDE0), weaponry = read<Address>(actor, 0xDD8);
    if (!chassis || !weaponry || read<unsigned>(chassis, 0x18) || (read<Byte>(weaponry, 0x70) & 2)) return false;
    return true;
}
inline bool admitted(Address brain, bool allow_attack_volume = false) {
    if (!base || depth != 1 || !scope.actor || !brain) return false;
    // Active simulation authority: SP/Skirmish/Conquest AND MP host (always passive on workers & MP clients).
    if (!runtime::is_host_main_thread(base)) return false;
    Address actor = scope.actor;
    if (!authorized_actor(actor, brain)) return false;
    auto vt = read<Address>(brain), weaponry = read<Address>(actor, 0xDD8);
    Address order = read<Address>(brain, 0xF0);
    if (!allow_attack_volume && order && read<Address>(order) == base + 0x110C018) {
        if (vt == base + 0x110FF88) return false;
        // On multi-weapon vehicles, keep secondary turrets (e.g., gun1/gun2 AP turrets and MGs)
        // prioritized once Order::eAttack already holds its selected weapon in mode 3.
        Address atk_weapon = native<Unary>(0x81B080)(weaponry);
        if (!atk_weapon || !read<int>(atk_weapon, 0xD0) || read<int>(atk_weapon, 0x208) != 3 ||
            !has_secondary_row(brain, atk_weapon))
            return false;
    }
    return true;
}

inline bool explicit_attack(Address image, Address brain) {
    auto order = read<Address>(brain, 0xf0);
    for (unsigned i = 0; order && i < 32; ++i) {
        if (read<Address>(order) == image + runtime::table().vt.attack_order) return true;
        order = auto_smoke::orders::active_child(image, order);
    }
    return order != 0;
}
enum class TargetVeto {
    none,
    ally,
    towing
};
inline TargetVeto target_veto(Address image, Address actor, Address subject) {
    if (read<Byte>(actor, 0x30) & 8) {
        const auto cannon = ai_support::extender(actor, runtime::table().vt.cannon_ext, image);
        if (cannon && (read<int>(cannon, 0xC30) || (read<unsigned>(actor, 0x1C0) & (1u << 28))))
            return TargetVeto::towing;
    }
    const auto target = read<Address>(subject, 0x18);
    if (auto_smoke::has_actor(target) && auto_smoke::allies(image, ai_support::owner(actor), ai_support::owner(target)))
        return TargetVeto::ally;
    return TargetVeto::none;
}
inline bool autonomous_fire(Address image, Address brain) {
    return (read<unsigned>(brain, 0x398) & 3u) <= 1 && engine_fixes::firing_permitted(brain) &&
           !explicit_attack(image, brain);
}
inline Address selection_rva(Address rva) {
    return base + rva;
}
inline auto resolve_selection_call = &selection_rva;
template <class Fn> inline Fn selection_call(Address rva) {
    return reinterpret_cast<Fn>(resolve_selection_call(rva));
}
inline bool eligible_subject(Address brain, Address s) {
    if (!hostile_observation(s) || read<Byte>(s, 0x6C) || !read<Address>(s, 0x18)) return false;
    if (config::active().modules.allegiance_guard && depth == 1 && scope.actor && runtime::is_host_main_thread(base) &&
        authorized_actor(scope.actor, brain) && autonomous_fire(base, brain) &&
        target_veto(base, scope.actor, s) != TargetVeto::none)
        return false;
    if (is_human_brain(brain)) {
        using Eligible = int(__fastcall*)(Address, Address, Address);
        return selection_call<Eligible>(0x95C040)(brain, s, 0) != 0;
    }
    using Eligible = Byte(__fastcall*)(Address, Address);
    return selection_call<Eligible>(0x92B320)(brain, s) != 0;
}

inline bool tag_equals(Address tag, const char* name) {
    auto size = std::strlen(name);
    if (read<std::size_t>(tag, 0x50) != size) return false;
    auto cap = read<std::size_t>(tag, 0x58);
    Address text = cap > 15 ? read<Address>(tag, 0x40) : tag + 0x40;
    return cap >= size && text && std::memcmp(reinterpret_cast<const void*>(text), name, size) == 0;
}
inline bool has_tag(Address def, const char* name) {
    auto tags = span(def, 0x11E0);
    if (!tags.valid(0x60)) return false;
    for (Address p = tags.first; p != tags.end; p += 0x60)
        if (tag_equals(p, name)) return true;
    return false;
}
inline Address hand_item(Address actor) {
    Address holder = native<Unary>(0x7E04B0)(actor);
    return holder ? read<Unary>(read<Address>(holder), 0x18)(holder) : 0;
}
inline Address selected_place(Address actor) {
    Address w = read<Address>(actor, 0xDD8);
    return w ? native<Unary>(0x81B080)(w) : 0;
}
inline Address clean_bone(Address place) {
    Address aimer = place ? read<Address>(place, 0xA8) : 0;
    Address bone = aimer ? read<Address>(aimer, 0x20) : 0;
    return bone && !(read<Byte>(bone, 0x10) & 1) ? bone : 0;
}
inline float spatial_scale() {
    const float scale = read<float>(base + 0x149E750);
    return std::isfinite(scale) && scale > 0.f ? scale : 20.f;
}
enum class RayPoint {
    muzzle,
    target
};
// Character data here belongs to the owner or a currently visible subject.
inline Vec3 raised_point(Vec3 feet, Address actor, RayPoint point, float scale) {
    const bool muzzle = point == RayPoint::muzzle;
    float height = muzzle ? 1.85f : 1.f;
    if (read<Byte>(actor, 0x30) & 4) {
        Address chassis = read<Address>(actor, 0xDE0);
        const int stance = chassis ? read<int>(chassis, 0x2C) : 0;
        switch (stance) {
            case 1:
            case 3: height = .35f; break;
            case 2: height = muzzle ? .90f : .70f; break;
            default: height = muzzle ? 1.35f : 1.05f; break;
        }
    }
    feet.z += height * scale;
    return feet;
}
inline Vec3 muzzle(Address place, Address bone) {
    Address aimer = read<Address>(place, 0xA8);
    if (read<Address>(read<Address>(aimer), 0x30) == base + 0x8367D0) {
        auto point = read<std::array<double, 3>>(bone, 0x120);
        return {static_cast<float>(point[0]), static_cast<float>(point[1]), static_cast<float>(point[2])};
    }
    return read<Vec3>(bone, 0xCC);
}
inline Address active_entity(Address place) {
    return place && read<int>(place, 0xD0) == 1 ? read<Address>(place, 0xE0) : 0;
}
inline bool enemy_aims_here(Address s, Address self) {
    if (!hostile_observation(s)) return false;
    Address enemy = read<Address>(s, 0x18);
    if (!enemy) return false;
    Span places = span(read<Address>(enemy, 0xDD8), 0x30);
    if (!places.valid(8)) return false;
    for (Address p = places.first; p != places.end; p += 8)
        if (active_entity(read<Address>(p)) == self) return true;
    return false;
}
inline bool enemy_aims_at_ally(Address s, Address self) {
    if (!base || !hostile_observation(s)) return false;
    Address enemy = read<Address>(s, 0x18);
    if (!enemy) return false;
    const unsigned self_owner = read<Byte>(self, 0xDA8);
    if (self_owner >= 18) return false;
    const int stride = read<int>(base + 0x15C9710);
    const Address table = read<Address>(base + 0x15C9700);
    if (!table || stride <= static_cast<int>(self_owner) || stride > 256) return false;
    Span places = span(read<Address>(enemy, 0xDD8), 0x30);
    if (!places.valid(8)) return false;
    for (Address p = places.first; p != places.end; p += 8) {
        Address victim = active_entity(read<Address>(p));
        if (!victim || victim == self || !(read<Byte>(victim, 0x30) & 2) || (read<unsigned>(victim, 0x38) & 0xA20u))
            continue;
        const unsigned victim_owner = read<Byte>(victim, 0xDA8);
        if (victim_owner >= 18 || static_cast<int>(victim_owner) >= stride) continue;
        if (victim_owner == self_owner ||
            read<int>(table, (victim_owner * static_cast<unsigned>(stride) + self_owner) * sizeof(int)) == 2)
            return true;
    }
    return false;
}

struct SceneHit {
    std::uint32_t kind;
    float distance;
    Vec3 position;
    std::uint32_t padding;
    Address entity, volume;
    Vec3 normal, direction;
    std::int32_t face;
    std::uint32_t reserved;
};
static_assert(sizeof(SceneHit) == 0x48 && offsetof(SceneHit, volume) == 0x20);
struct HitVec {
    SceneHit* begin;
    SceneHit* end;
    SceneHit* cap;
};

inline void free_buffer(Address p, Address cap, std::size_t stride) {
    if (!p || cap < p) return;
    const auto bytes = (cap - p) / stride * stride;
    if (bytes >= 0x1000) {
        Address raw = read<Address>(p - sizeof(Address));
        if (p - raw - sizeof(Address) > 0x1f) return;
        p = raw;
    }
    using Free = void(__fastcall*)(void*);
    native<Free>(0xDA1E38)(reinterpret_cast<void*>(p));
}
inline bool soft_or_human_hit(const SceneHit& h) {
    if (h.entity && (read<Byte>(h.entity, 0x30) & 4)) return true;
    Address vdef = h.volume ? read<Address>(h.volume, 0x30) : 0;
    Address mat = vdef ? read<Address>(vdef, 0x408) : 0;
    if (!mat) return false;
    const float steel = read<float>(mat, 0xE0);
    return std::isfinite(steel) && steel > 0.f && steel <= .06f;
}
inline bool clear_segment(const Ray& ray, const HitVec& hits, float scale, bool* evaluated = nullptr,
                          bool pass_soft_or_human = false) {
    if (evaluated) *evaluated = true;
    const float length = distance(ray.from, ray.to);
    const float stop_dist = (std::max)(0.f, length - .35f * scale);
    for (auto h = hits.begin; h != hits.end; ++h) {
        if (!std::isfinite(h->distance)) {
            if (evaluated) *evaluated = false;
            return false;
        }
        if (h->distance >= length) break;
        if (h->kind == 1 || h->kind == 2) {
            if (h->distance < stop_dist) return false;
            continue; // A nearby foreign volume can still block before the endpoint.
        }
        if (h->volume && !(read<Byte>(h->volume, 0x28) & 0x20) && (read<unsigned>(h->volume, 0x0C) & 2)) {
            if (h->entity == ray.target) return true;
            if (pass_soft_or_human && soft_or_human_hit(*h)) continue;
            return false;
        }
    }
    return true;
}
template <class Visit> inline bool visit_segment(const Ray& ray, Visit visit) {
    using Alloc = void*(__fastcall*)(std::size_t);
    using Init = void*(__fastcall*)(void*, unsigned, Address, int);
    using Set = void(__fastcall*)(void*, const Vec3*, const Vec3*);
    using Run = Byte(__fastcall*)(void*, HitVec*);
    alignas(16) std::byte query[0x100]{};
    auto* buf = static_cast<SceneHit*>(native<Alloc>(0x373650)(16 * sizeof(SceneHit)));
    if (!buf) return false;
    HitVec hits{buf, buf, buf + 16};
    native<Init>(0xA409C0)(query, 0x86803, scope.actor, -1);
    native<Set>(0xA40A20)(query, &ray.from, &ray.to);
    native<Run>(0xA40AC0)(query, &hits);
    Address q = reinterpret_cast<Address>(query);
    free_buffer(read<Address>(q, 0x90), read<Address>(q, 0xA0), 8);
    const bool open = visit(hits);
    free_buffer(reinterpret_cast<Address>(hits.begin), reinterpret_cast<Address>(hits.cap), sizeof(SceneHit));
    return open;
}

inline bool segment(const Ray& ray, bool* evaluated = nullptr, bool pass_soft_or_human = false) {
    if (evaluated) *evaluated = false;
    return visit_segment(ray, [&](const HitVec& hits) {
        return clear_segment(ray, hits, spatial_scale(), evaluated, pass_soft_or_human);
    });
}

enum class Sight {
    unknown,
    blocked,
    open
};
inline const Ray* cached_ray(const Ray& key) {
    for (unsigned i = 0; i < scope.used; ++i) {
        const auto& r = scope.rays[i];
        if (r.subject == key.subject && r.target == key.target && r.place == key.place &&
            r.weaponry_revision == key.weaponry_revision && r.from.x == key.from.x && r.from.y == key.from.y &&
            r.from.z == key.from.z && r.to.x == key.to.x && r.to.y == key.to.y && r.to.z == key.to.z)
            return &r;
    }
    return nullptr;
}
inline Sight probe(Address s, Address place, Vec3 from, Vec3 to, bool pass_soft_or_human = false) {
    if (!is_visible(s) || !finite(from) || !finite(to) || !(distance(from, to) > 0)) return Sight::unknown;
    const Ray key{s, read<Address>(s, 0x18), place, weaponry_revision(), from, to};
    if (const Ray* hit = cached_ray(key)) return hit->open ? Sight::open : Sight::blocked;
    if (scope.used == scope.rays.size()) return Sight::unknown;
    auto& ray = scope.rays[scope.used++];
    ray = key;
    ray.open = segment(ray, nullptr, pass_soft_or_human);
    return ray.open ? Sight::open : Sight::blocked;
}

struct Pair {
    Address place, ammo;
    float min_range, max_range;
    bool held;
    float approach_range{};
    Address item{}; // Borrowed inventory instance for a carried throwable.
};
using GrenadePriority = bool (*)(Address, Address, const Pair&);
inline GrenadePriority grenade_priority{};
struct Loadout {
    std::vector<Pair> pairs;
    Address sight_place{};
};
inline Address matching_rule(Address place, Address ammo, Address subject, bool held) {
    if (!ammo || !is_visible(subject)) return 0;
    alignas(16) std::byte ctx[0x20]{};
    using Init = void*(__fastcall*)(void*, Address, Address, Address, Address, Byte);
    using Match = Address(__fastcall*)(void*);
    return native<Match>(0x92C9C0)(native<Init>(0x92C8E0)(ctx, place, ammo, scope.actor, subject, held));
}
inline int rule_priority(Address place, Address ammo, Address subject, bool held) {
    auto rule = matching_rule(place, ammo, subject, held);
    return rule ? read<int>(rule, 0x390) : -1;
}
inline void add_pair(Loadout& kit, Pair pair) {
    if (!pair.ammo || !std::isfinite(pair.min_range) || !std::isfinite(pair.max_range) || pair.min_range < 0 ||
        pair.max_range <= pair.min_range)
        return;
    for (auto& p : kit.pairs) {
        if (p.place == pair.place && p.ammo == pair.ammo && p.min_range == pair.min_range &&
            p.max_range == pair.max_range && p.approach_range == pair.approach_range) {
            p.held |= pair.held;
            return;
        }
    }
    kit.pairs.push_back(pair);
}

inline bool usable_item(Address actor, Address def) {
    Address breed = read<Address>(actor, 0x1390);
    Span abilities = span(breed, 0x548);
    if (!abilities.valid(8)) return false;
    // The stock evaluator's read of the matching breed ability (0x970990).
    for (Address p = abilities.first; p != abilities.end; p += 8) {
        Address ability = read<Address>(p);
        Address selector = ability ? read<Address>(ability, 8) : 0;
        if (!selector) continue;
        Address match = read<Address>(read<Address>(selector), 0x30) - base;
        if (match == 0x1B51D0) continue; // base eAbility::Matches always returns false
        Address query;
        if (match == 0x998530 || match == 0x9988C0)
            query = selector + 0x68;
        else if (match == 0x999020)
            query = selector + 0x88;
        else
            return false;
        if (!native<Byte (*)(Address, Address)>(0x565A50)(def + 0xEC8, query)) continue;
        Address getter = read<Address>(read<Address>(ability), 0x28) - base;
        if (getter == 0x1B5150) continue; // no skill-level pair
        if (getter != 0x999A70) return false;
        // Stock weapon skill writes the level count to out[1] and clamps
        // out[0] to that count. A positive count passes 0x970990; an empty
        // vector yields out[0] <= 0. Neither case needs the ability event.
        Span levels = span(selector, 0x68);
        return levels.valid(0x100) && levels.size(0x100) <= INT32_MAX;
    }
    return true;
}
inline void human_item(Loadout& kit, Address item, Address hand, Span items) {
    if (!item || read<int>(item, 0x48) <= 0) return;
    Address def = read<Address>(item, 0x40);
    if (!def) return;
    bool weapon = has_tag(def, "weapon"), missile = has_tag(def, "missile");
    if ((!weapon && !missile) || !usable_item(scope.actor, def)) return;
    if (!weapon) {
        Address throwing = read<Address>(def, 0x7D0);
        if (throwing) add_pair(kit, {0, def, 0, read<float>(throwing, 0x10), item == hand, 0, item});
        return;
    }
    Address wdef = native<Unary>(0x83F1E0)(def);
    if (!wdef || !read<Address>(wdef, 0x1600)) return;
    // Live loaded rounds live at eItem+0xDC (stock 0x7DC450); eItem+0x58 is the fill capacity.
    int fills = read<int>(item, 0xDC);
    if ((read<Byte>(wdef, 0x15F8) & 1) && fills <= 0) return;
    using Range = float(__fastcall*)(Address, Address);
    using Profile = Address(__fastcall*)(Address, Address);
    auto add = [&](Address ammo) {
        if (ammo && native<Profile>(0x83DE90)(read<Address>(wdef, 0x1600), ammo + 0xEC8))
            add_pair(kit, {0, ammo, 0, native<Range>(0x83F210)(wdef, ammo), item == hand});
    };
    if (fills > 0) add(read<Address>(item, 0x50));
    Span required = span(def + 0x120, 0x318);
    if (required.first == required.end || !required.valid(0x60)) return;
    using Match = Byte(__fastcall*)(Address, Address);
    for (Address p = items.first; p != items.end; p += 8) {
        Address stack = read<Address>(p);
        if (!stack || read<int>(stack, 0x48) <= 0) continue;
        Address ammo = read<Address>(stack, 0x40);
        if (ammo && native<Match>(0x565A50)(ammo + 0xEC8, def + 0x120)) add(ammo);
    }
}

inline bool available_ammo(Address w, Address record) {
    Address charger = read<Address>(w, 0xB0), ammo = read<Address>(record);
    if (!charger || !ammo || native<Predicate>(0x820CE0)(charger)) return false;
    Address charged = read<Unary>(read<Address>(charger), 8)(charger);
    if (charged && read<Address>(charged, 0x50) == ammo && read<int>(charged, 0xDC) > 0) return true;
    Address stack = native<Unary>(0x7E1F40)(record + 8);
    return stack && read<Address>(stack, 0x40) == ammo && read<int>(stack, 0x48) > 0;
}
struct Ammo {
    Address def{}, record{}, stack{};
};
template <class Match, class Visit>
inline bool overflow_ammo(Span stacks, const std::array<Address, 4>& present, Match match, Visit visit) {
    if (!stacks.valid(8) || stacks.size(8) > 4096) return false;
    for (auto i = stacks.first; i != stacks.end; i += 8) {
        auto item = read<Address>(i);
        if (!item || read<int>(item, 0x48) <= 0) continue;
        auto ammo = read<Address>(item, 0x40);
        if (ammo && std::find(present.begin(), present.end(), ammo) == present.end() && match(ammo))
            visit(Ammo{ammo, 0, item});
    }
    return true;
}
template <class Visit> inline bool ammunition(Address row, Address w, bool loaded_only, Visit visit) {
    const auto charger = read<Address>(w, 0xb0);
    if (!charger || native<Predicate>(0x820CE0)(charger)) return false;
    auto loaded = native<Unary>(0x820B50)(charger);
    std::array<Address, 4> defs{};
    for (unsigned i = 0; i < defs.size(); ++i) {
        auto record = row + 0x98 + i * 0x48, ammo = read<Address>(record);
        if (!ammo) return true;
        if ((!loaded_only || loaded == ammo) && available_ammo(w, record)) {
            defs[i] = ammo;
            visit(Ammo{ammo, record + 8, 0});
        }
    }
    if (loaded_only || ((read<Byte>(charger, 0x15) & 1) && !native<Predicate>(0x820C50)(charger))) return true;
    auto def = read<Address>(w, 0x98);
    if (!def || !read<Address>(scope.actor, 0x578)) return false;
    auto matches = span(def + 0x120, 0x318);
    if (!matches.valid(0x60) || matches.first == matches.end) return false;
    const auto box = native<Unary>(0x7E0420)(scope.actor);
    return box &&
           overflow_ammo(
               span(box, 0x10), defs,
               [&](Address ammo) { return native<Byte (*)(Address, Address)>(0x565A50)(ammo + 0xec8, def + 0x120); },
               visit);
}
inline bool reserved_for_other(Address w, std::int16_t priority) {
    if (!read<int>(w, 0xD0)) return false;
    int mode = read<int>(w, 0x208);
    return mode == 1 || mode == 3 || read<std::int16_t>(w, 0x214) < priority;
}
// The first assignment pass measures approach from the actor, using +0x20
// of the currently charged profile. The selector also has muzzle-relative
// bounds (+0x28/+0x24). A promotion must satisfy both.
inline float approach_range(Address w, Address profile) {
    if (!profile || !native<Unary>(0x81EC40)(w)) return 0;
    using Range = float(__fastcall*)(Address);
    float current = native<Range>(0x81F660)(w), proposed = read<float>(profile, 0x20);
    if (!std::isfinite(current) || !std::isfinite(proposed)) return 0;
    return (std::min)(current, proposed);
}
inline void vehicle_pairs(Loadout& kit, Address brain) {
    if (!current_rows(brain)) return;
    Span rows = span(brain, 0x3B8);
    if (!rows.valid(row_size)) return;
    using Priority = std::int16_t(__fastcall*)(Address);
    auto priority = native<Priority>(0x81A8D0)(read<Address>(scope.actor, 0xDD8) + 0x10);
    bool loaded_only = native<Predicate>(0x91B190)(brain) != 0;
    for (Address row = rows.first; row != rows.end; row += row_size) {
        Address w = read<Address>(row, 8);
        if (!w || !read<Address>(w, 0x98) || !read<Address>(w, 0xA0) || reserved_for_other(w, priority)) continue;
        Address gunner = read<Address>(w, 0xC0);
        if (read<Byte>(brain, 0x420) &&
            (!gunner || (native<Predicate>(0x825600)(gunner) && native<Predicate>(0x825730)(gunner))))
            continue;
        ammunition(row, w, loaded_only, [&](Ammo candidate) {
            auto ammo = candidate.def;
            using Profile = Address(__fastcall*)(Address, Address);
            Address profile = native<Profile>(0x83DE90)(read<Address>(w, 0xA0), ammo + 0xEC8);
            if (profile)
                add_pair(kit, {w, ammo, read<float>(profile, 0x28), read<float>(profile, 0x24), false,
                               approach_range(w, profile)});
        });
    }
}
inline Loadout loadout(Address brain) {
    Loadout kit;
    if (!is_human_brain(brain)) {
        vehicle_pairs(kit, brain);
        return kit;
    }
    kit.sight_place = selected_place(scope.actor);
    if (!read<Address>(scope.actor, 0x578)) return kit;
    Span items = span(native<Unary>(0x7E0420)(scope.actor), 0x10);
    if (!items.valid(8)) return kit;
    Address hand = hand_item(scope.actor);
    human_item(kit, hand, hand, items);
    for (Address p = items.first; p != items.end; p += 8)
        if (Address item = read<Address>(p); item != hand) human_item(kit, item, hand, items);
    return kit;
}

enum class RowKind {
    other,
    mg,
    main_gun
};
inline RowKind row_kind(Address weapon) {
    Address def = weapon ? read<Address>(weapon, 0x98) : 0;
    if (!def) return RowKind::other;
    if (has_tag(def, "mgun")) return RowKind::mg;
    float calibre = read<float>(def, 0x1628);
    if (std::isfinite(calibre) && calibre >= 37.f) {
        // The actual ammo records distinguish single-shot tank/AT shells from mortars and autocannons.
        return RowKind::main_gun;
    }
    return RowKind::other;
}
struct Row {
    Address row, weapon, incumbent;
    RowKind kind;
};
struct Coverage {
    Address subject, row;
};
struct Assignment {
    Address brain{};
    std::vector<Row> rows;
    std::vector<Coverage> covered;
    bool economy{}, multi_cannon{}, selecting{};
    Address proposed_weapon{}, proposed_target{}, proposed_ammo{};
};
inline void capture_rows(Assignment& a) {
    Span rows = span(a.brain, 0x3B8);
    if (!rows.valid(row_size)) return;
    a.rows.reserve(rows.size(row_size));
    bool has_mg = false;
    unsigned cannon_count = 0;
    for (Address r = rows.first; r != rows.end; r += row_size) {
        Address w = read<Address>(r, 8), incumbent = active_entity(w);
        if (!incumbent && read<Byte>(r, 1) && read<int>(r, 0x20) == 1) incumbent = read<Address>(r, 0x30);
        RowKind kind = row_kind(w);
        if (kind == RowKind::main_gun) {
            bool shell = false;
            for (unsigned i = 0; i < 4; ++i) {
                Address ammo = read<Address>(r, 0x98 + i * 0x48);
                if (!ammo) break;
                if (has_tag(ammo, "shell") && !has_tag(ammo, "auto") && !has_tag(ammo, "zenite")) shell = true;
            }
            if (!shell) kind = RowKind::other;
        }
        a.rows.push_back({r, w, incumbent, kind});
        has_mg |= kind == RowKind::mg;
        cannon_count += kind == RowKind::main_gun;
    }
    a.economy = has_mg && cannon_count > 0;
    a.multi_cannon = cannon_count >= 2;
}
inline float incumbent_bonus(Address brain, Address s, Address w, Address target) {
    if (is_human_brain(brain)) {
        return read<Address>(brain, 0x3B8) == s || active_entity(w) == target ? .20f : 0.f;
    }
    if (auto* a = scope.assignment; a && a->brain == brain) {
        for (const Row& r : a->rows)
            if (r.weapon == w && r.incumbent == target) return r.kind == RowKind::main_gun ? .30f : .20f;
    }
    return active_entity(w) == target ? .20f : 0.f;
}
struct Candidate {
    Address subject{}, place{};
    Vec3 origin{}, aim_point{};
    float score{-1}, class_bonus{};
    bool has_origin{}, grenade{}, armored{}, soft_pass{};
    Sight sight{Sight::unknown};
    std::size_t ordinal{};
    penetration_at::policy::Outcome armor{};
    bool armor_complete{};
};
inline bool tank_class(Address target) {
    return classify::is_battle_tank_class(classify::gem_string(target + 0xDE8 + 0x40, 64));
}
inline bool armoured_class(Address target) {
    const auto sv = classify::gem_string(target + 0xDE8 + 0x40, 64);
    return classify::is_battle_tank_class(sv) || sv == "armoredcar" || sv == "armoredcar_l";
}
inline penetration_at::policy::Infantry infantry_loadout(Address target) {
    penetration_at::policy::Infantry data;
    if (!(read<Byte>(target, 0x30) & 4) || !tag_equals(target + 0xde8, "human") || !read<Address>(target, 0x578))
        return data;
    const auto box = ai_support::box(target);
    const auto items = ai_support::span(box, 0x10);
    const auto holders = ai_support::span(ai_support::manager(target), 0x88);
    if (!box || !items.valid(8, 4096) || !holders.valid(8, 1024)) return data;
    auto classify = [&](Address item) {
        if (!item || read<int>(item, 0x48) <= 0) return;
        auto def = read<Address>(item, 0x40);
        if (!def) return;
        data.launcher |= has_tag(def, "weapon") && has_tag(def, "rocketlauncher");
        data.rifle |= has_tag(def, "weapon") && (has_tag(def, "at_rifle") || has_tag(def, "ptr"));
        data.grenade |= (has_tag(def, "missile") || has_tag(def, "grenade")) &&
                        (has_tag(def, "at") || has_tag(def, "ic") || has_tag(def, "dynamite"));
    };
    for (auto i = holders.first; i != holders.end; i += 8) {
        const auto holder = read<Address>(i);
        if (holder && read<Address>(holder) == base + runtime::table().vt.holder_hand_right &&
            read<Address>(holder, 0x3d0) == target)
            classify(read<Address>(holder, 0x3d8));
    }
    for (auto i = items.first; i != items.end; i += 8)
        classify(read<Address>(i));
    data.known = true;
    return data;
}
inline bool defanged_armor(Address target) {
    auto rows = ai_support::span(target, 0xb90);
    if (!rows.valid(0x28, 1024)) return false;
    bool primary = false;
    for (auto i = rows.first; i != rows.end; i += 0x28) {
        if (!read<Address>(i) || read<Address>(i, 8) != target) return false;
        auto name = tank_auto_repair::detail::component_name(read<Address>(i));
        if (!tank_auto_repair::detail::gun(name)) continue;
        if (!read<Byte>(i, 0x24)) return false;
        primary |= name == "gun";
    }
    return primary;
}
struct ArmorMemory {
    ai_support::Identity vehicle{}, threat_entity{};
    Vec3 threat_pos{};
    float bearing_rad{};
    runtime::Tick last_seen_tick{};
    bool casemate{};
    unsigned player{};
};
inline store::GrowableTable<ArmorMemory, 64, 4096> armor_memory{};
inline Address memory_scene{};
inline runtime::Tick memory_clock{};
inline thread_local const ArmorMemory* assignment_memory{};

inline bool operational_armor(const auto_smoke::World& w, Address actor, Address target) {
    return auto_smoke::has_actor(target) && (read<Byte>(target, 0x30) & 8) && (read<unsigned>(target, 0xdb4) & 0x100) &&
           ai_support::owner(target) < 18 &&
           !auto_smoke::allies(w.base, ai_support::owner(actor), ai_support::owner(target)) && armoured_class(target) &&
           !ai_support::has_destruction_tag(target) && !defanged_armor(target);
}
inline bool casemate_hull(Address actor) {
    const auto parts = ai_support::span(actor, 0xb90);
    if (!parts.valid(0x28, 1024)) return false;
    bool body = false, gun = false;
    for (auto it = parts.first; it != parts.end; it += 0x28) {
        if (read<Address>(it, 8) != actor) return false;
        const auto name = tank_auto_repair::detail::component_name(read<Address>(it));
        if (tank_auto_repair::detail::turret(name)) return false;
        body |= name == "body";
        gun |= name == "gun";
    }
    return body && gun;
}

// Returns the last sighting only when neither committed list contains operational visible armor.
// Validate a hidden entity through the scene before inspecting its identity or current allegiance.
inline const ArmorMemory* update_armor_memory(const auto_smoke::World& w, Address actor, Span candidates) {
    const auto scene = ai_support::entity_scene(w);
    if (memory_scene != scene || w.now < memory_clock) armor_memory.clear();
    memory_scene = scene;
    memory_clock = w.now;
    if (!scene || !auto_smoke::has_actor(actor) || !(read<Byte>(actor, 0x30) & 8) ||
        !(read<unsigned>(actor, 0xdb4) & 0x100) || ai_support::owner(actor) >= 18 || !armoured_class(actor))
        return nullptr;
    const auto ttl = w.duration(config::active().tuning.tank_memory_hold_s);
    ArmorMemory *row = nullptr, *vacant = nullptr;
    for (auto& entry : armor_memory) {
        if (entry.vehicle.ptr == actor) {
            if (entry.vehicle.matches(actor) && entry.player == ai_support::owner(actor) &&
                entry.vehicle.mid == read<std::uint16_t>(actor, 0xdaa) && w.now - entry.last_seen_tick <= ttl)
                row = &entry;
            else {
                entry = {};
                vacant = &entry;
            }
            break;
        }
        if (!vacant && (!entry.vehicle.ptr || w.now - entry.last_seen_tick > ttl)) vacant = &entry;
    }
    const auto self = read<Vec3>(actor, 0x74);
    if (!finite(self)) return nullptr;
    Address best{};
    Vec3 pos{};
    float best_score = -1.f;
    const auto sensor = read<Address>(actor, 0xdd0);
    const auto observations = sensor && read<Address>(sensor, 0x260) == actor ? span(sensor, 0x280) : Span{};
    for (const auto list : {candidates, observations}) {
        if (!list.valid(8) || list.size(8) > 4096) return nullptr;
        for (auto it = list.first; it != list.end; it += 8) {
            const auto sub = read<Address>(it);
            if (!hostile_observation(sub) || read<Byte>(sub, 0x6c)) continue;
            const auto target = read<Address>(sub, 0x18);
            if (!operational_armor(w, actor, target)) continue;
            const auto feet = read<Vec3>(sub, 0x28);
            if (!finite(feet) || (feet.x == self.x && feet.y == self.y)) continue;
            const auto attack = read<unsigned>(sub, 0x14);
            const float rate = read<float>(sub, 0x4c);
            const float score = (std::isfinite(rate) && rate > 0 ? rate / (1.f + rate) : 0.f) +
                                ((attack & 0x100)  ? 1.f
                                 : (attack & 0x80) ? .5f
                                                   : 0.f) +
                                (enemy_aims_here(sub, actor) ? 1.f : 0.f) +
                                1.f / (1.f + distance(self, feet) / (80.f * w.length));
            if (score > best_score) {
                best_score = score;
                best = target;
                pos = feet;
            }
        }
    }
    if (best) {
        if (!row) row = vacant;
        if (!row) {
            try {
                row = armor_memory.append_slot();
            } catch (const std::bad_alloc&) {
                return nullptr;
            }
        }
        if (row)
            *row = {ai_support::identity(actor),
                    ai_support::identity(best),
                    pos,
                    std::atan2(pos.y - self.y, pos.x - self.x),
                    w.now,
                    casemate_hull(actor),
                    ai_support::owner(actor)};
        return nullptr;
    }
    if (row) {
        const auto live = ai_support::resolve(w, row->threat_entity);
        if (!operational_armor(w, actor, live) || row->threat_entity.mid != read<std::uint16_t>(live, 0xdaa)) {
            *row = {};
            return nullptr;
        }
    }
    return row;
}
inline const ArmorMemory* remember_armor(Address brain, Span source) {
    auto_smoke::World w{};
    if (!config::active().modules.tank_target_memory || is_human_brain(brain) || !autonomous_fire(base, brain) ||
        !auto_smoke::world(base, w))
        return nullptr;
    return update_armor_memory(w, scope.actor, source);
}

inline bool cannon_class(Address target) {
    Address tag = target + 0xDE8;
    return tag_equals(tag, "cannon_big") || tag_equals(tag, "cannon_small") || tag_equals(tag, "cannon_zenite") ||
           tag_equals(tag, "howitzer");
}
inline bool light_vehicle_class(Address target) {
    Address tag = target + 0xDE8;
    return tag_equals(tag, "car") || tag_equals(tag, "cannon_bullet");
}
inline float vehicle_class_bonus(bool armored, bool cannon, bool light_vehicle, bool defanged, float dist_m) noexcept {
    const float close = std::isfinite(dist_m) ? std::clamp(1.f - dist_m / 60.f, 0.f, 1.f) : 0.f;
    if (armored) return defanged ? .15f : (.45f + .25f * close);
    if (cannon) return defanged ? .10f : (.35f + .15f * close);
    if (light_vehicle) return .12f;
    return 0.f;
}
inline bool anti_armour(const Pair& pair) {
    return has_tag(pair.ammo, "ptr") || has_tag(pair.ammo, "rocketlauncher") ||
           has_tag(pair.ammo, "rifle_grenade_at") || has_tag(pair.ammo, "at") || has_tag(pair.ammo, "ic") ||
           has_tag(pair.ammo, "dynamite") || has_tag(pair.ammo, "heat") || has_tag(pair.ammo, "heat_low") ||
           has_tag(pair.ammo, "heat_medium") || has_tag(pair.ammo, "heat_high");
}
struct ArmorRating {
    penetration_at::policy::Outcome outcome{};
    bool complete{};
    float danger{}, discipline{};
    bool defanged{};
};
using RateArmor = ArmorRating (*)(Address, Address, const Loadout&, float);
using SelectArmor = bool (*)(const Assignment&, Address, std::int16_t, Address);
inline RateArmor rate_armor{};
inline SelectArmor select_armor{};
inline Candidate evaluate(Address brain, Address subject, const Loadout& kit, Address preferred) {
    Candidate c;
    c.subject = subject;
    if (!eligible_subject(brain, subject)) return c;
    const Address enemy = read<Address>(subject, 0x18);
    if (!read<Address>(enemy, 0xDC0)) return c; // native class/cover rules dereference this
    const Vec3 feet = read<Vec3>(subject, 0x28);
    const Vec3 self = read<Vec3>(scope.actor, 0x74);
    if (!finite(self) || !finite(feet)) return c;
    const bool human = is_human_brain(brain);
    const float scale = spatial_scale();
    const float prox_scale = (human ? 35.f : 80.f) * scale;
    c.aim_point = raised_point(feet, enemy, RayPoint::target, scale);
    const bool needs_at = human && tank_class(enemy);
    const bool armored_enemy = armoured_class(enemy);
    float rate = read<float>(subject, human ? 0x48 : 0x4C);
    if (!human && armored_enemy) {
        const float anti_inf = read<float>(subject, 0x48);
        if (std::isfinite(anti_inf) && anti_inf > 0.f)
            rate = (std::max)(std::isfinite(rate) && rate > 0.f ? rate : 0.f, .5f * anti_inf);
    }
    rate = rate > 0 && std::isfinite(rate) ? rate / (1 + rate) : 0;
    unsigned flags = read<unsigned>(subject, 0x14);
    float urgency = (flags & 0x100) ? 1.f : ((flags & 0x80) ? .5f : 0.f);
    const float aim_threat = enemy_aims_here(subject, scope.actor)
                                 ? 1.f
                                 : ((!human && enemy_aims_at_ally(subject, scope.actor)) ? .8f : 0.f);
    float danger = .5f * rate + .3f * aim_threat + .2f * urgency;
    const float actor_distance = distance(self, feet);
    auto rating = rate_armor ? rate_armor(brain, subject, kit, danger) : ArmorRating{};
    if (rate_armor) danger = rating.danger;
    c.armor = rating.outcome;
    c.armor_complete = rating.complete;
    if (!human) {
        c.armored = armored_enemy;
        c.soft_pass = armored_enemy;
        c.class_bonus =
            vehicle_class_bonus(armored_enemy, !armored_enemy && cannon_class(enemy),
                                !armored_enemy && light_vehicle_class(enemy), rating.defanged, actor_distance / scale);
    }
    int best_priority = -1;
    for (const auto& p : kit.pairs) {
        if (human && p.item && grenade_priority && grenade_priority(brain, subject, p)) {
            c.grenade = true;
            c.score = 2.f + 1.f / (1.f + actor_distance / prox_scale);
            return c;
        }
        if ((p.place && !(actor_distance < p.approach_range)) ||
            (needs_at && !anti_armour(p) && !((rating.outcome.seen || rating.outcome.component) && p.held)))
            continue;
        int priority = rule_priority(p.place, p.ammo, subject, p.held);
        if (priority < 0) continue;
        Address place = p.place ? p.place : kit.sight_place;
        Address bone = clean_bone(place);
        Vec3 origin = bone ? muzzle(place, bone) : raised_point(self, scope.actor, RayPoint::muzzle, scale);
        Vec3 delta = diff(feet, origin);
        float d = distance(feet, origin);
        if (!std::isfinite(d) || d < p.min_range || !(d < p.max_range)) continue;
        Vec3 forward = bone ? read<Vec3>(bone, 0xA8) : read<Vec3>(scope.actor, 0x50);
        const float prox = 1.f / (1.f + d / prox_scale);
        float score = .40f * danger + .35f * prox + .125f * (1 + alignment(forward, delta));
        score = (std::max)(0.f, score + rating.discipline);
        score += incumbent_bonus(brain, subject, place, enemy);
        if (subject == preferred) score += .12f;
        if (score > c.score || (score == c.score && priority > best_priority)) {
            best_priority = priority;
            c.score = score;
            c.origin = origin;
            c.has_origin = finite(origin);
            c.place = place;
        }
    }
    return c;
}

// Bypassing the squad shortcut only needs the ordinary one-group infantry
// profiles. Admit their class-only ignore clauses directly, including inversion.
inline bool profile_accepts(Address group, Address subject) {
    if (!group || !is_visible(subject)) return false;
    Span ignores = span(group, 0x18);
    if (!ignores.valid(0x18)) return false;
    for (Address f = ignores.first; f != ignores.end; f += 0x18) {
        Span terms = span(f, 0);
        if (!terms.valid(0x3A8)) return false;
        bool match = true;
        for (Address term = terms.first; term != terms.end; term += 0x3A8) {
            if (read<int>(term) != 0) return false;
            using Equal = Byte(__fastcall*)(Address, Address);
            bool equal = native<Equal>(0x565340)(term + 0x10, read<Address>(subject, 0x18) + 0xDE8);
            match &= equal != bool(read<Byte>(term, 8));
        }
        if (match) return false;
    }
    return true;
}
inline Address human_group() {
    Address profile = native<Unary>(0x931E70)(scope.actor);
    auto groups = span(profile, 0x6C0);
    if (!groups.valid(8) || groups.size(8) != 1) return 0;
    Address g = read<Address>(groups.first);
    auto select = span(g, 0);
    return select.first == select.end ? g : 0;
}
struct Plan {
    std::vector<Candidate> order;
    bool open{}, should_rewrite{};
};
inline void rank_candidates(Plan& result) {
    auto& order = result.order;
    if (order.size() < 2) return;
    const bool alternative =
        std::any_of(order.begin(), order.end(), [](const Candidate& c) { return c.score >= 0 && c.armor.clean; });
    for (auto& c : order) {
        if (c.score >= 0 && !c.grenade) {
            const bool impenetrable = c.armor_complete && c.armor.seen && !c.armor.clean && c.armor.ratio < .85f;
            const float bonus = impenetrable ? 0.f : c.class_bonus;
            c.score = (std::max)(0.f, c.score + bonus +
                                          penetration_at::policy::killability(c.armor, c.armor_complete, alternative));
        }
    }
    const bool stock_effective = order.front().score >= 0;
    auto higher = [](const Candidate& a, const Candidate& b) {
        if (a.score == b.score || (a.score < 0 && b.score < 0)) return a.ordinal < b.ordinal;
        return a.score > b.score;
    };
    std::sort(order.begin(), order.end(), higher);
    // Multi-volume penetration checks already proved muzzle-to-armor exposure;
    // reuse that proof so coarse feet+1m rays or ray caps cannot falsely demote it.
    for (auto& c : order) {
        if (c.grenade || c.score < 0) continue;
        if (c.armor.clean || (c.armor.seen && c.armor.ratio >= .85f) || c.armor.component) {
            c.sight = Sight::open;
            result.open = true;
        }
    }
    // Reserve up to two ray slots for top unprobed armored targets so riflemen
    // cannot crowd armored vehicles out of the four-ray cap.
    std::size_t attempts = 0, armored_attempts = 0;
    for (auto& c : order) {
        if (armored_attempts >= 2 || attempts >= scope.rays.size()) break;
        if (c.grenade || c.score < 0 || !c.has_origin || !c.armored || c.sight != Sight::unknown) continue;
        c.sight = probe(c.subject, c.place, c.origin, c.aim_point, c.soft_pass);
        result.open |= c.sight == Sight::open;
        ++armored_attempts;
        ++attempts;
    }
    for (auto& c : order) {
        if (attempts >= scope.rays.size()) break;
        if (c.grenade || c.score < 0 || !c.has_origin || c.sight != Sight::unknown) continue;
        c.sight = probe(c.subject, c.place, c.origin, c.aim_point, c.soft_pass);
        result.open |= c.sight == Sight::open;
        ++attempts;
    }
    result.should_rewrite = order.front().grenade || result.open || (!stock_effective && order.front().score >= 0);
    if (result.open) {
        std::sort(order.begin(), order.end(), [&](const Candidate& a, const Candidate& b) {
            if (a.grenade != b.grenade) return a.grenade;
            const bool a_open = a.sight == Sight::open, b_open = b.sight == Sight::open;
            return a_open != b_open ? a_open : higher(a, b);
        });
    }
}
struct Domain {
    bool sensor_scan{};
    Address cover{}; // cover geometry subobject, only for CoverBattleFallback
    unsigned sector{};
    Address original{};
};
inline Plan plan(Address brain, Span source, Address preferred, Domain domain = {}) {
    Plan result;
    if (!source.valid(8)) return result;
    try {
        Address group = domain.sensor_scan ? human_group() : 0;
        if (domain.sensor_scan && !group) return result;
        float radius = read<float>(brain, 0x14C);
        bool cutoff = false;
        Vec3 self = read<Vec3>(scope.actor, 0x74);
        if (domain.sensor_scan) {
            for (Address p = source.first; p != source.end; p += 8) {
                Address s = read<Address>(p);
                if (s && distance(self, read<Vec3>(s, 0x28)) < radius &&
                    (is_visible(s) ? eligible_subject(brain, s) : !read<Byte>(s, 0x6C))) {
                    cutoff = true;
                    break;
                }
            }
        }
        auto kit = loadout(brain);
        result.order.reserve(source.size(8));
        std::unordered_set<Address> seen;
        seen.reserve(source.size(8));
        // For a shortcut the stock head is its argument or return value, even
        // when absent from the sensor list. Leave it ineffective unless admitted.
        if (domain.original) result.order.emplace_back().subject = domain.original;
        for (Address p = source.first; p != source.end; p += 8) {
            Address s = read<Address>(p);
            if (!s || !seen.insert(s).second) continue;
            if (domain.sensor_scan) {
                if (!eligible_subject(brain, s) || !profile_accepts(group, s)) continue;
                if (cutoff && !(distance(self, read<Vec3>(s, 0x28)) < radius)) continue;
                using Sector = unsigned(__fastcall*)(Address, Address);
                if (domain.cover && !(native<Sector>(0x8BB8A0)(domain.cover, s + 0x28) & domain.sector)) continue;
            }
            auto c = evaluate(brain, s, kit, preferred);
            if (s == domain.original) {
                result.order.front() = c; // native ordinal zero
                continue;
            }
            c.ordinal = result.order.size();
            result.order.push_back(c);
        }
        rank_candidates(result);
        return result;
    } catch (const std::bad_alloc&) {
        return {};
    }
}
inline void log_promotion(const Plan& result, Address brain, Address previous, const char* stage) {
    const auto& winner = result.order.front();
    if (!logger || winner.subject == previous || !sample_event(promotion_count)) return;
    unsigned blocked = 0;
    for (const auto& c : result.order)
        blocked += c.sight == Sight::blocked;
    logger("target_prio promotion #%llu actor=%p role=%s stage=%s old=%p promoted=%p reason=%s blocked=%u",
           promotion_count, reinterpret_cast<void*>(scope.actor), is_human_brain(brain) ? "human" : "vehicle", stage,
           reinterpret_cast<void*>(previous), reinterpret_cast<void*>(winner.subject),
           winner.grenade ? "vehicle-grenade"
           : result.open  ? "open"
                          : "ineffective-head",
           blocked);
}
inline Address choose_subject(const Plan& result, Address brain, Address stock, const char* stage) {
    if (!result.should_rewrite) return stock;
    log_promotion(result, brain, stock, stage);
    return result.order.front().subject;
}
inline void rewrite(SubjectPtrVec& v, const Plan& result, Address brain, const char* stage) {
    if (!result.should_rewrite) return;
    log_promotion(result, brain, *v.begin, stage);
    auto out = v.begin;
    for (const auto& c : result.order)
        *out++ = c.subject;
    v.end = out;
    for (std::size_t i = 0; i < result.order.size(); ++i)
        *reinterpret_cast<Byte*>(v.begin[i] + 0x11) = static_cast<Byte>(result.order.size() - i);
}
inline void deduplicate_native(SubjectPtrVec& v) {
    // Compact borrowed pointers. The vector keeps its own allocation and capacity.
    if (!entries(v).valid(8) || v.begin == v.end) return;
    auto out = v.begin;
    for (auto p = v.begin; p != v.end; ++p)
        if (*p && std::find(v.begin, out, *p) == out) *out++ = *p;
    v.end = out;
    for (auto p = v.begin; p != out; ++p)
        *reinterpret_cast<Byte*>(*p + 0x11) = static_cast<Byte>(out - p);
}
struct CutoffCandidates {
    std::array<Address, 256> omitted{};
    std::size_t count{}, original_size{};
    Address* storage{};
    Address* capacity{};
    Address owner_brain{};

    void capture(const SubjectPtrVec& v, Address actor, Address brain) {
        const auto span = entries(v);
        const Span allocation{span.first, reinterpret_cast<Address>(v.cap)};
        if (!span.valid(8) || span.size(8) < 2 || (span.first % alignof(Address)) || !allocation.valid(8) ||
            allocation.end < span.end)
            return;
        const auto pos = read<Vec3>(actor, 0x74);
        const float radius = read<float>(brain, 0x14c);
        bool has_near = false;
        const auto limit = std::min(span.size(8), omitted.size());
        for (auto it = v.begin; it != v.begin + limit; ++it) {
            if (!*it) continue;
            if (distance(pos, read<Vec3>(*it, 0x28)) < radius) {
                has_near = true;
            } else if (std::find(omitted.begin(), omitted.begin() + count, *it) == omitted.begin() + count) {
                omitted[count++] = *it;
            }
        }
        owner_brain = brain;
        original_size = limit;
        if (!has_near) count = 0;
        storage = v.begin;
        capacity = v.cap;
    }
    void restore(SubjectPtrVec& v) const {
        if (!count || v.begin != storage || v.cap != capacity || !entries(v).valid(8) ||
            entries(v).size(8) >= original_size || reinterpret_cast<Address>(v.end) > reinterpret_cast<Address>(v.cap))
            return;
        for (std::size_t i = 0; i < count && v.end != v.cap; ++i)
            if (std::find(v.begin, v.end, omitted[i]) == v.end) *v.end++ = omitted[i];
    }
};
inline void __fastcall ranked(SubjectPtrVec* v, Address owner) {
    runtime::HookGuard guard(hook_gate);
    CutoffCandidates before{};
    if (hook_gate.active() && config::active().modules.target_prioritization && depth == 1 && owner == scope.actor &&
        v && engine_fixes::current() == owner && owner) {
        const auto brain = read<Address>(owner, 0xdc8);
        if (admitted(brain)) before.capture(*v, owner, brain);
    }
    orig_rank(v, owner);
    if (!hook_gate.accepting.load(std::memory_order_acquire) || !config::active().modules.target_prioritization) return;
    if (depth != 1 || owner != scope.actor || !owner || !v) return;
    Address brain = read<Address>(owner, 0xDC8);
    if (!admitted(brain)) return;
    ++scope.rank_calls;
    const bool human = is_human_brain(brain);
    bool rebuilding = !human && reinterpret_cast<Address>(v) == brain + 0x3A0;
    if (rebuilding) {
        delete scope.vehicle_rank;
        scope.vehicle_rank = nullptr;
    }
    if (before.count && before.owner_brain == brain && engine_fixes::current() == owner) before.restore(*v);
    deduplicate_native(*v);
    if (!entries(*v).valid(8)) return;
    if (!human) remember_armor(brain, entries(*v));
    if (v->begin == v->end) return;
    Plan result;
    if (v->end - v->begin > 1) result = plan(brain, entries(*v), human ? read<Address>(brain, 0x3D0) : 0);
    if (rebuilding) {
        // Assignment follows row rebuild and the optional preference prepend.
        // Keep borrowed identities to recover native order if those changes
        // invalidate our preliminary choice. No pointers into vector storage escape.
        try {
            auto snapshot = std::make_unique<VehicleRank>();
            snapshot->brain = brain;
            snapshot->native_order.assign(v->begin, v->end);
            snapshot->ranked_order = snapshot->native_order;
            if (result.should_rewrite)
                for (std::size_t i = 0; i < result.order.size(); ++i)
                    snapshot->ranked_order[i] = result.order[i].subject;
            scope.vehicle_rank = snapshot.release();
        } catch (const std::bad_alloc&) {
            return;
        }
    }
    if (human && scope.picker_grenade && result.should_rewrite && result.order.front().grenade)
        *scope.picker_grenade = result.order.front().subject;
    rewrite(*v, result, brain, "rank");
}
inline void put_hint_first(SubjectPtrVec& v, Address hint) {
    if (!hint || v.begin == v.end) return;
    auto p = std::find(v.begin, v.end, hint);
    if (p != v.end) std::iter_swap(v.begin, p);
}
inline Address __fastcall pick(Address brain) {
    runtime::HookGuard guard(hook_gate);
    if (!hook_gate.accepting.load(std::memory_order_acquire) || !config::active().modules.target_prioritization)
        return orig_picker(brain);
    const unsigned before = scope.rank_calls;
    Address promoted{};
    auto previous = scope.picker_grenade;
    scope.picker_grenade = &promoted;
    Address stock = orig_picker(brain);
    scope.picker_grenade = previous;
    // The stock hand evaluator can discard an open-top frag after the rank
    // hook because its named volume is absent. Only a still-admitted observed
    // grenade candidate from this specific picker invocation overrides it.
    if (promoted && promoted != stock && admitted(brain) && contains(subjects(scope.actor), promoted) &&
        eligible_subject(brain, promoted)) {
        try {
            auto kit = loadout(brain);
            for (const Pair& p : kit.pairs)
                if (p.item && grenade_priority && grenade_priority(brain, promoted, p)) return promoted;
        } catch (const std::bad_alloc&) {
            return stock;
        }
    }
    if (!stock || !admitted(brain) || !is_human_brain(brain) || before != scope.rank_calls ||
        read<Address>(brain, 0x3D0) != stock)
        return stock;
    auto result = plan(brain, subjects(scope.actor), stock, {true, 0, 0, stock});
    return choose_subject(result, brain, stock, "picker");
}
inline int __fastcall cover_battle(Address state, Address selected, unsigned mask, int rotate) {
    runtime::HookGuard guard(hook_gate);
    if (!hook_gate.accepting.load(std::memory_order_acquire) || !config::active().modules.target_prioritization)
        return orig_cover(state, selected, mask, rotate);
    Address brain = (depth == 1 && state) ? read<Address>(state, 0x30) : 0;
    if (selected && mask && admitted(brain) && is_human_brain(brain)) {
        Address cover = read<Address>(scope.actor, 0xDC0);
        if (cover) {
            auto result =
                plan(brain, subjects(scope.actor), read<Address>(brain, 0x3D0), {true, cover + 0x20, mask, selected});
            selected = choose_subject(result, brain, selected, "cover-battle");
        }
    }
    return orig_cover(state, selected, mask, rotate);
}
inline int __fastcall cover_list(Address state, SubjectPtrVec* input, const char* reason) {
    runtime::HookGuard guard(hook_gate);
    if (!hook_gate.accepting.load(std::memory_order_acquire) || !config::active().modules.target_prioritization)
        return orig_list(state, input, reason);
    Address brain = (depth == 1 && state) ? read<Address>(state, 0x30) : 0;
    if (!input || !admitted(brain) || !is_human_brain(brain) || !entries(*input).valid(8) ||
        input->begin == input->end || !read<Address>(brain, 0x3D0))
        return orig_list(state, input, reason);
    // Reproduce 0x96BCA0's public vector copy, scheduled rank and try loop.
    // The original routine unconditionally swaps +3D0 back in after ranking.
    struct Local {
        SubjectPtrVec v{};
        ~Local() {
            free_buffer(reinterpret_cast<Address>(v.begin), reinterpret_cast<Address>(v.cap), 8);
        }
    } list;
    using Copy = void(__fastcall*)(SubjectPtrVec*, Address*, std::size_t);
    native<Copy>(0x96DF30)(&list.v, input->begin, entries(*input).size(8));
    orig_rank(&list.v, scope.actor);
    deduplicate_native(list.v);
    Address hint = read<Address>(brain, 0x3D0);
    put_hint_first(list.v, hint); // Native first try defines the stock head.
    auto result = plan(brain, entries(list.v), hint);
    rewrite(list.v, result, brain, "cover-list");
    using Try = int(__fastcall*)(Address, Address, const char*);
    for (auto p = list.v.begin; p != list.v.end; ++p) {
        int status = native<Try>(0x96B430)(state, *p, reason);
        if (status >= 0) return status;
    }
    return -3;
}

inline SubjectPtrVec& vehicle_vector(Address brain) {
    return *reinterpret_cast<SubjectPtrVec*>(brain + 0x3A0);
}
inline Address restore_vehicle_rank(Address brain, SubjectPtrVec& v) {
    std::unique_ptr<VehicleRank> saved(scope.vehicle_rank);
    scope.vehicle_rank = nullptr;
    auto slots = entries(v);
    if (!saved || saved->brain != brain || !slots.valid(8)) return 0;
    auto size = slots.size(8), prior = saved->ranked_order.size();
    if (size < prior || size - prior > 1) return 0;
    auto* suffix = v.begin + (size - prior);
    if (!std::equal(saved->ranked_order.begin(), saved->ranked_order.end(), suffix)) return 0;
    std::copy(saved->native_order.begin(), saved->native_order.end(), suffix);
    for (auto p = suffix; p != v.end; ++p)
        *reinterpret_cast<Byte*>(*p + 0x11) = static_cast<Byte>(v.end - p);
    return size > prior ? *v.begin : 0;
}
inline void __fastcall alternate_rebuild(Address brain) {
    runtime::HookGuard guard(hook_gate);
    orig_rebuild(brain);
    if (!hook_gate.accepting.load(std::memory_order_acquire) || !config::active().modules.target_prioritization) return;
    if (!admitted(brain) || is_human_brain(brain)) return;
    auto& v = vehicle_vector(brain);
    Address preferred = restore_vehicle_rank(brain, v);
    remember_armor(brain, entries(v));
    auto result = plan(brain, entries(v), preferred);
    rewrite(v, result, brain, "rebuild");
}
inline void __fastcall vehicle_assign(Address brain) {
    runtime::HookGuard guard(hook_gate);
    if (!hook_gate.accepting.load(std::memory_order_acquire) || !config::active().modules.target_prioritization) {
        orig_assign(brain);
        return;
    }
    if (!admitted(brain) || is_human_brain(brain)) {
        orig_assign(brain);
        return;
    }
    if (!current_rows(brain)) {
        orig_assign(brain);
        return;
    }
    Assignment a;
    a.brain = brain;
    auto& v = vehicle_vector(brain);
    if (!entries(v).valid(8)) {
        orig_assign(brain);
        return;
    }
    Address preferred = restore_vehicle_rank(brain, v);
    try {
        capture_rows(a);
        a.covered.reserve(entries(v).size(8));
    } catch (const std::bad_alloc&) {
        orig_assign(brain);
        return;
    }
    ArmorMemory hidden{};
    if (const auto* memory = remember_armor(brain, entries(v))) hidden = *memory;
    struct Frame {
        Assignment* previous = scope.assignment;
        const ArmorMemory* previous_memory = assignment_memory;
        Frame(Assignment& assignment, const ArmorMemory& memory) {
            scope.assignment = &assignment;
            assignment_memory = memory.vehicle.ptr ? &memory : nullptr;
        }
        ~Frame() {
            scope.assignment = previous;
            assignment_memory = previous_memory;
        }
    } frame(a, hidden);
    if (entries(v).size(8) > 1) {
        auto result = plan(brain, entries(v), preferred);
        rewrite(v, result, brain, "assignment");
    }
    orig_assign(brain);
}

inline bool shell_radius(Address ammo, float& maximum) {
    float r = read<float>(ammo, 0x498);
    if (!std::isfinite(r)) return false;
    maximum = (std::max)(maximum, r);
    Span effects = span(ammo, 0x480);
    if (!effects.valid(8)) return false;
    for (Address p = effects.first; p != effects.end; p += 8) {
        Address effect = read<Address>(p);
        if (!effect) continue;
        const auto getter = read<Address>(read<Address>(effect), 0x28) - base;
        if (getter == 0xBA970) continue; // base and cumulative effects return zero
        if (getter == 0xBA980)
            r = read<float>(effect, 8); // fire
        else if (getter == 0x15F240 || getter == 0x1E4090)
            r = read<float>(effect, 0x14); // blastwave or flash
        else
            return false;
        if (!std::isfinite(r) || r < 0) return false;
        maximum = (std::max)(maximum, r);
    }
    return true;
}
inline bool isolated_soldier(const Assignment& a, Address s, std::int16_t priority) {
    if (!eligible_subject(a.brain, s)) return false;
    Address target = read<Address>(s, 0x18);
    if (!target || !read<Address>(target, 0xDC0) || !(read<Byte>(target, 0x30) & 4) ||
        !tag_equals(target + 0xDE8, "human"))
        return false;
    Span list = subjects(scope.actor);
    if (!contains(list, s)) return false;
    float radius = 0;
    bool main = false;
    bool any_free_main = false;
    for (const auto& row : a.rows) {
        if (row.kind == RowKind::main_gun && !read<Byte>(row.row) && !reserved_for_other(row.weapon, priority)) {
            any_free_main = true;
            break;
        }
    }
    for (const auto& row : a.rows) {
        if (row.kind != RowKind::main_gun) continue;
        if (any_free_main && (read<Byte>(row.row) || reserved_for_other(row.weapon, priority))) continue;
        main = true;
        for (unsigned i = 0; i < 4; ++i) {
            Address ammo = read<Address>(row.row, 0x98 + i * 0x48);
            if (!ammo) break;
            if (!shell_radius(ammo, radius)) return false;
        }
    }
    if (!main) return false;
    Vec3 pos = read<Vec3>(s, 0x28);
    if (!finite(pos)) return false;
    // Sensor commit 0x88E270 indexes four triples at +0x268 + category*0x18.
    // Count visible hostile observations even when their own eligibility timer
    // would keep them out of this assignment's candidate list.
    Address sensor = read<Address>(scope.actor, 0xDD0);
    for (unsigned category = 0; category < 4; ++category) {
        auto observations = span(sensor, 0x268 + category * 0x18);
        if (!observations.valid(8)) return false;
        for (Address p = observations.first; p != observations.end; p += 8) {
            Address other = read<Address>(p);
            if (other == s || !is_visible(other) || !(read<unsigned>(other, 0x14) & 2) ||
                read<Address>(other, 0x18) == target)
                continue;
            float d = distance(pos, read<Vec3>(other, 0x28));
            if (!std::isfinite(d) || d <= radius) return false;
        }
    }
    return true;
}

inline std::uint32_t float_bits(float value) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}
// Pure multi-cannon preference rule: returns true when at least one unused cannon
// clearly dominates another (by >5 mm AP/HEAT penetration vs armor, or >15% larger
// HE blast radius vs infantry clusters) and marks the winning subset in `preferred`.
inline bool select_preferred_cannons(const float* metrics, std::size_t count, bool armoured, bool* preferred) noexcept {
    float best = 0.f;
    float worst = -1.f;
    unsigned eligible = 0;
    for (std::size_t i = 0; i < count; ++i) {
        preferred[i] = false;
        if (!(metrics[i] >= 0.f) || !std::isfinite(metrics[i])) continue;
        ++eligible;
        if (metrics[i] > best) best = metrics[i];
        if (worst < 0.f || metrics[i] < worst) worst = metrics[i];
    }
    if (eligible < 2 || !(best > 0.f)) return false;
    const bool split = armoured ? ((best - worst) > 5.f) : (worst < best * 0.85f);
    if (!split) return false;
    bool any_preferred = false;
    bool any_other = false;
    for (std::size_t i = 0; i < count; ++i) {
        if (!(metrics[i] >= 0.f) || !std::isfinite(metrics[i])) continue;
        const bool keep = armoured ? ((best - metrics[i]) <= 5.f) : (metrics[i] >= best * 0.85f);
        preferred[i] = keep;
        any_preferred |= keep;
        any_other |= !keep;
    }
    return any_preferred && any_other;
}
// Returns -1 when the cannon row is unavailable for this target, 0 when usable
// without the requested AP/HE capability, or positive penetration (mm) / HE radius.
inline float cannon_role_metric(const Row& row, Address brain, Address s, std::int16_t priority, bool armoured) {
    if (row.kind != RowKind::main_gun || read<Byte>(row.row) || reserved_for_other(row.weapon, priority)) return -1.f;
    Address w = row.weapon;
    Address container = read<Address>(w, 0xA0);
    if (!container || !is_visible(s)) return -1.f;
    Address gunner = read<Address>(w, 0xC0);
    if (read<Byte>(brain, 0x420) &&
        (!gunner || (native<Predicate>(0x825600)(gunner) && native<Predicate>(0x825730)(gunner))))
        return -1.f;
    Address target = read<Address>(s, 0x18);
    if (!target) return -1.f;
    const Vec3 self = read<Vec3>(scope.actor, 0x74), feet = read<Vec3>(s, 0x28);
    if (!finite(self) || !finite(feet)) return -1.f;
    const float scale = spatial_scale();
    Address bone = clean_bone(w);
    const Vec3 from = bone ? muzzle(w, bone) : raised_point(self, scope.actor, RayPoint::muzzle, scale);
    const Vec3 to = raised_point(feet, target, RayPoint::target, scale);
    const float d = distance(from, feet);
    const float actor_distance = distance(self, feet);
    if (!std::isfinite(d) || !std::isfinite(actor_distance)) return -1.f;
    const Ray* hit = cached_ray({s, target, w, weaponry_revision(), from, to});
    if (hit && !hit->open) return -1.f;
    const bool loaded_only = native<Predicate>(0x91B190)(brain) != 0;
    using Profile = Address(__fastcall*)(Address, Address);
    using PenAtDist = float(__fastcall*)(Address, std::uint32_t);
    bool any_rule = false;
    float best = 0.f;
    ammunition(row.row, w, loaded_only, [&](Ammo candidate) {
        auto ammo = candidate.def;
        if (rule_priority(w, ammo, s, false) < 0) return;
        Address profile = native<Profile>(0x83DE90)(container, ammo + 0xEC8);
        if (!profile || !(actor_distance < approach_range(w, profile)) || d < read<float>(profile, 0x28) ||
            !(d < read<float>(profile, 0x24)))
            return;
        any_rule = true;
        if (armoured) {
            float pen = 0.f;
            if ((!has_tag(ammo, "all_he") || has_tag(ammo, "heat_shell")) &&
                (read<Address>(profile, 0x88) != read<Address>(profile, 0x90) ||
                 (read<Address>(profile, 0x80) != 0 && read<float>(profile, 0x20) > 0.f))) {
                float kinetic = native<PenAtDist>(0x841650)(profile, float_bits(d));
                if (std::isfinite(kinetic) && kinetic > pen) pen = kinetic;
            }
            if (pen > best) best = pen;
        } else if (has_tag(ammo, "all_he")) {
            float rad = 0.f;
            if (shell_radius(ammo, rad) && rad > best) best = rad;
        }
    });
    return any_rule ? best : -1.f;
}

inline bool mg_candidate(const Row& row, Address brain, Address s, std::int16_t priority) {
    if (row.kind != RowKind::mg || read<Byte>(row.row) || reserved_for_other(row.weapon, priority)) return false;
    Address w = row.weapon;
    Address bone = clean_bone(w), container = read<Address>(w, 0xA0);
    if (!container || !is_visible(s)) return false;
    Address target = read<Address>(s, 0x18);
    if (!target) return false;
    const Vec3 self = read<Vec3>(scope.actor, 0x74), feet = read<Vec3>(s, 0x28);
    const float scale = spatial_scale();
    const Vec3 from = bone ? muzzle(w, bone) : raised_point(self, scope.actor, RayPoint::muzzle, scale);
    const Vec3 to = raised_point(feet, target, RayPoint::target, scale);
    const float d = distance(from, feet);
    const float actor_distance = distance(self, feet);
    // Consult exactly the chord used by ranking, without another segment query.
    const Ray* hit = cached_ray({s, target, w, weaponry_revision(), from, to});
    if (hit && !hit->open) return false;
    for (unsigned i = 0; i < 4; ++i) {
        Address record = row.row + 0x98 + i * 0x48, ammo = read<Address>(record);
        if (!ammo) break;
        if (!available_ammo(w, record) || rule_priority(w, ammo, s, false) < 0) continue;
        if (native<Predicate>(0x91B190)(brain) && native<Unary>(0x820B50)(read<Address>(w, 0xB0)) != ammo) continue;
        using Profile = Address(__fastcall*)(Address, Address);
        Address profile = native<Profile>(0x83DE90)(container, ammo + 0xEC8);
        if (profile && actor_distance < approach_range(w, profile) && d >= read<float>(profile, 0x28) &&
            d < read<float>(profile, 0x24))
            return true;
    }
    return false;
}
struct PartitionEntry {
    Address row;
    Byte saved;
    bool preferred;
    RowKind kind;
};
enum class Pass {
    mg,
    fallback,
    covered,
    cannon_prefer,
    cannon_fallback
};
class RowMask {
    const std::vector<PartitionEntry>& rows_;
    Pass pass_;
    bool hides(const PartitionEntry& r) const {
        switch (pass_) {
            case Pass::covered: return r.kind == RowKind::main_gun;
            case Pass::mg: return !r.preferred;
            case Pass::fallback: return r.preferred;
            case Pass::cannon_prefer: return r.kind == RowKind::main_gun && !r.preferred;
            case Pass::cannon_fallback: return r.kind != RowKind::main_gun || r.preferred;
        }
        return false;
    }

public:
    RowMask(const std::vector<PartitionEntry>& rows, Pass pass) : rows_(rows), pass_(pass) {
        for (const auto& r : rows_)
            if (!r.saved && hides(r)) *reinterpret_cast<Byte*>(r.row) = 1;
    }
    ~RowMask() {
        for (const auto& r : rows_)
            if (!r.saved && hides(r)) *reinterpret_cast<Byte*>(r.row) = r.saved;
    }
    RowMask(const RowMask&) = delete;
    RowMask& operator=(const RowMask&) = delete;
};
inline Address main_cannon(const Assignment& assignment) {
    Address sole{};
    unsigned count = 0;
    for (const Row& row : assignment.rows) {
        if (row.kind != RowKind::main_gun || read<Address>(row.weapon, 0x90) != scope.actor) continue;
        const auto place = read<Address>(row.weapon, 0xc0);
        const auto seats = ai_support::span(place, 0x5e0);
        if (place && read<Address>(place, 0x10) != scope.actor) continue;
        if (ai_support::tag(seats, "gunner")) return row.weapon;
        // A sole available secondary row still belongs to its independently named station.
        sole = seats.first == seats.end ? row.weapon : 0;
        ++count;
    }
    return count == 1 ? sole : 0;
}
inline bool shares_cannon_aim(Address cannon, const Row& row) {
    if (row.weapon == cannon) return true;
    if (row.kind != RowKind::mg || read<Address>(row.weapon, 0x90) != scope.actor) return false;
    // 0x832EB0 follows muzzle parents (+138) and tests articulated bones (+12 bit 0).
    // An independently articulated MG can turn before reaching the main gun's chain.
    const auto muzzle_bone = [](Address w) { return read<Address>(read<Address>(w, 0xa8), 0x20); };
    const auto model = read<Address>(scope.actor, 0xb0);
    const auto gun = muzzle_bone(cannon);
    auto mg = muzzle_bone(row.weapon);
    if (!gun || !mg) return false;
    if (gun == mg) return true;
    for (unsigned i = 0; mg && mg != model && i < 32; ++i, mg = read<Address>(mg, 0x138)) {
        if (!(read<Byte>(mg, 0x12) & 1)) continue;
        auto node = gun;
        for (unsigned j = 0; node && node != model && j < 32; ++j, node = read<Address>(node, 0x138))
            if (node == mg) return true;
        return false;
    }
    return false;
}
inline void memory_mask(const Assignment& assignment, Address subject, std::vector<PartitionEntry>& mask) {
    if (!assignment_memory || !config::active().modules.tank_target_memory || !hostile_observation(subject)) return;
    const auto target = read<Address>(subject, 0x18);
    if (!auto_smoke::has_actor(target) || !(read<Byte>(target, 0x30) & 4) || read<Byte>(subject, 0x6c)) return;
    const auto self = read<Vec3>(scope.actor, 0x74), pos = read<Vec3>(subject, 0x28);
    const auto metres = distance(self, pos) / spatial_scale();
    const auto cannon = main_cannon(assignment);
    const bool limited = assignment_memory->casemate || read<int>(read<Address>(cannon, 0xa8), 0x18) == 7;
    if (!cannon || !engine_fixes::policy::suppress_coax_slew(assignment_memory->bearing_rad,
                                                             std::atan2(pos.y - self.y, pos.x - self.x), limited, false,
                                                             false, metres, true))
        return;
    const auto infantry = infantry_loadout(target);
    if (!infantry.known || infantry.carries_at()) return;
    for (const auto& row : assignment.rows)
        if (!read<Byte>(row.row) && shares_cannon_aim(cannon, row)) mask.push_back({row.row, 0, true, row.kind});
}
// Mirror 0x9A90DC..0x9A9112, including replacement of the caller's owned inventory location.
inline bool reject_selection(Address out) {
    std::memset(reinterpret_cast<void*>(out), 0, 3 * sizeof(Address));
    alignas(16) std::byte empty[0x40]{};
    const auto temp = reinterpret_cast<Address>(empty);
    selection_call<Address (*)(Address, Address)>(0x7e35e0)(out + 0x18, selection_call<Unary>(0x7e18f0)(temp));
    selection_call<void (*)(Address)>(0x7e1ec0)(temp);
    return false;
}
inline bool __fastcall vehicle_select(Address brain, Address s, std::int16_t priority, Address out, int mode) {
    runtime::HookGuard hook_guard(hook_gate);
    auto original = [&] { return orig_select(brain, s, priority, out, mode); };
    if (!hook_gate.accepting.load(std::memory_order_acquire) || !config::active().modules.target_prioritization)
        return original();
    auto* a = scope.assignment;
    if (a && !a->selecting) a->proposed_weapon = 0;
    if (mode || !out || !a || a->brain != brain || a->selecting || !admitted(brain)) return original();
    const bool autonomous = autonomous_fire(base, brain);
    if (autonomous && config::active().modules.allegiance_guard &&
        target_veto(base, scope.actor, s) != TargetVeto::none)
        return reject_selection(out);
    if (!current_rows(brain)) return original();
    Span live = span(brain, 0x3B8);
    if (!live.valid(row_size) || live.size(row_size) != a->rows.size()) return original();
    for (std::size_t i = 0; i < a->rows.size(); ++i) {
        const Row& r = a->rows[i];
        if (r.row != live.first + i * row_size || read<Address>(r.row, 8) != r.weapon) return original();
    }
    std::vector<PartitionEntry> held;
    if (autonomous && assignment_memory) {
        try {
            memory_mask(*a, s, held);
        } catch (const std::bad_alloc&) {
            return original();
        }
    }
    RowMask bearing_guard(held, Pass::fallback);
    if (!held.empty()) ++stat_memory_holds;
    if (!select_armor && !a->economy && !a->multi_cannon) return original();
    struct InSelector {
        bool& flag;
        explicit InSelector(bool& f) : flag(f) {
            flag = true;
        }
        ~InSelector() {
            flag = false;
        }
    };
    auto remember_selection = [&](bool result) {
        a->proposed_weapon = result && read<Address>(out) ? read<Address>(read<Address>(out), 8) : 0;
        a->proposed_target = result && s ? read<Address>(s, 0x18) : 0;
        a->proposed_ammo = result ? read<Address>(out, 0x10) : 0;
        return result;
    };
    auto choose = [&] {
        InSelector guard(a->selecting);
        return remember_selection((select_armor && select_armor(*a, s, priority, out)) || original());
    };
    if (a->economy && isolated_soldier(*a, s, priority)) {
        bool covered =
            std::any_of(a->covered.begin(), a->covered.end(), [s](const Coverage& c) { return c.subject == s; });
        std::vector<PartitionEntry> rows;
        bool has_mg = false;
        try {
            rows.reserve(a->rows.size());
            for (const Row& r : a->rows) {
                bool mg = !covered && mg_candidate(r, brain, s, priority);
                rows.push_back({r.row, read<Byte>(r.row), mg, r.kind});
                has_mg |= mg;
            }
        } catch (const std::bad_alloc&) {
            return remember_selection(original());
        }
        if (covered || has_mg) {
            InSelector guard(a->selecting);
            if (covered) {
                RowMask mask(rows, Pass::covered);
                return remember_selection(original());
            }
            bool mg_selected;
            {
                RowMask mask(rows, Pass::mg);
                mg_selected = remember_selection(original());
            }
            if (mg_selected) {
                // Selection precedes firing. The caller owns the result's inventory location.
                Address mg_row = read<Address>(out);
                if (a->covered.size() < a->covered.capacity()) a->covered.push_back({s, mg_row});
                if (logger && sample_event(mg_save_count))
                    logger("target_prio MG-first #%llu actor=%p role=vehicle subject=%p mg_row=%p", mg_save_count,
                           reinterpret_cast<void*>(scope.actor), reinterpret_cast<void*>(s),
                           reinterpret_cast<void*>(mg_row));
                return true;
            }
            RowMask remaining(rows, Pass::fallback);
            return remember_selection(original());
        }
    }
    // Multi-cannon role preference: give the highest-penetration AP turret (e.g., gun1/gun2
    // 45 mm on T-35 or 75 mm M2 on M3 Lee/Grant) first shot at armored targets, and give the
    // largest-HE cannon first shot at infantry clusters so AP turrets stay loaded for armor.
    if (!a->multi_cannon || !eligible_subject(brain, s)) return choose();
    Address target = read<Address>(s, 0x18);
    if (!target || !read<Address>(target, 0xDC0)) return choose();
    const bool armoured = armoured_class(target);
    const bool soft_human = !armoured && (read<Byte>(target, 0x30) & 4) && tag_equals(target + 0xDE8, "human");
    if (!armoured && !soft_human) return choose();
    constexpr std::size_t max_rows = 32;
    if (a->rows.size() > max_rows) return choose();
    std::array<float, max_rows> metrics{};
    std::array<bool, max_rows> preferred{};
    for (std::size_t i = 0; i < a->rows.size(); ++i)
        metrics[i] = cannon_role_metric(a->rows[i], brain, s, priority, armoured);
    if (!select_preferred_cannons(metrics.data(), a->rows.size(), armoured, preferred.data())) return choose();
    std::vector<PartitionEntry> rows;
    try {
        rows.reserve(a->rows.size());
        for (std::size_t i = 0; i < a->rows.size(); ++i)
            rows.push_back({a->rows[i].row, read<Byte>(a->rows[i].row), preferred[i], a->rows[i].kind});
    } catch (const std::bad_alloc&) {
        return choose();
    }
    bool picked;
    {
        RowMask mask(rows, Pass::cannon_prefer);
        picked = choose();
    }
    if (picked) return true;
    RowMask fallback(rows, Pass::cannon_fallback);
    return choose();
}
} // namespace detail

// Bracket the existing slot-23 trampoline itself. Nested actor invocations
// suspend custom decisions until the outer actor resumes.
inline void enter_actor_scope(Address actor) noexcept {
    if (detail::depth++ == 0) {
        detail::scope = {};
        detail::scope.actor = actor;
    }
}
inline void exit_actor_scope(Address /*actor*/) noexcept {
    if (detail::depth == 0) return;
    if (--detail::depth == 0) {
        delete detail::scope.vehicle_rank;
        detail::scope = {};
    }
}

// The caller owns MinHook initialization, batch enable, callback quiescence
// and removal. create/remove use MinHook's zero-success return convention.
template <class Create, class Remove>
inline bool install(Address base, const char* sha256, Create createHookFn, Remove removeHookFn, detail::LogFn logFn) {
    using namespace detail;
    if (!base || detail::base || rollback_pending || !logFn || !sha256 || std::strcmp(sha256, executable_sha256) != 0)
        return false;
    std::array<void*, 7> added{};
    std::size_t count = 0;
    auto add = [&](Address rva, auto hook, auto& original) {
        void* target = reinterpret_cast<void*>(base + rva);
        void* trampoline = nullptr;
        if (createHookFn(target, reinterpret_cast<void*>(hook), &trampoline) != 0) {
            logFn("target_prio: failed to create hook at RVA 0x%llx", static_cast<unsigned long long>(rva));
            return false;
        }
        added[count++] = target;
        original = reinterpret_cast<decltype(hook)>(trampoline);
        return trampoline != nullptr;
    };
    if (!add(0x92BF20, &ranked, orig_rank) || !add(0x95C120, &pick, orig_picker) ||
        !add(0x960AE0, &cover_battle, orig_cover) || !add(0x96BCA0, &cover_list, orig_list) ||
        !add(0x9A7FA0, &vehicle_assign, orig_assign) || !add(0x9A7E20, &alternate_rebuild, orig_rebuild) ||
        !add(0x9A90C0, &vehicle_select, orig_select)) {
        while (count) {
            void* target = added[--count];
            if (removeHookFn(target) != 0) {
                rollback_pending = true; // Keep any surviving entry passive until the host removes it.
                logFn("target_prio: rollback failed at RVA 0x%llx",
                      static_cast<unsigned long long>(reinterpret_cast<Address>(target) - base));
            }
        }
        return false;
    }
    logger = logFn;
    detail::base = base;
    detail::hook_gate.activate();
    logFn("target_prio: seven entry hooks prepared for build 26734796");
    return true;
}
inline void deactivate() noexcept {
    detail::hook_gate.deactivate();
}
inline std::uint32_t inflight_count() noexcept {
    return detail::hook_gate.count();
}
inline void after_hooks_removed() noexcept {
    using namespace detail;
    hook_gate.deactivate();
    base = 0;
    logger = nullptr;
    grenade_priority = nullptr;
    resolve_selection_call = &selection_rva;
    promotion_count = mg_save_count = 0;
    armor_memory.release();
    memory_clock = memory_scene = 0;
    assignment_memory = nullptr;
    stat_memory_holds = 0;
    rollback_pending = false;
    orig_rank = nullptr;
    orig_picker = nullptr;
    orig_cover = nullptr;
    orig_list = nullptr;
    orig_assign = nullptr;
    orig_rebuild = nullptr;
    orig_select = nullptr;
    delete scope.vehicle_rank;
    scope = {};
    depth = 0;
}
} // namespace goh::target_prio
