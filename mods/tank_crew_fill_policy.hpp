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
};

// Native ePlacer::IsPlaceAvailable (0x79C2F0) plus tank crew-group and
// non-Attach/non-NoCollect filters for freed interior crew slots.
inline bool is_crew_destination(const SlotFacts& s) {
    if (s.slot_index < 0 || s.group != "crew") return false;
    // Reject State.Disable (bit 4), Attach (bit 9), NoCollect (bit 15), Sealed (bit 20).
    if (s.flags & 0x00108210u) return false;
    if (s.occupied || s.reserved) return false;
    if (s.has_link_bone_name && !s.has_resolved_bone) return false;
    return true;
}

// Hull-top exterior passenger seats (properties/tank_crew.ext "seat_human":
// group "passenger", LinkBone present, no registered weapon/chassis request
// bit 12, no visor, not Attach/NoCollect). Excludes gunner_open roof MGs
// (gunner2) and passenger-group APC drivers/commanders.
inline bool is_hull_passenger_source(const SlotFacts& s) {
    if (s.slot_index < 0 || s.group != "passenger") return false;
    if (!s.occupied) return false;
    // Reject Attach (bit 9), registered crew request (bit 12), NoCollect (bit 15).
    if (s.flags & 0x00009200u) return false;
    if (s.has_visor) return false;
    if (!s.has_link_bone_name || !s.has_resolved_bone) return false;
    return true;
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

// Lower number = higher priority to move from hull passenger seat into tank crew:
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
