// Build 26734796 Host-Only Unified AI Extender Prototype (v4)
// Features:
//   1. Early Flying-Grenade Dodge & Sprint Away Opposite Landing + Enemy Threat
//      (contracts: research/notes/grenade-escape-26734796.md)
//   2. Shoot-Through-Soft-Cover LOF Override on Explicit Order::eAttack
//      (wood walls, fences, trees, soft cover for penetrating / high-caliber weapons,
//       with strict friendly-fire, terrain, hard-cover, and HE self-blast vetoes)
//   3. Smarter Explicit-Attack LOF Detour for BOTH Infantry and Vehicles
//      (structured lateral/corner candidates scored by RNG-free segment LOF & enemy exposure,
//       issuing parent-owned replicating MoveTask children so explicit Attack targets persist)
//      (contracts: research/notes/lof-detour-soft-cover-26734796.md)
//   4. Autonomous infantry/vehicle target prioritization and MG-first shell economy
//      (contract: research/notes/target-prioritization-26734796.md)
//   5. Auto-Smoke for vehicles, pinned infantry squads, and medics
//      (contract: research/notes/auto-smoke-26734796.md)
//   6. BattleCrusher path costs and front-armor reverse path scoring
//      (contract: research/notes/tank-crusher-26734796.md)
//
// Constraints (AGENTS.md):
//   - Zero resource/ modifications; exact PE SHA-256 gated.
//   - Host-only (!IsRemoteClient 0xC2B7A0), serial main thread (!IsQuantWorkerThread 0xA7A80).
//   - Concrete Actor::eHumanBrain (0x110FF88) / Actor::eVehicleBrain (0x101FD68) checks (never *Client).
//   - Respects Direct Control / inhibition ((brain+0x39C & 0x3F) == 0, chassis+0x18 == 0, weaponry+0x70 bit 1 == 0)
//     and Hold Position (brain+0x398 move_mode == 1) for detours.
//   - Outputs strictly through native replicating boundaries; zero custom engine RNG draws.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "MinHook.h"
#include "target_prioritization_module.hpp"
#include "auto_smoke_module.hpp"
#include "tank_crusher_module.hpp"

namespace {

// Exact build 26734796 PE SHA-256
constexpr const char* kExpectedExeSha256 =
    "81a13de787c1869b766004cdcadb9ff99012f0b1b58b89db9a7741b10b9f059a";

constexpr const char* kLogPath =
    "Z:\\Users\\ma9o\\Desktop\\goh-ai-research\\local-work\\grenade_dodge.log";

// Verified build 26734796 RVAs
namespace Rva {
    // Authority / runtime utilities
    constexpr uintptr_t kIsHostEmit         = 0x00C2B740;
    constexpr uintptr_t kIsRemoteClient     = 0x00C2B7A0;
    constexpr uintptr_t kIsQuantWorker      = 0x000A7A80;
    constexpr uintptr_t kClockRead          = 0x0066E7B0;
    constexpr uintptr_t kGameAlloc          = 0x00373650;
    constexpr uintptr_t kGameFree           = 0x00DA1E38;

    // Terrain / orders / weapons / armor / raycast
    constexpr uintptr_t kArmedGrenadeQuery  = 0x009207E0;
    constexpr uintptr_t kTerrainHeight      = 0x00A5D440;
    constexpr uintptr_t kTerrainEnv         = 0x00A62620;
    constexpr uintptr_t kMoveTaskPoint      = 0x0090A8C0;
    constexpr uintptr_t kOrderAttachChild   = 0x0091A180;

    constexpr uintptr_t kWeaponSelect       = 0x0081B080;
    constexpr uintptr_t kWeaponProfile      = 0x0081EC40;
    constexpr uintptr_t kWeaponIsDirect     = 0x0081EC90;
    constexpr uintptr_t kWeaponMaxRange     = 0x0081F710;
    constexpr uintptr_t kAmmoFromCharger    = 0x00820B50;
    constexpr uintptr_t kBulletCast         = 0x008514E0;
    constexpr uintptr_t kPenAtDist          = 0x00841650;
    constexpr uintptr_t kArmorAngleParams   = 0x007FC090;
    constexpr uintptr_t kArmorQueryInit     = 0x007FC310;
    constexpr uintptr_t kArmorThickness     = 0x007FC5B0;
    constexpr uintptr_t kArmorEffThickness  = 0x007FCA60;
    constexpr uintptr_t kTargetPos          = 0x008527F0;
    constexpr uintptr_t kTargetPos2         = 0x00852810;
    constexpr uintptr_t kRelation           = 0x00A65320;
    constexpr uintptr_t kSegQueryInit       = 0x00A409C0;
    constexpr uintptr_t kSegQuerySeg        = 0x00A40A20;
    constexpr uintptr_t kSegQueryRun        = 0x00A40AC0;

    // The host's 11 hook targets. Module headers own their respective entry RVAs.
    constexpr uintptr_t kGrenadeSimTick       = 0x007442F0;
    constexpr uintptr_t kGrenadeSimDtor       = 0x007450E0;
    constexpr uintptr_t kActorSlot23          = 0x00804AC0;
    constexpr uintptr_t kGrenadeScannerUpd    = 0x0091FFA0;
    constexpr uintptr_t kAvoidInnerTick       = 0x00916570;
    constexpr uintptr_t kAvoidPlanEscape      = 0x00917050;
    constexpr uintptr_t kAttackCheckAndFire   = 0x009032F0;
    constexpr uintptr_t kAttackObstructed     = 0x00902ED0;
    constexpr uintptr_t kTargetAccessorTry    = 0x00835790;
    constexpr uintptr_t kBulletShooterPreview = 0x0082D4C0;
    constexpr uintptr_t kCollisionEval        = 0x00836820;

    // Globals & vtables
    constexpr uintptr_t kSpatialScaleL      = 0x0149E750; // float (default 20.0)
    constexpr uintptr_t kGravity            = 0x015754E0; // float (~0.0001962)
    constexpr uintptr_t kScenePtr           = 0x015C9668; // uintptr_t*
    constexpr uintptr_t kSlopeDefaultA      = 0x01495030; // float
    constexpr uintptr_t kSlopeDefaultB      = 0x01495034; // float
    constexpr uintptr_t kHumanBrainVtable   = 0x0110FF88;
    constexpr uintptr_t kVehicleBrainVtable = 0x0101FD68;
    constexpr uintptr_t kAvoidGrenadeVtable = 0x0110D278;
    constexpr uintptr_t kAttackOrderVtable  = 0x0110C018;
}

// Verified offsets (build 26734796)
namespace Off {
    // eEntity / eActor
    constexpr uintptr_t kEntityType30       = 0x0030; // bit 1 actor, bit 2 human
    constexpr uintptr_t kActorFlags38       = 0x0038; // & 0x220 inactive/deleted
    constexpr uintptr_t kEntityPos          = 0x0074; // float[3] XYZ
    constexpr uintptr_t kEntityFlags1C0     = 0x01C0; // bit 28 linked
    constexpr uintptr_t kActorOrderedSub    = 0x0AF0; // order+8 points here
    constexpr uintptr_t kActorOperatableDB4 = 0x0DB4; // bit 8 operatable
    constexpr uintptr_t kActorCover         = 0x0DC0; // +0x24 cover group (0 = outdoors)
    constexpr uintptr_t kActorBrain         = 0x0DC8;
    constexpr uintptr_t kActorSensor        = 0x0DD0; // +0x280/+0x288 cat1 enemy subject vector
    constexpr uintptr_t kActorWeaponry      = 0x0DD8; // +0x70 bit 1 manual weaponry
    constexpr uintptr_t kActorChassis       = 0x0DE0; // +0x18 uint32 direct control

    // Actor::eBrain
    constexpr uintptr_t kBrainOwner         = 0x0038;
    constexpr uintptr_t kBrainCurrentOrder  = 0x00F0;
    constexpr uintptr_t kBrainFlags398      = 0x0398; // bits 12..13 move_mode (0=free,1=hold,2=zone)
    constexpr uintptr_t kBrainFlags39C      = 0x039C; // bits 0..5 inhibit count, bit 25 reconsider

    // Actor::eSensor / eSubject
    constexpr uintptr_t kSensorCat1Begin    = 0x0280;
    constexpr uintptr_t kSensorCat1End      = 0x0288;
    constexpr uintptr_t kSubjectFlags10     = 0x0010; // bit 28 known pos, bit 29 visible
    constexpr uintptr_t kSubjectFlags14     = 0x0014; // bit 1 enemy, bit 4 unusable, bits 7/8 threatening/attacking
    constexpr uintptr_t kSubjectActor       = 0x0018;
    constexpr uintptr_t kSubjectPos         = 0x0028; // float[3]
    constexpr uintptr_t kSubjectObsTime     = 0x0064; // uint32
    constexpr uintptr_t kSubjectSuppress    = 0x006C; // uint8

    // Actor::eGrenadeScanner
    constexpr uintptr_t kScannerBrain       = 0x0008;
    constexpr uintptr_t kScannerActive      = 0x0010; // uint8
    constexpr uintptr_t kScannerDangerZone  = 0x0018; // float (in L units, default 4.0)
    constexpr uintptr_t kScannerEntity      = 0x0028; // uintptr_t

    // Order common / eAvoidGrenade / eAttack
    constexpr uintptr_t kOrderOwner         = 0x0008; // actor + 0xAF0
    constexpr uintptr_t kOrderChild         = 0x0030;
    constexpr uintptr_t kAvoidEntity        = 0x0038;
    constexpr uintptr_t kAvoidRadius        = 0x0048; // float (in L units)
    constexpr uintptr_t kAvoidState         = 0x004C; // int32

    constexpr uintptr_t kAttackState        = 0x0038; // int32 (3=detour, 4=LOF, 5=move-to-target, 6=WeaponFire)
    constexpr uintptr_t kAttackTarget       = 0x0040; // eTarget inline (+0x18 = target entity -> order+0x58)
    constexpr uintptr_t kAttackTargetEnt    = 0x0058;
    constexpr uintptr_t kAttackWaitReady    = 0x00C0; // uint8 one-shot wait-ready rearm
    constexpr uintptr_t kAttackOptC1        = 0x00C1; // uint8 stationary attempt limit option
    constexpr uintptr_t kAttackStatTries    = 0x00D0; // int32
    constexpr uintptr_t kAttackDetourExp    = 0x00D4; // int32
    constexpr uintptr_t kAttackLastPos      = 0x00DC; // float[3]
    constexpr uintptr_t kAttackLastMoveClk  = 0x00E8; // uint32

    // Weapon / Ammo / Volume / Material
    constexpr uintptr_t kWeaponDef          = 0x0098;
    constexpr uintptr_t kWeaponAimer        = 0x00A8;
    constexpr uintptr_t kWeaponCharger      = 0x00B0;
    constexpr uintptr_t kWeaponShooter      = 0x00B8;
    constexpr uintptr_t kWeaponDefCalibre   = 0x1628; // float (mm)
    constexpr uintptr_t kAmmoEffectsBegin   = 0x0480;
    constexpr uintptr_t kAmmoEffectsEnd     = 0x0488;
    constexpr uintptr_t kAmmoPossDmgRad     = 0x0498; // float (world units; 0.1+ on HE/AA/SM/WP patterns)
    constexpr uintptr_t kAmmoDetThickKind   = 0x12B8; // int32 (0 = pierce; 0.5mm on HE/AA/SM/WP)
    constexpr uintptr_t kAmmoDetThickVal    = 0x12BC; // float (mm)
    constexpr uintptr_t kAmmoCumulative     = 0x12C4; // float (> 0 on HEAT / bazooka / panzerfaust)
    constexpr uintptr_t kVolumeDef          = 0x0030;
    constexpr uintptr_t kVolDefMaterial     = 0x0408;
    constexpr uintptr_t kMatSteelEquiv      = 0x00E0; // float (0.05 wood, 0.15 concrete, 1.0 steel)

    // eGrenadeSimulator
    constexpr uintptr_t kSimBody            = 0x0008; // +0x28 center xyz, +0x48 vel xyz
    constexpr uintptr_t kSimEntity          = 0x0308;

    // eChassisFoot vtable slots (build 26734796)
    constexpr uintptr_t kFootSetModeVOff    = 45 * 8; // 0x168 SetMovementMode(chassis, mode, force)
    constexpr uintptr_t kFootSetTypeVOff    = 46 * 8; // 0x170 SetMovementType(chassis, type, force)
}

// Native binary structs (exact verified layouts)
struct Vec3f {
    float x, y, z;
};

struct SceneHit48 {
    uint32_t  kind;       // +0x00: 1/2=terrain/water, 3/4=entity/volume
    float     dist;       // +0x04: cumulative distance
    float     xyz[3];     // +0x08
    uint32_t  pad14;      // +0x14
    uintptr_t entity;     // +0x18
    uintptr_t volume;     // +0x20
    float     normal[3];  // +0x28
    float     dir[3];     // +0x34
    int32_t   face;       // +0x40 (-1 for terrain)
    uint32_t  pad44;      // +0x44
};
static_assert(sizeof(SceneHit48) == 0x48, "SceneHit48 size mismatch");

template <typename T>
struct MsvcVec {
    T* begin;
    T* end;
    T* cap;
};
static_assert(sizeof(MsvcVec<SceneHit48>) == 24, "MsvcVec size mismatch");

struct ArmorHit56 {
    float     impact[3];  // +0x00
    float     normal[3];  // +0x0C
    float     dir[3];     // +0x18
    int32_t   face;       // +0x24
    uintptr_t volume;     // +0x28
    uint8_t   entryFlag;  // +0x30
    uint8_t   pad[7];     // +0x31..+0x37
};
static_assert(sizeof(ArmorHit56) == 0x38, "ArmorHit56 size mismatch");

struct ArmorParams56 {
    uint32_t flags;       // +0x00
    float    calibre;     // +0x04
    float    angleDeg;    // +0x08
    float    slopeA;      // +0x0C
    float    slopeB;      // +0x10
    float    f14;         // +0x14
    float    f18;         // +0x18
    float    f1C;         // +0x1C
    float    f20;         // +0x20
    float    invCos;      // +0x24
    float    outRatio;    // +0x28
    int32_t  dmgKind;     // +0x2C
    float    dmgVal;      // +0x30
    uint32_t pad34;       // +0x34
};
static_assert(sizeof(ArmorParams56) == 0x38, "ArmorParams56 size mismatch");

struct ArmorQuery40 {
    uintptr_t      armor;     // +0x00
    ArmorHit56*    hit;       // +0x08
    uintptr_t      setting;   // +0x10
    ArmorParams56* params;    // +0x18
    float          mult;      // +0x20
    uint8_t        canDamage; // +0x24
    uint8_t        pad[3];    // +0x25..+0x27
};
static_assert(sizeof(ArmorQuery40) == 0x28, "ArmorQuery40 size mismatch");

// Function typedefs
using FnBool0            = uint8_t(__fastcall*)();
using FnClockRead        = uint32_t(__fastcall*)();
using FnGameAlloc        = void*(__fastcall*)(size_t size);
using FnGameFree         = void(__fastcall*)(void* ptr);
using FnArmedGrenade     = uint8_t(__fastcall*)(uintptr_t entity);
using FnTerrainHeight    = float(__fastcall*)(uintptr_t terrain, const float* xy, int level);
using FnTerrainEnv       = uint16_t*(__fastcall*)(uint16_t* out, const float* xy);
using FnMoveTaskPoint    = uintptr_t(__fastcall*)(const float* xy, uint32_t flags);
using FnOrderAttachChild = void(__fastcall*)(uintptr_t parent, uintptr_t child);
using FnChassisSetInt    = void(__fastcall*)(uintptr_t chassis, int32_t val, uint8_t force);

using FnWeaponSelect     = uintptr_t(__fastcall*)(uintptr_t weaponry);
using FnWeaponProfile    = uintptr_t(__fastcall*)(uintptr_t weapon);
using FnWeaponIsDirect   = uint8_t(__fastcall*)(uintptr_t weapon);
using FnWeaponMaxRange   = float(__fastcall*)(uintptr_t weapon, uintptr_t targetEnt);
using FnAmmoFromCharger  = uintptr_t(__fastcall*)(uintptr_t charger);
using FnBulletCast       = uintptr_t(__fastcall*)(uintptr_t ammoDef);
using FnPenAtDist        = float(__fastcall*)(uintptr_t profile, uint32_t distFloatBits); // EDX = float bits, XMM0 return!
using FnArmorAngleParams = void(__fastcall*)(ArmorParams56* params, uintptr_t profile, float cosInc);
using FnArmorQueryInit   = ArmorQuery40*(__fastcall*)(ArmorQuery40* q, uintptr_t ent, uintptr_t src, ArmorHit56* hit, ArmorParams56* params);
using FnArmorThickness   = float(__fastcall*)(ArmorQuery40* q, uint8_t estimateMode);
using FnArmorEffThick    = float(__fastcall*)(ArmorQuery40* q, float rawThick);
using FnTargetPos        = const float*(__fastcall*)(uintptr_t eTarget);
using FnRelation         = int32_t(__fastcall*)(uintptr_t actorA, uintptr_t actorB, uint8_t flag);
using FnEffectRadius     = float(__fastcall*)(uintptr_t effectDesc);
using FnAimerMuzzlePos   = const double*(__fastcall*)(uintptr_t aimer);
using FnSegQueryInit     = void*(__fastcall*)(void* queryBuf, uint32_t flags, uintptr_t ignoreEnt, int32_t param4);
using FnSegQuerySeg      = void(__fastcall*)(void* queryBuf, const float* startXYZ, const float* endXYZ);
using FnSegQueryRun      = uint8_t(__fastcall*)(void* queryBuf, MsvcVec<SceneHit48>* hitsVec);

// Hook typedefs
using FnSimTick          = void(__fastcall*)(uintptr_t sim);
using FnSimDtor          = void*(__fastcall*)(uintptr_t sim, uint32_t flags);
using FnActorSlot23      = void(__fastcall*)(uintptr_t actor);
using FnScannerUpd       = void(__fastcall*)(uintptr_t scanner);
using FnAvoidInnerTick   = uint64_t(__fastcall*)(uintptr_t order);
using FnAvoidPlanEscape  = uint8_t(__fastcall*)(uintptr_t order);
using FnAttackFn         = uint64_t(__fastcall*)(uintptr_t order);
using FnTargetAccTry     = uint32_t(__fastcall*)(uintptr_t* ctx, uintptr_t target, SceneHit48* outHit);
using FnShooterPreview   = void(__fastcall*)(uintptr_t shooter, MsvcVec<SceneHit48>* hitsVec, uintptr_t optInput, MsvcVec<Vec3f>* pathVec, uint32_t flags);
using FnCollisionEval    = uint64_t(__fastcall*)(uintptr_t target, uintptr_t ownerEnt, MsvcVec<SceneHit48>* hitsVec, SceneHit48* outHit, MsvcVec<Vec3f>* pathVec);

// Globals
uintptr_t g_Base = 0;
HMODULE g_SelfModule = nullptr;
HANDLE g_InitThread = nullptr;
CRITICAL_SECTION g_LogCs;
CRITICAL_SECTION g_GrenadeCs;
FILE* g_LogFile = nullptr;
volatile LONG g_HooksActive = 0;

FnBool0            fnIsHostEmit        = nullptr;
FnBool0            fnIsRemoteClient    = nullptr;
FnBool0            fnIsQuantWorker     = nullptr;
FnClockRead        fnClockRead         = nullptr;
FnGameAlloc        fnGameAlloc         = nullptr;
FnGameFree         fnGameFree          = nullptr;
FnArmedGrenade     fnArmedGrenade      = nullptr;
FnTerrainHeight    fnTerrainHeight     = nullptr;
FnTerrainEnv       fnTerrainEnv        = nullptr;
FnMoveTaskPoint    fnMoveTaskPoint     = nullptr;
FnOrderAttachChild fnOrderAttachChild  = nullptr;

FnWeaponSelect     fnWeaponSelect      = nullptr;
FnWeaponProfile    fnWeaponProfile     = nullptr;
FnWeaponIsDirect   fnWeaponIsDirect    = nullptr;
FnWeaponMaxRange   fnWeaponMaxRange    = nullptr;
FnAmmoFromCharger  fnAmmoFromCharger   = nullptr;
FnBulletCast       fnBulletCast        = nullptr;
FnPenAtDist        fnPenAtDist         = nullptr;
FnArmorAngleParams fnArmorAngleParams  = nullptr;
FnArmorQueryInit   fnArmorQueryInit    = nullptr;
FnArmorThickness   fnArmorThickness    = nullptr;
FnArmorEffThick    fnArmorEffThick     = nullptr;
FnTargetPos        fnTargetPos         = nullptr;
FnTargetPos        fnTargetPos2        = nullptr;
FnRelation         fnRelation          = nullptr;
FnSegQueryInit     fnSegQueryInit      = nullptr;
FnSegQuerySeg      fnSegQuerySeg       = nullptr;
FnSegQueryRun      fnSegQueryRun       = nullptr;

FnSimTick          origSimTick         = nullptr;
FnSimDtor          origSimDtor         = nullptr;
FnActorSlot23      origActorSlot23     = nullptr;
FnScannerUpd       origScannerUpd      = nullptr;
FnAvoidInnerTick   origAvoidInnerTick  = nullptr;
FnAvoidPlanEscape  origAvoidPlanEscape = nullptr;
FnAttackFn         origAttackCheckFire = nullptr;
FnAttackFn         origAttackObstructed= nullptr;
FnTargetAccTry     origTargetAccTry    = nullptr;
FnShooterPreview   origShooterPreview  = nullptr;
FnCollisionEval    origCollisionEval   = nullptr;

// ============================================================================
// Feature 1 state: Active live grenade tracking
// ============================================================================
struct TrackedGrenade {
    uintptr_t sim;
    uintptr_t entity;
    uint32_t  lastTick;
    float     x, y, z;
    float     vx, vy, vz;
    float     landX, landY;
    float     flightTime;
    uint8_t   logged;
};
constexpr int kMaxGrenades = 64;
TrackedGrenade g_Grenades[kMaxGrenades];
int g_GrenadeCount = 0;

struct OrderRetry {
    uintptr_t order;
    uint32_t  lastTick;
    int       attempt;
};
constexpr int kMaxRetries = 64;
OrderRetry g_Retries[kMaxRetries];

// ============================================================================
// Feature 2 & 3 state: Explicit Attack LOF + Detour tracking (main thread only)
// ============================================================================
struct AttackEvalCtx {
    bool      active;
    bool      inDetour;
    bool      extendedRequested;
    uintptr_t order;
    uintptr_t actor;
    uintptr_t weapon;
    uintptr_t ammoDef;
    uintptr_t bulletDef;
    uintptr_t profile;
    float     calibre;
    float     blastRadius;
    float     muzzleHeight;
    uint32_t  lastVerdict;
    bool      hasBlockingHit;
    float     blockHitXYZ[3];
    uintptr_t blockEntity;
    uintptr_t blockVolume;
};
struct SoftLofCtx {
    bool      active;
    bool      isExplicit;
    bool      extendedRequested;
    uintptr_t actor;
    uintptr_t weapon;
    uintptr_t ammoDef;
    uintptr_t bulletDef;
    uintptr_t profile;
    float     calibre;
    float     blastRadius;
};
SoftLofCtx g_SoftCtx{}; // serial main simulation thread only

AttackEvalCtx g_AtkCtx{}; // serial main simulation thread only

struct DetourState {
    uintptr_t order;
    uint32_t  lastIssueTick;
    int       attempt;
    float     lastDestX;
    float     lastDestY;
    uint32_t  rng;
};
constexpr int kMaxDetours = 64;
DetourState g_Detours[kMaxDetours];

// Stats
uint32_t g_TotalDodges = 0;
uint32_t g_TotalWakeups = 0;
uint32_t g_TotalSoftOverrides = 0;
uint32_t g_TotalSoftRejects = 0;
uint32_t g_TotalDetoursInf = 0;
uint32_t g_TotalDetoursVeh = 0;
uint32_t g_LastSoftLogTick = 0;

// ============================================================================
// Low-level helpers
// ============================================================================
inline bool IsPlausiblePtr(uintptr_t p) {
    return p >= 0x10000ULL && p < 0x00007FFFFFFFFFFFULL && ((p & 3ULL) == 0);
}

inline uint32_t FloatToBits(float f) {
    uint32_t u = 0;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

inline uint32_t XorShift32(uint32_t& state) {
    uint32_t x = state ? state : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state = x;
    return x;
}

// Exact match for MSVC x64 _Deallocate (aligned >= 0x1000 stores raw pointer at p - 8)
inline void GameFreeMsvcBuffer(void* ptr, size_t byteSize) {
    if (!ptr) return;
    void* raw = ptr;
    if (byteSize >= 0x1000) {
        uintptr_t p = reinterpret_cast<uintptr_t>(ptr);
        uintptr_t orig = *reinterpret_cast<const uintptr_t*>(p - 8);
        if (orig < p && (p - orig) >= 8 && (p - orig) <= 0x27) {
            raw = reinterpret_cast<void*>(orig);
        }
    }
    fnGameFree(raw);
}

void LogMsg(const char* fmt, ...) {
    EnterCriticalSection(&g_LogCs);
    if (!g_LogFile) {
        g_LogFile = std::fopen(kLogPath, "a");
    }
    if (g_LogFile) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        std::fprintf(g_LogFile, "[%02d:%02d:%02d.%03d] ",
                     st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        va_list ap;
        va_start(ap, fmt);
        std::vfprintf(g_LogFile, fmt, ap);
        va_end(ap);
        std::fprintf(g_LogFile, "\n");
        std::fflush(g_LogFile);
    }
    LeaveCriticalSection(&g_LogCs);
}

// Minimal standalone SHA-256
struct Sha256Ctx {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  data[64];
    size_t   datalen;
};

inline uint32_t RotR(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

void Sha256Transform(Sha256Ctx* ctx, const uint8_t d[64]) {
    static const uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };
    uint32_t m[64];
    for (int i = 0; i < 16; ++i) {
        m[i] = (uint32_t(d[i*4]) << 24) | (uint32_t(d[i*4+1]) << 16) |
               (uint32_t(d[i*4+2]) << 8) | uint32_t(d[i*4+3]);
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = RotR(m[i-15], 7) ^ RotR(m[i-15], 18) ^ (m[i-15] >> 3);
        uint32_t s1 = RotR(m[i-2], 17) ^ RotR(m[i-2], 19) ^ (m[i-2] >> 10);
        m[i] = m[i-16] + s0 + m[i-7] + s1;
    }
    uint32_t a=ctx->state[0], b=ctx->state[1], c=ctx->state[2], d0=ctx->state[3];
    uint32_t e=ctx->state[4], f=ctx->state[5], g=ctx->state[6], h=ctx->state[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = RotR(e, 6) ^ RotR(e, 11) ^ RotR(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + S1 + ch + k[i] + m[i];
        uint32_t S0 = RotR(a, 2) ^ RotR(a, 13) ^ RotR(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;
        h = g; g = f; f = e; e = d0 + t1;
        d0 = c; c = b; b = a; a = t1 + t2;
    }
    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d0;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

void Sha256Init(Sha256Ctx* ctx) {
    ctx->datalen = 0; ctx->bitlen = 0;
    ctx->state[0]=0x6a09e667; ctx->state[1]=0xbb67ae85; ctx->state[2]=0x3c6ef372; ctx->state[3]=0xa54ff53a;
    ctx->state[4]=0x510e527f; ctx->state[5]=0x9b05688c; ctx->state[6]=0x1f83d9ab; ctx->state[7]=0x5be0cd19;
}

void Sha256Update(Sha256Ctx* ctx, const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        ctx->data[ctx->datalen++] = data[i];
        if (ctx->datalen == 64) {
            Sha256Transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

void Sha256FinalHex(Sha256Ctx* ctx, char outHex[65]) {
    size_t i = ctx->datalen;
    ctx->data[i++] = 0x80;
    if (i > 56) {
        while (i < 64) ctx->data[i++] = 0;
        Sha256Transform(ctx, ctx->data);
        i = 0;
    }
    while (i < 56) ctx->data[i++] = 0;
    ctx->bitlen += ctx->datalen * 8ULL;
    for (int j = 7; j >= 0; --j) {
        ctx->data[56 + (7 - j)] = uint8_t((ctx->bitlen >> (j * 8)) & 0xFF);
    }
    Sha256Transform(ctx, ctx->data);
    static const char* hex = "0123456789abcdef";
    for (int w = 0; w < 8; ++w) {
        for (int b = 3; b >= 0; --b) {
            uint8_t byte = uint8_t((ctx->state[w] >> (b * 8)) & 0xFF);
            outHex[(w * 4 + (3 - b)) * 2]     = hex[byte >> 4];
            outHex[(w * 4 + (3 - b)) * 2 + 1] = hex[byte & 0xF];
        }
    }
    outHex[64] = '\0';
}

bool VerifyGameExeSha256(char outHex[65]) {
    wchar_t exePath[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, exePath, MAX_PATH * 2);
    if (n == 0 || n >= MAX_PATH * 2) return false;
    HANDLE h = CreateFileW(exePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    Sha256Ctx ctx;
    Sha256Init(&ctx);
    uint8_t buf[65536];
    DWORD readBytes = 0;
    while (ReadFile(h, buf, sizeof(buf), &readBytes, nullptr) && readBytes > 0) {
        Sha256Update(&ctx, buf, readBytes);
    }
    CloseHandle(h);
    Sha256FinalHex(&ctx, outHex);
    return std::strcmp(outHex, kExpectedExeSha256) == 0;
}

inline bool IsHostMainThread() {
    if (!fnIsRemoteClient || !fnIsQuantWorker) return false;
    if (fnIsRemoteClient() != 0) return false;
    if (fnIsQuantWorker() != 0) return false;
    return true;
}

inline float GetSpatialScaleL() {
    float L = *reinterpret_cast<const float*>(g_Base + Rva::kSpatialScaleL);
    return (std::isfinite(L) && L > 1.0f && L < 200.0f) ? L : 20.0f;
}

inline float GetGravity() {
    float g = *reinterpret_cast<const float*>(g_Base + Rva::kGravity);
    return (std::isfinite(g) && g > 1e-7f && g < 1.0f) ? g : 0.0001962f;
}

inline float SampleTerrainHeight(float x, float y, float fallbackZ) {
    uintptr_t scene = *reinterpret_cast<const uintptr_t*>(g_Base + Rva::kScenePtr);
    if (!IsPlausiblePtr(scene)) return fallbackZ;
    uintptr_t terrain = *reinterpret_cast<const uintptr_t*>(scene + 0x18);
    if (!IsPlausiblePtr(terrain)) return fallbackZ;
    float xy[2] = { x, y };
    float h = fnTerrainHeight(terrain, xy, 0);
    return std::isfinite(h) ? h : fallbackZ;
}

inline bool IsPassableDryTerrain(float x, float y, bool isHuman = true) {
    if (!std::isfinite(x) || !std::isfinite(y)) return false;
    if (std::fabs(x) < 1.0f && std::fabs(y) < 1.0f) return false;
    float xy[2] = { x, y };
    uint16_t env = 0;
    fnTerrainEnv(&env, xy);
    uint8_t cls = uint8_t(env & 0xFF);
    // J1: 2=no_pass, 3=shallow_water, 4=water, 5=deep_water, 8=no_pass_human
    if (cls == 2 || cls == 3 || cls == 4 || cls == 5) return false;
    if (isHuman && cls == 8) return false;
    return true;
}

// ============================================================================
// Actor eligibility checks (Host + Concrete Brain + Respect Direct Control)
// ============================================================================
bool IsEligibleHumanForDodge(uintptr_t actor, uintptr_t& outBrain, uintptr_t& outChassis) {
    outBrain = 0;
    outChassis = 0;
    if (!IsPlausiblePtr(actor)) return false;

    uint32_t flags38 = *reinterpret_cast<const uint32_t*>(actor + Off::kActorFlags38);
    if ((flags38 & 0x220u) != 0) return false;

    uint32_t flags1c0 = *reinterpret_cast<const uint32_t*>(actor + Off::kEntityFlags1C0);
    if ((flags1c0 & (1u << 28)) != 0) return false;

    uintptr_t brain = *reinterpret_cast<const uintptr_t*>(actor + Off::kActorBrain);
    if (!IsPlausiblePtr(brain)) return false;

    uintptr_t brainVt = *reinterpret_cast<const uintptr_t*>(brain);
    if (brainVt != g_Base + Rva::kHumanBrainVtable) return false;

    uint32_t brainFlags = *reinterpret_cast<const uint32_t*>(brain + Off::kBrainFlags39C);
    if ((brainFlags & 0x3Fu) != 0) return false;

    uintptr_t chassis = *reinterpret_cast<const uintptr_t*>(actor + Off::kActorChassis);
    if (!IsPlausiblePtr(chassis)) return false;
    if (*reinterpret_cast<const uint32_t*>(chassis + 0x18) != 0) return false;

    uintptr_t weaponry = *reinterpret_cast<const uintptr_t*>(actor + Off::kActorWeaponry);
    if (IsPlausiblePtr(weaponry)) {
        uint32_t wFlags = *reinterpret_cast<const uint32_t*>(weaponry + 0x70);
        if ((wFlags & 2u) != 0) return false;
    }

    outBrain = brain;
    outChassis = chassis;
    return true;
}

bool IsEligibleActorForAttackAI(uintptr_t actor, bool& outIsHuman, uintptr_t& outBrain, uintptr_t& outChassis) {
    outIsHuman = false;
    outBrain = 0;
    outChassis = 0;
    if (!IsPlausiblePtr(actor)) return false;

    uint32_t flags38 = *reinterpret_cast<const uint32_t*>(actor + Off::kActorFlags38);
    if ((flags38 & 0x220u) != 0) return false;

    uintptr_t brain = *reinterpret_cast<const uintptr_t*>(actor + Off::kActorBrain);
    if (!IsPlausiblePtr(brain)) return false;
    if (*reinterpret_cast<const uintptr_t*>(brain + Off::kBrainOwner) != actor) return false;

    // Require concrete host human or vehicle brain (NEVER *Client)
    uintptr_t brainVt = *reinterpret_cast<const uintptr_t*>(brain);
    if (brainVt == g_Base + Rva::kHumanBrainVtable) {
        outIsHuman = true;
    } else if (brainVt == g_Base + Rva::kVehicleBrainVtable) {
        outIsHuman = false;
    } else {
        return false;
    }

    // Respect Direct Control / incapacitation / boarding / inventory exam
    uint32_t brainFlags = *reinterpret_cast<const uint32_t*>(brain + Off::kBrainFlags39C);
    if ((brainFlags & 0x3Fu) != 0) return false;

    uintptr_t chassis = *reinterpret_cast<const uintptr_t*>(actor + Off::kActorChassis);
    if (IsPlausiblePtr(chassis)) {
        if (*reinterpret_cast<const uint32_t*>(chassis + 0x18) != 0) return false;
    }

    uintptr_t weaponry = *reinterpret_cast<const uintptr_t*>(actor + Off::kActorWeaponry);
    if (!IsPlausiblePtr(weaponry)) return false;
    uint32_t wFlags = *reinterpret_cast<const uint32_t*>(weaponry + 0x70);
    if ((wFlags & 2u) != 0) return false; // manual weaponry mode

    outBrain = brain;
    outChassis = chassis;
    return true;
}

// ============================================================================
// FEATURE 1: Early Flying-Grenade Dodge & Sprint Away (proven v2)
// ============================================================================
void PredictLanding(float x0, float y0, float z0, float vx, float vy, float vz,
                    float& outX, float& outY, float& outT) {
    float G = GetGravity();
    float ground0 = SampleTerrainHeight(x0, y0, z0);
    float dz = z0 - ground0;
    float t = 0.0f;
    if (dz > 0.5f || vz > 0.001f) {
        float disc = vz * vz + 2.0f * G * (dz > 0.0f ? dz : 0.0f);
        if (disc > 0.0f) {
            t = (vz + std::sqrt(disc)) / G;
        }
    }
    if (t < 0.0f) t = 0.0f;
    if (t > 2500.0f) t = 2500.0f;

    float px = x0 + vx * t;
    float py = y0 + vy * t;
    if (t > 10.0f) {
        float ground1 = SampleTerrainHeight(px, py, ground0);
        float dz1 = z0 - ground1;
        float disc1 = vz * vz + 2.0f * G * (dz1 > 0.0f ? dz1 : 0.0f);
        if (disc1 > 0.0f) {
            float t1 = (vz + std::sqrt(disc1)) / G;
            if (t1 >= 0.0f && t1 <= 2500.0f) {
                t = t1;
                px = x0 + vx * t;
                py = y0 + vy * t;
            }
        }
    }

    float speedXY = std::sqrt(vx * vx + vy * vy);
    if (t < 150.0f && speedXY > 0.01f) {
        float extraT = 250.0f;
        px += vx * extraT;
        py += vy * extraT;
    }

    outX = std::isfinite(px) ? px : x0;
    outY = std::isfinite(py) ? py : y0;
    outT = t;
}

void PruneStaleGrenades(uint32_t now) {
    int dst = 0;
    for (int i = 0; i < g_GrenadeCount; ++i) {
        uint32_t age = now - g_Grenades[i].lastTick;
        if (age <= 300 && IsPlausiblePtr(g_Grenades[i].entity)) {
            if (dst != i) g_Grenades[dst] = g_Grenades[i];
            ++dst;
        }
    }
    g_GrenadeCount = dst;
}

const TrackedGrenade* FindTrackedByEntity(uintptr_t entity, uint32_t now) {
    for (int i = 0; i < g_GrenadeCount; ++i) {
        if (g_Grenades[i].entity == entity && (now - g_Grenades[i].lastTick) <= 300) {
            return &g_Grenades[i];
        }
    }
    return nullptr;
}

bool IsThreatenedBy(const TrackedGrenade& g, float sx, float sy, float dangerRadius, float maxDetectDist,
                    float& outUrgencyDistSq) {
    float dcx = sx - g.x;
    float dcy = sy - g.y;
    float curDistSq = dcx * dcx + dcy * dcy;
    if (curDistSq > maxDetectDist * maxDetectDist) return false;

    float dlx = sx - g.landX;
    float dly = sy - g.landY;
    float landDistSq = dlx * dlx + dly * dly;
    float radSq = dangerRadius * dangerRadius;

    if (landDistSq < radSq) {
        outUrgencyDistSq = landDistSq;
        return true;
    }

    float speedXY = std::sqrt(g.vx * g.vx + g.vy * g.vy);
    bool airborne = (g.flightTime > 120.0f);
    float dotToward = g.vx * dcx + g.vy * dcy;

    if (airborne) return false;
    if (speedXY > 0.035f && dotToward <= 0.0f) return false;

    if (curDistSq < radSq) {
        outUrgencyDistSq = curDistSq;
        return true;
    }
    return false;
}

bool ComputeAwayFromEnemies(uintptr_t actor, float sx, float sy, uint32_t now, float& outEx, float& outEy) {
    outEx = 0.0f;
    outEy = 0.0f;
    uintptr_t sensor = *reinterpret_cast<const uintptr_t*>(actor + Off::kActorSensor);
    if (!IsPlausiblePtr(sensor)) return false;

    uintptr_t beg = *reinterpret_cast<const uintptr_t*>(sensor + Off::kSensorCat1Begin);
    uintptr_t end = *reinterpret_cast<const uintptr_t*>(sensor + Off::kSensorCat1End);
    if (!IsPlausiblePtr(beg) || !IsPlausiblePtr(end) || end < beg) return false;
    size_t count = (end - beg) / sizeof(uintptr_t);
    if (count == 0 || count > 128) return false;

    float sumX = 0.0f, sumY = 0.0f;
    int used = 0;
    for (size_t i = 0; i < count; ++i) {
        uintptr_t subj = *reinterpret_cast<const uintptr_t*>(beg + i * sizeof(uintptr_t));
        if (!IsPlausiblePtr(subj)) continue;
        if (*reinterpret_cast<const uint8_t*>(subj + Off::kSubjectSuppress) != 0) continue;

        uint32_t f10 = *reinterpret_cast<const uint32_t*>(subj + Off::kSubjectFlags10);
        uint32_t f14 = *reinterpret_cast<const uint32_t*>(subj + Off::kSubjectFlags14);
        if ((f14 & (1u << 1)) == 0) continue;
        if ((f14 & (1u << 4)) != 0) continue;
        if ((f10 & (1u << 28)) == 0) continue;

        uint32_t obsTime = *reinterpret_cast<const uint32_t*>(subj + Off::kSubjectObsTime);
        if ((now - obsTime) > 20000) continue;

        float ex = *reinterpret_cast<const float*>(subj + Off::kSubjectPos);
        float ey = *reinterpret_cast<const float*>(subj + Off::kSubjectPos + 4);
        if (!std::isfinite(ex) || !std::isfinite(ey)) continue;

        float dx = sx - ex;
        float dy = sy - ey;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < 1.0f) continue;

        float w = 1.0f / (dist + 40.0f);
        if ((f10 & (1u << 29)) != 0) w *= 2.0f;
        sumX += (dx / dist) * w;
        sumY += (dy / dist) * w;
        ++used;
    }

    float len = std::sqrt(sumX * sumX + sumY * sumY);
    if (used == 0 || len < 1e-5f) return false;
    outEx = sumX / len;
    outEy = sumY / len;
    return true;
}

int GetAndIncrementRetry(uintptr_t order, uint32_t now) {
    int freeSlot = -1;
    int oldestSlot = 0;
    uint32_t oldestAge = 0;
    for (int i = 0; i < kMaxRetries; ++i) {
        if (g_Retries[i].order == order && (now - g_Retries[i].lastTick) < 10000) {
            g_Retries[i].lastTick = now;
            return g_Retries[i].attempt++;
        }
        uint32_t age = now - g_Retries[i].lastTick;
        if (g_Retries[i].order == 0 || age >= 10000) {
            if (freeSlot < 0) freeSlot = i;
        }
        if (age >= oldestAge) {
            oldestAge = age;
            oldestSlot = i;
        }
    }
    int slot = (freeSlot >= 0) ? freeSlot : oldestSlot;
    g_Retries[slot].order = order;
    g_Retries[slot].lastTick = now;
    g_Retries[slot].attempt = 1;
    return 0;
}

void __fastcall Hook_GrenadeSimTick(uintptr_t sim) {
    origSimTick(sim);
    if (!g_HooksActive || !IsHostMainThread() || !IsPlausiblePtr(sim)) return;

    uintptr_t entity = *reinterpret_cast<const uintptr_t*>(sim + Off::kSimEntity);
    if (!IsPlausiblePtr(entity)) return;

    uint32_t eFlags = *reinterpret_cast<const uint32_t*>(entity + Off::kEntityFlags1C0);
    if ((eFlags & (1u << 28)) != 0) return;
    if (fnArmedGrenade(entity) == 0) return;

    uintptr_t body = sim + Off::kSimBody;
    float x  = *reinterpret_cast<const float*>(body + 0x28);
    float y  = *reinterpret_cast<const float*>(body + 0x2C);
    float z  = *reinterpret_cast<const float*>(body + 0x30);
    float vx = *reinterpret_cast<const float*>(body + 0x48);
    float vy = *reinterpret_cast<const float*>(body + 0x4C);
    float vz = *reinterpret_cast<const float*>(body + 0x50);
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        !std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz)) {
        return;
    }

    uint32_t now = fnClockRead();
    float landX = x, landY = y, flightT = 0.0f;
    PredictLanding(x, y, z, vx, vy, vz, landX, landY, flightT);

    bool shouldLog = false;
    EnterCriticalSection(&g_GrenadeCs);
    PruneStaleGrenades(now);

    int idx = -1;
    for (int i = 0; i < g_GrenadeCount; ++i) {
        if (g_Grenades[i].sim == sim || g_Grenades[i].entity == entity) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        if (g_GrenadeCount < kMaxGrenades) {
            idx = g_GrenadeCount++;
        } else {
            idx = 0;
        }
        std::memset(&g_Grenades[idx], 0, sizeof(TrackedGrenade));
    }

    TrackedGrenade& g = g_Grenades[idx];
    g.sim        = sim;
    g.entity     = entity;
    g.lastTick   = now;
    g.x = x; g.y = y; g.z = z;
    g.vx = vx; g.vy = vy; g.vz = vz;
    g.landX = landX; g.landY = landY;
    g.flightTime = flightT;
    if (!g.logged) {
        g.logged = 1;
        shouldLog = true;
    }
    LeaveCriticalSection(&g_GrenadeCs);

    if (shouldLog) {
        float speed = std::sqrt(vx * vx + vy * vy + vz * vz);
        LogMsg("FLYING GRENADE detected: ent=0x%llx pos=(%.1f,%.1f,%.1f) vel=%.4f predLand=(%.1f,%.1f) t=%.0f",
               (unsigned long long)entity, x, y, z, speed, landX, landY, flightT);
    }
}

void* __fastcall Hook_GrenadeSimDtor(uintptr_t sim, uint32_t flags) {
    if (g_HooksActive) {
        EnterCriticalSection(&g_GrenadeCs);
        for (int i = 0; i < g_GrenadeCount; ++i) {
            if (g_Grenades[i].sim == sim) {
                g_Grenades[i] = g_Grenades[g_GrenadeCount - 1];
                --g_GrenadeCount;
                break;
            }
        }
        LeaveCriticalSection(&g_GrenadeCs);
    }
    return origSimDtor(sim, flags);
}

void __fastcall Hook_ActorSlot23(uintptr_t actor) {
    goh::tank_crusher::ActorScope tankScope(actor);
    if (g_HooksActive && g_GrenadeCount > 0 && IsHostMainThread()) {
        uint32_t now = fnClockRead();
        uintptr_t brain = 0, chassis = 0;
        if (IsEligibleHumanForDodge(actor, brain, chassis)) {
            uintptr_t curOrder = *reinterpret_cast<const uintptr_t*>(brain + Off::kBrainCurrentOrder);
            bool alreadyAvoiding = IsPlausiblePtr(curOrder) &&
                (*reinterpret_cast<const uintptr_t*>(curOrder) == g_Base + Rva::kAvoidGrenadeVtable);

            if (!alreadyAvoiding) {
                float sx = *reinterpret_cast<const float*>(actor + Off::kEntityPos);
                float sy = *reinterpret_cast<const float*>(actor + Off::kEntityPos + 4);
                float L = GetSpatialScaleL();
                float dangerRad = 5.0f * L;
                float detectRad = 22.0f * L;
                bool wake = false;

                EnterCriticalSection(&g_GrenadeCs);
                PruneStaleGrenades(now);
                for (int i = 0; i < g_GrenadeCount; ++i) {
                    float urgSq = 0.0f;
                    if (IsThreatenedBy(g_Grenades[i], sx, sy, dangerRad, detectRad, urgSq)) {
                        wake = true;
                        break;
                    }
                }
                LeaveCriticalSection(&g_GrenadeCs);

                if (wake) {
                    uint32_t* pFlags = reinterpret_cast<uint32_t*>(brain + Off::kBrainFlags39C);
                    if (((*pFlags >> 25) & 1u) == 0) {
                        *pFlags |= (1u << 25);
                        ++g_TotalWakeups;
                    }
                }
            }
        }
    }

    goh::auto_smoke::ActorQuantScope smokeScope(actor);
    struct TargetPrioActorScope {
        uintptr_t owner;
        explicit TargetPrioActorScope(uintptr_t actor) noexcept : owner(actor) {
            goh::target_prio::enter_actor_scope(owner);
        }
        ~TargetPrioActorScope() { goh::target_prio::exit_actor_scope(owner); }
    } scope(actor);
    origActorSlot23(actor);
}

void __fastcall Hook_GrenadeScannerUpd(uintptr_t scanner) {
    origScannerUpd(scanner);
    if (!g_HooksActive || g_GrenadeCount == 0 || !IsHostMainThread() || !IsPlausiblePtr(scanner)) {
        return;
    }

    uintptr_t brain = *reinterpret_cast<const uintptr_t*>(scanner + Off::kScannerBrain);
    if (!IsPlausiblePtr(brain)) return;
    uintptr_t actor = *reinterpret_cast<const uintptr_t*>(brain + Off::kBrainOwner);

    uintptr_t chkBrain = 0, chkChassis = 0;
    if (!IsEligibleHumanForDodge(actor, chkBrain, chkChassis)) return;

    uint32_t now = fnClockRead();
    float sx = *reinterpret_cast<const float*>(actor + Off::kEntityPos);
    float sy = *reinterpret_cast<const float*>(actor + Off::kEntityPos + 4);
    float L = GetSpatialScaleL();

    float cfgDanger = *reinterpret_cast<const float*>(scanner + Off::kScannerDangerZone);
    if (!std::isfinite(cfgDanger) || cfgDanger < 1.0f || cfgDanger > 20.0f) cfgDanger = 4.0f;
    float dangerRad = (cfgDanger + 1.0f) * L;
    float detectRad = 22.0f * L;

    uintptr_t bestEnt = 0;
    float bestUrgSq = 1e30f;

    EnterCriticalSection(&g_GrenadeCs);
    PruneStaleGrenades(now);
    for (int i = 0; i < g_GrenadeCount; ++i) {
        float urgSq = 0.0f;
        if (IsThreatenedBy(g_Grenades[i], sx, sy, dangerRad, detectRad, urgSq)) {
            if (urgSq < bestUrgSq) {
                bestUrgSq = urgSq;
                bestEnt = g_Grenades[i].entity;
            }
        }
    }
    LeaveCriticalSection(&g_GrenadeCs);

    if (bestEnt != 0 && fnArmedGrenade(bestEnt) != 0) {
        *reinterpret_cast<uint8_t*>(scanner + Off::kScannerActive) = 1;
        *reinterpret_cast<uintptr_t*>(scanner + Off::kScannerEntity) = bestEnt;
    }
}

uint8_t __fastcall Hook_AvoidPlanEscape(uintptr_t order);

uint64_t __fastcall Hook_AvoidInnerTick(uintptr_t order) {
    if (g_HooksActive && IsHostMainThread() && IsPlausiblePtr(order)) {
        int32_t state = *reinterpret_cast<const int32_t*>(order + Off::kAvoidState);
        uintptr_t gEnt = *reinterpret_cast<const uintptr_t*>(order + Off::kAvoidEntity);
        uintptr_t ownerSub = *reinterpret_cast<const uintptr_t*>(order + Off::kOrderOwner);

        if (state != 2 && IsPlausiblePtr(gEnt) && IsPlausiblePtr(ownerSub) && ownerSub > Off::kActorOrderedSub) {
            uintptr_t actor = ownerSub - Off::kActorOrderedSub;
            uintptr_t brain = 0, chassis = 0;
            if (IsEligibleHumanForDodge(actor, brain, chassis) && fnArmedGrenade(gEnt) != 0) {
                uint32_t now = fnClockRead();
                TrackedGrenade tgCopy{};
                bool hasTg = false;
                EnterCriticalSection(&g_GrenadeCs);
                const TrackedGrenade* tg = FindTrackedByEntity(gEnt, now);
                if (tg) { tgCopy = *tg; hasTg = true; }
                LeaveCriticalSection(&g_GrenadeCs);

                if (hasTg) {
                    float sx = *reinterpret_cast<const float*>(actor + Off::kEntityPos);
                    float sy = *reinterpret_cast<const float*>(actor + Off::kEntityPos + 4);
                    float L = GetSpatialScaleL();
                    float ordRad = *reinterpret_cast<const float*>(order + Off::kAvoidRadius);
                    if (!std::isfinite(ordRad) || ordRad < 1.0f) ordRad = 4.0f;
                    float dangerRad = (ordRad + 1.0f) * L;
                    float urgSq = 0.0f;
                    bool inDanger = IsThreatenedBy(tgCopy, sx, sy, dangerRad, 25.0f * L, urgSq);

                    if (inDanger) {
                        if (state == 1) {
                            *reinterpret_cast<int32_t*>(order + Off::kAvoidState) = 0;
                        }
                        uintptr_t cover = *reinterpret_cast<const uintptr_t*>(actor + Off::kActorCover);
                        if (IsPlausiblePtr(cover) && *reinterpret_cast<const int32_t*>(cover + 0x24) != 0) {
                            *reinterpret_cast<int32_t*>(order + Off::kAvoidState) = 0;
                            uint8_t ok = Hook_AvoidPlanEscape(order);
                            return ok ? 0ULL : 2ULL;
                        }
                    }
                }
            }
        }
    }
    return origAvoidInnerTick(order);
}

uint8_t __fastcall Hook_AvoidPlanEscape(uintptr_t order) {
    if (!g_HooksActive || !IsHostMainThread() || !IsPlausiblePtr(order)) {
        return origAvoidPlanEscape(order);
    }

    uintptr_t ownerSub = *reinterpret_cast<const uintptr_t*>(order + Off::kOrderOwner);
    uintptr_t gEnt     = *reinterpret_cast<const uintptr_t*>(order + Off::kAvoidEntity);
    if (!IsPlausiblePtr(ownerSub) || ownerSub <= Off::kActorOrderedSub || !IsPlausiblePtr(gEnt)) {
        return origAvoidPlanEscape(order);
    }

    uintptr_t actor = ownerSub - Off::kActorOrderedSub;
    uintptr_t brain = 0, chassis = 0;
    if (!IsEligibleHumanForDodge(actor, brain, chassis)) {
        return origAvoidPlanEscape(order);
    }

    uint32_t now = fnClockRead();
    int attempt = GetAndIncrementRetry(order, now);
    if (attempt >= 5) {
        return origAvoidPlanEscape(order);
    }

    float sx = *reinterpret_cast<const float*>(actor + Off::kEntityPos);
    float sy = *reinterpret_cast<const float*>(actor + Off::kEntityPos + 4);
    float L  = GetSpatialScaleL();

    float ordRad = *reinterpret_cast<const float*>(order + Off::kAvoidRadius);
    if (!std::isfinite(ordRad) || ordRad < 1.0f || ordRad > 20.0f) ordRad = 4.0f;

    float hx = *reinterpret_cast<const float*>(gEnt + Off::kEntityPos);
    float hy = *reinterpret_cast<const float*>(gEnt + Off::kEntityPos + 4);
    float gCurX = hx, gCurY = hy;
    float gvx = 0.0f, gvy = 0.0f;

    TrackedGrenade tgCopy{};
    bool hasTg = false;
    EnterCriticalSection(&g_GrenadeCs);
    const TrackedGrenade* tg = FindTrackedByEntity(gEnt, now);
    if (tg) { tgCopy = *tg; hasTg = true; }
    LeaveCriticalSection(&g_GrenadeCs);
    if (hasTg) {
        hx = tgCopy.landX;
        hy = tgCopy.landY;
        gCurX = tgCopy.x;
        gCurY = tgCopy.y;
        gvx = tgCopy.vx;
        gvy = tgCopy.vy;
    }

    float safeRad = (ordRad + 2.5f) * L;
    float qx = sx - hx, qy = sy - hy;
    float distLand = std::sqrt(qx * qx + qy * qy);
    float dcx = sx - gCurX, dcy = sy - gCurY;
    float distCur = std::sqrt(dcx * dcx + dcy * dcy);

    bool flyingAway = hasTg && (tgCopy.flightTime > 120.0f ||
        ((std::sqrt(gvx * gvx + gvy * gvy) > 0.035f) && (gvx * dcx + gvy * dcy <= 0.0f)));
    if (distLand >= safeRad && (flyingAway || distCur >= (ordRad + 1.5f) * L)) {
        *reinterpret_cast<int32_t*>(order + Off::kAvoidState) = 1;
        return 1;
    }

    float gx = 0.0f, gy = 0.0f;
    if (distLand > 1.0f) {
        gx = qx / distLand;
        gy = qy / distLand;
    }

    float ex = 0.0f, ey = 0.0f;
    bool hasEnemyDir = ComputeAwayFromEnemies(actor, sx, sy, now, ex, ey);
    if (!hasEnemyDir) {
        float vxy = std::sqrt(gvx * gvx + gvy * gvy);
        if (vxy > 0.005f) {
            ex = gvx / vxy;
            ey = gvy / vxy;
            hasEnemyDir = true;
        }
    }

    float ux = 0.0f, uy = 0.0f;
    if (distLand > 1.0f && hasEnemyDir) {
        float bx = 1.35f * gx + 0.85f * ex;
        float by = 1.35f * gy + 0.85f * ey;
        float blen = std::sqrt(bx * bx + by * by);
        if (blen > 0.15f && (bx * gx + by * gy) > 0.0f) {
            ux = bx / blen;
            uy = by / blen;
        } else {
            ux = -gy;
            uy = gx;
            if (ux * ex + uy * ey < 0.0f) { ux = -ux; uy = -uy; }
        }
    } else if (distLand > 1.0f) {
        ux = gx; uy = gy;
    } else if (hasEnemyDir) {
        ux = ex; uy = ey;
    } else {
        ux = 1.0f; uy = 0.0f;
    }

    static const float kAnglesRad[7] = {
        0.0f, 0.55f, -0.55f, 1.15f, -1.15f, 1.75f, -1.75f
    };

    float targetXY[2] = { 0.0f, 0.0f };
    bool foundPoint = false;

    for (int k = 0; k < 7; ++k) {
        int aIdx = (attempt + k) % 7;
        float ca = std::cos(kAnglesRad[aIdx]);
        float sa = std::sin(kAnglesRad[aIdx]);
        float dx = ux * ca - uy * sa;
        float dy = ux * sa + uy * ca;

        float dotQU = qx * dx + qy * dy;
        float disc = dotQU * dotQU + safeRad * safeRad - (qx * qx + qy * qy);
        float s = (disc > 0.0f) ? (-dotQU + std::sqrt(disc)) : safeRad;
        if (s < 2.5f * L) s = 2.5f * L;
        s += 1.5f * L;

        float candX = sx + dx * s;
        float candY = sy + dy * s;

        if (IsPassableDryTerrain(candX, candY, true)) {
            targetXY[0] = candX;
            targetXY[1] = candY;
            foundPoint = true;
            break;
        }
    }

    if (!foundPoint) {
        return origAvoidPlanEscape(order);
    }

    uintptr_t chassisVt = *reinterpret_cast<const uintptr_t*>(chassis);
    if (!IsPlausiblePtr(chassisVt)) return origAvoidPlanEscape(order);

    auto fnSetType = *reinterpret_cast<FnChassisSetInt*>(chassisVt + Off::kFootSetTypeVOff);
    auto fnSetMode = *reinterpret_cast<FnChassisSetInt*>(chassisVt + Off::kFootSetModeVOff);
    fnSetType(chassis, 0, 0);
    fnSetMode(chassis, 2, 0);

    uintptr_t moveChild = fnMoveTaskPoint(targetXY, 0);
    if (!IsPlausiblePtr(moveChild)) {
        return origAvoidPlanEscape(order);
    }
    fnOrderAttachChild(order, moveChild);

    ++g_TotalDodges;
    LogMsg("SPRINT ESCAPE #%u: actor=0x%llx from=(%.1f,%.1f) -> to=(%.1f,%.1f) haz=(%.1f,%.1f) enemyDir=%d att=%d",
           g_TotalDodges, (unsigned long long)actor, sx, sy, targetXY[0], targetXY[1],
           hx, hy, hasEnemyDir ? 1 : 0, attempt);
    return 1;
}

// ============================================================================
// FEATURE 2: Soft-Cover Shoot-Through LOF Override
// ============================================================================

// Compute TRUE max blast radius across PossibleDamageRadius (+0x498) AND EVERY
// effect descriptor in [ammo+0x480, ammo+0x488) via effect->slot5 (+0x28).
// Never trusts 0x7F5870 alone because stock 105mm HE sets +0x498=0.1 while blastwave r1=12.039*L!
float ComputeTrueBlastRadius(uintptr_t ammoDef) {
    if (!IsPlausiblePtr(ammoDef)) return 0.0f;
    float maxR = *reinterpret_cast<const float*>(ammoDef + Off::kAmmoPossDmgRad);
    if (!std::isfinite(maxR) || maxR < 0.0f) maxR = 0.0f;

    uintptr_t beg = *reinterpret_cast<const uintptr_t*>(ammoDef + Off::kAmmoEffectsBegin);
    uintptr_t end = *reinterpret_cast<const uintptr_t*>(ammoDef + Off::kAmmoEffectsEnd);
    if (IsPlausiblePtr(beg) && IsPlausiblePtr(end) && end >= beg) {
        size_t n = (end - beg) / sizeof(uintptr_t);
        if (n <= 32) {
            for (size_t i = 0; i < n; ++i) {
                uintptr_t eff = *reinterpret_cast<const uintptr_t*>(beg + i * sizeof(uintptr_t));
                if (!IsPlausiblePtr(eff)) continue;
                uintptr_t vt = *reinterpret_cast<const uintptr_t*>(eff);
                if (!IsPlausiblePtr(vt)) continue;
                auto fnRad = *reinterpret_cast<const FnEffectRadius*>(vt + 0x28);
                if (!fnRad) continue;
                float r = fnRad(eff);
                if (std::isfinite(r) && r > maxR && r < 5000.0f) {
                    maxR = r;
                }
            }
        }
    }
    return maxR;
}

inline bool IsOperatableAllyHit(uintptr_t ownerEnt, const SceneHit48& h) {
    if (h.kind != 3 && h.kind != 4) return false;
    if (!IsPlausiblePtr(h.entity)) return false;
    uint8_t type30 = *reinterpret_cast<const uint8_t*>(h.entity + Off::kEntityType30);
    if ((type30 & 2u) == 0) return false;
    uint32_t db4 = *reinterpret_cast<const uint32_t*>(h.entity + Off::kActorOperatableDB4);
    if (((db4 >> 8) & 1u) == 0) return false;
    return fnRelation(ownerEnt, h.entity, 0) == 2;
}

// Check if pathVec polyline comes within threshold (0.5 * L) of targetXYZ (matching 0x836820 lines 84-135)
bool PathReachesPoint(const MsvcVec<Vec3f>* pathVec, const float targetXYZ[3], float maxDistSq, float& outMinPathDist) {
    outMinPathDist = 1e30f;
    if (!pathVec || !pathVec->begin || !pathVec->end || pathVec->end <= pathVec->begin) return false;
    size_t count = static_cast<size_t>(pathVec->end - pathVec->begin);
    if (count < 2 || count > 4096) return false;

    float cumDist = 0.0f;
    for (size_t i = 1; i < count; ++i) {
        const Vec3f& a = pathVec->begin[i - 1];
        const Vec3f& b = pathVec->begin[i];
        float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
        float segLenSq = dx * dx + dy * dy + dz * dz;
        float segLen = (segLenSq > 1e-8f) ? std::sqrt(segLenSq) : 0.0f;

        float t = 0.0f;
        if (segLenSq > 1e-8f) {
            t = ((targetXYZ[0] - a.x) * dx + (targetXYZ[1] - a.y) * dy + (targetXYZ[2] - a.z) * dz) / segLenSq;
            if (t < 0.0f) t = 0.0f;
            else if (t > 1.0f) t = 1.0f;
        }
        float cx = a.x + dx * t - targetXYZ[0];
        float cy = a.y + dy * t - targetXYZ[1];
        float cz = a.z + dz * t - targetXYZ[2];
        float dSq = cx * cx + cy * cy + cz * cz;
        if (dSq < maxDistSq) {
            outMinPathDist = cumDist + segLen * t;
            return true;
        }
        cumDist += segLen;
    }
    return false;
}

// Strict piercing-only ammo gate:
// Rejects HE/AA/SM/WP (possibleDamageRadius > 0 or detonationThickness <= 2mm or huge blast > 5m)
// and HEAT/bazooka/panzerfaust (cumulative > 0). Allows kinetic AP/APHE/APCR/bullets.
bool IsPiercingAmmo(uintptr_t ammoDef, float blastRadius, float& outDetThickMm, const char*& outReason) {
    outDetThickMm = 0.0f;
    outReason = "ok";
    if (!IsPlausiblePtr(ammoDef)) {
        outReason = "no_ammo";
        return false;
    }

    float cumul = *reinterpret_cast<const float*>(ammoDef + Off::kAmmoCumulative);
    if (std::isfinite(cumul) && cumul > 0.0f) {
        outReason = "heat_cumulative";
        return false;
    }

    float possRad = *reinterpret_cast<const float*>(ammoDef + Off::kAmmoPossDmgRad);
    if (std::isfinite(possRad) && possRad > 0.0f) {
        outReason = "he_poss_rad";
        return false;
    }

    int32_t detKind = *reinterpret_cast<const int32_t*>(ammoDef + Off::kAmmoDetThickKind);
    float detVal    = *reinterpret_cast<const float*>(ammoDef + Off::kAmmoDetThickVal);
    if (detKind == 0 && std::isfinite(detVal) && detVal > 0.0f) {
        if (detVal <= 2.0f) {
            outReason = "he_det_thick";
            return false;
        }
        outDetThickMm = detVal;
    }

    float L = GetSpatialScaleL();
    if (blastRadius > 5.0f * L) {
        outReason = "large_blast_he";
        return false;
    }

    return true;
}

// Pure read-only evaluation of whether a list of intervening SceneHit48 blockers
// (all strictly before stopDist) can be cleanly pierced by a piercing/kinetic round.
bool EvaluateSoftCoverBlockers(
    uintptr_t ownerEnt,
    uintptr_t targetEnt,
    uintptr_t ammoDef,
    uintptr_t profile,
    float calibre,
    float blastRadius,
    const SceneHit48* hits,
    size_t hitCount,
    float stopDist,
    float& outRemFrac,
    float& outMaxSteelEq,
    float& outSumEffThickMm,
    int& outDistinctLayers,
    const char*& outRejectReason)
{
    outRemFrac = 1.0f;
    outMaxSteelEq = 0.0f;
    outSumEffThickMm = 0.0f;
    outDistinctLayers = 0;
    outRejectReason = "none";

    if (!IsPlausiblePtr(profile) || calibre < 5.0f) {
        outRejectReason = "no_profile_or_cal";
        return false;
    }

    float detThickMm = 0.0f;
    if (!IsPiercingAmmo(ammoDef, blastRadius, detThickMm, outRejectReason)) {
        return false;
    }

    float L = GetSpatialScaleL();
    bool isExplosive = (blastRadius > 0.5f * L);
    float slopeDefA = *reinterpret_cast<const float*>(g_Base + Rva::kSlopeDefaultA);
    float slopeDefB = *reinterpret_cast<const float*>(g_Base + Rva::kSlopeDefaultB);

    // Track distinct volume pointers so entry+exit hits on the same volume are charged once
    constexpr int kMaxSeenVols = 12;
    uintptr_t seenVols[kMaxSeenVols];
    int seenCount = 0;

    float remFrac = 1.0f;

    for (size_t i = 0; i < hitCount; ++i) {
        const SceneHit48& h = hits[i];
        if (h.dist >= stopDist - 0.1f) break;
        if (targetEnt != 0 && h.entity == targetEnt) break;

        // 1. Hard veto on terrain / water / missing volume
        if (h.kind == 1 || h.kind == 2 || !IsPlausiblePtr(h.volume) || !IsPlausiblePtr(h.entity)) {
            outRejectReason = "terrain_or_null";
            return false;
        }

        // 2. Hard veto on any actor hull (allies OR non-target vehicles/guns/humans)
        uint8_t type30 = *reinterpret_cast<const uint8_t*>(h.entity + Off::kEntityType30);
        if ((type30 & 2u) != 0) {
            if (IsOperatableAllyHit(ownerEnt, h)) {
                outRejectReason = "ally";
                return false;
            }
            // Never shoot through another vehicle/tank/gun hull as "soft cover"
            if ((type30 & 4u) == 0) {
                outRejectReason = "vehicle_hull";
                return false;
            }
        }

        // Deduplicate same volume (entry/exit faces of same plank/trunk)
        bool alreadySeen = false;
        for (int s = 0; s < seenCount; ++s) {
            if (seenVols[s] == h.volume) { alreadySeen = true; break; }
        }
        if (alreadySeen) continue;
        if (seenCount >= 6) {
            outRejectReason = "too_many_layers";
            return false;
        }
        seenVols[seenCount++] = h.volume;

        // 3. HE self-blast safety: NEVER detonate explosive rounds near our own unit!
        if (isExplosive && h.dist < (blastRadius + 3.0f * L)) {
            outRejectReason = "he_too_close";
            return false;
        }

        // 4. Read volume definition & material SteelEquivalent
        uintptr_t volDef = *reinterpret_cast<const uintptr_t*>(h.volume + Off::kVolumeDef);
        if (!IsPlausiblePtr(volDef)) {
            outRejectReason = "no_voldef";
            return false;
        }
        uintptr_t volName = *reinterpret_cast<const uintptr_t*>(volDef + 0x398);
        uintptr_t volShape = *reinterpret_cast<const uintptr_t*>(h.volume + 0x50);
        if (!IsPlausiblePtr(volName) || !IsPlausiblePtr(volShape)) {
            outRejectReason = "no_vol_meta";
            return false;
        }
        uintptr_t mat = *reinterpret_cast<const uintptr_t*>(volDef + Off::kVolDefMaterial);
        if (!IsPlausiblePtr(mat)) {
            outRejectReason = "no_material";
            return false;
        }
        float steelEq = *reinterpret_cast<const float*>(mat + Off::kMatSteelEquiv);
        if (!std::isfinite(steelEq) || steelEq < 0.0f) {
            outRejectReason = "bad_steeleq";
            return false;
        }
        if (steelEq > outMaxSteelEq) outMaxSteelEq = steelEq;

        // Material softness tiers (stock wood=0.05, concrete=0.15, steel=1.0)
        bool isSoftWoodFlora = (steelEq <= 0.09f);
        bool isLightMasonry  = (steelEq > 0.09f && steelEq <= 0.165f);
        if (!isSoftWoodFlora && !isLightMasonry) {
            outRejectReason = "hard_material";
            return false;
        }
        if (isLightMasonry && calibre < 75.0f) {
            outRejectReason = "masonry_low_cal";
            return false;
        }

        // 5. Native read-only armor & thickness query (estimate mode 1 = pure, zero error logs)
        float penMm = fnPenAtDist(profile, FloatToBits(h.dist)) * remFrac;
        if (!std::isfinite(penMm)) penMm = 0.0f;

        // Incidence cosine = |dot(unit(normal), unit(dir))|; default 1.0 if normal/dir unpopulated
        float nLenSq = h.normal[0]*h.normal[0] + h.normal[1]*h.normal[1] + h.normal[2]*h.normal[2];
        float dLenSq = h.dir[0]*h.dir[0] + h.dir[1]*h.dir[1] + h.dir[2]*h.dir[2];
        float nd = 1.0f;
        if (nLenSq > 0.01f && dLenSq > 0.01f) {
            nd = std::fabs(h.normal[0] * h.dir[0] + h.normal[1] * h.dir[1] + h.normal[2] * h.dir[2]) /
                 std::sqrt(nLenSq * dLenSq);
        }
        float minCos = isSoftWoodFlora ? 0.50f : 0.25f;
        if (!std::isfinite(nd) || nd < minCos) nd = minCos;
        if (nd > 1.0f) nd = 1.0f;

        ArmorHit56 armorHit{};
        armorHit.impact[0] = h.xyz[0]; armorHit.impact[1] = h.xyz[1]; armorHit.impact[2] = h.xyz[2];
        armorHit.normal[0] = h.normal[0]; armorHit.normal[1] = h.normal[1]; armorHit.normal[2] = h.normal[2];
        armorHit.dir[0]    = h.dir[0]; armorHit.dir[1]    = h.dir[1]; armorHit.dir[2]    = h.dir[2];
        armorHit.face      = (h.face >= 0 && h.face <= 5) ? h.face : 0;
        armorHit.volume    = h.volume;
        armorHit.entryFlag = 1;

        ArmorParams56 params{};
        params.flags   = 0;
        params.calibre = calibre;
        params.slopeA  = slopeDefA;
        params.slopeB  = slopeDefB;
        params.invCos  = 1.0f;
        params.dmgKind = 0; // pierce
        params.dmgVal  = penMm;

        fnArmorAngleParams(&params, profile, nd);

        ArmorQuery40 query{};
        fnArmorQueryInit(&query, h.entity, ownerEnt, &armorHit, &params);

        if (IsPlausiblePtr(query.setting)) {
            uint8_t sFlags = *reinterpret_cast<const uint8_t*>(query.setting + 0xB0);
            if ((sFlags & 1u) != 0) {
                outRejectReason = "immune";
                return false;
            }
            if ((sFlags & 6u) != 0 || isSoftWoodFlora) {
                params.invCos = 1.0f;
            }
            float minCal = *reinterpret_cast<const float*>(query.setting + 0x10);
            if (std::isfinite(minCal) && minCal > 0.0f && calibre < minCal) {
                outRejectReason = "below_min_cal";
                return false;
            }
        } else if (isSoftWoodFlora) {
            params.invCos = 1.0f;
        }

        float rawThick = fnArmorThickness(&query, 1); // estimate mode = pure read-only!
        if (!std::isfinite(rawThick) || rawThick < 0.0f) {
            outRejectReason = "bad_thick";
            return false;
        }
        float effThick = isSoftWoodFlora ? rawThick : fnArmorEffThick(&query, rawThick);
        if (!std::isfinite(effThick) || effThick < 0.0f) {
            effThick = rawThick;
        }
        outSumEffThickMm += effThick;

        // 6. Strict kinetic penetration through soft cover (no HE smash bypass!)
        // Also ensure APHE fuse threshold (if configured) will not detonate on this obstacle.
        if (detThickMm > 0.0f && rawThick >= detThickMm) {
            outRejectReason = "would_fuse";
            return false;
        }

        if (isSoftWoodFlora) {
            if (penMm < effThick * 1.25f || penMm <= 1.0f) {
                outRejectReason = "insufficient_pen";
                return false;
            }
            float loss = effThick / penMm;
            remFrac *= (1.0f - loss);
            if (remFrac < 0.25f) {
                outRejectReason = "low_residual_pen";
                return false;
            }
        } else if (isLightMasonry && calibre >= 75.0f && rawThick <= 25.0f) {
            if (penMm < effThick * 1.50f || penMm <= 1.0f) {
                outRejectReason = "insufficient_pen";
                return false;
            }
            float loss = effThick / penMm;
            remFrac *= (1.0f - loss);
            if (remFrac < 0.35f) {
                outRejectReason = "low_residual_pen";
                return false;
            }
        } else {
            outRejectReason = "insufficient_pen";
            return false;
        }
    }

    outRemFrac = remFrac;
    outDistinctLayers = seenCount;
    return seenCount > 0;
}

// ============================================================================
// HOOK 7: Order::eAttack::CheckAndFire (0x9032F0)
// Scopes active Attack trial context for Hooks 8, 9, and 10.
// ============================================================================
uint64_t __fastcall Hook_AttackCheckAndFire(uintptr_t order) {
    if (!g_HooksActive || !IsHostMainThread() || !IsPlausiblePtr(order)) {
        return origAttackCheckFire(order);
    }

    uintptr_t orderVt = *reinterpret_cast<const uintptr_t*>(order);
    if (orderVt != g_Base + Rva::kAttackOrderVtable) {
        return origAttackCheckFire(order);
    }

    uintptr_t ownerSub = *reinterpret_cast<const uintptr_t*>(order + Off::kOrderOwner);
    if (!IsPlausiblePtr(ownerSub) || ownerSub <= Off::kActorOrderedSub) {
        return origAttackCheckFire(order);
    }
    uintptr_t actor = ownerSub - Off::kActorOrderedSub;

    bool isHuman = false;
    uintptr_t brain = 0, chassis = 0;
    if (!IsEligibleActorForAttackAI(actor, isHuman, brain, chassis)) {
        return origAttackCheckFire(order);
    }

    uintptr_t weaponry = *reinterpret_cast<const uintptr_t*>(actor + Off::kActorWeaponry);
    uintptr_t weapon = fnWeaponSelect(weaponry);
    if (!IsPlausiblePtr(weapon)) {
        return origAttackCheckFire(order);
    }

    uintptr_t weaponDef = *reinterpret_cast<const uintptr_t*>(weapon + Off::kWeaponDef);
    uintptr_t charger   = *reinterpret_cast<const uintptr_t*>(weapon + Off::kWeaponCharger);
    uintptr_t ammoDef   = IsPlausiblePtr(charger) ? fnAmmoFromCharger(charger) : 0;
    uintptr_t bulletDef = IsPlausiblePtr(ammoDef) ? fnBulletCast(ammoDef) : 0;
    uintptr_t profile   = fnWeaponProfile(weapon);

    float calibre = 0.0f;
    if (IsPlausiblePtr(weaponDef)) {
        calibre = *reinterpret_cast<const float*>(weaponDef + Off::kWeaponDefCalibre);
        if (!std::isfinite(calibre) || calibre < 0.0f) calibre = 0.0f;
    }

    float blastRadius = ComputeTrueBlastRadius(ammoDef);

    // Measure current muzzle height above terrain for accurate Feature 3 detour rays
    float L = GetSpatialScaleL();
    float ax = *reinterpret_cast<const float*>(actor + Off::kEntityPos);
    float ay = *reinterpret_cast<const float*>(actor + Off::kEntityPos + 4);
    float az = *reinterpret_cast<const float*>(actor + Off::kEntityPos + 8);
    float groundZ = SampleTerrainHeight(ax, ay, az);
    float muzzleH = isHuman ? (1.35f * L) : (1.85f * L);
    uintptr_t aimer = *reinterpret_cast<const uintptr_t*>(weapon + Off::kWeaponAimer);
    if (IsPlausiblePtr(aimer)) {
        uintptr_t aimerVt = *reinterpret_cast<const uintptr_t*>(aimer);
        if (IsPlausiblePtr(aimerVt)) {
            auto fnMuzzle = *reinterpret_cast<const FnAimerMuzzlePos*>(aimerVt + 0x30);
            if (fnMuzzle) {
                const double* mpos = fnMuzzle(aimer);
                if (mpos && std::isfinite(mpos[2])) {
                    float h = static_cast<float>(mpos[2]) - groundZ;
                    if (h >= 0.35f * L && h <= 4.5f * L) {
                        muzzleH = h;
                    }
                }
            }
        }
    }

    AttackEvalCtx prevCtx = g_AtkCtx;
    g_AtkCtx = {};
    g_AtkCtx.active            = true;
    g_AtkCtx.inDetour          = false;
    g_AtkCtx.extendedRequested = false;
    g_AtkCtx.order             = order;
    g_AtkCtx.actor             = actor;
    g_AtkCtx.weapon            = weapon;
    g_AtkCtx.ammoDef           = ammoDef;
    g_AtkCtx.bulletDef         = bulletDef;
    g_AtkCtx.profile           = profile;
    g_AtkCtx.calibre           = calibre;
    g_AtkCtx.blastRadius       = blastRadius;
    g_AtkCtx.muzzleHeight      = muzzleH;

    uint64_t ret = origAttackCheckFire(order);

    g_AtkCtx = prevCtx;
    return ret;
}

// ============================================================================
// HOOK 7b: TargetAccessor::Try (0x835790)
// Scopes soft-cover shoot-through for ALL live firing checks (explicit Attack,
// autonomous fire-at-will, return-fire, and ongoing WeaponFire — infantry & vehicles).
// ============================================================================
uint32_t __fastcall Hook_TargetAccessorTry(uintptr_t* ctx, uintptr_t target, SceneHit48* outHit) {
    if (!g_HooksActive || g_AtkCtx.inDetour || !IsHostMainThread() ||
        !IsPlausiblePtr(reinterpret_cast<uintptr_t>(ctx))) {
        return origTargetAccTry(ctx, target, outHit);
    }

    // Only apply on live current-position LOF checks (ctx[5] == 0); skip speculative detour point probes
    if (ctx[5] != 0) {
        return origTargetAccTry(ctx, target, outHit);
    }

    uintptr_t weapon = ctx[1];
    uintptr_t actor  = ctx[4];
    if (!IsPlausiblePtr(weapon) || !IsPlausiblePtr(actor)) {
        return origTargetAccTry(ctx, target, outHit);
    }

    bool isHuman = false;
    uintptr_t brain = 0, chassis = 0;
    if (!IsEligibleActorForAttackAI(actor, isHuman, brain, chassis)) {
        return origTargetAccTry(ctx, target, outHit);
    }

    bool isExplicit = (g_AtkCtx.active && g_AtkCtx.actor == actor);
    if (!isExplicit) {
        // Respect Hold Fire (brain+0x398 bits 10..11 == 1) on autonomous checks
        uint32_t b398 = *reinterpret_cast<const uint32_t*>(brain + Off::kBrainFlags398);
        if (((b398 >> 10) & 3u) == 1u) {
            return origTargetAccTry(ctx, target, outHit);
        }
    }

    uintptr_t charger   = *reinterpret_cast<const uintptr_t*>(weapon + Off::kWeaponCharger);
    uintptr_t ammoDef   = IsPlausiblePtr(charger) ? fnAmmoFromCharger(charger) : 0;
    uintptr_t bulletDef = IsPlausiblePtr(ammoDef) ? fnBulletCast(ammoDef) : 0;
    if (!bulletDef) {
        return origTargetAccTry(ctx, target, outHit);
    }

    uintptr_t weaponDef = *reinterpret_cast<const uintptr_t*>(weapon + Off::kWeaponDef);
    uintptr_t profile   = fnWeaponProfile(weapon);
    float calibre = 0.0f;
    if (IsPlausiblePtr(weaponDef)) {
        calibre = *reinterpret_cast<const float*>(weaponDef + Off::kWeaponDefCalibre);
        if (!std::isfinite(calibre) || calibre < 0.0f) calibre = 0.0f;
    }
    float blastRadius = ComputeTrueBlastRadius(ammoDef);

    SoftLofCtx prevSoft = g_SoftCtx;
    g_SoftCtx = {};
    g_SoftCtx.active            = true;
    g_SoftCtx.isExplicit        = isExplicit;
    g_SoftCtx.extendedRequested = false;
    g_SoftCtx.actor             = actor;
    g_SoftCtx.weapon            = weapon;
    g_SoftCtx.ammoDef           = ammoDef;
    g_SoftCtx.bulletDef         = bulletDef;
    g_SoftCtx.profile           = profile;
    g_SoftCtx.calibre           = calibre;
    g_SoftCtx.blastRadius       = blastRadius;

    uint32_t ret = origTargetAccTry(ctx, target, outHit);

    g_SoftCtx = prevSoft;
    return ret;
}

// ============================================================================
// HOOK 8: BulletShooter::slot3 (0x82D4C0)
// Adds bit 22 (0x400000) during live TargetAccessor::Try so native preview
// collects ALL hits along the trajectory using the game's own vectors & zero extra RNG.
// ============================================================================
void __fastcall Hook_BulletShooterPreview(uintptr_t shooter, MsvcVec<SceneHit48>* hitsVec,
                                          uintptr_t optInput, MsvcVec<Vec3f>* pathVec, uint32_t flags) {
    if (g_HooksActive && g_SoftCtx.active && !g_AtkCtx.inDetour && IsHostMainThread() &&
        IsPlausiblePtr(shooter) && g_SoftCtx.bulletDef != 0) {
        uintptr_t shooterWeapon = *reinterpret_cast<const uintptr_t*>(shooter + 8);
        if (shooterWeapon == g_SoftCtx.weapon) {
            g_SoftCtx.extendedRequested = true;
            origShooterPreview(shooter, hitsVec, optInput, pathVec, flags | 0x400000u);
            return;
        }
    }
    origShooterPreview(shooter, hitsVec, optInput, pathVec, flags);
}

// ============================================================================
// HOOK 9: TargetAccessor::CollisionEval (0x836820)
// Evaluates extended trajectory hits: preserves vanilla clear/ally/hard vetoes,
// overrides trajectory-derived 5 -> 3 ONLY when all intervening blockers are soft cover.
// ============================================================================
uint64_t __fastcall Hook_CollisionEval(uintptr_t target, uintptr_t ownerEnt,
                                       MsvcVec<SceneHit48>* hitsVec, SceneHit48* outHit,
                                       MsvcVec<Vec3f>* pathVec) {
    bool trackAtk = (g_AtkCtx.active && !g_AtkCtx.inDetour && ownerEnt == g_AtkCtx.actor);

    if (!g_HooksActive || !g_SoftCtx.active || g_AtkCtx.inDetour || !IsHostMainThread() ||
        ownerEnt != g_SoftCtx.actor) {
        uint64_t v = origCollisionEval(target, ownerEnt, hitsVec, outHit, pathVec);
        if (trackAtk) g_AtkCtx.lastVerdict = static_cast<uint32_t>(v);
        return v;
    }

    if (!g_SoftCtx.extendedRequested) {
        uint64_t v = origCollisionEval(target, ownerEnt, hitsVec, outHit, pathVec);
        if (trackAtk) g_AtkCtx.lastVerdict = static_cast<uint32_t>(v);
        return v;
    }

    g_SoftCtx.extendedRequested = false;

    if (!hitsVec || !hitsVec->begin || !hitsVec->end || hitsVec->end <= hitsVec->begin) {
        uint64_t v = origCollisionEval(target, ownerEnt, hitsVec, outHit, pathVec);
        if (trackAtk) g_AtkCtx.lastVerdict = static_cast<uint32_t>(v);
        return v;
    }

    const SceneHit48* hits = hitsVec->begin;
    size_t hitCount = static_cast<size_t>(hitsVec->end - hitsVec->begin);
    uintptr_t targetEnt = *reinterpret_cast<const uintptr_t*>(target + 0x18);
    const float* targetXYZ = fnTargetPos2(target);
    float L = GetSpatialScaleL();
    float proxSq = (0.5f * L) * (0.5f * L);

    // 1. Check first hit (what vanilla would have seen!)
    if (IsOperatableAllyHit(ownerEnt, hits[0])) {
        if (outHit) *outHit = hits[0];
        uint64_t v = (targetEnt != 0 && hits[0].entity == targetEnt) ? 3ULL : 4ULL;
        if (trackAtk) g_AtkCtx.lastVerdict = static_cast<uint32_t>(v);
        return v;
    }
    if (targetEnt != 0 && (hits[0].kind == 3 || hits[0].kind == 4) && hits[0].entity == targetEnt) {
        if (outHit) *outHit = hits[0];
        if (trackAtk) g_AtkCtx.lastVerdict = 3;
        return 3;
    }

    float minPathDist = 1e30f;
    bool pathReaches = PathReachesPoint(pathVec, targetXYZ, proxSq, minPathDist);
    if (pathReaches && minPathDist <= hits[0].dist + 0.25f * L) {
        if (trackAtk) g_AtkCtx.lastVerdict = 3;
        return 3;
    }

    // hits[0] is a genuine obstruction before target -> record for Feature 3 detour if explicit Attack!
    if (trackAtk) {
        g_AtkCtx.hasBlockingHit = true;
        g_AtkCtx.blockHitXYZ[0] = hits[0].xyz[0];
        g_AtkCtx.blockHitXYZ[1] = hits[0].xyz[1];
        g_AtkCtx.blockHitXYZ[2] = hits[0].xyz[2];
        g_AtkCtx.blockEntity    = hits[0].entity;
        g_AtkCtx.blockVolume    = hits[0].volume;
    }

    // 2. Verify target IS actually reached behind the blockers
    int targetIdx = -1;
    float stopDist = 0.0f;
    if (targetEnt != 0) {
        for (size_t i = 0; i < hitCount; ++i) {
            if ((hits[i].kind == 3 || hits[i].kind == 4) && hits[i].entity == targetEnt) {
                targetIdx = static_cast<int>(i);
                stopDist = hits[i].dist;
                break;
            }
        }
    }
    if (targetIdx < 0) {
        if (!pathReaches) {
            if (outHit) *outHit = hits[0];
            if (trackAtk) g_AtkCtx.lastVerdict = 5;
            return 5;
        }
        stopDist = minPathDist;
    }

    // 3. Strictly check EVERY hit up to stopDist + 2.0*L for friendly actors (never shoot through/into friendlies!)
    for (size_t i = 0; i < hitCount; ++i) {
        if (hits[i].dist > stopDist + 2.0f * L) break;
        if (IsOperatableAllyHit(ownerEnt, hits[i]) && hits[i].entity != targetEnt) {
            if (outHit) *outHit = hits[i];
            uint64_t v = (hits[i].dist < stopDist) ? 4ULL : 5ULL;
            if (trackAtk) g_AtkCtx.lastVerdict = static_cast<uint32_t>(v);
            return v;
        }
    }

    // 4. Evaluate every intervening blocker strictly before stopDist
    float remFrac = 1.0f, maxSteelEq = 0.0f, sumEffThick = 0.0f;
    int layers = 0;
    const char* rejectReason = "none";

    bool softOk = EvaluateSoftCoverBlockers(
        ownerEnt, targetEnt, g_SoftCtx.ammoDef, g_SoftCtx.profile, g_SoftCtx.calibre, g_SoftCtx.blastRadius,
        hits, hitCount, stopDist, remFrac, maxSteelEq, sumEffThick, layers, rejectReason);

    if (softOk) {
        ++g_TotalSoftOverrides;
        if (outHit) {
            *outHit = (targetIdx >= 0) ? hits[targetIdx] : hits[0];
        }
        uint32_t now = fnClockRead();
        if (now - g_LastSoftLogTick > 600) {
            g_LastSoftLogTick = now;
            LogMsg("SOFT-COVER SHOOT-THROUGH #%u [%s]: actor=0x%llx cal=%.1fmm blastR=%.1fm layers=%d steelEq=%.3f thick=%.1fmm rem=%.2f dist=%.1fm",
                   g_TotalSoftOverrides, g_SoftCtx.isExplicit ? "EXP" : "AUTO",
                   (unsigned long long)ownerEnt, g_SoftCtx.calibre,
                   g_SoftCtx.blastRadius / L, layers, maxSteelEq, sumEffThick, remFrac, hits[0].dist / L);
        }
        if (trackAtk) g_AtkCtx.lastVerdict = 3;
        return 3;
    }

    ++g_TotalSoftRejects;
    {
        uint32_t now = fnClockRead();
        if (now - g_LastSoftLogTick > 1000) {
            g_LastSoftLogTick = now;
            LogMsg("SOFT-COVER VETO #%u [%s] (%s): actor=0x%llx cal=%.1fmm blastR=%.1fm steelEq=%.3f thick=%.1fmm dist=%.1fm",
                   g_TotalSoftRejects, g_SoftCtx.isExplicit ? "EXP" : "AUTO", rejectReason,
                   (unsigned long long)ownerEnt, g_SoftCtx.calibre,
                   g_SoftCtx.blastRadius / L, maxSteelEq, sumEffThick, hits[0].dist / L);
        }
    }
    if (outHit) *outHit = hits[0];
    if (trackAtk) g_AtkCtx.lastVerdict = 5;
    return 5;
}

// ============================================================================
// FEATURE 3: Smarter Explicit-Attack LOF Detour (Infantry + Vehicles)
// ============================================================================

DetourState* GetDetourState(uintptr_t order, uint32_t now) {
    int freeSlot = -1;
    int oldestSlot = 0;
    uint32_t oldestAge = 0;
    for (int i = 0; i < kMaxDetours; ++i) {
        if (g_Detours[i].order == order && (now - g_Detours[i].lastIssueTick) < 25000) {
            return &g_Detours[i];
        }
        uint32_t age = now - g_Detours[i].lastIssueTick;
        if (g_Detours[i].order == 0 || age >= 25000) {
            if (freeSlot < 0) freeSlot = i;
        }
        if (age >= oldestAge) {
            oldestAge = age;
            oldestSlot = i;
        }
    }
    int slot = (freeSlot >= 0) ? freeSlot : oldestSlot;
    g_Detours[slot] = {};
    g_Detours[slot].order = order;
    g_Detours[slot].rng = static_cast<uint32_t>((order >> 4) ^ now ^ 0xA5A55A5Au);
    return &g_Detours[slot];
}

// RNG-free single-segment LOF check from startXYZ to endXYZ using game allocator & exact cleanup.
// Returns: 0 = blocked, 1 = soft-cover passable, 2 = completely clear!
int TestSegmentLOF(
    uintptr_t actor,
    uintptr_t targetEnt,
    uintptr_t ammoDef,
    uintptr_t profile,
    float calibre,
    float blastRadius,
    const float startXYZ[3],
    const float endXYZ[3])
{
    alignas(16) uint8_t queryBuf[0x100];
    std::memset(queryBuf, 0, sizeof(queryBuf));

    // Pre-allocate 16-entry SceneHit48 vector using game allocator (0x480 bytes)
    MsvcVec<SceneHit48> hitsVec{};
    SceneHit48* buf = static_cast<SceneHit48*>(fnGameAlloc(16 * sizeof(SceneHit48)));
    if (!buf) return 0;
    hitsVec.begin = buf;
    hitsVec.end   = buf;
    hitsVec.cap   = buf + 16;

    // 0x86803 = exact native bullet collision mask
    fnSegQueryInit(queryBuf, 0x86803u, actor, -1);
    fnSegQuerySeg(queryBuf, startXYZ, endXYZ);
    fnSegQueryRun(queryBuf, &hitsVec);

    // Free internal vector inside queryBuf (+0x90 begin, +0x98 end, +0xA0 cap)
    uintptr_t qBeg = *reinterpret_cast<const uintptr_t*>(queryBuf + 0x90);
    uintptr_t qCap = *reinterpret_cast<const uintptr_t*>(queryBuf + 0xA0);
    if (qBeg != 0) {
        size_t qBytes = (qCap > qBeg) ? ((qCap - qBeg) & ~size_t(7)) : 0;
        GameFreeMsvcBuffer(reinterpret_cast<void*>(qBeg), qBytes);
    }

    int result = 2; // assume completely clear unless blocked
    size_t rawN = (hitsVec.begin && hitsVec.end > hitsVec.begin)
                      ? static_cast<size_t>(hitsVec.end - hitsVec.begin)
                      : 0;

    // Filter raw hits using exact 0x84F5A0 lines 213-215 bullet-blocking predicate:
    //   terrain/water (kind 1/2) OR (volume != 0 && (vol+0x28 & 0x20) == 0 && (vol+0x0C & 2) != 0)
    SceneHit48 filt[32];
    size_t n = 0;
    for (size_t i = 0; i < rawN && n < 32; ++i) {
        const SceneHit48& h = hitsVec.begin[i];
        if (h.kind == 1 || h.kind == 2) {
            filt[n++] = h;
        } else if (IsPlausiblePtr(h.volume)) {
            uint8_t  v28 = *reinterpret_cast<const uint8_t*>(h.volume + 0x28);
            uint32_t v0c = *reinterpret_cast<const uint32_t*>(h.volume + 0x0C);
            if ((v28 & 0x20u) == 0 && (v0c & 2u) != 0) {
                filt[n++] = h;
            }
        }
    }

    float dx = endXYZ[0] - startXYZ[0];
    float dy = endXYZ[1] - startXYZ[1];
    float dz = endXYZ[2] - startXYZ[2];
    float segLen = std::sqrt(dx * dx + dy * dy + dz * dz);
    float L = GetSpatialScaleL();

    if (n > 0) {
        // Find where the target starts along the segment
        float stopDist = segLen - 0.35f * L;
        if (stopDist < 0.0f) stopDist = 0.0f;

        if (targetEnt != 0) {
            for (size_t i = 0; i < n; ++i) {
                if ((filt[i].kind == 3 || filt[i].kind == 4) && filt[i].entity == targetEnt) {
                    stopDist = filt[i].dist;
                    break;
                }
            }
        }

        // Check if any bullet-blocking hit occurs strictly before stopDist
        bool hasBlocker = false;
        for (size_t i = 0; i < n; ++i) {
            if (filt[i].dist >= stopDist - 0.1f) break;
            if (targetEnt != 0 && filt[i].entity == targetEnt) break;
            hasBlocker = true;
            break;
        }

        if (hasBlocker) {
            // Check friendly veto first
            bool allyBlocked = false;
            for (size_t i = 0; i < n; ++i) {
                if (filt[i].dist > stopDist + 2.0f * L) break;
                if (IsOperatableAllyHit(actor, filt[i]) && filt[i].entity != targetEnt) {
                    allyBlocked = true;
                    break;
                }
            }
            if (allyBlocked) {
                result = 0;
            } else if (IsPlausiblePtr(profile) && calibre >= 5.0f) {
                float remFrac = 0.0f, maxSt = 0.0f, sumTh = 0.0f;
                int layers = 0;
                const char* why = nullptr;
                bool softOk = EvaluateSoftCoverBlockers(
                    actor, targetEnt, ammoDef, profile, calibre, blastRadius,
                    filt, n, stopDist, remFrac, maxSt, sumTh, layers, why);
                result = softOk ? 1 : 0;
            } else {
                result = 0;
            }
        }
    }

    // Free hitsVec using game allocator
    if (hitsVec.begin) {
        size_t bytes = (hitsVec.cap > hitsVec.begin)
                           ? static_cast<size_t>(hitsVec.cap - hitsVec.begin) * sizeof(SceneHit48)
                           : 0;
        GameFreeMsvcBuffer(hitsVec.begin, bytes);
    }

    return result;
}

// Compute exposure penalty at candidate (cx, cy) relative to current (ax, ay)
// against committed enemy subjects in sensor+0x280..+0x288 (prefer lower exposure!)
float ComputeEnemyExposurePenalty(uintptr_t actor, uintptr_t targetEnt,
                                  float ax, float ay, float cx, float cy, uint32_t now) {
    uintptr_t sensor = *reinterpret_cast<const uintptr_t*>(actor + Off::kActorSensor);
    if (!IsPlausiblePtr(sensor)) return 0.0f;

    uintptr_t beg = *reinterpret_cast<const uintptr_t*>(sensor + Off::kSensorCat1Begin);
    uintptr_t end = *reinterpret_cast<const uintptr_t*>(sensor + Off::kSensorCat1End);
    if (!IsPlausiblePtr(beg) || !IsPlausiblePtr(end) || end < beg) return 0.0f;
    size_t count = (end - beg) / sizeof(uintptr_t);
    if (count == 0 || count > 128) return 0.0f;

    float penalty = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        uintptr_t subj = *reinterpret_cast<const uintptr_t*>(beg + i * sizeof(uintptr_t));
        if (!IsPlausiblePtr(subj)) continue;
        if (*reinterpret_cast<const uint8_t*>(subj + Off::kSubjectSuppress) != 0) continue;

        uintptr_t subjAct = *reinterpret_cast<const uintptr_t*>(subj + Off::kSubjectActor);
        if (targetEnt != 0 && subjAct == targetEnt) continue;

        uint32_t f10 = *reinterpret_cast<const uint32_t*>(subj + Off::kSubjectFlags10);
        uint32_t f14 = *reinterpret_cast<const uint32_t*>(subj + Off::kSubjectFlags14);
        if ((f14 & (1u << 1)) == 0) continue; // not enemy
        if ((f14 & (1u << 4)) != 0) continue; // unusable / dead
        if ((f10 & (1u << 28)) == 0) continue; // no known pos

        uint32_t obsTime = *reinterpret_cast<const uint32_t*>(subj + Off::kSubjectObsTime);
        if ((now - obsTime) > 20000) continue;

        float ex = *reinterpret_cast<const float*>(subj + Off::kSubjectPos);
        float ey = *reinterpret_cast<const float*>(subj + Off::kSubjectPos + 4);
        if (!std::isfinite(ex) || !std::isfinite(ey)) continue;

        float dNow  = std::sqrt((ax - ex) * (ax - ex) + (ay - ey) * (ay - ey));
        float dCand = std::sqrt((cx - ex) * (cx - ex) + (cy - ey) * (cy - ey));
        float deltaCloser = dNow - dCand; // positive if candidate moves closer to this enemy
        if (deltaCloser > 0.0f) {
            float w = 0.45f;
            if ((f10 & (1u << 29)) != 0) w += 0.35f; // visible
            if ((f14 & ((1u << 7) | (1u << 8))) != 0) w += 0.90f; // threatening or attacking us!
            penalty += deltaCloser * w;
        }
    }
    return penalty;
}

// ============================================================================
// HOOK 10: Order::eAttack::Obstructed (0x902ED0)
// Structured lateral & corner-clearing LOF detour for BOTH infantry and vehicles!
// ============================================================================
uint64_t __fastcall Hook_AttackObstructed(uintptr_t order) {
    // Immediately mark inDetour so nested 0x833C30 calls inside vanilla 0x9575D0 stay untouched
    bool prevInDetour = g_AtkCtx.inDetour;
    g_AtkCtx.inDetour = true;

    auto callOrig = [&]() -> uint64_t {
        uint64_t r = origAttackObstructed(order);
        g_AtkCtx.inDetour = prevInDetour;
        return r;
    };

    if (!g_HooksActive || !IsHostMainThread() || !IsPlausiblePtr(order)) {
        return callOrig();
    }

    uintptr_t orderVt = *reinterpret_cast<const uintptr_t*>(order);
    if (orderVt != g_Base + Rva::kAttackOrderVtable) {
        return callOrig();
    }

    // If a child order is already active, let vanilla handle it
    if (*reinterpret_cast<const uintptr_t*>(order + Off::kOrderChild) != 0) {
        return callOrig();
    }

    uintptr_t ownerSub = *reinterpret_cast<const uintptr_t*>(order + Off::kOrderOwner);
    if (!IsPlausiblePtr(ownerSub) || ownerSub <= Off::kActorOrderedSub) {
        return callOrig();
    }
    uintptr_t actor = ownerSub - Off::kActorOrderedSub;

    bool isHuman = false;
    uintptr_t brain = 0, chassis = 0;
    if (!IsEligibleActorForAttackAI(actor, isHuman, brain, chassis)) {
        return callOrig();
    }
    if (!IsPlausiblePtr(chassis)) {
        return callOrig();
    }

    // Respect Hold Position (brain+0x398 bits 12..13 == 1)!
    uint32_t brainFlags398 = *reinterpret_cast<const uint32_t*>(brain + Off::kBrainFlags398);
    uint32_t moveMode = (brainFlags398 >> 12) & 3u;
    if (moveMode == 1u) {
        return callOrig();
    }

    // Preserve vanilla stationary retry cutoffs (lines 35-51 of 0x902ed0.c)
    float ax = *reinterpret_cast<const float*>(actor + Off::kEntityPos);
    float ay = *reinterpret_cast<const float*>(actor + Off::kEntityPos + 4);
    float az = *reinterpret_cast<const float*>(actor + Off::kEntityPos + 8);
    float lx = *reinterpret_cast<const float*>(order + Off::kAttackLastPos);
    float ly = *reinterpret_cast<const float*>(order + Off::kAttackLastPos + 4);
    float lz = *reinterpret_cast<const float*>(order + Off::kAttackLastPos + 8);
    uint32_t now = fnClockRead();

    bool samePos = (std::fabs(ax - lx) < 1e-3f && std::fabs(ay - ly) < 1e-3f && std::fabs(az - lz) < 1e-3f);
    if (samePos) {
        uint8_t optC1 = *reinterpret_cast<const uint8_t*>(order + Off::kAttackOptC1);
        int32_t statTries = *reinterpret_cast<const int32_t*>(order + Off::kAttackStatTries);
        if (optC1 != 0 && statTries >= 20) {
            return callOrig();
        }
        uint32_t lastMoveClk = *reinterpret_cast<const uint32_t*>(order + Off::kAttackLastMoveClk);
        if ((now - lastMoveClk) >= 10000u) {
            return callOrig();
        }
    }

    // Reacquire active weapon & check if vehicle simply needs hull rotation (aimer state 7 + verdict 1)
    uintptr_t weaponry = *reinterpret_cast<const uintptr_t*>(actor + Off::kActorWeaponry);
    uintptr_t weapon = fnWeaponSelect(weaponry);
    if (!IsPlausiblePtr(weapon)) {
        return callOrig();
    }
    uintptr_t aimer = *reinterpret_cast<const uintptr_t*>(weapon + Off::kWeaponAimer);
    if (!isHuman && IsPlausiblePtr(aimer)) {
        int32_t aimerState = *reinterpret_cast<const int32_t*>(aimer + 0x18);
        bool trayBlocked = (g_AtkCtx.active && g_AtkCtx.order == order &&
                            (g_AtkCtx.lastVerdict == 5 || g_AtkCtx.lastVerdict == 4));
        if (aimerState == 7 && !trayBlocked) {
            // Casemate / limited-traverse gun needs hull rotation (eLookAt) first!
            return callOrig();
        }
    }

    // Only detour on actual LOF obstruction / friendly-in-way / ballistic blockage
    if (g_AtkCtx.active && g_AtkCtx.order == order) {
        if (g_AtkCtx.lastVerdict == 1) {
            return callOrig();
        }
    }

    DetourState* ds = GetDetourState(order, now);
    if (ds->lastIssueTick != 0 && (now - ds->lastIssueTick) < 1500u) {
        // Cooldown between detour attempts so unit isn't thrashing
        if (!isHuman) {
            g_AtkCtx.inDetour = prevInDetour;
            return 0;
        }
        return callOrig();
    }
    if (ds->attempt >= 6) {
        return callOrig();
    }

    // Target geometry & weapon max range
    const float* tpos = fnTargetPos2(order + Off::kAttackTarget);
    if (!tpos || !std::isfinite(tpos[0]) || !std::isfinite(tpos[1]) || !std::isfinite(tpos[2])) {
        return callOrig();
    }
    float tx = tpos[0], ty = tpos[1], tz = tpos[2];
    uintptr_t targetEnt = *reinterpret_cast<const uintptr_t*>(order + Off::kAttackTargetEnt);

    float toX = tx - ax;
    float toY = ty - ay;
    float distToTarget = std::sqrt(toX * toX + toY * toY);
    float L = GetSpatialScaleL();
    if (distToTarget < 1.5f * L) {
        return callOrig();
    }

    uintptr_t profile = fnWeaponProfile(weapon);
    float maxRange = 0.0f;
    if (fnWeaponIsDirect(weapon) != 0) {
        maxRange = fnWeaponMaxRange(weapon, targetEnt);
    } else if (IsPlausiblePtr(profile)) {
        maxRange = *reinterpret_cast<const float*>(profile + 0x24);
    }
    if (!std::isfinite(maxRange) || maxRange < 5.0f * L) {
        maxRange = distToTarget + 10.0f * L;
    }

    uintptr_t charger = *reinterpret_cast<const uintptr_t*>(weapon + Off::kWeaponCharger);
    uintptr_t ammoDef = (g_AtkCtx.active && g_AtkCtx.order == order)
                            ? g_AtkCtx.ammoDef
                            : (IsPlausiblePtr(charger) ? fnAmmoFromCharger(charger) : 0);
    float blastRadius = (g_AtkCtx.active && g_AtkCtx.order == order)
                            ? g_AtkCtx.blastRadius
                            : ComputeTrueBlastRadius(ammoDef);
    float calibre = (g_AtkCtx.active && g_AtkCtx.order == order) ? g_AtkCtx.calibre : 0.0f;
    float minSafeTargetDist = (blastRadius > 0.5f * L) ? (blastRadius + 2.5f * L) : (1.5f * L);

    float testMuzzleH = isHuman ? (1.35f * L)
                                : ((g_AtkCtx.active && g_AtkCtx.order == order) ? g_AtkCtx.muzzleHeight : (1.85f * L));

    // Infantry quick win: if currently crouched/prone and standing in place ALREADY clears LOF -> stand up!
    if (isHuman && g_AtkCtx.active && g_AtkCtx.order == order && g_AtkCtx.muzzleHeight < 1.05f * L) {
        float standStart[3] = { ax, ay, SampleTerrainHeight(ax, ay, az) + 1.35f * L };
        float tgtArr[3] = { tx, ty, tz };
        int standLof = TestSegmentLOF(actor, targetEnt, ammoDef, profile, calibre, blastRadius, standStart, tgtArr);
        if (standLof >= 1) {
            uintptr_t chassisVt = *reinterpret_cast<const uintptr_t*>(chassis);
            if (IsPlausiblePtr(chassisVt)) {
                auto fnSetType = *reinterpret_cast<FnChassisSetInt*>(chassisVt + Off::kFootSetTypeVOff);
                fnSetType(chassis, 0, 0); // stand up via replicating setter!
                ds->lastIssueTick = now;
                ++ds->attempt;
                LogMsg("LOF STAND-UP (inf): actor=0x%llx pos=(%.1f,%.1f) -> clear=%d",
                       (unsigned long long)actor, ax, ay, standLof);
                g_AtkCtx.inDetour = prevInDetour;
                return 0;
            }
        }
    }

    // Build structured lateral & corner candidates perpendicular to attacker->target line
    float fwdX = toX / distToTarget;
    float fwdY = toY / distToTarget;
    float latX = -fwdY;
    float latY = fwdX;

    struct Cand { float x, y; };
    Cand cands[32];
    int candCount = 0;

    auto pushCand = [&](float x, float y) {
        if (candCount < 32 && std::isfinite(x) && std::isfinite(y)) {
            cands[candCount++] = { x, y };
        }
    };

    if (isHuman) {
        static const float kLatH[4] = { 2.5f, 4.5f, 7.5f, 11.0f };
        static const float kFwdH[3] = { 0.0f, 2.5f, -1.5f };
        for (int li = 0; li < 4; ++li) {
            for (int fi = 0; fi < 3; ++fi) {
                float dLat = kLatH[li] * L;
                float dFwd = kFwdH[fi] * L;
                pushCand(ax + latX * dLat + fwdX * dFwd, ay + latY * dLat + fwdY * dFwd);
                pushCand(ax - latX * dLat + fwdX * dFwd, ay - latY * dLat + fwdY * dFwd);
            }
        }
    } else {
        // Vehicles need wider lateral offsets to clear corners cleanly and support reverse-flank
        static const float kLatV[4] = { 4.5f, 7.5f, 11.5f, 16.0f };
        static const float kFwdV[3] = { 0.0f, 4.0f, -3.5f };
        for (int li = 0; li < 4; ++li) {
            for (int fi = 0; fi < 3; ++fi) {
                float dLat = kLatV[li] * L;
                float dFwd = kFwdV[fi] * L;
                pushCand(ax + latX * dLat + fwdX * dFwd, ay + latY * dLat + fwdY * dFwd);
                pushCand(ax - latX * dLat + fwdX * dFwd, ay - latY * dLat + fwdY * dFwd);
            }
        }
    }

    // Add blocking-obstacle corner candidates if we captured the blocking hit point!
    if (g_AtkCtx.active && g_AtkCtx.order == order && g_AtkCtx.hasBlockingHit) {
        float bx = g_AtkCtx.blockHitXYZ[0];
        float by = g_AtkCtx.blockHitXYZ[1];
        float bDist = std::sqrt((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
        if (bDist > 1.0f * L && bDist < 14.0f * L) {
            float off1 = isHuman ? 3.5f * L : 6.0f * L;
            float off2 = isHuman ? 6.5f * L : 10.5f * L;
            pushCand(bx + latX * off1 - fwdX * (1.5f * L), by + latY * off1 - fwdY * (1.5f * L));
            pushCand(bx - latX * off1 - fwdX * (1.5f * L), by - latY * off1 - fwdY * (1.5f * L));
            pushCand(bx + latX * off2, by + latY * off2);
            pushCand(bx - latX * off2, by - latY * off2);
        }
    }

    float bestX = 0.0f, bestY = 0.0f;
    float bestCost = 1e30f;
    int bestLofQuality = 0;
    bool found = false;
    float tgtArr[3] = { tx, ty, tz };

    for (int i = 0; i < candCount; ++i) {
        float cx = cands[i].x;
        float cy = cands[i].y;

        float mdx = cx - ax, mdy = cy - ay;
        float moveDist = std::sqrt(mdx * mdx + mdy * mdy);
        if (moveDist < (isHuman ? 1.8f * L : 3.2f * L)) continue;

        // Avoid re-picking the exact same spot we just failed from
        if (ds->attempt > 0) {
            float pdx = cx - ds->lastDestX, pdy = cy - ds->lastDestY;
            if (pdx * pdx + pdy * pdy < (2.5f * L) * (2.5f * L)) continue;
        }

        // Check range & safe blast distance to target
        float tdx = tx - cx, tdy = ty - cy;
        float dTgt = std::sqrt(tdx * tdx + tdy * tdy);
        if (dTgt > maxRange * 0.94f || dTgt < minSafeTargetDist) continue;

        // Check passable dry terrain
        if (!IsPassableDryTerrain(cx, cy, isHuman)) continue;

        // RNG-free segment LOF test from candidate -> target
        float cz = SampleTerrainHeight(cx, cy, az) + testMuzzleH;
        float startArr[3] = { cx, cy, cz };
        int lofQ = TestSegmentLOF(actor, targetEnt, ammoDef, profile, calibre, blastRadius, startArr, tgtArr);
        if (lofQ == 0) continue;

        // Score candidate: prefer minimal move, completely clear lane, and lower enemy exposure
        float cost = moveDist;
        if (lofQ == 1) cost += 4.0f * L; // prefer completely clear over soft-cover lane
        cost += ComputeEnemyExposurePenalty(actor, targetEnt, ax, ay, cx, cy, now);
        // Tiny deterministic private-PRNG tie-breaker (zero engine RNG!)
        cost += (static_cast<float>(XorShift32(ds->rng) & 0xFFu) / 255.0f) * (0.15f * L);

        if (cost < bestCost) {
            bestCost = cost;
            bestX = cx;
            bestY = cy;
            bestLofQuality = lofQ;
            found = true;
        }
    }

    if (!found) {
        return callOrig();
    }

    // Issue native replicating Order::eMoveTask child owned by Order::eAttack (preserving Attack target!)
    float destXY[2] = { bestX, bestY };
    uintptr_t moveChild = fnMoveTaskPoint(destXY, 0);
    if (!IsPlausiblePtr(moveChild)) {
        return callOrig();
    }

    // Update native stationary position snapshot (matching 0x902ed0 lines 58-59) & transition to state 3
    *reinterpret_cast<float*>(order + Off::kAttackLastPos)     = ax;
    *reinterpret_cast<float*>(order + Off::kAttackLastPos + 4) = ay;
    *reinterpret_cast<float*>(order + Off::kAttackLastPos + 8) = az;
    *reinterpret_cast<int32_t*>(order + Off::kAttackDetourExp) = 0;
    *reinterpret_cast<int32_t*>(order + Off::kAttackState)     = 3;
    *reinterpret_cast<uint8_t*>(order + Off::kAttackWaitReady) = 1;

    // For infantry, ensure standing stance if currently prone/crouched
    if (isHuman) {
        uintptr_t chassisVt = *reinterpret_cast<const uintptr_t*>(chassis);
        if (IsPlausiblePtr(chassisVt)) {
            auto fnSetType = *reinterpret_cast<FnChassisSetInt*>(chassisVt + Off::kFootSetTypeVOff);
            fnSetType(chassis, 0, 0);
        }
    }

    fnOrderAttachChild(order, moveChild);

    ds->lastIssueTick = now;
    ds->lastDestX = bestX;
    ds->lastDestY = bestY;
    ++ds->attempt;

    float moveMeters = std::sqrt((bestX - ax) * (bestX - ax) + (bestY - ay) * (bestY - ay)) / L;
    if (isHuman) {
        ++g_TotalDetoursInf;
        LogMsg("LOF DETOUR #%u (INF): actor=0x%llx from=(%.1f,%.1f) -> to=(%.1f,%.1f) move=%.1fm lof=%s att=%d",
               g_TotalDetoursInf, (unsigned long long)actor, ax, ay, bestX, bestY,
               moveMeters, (bestLofQuality == 2) ? "clear" : "soft", ds->attempt);
    } else {
        ++g_TotalDetoursVeh;
        LogMsg("LOF DETOUR #%u (VEH): actor=0x%llx from=(%.1f,%.1f) -> to=(%.1f,%.1f) move=%.1fm lof=%s att=%d",
               g_TotalDetoursVeh, (unsigned long long)actor, ax, ay, bestX, bestY,
               moveMeters, (bestLofQuality == 2) ? "clear" : "soft", ds->attempt);
    }

    g_AtkCtx.inDetour = prevInDetour;
    return 0;
}

// ============================================================================
// Init / Shutdown
// ============================================================================
DWORD WINAPI InitThread(LPVOID) {
    LogMsg("=== goh_ai InitThread starting (35 hooks including 3 tank pathing hooks) ===");

    g_Base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    LogMsg("Game module base: 0x%llx", (unsigned long long)g_Base);

    char actualSha[65] = {};
    if (!VerifyGameExeSha256(actualSha)) {
        LogMsg("ABORT: PE SHA-256 mismatch! actual=%s expected=%s", actualSha, kExpectedExeSha256);
        return 1;
    }
    LogMsg("PE SHA-256 verified: %s (build 26734796)", actualSha);

    // Resolve helpers
    fnIsHostEmit        = reinterpret_cast<FnBool0>(g_Base + Rva::kIsHostEmit);
    fnIsRemoteClient    = reinterpret_cast<FnBool0>(g_Base + Rva::kIsRemoteClient);
    fnIsQuantWorker     = reinterpret_cast<FnBool0>(g_Base + Rva::kIsQuantWorker);
    fnClockRead         = reinterpret_cast<FnClockRead>(g_Base + Rva::kClockRead);
    fnGameAlloc         = reinterpret_cast<FnGameAlloc>(g_Base + Rva::kGameAlloc);
    fnGameFree          = reinterpret_cast<FnGameFree>(g_Base + Rva::kGameFree);
    fnArmedGrenade      = reinterpret_cast<FnArmedGrenade>(g_Base + Rva::kArmedGrenadeQuery);
    fnTerrainHeight     = reinterpret_cast<FnTerrainHeight>(g_Base + Rva::kTerrainHeight);
    fnTerrainEnv        = reinterpret_cast<FnTerrainEnv>(g_Base + Rva::kTerrainEnv);
    fnMoveTaskPoint     = reinterpret_cast<FnMoveTaskPoint>(g_Base + Rva::kMoveTaskPoint);
    fnOrderAttachChild  = reinterpret_cast<FnOrderAttachChild>(g_Base + Rva::kOrderAttachChild);

    fnWeaponSelect      = reinterpret_cast<FnWeaponSelect>(g_Base + Rva::kWeaponSelect);
    fnWeaponProfile     = reinterpret_cast<FnWeaponProfile>(g_Base + Rva::kWeaponProfile);
    fnWeaponIsDirect    = reinterpret_cast<FnWeaponIsDirect>(g_Base + Rva::kWeaponIsDirect);
    fnWeaponMaxRange    = reinterpret_cast<FnWeaponMaxRange>(g_Base + Rva::kWeaponMaxRange);
    fnAmmoFromCharger   = reinterpret_cast<FnAmmoFromCharger>(g_Base + Rva::kAmmoFromCharger);
    fnBulletCast        = reinterpret_cast<FnBulletCast>(g_Base + Rva::kBulletCast);
    fnPenAtDist         = reinterpret_cast<FnPenAtDist>(g_Base + Rva::kPenAtDist);
    fnArmorAngleParams  = reinterpret_cast<FnArmorAngleParams>(g_Base + Rva::kArmorAngleParams);
    fnArmorQueryInit    = reinterpret_cast<FnArmorQueryInit>(g_Base + Rva::kArmorQueryInit);
    fnArmorThickness    = reinterpret_cast<FnArmorThickness>(g_Base + Rva::kArmorThickness);
    fnArmorEffThick     = reinterpret_cast<FnArmorEffThick>(g_Base + Rva::kArmorEffThickness);
    fnTargetPos         = reinterpret_cast<FnTargetPos>(g_Base + Rva::kTargetPos);
    fnTargetPos2        = reinterpret_cast<FnTargetPos>(g_Base + Rva::kTargetPos2);
    fnRelation          = reinterpret_cast<FnRelation>(g_Base + Rva::kRelation);
    fnSegQueryInit      = reinterpret_cast<FnSegQueryInit>(g_Base + Rva::kSegQueryInit);
    fnSegQuerySeg       = reinterpret_cast<FnSegQuerySeg>(g_Base + Rva::kSegQuerySeg);
    fnSegQueryRun       = reinterpret_cast<FnSegQueryRun>(g_Base + Rva::kSegQueryRun);

    LogMsg("Initial state: remote=%u hostEmit=%u clock=%u L=%.1f G=%.7f",
           (unsigned)fnIsRemoteClient(), (unsigned)fnIsHostEmit(),
           fnClockRead(), GetSpatialScaleL(), GetGravity());

    if (MH_Initialize() != MH_OK) {
        LogMsg("ERROR: MH_Initialize failed");
        return 1;
    }

    auto addHook = [](const char* name, uintptr_t rva, void* detour, void** orig) -> bool {
        void* target = reinterpret_cast<void*>(g_Base + rva);
        MH_STATUS st = MH_CreateHook(target, detour, orig);
        if (st != MH_OK) {
            LogMsg("ERROR: MH_CreateHook(%s @ 0x%llx) failed: %d", name, (unsigned long long)rva, (int)st);
            return false;
        }
        LogMsg("Hook staged: %s @ 0x%llx", name, (unsigned long long)rva);
        return true;
    };

    bool ok = true;
    // Grenade dodge (6 hooks)
    ok &= addHook("eGrenadeSimulator::Tick",      Rva::kGrenadeSimTick,       reinterpret_cast<void*>(Hook_GrenadeSimTick),       reinterpret_cast<void**>(&origSimTick));
    ok &= addHook("eGrenadeSimulator::~dtor",     Rva::kGrenadeSimDtor,       reinterpret_cast<void*>(Hook_GrenadeSimDtor),       reinterpret_cast<void**>(&origSimDtor));
    ok &= addHook("eActor::slot23",               Rva::kActorSlot23,          reinterpret_cast<void*>(Hook_ActorSlot23),          reinterpret_cast<void**>(&origActorSlot23));
    ok &= addHook("eGrenadeScanner::Update",      Rva::kGrenadeScannerUpd,    reinterpret_cast<void*>(Hook_GrenadeScannerUpd),    reinterpret_cast<void**>(&origScannerUpd));
    ok &= addHook("eAvoidGrenade::InnerTick",     Rva::kAvoidInnerTick,       reinterpret_cast<void*>(Hook_AvoidInnerTick),       reinterpret_cast<void**>(&origAvoidInnerTick));
    ok &= addHook("eAvoidGrenade::PlanEscape",    Rva::kAvoidPlanEscape,      reinterpret_cast<void*>(Hook_AvoidPlanEscape),      reinterpret_cast<void**>(&origAvoidPlanEscape));
    // Soft-cover LOF override (general + explicit) + Smart Attack detour (5 hooks)
    ok &= addHook("Order::eAttack::CheckAndFire", Rva::kAttackCheckAndFire,   reinterpret_cast<void*>(Hook_AttackCheckAndFire),   reinterpret_cast<void**>(&origAttackCheckFire));
    ok &= addHook("Order::eAttack::Obstructed",   Rva::kAttackObstructed,     reinterpret_cast<void*>(Hook_AttackObstructed),     reinterpret_cast<void**>(&origAttackObstructed));
    ok &= addHook("TargetAccessor::Try",          Rva::kTargetAccessorTry,    reinterpret_cast<void*>(Hook_TargetAccessorTry),    reinterpret_cast<void**>(&origTargetAccTry));
    ok &= addHook("BulletShooter::slot3",         Rva::kBulletShooterPreview, reinterpret_cast<void*>(Hook_BulletShooterPreview), reinterpret_cast<void**>(&origShooterPreview));
    ok &= addHook("TargetAccessor::CollisionEval",Rva::kCollisionEval,        reinterpret_cast<void*>(Hook_CollisionEval),        reinterpret_cast<void**>(&origCollisionEval));

    ok &= goh::target_prio::install(g_Base, actualSha, MH_CreateHook, MH_RemoveHook, LogMsg);

    // Auto-Smoke (14 hooks: vehicles, pinned infantry squads, medics — strict WP exclusion)
    goh::auto_smoke::log_sink = &LogMsg;
    bool smokeOk = goh::auto_smoke::prepare_hooks(g_Base, actualSha, true, &MH_CreateHook, &MH_RemoveHook);
    if (!smokeOk) {
        LogMsg("ERROR: goh::auto_smoke::prepare_hooks failed");
    } else {
        LogMsg("Auto-Smoke 14 hooks staged cleanly");
    }
    ok &= smokeOk;

    goh::tank_crusher::log_sink = &LogMsg;
    if (!goh::tank_crusher::prepare_hooks(g_Base, actualSha, MH_CreateHook, MH_RemoveHook)) {
        LogMsg("ERROR: tank pathing hook staging failed");
        ok = false;
    }
    if (!ok) {
        if (MH_Uninitialize() != MH_OK) {
            LogMsg("Hook staging rollback failed; retaining trampolines for shutdown");
            return 1;
        }
        goh::tank_crusher::after_hooks_removed();
        if (smokeOk) goh::auto_smoke::after_hooks_removed();
        goh::target_prio::after_hooks_removed();
        return 1;
    }

    InterlockedExchange(&g_HooksActive, 1);
    MH_STATUS en = MH_EnableHook(MH_ALL_HOOKS);
    if (en != MH_OK) {
        InterlockedExchange(&g_HooksActive, 0);
        goh::tank_crusher::deactivate();
        LogMsg("ERROR: MH_EnableHook failed: %d", (int)en);
        MH_DisableHook(MH_ALL_HOOKS);
        for (int wait = 0; wait < 50 && goh::tank_crusher::inflight_count() > 0; ++wait) Sleep(10);
        Sleep(50);
        if (MH_Uninitialize() != MH_OK) {
            LogMsg("Retaining trampolines after partial activation until shutdown");
            return 1;
        }
        goh::tank_crusher::after_hooks_removed();
        goh::target_prio::after_hooks_removed();
        goh::auto_smoke::after_hooks_removed();
        return 1;
    }

    uintptr_t masterPtr = *reinterpret_cast<const uintptr_t*>(g_Base + 0x15D2BE0);
    uint8_t localPl = *reinterpret_cast<const uint8_t*>(g_Base + 0x14AEF38);
    auto vRoute = goh::auto_smoke::vehicle::vehicle_control_route(g_Base, masterPtr, localPl);
    LogMsg("Auto-Smoke vehicle route check: master=0x%llx localPl=%u routeCtx=0x%llx",
           (unsigned long long)masterPtr, (unsigned)localPl, (unsigned long long)vRoute.context);
    LogMsg("=== ALL 35 HOOKS ACTIVE (11 grenade/LOF/detour + 7 target prioritization + 14 Auto-Smoke + 3 tank pathing) ===");
    return 0;
}

} // namespace

// Clean hot-unload helper (unhooks outside loader lock before FreeLibraryAndExitThread)
extern "C" __declspec(dllexport) DWORD WINAPI ShutdownAndUnload(LPVOID) {
    if (g_InitThread && WaitForSingleObject(g_InitThread, INFINITE) != WAIT_OBJECT_0) return ERROR_BUSY;
    InterlockedExchange(&g_HooksActive, 0);
    goh::tank_crusher::deactivate();
    MH_STATUS disabled = MH_DisableHook(MH_ALL_HOOKS);
    if (disabled != MH_OK && disabled != MH_ERROR_NOT_INITIALIZED) return ERROR_BUSY;
    for (int wait = 0; wait < 50 && goh::tank_crusher::inflight_count() > 0; ++wait) Sleep(10);
    Sleep(50);
    if (goh::tank_crusher::inflight_count() > 0) {
        LogMsg("Unload deferred: tank pathing hook still inflight");
        return ERROR_BUSY;
    }
    MH_STATUS removed = MH_Uninitialize();
    if (removed != MH_OK && removed != MH_ERROR_NOT_INITIALIZED) return ERROR_BUSY;
    const unsigned tankCtx = goh::tank_crusher::admitted_contexts.load();
    const unsigned tankExp = goh::tank_crusher::expanded_contexts.load();
    const unsigned tankDisc = goh::tank_crusher::discounted_obstacles.load();
    const unsigned tankRev = goh::tank_crusher::biased_estimates.load();
    goh::tank_crusher::after_hooks_removed();
    goh::target_prio::after_hooks_removed();
    goh::auto_smoke::after_hooks_removed();
    LogMsg("=== goh_ai ShutdownAndUnload complete (dodges=%u softLOF=%u softRej=%u detInf=%u detVeh=%u smokeVeh=%u smokeInf=%u smokeMed=%u smokeRel=%u tankCtx=%u tankExp=%u tankDisc=%u tankRev=%u) ===",
           g_TotalDodges, g_TotalSoftOverrides, g_TotalSoftRejects, g_TotalDetoursInf, g_TotalDetoursVeh,
           goh::auto_smoke::stat_veh_smoke, goh::auto_smoke::stat_inf_smoke,
           goh::auto_smoke::stat_med_smoke, goh::auto_smoke::stat_inf_released,
           tankCtx, tankExp, tankDisc, tankRev);
    FreeLibraryAndExitThread(g_SelfModule, 0);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        g_SelfModule = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
        InitializeCriticalSection(&g_LogCs);
        InitializeCriticalSection(&g_GrenadeCs);
        g_InitThread = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        InterlockedExchange(&g_HooksActive, 0);
        goh::tank_crusher::deactivate();
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        goh::tank_crusher::after_hooks_removed();
        goh::target_prio::after_hooks_removed();
        goh::auto_smoke::after_hooks_removed();
        if (g_InitThread) CloseHandle(g_InitThread);
        LogMsg("=== goh_ai unloaded ===");
        EnterCriticalSection(&g_LogCs);
        if (g_LogFile) {
            std::fclose(g_LogFile);
            g_LogFile = nullptr;
        }
        LeaveCriticalSection(&g_LogCs);
        DeleteCriticalSection(&g_GrenadeCs);
        DeleteCriticalSection(&g_LogCs);
    }
    return TRUE;
}
