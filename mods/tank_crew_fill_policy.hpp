#pragma once

#include <cmath>
#include <cstdint>
#include <string_view>

namespace goh::tank_crew_fill::policy {

struct SlotFacts {
    std::string_view name;
    std::string_view group;
    std::uint32_t flags{};
    bool has_visor{};
    bool has_link_bone_name{};
    bool has_resolved_bone{};
    bool occupied{};
    bool reserved{};
    bool is_cannon_gunner{};
    std::int16_t slot_index{-1};
    bool actively_requested{};
};

// Native ePlacer::IsPlaceAvailable (0x79C2F0) plus crew-group and
// non-Attach/non-NoCollect filters for freed interior crew slots.
inline bool is_crew_destination(const SlotFacts& s) {
    if (s.slot_index < 0 || s.group != "crew") return false;
    // Reject State.Disable (bit 4), Attach (bit 9), NoCollect (bit 15), Sealed (bit 20).
    if (s.flags & 0x00108210u) return false;
    if (s.occupied || s.reserved) return false;
    if (s.has_link_bone_name && !s.has_resolved_bone) return false;
    return true;
}

inline bool is_passenger_source(const SlotFacts& s, bool is_tank) {
    if (s.slot_index < 0 || s.group != "passenger" || !s.occupied || s.has_visor) return false;
    if (s.flags & 0x9200u) return false; // Attach, registered weapon/chassis request, NoCollect.
    return s.has_link_bone_name ? s.has_resolved_bone : !is_tank;
}

inline bool is_hull_passenger_source(const SlotFacts& s) {
    return is_passenger_source(s, true);
}

inline bool is_internal_crew_source(const SlotFacts& s) {
    return s.slot_index >= 0 && s.group == "crew" && s.occupied && !(s.flags & 0x8200u);
}

// Lower number = higher priority when filling freed crew slots.
inline int crew_slot_priority(const SlotFacts& s) {
    if (s.name == "gunner") return 0;
    if (s.name == "driver") return 1;
    if (s.name == "charger") return 2;
    if (s.name == "commander") return 3;
    if (s.is_cannon_gunner) return 4;
    if (s.name.size() == 8 && s.name.substr(0, 7) == "charger" && s.name[7] >= '1' && s.name[7] <= '6') return 5;
    if (s.flags & 0x00001000u) return 6;
    return 7;
}

inline bool better_crew_slot(const SlotFacts& a, const SlotFacts& b) {
    const int pa = crew_slot_priority(a);
    const int pb = crew_slot_priority(b);
    if (pa != pb) return pa < pb;
    return a.slot_index < b.slot_index;
}

struct VehicleTacticalContext {
    bool is_tank{};
    bool immobilized{};
    bool has_move_order{};
    bool has_visible_enemy{};
    bool swap_cooldown_active{};
    unsigned occupied_crew_count{};
    bool has_eligible_passenger{};
    bool main_gun_broken{};
    bool secondary_gunner_backoff{};
};

inline bool secondary_gunner(const SlotFacts& s) {
    return s.is_cannon_gunner
               ? s.name != "gunner"
               : s.name.size() == 7 && s.name.substr(0, 6) == "gunner" && s.name[6] >= '1' && s.name[6] <= '9';
}

inline bool secondary_charger(const SlotFacts& s) {
    return s.name.size() == 8 && s.name.substr(0, 7) == "charger" && s.name[7] >= '1' && s.name[7] <= '6';
}

inline bool secondary_turret_seat(const SlotFacts& s) {
    return secondary_gunner(s) || secondary_charger(s);
}

inline bool internal_destination_useful(const SlotFacts& s, const VehicleTacticalContext& ctx) {
    if (s.name == "driver") return !ctx.immobilized;
    return !ctx.main_gun_broken || (s.name != "gunner" && s.name != "charger");
}

inline bool internal_source_available(const SlotFacts& s, const VehicleTacticalContext& ctx) {
    return is_internal_crew_source(s) && !s.actively_requested && !(secondary_gunner(s) && ctx.main_gun_broken) &&
           !(secondary_turret_seat(s) && ctx.secondary_gunner_backoff);
}

inline bool can_promote_internal(const SlotFacts& src, const SlotFacts& dst, const VehicleTacticalContext& ctx) {
    if (!internal_source_available(src, ctx) || !is_crew_destination(dst) || !internal_destination_useful(dst, ctx) ||
        src.slot_index == dst.slot_index)
        return false;
    const int from = crew_slot_priority(src);
    const int to = crew_slot_priority(dst);
    // Internal reshuffles only rescue essential primary seats (gunner=0, driver=1, charger=2).
    // Commander and secondary turret seats are refilled from hull passengers, never by
    // cannibalizing interior crew seats (which fights stock ePlacer on multi-turret tanks).
    if (to > 2) return false;
    // Never pull an active secondary cannon gunner off their turret just to load the main gun.
    if (to == 2 && src.is_cannon_gunner) return false;
    if (from >= 2) return to < from;
    if (ctx.has_eligible_passenger || ctx.swap_cooldown_active) return false;
    if (from == 1 && to == 0) return !ctx.has_move_order && (ctx.immobilized || ctx.has_visible_enemy);
    return from == 0 && to == 1 && ctx.occupied_crew_count == 1 && ctx.has_move_order && !ctx.immobilized;
}

struct PassengerFacts {
    bool has_crew_tag{};
    bool is_mechanic{};
    float mechanic_rate{};
    bool is_medic{};
    bool has_at_tag{};
    bool same_player{true};
    std::int16_t slot_index{-1};
    std::uint32_t entity_id{};
};

// Lower numbers identify preferred donors within a seat role:
//   0: Dedicated tank crewmen ("crew" tag + Mechanic ability: tankman / tank_commander)
//   1: Skilled tank/vehicle mechanics without "crew" tag (tankman_ace, SP tankman)
//   2: Other "crew"-tagged personnel (AT-gun / artillery / MG crew)
//   3: Basic mechanics / vehicle drivers
//   4: Ordinary friendly infantry (riflemen, SMG, autorifle, etc.)
//   5: High-value infantry specialists (medics, AT-rocket / AT-rifle troopers)
inline int passenger_tier(const PassengerFacts& p) {
    const bool skilled_mech = p.is_mechanic && std::isfinite(p.mechanic_rate) && p.mechanic_rate >= 0.99f;
    if (p.has_crew_tag && p.is_mechanic) return 0;
    if (skilled_mech) return 1;
    if (p.has_crew_tag) return 2;
    if (p.is_mechanic) return 3;
    if (!p.is_medic && !p.has_at_tag) return 4;
    return 5;
}

inline bool better_passenger(const PassengerFacts& a, const PassengerFacts& b) {
    const int ta = passenger_tier(a);
    const int tb = passenger_tier(b);
    if (ta != tb) return ta < tb;
    if (a.same_player != b.same_player) return a.same_player;
    const float ra = (a.is_mechanic && std::isfinite(a.mechanic_rate)) ? a.mechanic_rate : 0.f;
    const float rb = (b.is_mechanic && std::isfinite(b.mechanic_rate)) ? b.mechanic_rate : 0.f;
    if (ra != rb) return ra > rb;
    if (a.slot_index != b.slot_index) return a.slot_index < b.slot_index;
    return a.entity_id < b.entity_id;
}

struct SeatOccupant {
    SlotFacts slot{};
    PassengerFacts passenger{};

    SeatOccupant() = default;
    SeatOccupant(const SlotFacts& seat, const PassengerFacts& traits = {}) : slot(seat), passenger(traits) {
        passenger.slot_index = seat.slot_index;
    }
};

inline bool better_internal_source(const SeatOccupant& a, const SeatOccupant& b) {
    const int pa = crew_slot_priority(a.slot);
    const int pb = crew_slot_priority(b.slot);
    return pa != pb ? pa > pb : better_passenger(a.passenger, b.passenger);
}

struct TransferGate {
    unsigned available_crew_slots{};
    unsigned expected_returning_crew{};
    bool has_eligible_passenger{};
    bool placer_blocked{};
    bool emit_active{};
    bool emit_cooldown{};
    bool exit_walker_active{};
};

inline bool can_transfer(const TransferGate& g) {
    if (g.placer_blocked || g.emit_active || g.emit_cooldown || g.exit_walker_active) return false;
    if (!g.has_eligible_passenger) return false;
    return g.available_crew_slots > g.expected_returning_crew;
}

} // namespace goh::tank_crew_fill::policy
