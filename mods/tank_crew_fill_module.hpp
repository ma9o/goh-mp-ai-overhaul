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
    stat_transfers_internal{}, stat_transfers_nontank{}, stat_transfer_failures{}, stat_deferred_repair{},
    stat_deferred_emit{};

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
    struct TransferAttempt {
        unsigned human_id{};
        short src{-1}, dst{-1};
        Tick retry_until{};
        bool retried{};
    };
    support::Identity entity{};
    std::uint16_t route{};
    Tick last_tick{};
    Tick emit_until{};
    Tick retry_until{};
    Tick driver_gunner_swap_until{};
    Tick secondary_gunner_until{};
    Tick defer_stat_until{};
    TransferAttempt last_transfer{};
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

inline bool has_seated_actor(Address a) {
    return a && (read<Byte>(a, 0x30) & 2) && !(read<unsigned>(a, 0x38) & 0x820u);
}

inline Address brain_of(Address a, Address table, bool allow_seated = false) {
    if (!(allow_seated ? has_seated_actor(a) : sim::has_actor(a))) return 0;
    const auto b = read<Address>(a, 0xDC8);
    return b && read<Address>(b) == base + table && read<Address>(b, 0x38) == a ? b : 0;
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

inline bool eligible_vehicle(Address a, Address& out_brain, pol::VehicleTacticalContext& ctx) {
    const auto b = support::autonomous_vehicle_brain(base, a);
    if (!b) return false;
    const auto rows = span(a, 0xB90);
    if (!rows.valid(0x28, 1024)) return false;
    bool has_tracks = false;
    for (auto p = rows.first; p != rows.end; p += 0x28) {
        const auto cdef = read<Address>(p);
        if (!cdef || read<Address>(p, 8) != a) return false;
        const auto names = span(cdef, 0x328);
        if (!names.valid(0x60, 4096)) return false;
        if (names.first == names.end) continue;
        const auto n = support::native_string(names.first + 0x40);
        const bool damaged = read<Byte>(p, 0x24) != 0;
        if (n == "body" && damaged) return false;
        if (n == "gun") ctx.main_gun_broken |= damaged;
        const bool track = n == "trackleft" || n == "trackright" || support::tag(names, "track");
        has_tracks |= track;
        ctx.immobilized |=
            damaged && (n == "engine" || track || n.substr(0, 5) == "track" || n.substr(0, 5) == "wheel");
    }
    ctx.is_tank = has_tracks && support::tank_class(a);
    if (!ctx.is_tank && !config::active().modules.nontank_crew_fill) return false;
    const Address cannon = support::extender(a, runtime::table().vt.cannon_ext, base);
    if (cannon && read<int>(cannon, 0xC30) != 0) return false;
    out_brain = b;
    return true;
}

struct VehicleOrders {
    bool emit_active{};
    bool emit_crew_or_all{};
    bool has_move_order{};
};

inline VehicleOrders inspect_orders(Address tank, Address brain) {
    VehicleOrders st{};
    st.has_move_order = (read<unsigned>(brain, 0x39C) & ((1u << 6) | (1u << 9) | (1u << 28))) != 0;
    auto o = read<Address>(brain, 0xF0);
    for (unsigned depth = 0; o && depth < 32; ++depth, o = sim::orders::active_child(base, o)) {
        if (read<Address>(o, 8) != tank + 0xAF0) break;
        const auto vt = read<Address>(o);
        if (vt == base + runtime::table().vt.move_task_order) st.has_move_order = true;
        if (vt == base + emit_vt) {
            st.emit_active = true;
            st.emit_crew_or_all = read<int>(o, 0x5C) != 1;
            break;
        }
    }
    return st;
}

inline bool has_visible_enemy(Address brain) {
    const auto subjects = span(brain, 0x3A0);
    if (!subjects.valid(sizeof(Address), 16384)) return false;
    for (auto p = subjects.first; p != subjects.end; p += sizeof(Address)) {
        const auto subject = read<Address>(p);
        if (subject && (read<unsigned>(subject, 0x10) & (1u << 29))) return true;
    }
    return false;
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

inline pol::SlotFacts read_slot(Address slot, short index) {
    pol::SlotFacts s{};
    s.name = support::native_string(slot + 0x40);
    s.group = support::native_string(slot + 0xA0);
    s.flags = read<unsigned>(slot, 0x248);
    s.has_visor = read<std::size_t>(slot, 0x110) != 0;
    s.has_link_bone_name = read<std::size_t>(slot, 0x170) != 0;
    s.has_resolved_bone = read<Address>(slot, 0x240) != 0;
    s.occupied = read<Address>(slot, 0x280) != 0;
    s.reserved = read<Address>(slot, 0x290) != 0;
    s.slot_index = index;
    return s;
}

inline bool eligible_occupant(Address human, Address vehicle, Address vehicle_brain, bool crew_inside,
                              bool same_player_crew_inside, Span walkers, Span seats, pol::PassengerFacts& pf) {
    if (!(read<Byte>(human, 0x30) & 4) || !read<std::uint16_t>(human, 0xDAA)) return false;
    const auto hb = brain_of(human, human_vt, true);
    if (!hb || vitality(human) != 0 || !available_seat(human, vehicle, hb)) return false;
    if (is_walking(walkers, human) || has_any_reservation(seats, human)) return false;
    const unsigned player = support::owner(human), vehicle_player = support::owner(vehicle);
    if (player >= 18) return false;
    pf.same_player = player == vehicle_player;
    if (!pf.same_player) {
        // Finish may change vehicle ownership if the allied donor is the first crewman.
        const char hp = static_cast<char>(player);
        if (!same_player_crew_inside || !ops.allies_fn || !ops.allies_fn(base, vehicle_player, player) ||
            !ops.can_access || !ops.can_access(vehicle, &hp))
            return false;
    } else if (!crew_inside && ((read<unsigned>(hb, 0x398) ^ read<unsigned>(vehicle_brain, 0x398)) & 3u)) {
        return false;
    }
    pf.entity_id = read<std::uint32_t>(human, 0xC0);
    inspect_breed_traits(human, pf);
    return true;
}

inline void service_vehicle(Address tank, Address tank_brain, const World& w, pol::VehicleTacticalContext ctx) {
    auto* st = state_for(tank, w.now);
    if (st->serviced && st->last_tick == w.now) return;
    st->last_tick = w.now;
    st->serviced = true;

    const auto orders = inspect_orders(tank, tank_brain);
    if (orders.emit_crew_or_all) st->emit_until = w.now + w.duration(config::active().tuning.crew_fill_emit_backoff_s);
    if (orders.emit_active) {
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
    unsigned available_crew_slots = 0;
    pol::SlotFacts best_dst{}, internal_dst{};
    bool same_player_crew_inside = false;
    for (auto slot = seats.first; slot != seats.end; slot += 0x298) {
        const auto index = static_cast<short>((slot - seats.first) / 0x298);
        auto facts = read_slot(slot, index);
        if ((index == st->last_transfer.src && !facts.occupied) || (index == st->last_transfer.dst && facts.occupied))
            st->last_transfer = {};
        if (facts.group != "crew") continue;
        if (facts.occupied) {
            ++ctx.occupied_crew_count;
            if (support::owner(read<Address>(slot, 0x280)) == tank_player) same_player_crew_inside = true;
        } else if (pol::is_crew_destination(facts) && is_place_node(boarder, index)) {
            facts.is_cannon_gunner = repair::detail::cannon_gunner_seat(tank, facts.name);
            ++available_crew_slots;
            if (best_dst.slot_index < 0 || pol::better_crew_slot(facts, best_dst)) best_dst = facts;
            if (pol::internal_destination_useful(facts, ctx) &&
                (internal_dst.slot_index < 0 || pol::better_crew_slot(facts, internal_dst)))
                internal_dst = facts;
        }
    }
    if (!available_crew_slots) return;

    pol::SeatOccupant internal{}, passenger{};
    ctx.secondary_gunner_backoff = pending(st->secondary_gunner_until, w.now);
    for (auto slot = seats.first; slot != seats.end; slot += 0x298) {
        const auto human = read<Address>(slot, 0x280);
        if (!human) continue;
        const auto index = static_cast<short>((slot - seats.first) / 0x298);
        pol::SeatOccupant candidate{read_slot(slot, index)};
        const bool crew = config::active().modules.internal_crew_swap && pol::is_internal_crew_source(candidate.slot);
        if (!crew && !pol::is_passenger_source(candidate.slot, ctx.is_tank)) continue;
        if (!is_place_node(boarder, index) ||
            !eligible_occupant(human, tank, tank_brain, ctx.occupied_crew_count != 0, same_player_crew_inside, walkers,
                               seats, candidate.passenger))
            continue;

        if (crew) {
            candidate.slot.is_cannon_gunner = repair::detail::cannon_gunner_seat(tank, candidate.slot.name);
            if (!pol::internal_source_available(candidate.slot, ctx)) continue;
            if (internal.slot.slot_index < 0 || pol::better_internal_source(candidate, internal)) internal = candidate;
        } else if (passenger.slot.slot_index < 0 || pol::better_passenger(candidate.passenger, passenger.passenger)) {
            passenger = candidate;
        }
    }

    // If the least critical crewman cannot promote, no higher-priority crewman can either.
    // The downward gunner-to-driver exception requires a sole crewman.
    ctx.has_eligible_passenger = passenger.slot.slot_index >= 0;
    ctx.has_move_order = orders.has_move_order;
    ctx.swap_cooldown_active = pending(st->driver_gunner_swap_until, w.now);
    if (internal.slot.name == "driver" && internal_dst.name == "gunner" && !ctx.has_eligible_passenger)
        ctx.has_visible_enemy = has_visible_enemy(tank_brain);
    const bool promote = pol::can_promote_internal(internal.slot, internal_dst, ctx);
    unsigned returning = 0;
    if (!promote) {
        if (!ctx.has_eligible_passenger) return;
        returning = count_expected_returning_crew(w, tank);
        const pol::TransferGate gate{available_crew_slots, returning,          ctx.has_eligible_passenger,
                                     placer_blocked,       orders.emit_active, emit_cooldown,
                                     exit_walker};
        if (!pol::can_transfer(gate)) {
            if (!pending(st->defer_stat_until, w.now)) {
                ++stat_deferred_repair;
                st->defer_stat_until = w.now + w.duration(5);
            }
            return;
        }
    }

    const auto& src = promote ? internal : passenger;
    const auto& dst = promote ? internal_dst : best_dst;
    const bool swap_driver_gunner = promote && pol::crew_slot_priority(src.slot) <= 1;
    short src_place = src.slot.slot_index;
    short dst_place = dst.slot_index;
    const auto human = read<Address>(seats.first + static_cast<Address>(src_place) * 0x298, 0x280);
    auto& last = st->last_transfer;
    const bool same = last.human_id == src.passenger.entity_id && last.src == src_place && last.dst == dst_place;
    if (same && pending(last.retry_until, w.now)) return;

    // Acceptance can precede the seat update. Give every move time to settle,
    // and reserve the longer delays for a crewman still attempting the same move.
    const Byte moved = ops.boarder_move(boarder, tank, &src_place, tank, &dst_place);
    st->retry_until = w.now + w.duration(moved ? 1.5f : 1.f);
    if (same) {
        last.retry_until = w.now + w.duration(last.retried ? 10.f : 5.f);
        last.retried = true;
    } else {
        last = {src.passenger.entity_id, src_place, dst_place};
    }
    if (!moved) {
        ++stat_transfer_failures;
        return;
    }

    const unsigned n = ++stat_transfers;
    if (promote) ++stat_transfers_internal;
    // Stock can re-man a secondary weapon with a different human on the next
    // quant. Leave that weapon staffed even if the next vacancy changes too.
    if (promote && pol::secondary_gunner(src.slot)) st->secondary_gunner_until = w.now + w.duration(10);
    if (swap_driver_gunner) st->driver_gunner_swap_until = w.now + w.duration(3.0f);
    if (!ctx.is_tank) ++stat_transfers_nontank;
    const int tier = pol::passenger_tier(src.passenger);
    if (tier <= 3)
        ++stat_transfers_crew_breed;
    else
        ++stat_transfers_infantry;
    if (log_sink) {
        log_sink("TANK CREW FILL #%u: tank=0x%llx human=0x%llx %.*s(%d) -> %.*s(%d) tier=%d freeCrew=%u %s=%u", n,
                 static_cast<unsigned long long>(tank), static_cast<unsigned long long>(human),
                 static_cast<int>(src.slot.name.size()), src.slot.name.data(), static_cast<int>(src_place),
                 static_cast<int>(dst.name.size()), dst.name.data(), static_cast<int>(dst_place), tier,
                 available_crew_slots, promote ? "occupiedCrew" : "returning",
                 promote ? ctx.occupied_crew_count : returning);
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
        stat_transfers_internal = stat_transfers_nontank = stat_deferred_repair = stat_deferred_emit = 0;
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
    pol::VehicleTacticalContext ctx{};
    if (!eligible_vehicle(actor, tank_brain, ctx)) return;

    struct Scope {
        Scope() {
            coordinating = true;
        }
        ~Scope() {
            coordinating = false;
        }
    } scope;
    service_vehicle(actor, tank_brain, w, ctx);
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
