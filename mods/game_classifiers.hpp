#pragma once

#include "config_and_log.hpp"
#include "game_runtime.hpp"

#include <cstring>
#include <string>
#include <string_view>

namespace goh::classify {

using runtime::Address;
using runtime::read;
using runtime::Span;

// Reads a GEM MSVC SSO string (length at +0x10, capacity at +0x18, inline or heap at +0x00).
inline std::string_view gem_string(Address addr, std::size_t max_len = 256) noexcept {
    if (!addr) return {};
    const auto len = read<std::size_t>(addr, 0x10);
    const auto cap = read<std::size_t>(addr, 0x18);
    if (!len || len > max_len || cap < 15 || cap < len || cap > 4096) return {};
    const char* ptr =
        cap >= 16 ? reinterpret_cast<const char*>(read<Address>(addr, 0)) : reinterpret_cast<const char*>(addr);
    return ptr ? std::string_view(ptr, len) : std::string_view{};
}

// Tracked battle tanks eligible for BattleCrusher obstacle-cost policies.
inline bool is_battle_tank_class(std::string_view type) noexcept {
    return type == "tank" || type == "tank_light" || type == "tank_medium" || type == "tank_heavy";
}

// All tank target classes including bulletproof light tanks / tankettes.
inline bool is_tank_or_bullet_class(std::string_view type) noexcept {
    return is_battle_tank_class(type) || type == "tank_bullet";
}

inline bool is_armored_vehicle_class(std::string_view type) noexcept {
    return is_tank_or_bullet_class(type) || type == "armoredcar" || type == "armoredcar_l" || type == "armored_car" ||
           type == "halftrack" || type == "tracked";
}

inline bool is_open_top_tag(std::string_view tag) noexcept {
    return tag == "open_top" || tag == "ammo_open_top_turret" || tag == "explosion_throw_crew";
}

inline bool is_at_weapon_tag(std::string_view tag) noexcept {
    return tag == "rocketlauncher" || tag == "ptr" || tag == "at_rifle";
}

inline bool is_heavy_support_weapon_tag(std::string_view tag) noexcept {
    return is_at_weapon_tag(tag) || tag == "mgun" || tag == "sniper";
}

inline bool is_at_grenade_tag(std::string_view tag) noexcept {
    return tag == "at" || tag == "dynamite" || tag == "ic";
}

inline bool is_at_breed_tag(std::string_view tag) noexcept {
    return tag == "soldier_bazooka" || tag == "soldier_pzscheck" || tag == "soldier_pzfaust" || tag == "soldier_atr" ||
           tag == "soldier_atr_grenade";
}

// Strict WP / incendiary / lethal name deny-list so user-configured extra smoke
// keys can never admit stock White Phosphorus or explosive items.
inline bool is_blocked_wp_or_lethal_key(std::string_view key) noexcept {
    if (key.empty()) return true;
    const std::string low = config::detail::to_lower(key);
    static constexpr const char* blocked[] = {"m15_wp",   "m42_wp",   "no77_wp", "2in_wp", "wp",
                                              "phosphor", "incend",   "molotov", "flame",  "fire",
                                              "frag",     "dynamite", "satchel", "charge", "he_"};
    for (const char* bad : blocked) {
        if (low.find(bad) != std::string::npos) return true;
    }
    return false;
}

inline bool is_audited_stock_smoke_grenade(std::string_view key) noexcept {
    static constexpr const char* stock[] = {"anm8_smoke", "m24_smoke", "m43_smoke", "rdg1_smoke",
                                            "svkkr_pm",   "nbk39",     "nbks",      "no8_smoke"};
    for (const char* name : stock) {
        if (key == name) return true;
    }
    return false;
}

inline bool is_allowed_smoke_grenade_key(std::string_view key) noexcept {
    if (is_audited_stock_smoke_grenade(key)) return true;
    if (is_blocked_wp_or_lethal_key(key)) return false;
    const auto& extras = config::active().smoke_extra_grenades;
    if (extras.empty()) return false;
    const std::string low = config::detail::to_lower(key);
    for (const auto& extra : extras) {
        if (low == extra) return true;
    }
    return false;
}

inline bool is_allowed_extra_smoke_program(std::string_view prog) noexcept {
    if (is_blocked_wp_or_lethal_key(prog)) return false;
    const auto& extras = config::active().smoke_extra_programs;
    if (extras.empty()) return false;
    const std::string low = config::detail::to_lower(prog);
    for (const auto& extra : extras) {
        if (low == extra) return true;
    }
    return false;
}

// Inspects an eStuffDefinition's expanded tag table at [def+0x11E0, def+0x11E8)
// (stride 0x60, string at row+0x40).
template <class Pred> inline bool def_any_tag(Address def, Pred&& pred) noexcept {
    if (!def) return false;
    const Span tags{read<Address>(def, 0x11E0), read<Address>(def, 0x11E8)};
    if (!tags.valid(0x60, 4096)) return false;
    for (Address row = tags.first; row < tags.end; row += 0x60) {
        if (pred(gem_string(row + 0x40, 128))) return true;
    }
    return false;
}

inline bool def_has_tag(Address def, std::string_view wanted) noexcept {
    if (wanted.empty()) return false;
    return def_any_tag(def, [&](std::string_view sv) { return sv == wanted; });
}

inline bool def_is_ranged_at_weapon(Address def) noexcept {
    return def_any_tag(def, [](std::string_view sv) { return is_at_weapon_tag(sv); });
}

inline bool def_is_heavy_support_weapon(Address def) noexcept {
    return def_any_tag(def, [](std::string_view sv) { return is_heavy_support_weapon_tag(sv); });
}

inline bool def_is_at_weapon(Address def) noexcept {
    if (!def) return false;
    const Span tags{read<Address>(def, 0x11E0), read<Address>(def, 0x11E8)};
    if (!tags.valid(0x60, 4096)) return false;
    bool has_grenade = false;
    bool has_missile = false;
    bool has_at_grenade = false;
    for (Address row = tags.first; row < tags.end; row += 0x60) {
        const auto sv = gem_string(row + 0x40, 128);
        if (is_at_weapon_tag(sv)) return true;
        if (sv == "grenade")
            has_grenade = true;
        else if (sv == "missile")
            has_missile = true;
        else if (is_at_grenade_tag(sv))
            has_at_grenade = true;
    }
    return has_grenade && has_missile && has_at_grenade;
}

// Dynamically inspects a human actor's live inventory box (actor+0x578 ->
// agg+0x28 -> manager+0xA0 -> items [box+0x10, box+0x18)) and right-hand holder
// ([manager+0x88, manager+0x90)) for any live carried AT weapon or AT grenade
// with positive count. Returns {inventory_inspected, has_at_item}.
struct CarriedAtResult {
    bool inventory_present{};
    bool has_at_weapon{};
};

inline CarriedAtResult inspect_carried_at_inventory(Address actor, Address base = 0) noexcept {
    CarriedAtResult out{};
    if (!actor) return out;
    const Address agg = read<Address>(actor, 0x578);
    const Address manager = agg ? read<Address>(agg, 0x28) : 0;
    if (!manager) return out;

    // 1. Check right-hand / mounted item holders.
    const Address hand_vt = base ? base + runtime::table().vt.holder_hand_right : 0;
    const Span holders{read<Address>(manager, 0x88), read<Address>(manager, 0x90)};
    if (holders.valid(8, 32)) {
        for (Address p = holders.first; p < holders.end; p += 8) {
            const Address holder = read<Address>(p);
            if (!holder) continue;
            if (hand_vt && read<Address>(holder) != hand_vt) continue;
            if (read<Address>(holder, 0x3D0) != actor) continue;
            out.inventory_present = true;
            const Address item = read<Address>(holder, 0x3D8);
            if (item && read<std::int32_t>(item, 0x48) > 0) {
                if (def_is_at_weapon(read<Address>(item, 0x40))) {
                    out.has_at_weapon = true;
                    return out;
                }
            }
        }
    }

    // 2. Check inventory box slots.
    const Address box = read<Address>(manager, 0xA0);
    if (!box || read<Address>(box, 0x50) != actor) return out;
    const Span items{read<Address>(box, 0x10), read<Address>(box, 0x18)};
    if (!items.valid(8, 1024)) return out;
    out.inventory_present = true;
    for (Address p = items.first; p < items.end; p += 8) {
        const Address item = read<Address>(p);
        if (!item || read<std::int32_t>(item, 0x48) <= 0) continue;
        if (def_is_at_weapon(read<Address>(item, 0x40))) {
            out.has_at_weapon = true;
            return out;
        }
    }
    return out;
}

} // namespace goh::classify
