#pragma once

#include "tank_auto_repair_module.hpp"
#include "tank_crew_fill_policy.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>

namespace goh::tank_crew_fill {
namespace sim = goh::auto_smoke;
namespace support = goh::ai_support;
namespace repair = goh::tank_auto_repair;
namespace pol = goh::tank_crew_fill::policy;
using sim::Address;

inline constexpr const char* executable_sha256 = runtime::kBuild26734796Sha256;
static_assert(std::string_view(repair::executable_sha256) == executable_sha256);
inline constexpr bool runtime_allowed = false;
inline constexpr unsigned hook_count = 0;

inline void (*log_sink)(const char* fmt, ...)
#if defined(__MINGW_PRINTF_FORMAT)
    __attribute__((format(__MINGW_PRINTF_FORMAT, 1, 2)))
#endif
    = nullptr;

inline std::atomic<unsigned> stat_transfers{}, stat_transfers_crew_breed{}, stat_transfers_infantry{},
    stat_transfer_failures{}, stat_deferred_repair{}, stat_deferred_emit{};

namespace detail {
using sim::read;
using sim::World;
using support::Span;
using support::span;
using Byte = std::uint8_t;
using Tick = std::uint32_t;

inline Address base{};
inline std::atomic<bool> active{};
inline std::atomic<int> inflight{};
inline bool coordinating{};
inline Address last_scene{};
inline Tick last_clock{};

template <class Fn> inline Fn native(Address rva) {
    return reinterpret_cast<Fn>(base + rva);
}

constexpr Address human_vt = 0x110FF88;
constexpr Address vehicle_vt = 0x101FD68;
constexpr Address emit_vt = 0x110B808;
constexpr Address smart_vt = 0x10225B0;
constexpr Address repair_vt = 0x110D0E8;
constexpr Address exchange_vt = 0x10224F8;
constexpr Address board_vt = 0x110B590;
constexpr Address mechanic_info_vt = 0x107F0F8;
constexpr Address medic_info_vt = 0x107EEA8;
constexpr Address vet_test_a = 0x1B51C0;
constexpr Address vet_test_b = 0x368AC0;

struct Ops {
    Byte (*is_worker)(){};
    Byte (*is_remote_client)(){};
    bool (*read_world)(Address, World&){};
    bool (*allies_fn)(Address, unsigned, unsigned){};
    Address (*boarder_root)(Address){};
    Address (*find_node)(Address, const short*){};
    Byte (*boarder_move)(Address, Address, const short*, Address, const short*){};
    Byte (*can_access)(Address, const char*){};
    unsigned (*vet_level)(Address){};
    std::size_t (*returning_crew)(Address, Address*, std::size_t){};
};

inline Ops ops{};

inline void bind_native_ops() {
    ops.is_worker = native<Byte (*)()>(0xA7A80);
    ops.is_remote_client = native<Byte (*)()>(0xC2B7A0);
    ops.read_world = &sim::world;
    ops.allies_fn = &sim::allies;
    ops.boarder_root = native<Address (*)(Address)>(0x79D790);
    ops.find_node = native<Address (*)(Address, const short*)>(0x79E930);
    ops.boarder_move = native<Byte (*)(Address, Address, const short*, Address, const short*)>(0x7A0240);
    ops.can_access = native<Byte (*)(Address, const char*)>(0x80F710);
    ops.vet_level = native<unsigned (*)(Address)>(0x94F3D0);
    ops.returning_crew = &repair::collect_returning_crew;
}

struct Guard {
    Guard() {
        runtime::global_inflight.fetch_add(1, std::memory_order_acq_rel);
        ++inflight;
    }
    ~Guard() {
        --inflight;
        runtime::global_inflight.fetch_sub(1, std::memory_order_acq_rel);
    }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
};

struct TankState {
    support::Identity entity{};
    std::uint16_t route{};
    Tick last_tick{};
    Tick emit_until{};
    Tick retry_until{};
    Tick defer_stat_until{};
    bool serviced{};
    bool matches(Address a) const {
        return entity.ptr && entity.matches(a) && read<std::uint16_t>(a, 0xDAA) == route;
    }
};

inline store::GrowableTable<TankState, 64, 4096> states{};

inline void reset() {
    states.clear();
    last_scene = 0;
    last_clock = 0;
}

inline bool pending(Tick deadline, Tick now) {
    return deadline && static_cast<std::int32_t>(deadline - now) > 0;
}

inline TankState* state_for(Address tank, Tick now) {
    TankState* empty = nullptr;
    TankState* oldest = nullptr;
    for (auto& s : states) {
        if (s.matches(tank)) return &s;
        if (!s.entity.ptr && !empty) empty = &s;
        if (!oldest || static_cast<std::int32_t>(s.last_tick - oldest->last_tick) < 0) oldest = &s;
    }
    TankState* slot = empty ? empty : states.append_slot();
    if (!slot) slot = oldest;
    *slot = {};
    slot->entity = support::identity(tank);
    slot->route = read<std::uint16_t>(tank, 0xDAA);
    slot->last_tick = now;
    return slot;
}

inline bool has_ability(Address a, Address index) {
    const auto bit = read<unsigned>(base, index);
    return bit < 32 && (read<Byte>(a, 0xDAD + bit / 8) & (1u << (bit % 8)));
}

inline bool has_seated_actor(Address a) {
    return a && (read<Byte>(a, 0x30) & 2) && !(read<unsigned>(a, 0x38) & 0x820u);
}

inline Address brain_of(Address a, Address table, bool allow_seated = false) {
    if (!(allow_seated ? has_seated_actor(a) : sim::has_actor(a))) return 0;
    const auto b = read<Address>(a, 0xDC8);
    return b && read<Address>(b) == base + table && read<Address>(b, 0x38) == a ? b : 0;
}

inline bool manual_tank(Address a, Address b) {
    const auto ch = read<Address>(a, 0xDE0), wp = read<Address>(a, 0xDD8);
    return !ch || !wp || read<Address>(ch, 0x10) != a || read<unsigned>(ch, 0x18) || (read<Byte>(wp, 0x70) & 2) ||
           (read<unsigned>(b, 0x39C) & 0x3fu);
}

inline bool available_seat(Address human, Address tank, Address b) {
    const auto ch = read<Address>(human, 0xDE0), wp = read<Address>(human, 0xDD8);
    const auto flags = read<unsigned>(b, 0x39C), policy = read<unsigned>(b, 0x398);
    if (!ch || !wp || read<Address>(ch, 0x10) != human || read<unsigned>(ch, 0x18) || (read<Byte>(wp, 0x70) & 2) ||
        (flags & (1u << 28)) || (policy & 3u) > 1u || ((policy >> 12) & 3u) > 2u)
        return false;
    if (!(flags & 0x3fu)) return true;
    const auto link = read<Address>(human, 0x560);
    return (flags & 0x3fu) == 1u && (flags & (1u << 19)) && link && read<Address>(link, 8) == tank;
}

inline unsigned vitality(Address a) {
    const auto agg = read<Address>(a, 0x578), vit = agg ? read<Address>(agg, 0x18) : 0;
    return vit ? read<Byte>(vit, 0x174) & 3u : 3u;
}

inline bool eligible_tank(Address a, Address& out_brain) {
    if (!sim::has_actor(a) || !(read<Byte>(a, 0x30) & 8) || support::owner(a) >= 18 || !read<std::uint16_t>(a, 0xDAA))
        return false;
    const auto b = brain_of(a, vehicle_vt);
    if (!b || manual_tank(a, b) || (read<unsigned>(b, 0x398) & 3u) > 1u) return false;
    if (has_ability(a, 0x15C6A58) || has_ability(a, 0x15C6A5C) || !(read<float>(a, 0x70) >= 0.34202014f)) return false;
    const auto tags = span(a, 0x4E8);
    const auto def = read<Address>(a, 0x548);
    if (!def || !tags.valid(0x60, 4096)) return false;
    for (const char* name : {"destroyed", "exploded", "abandoned"})
        if (support::tag(tags, name) || support::tag(span(def, 0x330), name)) return false;
    const auto type = support::native_string(a + 0xDE8 + 0x40);
    const bool tank_class = classify::is_tank_or_bullet_class(type) || support::tag(span(def, 0x330), "tank");
    if (!tank_class) return false;
    const auto rows = span(a, 0xB90);
    if (!rows.valid(0x28, 1024)) return false;
    int body_state = -1;
    bool has_tracks = false;
    for (auto p = rows.first; p != rows.end; p += 0x28) {
        const auto cdef = read<Address>(p);
        if (!cdef || read<Address>(p, 8) != a) return false;
        const auto names = span(cdef, 0x328);
        if (!names.valid(0x60, 4096) || names.first == names.end) continue;
        const auto n = support::native_string(names.first + 0x40);
        if (n == "body") body_state = read<Byte>(p, 0x24);
        if (n == "trackleft" || n == "trackright" || support::tag(names, "track")) has_tracks = true;
    }
    if (!has_tracks || body_state != 0) return false;
    out_brain = b;
    return true;
}

struct EmitStatus {
    bool active{};
    bool crew_or_all{};
};

inline EmitStatus inspect_emit_order(Address tank, Address brain) {
    EmitStatus st{};
    auto o = read<Address>(brain, 0xF0);
    for (unsigned depth = 0; o && depth < 32; ++depth, o = sim::orders::active_child(base, o)) {
        if (read<Address>(o, 8) != tank + 0xAF0) break;
        if (read<Address>(o) == base + emit_vt) {
            st.active = true;
            if (read<int>(o, 0x5C) != 1) st.crew_or_all = true;
            break;
        }
    }
    return st;
}

inline bool has_exit_walker(Span walkers) {
    for (auto p = walkers.first; p != walkers.end; p += 0x90) {
        const auto actor = read<Address>(p, 8);
        const auto final_node = read<Address>(p, 0x30);
        if (actor && final_node && read<int>(final_node, 4) == 1) return true;
    }
    return false;
}

inline bool is_walking(Span walkers, Address human) {
    for (auto p = walkers.first; p != walkers.end; p += 0x90)
        if (read<Address>(p, 8) == human) return true;
    return false;
}

inline bool has_any_reservation(Span seats, Address human) {
    for (auto p = seats.first; p != seats.end; p += 0x298)
        if (read<Address>(p, 0x290) == human) return true;
    return false;
}

inline bool is_place_node(Address boarder, short slot_index) {
    if (!ops.find_node || slot_index < 0) return false;
    const auto node = ops.find_node(boarder, &slot_index);
    return node && read<int>(node, 4) == 2;
}

inline bool targeting_tank_for_crew_work(Address human, Address tank) {
    const auto b = brain_of(human, human_vt);
    auto o = b ? read<Address>(b, 0xF0) : 0;
    for (unsigned i = 0; o && i < 32; ++i, o = sim::orders::active_child(base, o)) {
        if (read<Address>(o, 8) != human + 0xAF0) break;
        const auto table = read<Address>(o) - base;
        if (table == smart_vt && read<Address>(o, 0x58) == tank) return true;
        if (table == repair_vt && read<Address>(o, 0x68) == tank) return true;
        if (table == exchange_vt && read<Address>(o, 0x38) == tank) return true;
        if (table == board_vt && read<Address>(o, 0x58) == tank) return true;
    }
    return false;
}

inline unsigned count_expected_returning_crew(const World& w, Address tank) {
    std::array<Address, 64> seen{};
    std::size_t seen_count = ops.returning_crew ? ops.returning_crew(tank, seen.data(), seen.size()) : 0;
    unsigned total = static_cast<unsigned>(seen_count);
    support::actors(w, [&](Address h) {
        if (!(read<Byte>(h, 0x30) & 4) || (read<unsigned>(h, 0x1C0) & (1u << 28)) || vitality(h) != 0) return;
        const auto end = seen.begin() + static_cast<std::ptrdiff_t>(seen_count);
        if (std::find(seen.begin(), end, h) != end) return;
        if (targeting_tank_for_crew_work(h, tank)) {
            if (seen_count < seen.size()) seen[seen_count++] = h;
            ++total;
        }
    });
    return total;
}

inline void inspect_breed_traits(Address human, pol::PassengerFacts& out) {
    const auto actor_tags = span(human, 0x4E8);
    if (support::tag(actor_tags, "crew")) out.has_crew_tag = true;
    const auto breed = read<Address>(human, 0x1390);
    if (!breed) return;
    const auto breed_tags = span(breed, 0x440);
    if (support::tag(breed_tags, "crew")) out.has_crew_tag = true;
    // Prefer dynamic live inventory inspection (protects scavenged AT weapons
    // and avoids demoting ex-AT soldiers whose launcher/rockets are spent);
    // fall back to breed AT tags only when no inventory box is populated.
    const auto carried = classify::inspect_carried_at_inventory(human, base);
    if (carried.inventory_present) {
        out.has_at_tag = carried.has_at_weapon;
    } else if (breed_tags.valid(0x60, 4096)) {
        for (Address row = breed_tags.first; row < breed_tags.end; row += 0x60) {
            if (classify::is_at_breed_tag(classify::gem_string(row + 0x40, 128))) {
                out.has_at_tag = true;
                break;
            }
        }
    }
    const auto abilities = span(breed + 0x528, 0x20);
    const auto levels = span(breed + 0x560, 0x20);
    if (!abilities.valid(8, 1024)) return;
    bool vet_checked = false;
    bool vet_ok = false;
    unsigned vet = 0;
    auto check_vet = [&](unsigned required) -> bool {
        if (!required) return true;
        if (!vet_checked) {
            vet_checked = true;
            if (read<Address>(human, 0x1178) && levels.valid(8, 1024) && ops.vet_level) {
                bool valid_levels = true;
                for (auto q = levels.first; q != levels.end; q += 8) {
                    const auto row = read<Address>(q);
                    const auto ability = row ? read<Address>(row, 8) : 0;
                    if (!ability) {
                        valid_levels = false;
                        break;
                    }
                    const auto test = read<Address>(read<Address>(ability), 0x28);
                    if (test != base + vet_test_a && test != base + vet_test_b) {
                        valid_levels = false;
                        break;
                    }
                }
                if (valid_levels) {
                    vet = ops.vet_level(human);
                    vet_ok = true;
                }
            }
        }
        return vet_ok && vet >= required;
    };
    for (auto p = abilities.first; p != abilities.end; p += 8) {
        const auto info = read<Address>(p);
        if (!info) continue;
        const auto vt = read<Address>(info);
        if (vt == base + mechanic_info_vt) {
            if (check_vet(read<unsigned>(info, 0x18))) {
                const float rate = read<float>(info, 0x1C);
                if (std::isfinite(rate) && rate > 0.f) {
                    if (!out.is_mechanic || rate > out.mechanic_rate) out.mechanic_rate = rate;
                    out.is_mechanic = true;
                }
            }
        } else if (vt == base + medic_info_vt) {
            out.is_medic = true;
        }
    }
}

struct HullOccupant {
    short slot_index{-1};
    Address human{};
    std::string_view slot_name;
};

inline void service_tank(Address tank, Address tank_brain, const World& w) {
    auto* st = state_for(tank, w.now);
    if (st->serviced && st->last_tick == w.now) return;
    st->last_tick = w.now;
    st->serviced = true;

    const auto emit_st = inspect_emit_order(tank, tank_brain);
    if (emit_st.crew_or_all) st->emit_until = w.now + w.duration(config::active().tuning.crew_fill_emit_backoff_s);
    if (emit_st.active) {
        if (!pending(st->defer_stat_until, w.now)) {
            ++stat_deferred_emit;
            st->defer_stat_until = w.now + w.duration(5);
        }
        return;
    }

    const auto placer = read<Address>(tank, 0x568);
    const auto boarder = read<Address>(tank, 0x570);
    if (!placer || read<Address>(placer, 0x40) != tank || !read<Byte>(placer, 0x49)) return;
    if (!boarder || read<Address>(boarder, 0x58) != tank || !ops.boarder_root || !ops.boarder_move) return;
    const auto root = ops.boarder_root(boarder);
    if (root != boarder) return;

    const auto seats = span(placer, 0x28);
    const auto occupied = span(placer, 0x10);
    const auto walkers = span(root, 0x60);
    if (!seats.valid(0x298, 4096) || !occupied.valid(2, 4096) || !walkers.valid(0x90, 4096)) return;

    const bool placer_blocked = read<Byte>(placer, 0x48) != 0;
    const bool emit_cooldown = pending(st->emit_until, w.now);
    const bool exit_walker = has_exit_walker(walkers);
    if (placer_blocked || emit_cooldown || exit_walker || pending(st->retry_until, w.now)) return;

    const unsigned tank_player = support::owner(tank);
    const unsigned tank_policy = read<unsigned>(tank_brain, 0x398);
    const std::size_t slot_count = (seats.end - seats.first) / 0x298;

    unsigned available_crew_slots = 0;
    bool has_best_dst = false;
    pol::SlotFacts best_dst{};
    bool any_crew_inside = false;
    bool same_player_crew_inside = false;

    std::array<HullOccupant, 64> hull_occupants{};
    std::size_t hull_count = 0;

    for (std::size_t idx = 0; idx < slot_count && idx <= 32767; ++idx) {
        const Address slot = seats.first + idx * 0x298;
        const Address occupant = read<Address>(slot, 0x280);
        const auto sname = support::native_string(slot + 0x40);
        const auto sgroup = support::native_string(slot + 0x60 + 0x40);

        pol::SlotFacts facts{};
        facts.name = sname;
        facts.group = sgroup;
        facts.flags = read<unsigned>(slot, 0x248);
        facts.has_visor = read<std::size_t>(slot, 0x110) != 0;
        facts.has_link_bone_name = read<std::size_t>(slot, 0x170) != 0;
        facts.has_resolved_bone = read<Address>(slot, 0x240) != 0;
        facts.occupied = occupant != 0;
        facts.reserved = read<Address>(slot, 0x290) != 0;
        facts.slot_index = static_cast<std::int16_t>(idx);

        if (facts.group == "crew" && occupant != 0) {
            any_crew_inside = true;
            if (support::owner(occupant) == tank_player) same_player_crew_inside = true;
        }

        if (pol::is_crew_destination(facts) && is_place_node(boarder, facts.slot_index)) {
            facts.is_cannon_gunner = repair::detail::cannon_gunner_seat(tank, facts.name);
            ++available_crew_slots;
            if (!has_best_dst || pol::better_crew_slot(facts, best_dst)) {
                best_dst = facts;
                has_best_dst = true;
            }
        } else if (pol::is_hull_passenger_source(facts) && hull_count < hull_occupants.size()) {
            hull_occupants[hull_count++] = {facts.slot_index, occupant, sname};
        }
    }

    if (!available_crew_slots || !has_best_dst || !hull_count) return;

    bool has_best_src = false;
    pol::PassengerFacts best_src_facts{};
    HullOccupant best_src{};

    for (std::size_t i = 0; i < hull_count; ++i) {
        const auto& cand = hull_occupants[i];
        const Address h = cand.human;
        if (!h || !(read<Byte>(h, 0x30) & 4) || !has_seated_actor(h) || !read<std::uint16_t>(h, 0xDAA)) continue;
        const Address hb = brain_of(h, human_vt, true);
        if (!hb || vitality(h) != 0 || !available_seat(h, tank, hb)) continue;
        if (is_walking(walkers, h) || has_any_reservation(seats, h)) continue;
        if (!is_place_node(boarder, cand.slot_index)) continue;

        const unsigned h_player = support::owner(h);
        if (h_player >= 18) continue;
        const bool same_player = (h_player == tank_player);
        const bool same_control = ((read<unsigned>(hb, 0x398) & 3u) == (tank_policy & 3u));

        if (!same_player) {
            // Never let an allied passenger take over a crewless tank and flip ownership.
            if (!same_player_crew_inside || !ops.allies_fn || !ops.allies_fn(base, tank_player, h_player)) continue;
            const char hp = static_cast<char>(h_player);
            if (!ops.can_access || !ops.can_access(tank, &hp)) continue;
        } else if (!same_control && !any_crew_inside) {
            continue;
        }

        pol::PassengerFacts pf{};
        pf.same_player = same_player;
        pf.slot_index = cand.slot_index;
        pf.entity_id = read<std::uint32_t>(h, 0xC0);
        inspect_breed_traits(h, pf);

        if (!has_best_src || pol::better_passenger(pf, best_src_facts)) {
            best_src_facts = pf;
            best_src = cand;
            has_best_src = true;
        }
    }

    if (!has_best_src) return;

    const unsigned returning = count_expected_returning_crew(w, tank);
    const pol::TransferGate gate{available_crew_slots, returning,     has_best_src, placer_blocked,
                                 emit_st.active,       emit_cooldown, exit_walker};
    if (!pol::can_transfer(gate)) {
        if (returning > 0 && !pending(st->defer_stat_until, w.now)) {
            ++stat_deferred_repair;
            st->defer_stat_until = w.now + w.duration(5);
        }
        return;
    }

    short src_place = best_src.slot_index;
    short dst_place = best_dst.slot_index;
    const Byte moved = ops.boarder_move(boarder, tank, &src_place, tank, &dst_place);
    if (!moved) {
        st->retry_until = w.now + w.duration(1);
        ++stat_transfer_failures;
        return;
    }

    const unsigned n = ++stat_transfers;
    const int tier = pol::passenger_tier(best_src_facts);
    if (tier <= 3)
        ++stat_transfers_crew_breed;
    else
        ++stat_transfers_infantry;
    if (log_sink) {
        log_sink("TANK CREW FILL #%u: tank=0x%llx human=0x%llx %.*s(%d) -> %.*s(%d) tier=%d freeCrew=%u returning=%u",
                 n, static_cast<unsigned long long>(tank), static_cast<unsigned long long>(best_src.human),
                 static_cast<int>(best_src.slot_name.size()), best_src.slot_name.data(), static_cast<int>(src_place),
                 static_cast<int>(best_dst.name.size()), best_dst.name.data(), static_cast<int>(dst_place), tier,
                 available_crew_slots, returning);
    }
}

} // namespace detail

inline bool init(Address image, const char* measured_sha256) {
    using namespace detail;
    if (base || !image || !measured_sha256 || std::strcmp(measured_sha256, executable_sha256) != 0) return false;
    base = image;
    bind_native_ops();
    reset();
    stat_transfers = stat_transfers_crew_breed = stat_transfers_infantry = stat_transfer_failures =
        stat_deferred_repair = stat_deferred_emit = 0;
    active.store(true, std::memory_order_release);
    return true;
}

inline void on_actor_post_quant(Address actor) {
    using namespace detail;
    Guard guard;
    if (!active.load(std::memory_order_acquire) || !config::active().modules.tank_crew_fill || !actor ||
        !ops.is_worker || !ops.is_remote_client || !ops.read_world || ops.is_worker())
        return;
    if (coordinating) return;
    World w{};
    if (ops.is_remote_client() || !ops.read_world(base, w)) {
        if (last_scene) reset();
        return;
    }
    if (w.scene != last_scene || w.now < last_clock) {
        reset();
        last_scene = w.scene;
    }
    last_clock = w.now;

    Address tank_brain = 0;
    if (!eligible_tank(actor, tank_brain)) return;

    struct Scope {
        Scope() {
            coordinating = true;
        }
        ~Scope() {
            coordinating = false;
        }
    } scope;
    service_tank(actor, tank_brain, w);
}

inline void deactivate() noexcept {
    detail::active.store(false, std::memory_order_release);
}

inline int inflight_count() noexcept {
    return detail::inflight.load(std::memory_order_acquire);
}

inline void after_hooks_removed() {
    using namespace detail;
    deactivate();
    reset();
    states.release();
    base = 0;
    ops = {};
}

} // namespace goh::tank_crew_fill
