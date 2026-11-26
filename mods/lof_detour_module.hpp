#pragma once

#include "combat_orders_module.hpp"
#include "config_and_log.hpp"
#include "game_runtime.hpp"
#include "penetration_at_module.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <unordered_map>

namespace goh::lof_detour {

using runtime::Address;
using runtime::Byte;
using runtime::HookGate;
using runtime::HookGuard;
using runtime::is_plausible_ptr;
using runtime::native;
using runtime::read;
using runtime::Tick;
using runtime::Vec3;

inline constexpr const char* executable_sha256 = runtime::kBuild26734796Sha256;
inline constexpr unsigned hook_count = 5; // AttackCheckAndFire, AttackObstructed, TargetAccessorTry, Preview, CollEval

namespace detail {

namespace Off {
constexpr Address kEntityType30 = 0x0030;
constexpr Address kActorFlags38 = 0x0038;
constexpr Address kEntityPos = 0x0074;
constexpr Address kActorOrderedSub = 0x0AF0;
constexpr Address kActorOperatableDB4 = 0x0DB4;
constexpr Address kActorBrain = 0x0DC8;
constexpr Address kActorSensor = 0x0DD0;
constexpr Address kActorWeaponry = 0x0DD8;
constexpr Address kActorChassis = 0x0DE0;

constexpr Address kBrainOwner = 0x0038;
constexpr Address kBrainFlags398 = 0x0398;
constexpr Address kBrainFlags39C = 0x039C;

constexpr Address kSensorCat1Begin = 0x0280;
constexpr Address kSensorCat1End = 0x0288;
constexpr Address kSubjectFlags10 = 0x0010;
constexpr Address kSubjectFlags14 = 0x0014;
constexpr Address kSubjectActor = 0x0018;
constexpr Address kSubjectPos = 0x0028;
constexpr Address kSubjectObsTime = 0x0064;
constexpr Address kSubjectSuppress = 0x006C;

constexpr Address kOrderOwner = 0x0008;
constexpr Address kOrderChild = 0x0030;
constexpr Address kAttackState = 0x0038;
constexpr Address kAttackTarget = 0x0040;
constexpr Address kAttackTargetEnt = 0x0058;
constexpr Address kAttackWaitReady = 0x00C0;
constexpr Address kAttackOptC1 = 0x00C1;
constexpr Address kAttackStatTries = 0x00D0;
constexpr Address kAttackDetourExp = 0x00D4;
constexpr Address kAttackLastPos = 0x00DC;
constexpr Address kAttackLastMoveClk = 0x00E8;

constexpr Address kWeaponDef = 0x0098;
constexpr Address kWeaponAimer = 0x00A8;
constexpr Address kWeaponCharger = 0x00B0;
constexpr Address kWeaponDefCalibre = 0x1628;
constexpr Address kAmmoEffectsBegin = 0x0480;
constexpr Address kAmmoEffectsEnd = 0x0488;
constexpr Address kAmmoPossDmgRad = 0x0498;
constexpr Address kAmmoDetThickKind = 0x12B8;
constexpr Address kAmmoDetThickVal = 0x12BC;
constexpr Address kAmmoCumulative = 0x12C4;
constexpr Address kVolumeDef = 0x0030;
constexpr Address kVolDefMaterial = 0x0408;
constexpr Address kMatSteelEquiv = 0x00E0;

constexpr Address kFootSetTypeVOff = 46 * 8;
} // namespace Off

struct SceneHit48 {
    std::uint32_t kind;
    float dist;
    float xyz[3];
    std::uint32_t pad14;
    Address entity;
    Address volume;
    float normal[3];
    float dir[3];
    std::int32_t face;
    std::uint32_t pad44;
};
static_assert(sizeof(SceneHit48) == 0x48, "SceneHit48 size mismatch");

template <typename T> struct MsvcVec {
    T* begin;
    T* end;
    T* cap;
};
static_assert(sizeof(MsvcVec<SceneHit48>) == 24, "MsvcVec size mismatch");

using goh::penetration_at::detail::ArmorHit56;
using goh::penetration_at::detail::ArmorParams56;
using goh::penetration_at::detail::ArmorQuery40;

using FnGameAlloc = void*(__fastcall*)(std::size_t size);
using FnGameFree = void(__fastcall*)(void* ptr);
using FnTerrainHeight = float(__fastcall*)(Address terrain, const float* xy, int level);
using FnTerrainEnv = std::uint16_t*(__fastcall*)(std::uint16_t* out, const float* xy);
using FnMoveTaskPoint = Address(__fastcall*)(const float* xy, std::uint32_t flags);
using FnOrderAttachChild = void(__fastcall*)(Address parent, Address child);
using FnChassisSetInt = void(__fastcall*)(Address chassis, std::int32_t val, Byte force);

using FnWeaponSelect = Address(__fastcall*)(Address weaponry);
using FnWeaponProfile = Address(__fastcall*)(Address weapon);
using FnWeaponIsDirect = Byte(__fastcall*)(Address weapon);
using FnWeaponMaxRange = float(__fastcall*)(Address weapon, Address target_ent);
using FnAmmoFromCharger = Address(__fastcall*)(Address charger);
using FnBulletCast = Address(__fastcall*)(Address ammo_def);
using FnPenAtDist = float(__fastcall*)(Address profile, std::uint32_t dist_float_bits);
using FnArmorAngleParams = void(__fastcall*)(ArmorParams56* params, Address profile, float cos_inc);
using FnArmorQueryInit = ArmorQuery40*(__fastcall*)(ArmorQuery40 * q, Address ent, Address src, ArmorHit56* hit,
                                                    ArmorParams56* params);
using FnArmorThickness = float(__fastcall*)(ArmorQuery40* q, Byte estimate_mode);
using FnArmorEffThick = float(__fastcall*)(ArmorQuery40* q, float raw_thick);
using FnTargetPos = const float*(__fastcall*)(Address target);
using FnRelation = std::int32_t(__fastcall*)(Address actor_a, Address actor_b, Byte flag);
using FnEffectRadius = float(__fastcall*)(Address effect_desc);
using FnAimerMuzzlePos = const double*(__fastcall*)(Address aimer);
using FnSegQueryInit = void*(__fastcall*)(void* query_buf, std::uint32_t flags, Address ignore_ent,
                                          std::int32_t param4);
using FnSegQuerySeg = void(__fastcall*)(void* query_buf, const float* start_xyz, const float* end_xyz);
using FnSegQueryRun = Byte(__fastcall*)(void* query_buf, MsvcVec<SceneHit48>* hits_vec);

using FnAttackFn = std::uint64_t(__fastcall*)(Address order);
using FnTargetAccTry = std::uint32_t(__fastcall*)(Address* ctx, Address target, SceneHit48* out_hit);
using FnShooterPreview = void(__fastcall*)(Address shooter, MsvcVec<SceneHit48>* hits_vec, Address opt_input,
                                           MsvcVec<Vec3>* path_vec, std::uint32_t flags);
using FnCollisionEval = std::uint64_t(__fastcall*)(Address target, Address owner_ent, MsvcVec<SceneHit48>* hits_vec,
                                                   SceneHit48* out_hit, MsvcVec<Vec3>* path_vec);

struct AttackEvalCtx {
    bool active{};
    bool in_detour{};
    bool extended_requested{};
    bool has_blocking_hit{};
    Address order{};
    Address actor{};
    Address weapon{};
    Address ammo_def{};
    Address bullet_def{};
    Address profile{};
    float calibre{};
    float blast_radius{};
    float muzzle_height{};
    std::uint32_t last_verdict{};
    float block_hit_xyz[3]{};
    Address block_entity{};
    Address block_volume{};
};

struct SoftLofCtx {
    bool active{};
    bool is_explicit{};
    bool extended_requested{};
    Address actor{};
    Address weapon{};
    Address ammo_def{};
    Address bullet_def{};
    Address profile{};
    float calibre{};
    float blast_radius{};
};

struct DetourState {
    Tick last_issue_tick{};
    int attempt{};
    float last_dest_x{};
    float last_dest_y{};
    std::uint32_t rng{};
};

inline HookGate gate{};
inline Address base{};
inline bool staged{};
inline bool rollback_pending{};

inline FnAttackFn orig_attack_check_fire{};
inline FnAttackFn orig_attack_obstructed{};
inline FnTargetAccTry orig_target_acc_try{};
inline FnShooterPreview orig_shooter_preview{};
inline FnCollisionEval orig_collision_eval{};

inline AttackEvalCtx atk_ctx{};
inline SoftLofCtx soft_ctx{};

inline std::unordered_map<Address, DetourState>& detours() noexcept {
    static std::unordered_map<Address, DetourState> map;
    return map;
}

inline std::atomic<std::uint32_t> stat_soft_overrides{0};
inline std::atomic<std::uint32_t> stat_soft_rejects{0};
inline std::atomic<std::uint32_t> stat_detours_inf{0};
inline std::atomic<std::uint32_t> stat_detours_veh{0};
inline Tick last_soft_log_tick{};

inline std::uint32_t float_to_bits(float f) noexcept {
    std::uint32_t u = 0;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

inline std::uint32_t xorshift32(std::uint32_t& s) noexcept {
    std::uint32_t x = s ? s : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s = x;
    return x;
}

inline void game_free_msvc_buffer(void* ptr, std::size_t byte_size) noexcept {
    if (!ptr) return;
    void* raw = ptr;
    if (byte_size >= 0x1000) {
        const Address p = reinterpret_cast<Address>(ptr);
        const Address orig = read<Address>(p - 8);
        if (orig < p && (p - orig) >= 8 && (p - orig) <= 0x27) raw = reinterpret_cast<void*>(orig);
    }
    native<FnGameFree>(base, runtime::table().fn.game_free)(raw);
}

inline float spatial_scale() noexcept {
    const float L = read<float>(base + runtime::table().glob.spatial_scale_l);
    return (std::isfinite(L) && L > 1.0f && L < 200.0f) ? L : 20.0f;
}

inline float sample_terrain_height(float x, float y, float fallback_z) noexcept {
    const Address scene = read<Address>(base + runtime::table().glob.scene_ptr);
    if (!is_plausible_ptr(scene)) return fallback_z;
    const Address terrain = read<Address>(scene, 0x18);
    if (!is_plausible_ptr(terrain)) return fallback_z;
    const float xy[2] = {x, y};
    const float h = native<FnTerrainHeight>(base, runtime::table().fn.terrain_height)(terrain, xy, 0);
    return std::isfinite(h) ? h : fallback_z;
}

inline bool is_passable_dry_terrain(float x, float y, bool is_human) noexcept {
    if (!std::isfinite(x) || !std::isfinite(y)) return false;
    if (std::fabs(x) < 1.0f && std::fabs(y) < 1.0f) return false;
    const float xy[2] = {x, y};
    std::uint16_t env = 0;
    native<FnTerrainEnv>(base, runtime::table().fn.terrain_env)(&env, xy);
    const Byte cls = static_cast<Byte>(env & 0xFFu);
    if (cls == 2 || cls == 3 || cls == 4 || cls == 5) return false;
    if (is_human && cls == 8) return false;
    return true;
}

inline bool is_eligible_actor(Address actor, bool& out_is_human, Address& out_brain, Address& out_chassis) noexcept {
    out_is_human = false;
    out_brain = 0;
    out_chassis = 0;
    if (!is_plausible_ptr(actor)) return false;
    if ((read<std::uint32_t>(actor, Off::kActorFlags38) & 0x220u) != 0) return false;

    const Address brain = read<Address>(actor, Off::kActorBrain);
    if (!is_plausible_ptr(brain) || read<Address>(brain, Off::kBrainOwner) != actor) return false;

    const Address brain_vt = read<Address>(brain);
    if (brain_vt == base + runtime::table().vt.human_brain)
        out_is_human = true;
    else if (brain_vt == base + runtime::table().vt.vehicle_brain)
        out_is_human = false;
    else
        return false;

    if ((read<std::uint32_t>(brain, Off::kBrainFlags39C) & 0x3Fu) != 0) return false;

    const Address chassis = read<Address>(actor, Off::kActorChassis);
    if (is_plausible_ptr(chassis) && read<std::uint32_t>(chassis, 0x18) != 0) return false;

    const Address weaponry = read<Address>(actor, Off::kActorWeaponry);
    if (!is_plausible_ptr(weaponry)) return false;
    if ((read<std::uint32_t>(weaponry, 0x70) & 2u) != 0) return false;

    out_brain = brain;
    out_chassis = chassis;
    return true;
}

inline float compute_true_blast_radius(Address ammo_def) noexcept {
    if (!is_plausible_ptr(ammo_def)) return 0.0f;
    float max_r = read<float>(ammo_def, Off::kAmmoPossDmgRad);
    if (!std::isfinite(max_r) || max_r < 0.0f) max_r = 0.0f;

    const Address beg = read<Address>(ammo_def, Off::kAmmoEffectsBegin);
    const Address end = read<Address>(ammo_def, Off::kAmmoEffectsEnd);
    if (is_plausible_ptr(beg) && is_plausible_ptr(end) && end >= beg) {
        const std::size_t n = (end - beg) / sizeof(Address);
        if (n <= 32) {
            for (std::size_t i = 0; i < n; ++i) {
                const Address eff = read<Address>(beg, i * sizeof(Address));
                if (!is_plausible_ptr(eff)) continue;
                const Address vt = read<Address>(eff);
                if (!is_plausible_ptr(vt)) continue;
                const auto fn_rad = read<FnEffectRadius>(vt, 0x28);
                if (!fn_rad) continue;
                const float r = fn_rad(eff);
                if (std::isfinite(r) && r > max_r && r < 5000.0f) max_r = r;
            }
        }
    }
    return max_r;
}

inline bool is_operatable_ally_hit(Address owner_ent, const SceneHit48& h) noexcept {
    if (h.kind != 3 && h.kind != 4) return false;
    if (!is_plausible_ptr(h.entity)) return false;
    if ((read<Byte>(h.entity, Off::kEntityType30) & 2u) == 0) return false;
    if (((read<std::uint32_t>(h.entity, Off::kActorOperatableDB4) >> 8) & 1u) == 0) return false;
    return native<FnRelation>(base, runtime::table().fn.relation)(owner_ent, h.entity, 0) == 2;
}

inline bool path_reaches_point(const MsvcVec<Vec3>* path_vec, const float target_xyz[3], float max_dist_sq,
                               float& out_min_path_dist) noexcept {
    out_min_path_dist = 1e30f;
    if (!path_vec || !path_vec->begin || !path_vec->end || path_vec->end <= path_vec->begin) return false;
    const std::size_t count = static_cast<std::size_t>(path_vec->end - path_vec->begin);
    if (count < 2 || count > 4096) return false;

    float cum_dist = 0.0f;
    for (std::size_t i = 1; i < count; ++i) {
        const Vec3& a = path_vec->begin[i - 1];
        const Vec3& b = path_vec->begin[i];
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        const float dz = b.z - a.z;
        const float seg_len_sq = dx * dx + dy * dy + dz * dz;
        const float seg_len = (seg_len_sq > 1e-8f) ? std::sqrt(seg_len_sq) : 0.0f;

        float t = 0.0f;
        if (seg_len_sq > 1e-8f) {
            t = ((target_xyz[0] - a.x) * dx + (target_xyz[1] - a.y) * dy + (target_xyz[2] - a.z) * dz) / seg_len_sq;
            if (t < 0.0f)
                t = 0.0f;
            else if (t > 1.0f)
                t = 1.0f;
        }
        const float cx = a.x + dx * t - target_xyz[0];
        const float cy = a.y + dy * t - target_xyz[1];
        const float cz = a.z + dz * t - target_xyz[2];
        if (cx * cx + cy * cy + cz * cz < max_dist_sq) {
            out_min_path_dist = cum_dist + seg_len * t;
            return true;
        }
        cum_dist += seg_len;
    }
    return false;
}

inline bool is_piercing_ammo(Address ammo_def, float blast_radius, float& out_det_thick_mm,
                             const char*& out_reason) noexcept {
    out_det_thick_mm = 0.0f;
    out_reason = "ok";
    if (!is_plausible_ptr(ammo_def)) {
        out_reason = "no_ammo";
        return false;
    }
    const float cumul = read<float>(ammo_def, Off::kAmmoCumulative);
    if (std::isfinite(cumul) && cumul > 0.0f) {
        out_reason = "heat_cumulative";
        return false;
    }
    const float poss_rad = read<float>(ammo_def, Off::kAmmoPossDmgRad);
    if (std::isfinite(poss_rad) && poss_rad > 0.0f) {
        out_reason = "he_poss_rad";
        return false;
    }
    const std::int32_t det_kind = read<std::int32_t>(ammo_def, Off::kAmmoDetThickKind);
    const float det_val = read<float>(ammo_def, Off::kAmmoDetThickVal);
    if (det_kind == 0 && std::isfinite(det_val) && det_val > 0.0f) {
        if (det_val <= 2.0f) {
            out_reason = "he_det_thick";
            return false;
        }
        out_det_thick_mm = det_val;
    }
    if (blast_radius > 5.0f * spatial_scale()) {
        out_reason = "large_blast_he";
        return false;
    }
    return true;
}

inline bool evaluate_soft_cover_blockers(Address owner_ent, Address target_ent, Address ammo_def, Address profile,
                                         float calibre, float blast_radius, const SceneHit48* hits,
                                         std::size_t hit_count, float stop_dist, float& out_rem_frac,
                                         float& out_max_steel_eq, float& out_sum_eff_thick_mm, int& out_distinct_layers,
                                         const char*& out_reject_reason) noexcept {
    out_rem_frac = 1.0f;
    out_max_steel_eq = 0.0f;
    out_sum_eff_thick_mm = 0.0f;
    out_distinct_layers = 0;
    out_reject_reason = "none";

    if (!is_plausible_ptr(profile) || calibre < 5.0f) {
        out_reject_reason = "no_profile_or_cal";
        return false;
    }
    float det_thick_mm = 0.0f;
    if (!is_piercing_ammo(ammo_def, blast_radius, det_thick_mm, out_reject_reason)) return false;

    const float L = spatial_scale();
    const bool is_explosive = (blast_radius > 0.5f * L);
    const float slope_def_a = read<float>(base + runtime::table().glob.slope_default_a);
    const float slope_def_b = read<float>(base + runtime::table().glob.slope_default_b);

    constexpr int kMaxSeenVols = 12;
    Address seen_vols[kMaxSeenVols]{};
    int seen_count = 0;
    float rem_frac = 1.0f;

    const auto fn_pen_at_dist = native<FnPenAtDist>(base, runtime::table().fn.pen_at_dist);
    const auto fn_angle_params = native<FnArmorAngleParams>(base, runtime::table().fn.armor_angle_params);
    const auto fn_query_init = native<FnArmorQueryInit>(base, runtime::table().fn.armor_query_init);
    const auto fn_thick = native<FnArmorThickness>(base, runtime::table().fn.armor_thickness);
    const auto fn_eff_thick = native<FnArmorEffThick>(base, runtime::table().fn.armor_eff_thick);

    for (std::size_t i = 0; i < hit_count; ++i) {
        const SceneHit48& h = hits[i];
        if (h.dist >= stop_dist - 0.1f) break;
        if (target_ent != 0 && h.entity == target_ent) break;

        if (h.kind == 1 || h.kind == 2 || !is_plausible_ptr(h.volume) || !is_plausible_ptr(h.entity)) {
            out_reject_reason = "terrain_or_null";
            return false;
        }
        const Byte type30 = read<Byte>(h.entity, Off::kEntityType30);
        if ((type30 & 2u) != 0) {
            if (is_operatable_ally_hit(owner_ent, h)) {
                out_reject_reason = "ally";
                return false;
            }
            if ((type30 & 4u) == 0) {
                out_reject_reason = "vehicle_hull";
                return false;
            }
        }

        bool already_seen = false;
        for (int s = 0; s < seen_count; ++s) {
            if (seen_vols[s] == h.volume) {
                already_seen = true;
                break;
            }
        }
        if (already_seen) continue;
        if (seen_count >= 6) {
            out_reject_reason = "too_many_layers";
            return false;
        }
        seen_vols[seen_count++] = h.volume;

        if (is_explosive && h.dist < (blast_radius + 3.0f * L)) {
            out_reject_reason = "he_too_close";
            return false;
        }

        const Address vol_def = read<Address>(h.volume, Off::kVolumeDef);
        if (!is_plausible_ptr(vol_def)) {
            out_reject_reason = "no_voldef";
            return false;
        }
        const Address vol_name = read<Address>(vol_def, 0x398);
        const Address vol_shape = read<Address>(h.volume, 0x50);
        if (!is_plausible_ptr(vol_name) || !is_plausible_ptr(vol_shape)) {
            out_reject_reason = "no_vol_meta";
            return false;
        }
        const Address mat = read<Address>(vol_def, Off::kVolDefMaterial);
        if (!is_plausible_ptr(mat)) {
            out_reject_reason = "no_material";
            return false;
        }
        const float steel_eq = read<float>(mat, Off::kMatSteelEquiv);
        if (!std::isfinite(steel_eq) || steel_eq < 0.0f) {
            out_reject_reason = "bad_steeleq";
            return false;
        }
        if (steel_eq > out_max_steel_eq) out_max_steel_eq = steel_eq;

        const bool is_soft_wood = (steel_eq <= 0.09f);
        const bool is_light_masonry = (steel_eq > 0.09f && steel_eq <= 0.165f);
        if (!is_soft_wood && !is_light_masonry) {
            out_reject_reason = "hard_material";
            return false;
        }
        if (is_light_masonry && calibre < 75.0f) {
            out_reject_reason = "masonry_low_cal";
            return false;
        }

        float pen_mm = fn_pen_at_dist(profile, float_to_bits(h.dist)) * rem_frac;
        if (!std::isfinite(pen_mm)) pen_mm = 0.0f;

        const float n_len_sq = h.normal[0] * h.normal[0] + h.normal[1] * h.normal[1] + h.normal[2] * h.normal[2];
        const float d_len_sq = h.dir[0] * h.dir[0] + h.dir[1] * h.dir[1] + h.dir[2] * h.dir[2];
        float nd = 1.0f;
        if (n_len_sq > 0.01f && d_len_sq > 0.01f) {
            nd = std::fabs(h.normal[0] * h.dir[0] + h.normal[1] * h.dir[1] + h.normal[2] * h.dir[2]) /
                 std::sqrt(n_len_sq * d_len_sq);
        }
        const float min_cos = is_soft_wood ? 0.50f : 0.25f;
        if (!std::isfinite(nd) || nd < min_cos) nd = min_cos;
        if (nd > 1.0f) nd = 1.0f;

        ArmorHit56 armor_hit{};
        for (int k = 0; k < 3; ++k) {
            armor_hit.impact[k] = h.xyz[k];
            armor_hit.normal[k] = h.normal[k];
            armor_hit.dir[k] = h.dir[k];
        }
        armor_hit.face = (h.face >= 0 && h.face <= 5) ? h.face : 0;
        armor_hit.volume = h.volume;
        armor_hit.entryFlag = 1;

        ArmorParams56 params{};
        params.flags = 0;
        params.calibre = calibre;
        params.slopeA = slope_def_a;
        params.slopeB = slope_def_b;
        params.invCos = 1.0f;
        params.dmgKind = 0;
        params.dmgVal = pen_mm;

        fn_angle_params(&params, profile, nd);

        ArmorQuery40 query{};
        fn_query_init(&query, h.entity, owner_ent, &armor_hit, &params);

        if (is_plausible_ptr(query.setting)) {
            const Byte s_flags = read<Byte>(query.setting, 0xB0);
            if ((s_flags & 1u) != 0) {
                out_reject_reason = "immune";
                return false;
            }
            if ((s_flags & 6u) != 0 || is_soft_wood) params.invCos = 1.0f;
            const float min_cal = read<float>(query.setting, 0x10);
            if (std::isfinite(min_cal) && min_cal > 0.0f && calibre < min_cal) {
                out_reject_reason = "below_min_cal";
                return false;
            }
        } else if (is_soft_wood) {
            params.invCos = 1.0f;
        }

        const float raw_thick = fn_thick(&query, 1);
        if (!std::isfinite(raw_thick) || raw_thick < 0.0f) {
            out_reject_reason = "bad_thick";
            return false;
        }
        float eff_thick = is_soft_wood ? raw_thick : fn_eff_thick(&query, raw_thick);
        if (!std::isfinite(eff_thick) || eff_thick < 0.0f) eff_thick = raw_thick;
        out_sum_eff_thick_mm += eff_thick;

        if (det_thick_mm > 0.0f && raw_thick >= det_thick_mm) {
            out_reject_reason = "would_fuse";
            return false;
        }

        if (is_soft_wood) {
            if (pen_mm < eff_thick * 1.25f || pen_mm <= 1.0f) {
                out_reject_reason = "insufficient_pen";
                return false;
            }
            rem_frac *= (1.0f - eff_thick / pen_mm);
            if (rem_frac < 0.25f) {
                out_reject_reason = "low_residual_pen";
                return false;
            }
        } else if (is_light_masonry && calibre >= 75.0f && raw_thick <= 25.0f) {
            if (pen_mm < eff_thick * 1.50f || pen_mm <= 1.0f) {
                out_reject_reason = "insufficient_pen";
                return false;
            }
            rem_frac *= (1.0f - eff_thick / pen_mm);
            if (rem_frac < 0.35f) {
                out_reject_reason = "low_residual_pen";
                return false;
            }
        } else {
            out_reject_reason = "insufficient_pen";
            return false;
        }
    }

    out_rem_frac = rem_frac;
    out_distinct_layers = seen_count;
    return seen_count > 0;
}

inline DetourState* get_detour_state(Address order, Tick now) {
    auto& map = detours();
    if (map.size() > 512) {
        for (auto it = map.begin(); it != map.end();) {
            if ((now - it->second.last_issue_tick) >= 25000)
                it = map.erase(it);
            else
                ++it;
        }
    }
    auto it = map.find(order);
    if (it != map.end() && (now - it->second.last_issue_tick) < 25000) return &it->second;
    DetourState& ds = map[order];
    ds = {};
    ds.rng = static_cast<std::uint32_t>((order >> 4) ^ now ^ 0xA5A55A5Au);
    return &ds;
}

inline int test_segment_lof(Address actor, Address target_ent, Address ammo_def, Address profile, float calibre,
                            float blast_radius, const float start_xyz[3], const float end_xyz[3]) noexcept {
    alignas(16) Byte query_buf[0x100]{};
    MsvcVec<SceneHit48> hits_vec{};
    auto* buf =
        static_cast<SceneHit48*>(native<FnGameAlloc>(base, runtime::table().fn.game_alloc)(16 * sizeof(SceneHit48)));
    if (!buf) return 0;
    hits_vec.begin = buf;
    hits_vec.end = buf;
    hits_vec.cap = buf + 16;

    native<FnSegQueryInit>(base, runtime::table().fn.seg_query_init)(query_buf, 0x86803u, actor, -1);
    native<FnSegQuerySeg>(base, runtime::table().fn.seg_query_seg)(query_buf, start_xyz, end_xyz);
    native<FnSegQueryRun>(base, runtime::table().fn.seg_query_run)(query_buf, &hits_vec);

    Address q_beg = 0;
    Address q_cap = 0;
    std::memcpy(&q_beg, query_buf + 0x90, sizeof(q_beg));
    std::memcpy(&q_cap, query_buf + 0xA0, sizeof(q_cap));
    if (q_beg != 0) {
        const std::size_t q_bytes = (q_cap > q_beg) ? ((q_cap - q_beg) & ~std::size_t(7)) : 0;
        game_free_msvc_buffer(reinterpret_cast<void*>(q_beg), q_bytes);
    }

    int result = 2;
    const std::size_t raw_n =
        (hits_vec.begin && hits_vec.end > hits_vec.begin) ? static_cast<std::size_t>(hits_vec.end - hits_vec.begin) : 0;

    SceneHit48 filt[32]{};
    std::size_t n = 0;
    for (std::size_t i = 0; i < raw_n && n < 32; ++i) {
        const SceneHit48& h = hits_vec.begin[i];
        if (h.kind == 1 || h.kind == 2) {
            filt[n++] = h;
        } else if (is_plausible_ptr(h.volume)) {
            const Byte v28 = read<Byte>(h.volume, 0x28);
            const std::uint32_t v0c = read<std::uint32_t>(h.volume, 0x0C);
            if ((v28 & 0x20u) == 0 && (v0c & 2u) != 0) filt[n++] = h;
        }
    }

    const float dx = end_xyz[0] - start_xyz[0];
    const float dy = end_xyz[1] - start_xyz[1];
    const float dz = end_xyz[2] - start_xyz[2];
    const float seg_len = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float L = spatial_scale();

    if (n > 0) {
        float stop_dist = seg_len - 0.35f * L;
        if (stop_dist < 0.0f) stop_dist = 0.0f;
        if (target_ent != 0) {
            for (std::size_t i = 0; i < n; ++i) {
                if ((filt[i].kind == 3 || filt[i].kind == 4) && filt[i].entity == target_ent) {
                    stop_dist = filt[i].dist;
                    break;
                }
            }
        }
        bool has_blocker = false;
        for (std::size_t i = 0; i < n; ++i) {
            if (filt[i].dist >= stop_dist - 0.1f) break;
            if (target_ent != 0 && filt[i].entity == target_ent) break;
            has_blocker = true;
            break;
        }
        if (has_blocker) {
            bool ally_blocked = false;
            for (std::size_t i = 0; i < n; ++i) {
                if (filt[i].dist > stop_dist + 2.0f * L) break;
                if (is_operatable_ally_hit(actor, filt[i]) && filt[i].entity != target_ent) {
                    ally_blocked = true;
                    break;
                }
            }
            if (!ally_blocked && is_plausible_ptr(profile) && calibre >= 5.0f) {
                float rem_frac = 0.0f;
                float max_st = 0.0f;
                float sum_th = 0.0f;
                int layers = 0;
                const char* why = nullptr;
                const bool soft_ok =
                    evaluate_soft_cover_blockers(actor, target_ent, ammo_def, profile, calibre, blast_radius, filt, n,
                                                 stop_dist, rem_frac, max_st, sum_th, layers, why);
                result = soft_ok ? 1 : 0;
            } else {
                result = 0;
            }
        }
    }

    if (hits_vec.begin) {
        const std::size_t bytes = (hits_vec.cap > hits_vec.begin)
                                      ? static_cast<std::size_t>(hits_vec.cap - hits_vec.begin) * sizeof(SceneHit48)
                                      : 0;
        game_free_msvc_buffer(hits_vec.begin, bytes);
    }
    return result;
}

inline float compute_enemy_exposure_penalty(Address actor, Address target_ent, float ax, float ay, float cx, float cy,
                                            Tick now) noexcept {
    const Address sensor = read<Address>(actor, Off::kActorSensor);
    if (!is_plausible_ptr(sensor)) return 0.0f;
    const Address beg = read<Address>(sensor, Off::kSensorCat1Begin);
    const Address end = read<Address>(sensor, Off::kSensorCat1End);
    if (!is_plausible_ptr(beg) || !is_plausible_ptr(end) || end < beg) return 0.0f;
    const std::size_t count = (end - beg) / sizeof(Address);
    if (count == 0 || count > 128) return 0.0f;

    float penalty = 0.0f;
    for (std::size_t i = 0; i < count; ++i) {
        const Address subj = read<Address>(beg, i * sizeof(Address));
        if (!is_plausible_ptr(subj)) continue;
        if (read<Byte>(subj, Off::kSubjectSuppress) != 0) continue;
        if (target_ent != 0 && read<Address>(subj, Off::kSubjectActor) == target_ent) continue;

        const std::uint32_t f10 = read<std::uint32_t>(subj, Off::kSubjectFlags10);
        const std::uint32_t f14 = read<std::uint32_t>(subj, Off::kSubjectFlags14);
        if ((f14 & (1u << 1)) == 0 || (f14 & (1u << 4)) != 0 || (f10 & (1u << 28)) == 0) continue;
        if ((now - read<Tick>(subj, Off::kSubjectObsTime)) > 20000) continue;

        const float ex = read<float>(subj, Off::kSubjectPos);
        const float ey = read<float>(subj, Off::kSubjectPos + 4);
        if (!std::isfinite(ex) || !std::isfinite(ey)) continue;

        const float d_now = std::sqrt((ax - ex) * (ax - ex) + (ay - ey) * (ay - ey));
        const float d_cand = std::sqrt((cx - ex) * (cx - ex) + (cy - ey) * (cy - ey));
        const float delta_closer = d_now - d_cand;
        if (delta_closer > 0.0f) {
            float w = 0.45f;
            if ((f10 & (1u << 29)) != 0) w += 0.35f;
            if ((f14 & ((1u << 7) | (1u << 8))) != 0) w += 0.90f;
            penalty += delta_closer * w;
        }
    }
    return penalty;
}

inline std::uint64_t __fastcall hook_attack_check_and_fire(Address order) {
    HookGuard guard(gate);
    const auto& mods = config::active().modules;
    if (!gate.active() || (!mods.soft_cover_lof && !mods.attack_detour && !mods.attack_order_polish) ||
        !runtime::is_host_main_thread(base) || !is_plausible_ptr(order)) {
        return orig_attack_check_fire(order);
    }
    if (read<Address>(order) != base + runtime::table().vt.attack_order) return orig_attack_check_fire(order);
    combat_orders::detail::AttackMemory memory(order);
    if (!mods.soft_cover_lof && !mods.attack_detour) return orig_attack_check_fire(order);

    const Address owner_sub = read<Address>(order, Off::kOrderOwner);
    if (!is_plausible_ptr(owner_sub) || owner_sub <= Off::kActorOrderedSub) return orig_attack_check_fire(order);
    const Address actor = owner_sub - Off::kActorOrderedSub;

    bool is_human = false;
    Address brain = 0;
    Address chassis = 0;
    if (!is_eligible_actor(actor, is_human, brain, chassis)) return orig_attack_check_fire(order);

    const Address weaponry = read<Address>(actor, Off::kActorWeaponry);
    const Address weapon = native<FnWeaponSelect>(base, runtime::table().fn.weapon_select)(weaponry);
    if (!is_plausible_ptr(weapon)) return orig_attack_check_fire(order);

    const Address weapon_def = read<Address>(weapon, Off::kWeaponDef);
    const Address charger = read<Address>(weapon, Off::kWeaponCharger);
    const Address ammo_def =
        is_plausible_ptr(charger) ? native<FnAmmoFromCharger>(base, runtime::table().fn.ammo_from_charger)(charger) : 0;
    const Address bullet_def =
        is_plausible_ptr(ammo_def) ? native<FnBulletCast>(base, runtime::table().fn.bullet_cast)(ammo_def) : 0;
    const Address profile = native<FnWeaponProfile>(base, runtime::table().fn.weapon_profile)(weapon);

    float calibre = 0.0f;
    if (is_plausible_ptr(weapon_def)) {
        calibre = read<float>(weapon_def, Off::kWeaponDefCalibre);
        if (!std::isfinite(calibre) || calibre < 0.0f) calibre = 0.0f;
    }
    const float blast_radius = compute_true_blast_radius(ammo_def);

    const float L = spatial_scale();
    const float ax = read<float>(actor, Off::kEntityPos);
    const float ay = read<float>(actor, Off::kEntityPos + 4);
    const float az = read<float>(actor, Off::kEntityPos + 8);
    const float ground_z = sample_terrain_height(ax, ay, az);
    float muzzle_h = 1.85f * L;
    if (is_human && is_plausible_ptr(chassis)) {
        const std::int32_t stance = read<std::int32_t>(chassis, 0x2C);
        muzzle_h = (stance == 1 || stance == 3) ? (0.35f * L) : (stance == 2 ? (0.90f * L) : (1.35f * L));
    }
    const Address aimer = read<Address>(weapon, Off::kWeaponAimer);
    if (is_plausible_ptr(aimer)) {
        const Address aimer_vt = read<Address>(aimer);
        if (is_plausible_ptr(aimer_vt)) {
            const Address slot6 = read<Address>(aimer_vt, 0x30);
            if (slot6 == base + runtime::table().fn.aimer_muzzle_slot6) {
                const double* mpos = reinterpret_cast<FnAimerMuzzlePos>(slot6)(aimer);
                if (mpos && std::isfinite(mpos[2])) {
                    const float h = static_cast<float>(mpos[2]) - ground_z;
                    if (h >= 0.15f * L && h <= 4.5f * L) muzzle_h = h;
                }
            } else {
                const Address bone = read<Address>(aimer, 0x20);
                if (is_plausible_ptr(bone) && (read<Byte>(bone, 0x10) & 1u) == 0u) {
                    const float bz = read<float>(bone, 0xD4);
                    if (std::isfinite(bz)) {
                        const float h = bz - ground_z;
                        if (h >= 0.15f * L && h <= 4.5f * L) muzzle_h = h;
                    }
                }
            }
        }
    }

    const AttackEvalCtx prev = atk_ctx;
    atk_ctx = {};
    atk_ctx.active = true;
    atk_ctx.order = order;
    atk_ctx.actor = actor;
    atk_ctx.weapon = weapon;
    atk_ctx.ammo_def = ammo_def;
    atk_ctx.bullet_def = bullet_def;
    atk_ctx.profile = profile;
    atk_ctx.calibre = calibre;
    atk_ctx.blast_radius = blast_radius;
    atk_ctx.muzzle_height = muzzle_h;

    const std::uint64_t ret = orig_attack_check_fire(order);
    atk_ctx = prev;
    return ret;
}

inline std::uint32_t __fastcall hook_target_accessor_try(Address* ctx, Address target, SceneHit48* out_hit) {
    HookGuard guard(gate);
    if (!gate.active() || !config::active().modules.soft_cover_lof || atk_ctx.in_detour ||
        !runtime::is_host_main_thread(base) || !is_plausible_ptr(reinterpret_cast<Address>(ctx))) {
        return orig_target_acc_try(ctx, target, out_hit);
    }
    if (ctx[5] != 0) return orig_target_acc_try(ctx, target, out_hit);

    const Address weapon = ctx[1];
    const Address actor = ctx[4];
    if (!is_plausible_ptr(weapon) || !is_plausible_ptr(actor)) return orig_target_acc_try(ctx, target, out_hit);

    bool is_human = false;
    Address brain = 0;
    Address chassis = 0;
    if (!is_eligible_actor(actor, is_human, brain, chassis)) return orig_target_acc_try(ctx, target, out_hit);

    const bool is_explicit = (atk_ctx.active && atk_ctx.actor == actor && atk_ctx.weapon == weapon);
    if (!is_explicit && (((read<std::uint32_t>(brain, Off::kBrainFlags398) >> 10) & 3u) == 1u)) {
        return orig_target_acc_try(ctx, target, out_hit);
    }

    const Address charger = read<Address>(weapon, Off::kWeaponCharger);
    const Address ammo_def =
        is_plausible_ptr(charger) ? native<FnAmmoFromCharger>(base, runtime::table().fn.ammo_from_charger)(charger) : 0;
    const Address bullet_def =
        is_plausible_ptr(ammo_def) ? native<FnBulletCast>(base, runtime::table().fn.bullet_cast)(ammo_def) : 0;
    if (!bullet_def) return orig_target_acc_try(ctx, target, out_hit);

    const Address weapon_def = read<Address>(weapon, Off::kWeaponDef);
    const Address profile = native<FnWeaponProfile>(base, runtime::table().fn.weapon_profile)(weapon);
    float calibre = 0.0f;
    if (is_plausible_ptr(weapon_def)) {
        calibre = read<float>(weapon_def, Off::kWeaponDefCalibre);
        if (!std::isfinite(calibre) || calibre < 0.0f) calibre = 0.0f;
    }
    const float blast_radius = compute_true_blast_radius(ammo_def);

    const SoftLofCtx prev = soft_ctx;
    soft_ctx = {};
    soft_ctx.active = true;
    soft_ctx.is_explicit = is_explicit;
    soft_ctx.actor = actor;
    soft_ctx.weapon = weapon;
    soft_ctx.ammo_def = ammo_def;
    soft_ctx.bullet_def = bullet_def;
    soft_ctx.profile = profile;
    soft_ctx.calibre = calibre;
    soft_ctx.blast_radius = blast_radius;

    const std::uint32_t ret = orig_target_acc_try(ctx, target, out_hit);
    soft_ctx = prev;
    return ret;
}

inline void __fastcall hook_bullet_shooter_preview(Address shooter, MsvcVec<SceneHit48>* hits_vec, Address opt_input,
                                                   MsvcVec<Vec3>* path_vec, std::uint32_t flags) {
    HookGuard guard(gate);
    if (gate.active() && config::active().modules.soft_cover_lof && soft_ctx.active && !atk_ctx.in_detour &&
        runtime::is_host_main_thread(base) && is_plausible_ptr(shooter) && soft_ctx.bullet_def != 0) {
        if (read<Address>(shooter, 8) == soft_ctx.weapon) {
            soft_ctx.extended_requested = true;
            orig_shooter_preview(shooter, hits_vec, opt_input, path_vec, flags | 0x400000u);
            return;
        }
    }
    orig_shooter_preview(shooter, hits_vec, opt_input, path_vec, flags);
}

inline std::uint64_t __fastcall hook_collision_eval(Address target, Address owner_ent, MsvcVec<SceneHit48>* hits_vec,
                                                    SceneHit48* out_hit, MsvcVec<Vec3>* path_vec) {
    HookGuard guard(gate);
    const bool track_atk = (atk_ctx.active && !atk_ctx.in_detour && owner_ent == atk_ctx.actor);

    if (!gate.active() || !config::active().modules.soft_cover_lof || !soft_ctx.active || atk_ctx.in_detour ||
        !runtime::is_host_main_thread(base) || owner_ent != soft_ctx.actor || !soft_ctx.extended_requested) {
        const std::uint64_t v = orig_collision_eval(target, owner_ent, hits_vec, out_hit, path_vec);
        if (track_atk) atk_ctx.last_verdict = static_cast<std::uint32_t>(v);
        return v;
    }

    soft_ctx.extended_requested = false;
    if (!hits_vec || !hits_vec->begin || !hits_vec->end || hits_vec->end <= hits_vec->begin) {
        const std::uint64_t v = orig_collision_eval(target, owner_ent, hits_vec, out_hit, path_vec);
        if (track_atk) atk_ctx.last_verdict = static_cast<std::uint32_t>(v);
        return v;
    }

    const SceneHit48* hits = hits_vec->begin;
    const std::size_t hit_count = static_cast<std::size_t>(hits_vec->end - hits_vec->begin);
    const Address target_ent = read<Address>(target, 0x18);
    const float* target_xyz = native<FnTargetPos>(base, runtime::table().fn.target_pos2)(target);
    const float L = spatial_scale();
    const float prox_sq = (0.5f * L) * (0.5f * L);

    if (is_operatable_ally_hit(owner_ent, hits[0])) {
        if (out_hit) *out_hit = hits[0];
        const std::uint64_t v = (target_ent != 0 && hits[0].entity == target_ent) ? 3ULL : 4ULL;
        if (track_atk) atk_ctx.last_verdict = static_cast<std::uint32_t>(v);
        return v;
    }
    if (target_ent != 0 && (hits[0].kind == 3 || hits[0].kind == 4) && hits[0].entity == target_ent) {
        if (out_hit) *out_hit = hits[0];
        if (track_atk) atk_ctx.last_verdict = 3;
        return 3;
    }

    float min_path_dist = 1e30f;
    const bool path_reaches = path_reaches_point(path_vec, target_xyz, prox_sq, min_path_dist);
    if (path_reaches && min_path_dist <= hits[0].dist + 0.25f * L) {
        if (track_atk) atk_ctx.last_verdict = 3;
        return 3;
    }

    if (track_atk) {
        atk_ctx.has_blocking_hit = true;
        atk_ctx.block_hit_xyz[0] = hits[0].xyz[0];
        atk_ctx.block_hit_xyz[1] = hits[0].xyz[1];
        atk_ctx.block_hit_xyz[2] = hits[0].xyz[2];
        atk_ctx.block_entity = hits[0].entity;
        atk_ctx.block_volume = hits[0].volume;
    }

    int target_idx = -1;
    float stop_dist = 0.0f;
    if (target_ent != 0) {
        for (std::size_t i = 0; i < hit_count; ++i) {
            if ((hits[i].kind == 3 || hits[i].kind == 4) && hits[i].entity == target_ent) {
                target_idx = static_cast<int>(i);
                stop_dist = hits[i].dist;
                break;
            }
        }
    }
    if (target_idx < 0) {
        if (!path_reaches) {
            if (out_hit) *out_hit = hits[0];
            if (track_atk) atk_ctx.last_verdict = 5;
            return 5;
        }
        stop_dist = min_path_dist;
    }

    for (std::size_t i = 0; i < hit_count; ++i) {
        if (hits[i].dist > stop_dist + 2.0f * L) break;
        if (is_operatable_ally_hit(owner_ent, hits[i]) && hits[i].entity != target_ent) {
            if (out_hit) *out_hit = hits[i];
            const std::uint64_t v = (hits[i].dist < stop_dist) ? 4ULL : 5ULL;
            if (track_atk) atk_ctx.last_verdict = static_cast<std::uint32_t>(v);
            return v;
        }
    }

    float rem_frac = 1.0f;
    float max_steel_eq = 0.0f;
    float sum_eff_thick = 0.0f;
    int layers = 0;
    const char* reject_reason = "none";

    const bool soft_ok = evaluate_soft_cover_blockers(
        owner_ent, target_ent, soft_ctx.ammo_def, soft_ctx.profile, soft_ctx.calibre, soft_ctx.blast_radius, hits,
        hit_count, stop_dist, rem_frac, max_steel_eq, sum_eff_thick, layers, reject_reason);

    const Tick log_interval = config::active().tuning.soft_cover_log_interval_ms;
    if (soft_ok) {
        const std::uint32_t count = stat_soft_overrides.fetch_add(1, std::memory_order_relaxed) + 1;
        if (out_hit) *out_hit = (target_idx >= 0) ? hits[target_idx] : hits[0];
        if (log::enabled(log::Level::verbose)) {
            const Tick now = runtime::read_clock(base);
            if (now - last_soft_log_tick > log_interval) {
                last_soft_log_tick = now;
                log::write(log::Level::verbose,
                           "SOFT-COVER SHOOT-THROUGH #%u [%s]: actor=0x%llx cal=%.1fmm blastR=%.1fm layers=%d "
                           "steelEq=%.3f thick=%.1fmm rem=%.2f dist=%.1fm",
                           count, soft_ctx.is_explicit ? "EXP" : "AUTO", static_cast<unsigned long long>(owner_ent),
                           soft_ctx.calibre, soft_ctx.blast_radius / L, layers, max_steel_eq, sum_eff_thick, rem_frac,
                           hits[0].dist / L);
            }
        }
        if (track_atk) atk_ctx.last_verdict = 3;
        return 3;
    }

    const std::uint32_t count = stat_soft_rejects.fetch_add(1, std::memory_order_relaxed) + 1;
    if (log::enabled(log::Level::verbose)) {
        const Tick now = runtime::read_clock(base);
        if (now - last_soft_log_tick > log_interval) {
            last_soft_log_tick = now;
            log::write(log::Level::verbose,
                       "SOFT-COVER VETO #%u [%s] (%s): actor=0x%llx cal=%.1fmm blastR=%.1fm steelEq=%.3f thick=%.1fmm "
                       "dist=%.1fm",
                       count, soft_ctx.is_explicit ? "EXP" : "AUTO", reject_reason,
                       static_cast<unsigned long long>(owner_ent), soft_ctx.calibre, soft_ctx.blast_radius / L,
                       max_steel_eq, sum_eff_thick, hits[0].dist / L);
        }
    }
    if (out_hit) *out_hit = hits[0];
    if (track_atk) atk_ctx.last_verdict = 5;
    return 5;
}

inline std::uint64_t __fastcall hook_attack_obstructed(Address order) {
    HookGuard guard(gate);
    const bool prev_in_detour = atk_ctx.in_detour;
    atk_ctx.in_detour = true;
    auto call_orig = [&]() -> std::uint64_t {
        const std::uint64_t r = orig_attack_obstructed(order);
        atk_ctx.in_detour = prev_in_detour;
        return r;
    };

    if (!gate.active() || !config::active().modules.attack_detour || !runtime::is_host_main_thread(base) ||
        !is_plausible_ptr(order)) {
        return call_orig();
    }
    if (read<Address>(order) != base + runtime::table().vt.attack_order) return call_orig();
    if (read<Address>(order, Off::kOrderChild) != 0) return call_orig();

    const Address owner_sub = read<Address>(order, Off::kOrderOwner);
    if (!is_plausible_ptr(owner_sub) || owner_sub <= Off::kActorOrderedSub) return call_orig();
    const Address actor = owner_sub - Off::kActorOrderedSub;

    bool is_human = false;
    Address brain = 0;
    Address chassis = 0;
    if (!is_eligible_actor(actor, is_human, brain, chassis) || !is_plausible_ptr(chassis)) return call_orig();

    // Respect Hold Position (move_mode == 1).
    if (((read<std::uint32_t>(brain, Off::kBrainFlags398) >> 12) & 3u) == 1u) return call_orig();

    const float ax = read<float>(actor, Off::kEntityPos);
    const float ay = read<float>(actor, Off::kEntityPos + 4);
    const float az = read<float>(actor, Off::kEntityPos + 8);
    const float lx = read<float>(order, Off::kAttackLastPos);
    const float ly = read<float>(order, Off::kAttackLastPos + 4);
    const float lz = read<float>(order, Off::kAttackLastPos + 8);
    const Tick now = runtime::read_clock(base);

    const bool same_pos = (std::fabs(ax - lx) < 1e-3f && std::fabs(ay - ly) < 1e-3f && std::fabs(az - lz) < 1e-3f);
    if (same_pos) {
        if (read<Byte>(order, Off::kAttackOptC1) != 0 && read<std::int32_t>(order, Off::kAttackStatTries) >= 20)
            return call_orig();
        if ((now - read<Tick>(order, Off::kAttackLastMoveClk)) >= 10000u) return call_orig();
    }

    const Address weaponry = read<Address>(actor, Off::kActorWeaponry);
    const Address weapon = native<FnWeaponSelect>(base, runtime::table().fn.weapon_select)(weaponry);
    if (!is_plausible_ptr(weapon)) return call_orig();

    const Address aimer = read<Address>(weapon, Off::kWeaponAimer);
    if (!is_human && is_plausible_ptr(aimer)) {
        const std::int32_t aimer_state = read<std::int32_t>(aimer, 0x18);
        const bool tray_blocked =
            (atk_ctx.active && atk_ctx.order == order && (atk_ctx.last_verdict == 5 || atk_ctx.last_verdict == 4));
        if (aimer_state == 7 && !tray_blocked) return call_orig();
    }

    if (atk_ctx.active && atk_ctx.order == order && atk_ctx.last_verdict == 1) return call_orig();

    DetourState* ds = get_detour_state(order, now);
    if (ds->last_issue_tick != 0 && (now - ds->last_issue_tick) < 1500u) {
        if (!is_human) {
            atk_ctx.in_detour = prev_in_detour;
            return 0;
        }
        return call_orig();
    }
    if (ds->attempt >= 6) return call_orig();

    const float* tpos = native<FnTargetPos>(base, runtime::table().fn.target_pos2)(order + Off::kAttackTarget);
    if (!tpos || !std::isfinite(tpos[0]) || !std::isfinite(tpos[1]) || !std::isfinite(tpos[2])) return call_orig();
    const float tx = tpos[0];
    const float ty = tpos[1];
    const float tz = tpos[2];
    const Address target_ent = read<Address>(order, Off::kAttackTargetEnt);

    const float to_x = tx - ax;
    const float to_y = ty - ay;
    const float dist_to_target = std::sqrt(to_x * to_x + to_y * to_y);
    const float L = spatial_scale();
    if (dist_to_target < 1.5f * L) return call_orig();

    const Address profile = native<FnWeaponProfile>(base, runtime::table().fn.weapon_profile)(weapon);
    float max_range = 0.0f;
    if (native<FnWeaponIsDirect>(base, runtime::table().fn.weapon_is_direct)(weapon) != 0) {
        max_range = native<FnWeaponMaxRange>(base, runtime::table().fn.weapon_max_range)(weapon, target_ent);
    } else if (is_plausible_ptr(profile)) {
        max_range = read<float>(profile, 0x24);
    }
    if (!std::isfinite(max_range) || max_range < 5.0f * L) max_range = dist_to_target + 10.0f * L;

    const Address charger = read<Address>(weapon, Off::kWeaponCharger);
    const Address ammo_def =
        (atk_ctx.active && atk_ctx.order == order)
            ? atk_ctx.ammo_def
            : (is_plausible_ptr(charger)
                   ? native<FnAmmoFromCharger>(base, runtime::table().fn.ammo_from_charger)(charger)
                   : 0);
    const float blast_radius =
        (atk_ctx.active && atk_ctx.order == order) ? atk_ctx.blast_radius : compute_true_blast_radius(ammo_def);
    const float calibre = (atk_ctx.active && atk_ctx.order == order) ? atk_ctx.calibre : 0.0f;
    const float min_safe_target_dist = (blast_radius > 0.5f * L) ? (blast_radius + 2.5f * L) : (1.5f * L);

    const float test_muzzle_h =
        is_human ? (1.35f * L) : ((atk_ctx.active && atk_ctx.order == order) ? atk_ctx.muzzle_height : (1.85f * L));

    const std::int32_t human_stance = is_human ? read<std::int32_t>(chassis, 0x2C) : 0;
    if (is_human && (human_stance == 1 || human_stance == 2)) {
        const float stand_start[3] = {ax, ay, sample_terrain_height(ax, ay, az) + 1.35f * L};
        const float tgt_arr[3] = {tx, ty, tz};
        const int stand_lof =
            test_segment_lof(actor, target_ent, ammo_def, profile, calibre, blast_radius, stand_start, tgt_arr);
        if (stand_lof >= 1) {
            const Address chassis_vt = read<Address>(chassis);
            if (is_plausible_ptr(chassis_vt)) {
                read<FnChassisSetInt>(chassis_vt, Off::kFootSetTypeVOff)(chassis, 0, 0);
                ds->last_issue_tick = now;
                ++ds->attempt;
                log::write(log::Level::verbose, "LOF STAND-UP (inf): actor=0x%llx pos=(%.1f,%.1f) -> clear=%d",
                           static_cast<unsigned long long>(actor), ax, ay, stand_lof);
                atk_ctx.in_detour = prev_in_detour;
                return 0;
            }
        }
    }

    const float fwd_x = to_x / dist_to_target;
    const float fwd_y = to_y / dist_to_target;
    const float lat_x = -fwd_y;
    const float lat_y = fwd_x;

    struct Cand {
        float x, y;
    };
    Cand cands[32]{};
    int cand_count = 0;
    auto push_cand = [&](float x, float y) {
        if (cand_count < 32 && std::isfinite(x) && std::isfinite(y)) cands[cand_count++] = {x, y};
    };

    if (is_human) {
        static constexpr float kLatH[4] = {2.5f, 4.5f, 7.5f, 11.0f};
        static constexpr float kFwdH[3] = {0.0f, 2.5f, -1.5f};
        for (float li : kLatH) {
            for (float fi : kFwdH) {
                const float d_lat = li * L;
                const float d_fwd = fi * L;
                push_cand(ax + lat_x * d_lat + fwd_x * d_fwd, ay + lat_y * d_lat + fwd_y * d_fwd);
                push_cand(ax - lat_x * d_lat + fwd_x * d_fwd, ay - lat_y * d_lat + fwd_y * d_fwd);
            }
        }
    } else {
        static constexpr float kLatV[4] = {4.5f, 7.5f, 11.5f, 16.0f};
        static constexpr float kFwdV[3] = {0.0f, 4.0f, -3.5f};
        for (float li : kLatV) {
            for (float fi : kFwdV) {
                const float d_lat = li * L;
                const float d_fwd = fi * L;
                push_cand(ax + lat_x * d_lat + fwd_x * d_fwd, ay + lat_y * d_lat + fwd_y * d_fwd);
                push_cand(ax - lat_x * d_lat + fwd_x * d_fwd, ay - lat_y * d_lat + fwd_y * d_fwd);
            }
        }
    }

    if (atk_ctx.active && atk_ctx.order == order && atk_ctx.has_blocking_hit) {
        const float bx = atk_ctx.block_hit_xyz[0];
        const float by = atk_ctx.block_hit_xyz[1];
        const float b_dist = std::sqrt((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
        if (b_dist > 1.0f * L && b_dist < 14.0f * L) {
            const float off1 = is_human ? 3.5f * L : 6.0f * L;
            const float off2 = is_human ? 6.5f * L : 10.5f * L;
            push_cand(bx + lat_x * off1 - fwd_x * (1.5f * L), by + lat_y * off1 - fwd_y * (1.5f * L));
            push_cand(bx - lat_x * off1 - fwd_x * (1.5f * L), by - lat_y * off1 - fwd_y * (1.5f * L));
            push_cand(bx + lat_x * off2, by + lat_y * off2);
            push_cand(bx - lat_x * off2, by - lat_y * off2);
        }
    }

    float best_x = 0.0f;
    float best_y = 0.0f;
    float best_cost = 1e30f;
    int best_lof_q = 0;
    bool found = false;
    const float tgt_arr[3] = {tx, ty, tz};

    for (int i = 0; i < cand_count; ++i) {
        const float cx = cands[i].x;
        const float cy = cands[i].y;
        const float mdx = cx - ax;
        const float mdy = cy - ay;
        const float move_dist = std::sqrt(mdx * mdx + mdy * mdy);
        if (move_dist < (is_human ? 1.8f * L : 3.2f * L)) continue;

        if (ds->attempt > 0) {
            const float pdx = cx - ds->last_dest_x;
            const float pdy = cy - ds->last_dest_y;
            if (pdx * pdx + pdy * pdy < (2.5f * L) * (2.5f * L)) continue;
        }

        const float tdx = tx - cx;
        const float tdy = ty - cy;
        const float d_tgt = std::sqrt(tdx * tdx + tdy * tdy);
        if (d_tgt > max_range * 0.94f || d_tgt < min_safe_target_dist) continue;
        if (!is_passable_dry_terrain(cx, cy, is_human)) continue;

        const float cz = sample_terrain_height(cx, cy, az) + test_muzzle_h;
        const float start_arr[3] = {cx, cy, cz};
        const int lof_q =
            test_segment_lof(actor, target_ent, ammo_def, profile, calibre, blast_radius, start_arr, tgt_arr);
        if (lof_q == 0) continue;

        float cost = move_dist;
        if (lof_q == 1) cost += 4.0f * L;
        cost += compute_enemy_exposure_penalty(actor, target_ent, ax, ay, cx, cy, now);
        cost += (static_cast<float>(xorshift32(ds->rng) & 0xFFu) / 255.0f) * (0.15f * L);

        if (cost < best_cost) {
            best_cost = cost;
            best_x = cx;
            best_y = cy;
            best_lof_q = lof_q;
            found = true;
        }
    }

    if (!found) return call_orig();

    const float dest_xy[2] = {best_x, best_y};
    const Address move_child = native<FnMoveTaskPoint>(base, runtime::table().fn.move_task_point)(dest_xy, 0);
    if (!is_plausible_ptr(move_child)) return call_orig();

    runtime::write<float>(order, Off::kAttackLastPos, ax);
    runtime::write<float>(order, Off::kAttackLastPos + 4, ay);
    runtime::write<float>(order, Off::kAttackLastPos + 8, az);
    runtime::write<std::int32_t>(order, Off::kAttackDetourExp, 0);
    runtime::write<std::int32_t>(order, Off::kAttackState, 3);
    runtime::write<Byte>(order, Off::kAttackWaitReady, 1);

    if (is_human) {
        const Address chassis_vt = read<Address>(chassis);
        if (is_plausible_ptr(chassis_vt)) read<FnChassisSetInt>(chassis_vt, Off::kFootSetTypeVOff)(chassis, 0, 0);
    }

    native<FnOrderAttachChild>(base, runtime::table().fn.order_attach_child)(order, move_child);

    ds->last_issue_tick = now;
    ds->last_dest_x = best_x;
    ds->last_dest_y = best_y;
    ++ds->attempt;

    const float move_m = std::sqrt((best_x - ax) * (best_x - ax) + (best_y - ay) * (best_y - ay)) / L;
    if (is_human) {
        const std::uint32_t c = stat_detours_inf.fetch_add(1, std::memory_order_relaxed) + 1;
        log::write(log::Level::verbose,
                   "LOF DETOUR #%u (INF): actor=0x%llx from=(%.1f,%.1f) -> to=(%.1f,%.1f) move=%.1fm lof=%s att=%d", c,
                   static_cast<unsigned long long>(actor), ax, ay, best_x, best_y, move_m,
                   (best_lof_q == 2) ? "clear" : "soft", ds->attempt);
    } else {
        const std::uint32_t c = stat_detours_veh.fetch_add(1, std::memory_order_relaxed) + 1;
        log::write(log::Level::verbose,
                   "LOF DETOUR #%u (VEH): actor=0x%llx from=(%.1f,%.1f) -> to=(%.1f,%.1f) move=%.1fm lof=%s att=%d", c,
                   static_cast<unsigned long long>(actor), ax, ay, best_x, best_y, move_m,
                   (best_lof_q == 2) ? "clear" : "soft", ds->attempt);
    }

    atk_ctx.in_detour = prev_in_detour;
    return 0;
}

} // namespace detail

template <class Create, class Remove>
inline bool prepare_hooks(Address image, const char* digest, Create create, Remove remove) {
    using namespace detail;
    if (!image || !digest || std::strcmp(digest, executable_sha256) != 0 || staged || rollback_pending) return false;

    const auto& h = runtime::table().hook;
    std::array<void*, hook_count> created{};
    unsigned count = 0;
    auto stage = [&](Address rva, void* detour, auto& orig) -> bool {
        void* target = reinterpret_cast<void*>(image + rva);
        void* trampoline = nullptr;
        if (create(target, detour, &trampoline) != 0) return false;
        orig = reinterpret_cast<std::decay_t<decltype(orig)>>(trampoline);
        created[count++] = target;
        return true;
    };

    const bool ok =
        stage(h.attack_check_and_fire, reinterpret_cast<void*>(hook_attack_check_and_fire), orig_attack_check_fire) &&
        stage(h.attack_obstructed, reinterpret_cast<void*>(hook_attack_obstructed), orig_attack_obstructed) &&
        stage(h.target_accessor_try, reinterpret_cast<void*>(hook_target_accessor_try), orig_target_acc_try) &&
        stage(h.bullet_shooter_preview, reinterpret_cast<void*>(hook_bullet_shooter_preview), orig_shooter_preview) &&
        stage(h.collision_eval, reinterpret_cast<void*>(hook_collision_eval), orig_collision_eval);

    if (!ok) {
        bool clean = true;
        while (count)
            clean &= (remove(created[--count]) == 0);
        if (clean) {
            orig_attack_check_fire = nullptr;
            orig_attack_obstructed = nullptr;
            orig_target_acc_try = nullptr;
            orig_shooter_preview = nullptr;
            orig_collision_eval = nullptr;
        } else {
            rollback_pending = true;
        }
        return false;
    }

    base = image;
    staged = true;
    stat_soft_overrides.store(0, std::memory_order_relaxed);
    stat_soft_rejects.store(0, std::memory_order_relaxed);
    stat_detours_inf.store(0, std::memory_order_relaxed);
    stat_detours_veh.store(0, std::memory_order_relaxed);
    last_soft_log_tick = 0;
    gate.activate();
    return true;
}

inline void deactivate() noexcept {
    detail::gate.deactivate();
}

inline std::uint32_t inflight_count() noexcept {
    return detail::gate.count();
}

inline std::uint32_t total_soft_overrides() noexcept {
    return detail::stat_soft_overrides.load(std::memory_order_relaxed);
}

inline std::uint32_t total_soft_rejects() noexcept {
    return detail::stat_soft_rejects.load(std::memory_order_relaxed);
}

inline std::uint32_t total_detours_inf() noexcept {
    return detail::stat_detours_inf.load(std::memory_order_relaxed);
}

inline std::uint32_t total_detours_veh() noexcept {
    return detail::stat_detours_veh.load(std::memory_order_relaxed);
}

inline void after_hooks_removed() {
    using namespace detail;
    gate.deactivate();
    std::unordered_map<Address, DetourState>{}.swap(detours());
    atk_ctx = {};
    soft_ctx = {};
    orig_attack_check_fire = nullptr;
    orig_attack_obstructed = nullptr;
    orig_target_acc_try = nullptr;
    orig_shooter_preview = nullptr;
    orig_collision_eval = nullptr;
    base = 0;
    staged = false;
    rollback_pending = false;
}

} // namespace goh::lof_detour
