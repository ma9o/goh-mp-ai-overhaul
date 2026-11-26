// Build 26734796 Host-Only Early Flying-Grenade Dodge Prototype
// Static contracts: research/notes/grenade-escape-26734796.md
// Constraints (AGENTS.md):
//   - Zero resource/ modifications; PE SHA-256 gated.
//   - Host-only (!IsRemoteClient 0xC2B7A0), serial main thread (!IsQuantWorkerThread 0xA7A80).
//   - Concrete Actor::eHumanBrain vtable check (0x110FF88).
//   - Respects Direct Control / inhibition ((brain+0x39C & 0x3F) == 0, chassis+0x18 == 0, weaponry+0x70 bit 1 == 0).
//   - Outputs strictly through native replicating setters/orders (SetMovementType, SetMovementMode, Order::eMoveTask, eAvoidGrenade).
//   - Zero custom engine RNG draws.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iterator>

#include "MinHook.h"

namespace {

// Exact build 26734796 PE SHA-256
constexpr const char* kExpectedExeSha256 = "81a13de787c1869b766004cdcadb9ff99012f0b1b58b89db9a7741b10b9f059a";

// Verified build 26734796 RVAs (research/notes/grenade-escape-26734796.md)
namespace Rva {
constexpr uintptr_t kIsHostEmit = 0x00C2B740;
constexpr uintptr_t kIsRemoteClient = 0x00C2B7A0;
constexpr uintptr_t kIsQuantWorker = 0x000A7A80;
constexpr uintptr_t kClockRead = 0x0066E7B0;
constexpr uintptr_t kArmedGrenadeQuery = 0x009207E0;
constexpr uintptr_t kTerrainHeight = 0x00A5D440;
constexpr uintptr_t kTerrainEnv = 0x00A62620;
constexpr uintptr_t kMoveTaskPoint = 0x0090A8C0;
constexpr uintptr_t kOrderAttachChild = 0x0091A180;

// Hook targets
constexpr uintptr_t kGrenadeSimTick = 0x007442F0;
constexpr uintptr_t kGrenadeSimDtor = 0x007450E0;
constexpr uintptr_t kActorSlot23 = 0x00804AC0;
constexpr uintptr_t kGrenadeScannerUpd = 0x0091FFA0;
constexpr uintptr_t kAvoidInnerTick = 0x00916570;
constexpr uintptr_t kAvoidPlanEscape = 0x00917050;

// Globals & vtables
constexpr uintptr_t kSpatialScaleL = 0x0149E750; // float (default 20.0)
constexpr uintptr_t kGravity = 0x015754E0;       // float (~0.0001962)
constexpr uintptr_t kScenePtr = 0x015C9668;      // uintptr_t*
constexpr uintptr_t kHumanBrainVtable = 0x0110FF88;
constexpr uintptr_t kAvoidGrenadeVtable = 0x0110D278;
} // namespace Rva

// Verified offsets (build 26734796)
namespace Off {
// eEntity / eActor
constexpr uintptr_t kActorFlags38 = 0x0038;    // & 0x220 inactive/deleted
constexpr uintptr_t kEntityPos = 0x0074;       // float[3] XYZ
constexpr uintptr_t kEntityFlags1C0 = 0x01C0;  // bit 28 linked, bit 13 explosive
constexpr uintptr_t kActorOrderedSub = 0x0AF0; // order+8 points here
constexpr uintptr_t kActorCover = 0x0DC0;      // +0x24 cover group (0 = outdoors)
constexpr uintptr_t kActorBrain = 0x0DC8;
constexpr uintptr_t kActorSensor = 0x0DD0;   // +0x280/+0x288 cat1 enemy subject vector
constexpr uintptr_t kActorWeaponry = 0x0DD8; // +0x70 bit 1 manual weaponry
constexpr uintptr_t kActorChassis = 0x0DE0;  // +0x18 uint32 direct control

// Actor::eBrain
constexpr uintptr_t kBrainOwner = 0x0038;
constexpr uintptr_t kBrainCurrentOrder = 0x00F0;
constexpr uintptr_t kBrainFlags39C = 0x039C; // bits 0..5 inhibit count, bit 25 reconsider

// Actor::eSensor
constexpr uintptr_t kSensorCat1Begin = 0x0280;
constexpr uintptr_t kSensorCat1End = 0x0288;

// Actor::eSubject
constexpr uintptr_t kSubjectFlags10 = 0x0010;  // bit 28 known pos, bit 29 visible
constexpr uintptr_t kSubjectFlags14 = 0x0014;  // bit 1 enemy, bit 4 unusable
constexpr uintptr_t kSubjectPos = 0x0028;      // float[3]
constexpr uintptr_t kSubjectObsTime = 0x0064;  // uint32
constexpr uintptr_t kSubjectSuppress = 0x006C; // uint8

// Actor::eGrenadeScanner
constexpr uintptr_t kScannerBrain = 0x0008;
constexpr uintptr_t kScannerActive = 0x0010;     // uint8
constexpr uintptr_t kScannerDangerZone = 0x0018; // float (in L units, default 4.0)
constexpr uintptr_t kScannerEntity = 0x0028;     // uintptr_t

// Order::eAvoidGrenade
constexpr uintptr_t kOrderOwner = 0x0008; // actor + 0xAF0
constexpr uintptr_t kOrderChild = 0x0030;
constexpr uintptr_t kAvoidEntity = 0x0038;
constexpr uintptr_t kAvoidRadius = 0x0048; // float (in L units)
constexpr uintptr_t kAvoidState = 0x004C;  // int32 (0=run, 1=hide/wait, 2=expiry delay)

// eGrenadeSimulator
constexpr uintptr_t kSimBody = 0x0008; // +0x28 center xyz, +0x48 vel xyz
constexpr uintptr_t kSimEntity = 0x0308;

// eChassisFoot vtable slots (build 26734796: shifted +1 vs 25492057)
constexpr uintptr_t kFootSetModeVOff = 45 * 8; // 0x168 SetMovementMode(chassis, mode, force)
constexpr uintptr_t kFootSetTypeVOff = 46 * 8; // 0x170 SetMovementType(chassis, type, force)
} // namespace Off

// Function typedefs
using FnBool0 = uint8_t(__fastcall*)();
using FnClockRead = uint32_t(__fastcall*)();
using FnArmedGrenade = uint8_t(__fastcall*)(uintptr_t entity);
using FnTerrainHeight = float(__fastcall*)(uintptr_t terrain, const float* xy, int level);
using FnTerrainEnv = uint16_t*(__fastcall*)(uint16_t* out, const float* xy);
using FnMoveTaskPoint = uintptr_t(__fastcall*)(const float* xy, uint32_t flags);
using FnOrderAttachChild = void(__fastcall*)(uintptr_t parent, uintptr_t child);
using FnChassisSetInt = void(__fastcall*)(uintptr_t chassis, int32_t val, uint8_t force);

using FnSimTick = void(__fastcall*)(uintptr_t sim);
using FnSimDtor = void*(__fastcall*)(uintptr_t sim, uint32_t flags);
using FnActorSlot23 = void(__fastcall*)(uintptr_t actor);
using FnScannerUpd = void(__fastcall*)(uintptr_t scanner);
using FnAvoidInnerTick = uint64_t(__fastcall*)(uintptr_t order);
using FnAvoidPlanEscape = uint8_t(__fastcall*)(uintptr_t order);

// Globals
uintptr_t g_Base = 0;
HMODULE g_SelfModule = nullptr;
CRITICAL_SECTION g_LogCs;
CRITICAL_SECTION g_GrenadeCs;
FILE* g_LogFile = nullptr;
volatile LONG g_HooksActive = 0;

FnBool0 fnIsHostEmit = nullptr;
FnBool0 fnIsRemoteClient = nullptr;
FnBool0 fnIsQuantWorker = nullptr;
FnClockRead fnClockRead = nullptr;
FnArmedGrenade fnArmedGrenade = nullptr;
FnTerrainHeight fnTerrainHeight = nullptr;
FnTerrainEnv fnTerrainEnv = nullptr;
FnMoveTaskPoint fnMoveTaskPoint = nullptr;
FnOrderAttachChild fnOrderAttachChild = nullptr;

FnSimTick origSimTick = nullptr;
FnSimDtor origSimDtor = nullptr;
FnActorSlot23 origActorSlot23 = nullptr;
FnScannerUpd origScannerUpd = nullptr;
FnAvoidInnerTick origAvoidInnerTick = nullptr;
FnAvoidPlanEscape origAvoidPlanEscape = nullptr;

// Active live grenade tracking (main thread only, bounded fixed array)
struct TrackedGrenade {
    uintptr_t sim;
    uintptr_t entity;
    uint32_t lastTick;
    float x, y, z;
    float vx, vy, vz;
    float landX, landY;
    float flightTime;
    uint8_t logged;
};

constexpr int kMaxGrenades = 64;
TrackedGrenade g_Grenades[kMaxGrenades];
int g_GrenadeCount = 0;

// Small per-order retry counter so failed escape points rotate angles cleanly
struct OrderRetry {
    uintptr_t order;
    uint32_t lastTick;
    int attempt;
};
constexpr int kMaxRetries = 64;
OrderRetry g_Retries[kMaxRetries];

// Stats for readable logging
uint32_t g_TotalDodges = 0;
uint32_t g_TotalWakeups = 0;

inline bool IsPlausiblePtr(uintptr_t p) {
    return p >= 0x10000ULL && p < 0x00007FFFFFFFFFFFULL && ((p & 3ULL) == 0);
}

__attribute__((format(__MINGW_PRINTF_FORMAT, 1, 2))) void LogMsg(const char* fmt, ...) {
    EnterCriticalSection(&g_LogCs);
    if (!g_LogFile) {
        static wchar_t path[32768]; // Protected by g_LogCs.
        const DWORD n = GetModuleFileNameW(g_SelfModule, path, static_cast<DWORD>(std::size(path)));
        if (n && n < std::size(path)) {
            auto* slash = std::wcsrchr(path, L'\\');
            constexpr wchar_t name[] = L"grenade_dodge.log";
            if (slash && static_cast<std::size_t>(slash + 1 - path) + std::size(name) <= std::size(path)) {
                std::copy_n(name, std::size(name), slash + 1);
                g_LogFile = _wfopen(path, L"a");
            }
        }
    }
    if (g_LogFile) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        std::fprintf(g_LogFile, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        va_list ap;
        va_start(ap, fmt);
        std::vfprintf(g_LogFile, fmt, ap);
        va_end(ap);
        std::fprintf(g_LogFile, "\n");
        std::fflush(g_LogFile);
    }
    LeaveCriticalSection(&g_LogCs);
}

// Minimal standalone SHA-256 (zero external DLL dependencies)
struct Sha256Ctx {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t data[64];
    size_t datalen;
};

inline uint32_t RotR(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

void Sha256Transform(Sha256Ctx* ctx, const uint8_t d[64]) {
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t m[64];
    for (int i = 0; i < 16; ++i) {
        m[i] = (uint32_t(d[i * 4]) << 24) | (uint32_t(d[i * 4 + 1]) << 16) | (uint32_t(d[i * 4 + 2]) << 8) |
               uint32_t(d[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = RotR(m[i - 15], 7) ^ RotR(m[i - 15], 18) ^ (m[i - 15] >> 3);
        uint32_t s1 = RotR(m[i - 2], 17) ^ RotR(m[i - 2], 19) ^ (m[i - 2] >> 10);
        m[i] = m[i - 16] + s0 + m[i - 7] + s1;
    }
    uint32_t a = ctx->state[0], b = ctx->state[1], c = ctx->state[2], d0 = ctx->state[3];
    uint32_t e = ctx->state[4], f = ctx->state[5], g = ctx->state[6], h = ctx->state[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = RotR(e, 6) ^ RotR(e, 11) ^ RotR(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + S1 + ch + k[i] + m[i];
        uint32_t S0 = RotR(a, 2) ^ RotR(a, 13) ^ RotR(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;
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

void Sha256Init(Sha256Ctx* ctx) {
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
        while (i < 64)
            ctx->data[i++] = 0;
        Sha256Transform(ctx, ctx->data);
        i = 0;
    }
    while (i < 56)
        ctx->data[i++] = 0;
    ctx->bitlen += ctx->datalen * 8ULL;
    for (int j = 7; j >= 0; --j) {
        ctx->data[56 + (7 - j)] = uint8_t((ctx->bitlen >> (j * 8)) & 0xFF);
    }
    Sha256Transform(ctx, ctx->data);
    static const char* hex = "0123456789abcdef";
    for (int w = 0; w < 8; ++w) {
        for (int b = 3; b >= 0; --b) {
            uint8_t byte = uint8_t((ctx->state[w] >> (b * 8)) & 0xFF);
            outHex[(w * 4 + (3 - b)) * 2] = hex[byte >> 4];
            outHex[(w * 4 + (3 - b)) * 2 + 1] = hex[byte & 0xF];
        }
    }
    outHex[64] = '\0';
}

bool VerifyGameExeSha256(char outHex[65]) {
    wchar_t exePath[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, exePath, MAX_PATH * 2);
    if (n == 0 || n >= MAX_PATH * 2) return false;
    HANDLE h = CreateFileW(exePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
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
    float xy[2] = {x, y};
    float h = fnTerrainHeight(terrain, xy, 0);
    return std::isfinite(h) ? h : fallbackZ;
}

inline bool IsPassableDryTerrain(float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y)) return false;
    if (std::fabs(x) < 1.0f && std::fabs(y) < 1.0f) return false;
    float xy[2] = {x, y};
    uint16_t env = 0;
    fnTerrainEnv(&env, xy);
    uint8_t cls = uint8_t(env & 0xFF);
    // J1: 2=no_pass, 3=shallow_water, 4=water, 5=deep_water, 8=no_pass_human
    if (cls == 2 || cls == 3 || cls == 4 || cls == 5 || cls == 8) return false;
    return true;
}

void PredictLanding(float x0, float y0, float z0, float vx, float vy, float vz, float& outX, float& outY, float& outT) {
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
    if (t > 2500.0f) t = 2500.0f; // cap horizon at ~2.5s

    // Refine once against terrain height at the estimated landing point
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

    // Also include a tiny short roll projection when already moving fast horizontally on/near ground
    float speedXY = std::sqrt(vx * vx + vy * vy);
    if (t < 150.0f && speedXY > 0.01f) {
        float extraT = 250.0f; // ~0.25s short roll lead
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
        // 300 ticks (~0.3s) without simulator tick means expired/asleep/stale
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

bool IsEligibleHumanForDodge(uintptr_t actor, uintptr_t& outBrain, uintptr_t& outChassis) {
    outBrain = 0;
    outChassis = 0;
    if (!IsPlausiblePtr(actor)) return false;

    uint32_t flags38 = *reinterpret_cast<const uint32_t*>(actor + Off::kActorFlags38);
    if ((flags38 & 0x220u) != 0) return false;

    // Linked to parent (mounted in vehicle/gun) -> cannot run on foot
    uint32_t flags1c0 = *reinterpret_cast<const uint32_t*>(actor + Off::kEntityFlags1C0);
    if ((flags1c0 & (1u << 28)) != 0) return false;

    uintptr_t brain = *reinterpret_cast<const uintptr_t*>(actor + Off::kActorBrain);
    if (!IsPlausiblePtr(brain)) return false;

    // Concrete Actor::eHumanBrain check (never *Client)
    uintptr_t brainVt = *reinterpret_cast<const uintptr_t*>(brain);
    if (brainVt != g_Base + Rva::kHumanBrainVtable) return false;

    // Respect Direct Control / incapacitation / boarding / inventory exam (6-bit counter)
    uint32_t brainFlags = *reinterpret_cast<const uint32_t*>(brain + Off::kBrainFlags39C);
    if ((brainFlags & 0x3Fu) != 0) return false;

    uintptr_t chassis = *reinterpret_cast<const uintptr_t*>(actor + Off::kActorChassis);
    if (!IsPlausiblePtr(chassis)) return false;
    if (*reinterpret_cast<const uint32_t*>(chassis + 0x18) != 0) return false;

    uintptr_t weaponry = *reinterpret_cast<const uintptr_t*>(actor + Off::kActorWeaponry);
    if (IsPlausiblePtr(weaponry)) {
        uint32_t wFlags = *reinterpret_cast<const uint32_t*>(weaponry + 0x70);
        if ((wFlags & 2u) != 0) return false; // manual weaponry mode
    }

    outBrain = brain;
    outChassis = chassis;
    return true;
}

// Check if soldier at (sx, sy) is threatened by grenade g within dangerRadius.
// Ignores outbound/passing airborne grenades that are flying away from the soldier
// and predicted to land outside dangerRadius (so throwers & nearby allies don't dodge their own throw).
bool IsThreatenedBy(const TrackedGrenade& g, float sx, float sy, float dangerRadius, float maxDetectDist,
                    float& outUrgencyDistSq) {
    // Soldier must be within plausible awareness distance of the grenade itself
    float dcx = sx - g.x;
    float dcy = sy - g.y;
    float curDistSq = dcx * dcx + dcy * dcy;
    if (curDistSq > maxDetectDist * maxDetectDist) return false;

    float dlx = sx - g.landX;
    float dly = sy - g.landY;
    float landDistSq = dlx * dlx + dly * dly;
    float radSq = dangerRadius * dangerRadius;

    // 1. Always threatened if predicted landing is inside dangerRadius
    if (landDistSq < radSq) {
        outUrgencyDistSq = landDistSq;
        return true;
    }

    // 2. If predicted landing is outside dangerRadius, only treat current position as a threat
    //    when the grenade is nearly stationary/settled OR actively moving toward the soldier.
    float speedXY = std::sqrt(g.vx * g.vx + g.vy * g.vy);
    bool airborne = (g.flightTime > 120.0f);
    // dot(vel_xy, soldier - cur_pos): positive means grenade is moving toward soldier
    float dotToward = g.vx * dcx + g.vy * dcy;

    if (airborne) {
        // Airborne grenade landing elsewhere (e.g. just thrown by this soldier/ally or arcing overhead) -> ignore!
        return false;
    }
    if (speedXY > 0.035f && dotToward <= 0.0f) {
        // Rolling fast away from soldier and landing outside dangerRadius -> ignore!
        return false;
    }

    if (curDistSq < radSq) {
        outUrgencyDistSq = curDistSq;
        return true;
    }
    return false;
}

// Compute unit vector pointing AWAY from visible/recent enemy threats in sensor category 1
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
        if ((f14 & (1u << 1)) == 0) continue;  // not enemy
        if ((f14 & (1u << 4)) != 0) continue;  // unusable / dead
        if ((f10 & (1u << 28)) == 0) continue; // no known position

        uint32_t obsTime = *reinterpret_cast<const uint32_t*>(subj + Off::kSubjectObsTime);
        uint32_t age = now - obsTime;
        if (age > 20000) continue; // stale (>20s)

        float ex = *reinterpret_cast<const float*>(subj + Off::kSubjectPos);
        float ey = *reinterpret_cast<const float*>(subj + Off::kSubjectPos + 4);
        if (!std::isfinite(ex) || !std::isfinite(ey)) continue;

        float dx = sx - ex;
        float dy = sy - ey;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < 1.0f) continue;

        float w = 1.0f / (dist + 40.0f);
        if ((f10 & (1u << 29)) != 0) w *= 2.0f; // currently visible enemy weighs double
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

// ============================================================================
// HOOK 1: eGrenadeSimulator::Tick (0x7442F0)
// ============================================================================
void __fastcall Hook_GrenadeSimTick(uintptr_t sim) {
    origSimTick(sim);

    if (!g_HooksActive || !IsHostMainThread() || !IsPlausiblePtr(sim)) return;

    uintptr_t entity = *reinterpret_cast<const uintptr_t*>(sim + Off::kSimEntity);
    if (!IsPlausiblePtr(entity)) return;

    // Only track unlinked, armed, grenade-tagged entities
    uint32_t eFlags = *reinterpret_cast<const uint32_t*>(entity + Off::kEntityFlags1C0);
    if ((eFlags & (1u << 28)) != 0) return;
    if (fnArmedGrenade(entity) == 0) return;

    uintptr_t body = sim + Off::kSimBody;
    float x = *reinterpret_cast<const float*>(body + 0x28);
    float y = *reinterpret_cast<const float*>(body + 0x2C);
    float z = *reinterpret_cast<const float*>(body + 0x30);
    float vx = *reinterpret_cast<const float*>(body + 0x48);
    float vy = *reinterpret_cast<const float*>(body + 0x4C);
    float vz = *reinterpret_cast<const float*>(body + 0x50);
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(vx) || !std::isfinite(vy) ||
        !std::isfinite(vz)) {
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
            std::memset(&g_Grenades[idx], 0, sizeof(TrackedGrenade));
        } else {
            idx = 0;
            std::memset(&g_Grenades[idx], 0, sizeof(TrackedGrenade));
        }
    }

    TrackedGrenade& g = g_Grenades[idx];
    g.sim = sim;
    g.entity = entity;
    g.lastTick = now;
    g.x = x;
    g.y = y;
    g.z = z;
    g.vx = vx;
    g.vy = vy;
    g.vz = vz;
    g.landX = landX;
    g.landY = landY;
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

// ============================================================================
// HOOK 2: eGrenadeSimulator::~dtor (0x7450E0)
// ============================================================================
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

// ============================================================================
// HOOK 3: eActor::slot23 (0x804AC0) - Wake up threatened soldiers immediately!
// ============================================================================
void __fastcall Hook_ActorSlot23(uintptr_t actor) {
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

    origActorSlot23(actor);
}

// ============================================================================
// HOOK 4: Actor::eGrenadeScanner::Update (0x91FFA0)
// ============================================================================
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

// Forward declaration for Hook_AvoidInnerTick
uint8_t __fastcall Hook_AvoidPlanEscape(uintptr_t order);

// ============================================================================
// HOOK 5: Order::eAvoidGrenade::InnerTick (0x916570)
// Fix cover-hide trap for flying grenades & wake up state==1 if grenade rolls closer!
// ============================================================================
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
                if (tg) {
                    tgCopy = *tg;
                    hasTg = true;
                }
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

// ============================================================================
// HOOK 6: Order::eAvoidGrenade::PlanEscape (0x917050)
// Sprint away opposite BOTH predicted grenade landing AND enemy threat direction!
// ============================================================================
uint8_t __fastcall Hook_AvoidPlanEscape(uintptr_t order) {
    if (!g_HooksActive || !IsHostMainThread() || !IsPlausiblePtr(order)) {
        return origAvoidPlanEscape(order);
    }

    uintptr_t ownerSub = *reinterpret_cast<const uintptr_t*>(order + Off::kOrderOwner);
    uintptr_t gEnt = *reinterpret_cast<const uintptr_t*>(order + Off::kAvoidEntity);
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
        // After 5 explicit angle attempts fail, cleanly fall back to vanilla eAreaOutside
        return origAvoidPlanEscape(order);
    }

    float sx = *reinterpret_cast<const float*>(actor + Off::kEntityPos);
    float sy = *reinterpret_cast<const float*>(actor + Off::kEntityPos + 4);
    float L = GetSpatialScaleL();

    float ordRad = *reinterpret_cast<const float*>(order + Off::kAvoidRadius);
    if (!std::isfinite(ordRad) || ordRad < 1.0f || ordRad > 20.0f) ordRad = 4.0f;

    // Hazard center: predicted landing XY if tracked in flight, else current grenade XY
    float hx = *reinterpret_cast<const float*>(gEnt + Off::kEntityPos);
    float hy = *reinterpret_cast<const float*>(gEnt + Off::kEntityPos + 4);
    float gCurX = hx, gCurY = hy;
    float gvx = 0.0f, gvy = 0.0f;

    TrackedGrenade tgCopy{};
    bool hasTg = false;
    EnterCriticalSection(&g_GrenadeCs);
    const TrackedGrenade* tg = FindTrackedByEntity(gEnt, now);
    if (tg) {
        tgCopy = *tg;
        hasTg = true;
    }
    LeaveCriticalSection(&g_GrenadeCs);
    if (hasTg) {
        hx = tgCopy.landX;
        hy = tgCopy.landY;
        gCurX = tgCopy.x;
        gCurY = tgCopy.y;
        gvx = tgCopy.vx;
        gvy = tgCopy.vy;
    }

    float safeRad = (ordRad + 2.5f) * L; // strictly outside danger_zone + 2.5L buffer
    float qx = sx - hx, qy = sy - hy;
    float distLand = std::sqrt(qx * qx + qy * qy);
    float dcx = sx - gCurX, dcy = sy - gCurY;
    float distCur = std::sqrt(dcx * dcx + dcy * dcy);

    // Already safely outside blast radius of predicted landing (and current pos if not flying away) -> wait safely!
    bool flyingAway = hasTg && (tgCopy.flightTime > 120.0f ||
                                ((std::sqrt(gvx * gvx + gvy * gvy) > 0.035f) && (gvx * dcx + gvy * dcy <= 0.0f)));
    if (distLand >= safeRad && (flyingAway || distCur >= (ordRad + 1.5f) * L)) {
        *reinterpret_cast<int32_t*>(order + Off::kAvoidState) = 1;
        return 1;
    }

    // 1. Unit vector away from predicted landing
    float gx = 0.0f, gy = 0.0f;
    if (distLand > 1.0f) {
        gx = qx / distLand;
        gy = qy / distLand;
    }

    // 2. Unit vector away from enemy danger (fallback: grenade travel direction away from thrower)
    float ex = 0.0f, ey = 0.0f;
    bool hasEnemyDir = ComputeAwayFromEnemies(actor, sx, sy, now, ex, ey);
    if (!hasEnemyDir) {
        float vxy = std::sqrt(gvx * gvx + gvy * gvy);
        if (vxy > 0.005f) {
            // Away from thrower = along grenade travel vector (or perpendicular if standing directly downrange)
            ex = gvx / vxy;
            ey = gvy / vxy;
            hasEnemyDir = true;
        }
    }

    // 3. Blend away-from-grenade (priority) + away-from-enemy
    float ux = 0.0f, uy = 0.0f;
    if (distLand > 1.0f && hasEnemyDir) {
        // Weight grenade stronger so soldier never runs into the blast
        float bx = 1.35f * gx + 0.85f * ex;
        float by = 1.35f * gy + 0.85f * ey;
        float blen = std::sqrt(bx * bx + by * by);
        if (blen > 0.15f && (bx * gx + by * gy) > 0.0f) {
            ux = bx / blen;
            uy = by / blen;
        } else {
            // Opposed vectors: choose perpendicular side away from blast
            ux = -gy;
            uy = gx;
            if (ux * ex + uy * ey < 0.0f) {
                ux = -ux;
                uy = -uy;
            }
        }
    } else if (distLand > 1.0f) {
        ux = gx;
        uy = gy;
    } else if (hasEnemyDir) {
        ux = ex;
        uy = ey;
    } else {
        ux = 1.0f;
        uy = 0.0f;
    }

    // Rotate base direction by attempt angle fan so blocked routes cleanly try flanks
    static const float kAnglesRad[7] = {0.0f, 0.55f, -0.55f, 1.15f, -1.15f, 1.75f, -1.75f};

    float targetXY[2] = {0.0f, 0.0f};
    bool foundPoint = false;

    for (int k = 0; k < 7; ++k) {
        int aIdx = (attempt + k) % 7;
        float ca = std::cos(kAnglesRad[aIdx]);
        float sa = std::sin(kAnglesRad[aIdx]);
        float dx = ux * ca - uy * sa;
        float dy = ux * sa + uy * ca;

        // Ray-circle intersection distance out of safeRad around landing point (hx, hy)
        float dotQU = qx * dx + qy * dy;
        float disc = dotQU * dotQU + safeRad * safeRad - (qx * qx + qy * qy);
        float s = (disc > 0.0f) ? (-dotQU + std::sqrt(disc)) : safeRad;
        if (s < 2.5f * L) s = 2.5f * L;
        s += 1.5f * L; // extra sprint buffer past safeRad

        float candX = sx + dx * s;
        float candY = sy + dy * s;

        if (IsPassableDryTerrain(candX, candY)) {
            targetXY[0] = candX;
            targetXY[1] = candY;
            foundPoint = true;
            break;
        }
    }

    if (!foundPoint) {
        return origAvoidPlanEscape(order);
    }

    // Request STANDING (type=0) + FAST/SPRINT (mode=2) through native replicating foot chassis slots!
    uintptr_t chassisVt = *reinterpret_cast<const uintptr_t*>(chassis);
    if (!IsPlausiblePtr(chassisVt)) return origAvoidPlanEscape(order);

    auto fnSetType = *reinterpret_cast<FnChassisSetInt*>(chassisVt + Off::kFootSetTypeVOff);
    auto fnSetMode = *reinterpret_cast<FnChassisSetInt*>(chassisVt + Off::kFootSetModeVOff);
    fnSetType(chassis, 0, 0); // normal/stand (emits eSetMovementTypeCommand on host)
    fnSetMode(chassis, 2, 0); // fast/sprint  (emits eSetMovementModeCommand + eChassisFootSetModeCommand)

    // Create native MoveTask(point) and attach as child of Order::eAvoidGrenade
    // MoveTask spawns native Order::eMove whose phase 0 sets +0x3C=IsHost() -> emits eOrderMoveCommand!
    uintptr_t moveChild = fnMoveTaskPoint(targetXY, 0);
    if (!IsPlausiblePtr(moveChild)) {
        return origAvoidPlanEscape(order);
    }
    fnOrderAttachChild(order, moveChild);

    ++g_TotalDodges;
    LogMsg("SPRINT ESCAPE #%u: actor=0x%llx from=(%.1f,%.1f) -> to=(%.1f,%.1f) haz=(%.1f,%.1f) enemyDir=%d att=%d",
           g_TotalDodges, (unsigned long long)actor, sx, sy, targetXY[0], targetXY[1], hx, hy, hasEnemyDir ? 1 : 0,
           attempt);
    return 1;
}

DWORD WINAPI InitThread(LPVOID) {
    LogMsg("=== grenade_dodge.dll InitThread starting ===");

    g_Base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    LogMsg("Game module base: 0x%llx", (unsigned long long)g_Base);

    char actualSha[65] = {};
    if (!VerifyGameExeSha256(actualSha)) {
        LogMsg("ABORT: PE SHA-256 mismatch! actual=%s expected=%s", actualSha, kExpectedExeSha256);
        return 1;
    }
    LogMsg("PE SHA-256 verified: %s (build 26734796)", actualSha);

    fnIsHostEmit = reinterpret_cast<FnBool0>(g_Base + Rva::kIsHostEmit);
    fnIsRemoteClient = reinterpret_cast<FnBool0>(g_Base + Rva::kIsRemoteClient);
    fnIsQuantWorker = reinterpret_cast<FnBool0>(g_Base + Rva::kIsQuantWorker);
    fnClockRead = reinterpret_cast<FnClockRead>(g_Base + Rva::kClockRead);
    fnArmedGrenade = reinterpret_cast<FnArmedGrenade>(g_Base + Rva::kArmedGrenadeQuery);
    fnTerrainHeight = reinterpret_cast<FnTerrainHeight>(g_Base + Rva::kTerrainHeight);
    fnTerrainEnv = reinterpret_cast<FnTerrainEnv>(g_Base + Rva::kTerrainEnv);
    fnMoveTaskPoint = reinterpret_cast<FnMoveTaskPoint>(g_Base + Rva::kMoveTaskPoint);
    fnOrderAttachChild = reinterpret_cast<FnOrderAttachChild>(g_Base + Rva::kOrderAttachChild);

    LogMsg("Initial state: remote=%u hostEmit=%u clock=%u L=%.1f G=%.7f", (unsigned)fnIsRemoteClient(),
           (unsigned)fnIsHostEmit(), fnClockRead(), GetSpatialScaleL(), GetGravity());

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
    ok &= addHook("eGrenadeSimulator::Tick", Rva::kGrenadeSimTick, reinterpret_cast<void*>(Hook_GrenadeSimTick),
                  reinterpret_cast<void**>(&origSimTick));
    ok &= addHook("eGrenadeSimulator::~dtor", Rva::kGrenadeSimDtor, reinterpret_cast<void*>(Hook_GrenadeSimDtor),
                  reinterpret_cast<void**>(&origSimDtor));
    ok &= addHook("eActor::slot23", Rva::kActorSlot23, reinterpret_cast<void*>(Hook_ActorSlot23),
                  reinterpret_cast<void**>(&origActorSlot23));
    ok &= addHook("eGrenadeScanner::Update", Rva::kGrenadeScannerUpd, reinterpret_cast<void*>(Hook_GrenadeScannerUpd),
                  reinterpret_cast<void**>(&origScannerUpd));
    ok &= addHook("eAvoidGrenade::InnerTick", Rva::kAvoidInnerTick, reinterpret_cast<void*>(Hook_AvoidInnerTick),
                  reinterpret_cast<void**>(&origAvoidInnerTick));
    ok &= addHook("eAvoidGrenade::PlanEscape", Rva::kAvoidPlanEscape, reinterpret_cast<void*>(Hook_AvoidPlanEscape),
                  reinterpret_cast<void**>(&origAvoidPlanEscape));

    if (!ok) {
        MH_Uninitialize();
        return 1;
    }

    InterlockedExchange(&g_HooksActive, 1);
    MH_STATUS en = MH_EnableHook(MH_ALL_HOOKS);
    if (en != MH_OK) {
        InterlockedExchange(&g_HooksActive, 0);
        LogMsg("ERROR: MH_EnableHook failed: %d", (int)en);
        MH_Uninitialize();
        return 1;
    }

    LogMsg("=== ALL 6 HOOKS ACTIVE! Throw a grenade to test! ===");
    return 0;
}

} // namespace

// Clean hot-unload helper (unhooks outside loader lock before FreeLibraryAndExitThread)
extern "C" __declspec(dllexport) DWORD WINAPI ShutdownAndUnload(LPVOID) {
    InterlockedExchange(&g_HooksActive, 0);
    MH_DisableHook(MH_ALL_HOOKS);
    Sleep(100);
    MH_Uninitialize();
    LogMsg("=== grenade_dodge.dll ShutdownAndUnload complete ===");
    FreeLibraryAndExitThread(g_SelfModule, 0);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID reserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        g_SelfModule = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
        InitializeCriticalSection(&g_LogCs);
        InitializeCriticalSection(&g_GrenadeCs);
        HANDLE h = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
        if (h) CloseHandle(h);
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        if (reserved) return TRUE;
        InterlockedExchange(&g_HooksActive, 0);
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        LogMsg("=== grenade_dodge.dll unloaded ===");
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
