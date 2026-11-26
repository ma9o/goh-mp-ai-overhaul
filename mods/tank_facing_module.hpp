#pragma once

#include "tank_auto_repair_module.hpp"
#include "tank_facing_policy.hpp"
#include "target_prioritization_module.hpp"

namespace goh::tank_facing {
namespace sim = goh::auto_smoke;
namespace support = goh::ai_support;
namespace repair = goh::tank_auto_repair;
namespace pol = policy;
using runtime::Address;
inline constexpr const char* executable_sha256 = runtime::kBuild26734796Sha256;
inline constexpr bool runtime_allowed = false;
inline constexpr unsigned hook_count = 0;
inline std::atomic<unsigned> stat_rotations{}, stat_retargets{}, stat_timeouts{};

namespace detail {
using pol::ThreatTier;
using runtime::Byte;
using runtime::read;
using runtime::Tick;
using runtime::XY;
using sim::Vec3;
using sim::World;
using support::span;
inline Address base{};
inline runtime::HookGate gate{};
inline Address last_scene{};
inline Tick last_clock{};
inline bool issuing{};

inline Address make_rotate(const Vec3& point) {
    runtime::PointTarget target(base, reinterpret_cast<Address>(&point));
    return runtime::native<Address (*)(Address, float)>(base, runtime::table().fn.rotate_factory)(target.ptr(), 0.f);
}
inline void set_order(Address brain, Address order) {
    const auto setter = read<Address>(read<Address>(brain), 13 * sizeof(Address));
    reinterpret_cast<void (*)(Address, Address)>(setter)(brain, order);
}
struct Ops {
    bool (*is_worker)(Address) = runtime::is_quant_worker;
    bool (*is_remote)(Address) = runtime::is_remote_client;
    bool (*world)(Address, World&) = sim::world;
    bool (*allies)(Address, unsigned, unsigned) = sim::allies;
    bool (*repairing)(Address) = repair::has_active_episode;
    bool (*recently_hit)(const World&, Address) = sim::recently_hit;
    Address (*make_rotate)(const Vec3&) = detail::make_rotate;
    void (*set_order)(Address, Address) = detail::set_order;
    void (*discard)(Address) = support::discard;
};
inline Ops ops{};

struct TankFacingState {
    support::Identity id{};
    Address brain{}, active_order{};
    unsigned player{};
    float active_target_yaw{};
    Vec3 issued_point{};
    Tick issued_tick{}, timeout_until{}, cooldown_until{}, backoff_until{};
    Tick last_scan{}, threat_deadline{}, last_seen{};
    XY last_origin{};
    bool scanned{}, sampled_origin{}, translating{};
};
inline store::GrowableTable<TankFacingState, 64, 4096> states{};
inline void reset() {
    states.clear();
    last_scene = 0;
    last_clock = 0;
}
inline TankFacingState& state_for(Address actor, Address brain, Tick now) {
    TankFacingState* free_slot = nullptr;
    TankFacingState* oldest = nullptr;
    const auto id = support::identity(actor);
    for (auto& st : states) {
        if (st.id.ptr == actor) {
            if (st.id.matches(actor) && st.id.mid == id.mid && st.brain == brain &&
                st.player == support::owner(actor)) {
                return st;
            }
            free_slot = &st;
            break;
        }
        if (!st.id.ptr) free_slot = &st;
        if (!oldest || st.last_seen < oldest->last_seen) oldest = &st;
    }
    if (!free_slot) free_slot = states.append_slot();
    if (!free_slot) free_slot = oldest;
    *free_slot = {};
    free_slot->id = id;
    free_slot->brain = brain;
    free_slot->player = support::owner(actor);
    free_slot->last_seen = now;
    return *free_slot;
}

inline bool mobile_hull(Address a) {
    const auto rows = span(a, 0xB90);
    if (!rows.valid(0x28, 1024)) return false;
    bool body = false;
    for (auto p = rows.first; p != rows.end; p += 0x28) {
        const auto def = read<Address>(p);
        if (!def || read<Address>(p, 8) != a) return false;
        const auto names = span(def, 0x328);
        if (!names.valid(0x60, 4096)) return false;
        if (names.first == names.end) continue;
        const auto name = support::native_string(names.first + 0x40);
        body |= name == "body";
        if (read<Byte>(p, 0x24) &&
            (name == "body" || name == "engine" || name.substr(0, 5) == "track" || support::tag(names, "track")))
            return false;
    }
    return body;
}
inline bool has_driver(Address a) {
    const auto placer = read<Address>(a, 0x568);
    if (!placer || read<Address>(placer, 0x40) != a || read<Byte>(placer, 0x48)) return false;
    const auto seats = span(placer, 0x28);
    if (!seats.valid(0x298, 4096)) return false;
    for (auto p = seats.first; p != seats.end; p += 0x298) {
        const auto h = read<Address>(p, 0x280);
        if (h && support::native_string(p + 0x40) == "driver" && repair::detail::has_seated_actor(h) &&
            (read<Byte>(h, 0x30) & 4) && !repair::detail::vitality(h))
            return true;
    }
    return false;
}
inline bool free_to_turn(Address a, Address b, const World& w) {
    const unsigned flags = read<unsigned>(b, 0x39C);
    if ((flags & ((1u << 6) | (1u << 9) | (1u << 28) | (1u << 29))) || read<int>(b, 0x88) > 0 ||
        read<Address>(b, 0x98) != read<Address>(b, 0xA0) || !(read<float>(b, 0x3D4) < 0.f) ||
        (read<unsigned>(a, 0x1C0) & (1u << 4)) || read<Byte>(a, 0xBAA))
        return false;
    return !support::extender(a, runtime::table().vt.cannon_ext, base) && mobile_hull(a) && has_driver(a) &&
           !ops.repairing(a) &&
           coord::instance().can_act(a, coord::Module::tank_facing, coord::Priority::tank_facing,
                                     read<unsigned>(a, 0xC0), w.now);
}

inline Address main_target(Address brain) {
    const auto rows = span(brain, 0x3B8);
    if (!rows.valid(0x1B8, 256) || rows.first == rows.end) return 0;
    const auto weapon = read<Address>(rows.first, 8);
    if (weapon && read<Address>(weapon, 0x90) == read<Address>(brain, 0x38) && read<int>(weapon, 0xD0) == 1)
        return read<Address>(weapon, 0xE0);
    return read<Byte>(rows.first, 1) && read<int>(rows.first, 0x20) == 1 ? read<Address>(rows.first, 0x30) : 0;
}

// Guns without named damage components can still identify their mounted weapon through the weaponry list.
inline ThreatTier mounted_threat(Address a, bool cannon) {
    const auto rows = span(a, 0xB90);
    if (!rows.valid(0x28, 1024)) return ThreatTier::none;
    bool has_gun = false, working = false;
    for (auto p = rows.first; p != rows.end; p += 0x28) {
        const auto def = read<Address>(p);
        if (!def || read<Address>(p, 8) != a) return ThreatTier::none;
        if (repair::detail::gun(repair::detail::component_name(def))) {
            has_gun = true;
            working |= !read<Byte>(p, 0x24);
        }
    }
    if (has_gun && !working) return ThreatTier::none;
    const auto weapons = span(read<Address>(a, 0xDD8), 0x30);
    if (!weapons.valid(sizeof(Address), 256)) return ThreatTier::none;
    for (auto p = weapons.first; p != weapons.end; p += sizeof(Address)) {
        const auto weapon = read<Address>(p);
        const auto def = read<Address>(weapon, 0x98);
        if (!def || read<Address>(weapon, 0x90) != a) continue;
        working = true;
        const float calibre = read<float>(def, 0x1628);
        if (cannon && std::isfinite(calibre) && calibre >= 25.f) return ThreatTier::heavy_at;
    }
    return working ? ThreatTier::light_at : ThreatTier::none;
}

inline ThreatTier classify_contact(Address e, float rate, float metres, bool attacking, unsigned player) {
    if (!e) {
        if (rate >= 1.f) return ThreatTier::heavy_at;
        return rate > 0.1f ? ThreatTier::light_at : ThreatTier::none;
    }
    if (!sim::has_actor(e) || !(read<unsigned>(e, 0xDB4) & 0x100u) || support::owner(e) >= 18 ||
        ops.allies(base, player, support::owner(e)))
        return ThreatTier::none;
    if (support::has_destruction_tag(e)) return ThreatTier::none;
    const auto type = read<Byte>(e, 0x30);
    if (type & 4) {
        if (repair::detail::vitality(e)) return ThreatTier::none;
        if (rate > 0.1f || (metres <= 60.f && classify::inspect_carried_at_inventory(e, base).has_at_weapon))
            return ThreatTier::light_at;
        return metres <= 18.f && attacking ? ThreatTier::close_infantry : ThreatTier::none;
    }
    if (!(type & 8) || !read<Address>(e, 0x548)) return ThreatTier::none;
    const auto tags = span(e, 0x4E8);
    if (support::tank_class(e) || support::tag(tags, "tank")) {
        if (config::active().modules.tank_target_memory && target_prio::detail::defanged_armor(e))
            return ThreatTier::none;
        return ThreatTier::heavy_at;
    }
    const bool cannon = support::tag(tags, "cannon") || support::tag(span(read<Address>(e, 0x548), 0x330), "cannon") ||
                        support::extender(e, runtime::table().vt.cannon_ext, base);
    if (cannon || rate > 0.1f || classify::is_armored_vehicle_class(support::native_string(e + 0xDE8 + 0x40)))
        return mounted_threat(e, cannon);
    return ThreatTier::none;
}

struct Observation {
    pol::ThreatSample threat{};
    Address entity{};
    Vec3 point{};
    Tick timestamp{};
    float weight{};
};
struct Contacts {
    std::array<Observation, 16> rows{};
    std::size_t count{};

    void add(Observation o, float scale) {
        std::size_t weakest = 0;
        for (std::size_t i = 0; i < count; ++i) {
            auto& old = rows[i];
            const bool same =
                old.entity && o.entity ? old.entity == o.entity : support::within(old.point, o.point, 4.f * scale);
            if (same) {
                const bool newer =
                    o.timestamp > old.timestamp || (o.timestamp == old.timestamp && o.weight > old.weight);
                if (o.threat.visible != old.threat.visible ? o.threat.visible : newer) old = o;
                return;
            }
            if (old.weight < rows[weakest].weight) weakest = i;
        }
        if (count < rows.size())
            rows[count++] = o;
        else if (o.weight > rows[weakest].weight)
            rows[weakest] = o;
    }
    bool heavy(bool visible_only) const {
        return std::any_of(rows.begin(), rows.begin() + count, [=](const Observation& o) {
            return o.threat.tier == ThreatTier::heavy_at && (!visible_only || o.threat.visible);
        });
    }
};
inline void observe(support::Span list, Contacts& contacts, const World& w, const Vec3& self, unsigned player,
                    Address main, const pol::FacingConfig& cfg) {
    if (!list.valid(sizeof(Address), 4096)) return;
    for (auto p = list.first; p != list.end; p += sizeof(Address)) {
        const auto sub = read<Address>(p);
        if (!sub || read<Byte>(sub, 0x6C)) continue;
        const unsigned relation = read<unsigned>(sub, 0x14), flags = read<unsigned>(sub, 0x10);
        if ((relation & 0x1Eu) != 2 || (flags & (1u << 16))) continue;
        Observation o{};
        o.threat.visible = flags & (1u << 29);
        o.timestamp = read<Tick>(sub, 0x64);
        if (!o.threat.visible && (!(flags & (1u << 28)) || !sim::recent(w, o.timestamp, 10))) continue;
        o.threat.attacking_or_threatening = relation & 0x180;
        if (!pol::stealth_allows(o.threat, cfg)) continue;
        o.point = read<Vec3>(sub, 0x28);
        if (!sim::finite(o.point)) continue;
        const float dx = o.point.x - self.x, dy = o.point.y - self.y;
        o.threat.dist_m = std::hypot(dx, dy) / w.length;
        if (!(o.threat.dist_m > 0.f && o.threat.dist_m <= 200.f)) continue;
        o.entity = read<Address>(sub, 0x18);
        o.threat.tier = classify_contact(o.entity, read<float>(sub, 0x58), o.threat.dist_m,
                                         o.threat.attacking_or_threatening, player);
        if (o.threat.tier == ThreatTier::none) continue;
        o.threat.bearing_rad = std::atan2(dy, dx);
        o.threat.is_main_target = main && o.entity == main;
        o.weight = pol::threat_weight(o.threat);
        contacts.add(o, w.length);
    }
}
inline void face_remembered_armor(Contacts& contacts, const target_prio::detail::ArmorMemory& memory, const World& w,
                                  Vec3 self, const pol::FacingConfig& cfg) {
    if (contacts.heavy(true)) return;
    Observation armor{};
    armor.entity = memory.threat_entity.ptr;
    armor.point = {memory.threat_pos.x, memory.threat_pos.y, memory.threat_pos.z};
    const float dx = armor.point.x - self.x, dy = armor.point.y - self.y;
    armor.threat.dist_m = std::hypot(dx, dy) / w.length;
    if (!(armor.threat.dist_m > 0.f && armor.threat.dist_m <= 200.f)) return;
    armor.threat.tier = ThreatTier::heavy_at;
    armor.threat.bearing_rad = std::atan2(dy, dx);
    armor.timestamp = memory.last_seen_tick;
    if (!pol::stealth_allows(armor.threat, cfg)) return;
    armor.weight = pol::threat_weight(armor.threat);
    // Even several close riflemen together must not pull the hull away during the short hold.
    auto end = std::remove_if(contacts.rows.begin(), contacts.rows.begin() + contacts.count, [](const Observation& o) {
        if (o.threat.tier == ThreatTier::close_infantry) return true;
        if (o.threat.tier != ThreatTier::light_at || !o.threat.visible || !(read<Byte>(o.entity, 0x30) & 4))
            return false;
        const auto equipment = target_prio::detail::infantry_loadout(o.entity);
        return equipment.known && !equipment.carries_at();
    });
    contacts.count = static_cast<std::size_t>(end - contacts.rows.begin());
    contacts.add(armor, w.length);
}
inline Contacts gather(const World& w, Address a, Address b, Vec3 self, const pol::FacingConfig& cfg) {
    Contacts contacts{};
    const auto main = main_target(b);
    const unsigned player = support::owner(a);
    auto sensor = [&](Address observer) {
        const auto s = read<Address>(observer, 0xDD0);
        if (s && read<Address>(s, 0x260) == observer) observe(span(s, 0x280), contacts, w, self, player, main, cfg);
    };
    sensor(a);
    observe(span(b, 0x3A0), contacts, w, self, player, main, cfg);
    if (!contacts.heavy(true)) {
        support::actors(w, [&](Address ally) {
            if (ally != a && support::owner(ally) < 18 && (read<unsigned>(ally, 0xDB4) & 0x100u) &&
                support::within(self, support::pos(ally), 45.f * w.length) &&
                ops.allies(base, player, support::owner(ally)) &&
                (!(read<Byte>(ally, 0x30) & 4) || !repair::detail::vitality(ally)))
                sensor(ally);
        });
    }
    if (config::active().modules.tank_target_memory && target_prio::detail::autonomous_fire(w.base, b)) {
        if (const auto* memory = target_prio::detail::update_armor_memory(w, a, target_prio::detail::span(b, 0x3a0)))
            face_remembered_armor(contacts, *memory, w, self, cfg);
    }
    return contacts;
}
inline bool hull_yaw(Address a, float& yaw) {
    auto forward = read<XY>(read<Address>(a, 0xDE0), 0x5C);
    auto valid = [](XY v) { return runtime::finite(v) && std::hypot(v.x, v.y) >= 1e-4f; };
    if (!valid(forward)) forward = read<XY>(a, 0x50);
    if (!valid(forward)) return false;
    yaw = std::atan2(forward.y, forward.x);
    return true;
}

inline bool owns_order(const TankFacingState& st, Address actor, Address order) {
    if (!order || order != st.active_order || read<Address>(order) != base + runtime::table().vt.rotate_order ||
        read<Address>(order, 8) != actor + 0xAF0 || read<int>(order, 0x48) != 2)
        return false;
    // An intervening player order can reuse the allocation. Compare the copied point too.
    const auto point = read<Vec3>(order, 0x4C);
    return point.x == st.issued_point.x && point.y == st.issued_point.y && point.z == st.issued_point.z;
}
inline void service(const World& w, Address a, Address b, TankFacingState& st) {
    const auto self = support::pos(a);
    if (!st.sampled_origin || w.now != st.last_seen) {
        const XY origin{self.x, self.y};
        st.translating = st.sampled_origin && (origin.x != st.last_origin.x || origin.y != st.last_origin.y);
        st.last_origin = origin;
        st.last_seen = w.now;
        st.sampled_origin = true;
    }
    const auto order = read<Address>(b, 0xF0);
    const bool own = owns_order(st, a, order);
    if (st.active_order && !own) {
        st.active_order = 0;
        st.cooldown_until = w.now + w.duration(2.5f);
    }
    if (order && !own) return;
    if (own && !repair::detail::pending(st.timeout_until, w.now)) {
        st.active_order = 0; // The engine retains ownership and can finish or preempt the stalled turn.
        st.backoff_until = w.now + w.duration(6);
        ++stat_timeouts;
        return;
    }
    if (st.translating || repair::detail::pending(st.backoff_until, w.now)) return;
    const auto deadline = read<Tick>(b, 0x3F8);
    const bool refreshed = deadline != st.threat_deadline && repair::detail::pending(deadline, w.now);
    if (st.scanned && (st.last_scan == w.now || (!refreshed && w.now - st.last_scan < w.duration(0.4f)))) return;
    st.scanned = true;
    st.last_scan = w.now;
    st.threat_deadline = deadline;
    if (!free_to_turn(a, b, w)) return;
    float yaw{};
    if (!sim::finite(self) || !hull_yaw(a, yaw)) return;
    pol::FacingConfig cfg{};
    cfg.min_angle_rad = pol::radians(config::active().tuning.tank_facing_min_angle_deg);
    cfg.hold_fire_mode = ((read<unsigned>(b, 0x398) >> 10) & 3u) == 1u;
    cfg.recently_hit = cfg.hold_fire_mode && (ops.recently_hit(w, a) || support::tag(span(a, 0x4E8), "pen_fx"));
    const auto contacts = gather(w, a, b, self, cfg);
    std::array<pol::ThreatSample, 16> samples{};
    for (std::size_t i = 0; i < contacts.count; ++i)
        samples[i] = contacts.rows[i].threat;
    const auto d = pol::evaluate_facing(yaw, samples.data(), contacts.count, cfg);
    const pol::RotationGate issue{own,
                                  false,
                                  repair::detail::pending(st.cooldown_until, w.now),
                                  contacts.heavy(false),
                                  st.active_target_yaw,
                                  static_cast<float>(w.now - st.issued_tick) / w.ticks};
    if (!pol::can_issue_rotation(d, issue)) return;
    const float range = 50.f * w.length;
    const Vec3 point{self.x + range * std::cos(d.desired_yaw_rad), self.y + range * std::sin(d.desired_yaw_rad),
                     self.z};
    if (!sim::finite(point)) return;
    const auto next = ops.make_rotate(point);
    if (!next) return;
    if (!gate.active() || read<Address>(b, 0xF0) != order || (own && !owns_order(st, a, order)) ||
        support::autonomous_vehicle_brain(base, a) != b || !free_to_turn(a, b, w)) {
        ops.discard(next);
        return;
    }
    ops.set_order(b, next);
    if (read<Address>(b, 0xF0) != next) return;
    st.active_order = next;
    st.active_target_yaw = d.desired_yaw_rad;
    st.issued_point = point;
    st.issued_tick = w.now;
    st.timeout_until = w.now + w.duration(8);
    ++stat_rotations;
    if (own) ++stat_retargets;
}
} // namespace detail

inline bool init(Address image, const char* sha) {
    using namespace detail;
    if (base || !image || !sha || std::strcmp(sha, executable_sha256) != 0) return false;
    base = image;
    reset();
    stat_rotations = stat_retargets = stat_timeouts = 0;
    gate.activate();
    return true;
}
inline void on_actor_post_quant(Address a) {
    using namespace detail;
    runtime::HookGuard guard(gate);
    if (!gate.active() || !config::active().modules.tank_facing || !a || ops.is_worker(base) || issuing) return;
    World w{};
    if (ops.is_remote(base) || !ops.world(base, w)) {
        if (last_scene) reset();
        return;
    }
    if (w.scene != last_scene || w.now < last_clock) {
        reset();
        last_scene = w.scene;
    }
    last_clock = w.now;
    coord::instance().sync_world(w.scene, w.now);
    if (!(read<Byte>(a, 0x30) & 8) || !(read<unsigned>(a, 0xDB4) & 0x100u)) return;
    const auto b = support::autonomous_vehicle_brain(base, a);
    if (!b || read<Address>(read<Address>(a, 0xDE0)) != base + runtime::table().vt.chassis_track ||
        !support::tank_class(a))
        return;
    struct Scope {
        Scope() {
            issuing = true;
        }
        ~Scope() {
            issuing = false;
        }
    } scope;
    service(w, a, b, state_for(a, b, w.now));
}
inline void deactivate() noexcept {
    detail::gate.deactivate();
}
inline int inflight_count() noexcept {
    return static_cast<int>(detail::gate.count());
}
inline void after_hooks_removed() {
    deactivate();
    detail::reset();
    detail::states.release();
    detail::ops = {};
    detail::base = 0;
}

} // namespace goh::tank_facing
