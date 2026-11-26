#pragma once
#include "close_grenade_module.hpp"
#include "tank_auto_repair_module.hpp"
#include "penetration_at_policy.hpp"

namespace goh::penetration_at {
namespace p = policy;
namespace pr = target_prio::detail;
namespace support = ai_support;
using pr::Byte;
using pr::native;
using pr::read;
using pr::Vec3;
using support::World;
using target_prio::Address;
inline constexpr bool runtime_allowed = false;
inline constexpr unsigned hook_count = 2;
namespace detail {
struct ArmorHit56 {
    float impact[3];   // +0x00
    float normal[3];   // +0x0C
    float dir[3];      // +0x18
    int32_t face;      // +0x24
    uintptr_t volume;  // +0x28
    uint8_t entryFlag; // +0x30
    uint8_t pad[7];    // +0x31..+0x37
};
static_assert(sizeof(ArmorHit56) == 0x38, "ArmorHit56 size mismatch");

struct ArmorParams56 {
    uint32_t flags;  // +0x00
    float calibre;   // +0x04
    float angleDeg;  // +0x08
    float slopeA;    // +0x0C
    float slopeB;    // +0x10
    float f14;       // +0x14
    float f18;       // +0x18
    float f1C;       // +0x1C
    float f20;       // +0x20
    float invCos;    // +0x24
    float outRatio;  // +0x28
    int32_t dmgKind; // +0x2C
    float dmgVal;    // +0x30
    uint32_t pad34;  // +0x34
};
static_assert(sizeof(ArmorParams56) == 0x38, "ArmorParams56 size mismatch");

struct ArmorQuery40 {
    uintptr_t armor;       // +0x00
    ArmorHit56* hit;       // +0x08
    uintptr_t setting;     // +0x10
    ArmorParams56* params; // +0x18
    float mult;            // +0x20
    uint8_t canDamage;     // +0x24
    uint8_t pad[3];        // +0x25..+0x27
};
static_assert(sizeof(ArmorQuery40) == 0x28, "ArmorQuery40 size mismatch");

using SelectVolume = void (*)(Address, Byte, Address, Address, Address, Address);
using SetTarget = void (*)(Address, Address, int, int, int);
inline SelectVolume original_volume{};
inline SetTarget original_target{};
inline std::atomic<bool> accepting{};
inline std::atomic<unsigned> inflight{}, volumes{}, shells{}, holds{};
inline bool staged{};
struct Guard {
    Guard() {
        ++inflight;
    }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
    ~Guard() {
        --inflight;
    }
};
inline void count(std::atomic<unsigned>& counter, const char* action) {
    const auto n = ++counter;
    if (pr::logger && (n <= 5 || n % 256 == 0)) pr::logger("penetration_at %s #%u", action, n);
}
inline bool context(Address brain, World& w, bool allow_attack = false) {
    return accepting.load() && pr::depth == 1 && support::callback(pr::scope.actor, w) &&
           pr::admitted(brain, allow_attack) && (allow_attack || !close_grenade::has_explicit_attack(brain));
}
inline Address observer_brain() {
    return accepting.load() && pr::depth == 1 && pr::scope.actor && support::serial_thread() && support::host()
               ? read<Address>(pr::scope.actor, 0xDC8)
               : 0;
}
inline Address observation(Address target) {
    auto list = pr::subjects(pr::scope.actor);
    if (!list.valid(8) || list.size(8) > 4096) return 0;
    for (auto q = list.first; q != list.end; q += 8) {
        auto s = read<Address>(q);
        if (pr::hostile_observation(s) && !read<Byte>(s, 0x6c) && read<Address>(s, 0x18) == target) return s;
    }
    return 0;
}
inline bool tagged(Address def, std::initializer_list<const char*> names) {
    return def && std::any_of(names.begin(), names.end(), [&](const char* n) { return pr::has_tag(def, n); });
}
inline p::Shell shell(Address weapon, Address ammo) {
    const Address def = weapon ? read<Address>(weapon, 0x98) : 0;
    if (!def || !ammo || pr::tag_equals(def + 0x808, "weapon_throwing") ||
        tagged(def, {"weapon_throwing", "missile", "mgun", "flame", "mortar"}) ||
        tagged(ammo, {"missile", "smoke", "wp", "white_phosphorus", "sm", "phosphorus", "smoke_shell"}))
        return p::Shell::unavailable;
    if (!tagged(ammo, {"shell", "rocketlauncher", "ptr", "at_rifle", "heat_shell"}) &&
        !tagged(def, {"rocketlauncher", "ptr", "at_rifle"}))
        return p::Shell::unavailable;
    if (!native<pr::Unary>(0x8514E0)(ammo)) return p::Shell::unavailable;
    if (tagged(ammo, {"heat_shell", "heat", "heat_low", "heat_medium", "heat_high", "rocketlauncher"}) ||
        tagged(def, {"rocketlauncher"}))
        return p::Shell::cumulative;
    if (tagged(ammo, {"hv_ap", "apcr", "apds", "hvap"})) return p::Shell::subcaliber;
    if (tagged(ammo, {"std_ap", "all_ap", "ptr", "at_rifle"}) || tagged(def, {"ptr", "at_rifle"}))
        return p::Shell::standard;
    return tagged(ammo, {"all_he", "fg"}) ? p::Shell::explosive : p::Shell::unavailable;
}
inline Address profile_for(Address weapon, Address ammo) {
    auto set = read<Address>(weapon, 0xa0);
    return set && ammo ? native<Address (*)(Address, Address)>(0x83DE90)(set, ammo + 0xec8) : 0;
}
inline bool pen_table(Address profile) {
    auto table = pr::span(profile, 0x88);
    return table.valid(8) &&
           (table.first != table.end || (read<Address>(profile, 0x80) && read<float>(profile, 0x20) > 0));
}
struct Contact {
    Address volume{};
    p::Part part{};
    pr::SceneHit hit{};
    float cosine{};
    bool known{}, exposed{};
};
struct Sample {
    Address volume{}, ammo{}, profile{};
    p::Estimate estimate{};
};
struct Exposure {
    Address actor{}, target{}, weapon{};
    unsigned entity{}, owner_id{};
    unsigned short revision{};
    Vec3 origin{};
    std::array<Contact, 24> faces{};
    std::array<Sample, 96> samples{};
    unsigned used{}, computed{};
};
struct TickCache {
    Address scene{};
    unsigned now{}, rays{};
    std::array<Exposure, 24> entries{};
    unsigned used{};
    void tick(Address current_scene, unsigned current_tick) {
        if (scene != current_scene || now != current_tick) {
            scene = current_scene;
            now = current_tick;
            used = 0;
            rays = 0;
        }
    }
};
inline TickCache cache{};
inline Exposure* exposure(const World& w, Address target, Address weapon) {
    const auto bone = pr::clean_bone(weapon);
    if (!bone) return nullptr;
    const auto from = pr::muzzle(weapon, bone);
    if (!pr::finite(from)) return nullptr;
    cache.tick(support::entity_scene(w), w.now);
    const auto revision = pr::weaponry_revision();
    const auto id = read<unsigned>(target, 0xc0);
    for (unsigned i = 0; i < cache.used; ++i) {
        auto& e = cache.entries[i];
        if (e.actor == pr::scope.actor && e.owner_id == read<unsigned>(pr::scope.actor, 0xc0) && e.target == target &&
            e.entity == id && e.weapon == weapon && e.revision == revision && e.origin.x == from.x &&
            e.origin.y == from.y && e.origin.z == from.z)
            return &e;
    }
    if (cache.used == cache.entries.size()) return nullptr;
    auto& e = cache.entries[cache.used++];
    e.actor = pr::scope.actor;
    e.owner_id = read<unsigned>(e.actor, 0xc0);
    e.target = target;
    e.weapon = weapon;
    e.entity = id;
    e.revision = revision;
    e.origin = from;
    e.used = e.computed = 0;
    return &e;
}
inline bool first_contact(Contact& c, const pr::HitVec& hits, Address target) {
    for (auto h = hits.begin; h != hits.end; ++h) {
        if (!std::isfinite(h->distance) || h->distance < 0) return false;
        if (h->kind == 1 || h->kind == 2) return true;
        if (h->kind == 3 && !h->volume) {
            if (h->entity != target) return true;
            continue;
        } // Object-level broad phase has no armor face.
        if (!h->volume) return false;
        if ((read<Byte>(h->volume, 0x28) & 0x20) || !(read<unsigned>(h->volume, 0xc) & 2)) continue;
        if (h->entity != target || h->volume != c.volume) return true;
        if (h->kind != 4 || h->face < 0 || h->face > 5 || !pr::finite(h->normal) || !pr::finite(h->direction) ||
            !pr::finite(h->position))
            return false;
        auto cos = -pr::alignment(h->normal, h->direction);
        if (!(cos > 0)) return false;
        c.exposed = true;
        std::memcpy(&c.hit, h, sizeof c.hit);
        c.cosine = cos;
        return true;
    }
    return true;
}
struct Volume {
    Address v{}, name{};
    p::Part part{};
    bool first_kind{};
};
struct Choices {
    std::array<Volume, 10> list{};
    unsigned size{};
    bool complete{true};
};
inline Choices choices(Address target, Address tag) {
    Choices out;
    auto vs = pr::span(target, 0x510);
    auto requested = tag ? support::native_string(tag + 0x40) : std::string_view{};
    if (!vs.valid(8) || vs.size(8) > 4096 || (tag && read<size_t>(tag, 0x50) && requested.empty())) {
        out.complete = false;
        return out;
    }
    std::array<bool, 8> seen{};
    for (auto i = vs.first; i != vs.end; i += 8) {
        Address v = read<Address>(i), def = v ? read<Address>(v, 0x30) : 0, component = v ? read<Address>(v, 0x38) : 0;
        if (!def || !read<Address>(v, 0x48) || !read<Address>(v, 0x50)) continue;
        auto names = pr::span(def, 0x398);
        if (!names.valid(0x60) || !names.size(0x60) || names.size(0x60) > 128) continue;
        bool exact = false, forbidden = false;
        p::Part kind = p::Part::other;
        for (auto n = names.first; n != names.end; n += 0x60) {
            auto name = support::native_string(n + 0x40);
            exact |= name == requested;
            forbidden |= name == "no_target";
            if (kind == p::Part::other) kind = p::part(name);
        }
        if (component && (kind == p::Part::other || kind == p::Part::body)) {
            const auto name = tank_auto_repair::detail::component_name(read<Address>(component));
            const auto typed = p::part(name);
            if (typed != p::Part::other) kind = typed;
        }
        if (!p::permitted_volume(requested, exact, forbidden, component && read<Byte>(component, 0x24), kind)) continue;
        if (native<Address (*)(Address, Address)>(0x7AA9C0)(target + 0x500, names.first) != v) continue;
        auto index = static_cast<unsigned>(kind);
        Volume item{v, names.first, kind, !seen[index]};
        seen[index] = true;
        // Give each tagged family one probe before spending the budget on duplicates.
        auto end = out.list.begin() + out.size;
        auto pos =
            item.first_kind ? std::find_if(out.list.begin(), end, [](const Volume& x) { return !x.first_kind; }) : end;
        if (out.size == out.list.size()) {
            out.complete = false;
            if (pos == end) continue;
            --end;
        } else
            ++out.size;
        std::move_backward(pos, end, end + 1);
        *pos = item;
    }
    return out;
}
inline Contact* contact(Exposure& e, Volume v, unsigned& remaining) {
    for (unsigned i = 0; i < e.used; ++i)
        if (e.faces[i].volume == v.v) return &e.faces[i];
    if (!remaining || cache.rays >= 192 || e.used == e.faces.size()) return nullptr;
    native<void (*)(Address)>(0x7A8A30)(v.v);
    const auto center = read<Vec3>(v.v, 0x7c);
    if (!pr::finite(center) || !(pr::distance(e.origin, center) > 0)) return nullptr;
    --remaining;
    ++cache.rays;
    auto& c = e.faces[e.used++];
    c = {};
    c.volume = v.v;
    c.part = v.part;
    c.known = pr::visit_segment({0, e.target, e.weapon, e.revision, e.origin, center},
                                [&](const pr::HitVec& hits) { return first_contact(c, hits, e.target); });
    return &c;
}
inline p::Estimate estimate(Exposure& e, const Contact& c, Address ammo, Address profile, p::Shell kind) {
    for (unsigned i = 0; i < e.computed; ++i) {
        const auto& x = e.samples[i];
        if (x.ammo == ammo && x.profile == profile && x.volume == c.volume) return x.estimate;
    }
    if (e.computed == e.samples.size()) return {};
    auto& x = e.samples[e.computed++];
    x = {c.volume, ammo, profile, {}};
    if (kind == p::Shell::explosive) return x.estimate = {0, 0, c.cosine};
    if (!pen_table(profile)) return {};
    const float pen = native<float (*)(Address, std::uint32_t)>(0x841650)(profile, pr::float_bits(c.hit.distance));
    const auto cal = read<float>(read<Address>(e.weapon, 0x98), 0x1628);
    if (!std::isfinite(pen) || pen < 0 || !(cal > 0) || !std::isfinite(cal)) return {};
    ArmorParams56 params{};
    params.calibre = cal;
    params.dmgVal = pen;
    params.invCos = 1;
    params.slopeA = read<float>(pr::base + 0x1495030);
    params.slopeB = read<float>(pr::base + 0x1495034);
    std::memcpy(&params.f14, reinterpret_cast<const void*>(pr::base + 0x10F15D0), 16);
    native<void (*)(ArmorParams56*, Address, float)>(0x7FC090)(&params, profile, (std::max)(.01f, c.cosine));
    ArmorHit56 hit{};
    std::memcpy(hit.impact, &c.hit.position, 12);
    std::memcpy(hit.normal, &c.hit.normal, 12);
    std::memcpy(hit.dir, &c.hit.direction, 12);
    hit.volume = c.volume;
    hit.face = c.hit.face;
    hit.entryFlag = 1;
    ArmorQuery40 q{};
    native<ArmorQuery40* (*)(ArmorQuery40*, Address, Address, ArmorHit56*, ArmorParams56*)>(0x7FC310)(
        &q, e.target, e.actor, &hit, &params);
    if (!q.armor || !q.setting || !q.canDamage) return {};
    auto flags = read<Byte>(q.setting, 0xb0);
    if (flags & 6) params.invCos = 1;
    const auto def = read<Address>(c.volume, 0x30);
    if ((flags & 4) && !read<Address>(def, 0x408)) return {};
    const float raw = native<float (*)(ArmorQuery40*, Byte)>(0x7FC5B0)(&q, 1);
    if (!std::isfinite(raw) || raw < 0) return {};
    const float thickness = native<float (*)(ArmorQuery40*, float)>(0x7FCA60)(&q, raw);
    return x.estimate = {pen, thickness, c.cosine};
}
struct Ranked {
    Address volume{}, name{};
    float score{};
};
struct Assessment {
    p::Outcome outcome{};
    bool complete{};
    std::array<Ranked, 10> ranked{};
    unsigned size{};
};
inline Assessment assess(const World& w, Address weapon, Address target, Address ammo, Address profile, p::Shell kind,
                         Address tag = 0, Address previous = 0) {
    Assessment a;
    auto* e = exposure(w, target, weapon);
    if (!e || !profile || kind == p::Shell::unavailable) return a;
    auto options = choices(target, tag);
    unsigned budget = 8;
    a.complete = options.complete && options.size && options.size <= 8;
    for (unsigned i = 0; i < options.size && i < 8; ++i) {
        auto v = options.list[i];
        auto c = contact(*e, v, budget);
        if (!c || !c->known) {
            a.complete = false;
            continue;
        }
        if (!c->exposed) continue;
        auto value = estimate(*e, *c, ammo, profile, kind);
        if (!value.valid()) {
            a.complete = false;
            continue;
        }
        a.outcome.observe(v.part, value);
        a.ranked[a.size++] = {v.v, v.name, p::volume_score(v.part, value, v.v == previous)};
    }
    std::stable_sort(a.ranked.begin(), a.ranked.begin() + a.size, [](Ranked l, Ranked r) { return l.score > r.score; });
    return a;
}
using pr::Ammo;
struct Proposal {
    Address row{}, weapon{}, rule{};
    Ammo ammo{};
    int tier{}, priority{};
    float ratio{};
};
struct Options {
    p::Outcome all{};
    bool complete{true}, capable{};
    std::array<Proposal, 32> rows{};
    unsigned used{};
};
inline bool open_top(Address target) {
    auto props = pr::span(read<Address>(target, 0x548), 0x330);
    return close_grenade::has(props, "open_top") || close_grenade::has(props, "ammo_open_top_turret") ||
           close_grenade::has(props, "explosion_throw_crew");
}
inline bool near_ready(const World& w, Address weapon, Address ammo) {
    const Address charger = read<Address>(weapon, 0xb0);
    if (native<pr::Unary>(0x820B50)(charger) != ammo) return false;
    auto charged = read<pr::Unary>(read<Address>(charger), 8)(charger);
    if (!charged || read<int>(charged, 0xdc) <= 0) return false;
    const auto deadline = read<unsigned>(charger, 0x10);
    return p::ready_soon(native<pr::Predicate>(0x81EDA0)(weapon) != 0, deadline != 0,
                         static_cast<float>(static_cast<std::int32_t>(deadline - w.now)) / w.ticks);
}
inline bool better(const Proposal& a, const Proposal& b) {
    if (a.tier != b.tier) return a.tier > b.tier;
    if (a.ratio != b.ratio) return a.ratio > b.ratio;
    return a.priority > b.priority;
}
inline Options vehicle_options(const World& w, Address brain, Address s, std::int16_t priority, bool selecting,
                               bool soft_vehicle = false) {
    Options out;
    const auto target = read<Address>(s, 0x18);
    const auto rows = pr::span(brain, 0x3b8);
    if (!pr::current_rows(brain) || !rows.valid(pr::row_size) || rows.size(pr::row_size) > out.rows.size()) {
        out.complete = false;
        return out;
    }
    const bool locked = native<pr::Predicate>(0x91B190)(brain), open = soft_vehicle || open_top(target);
    const Vec3 feet = read<Vec3>(s, 0x28);
    const float actor_distance = pr::distance(read<Vec3>(pr::scope.actor, 0x74), feet);
    for (auto row = rows.first; row != rows.end; row += pr::row_size) {
        auto weapon = read<Address>(row, 8);
        if (!weapon || read<Address>(weapon, 0x90) != pr::scope.actor || !read<Address>(weapon, 0x98) ||
            !read<Address>(weapon, 0xa0) || (selecting && read<Byte>(row)) || pr::reserved_for_other(weapon, priority))
            continue;
        auto gunner = read<Address>(weapon, 0xc0);
        if (!gunner || (read<Byte>(brain, 0x420) && native<pr::Predicate>(0x825600)(gunner) &&
                        native<pr::Predicate>(0x825730)(gunner)))
            continue;
        auto bone = pr::clean_bone(weapon);
        Proposal best{}, subcaliber{};
        float standard_penetration = 0, subcaliber_penetration = 0;
        bool known = pr::ammunition(row, weapon, locked, [&](Ammo a) {
            auto k = shell(weapon, a.def);
            if (k == p::Shell::unavailable) return;
            auto rule = pr::matching_rule(weapon, a.def, s, false);
            if (!rule || read<int>(rule, 0x390) < 0) return;
            auto profile = profile_for(weapon, a.def);
            auto current = native<pr::Unary>(0x81EC40)(weapon);
            if (!bone || !profile || !current) {
                out.complete = false;
                return;
            }
            auto d = pr::distance(pr::muzzle(weapon, bone), feet);
            if (!(d >= read<float>(current, 0x28) && d < read<float>(current, 0x24)) ||
                !(d >= read<float>(profile, 0x28) && d < read<float>(profile, 0x24)) ||
                !(actor_distance < pr::approach_range(weapon, profile)))
                return;
            // Ground-target rules use a different native resolver.
            if ((read<Byte>(rule, 0x394) & 1) && k != p::Shell::explosive) {
                out.complete = false;
                return;
            }
            auto tag = (read<Byte>(rule, 0x394) & 2) ? rule + 0x330 : 0;
            Assessment rating;
            if (soft_vehicle) {
                if (k != p::Shell::explosive) return;
                rating.complete = true;
                rating.outcome.component = true;
            } else
                rating = assess(w, weapon, target, a.def, profile, k, tag);
            out.capable |= k != p::Shell::explosive;
            out.complete &= rating.complete;
            if (k != p::Shell::explosive) out.all.merge(rating.outcome);
            bool open_choice = open;
            if (k == p::Shell::explosive && open_choice) {
                float rad = 0.f;
                if (!pr::shell_radius(a.def, rad) ||
                    (rad > 2.f * w.length &&
                     !close_grenade::clear_area(w, pr::scope.actor, feet, rad, close_grenade::Payload::charge)))
                    open_choice = false;
                if (soft_vehicle && !open_choice) return;
            }
            int tier = p::shell_priority(k, rating.outcome, near_ready(w, weapon, a.def), open_choice);
            Proposal next{row, weapon, rule, a, tier, read<int>(rule, 0x390), rating.outcome.ratio};
            if (k == p::Shell::standard)
                standard_penetration = (std::max)(standard_penetration, rating.outcome.penetration);
            if (k == p::Shell::subcaliber) {
                if (tier && better(next, subcaliber)) {
                    subcaliber = next;
                    subcaliber_penetration = rating.outcome.penetration;
                }
            } else if (tier && better(next, best))
                best = next;
        });
        if (!known && pr::row_kind(weapon) == pr::RowKind::main_gun) out.complete = false;
        if (subcaliber.row &&
            p::subcaliber_improves(subcaliber_penetration, standard_penetration,
                                   subcaliber.tier == p::chambered_round) &&
            better(subcaliber, best))
            best = subcaliber;
        if (best.row) out.rows[out.used++] = best;
    }
    return out;
}
struct Target {
    alignas(16) std::byte storage[0x80]{};
    Address ptr() {
        return reinterpret_cast<Address>(storage);
    }
    Target(const Target&) = delete;
    Target& operator=(const Target&) = delete;
    Target() {
        auto vt = pr::base + 0x101FD18;
        std::memcpy(storage, &vt, sizeof vt);
        native<pr::Unary>(0x564D50)(ptr() + 0x20);
    }
    ~Target() {
        native<void (*)(Address)>(0x853240)(ptr());
        native<void (*)(Address)>(0x565010)(ptr() + 0x20);
    }
};
template <class FromStack, class Assign, class Release>
inline void handoff(Address out, const Proposal& v, FromStack from_stack, Assign assign, Release release) {
    alignas(16) std::byte location[0x40]{};
    auto temp = reinterpret_cast<Address>(location);
    auto loc = v.ammo.record;
    if (v.ammo.stack) {
        from_stack(temp, v.ammo.stack);
        loc = temp;
    }
    assign(out + 0x18, loc);
    if (v.ammo.stack) release(temp);
    std::array<Address, 3> result{v.row, v.rule, v.ammo.def};
    std::memcpy(reinterpret_cast<void*>(out), result.data(), sizeof result);
}
inline bool select_ammo(const pr::Assignment& a, Address subject, std::int16_t priority, Address out) {
    Guard guard;
    World w{};
    if (!context(a.brain, w) || !pr::eligible_subject(a.brain, subject)) return false;
    auto target = read<Address>(subject, 0x18);
    auto type = close_grenade::vehicle(target);
    bool soft = type == close_grenade::Vehicle::gun || type == close_grenade::Vehicle::car;
    if ((!soft && !pr::armoured_class(target)) || !read<Address>(target, 0xDC0)) return false;
    auto choices = vehicle_options(w, a.brain, subject, priority, true, soft);
    std::stable_sort(choices.rows.begin(), choices.rows.begin() + choices.used, better);
    for (unsigned i = 0; i < choices.used; ++i) {
        auto& v = choices.rows[i];
        if (v.tier == p::component_he && !choices.complete) continue;
        Target point;
        native<void (*)(Address, Address)>(0x853180)(point.ptr(), subject + 0x28);
        int time{};
        if (native<int (*)(Address, Address, int*, Byte)>(0x833D80)(read<Address>(v.weapon, 0xa8), point.ptr(), &time,
                                                                    1) == 6)
            continue;
        handoff(out, v, native<Address (*)(Address, Address)>(0x7E1AA0),
                native<Address (*)(Address, Address)>(0x7E35E0), native<void (*)(Address)>(0x7E1EC0));
        count(shells, "ammo");
        return true;
    }
    return false;
}
inline p::Infantry infantry(Address target) {
    p::Infantry data;
    if (!(read<Byte>(target, 0x30) & 4) || !pr::tag_equals(target + 0xde8, "human") || !read<Address>(target, 0x578))
        return data;
    auto box = native<pr::Unary>(0x7E0420)(target);
    auto items = pr::span(box, 0x10);
    if (!box || !items.valid(8) || items.size(8) > 4096) return data;
    auto classify = [&](Address item) {
        if (!item || read<int>(item, 0x48) <= 0) return;
        auto def = read<Address>(item, 0x40);
        data.launcher |= tagged(def, {"weapon"}) && tagged(def, {"rocketlauncher"});
        data.rifle |= tagged(def, {"weapon"}) && tagged(def, {"at_rifle", "ptr"});
        data.grenade |= tagged(def, {"missile", "grenade"}) && tagged(def, {"at", "ic", "dynamite"});
    };
    classify(pr::hand_item(target));
    for (auto i = items.first; i != items.end; i += 8)
        classify(read<Address>(i));
    data.known = true;
    return data;
}
inline bool defanged(Address target) {
    auto rows = support::span(target, 0xb90);
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
inline bool quiet(const World& w, Address actor) {
    auto sensor = read<Address>(actor, 0xdd0), weaponry = read<Address>(actor, 0xdd8);
    if (!sensor || read<Address>(sensor, 0x260) != actor || !weaponry ||
        auto_smoke::recent(w, read<unsigned>(sensor, 0x2f4), 4) ||
        auto_smoke::recent(w, read<unsigned>(weaponry, 0x90), 4) || auto_smoke::recently_hit(w, actor))
        return false;
    for (unsigned cat = 0; cat < 4; ++cat) {
        auto list = pr::span(sensor, 0x268 + cat * 0x18);
        if (!list.valid(8) || list.size(8) > 4096) return false;
        for (auto i = list.first; i != list.end; i += 8) {
            auto s = read<Address>(i);
            if (!s || !(read<unsigned>(s, 0x14) & 2)) continue;
            if (((read<unsigned>(s, 0x14) & 0x180) && auto_smoke::recent(w, read<unsigned>(s, 0x68), 4)) ||
                pr::enemy_aims_here(s, actor))
                return false;
        }
    }
    return true;
}
inline std::int16_t priority() {
    return native<std::int16_t (*)(Address)>(0x81A8D0)(read<Address>(pr::scope.actor, 0xdd8) + 0x10);
}
inline bool stray(const pr::Assignment* a, Address s, const p::Infantry& threat, float metres) {
    if (!a || !p::stray(true, true, threat, metres)) return false;
    const bool towed = support::native_string(pr::scope.actor + 0xde8 + 0x40).substr(0, 7) == "cannon_";
    bool main = false;
    for (const auto& r : a->rows) {
        if (!towed && r.kind == pr::RowKind::mg) return false;
        main |= r.kind == pr::RowKind::main_gun;
    }
    return main && pr::isolated_soldier(*a, s, priority());
}
inline pr::ArmorRating rate(Address brain, Address s, const pr::Loadout& kit, float danger) {
    Guard guard;
    pr::ArmorRating result{};
    result.danger = danger;
    World w{};
    if (!context(brain, w)) return result;
    const auto target = read<Address>(s, 0x18);
    const bool human = pr::is_human_brain(brain);
    if (pr::armoured_class(target)) {
        const bool broken_gun = defanged(target);
        result.defanged = broken_gun;
        result.danger = p::danger(danger, broken_gun, {}, 0);
        if (!human) {
            auto options = vehicle_options(w, brain, s, priority(), false);
            result.outcome = options.all;
            result.complete = options.capable && options.complete;
        } else {
            auto weapon = kit.sight_place;
            if (!weapon || read<Address>(weapon, 0x90) != pr::scope.actor) return result;
            const auto bone = pr::clean_bone(weapon);
            if (!bone) return result;
            const float distance = pr::distance(pr::muzzle(weapon, bone), read<Vec3>(s, 0x28));
            bool complete = true, applicable = false;
            for (const auto& pair : kit.pairs) {
                if (!pair.held || pair.item || !(distance >= pair.min_range && distance < pair.max_range)) continue;
                auto kind = shell(weapon, pair.ammo);
                if (kind == p::Shell::unavailable || kind == p::Shell::explosive) continue;
                auto rule = pr::matching_rule(0, pair.ammo, s, true);
                if (!rule || read<int>(rule, 0x390) < 0) continue;
                applicable = true;
                if (read<Byte>(rule, 0x394) & 1) {
                    complete = false;
                    continue;
                }
                auto rating = assess(w, weapon, target, pair.ammo, profile_for(weapon, pair.ammo), kind,
                                     (read<Byte>(rule, 0x394) & 2) ? rule + 0x330 : 0);
                result.outcome.merge(rating.outcome);
                complete &= rating.complete;
            }
            result.complete = applicable && complete;
        }
    } else if (!human && (read<Byte>(target, 0x30) & 4)) {
        const auto threat = infantry(target);
        float metres = pr::distance(read<Vec3>(pr::scope.actor, 0x74), read<Vec3>(s, 0x28)) / w.length;
        result.danger = p::danger(danger, false, threat, metres);
        if (stray(pr::scope.assignment, s, threat, metres) && quiet(w, pr::scope.actor)) result.discipline = -.55f;
    }
    return result;
}
inline Address selected_ammo(Address weapon, Address target) {
    auto* a = pr::scope.assignment;
    if (a && !(read<int>(weapon, 0xd0) && read<int>(weapon, 0x208) == 3) && a->proposed_weapon == weapon &&
        a->proposed_target == target && a->proposed_ammo)
        return a->proposed_ammo;
    auto charger = read<Address>(weapon, 0xb0);
    return charger ? native<pr::Unary>(0x820B50)(charger) : 0;
}
// An upper bound is sufficient to reserve the fallback's share of the five native trials.
inline unsigned stock_trials(Address target, Address tag, unsigned limit) {
    auto entries = pr::span(target, 0x510);
    if (!entries.valid(8) || entries.size(8) > 4096) return limit;
    unsigned count = 0;
    for (auto i = entries.first; i != entries.end && count < limit; i += 8) {
        auto v = read<Address>(i);
        if (!v || !read<Address>(v, 0x30)) return limit;
        auto part = read<Address>(v, 0x38);
        if (part && read<Byte>(part, 0x24)) continue;
        auto names = pr::span(read<Address>(v, 0x30), 0x398);
        if (!names.valid(0x60) || names.size(0x60) > 128) return limit;
        bool matches = !read<std::size_t>(tag, 0x50);
        for (auto n = names.first; n != names.end && !matches; n += 0x60)
            matches = native<Byte (*)(Address, Address)>(0x565340)(n, tag) != 0;
        if (matches && !close_grenade::has(names, "no_target")) ++count;
    }
    return count;
}
inline void select_volume(Address out, Byte skip, Address weapon, Address target, Address tag, Address prev) {
    Guard guard;
    const auto brain = observer_brain();
    World w{};
    if (!brain || !out || !weapon || !target || !tag || !context(brain, w, true) ||
        read<Address>(weapon, 0x90) != pr::scope.actor || !read<Address>(weapon, 0xa8) ||
        (read<int>(weapon, 0xd0) && read<int>(weapon, 0x208) == 1) || !observation(target) ||
        !pr::armoured_class(target))
        return original_volume(out, skip, weapon, target, tag, prev);
    auto ammo = selected_ammo(weapon, target);
    auto kind = shell(weapon, ammo);
    if (kind == p::Shell::unavailable) return original_volume(out, skip, weapon, target, tag, prev);
    auto previous = prev && read<Address>(prev, 0x18) == target ? native<pr::Unary>(0x8527C0)(prev) : 0;
    auto ranked = assess(w, weapon, target, ammo, profile_for(weapon, ammo), kind, tag, previous);
    if (!ranked.size) return original_volume(out, skip, weapon, target, tag, prev);
    auto bind = native<void (*)(Address, Address, Address)>(0x852DC0);
    if (skip) {
        bind(out, target, ranked.ranked[0].volume);
        count(volumes, "volume");
        return;
    }
    const auto range = native<float (*)(Address, Address)>(0x81F710)(weapon, target);
    if (!std::isfinite(range) || !(range > 0)) return original_volume(out, skip, weapon, target, tag, prev);
    // Spend the scheduled trial budget on ranked volumes. The last attempt goes
    // through the stock resolver. Its narrow tag must name exactly one live volume.
    unsigned trials = 0;
    for (unsigned i = 0; i < ranked.size && trials < 5; ++i) {
        const auto& choice = ranked.ranked[i];
        bind(out, target, choice.volume);
        auto pos = native<const Vec3* (*)(Address)>(0x852810)(out);
        if (!pos || !(pr::distance(read<Vec3>(pr::scope.actor, 0x74), *pos) < range)) continue;
        if (i + 1 == ranked.size || trials == 4) {
            if (stock_trials(target, choice.name, 2) == 1) {
                original_volume(out, 0, weapon, target, choice.name, out);
                if (read<int>(out, 8)) {
                    count(volumes, "volume");
                    return;
                }
                ++trials;
                break;
            }
        }
        ++trials;
        if (native<int (*)(Address, Address, Address, Address, Address, Address)>(0x833C30)(read<Address>(weapon, 0xa8),
                                                                                            out, 0, 0, 0, 0) == 3) {
            count(volumes, "volume");
            return;
        }
    }
    if (!trials || stock_trials(target, tag, 5) <= 5 - trials)
        return original_volume(out, skip, weapon, target, tag, prev);
    // Use the native empty-target terminal at 0x92BA09. A second full resolver could replay up to five previews.
    native<void (*)(Address)>(0x853240)(out);
}
inline int option_for(Address weapon, Address target, int option, int mode, Address caller) {
    if (mode != 2 || option != 3 || caller != pr::base + 0x9A85D1) return option;
    const auto brain = observer_brain();
    World w{};
    if (!brain || !weapon || !target || !context(brain, w) || pr::is_human_brain(brain) ||
        read<Address>(weapon, 0x90) != pr::scope.actor || read<int>(target, 8) != 1 || !pr::scope.assignment ||
        pr::scope.assignment->brain != brain || !pr::current_rows(brain))
        return option;
    auto enemy = read<Address>(target, 0x18);
    auto s = observation(enemy);
    if (!s || !pr::eligible_subject(brain, s) || !read<Address>(enemy, 0xdc0)) return option;
    auto distance = pr::distance(read<Vec3>(pr::scope.actor, 0x74), read<Vec3>(s, 0x28)) / w.length;
    if (!(distance > 35) || !quiet(w, pr::scope.actor)) return option;
    bool armored = pr::armoured_class(enemy);
    bool isolated = false;
    Options choices;
    if (armored) {
        auto k = shell(weapon, selected_ammo(weapon, enemy));
        if (k == p::Shell::unavailable || open_top(enemy)) return option;
        choices = vehicle_options(w, brain, s, priority(), false);
    } else if (distance > 40 && pr::row_kind(weapon) == pr::RowKind::main_gun && (read<Byte>(enemy, 0x30) & 4))
        isolated = stray(pr::scope.assignment, s, infantry(enemy), distance);
    return p::fire_option(mode, option, true, distance, isolated, armored, choices.all,
                          choices.capable && choices.complete);
}
inline void set_target(Address weapon, Address target, int option, int mode, int shots) {
    Guard guard;
    const int next = accepting.load() ? option_for(weapon, target, option, mode,
                                                   reinterpret_cast<Address>(__builtin_return_address(0)))
                                      : option;
    if (next != option) count(holds, "aim-only");
    original_target(weapon, target, next, mode, shots);
}
} // namespace detail
inline void deactivate() {
    detail::accepting.store(false);
}
inline unsigned inflight_count() {
    return detail::inflight.load();
}
inline void after_hooks_removed() {
    deactivate();
    pr::rate_armor = nullptr;
    pr::select_armor = nullptr;
    detail::original_target = nullptr;
    detail::original_volume = nullptr;
    detail::staged = false;
    detail::cache.used = detail::cache.rays = 0;
    detail::volumes = 0;
    detail::holds = 0;
    detail::shells = 0;
}
template <class Create, class Remove>
inline bool prepare_hooks(Address base, const char* digest, Create create, Remove remove) {
    using namespace detail;
    if (!base || !digest || std::strcmp(digest, target_prio::executable_sha256) != 0 || staged) return false;
    std::array<void*, hook_count> installed{};
    unsigned n = 0;
    auto add = [&](Address rva, auto hook, auto& orig) {
        auto entry = reinterpret_cast<void*>(base + rva);
        void* trampoline{};
        if (create(entry, reinterpret_cast<void*>(hook), &trampoline) != 0) return false;
        installed[n++] = entry;
        orig = reinterpret_cast<decltype(hook)>(trampoline);
        return trampoline != nullptr;
    };
    staged = true;
    if (!add(0x92B6F0, &select_volume, original_volume) || !add(0x81F0C0, &set_target, original_target)) {
        bool removed = true;
        while (n)
            if (remove(installed[--n]) != 0) removed = false;
        if (removed) after_hooks_removed();
        return false;
    }
    pr::rate_armor = &rate;
    pr::select_armor = &select_ammo;
    accepting = true;
    return true;
}
} // namespace goh::penetration_at
