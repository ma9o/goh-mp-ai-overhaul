#pragma once

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "config_and_log.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace goh::runtime {

using Address = std::uintptr_t;
using Byte = std::uint8_t;
using Tick = std::uint32_t;

// Exact stock Steam build 26734796 call_to_arms.exe PE SHA-256.
inline constexpr char kBuild26734796Sha256[] = "81a13de787c1869b766004cdcadb9ff99012f0b1b58b89db9a7741b10b9f059a";

struct Vec3 {
    float x{}, y{}, z{};
};

struct XY {
    float x{}, y{};
};

inline bool operator==(const Vec3& a, const Vec3& b) noexcept {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

inline bool finite(Vec3 v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

inline bool finite(XY v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y);
}

template <class T> inline T read(Address address, std::size_t offset = 0) noexcept {
    T value{};
    if (!address) return value;
    std::memcpy(&value, reinterpret_cast<const void*>(address + offset), sizeof(value));
    return value;
}

template <class T> inline void write(Address address, std::size_t offset, const T& value) noexcept {
    if (!address) return;
    std::memcpy(reinterpret_cast<void*>(address + offset), &value, sizeof(value));
}

inline bool is_plausible_ptr(Address p) noexcept {
    return p >= 0x10000ULL && p < 0x00007FFFFFFFFFFFULL && ((p & 3ULL) == 0);
}

inline bool actor_ai_allowed(Address actor) noexcept {
    return config::active().ai_player_mask == 0xFFFFFFFFu ||
           (is_plausible_ptr(actor) && config::player_ai_enabled(read<Byte>(actor, 0xDA8)));
}

template <class Fn> inline Fn native(Address base, Address rva) noexcept {
    return reinterpret_cast<Fn>(base + rva);
}

struct Span {
    Address first{};
    Address end{};

    bool valid(std::size_t stride, std::size_t limit) const noexcept {
        return first && end >= first && ((end - first) % stride) == 0 && (end - first) / stride <= limit;
    }

    std::size_t count(std::size_t stride) const noexcept {
        return (first && end >= first && stride) ? (end - first) / stride : 0;
    }
};

// Centralized build-26734796 RVA and vtable table. Vtables are dynamically
// cross-checked (and can be resolved on updated builds) from the live PE32+
// image's MSVC x64 RTTI metadata by resolve_and_verify().
struct BuildTable {
    struct Authority {
        Address is_host_emit = 0x00C2B740;
        Address is_remote_client = 0x00C2B7A0;
        Address is_quant_worker = 0x000A7A80;
        Address clock_read = 0x0066E7B0;
    } auth;

    struct Functions {
        Address game_alloc = 0x00373650;
        Address game_free = 0x00DA1E38;
        Address armed_grenade_query = 0x009207E0;
        Address terrain_height = 0x00A5D440;
        Address terrain_env = 0x00A62620;
        Address rotate_factory = 0x0090AB40;
        Address move_task_point = 0x0090A8C0;
        Address order_attach_child = 0x0091A180;
        Address weapon_select = 0x0081B080;
        Address right_hand_definition = 0x007E04E0;
        Address weapon_profile = 0x0081EC40;
        Address weapon_is_direct = 0x0081EC90;
        Address weapon_max_range = 0x0081F710;
        Address ammo_from_charger = 0x00820B50;
        Address bullet_cast = 0x008514E0;
        Address pen_at_dist = 0x00841650;
        Address armor_angle_params = 0x007FC090;
        Address armor_query_init = 0x007FC310;
        Address armor_thickness = 0x007FC5B0;
        Address armor_eff_thick = 0x007FCA60;
        Address target_pos = 0x008527F0;
        Address target_pos2 = 0x00852810;
        Address relation = 0x00A65320;
        Address seg_query_init = 0x00A409C0;
        Address seg_query_seg = 0x00A40A20;
        Address seg_query_run = 0x00A40AC0;
        Address aimer_muzzle_slot6 = 0x008367D0;
        Address avoid_grenade_hide = 0x00917420;
        Address excluded_vehicle = 0x00855D60;
        Address repair_duration = 0x008AACF0;
        Address collect_order = 0x008E2910;
        Address board_order = 0x008FADC0;
        Address boarder_move = 0x007A0240;
    } fn;

    struct Hooks {
        Address grenade_sim_tick = 0x007442F0;
        Address grenade_sim_dtor = 0x007450E0;
        Address actor_slot23 = 0x00804AC0;
        Address grenade_scanner_upd = 0x0091FFA0;
        Address avoid_inner_tick = 0x00916570;
        Address avoid_plan_escape = 0x00917050;
        Address attack_check_and_fire = 0x009032F0;
        Address attack_obstructed = 0x00902ED0;
        Address target_accessor_try = 0x00835790;
        Address bullet_shooter_preview = 0x0082D4C0;
        Address collision_eval = 0x00836820;
        Address advance_engagement = 0x008E79F0;
        Address attack_tick = 0x00904930;
        Address world_publish_signal = 0x00A7B5F0;
        Address crush_update = 0x0091F2C0;
        Address crush_build = 0x0091EA20;
        Address root_execute = 0x0096E260;
        Address human_weapon_swap = 0x0095CEA0;
        Address select_exit_door = 0x0079D3C0;
        Address weapon_fire_dtor = 0x008EF8E0;
    } hook;

    struct Globals {
        Address spatial_scale_l = 0x0149E750;
        Address gravity = 0x015754E0;
        Address scene_ptr = 0x015C9668;
        Address slope_default_a = 0x01495030;
        Address slope_default_b = 0x01495034;
        Address master_client = 0x015D2BE0;
        Address local_player = 0x014AEF38;
    } glob;

    struct Vtables {
        Address human_brain = 0x0110FF88;
        Address vehicle_brain = 0x0101FD68;
        Address avoid_grenade = 0x0110D278;
        Address advance_order = 0x0110A070;
        Address crush_scanner_vehicle = 0x01110BC0;
        Address root_soldier = 0x01110CD0;
        Address attack_order = 0x0110C018;
        Address rotate_order = 0x0110C760;
        Address chassis_track = 0x01105E80;
        Address move_order = 0x0110C810;
        Address move_to_cover = 0x01109C60;
        Address move_task_order = 0x01023970;
        Address repair_order = 0x0110D0E8;
        Address smart_repair_order = 0x010225B0;
        Address kit_exchange_order = 0x010224F8;
        Address board_order = 0x0110B590;
        Address emit_order = 0x0110B808;
        Address medic_order = 0x01080638;
        Address weapon_fire_order = 0x0110A6E0;
        Address outdoors_battle = 0x01110738;
        Address cover_battle = 0x01110538;
        Address squad = 0x0110F3A0;
        Address throwing_shooter = 0x01102F20;
        Address effect_blastwave = 0x01038380;
        Address smoke_screen_ext = 0x01031C48;
        Address collect_order = 0x01109BB8;
        Address stuff_ext = 0x010FD200;
        Address cannon_ext = 0x0110F040;
        Address map_point_ext = 0x010FBFF0;
        Address map_point_capture = 0x0116C550;
        Address supply_zone_ext = 0x0109BA88;
        Address info_mechanic = 0x0107F0F8;
        Address info_medic = 0x0107EEA8;
        Address burning = 0x010FD478;
        Address holder_hand_right = 0x010FF210;
    } vt;
};

inline BuildTable& table() noexcept {
    static BuildTable instance{};
    return instance;
}

inline void reset_table() noexcept {
    table() = BuildTable{};
}

// Stack eTarget for a world-space point. The native order factory copies it.
class PointTarget {
public:
    PointTarget(Address image, Address xyz) : base_(image) {
        const Address vt = image + 0x101FD18;
        std::memcpy(storage_, &vt, sizeof(vt));
        native<Address (*)(Address)>(image, 0x564D50)(ptr() + 0x20);
        native<void (*)(Address, Address)>(image, 0x853180)(ptr(), xyz);
    }
    ~PointTarget() {
        native<void (*)(Address)>(base_, 0x853240)(ptr());
        native<void (*)(Address)>(base_, 0x565010)(ptr() + 0x20);
    }
    Address ptr() const noexcept {
        return reinterpret_cast<Address>(storage_);
    }
    PointTarget(const PointTarget&) = delete;
    PointTarget& operator=(const PointTarget&) = delete;

private:
    alignas(16) std::byte storage_[0x80]{};
    Address base_;
};

// ============================================================================
// Dynamic PE32+ section parsing, MSVC x64 RTTI vtable resolver, and pattern
// scanner (works on both live loaded game images and synthetic test PE buffers).
// ============================================================================

struct PeSection {
    char name[9]{};
    std::uint32_t virtual_address{};
    std::uint32_t virtual_size{};
    std::uint32_t raw_size{};
    std::uint32_t characteristics{};

    std::uint32_t span_size() const noexcept {
        return virtual_size ? virtual_size : raw_size;
    }
};

struct PeView {
    Address base{};
    std::uint32_t image_size{};
    std::uint16_t section_count{};
    PeSection sections[32]{};

    const PeSection* find_section(std::string_view name) const noexcept {
        for (std::uint16_t i = 0; i < section_count; ++i) {
            if (name == sections[i].name) return &sections[i];
        }
        return nullptr;
    }
};

inline bool parse_pe64(Address base, PeView& out, std::size_t max_bytes = 0) noexcept {
    out = {};
    if (!base) return false;
    if (max_bytes && max_bytes < 0x100) return false;
    if (read<std::uint16_t>(base, 0) != 0x5A4Du) return false; // "MZ"
    const std::uint32_t lfanew = read<std::uint32_t>(base, 0x3C);
    if (lfanew < 0x40 || lfanew > 0x4000) return false;
    if (max_bytes && lfanew + 0x108 > max_bytes) return false;
    if (read<std::uint32_t>(base, lfanew) != 0x00004550u) return false; // "PE\0\0"
    const std::uint16_t magic = read<std::uint16_t>(base, lfanew + 0x18);
    if (magic != 0x20Bu) return false; // PE32+
    const std::uint16_t num_sec = read<std::uint16_t>(base, lfanew + 0x06);
    const std::uint16_t opt_size = read<std::uint16_t>(base, lfanew + 0x14);
    const std::uint32_t img_size = read<std::uint32_t>(base, lfanew + 0x50);
    if (!num_sec || num_sec > 32 || opt_size < 0x70 || !img_size) return false;
    const std::size_t sec_off = static_cast<std::size_t>(lfanew) + 0x18u + opt_size;
    if (max_bytes && sec_off + static_cast<std::size_t>(num_sec) * 40u > max_bytes) return false;

    out.base = base;
    out.image_size = max_bytes ? static_cast<std::uint32_t>(std::min<std::size_t>(img_size, max_bytes)) : img_size;
    out.section_count = num_sec;
    for (std::uint16_t i = 0; i < num_sec; ++i) {
        const std::size_t off = sec_off + static_cast<std::size_t>(i) * 40u;
        PeSection& s = out.sections[i];
        std::memcpy(s.name, reinterpret_cast<const void*>(base + off), 8);
        s.name[8] = '\0';
        s.virtual_size = read<std::uint32_t>(base, off + 8);
        s.virtual_address = read<std::uint32_t>(base, off + 12);
        s.raw_size = read<std::uint32_t>(base, off + 16);
        s.characteristics = read<std::uint32_t>(base, off + 36);
    }
    return true;
}

// Masked byte-pattern scan inside a named PE section. Returns unique RVA or 0
// when missing or ambiguous (multiple matches).
inline Address find_unique_pattern(const PeView& pe, std::string_view section_name, const Byte* pattern,
                                   const char* mask) noexcept {
    if (!pe.base || !pattern || !mask) return 0;
    const std::size_t len = std::strlen(mask);
    if (!len) return 0;
    const PeSection* sec = pe.find_section(section_name);
    if (!sec) return 0;
    const std::size_t span = sec->span_size();
    if (span < len || static_cast<std::size_t>(sec->virtual_address) + span > pe.image_size) return 0;
    const auto* bytes = reinterpret_cast<const Byte*>(pe.base + sec->virtual_address);
    Address found = 0;
    for (std::size_t i = 0; i + len <= span; ++i) {
        bool match = true;
        for (std::size_t j = 0; j < len; ++j) {
            if (mask[j] != '?' && bytes[i + j] != pattern[j]) {
                match = false;
                break;
            }
        }
        if (match) {
            if (found) return 0; // Ambiguous match: fail closed.
            found = sec->virtual_address + i;
        }
    }
    return found;
}

// Dynamically resolves an MSVC x64 vtable RVA from live RTTI metadata inside a
// PE32+ image:
//   1. Find TypeDescriptor's mangled name string (at TypeDescriptor + 0x10) in
//      readable data sections (.data / .rdata).
//   2. Find RTTICompleteObjectLocator in .rdata with signature==1,
//      offset==subobject_offset, pTypeDescriptor==td_rva, pSelf==col_rva.
//   3. Find the 8-byte VA pointer (base + col_rva) inside .rdata; the vtable
//      immediately follows at (slot_rva + 8).
// Returns unique vtable RVA or 0 if not found or ambiguous.
inline Address resolve_rtti_vtable(const PeView& pe, std::string_view mangled_name,
                                   std::uint32_t subobject_offset = 0) noexcept {
    if (!pe.base || mangled_name.empty()) return 0;
    const std::size_t name_len = mangled_name.size() + 1; // Include NUL terminator.

    // Step 1: Locate unique TypeDescriptor RVA (name lives at td_rva + 0x10).
    // MSVC x64 TypeDescriptors are 8-byte aligned in writable data (.data) with
    // a type_info vtable pointer at +0x00 pointing inside the image.
    std::uint32_t td_rva = 0;
    for (std::uint16_t si = 0; si < pe.section_count; ++si) {
        const PeSection& sec = pe.sections[si];
        // Skip executable sections and non-writable read-only metadata (.pdata/.rsrc/.reloc).
        if ((sec.characteristics & 0x20000000u) != 0) continue;
        if ((sec.characteristics & 0x80000000u) == 0) continue; // IMAGE_SCN_MEM_WRITE
        const std::size_t span = sec.span_size();
        if (span < 0x10 + name_len || static_cast<std::size_t>(sec.virtual_address) + span > pe.image_size) continue;
        const auto* bytes = reinterpret_cast<const char*>(pe.base + sec.virtual_address);
        for (std::size_t i = 0x10; i + name_len <= span; i += 8) {
            if (bytes[i] == mangled_name[0] && std::memcmp(bytes + i, mangled_name.data(), name_len) == 0) {
                const std::uint32_t candidate = sec.virtual_address + static_cast<std::uint32_t>(i - 0x10);
                const Address td_vtable = read<Address>(pe.base, candidate);
                if (td_vtable < pe.base || td_vtable >= pe.base + pe.image_size) continue;
                if (td_rva && td_rva != candidate) return 0; // Ambiguous TypeDescriptor.
                td_rva = candidate;
            }
        }
    }
    if (!td_rva) return 0;

    // Step 2: Locate unique RTTICompleteObjectLocator in .rdata referencing td_rva.
    const PeSection* rdata = pe.find_section(".rdata");
    if (!rdata) return 0;
    const std::size_t rspan = rdata->span_size();
    if (rspan < 24 || static_cast<std::size_t>(rdata->virtual_address) + rspan > pe.image_size) return 0;

    std::uint32_t col_rva = 0;
    for (std::size_t off = 0; off + 24 <= rspan; off += 4) {
        const std::uint32_t rva = rdata->virtual_address + static_cast<std::uint32_t>(off);
        const std::uint32_t sig = read<std::uint32_t>(pe.base, rva + 0);
        if (sig != 1u) continue; // x64 relative RVA COL signature == 1
        if (read<std::uint32_t>(pe.base, rva + 4) != subobject_offset) continue;
        if (read<std::uint32_t>(pe.base, rva + 8) != 0u) continue; // cdOffset == 0
        if (read<std::uint32_t>(pe.base, rva + 12) != td_rva) continue;
        if (read<std::uint32_t>(pe.base, rva + 20) != rva) continue; // pSelf self-check
        if (col_rva) return 0;                                       // Ambiguous COL.
        col_rva = rva;
    }
    if (!col_rva) return 0;

    // Step 3: Find the unique pointer to (base + col_rva) in .rdata -> vtable at +8.
    const Address target_va = pe.base + col_rva;
    Address vt_rva = 0;
    for (std::size_t off = 0; off + 16 <= rspan; off += 8) {
        const std::uint32_t rva = rdata->virtual_address + static_cast<std::uint32_t>(off);
        if (read<Address>(pe.base, rva) == target_va) {
            if (vt_rva) return 0; // Ambiguous vtable reference.
            vt_rva = static_cast<Address>(rva) + 8u;
        }
    }
    return vt_rva;
}

struct RttiVerifyResult {
    unsigned checked{};
    unsigned matched{};
    unsigned mismatched{};
    unsigned missing{};

    bool ok() const noexcept {
        return checked > 0 && mismatched == 0 && missing == 0;
    }
};

// Cross-validates all core vtables in BuildTable against live MSVC x64 RTTI in
// the loaded PE image. When update_on_match is true and a vtable resolves
// cleanly, writes the resolved RVA into BuildTable; if fail_on_mismatch is true,
// mismatches are reported in `mismatched`.
inline RttiVerifyResult resolve_and_verify_vtables(Address base, bool apply_resolved = false,
                                                   std::size_t max_bytes = 0) noexcept {
    RttiVerifyResult res{};
    PeView pe{};
    if (!parse_pe64(base, pe, max_bytes)) return res;

    auto& vt = table().vt;
    struct Entry {
        const char* mangled;
        std::uint32_t subobject_offset;
        Address* field;
    };
    const Entry entries[] = {
        {".?AVeHumanBrain@Actor@@", 0, &vt.human_brain},
        {".?AVeVehicleBrain@Actor@@", 0, &vt.vehicle_brain},
        {".?AUeAvoidGrenade@Order@@", 0, &vt.avoid_grenade},
        {".?AUeAdvance@Order@@", 0, &vt.advance_order},
        {".?AVeCrushScannerVehicle@Actor@@", 0, &vt.crush_scanner_vehicle},
        {".?AVeHS_RootSoldier@Human@@", 0, &vt.root_soldier},
        {".?AUeAttack@Order@@", 0, &vt.attack_order},
        {".?AUeRotate@Order@@", 0, &vt.rotate_order},
        {".?AVeChassisTrack@Actor@@", 0, &vt.chassis_track},
        {".?AUeMove@Order@@", 0, &vt.move_order},
        {".?AUeMoveToCover@Order@@", 0, &vt.move_to_cover},
        {".?AUeMoveTask@Order@@", 0, &vt.move_task_order},
        {".?AUeRepair@Order@@", 0, &vt.repair_order},
        {".?AUeSmartRepair@Order@@", 0, &vt.smart_repair_order},
        {".?AUeKitExchange@Order@@", 0, &vt.kit_exchange_order},
        {".?AUeBoard@Order@@", 0, &vt.board_order},
        {".?AUeEmit@Order@@", 0, &vt.emit_order},
        {".?AUeMedic@Order@@", 0, &vt.medic_order},
        {".?AUeWeaponFire@Order@@", 0, &vt.weapon_fire_order},
        {".?AUeOutdoorsBattle@Human@@", 0, &vt.outdoors_battle},
        {".?AVeCoverBattle@Human@@", 0, &vt.cover_battle},
        {".?AVeSquad@Actor@@", 0, &vt.squad},
        {".?AUeThrowingShooter@Weapon@Actor@@", 0, &vt.throwing_shooter},
        {".?AVeEffectDescBlastwave@Damage@@", 0, &vt.effect_blastwave},
        {".?AVeSmokeScreenExtender@Entity@@", 0, &vt.smoke_screen_ext},
        {".?AUeCollect@Order@@", 0, &vt.collect_order},
        {".?AVeStuffExtender@Entity@@", 0, &vt.stuff_ext},
        {".?AVeCannonExtender@Entity@@", 0, &vt.cannon_ext},
        {".?AVeMapPointExtender@Entity@@", 0, &vt.map_point_ext},
        {".?AVeMapPointCaptureEngine@GamePlay@Multiplayer@@", 0, &vt.map_point_capture},
        {".?AVeSupplyZoneExtender@Entity@@", 0, &vt.supply_zone_ext},
        {".?AUeInfo_Mechanic@Human@@", 0, &vt.info_mechanic},
        {".?AUeInfo_Medic@Human@@", 0, &vt.info_medic},
        {".?AVeBurning@Entity@@", 0, &vt.burning},
        {".?AVeHolderHandRight@Inventory@@", 0, &vt.holder_hand_right},
    };

    for (const auto& e : entries) {
        ++res.checked;
        const Address resolved = resolve_rtti_vtable(pe, e.mangled, e.subobject_offset);
        if (!resolved) {
            ++res.missing;
            continue;
        }
        if (resolved == *e.field) {
            ++res.matched;
        } else {
            ++res.mismatched;
            if (apply_resolved) *e.field = resolved;
        }
    }
    return res;
}

// ============================================================================
// Shared authority, thread, and Direct Control guards.
// ============================================================================

using FnBool0 = Byte(__fastcall*)();
using FnClockRead = Tick(__fastcall*)();

inline bool is_remote_client(Address base) noexcept {
    if (!base) return true;
    return native<FnBool0>(base, table().auth.is_remote_client)() != 0;
}

inline bool is_host_emit(Address base) noexcept {
    if (!base) return false;
    return native<FnBool0>(base, table().auth.is_host_emit)() != 0;
}

inline bool is_quant_worker(Address base) noexcept {
    if (!base) return true;
    return native<FnBool0>(base, table().auth.is_quant_worker)() != 0;
}

inline bool is_host_main_thread(Address base) noexcept {
    return base && !is_remote_client(base) && !is_quant_worker(base);
}

inline Tick read_clock(Address base) noexcept {
    return base ? native<FnClockRead>(base, table().auth.clock_read)() : 0;
}

inline bool is_inhibited_or_direct_control(Address actor, Address brain) noexcept {
    if (!actor || !brain) return true;
    if ((read<std::uint32_t>(brain, 0x39C) & 0x3Fu) != 0) return true;
    const Address chassis = read<Address>(actor, 0xDE0);
    if (chassis && read<std::uint32_t>(chassis, 0x18) != 0) return true;
    const Address weaponry = read<Address>(actor, 0xDD8);
    if (weaponry && (read<std::uint32_t>(weaponry, 0x70) & 2u) != 0) return true;
    return false;
}

// ============================================================================
// Universal hook lifecycle gate & RAII inflight counter (covers all unified hooks).
// ============================================================================

inline std::atomic<std::uint32_t> global_inflight{0};
inline std::atomic<bool> global_accepting{false};

struct HookGate {
    std::atomic<bool> accepting{false};
    std::atomic<std::uint32_t> inflight{0};

    void activate() noexcept {
        accepting.store(true, std::memory_order_release);
    }

    void deactivate() noexcept {
        accepting.store(false, std::memory_order_release);
    }

    bool active() const noexcept {
        return accepting.load(std::memory_order_acquire) && global_accepting.load(std::memory_order_acquire);
    }

    std::uint32_t count() const noexcept {
        return inflight.load(std::memory_order_acquire);
    }
};

struct HookGuard {
    HookGate* gate{};

    explicit HookGuard(HookGate& g) noexcept : gate(&g) {
        global_inflight.fetch_add(1, std::memory_order_acq_rel);
        gate->inflight.fetch_add(1, std::memory_order_acq_rel);
    }

    HookGuard() noexcept {
        global_inflight.fetch_add(1, std::memory_order_acq_rel);
    }

    ~HookGuard() {
        if (gate) gate->inflight.fetch_sub(1, std::memory_order_acq_rel);
        global_inflight.fetch_sub(1, std::memory_order_acq_rel);
    }

    HookGuard(const HookGuard&) = delete;
    HookGuard& operator=(const HookGuard&) = delete;
};

// ============================================================================
// Standalone SHA-256 (zero external DLL dependencies).
// ============================================================================

struct Sha256Ctx {
    std::uint32_t state[8]{};
    std::uint64_t bitlen{};
    Byte data[64]{};
    std::size_t datalen{};
};

inline std::uint32_t rotr32(std::uint32_t x, std::uint32_t n) noexcept {
    return (x >> n) | (x << (32u - n));
}

inline void sha256_transform(Sha256Ctx* ctx, const Byte d[64]) noexcept {
    static constexpr std::uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    std::uint32_t m[64]{};
    for (int i = 0; i < 16; ++i) {
        m[i] = (std::uint32_t(d[i * 4]) << 24) | (std::uint32_t(d[i * 4 + 1]) << 16) |
               (std::uint32_t(d[i * 4 + 2]) << 8) | std::uint32_t(d[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr32(m[i - 15], 7) ^ rotr32(m[i - 15], 18) ^ (m[i - 15] >> 3);
        const std::uint32_t s1 = rotr32(m[i - 2], 17) ^ rotr32(m[i - 2], 19) ^ (m[i - 2] >> 10);
        m[i] = m[i - 16] + s0 + m[i - 7] + s1;
    }
    std::uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d0 = ctx->state[3];
    std::uint32_t e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];
    for (int i = 0; i < 64; ++i) {
        const std::uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        const std::uint32_t ch = (e & f) ^ ((~e) & g);
        const std::uint32_t t1 = h + S1 + ch + k[i] + m[i];
        const std::uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = S0 + maj;
        h = g;
        g = f;
        f = e;
        e = d0 + t1;
        d0 = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d0;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

inline void sha256_init(Sha256Ctx* ctx) noexcept {
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
}

inline void sha256_update(Sha256Ctx* ctx, const Byte* data, std::size_t len) noexcept {
    for (std::size_t i = 0; i < len; ++i) {
        ctx->data[ctx->datalen++] = data[i];
        if (ctx->datalen == 64) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

inline void sha256_final_hex(Sha256Ctx* ctx, char out_hex[65]) noexcept {
    std::size_t i = ctx->datalen;
    if (i < 56) {
        ctx->data[i++] = 0x80;
        while (i < 56)
            ctx->data[i++] = 0x00;
    } else {
        ctx->data[i++] = 0x80;
        while (i < 64)
            ctx->data[i++] = 0x00;
        sha256_transform(ctx, ctx->data);
        std::memset(ctx->data, 0, 56);
    }
    ctx->bitlen += static_cast<std::uint64_t>(ctx->datalen) * 8u;
    for (int j = 0; j < 8; ++j) {
        ctx->data[63 - j] = static_cast<Byte>((ctx->bitlen >> (j * 8)) & 0xFFu);
    }
    sha256_transform(ctx, ctx->data);
    static constexpr char hex[] = "0123456789abcdef";
    for (int j = 0; j < 8; ++j) {
        const std::uint32_t w = ctx->state[j];
        for (int b = 0; b < 4; ++b) {
            const Byte v = static_cast<Byte>((w >> (24 - b * 8)) & 0xFFu);
            out_hex[j * 8 + b * 2] = hex[v >> 4];
            out_hex[j * 8 + b * 2 + 1] = hex[v & 0x0Fu];
        }
    }
    out_hex[64] = '\0';
}

#if defined(_WIN32)
inline bool verify_game_exe_sha256(char out_hex[65], const char* expected = kBuild26734796Sha256) noexcept {
    wchar_t exe_path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, exe_path, MAX_PATH)) return false;
    FILE* f = _wfopen(exe_path, L"rb");
    if (!f) return false;
    Sha256Ctx ctx{};
    sha256_init(&ctx);
    Byte buf[65536];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
        sha256_update(&ctx, buf, n);
    std::fclose(f);
    sha256_final_hex(&ctx, out_hex);
    return expected && std::strcmp(out_hex, expected) == 0;
}
#endif

} // namespace goh::runtime
