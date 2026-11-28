// Call to Arms - Gates of Hell (Steam build 26734796)
// Unified AI Enhancement DLL coordinator (79 host-only replicating hooks across 4 TUs)
//
// Translation unit layout:
//   - mods/goh_ai_mod.cpp  : PE SHA-256 + live RTTI verification, goh_ai.ini config,
//                            leveled logging, eActor::slot23 dispatch, 79/79 inflight drain.
//   - mods/tu_combat.cpp   : Grenade Dodge, Soft-Cover LOF, Attack Detour,
//                            Target Prioritization, Close-Range Grenades, Penetration AT.
//   - mods/tu_infantry.cpp : AI Support, Auto-Smoke, Auto-Medic, Auto-Resupply,
//                            Tactical Infantry, Tactical Objectives, Engine Fixes, Weapon Swap, Squad Logistics.
//   - mods/tu_tanks.cpp    : Tank Crusher, Tank Auto-Repair, Tank Crew Fill, Tank Facing, Safe Disembark.
//
// All modules obey the build-26734796 non-desync invariants:
//   - Zero resource/ edits; gated on exact call_to_arms.exe PE SHA-256 + live RTTI.
//   - Host-only (!IsRemoteClient()), serial main thread (!IsQuantWorkerThread()).
//   - Replicating order/setter boundaries only; zero engine RNG draws.
//   - Respects Direct Control, brain inhibition, Hold Fire, and Hold Position.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "MinHook.h"
#include "actor_coordinator.hpp"
#include "ai_modules.hpp"
#include "config_and_log.hpp"
#include "game_runtime.hpp"
#include "live_observer_26734796.hpp"

#include <atomic>
#include <cmath>
#include <cstdint>

namespace {

using FnActorSlot23 = void(__fastcall*)(std::uintptr_t actor);
using FnOpeningMainPage = void(__fastcall*)(std::uintptr_t self);
using FnPageLoadingEvent = void(__fastcall*)(std::uintptr_t page, std::uintptr_t event);
using FnSetCursorPos = BOOL(WINAPI*)(int x, int y);
using FnClipCursor = BOOL(WINAPI*)(const RECT* rect);
using FnMouseEvent = void(WINAPI*)(DWORD flags, DWORD dx, DWORD dy, DWORD data, ULONG_PTR extra);
using FnSetForegroundWindow = BOOL(WINAPI*)(HWND hwnd);
using FnShowWindow = BOOL(WINAPI*)(HWND hwnd, int cmd);
using FnSetWindowPos = BOOL(WINAPI*)(HWND hwnd, HWND after, int x, int y, int cx, int cy, UINT flags);

HMODULE g_SelfModule = nullptr;
HANDLE g_InitThread = nullptr;
goh::runtime::Address g_Base = 0;
FnActorSlot23 g_OrigActorSlot23 = nullptr;
FnOpeningMainPage g_OrigOpeningMainPage = nullptr;
FnPageLoadingEvent g_OrigPageLoadingEvent = nullptr;
FnSetCursorPos g_OrigSetCursorPos = nullptr;
FnClipCursor g_OrigClipCursor = nullptr;
FnMouseEvent g_OrigMouseEvent = nullptr;
FnSetForegroundWindow g_OrigSetForegroundWindow = nullptr;
FnShowWindow g_OrigShowWindow = nullptr;
FnSetWindowPos g_OrigSetWindowPos = nullptr;
goh::runtime::HookGate g_ActorSlotGate{};
goh::runtime::HookGate g_OpeningMainPageGate{};
goh::runtime::HookGate g_PageLoadingGate{};
goh::runtime::HookGate g_CursorGrabGate{};
std::atomic<bool> g_AutoMissionLaunched{false};
std::atomic<bool> g_SuppressCursorGrab{false};
std::atomic<bool> g_ShutdownDone{false};
goh::live::Observer g_Observer;

int CreateHookAdapter(void* target, void* detour, void** trampoline) {
    return static_cast<int>(MH_CreateHook(target, detour, trampoline));
}

int RemoveHookAdapter(void* target) {
    return static_cast<int>(MH_RemoveHook(target));
}

float ReadSpatialScaleL() noexcept {
    if (!g_Base) return 20.0f;
    const float val = goh::runtime::read<float>(g_Base, goh::runtime::table().glob.spatial_scale_l);
    return (std::isfinite(val) && val > 1.0f && val < 200.0f) ? val : 20.0f;
}

float ReadGravity() noexcept {
    if (!g_Base) return 0.000196f;
    const float val = goh::runtime::read<float>(g_Base, goh::runtime::table().glob.gravity);
    return (std::isfinite(val) && val > 1e-6f && val < 0.1f) ? val : 0.000196f;
}

const char* LogLevelName(goh::log::Level lvl) noexcept {
    switch (lvl) {
        case goh::log::Level::off: return "off";
        case goh::log::Level::error: return "error";
        case goh::log::Level::summary: return "summary";
        case goh::log::Level::verbose: return "verbose";
    }
    return "summary";
}

void RunOrigSlot23(goh::runtime::Address actor, void*) {
    g_Observer.observe(g_Base, actor, [](const goh::config::Settings& settings) {
        goh::config::published().publish(settings);
        goh::log::configure(settings);
        g_SuppressCursorGrab.store(settings.suppress_cursor_grab, std::memory_order_release);
    });
    g_OrigActorSlot23(actor);
}

void RunCombatLayer(goh::runtime::Address actor, void*) {
    goh::tu_combat::with_actor_quant(actor, &RunOrigSlot23, nullptr);
}

void RunTanksLayer(goh::runtime::Address actor, void*) {
    goh::tu_tanks::with_actor_quant(actor, &RunCombatLayer, nullptr);
}

void __fastcall Hook_ActorSlot23(std::uintptr_t actor) {
    goh::runtime::HookGuard guard(g_ActorSlotGate);
    goh::tu_infantry::with_actor_quant(actor, &RunTanksLayer, nullptr);
}

void __fastcall Hook_OpeningMainPage(std::uintptr_t self) {
    goh::runtime::HookGuard guard(g_OpeningMainPageGate);
    if (g_OrigOpeningMainPage) g_OrigOpeningMainPage(self);
    if (!g_OpeningMainPageGate.active()) return;
    const auto& mission_path = goh::config::startup_settings().auto_mission;
    if (!mission_path.empty() && !g_AutoMissionLaunched.exchange(true, std::memory_order_acq_rel)) {
        goh::live::launch_singleplayer_mission(g_Base, mission_path);
    }
}

void __fastcall Hook_PageLoadingEvent(std::uintptr_t page, std::uintptr_t event) {
    goh::runtime::HookGuard guard(g_PageLoadingGate);
    const bool is_io_end =
        g_PageLoadingGate.active() && page && event && g_Base &&
        goh::runtime::read<std::uint32_t>(event, 8) == 0x102Au &&
        goh::runtime::read<goh::runtime::Address>(page) == g_Base + goh::runtime::table().vt.page_loading &&
        (goh::config::active().auto_dismiss_loading || !goh::config::startup_settings().auto_mission.empty());
    if (g_OrigPageLoadingEvent) g_OrigPageLoadingEvent(page, event);
    if (is_io_end && goh::runtime::read<std::uint8_t>(page, 0x1B9) != 0) {
        struct ContinueEvent {
            std::uint64_t pad0{};
            std::uint32_t id{7u};
            std::uint32_t pad1{};
        };
        ContinueEvent ev{};
        using FnOnContinue = void(__fastcall*)(std::uintptr_t subobj, void* ev);
        goh::runtime::native<FnOnContinue>(g_Base, goh::runtime::table().fn.page_loading_continue)(page + 0x28u, &ev);
    }
}

BOOL WINAPI Hook_SetCursorPos(int x, int y) {
    goh::runtime::HookGuard guard(g_CursorGrabGate);
    if (g_CursorGrabGate.active() && g_SuppressCursorGrab.load(std::memory_order_acquire)) return TRUE;
    return g_OrigSetCursorPos ? g_OrigSetCursorPos(x, y) : FALSE;
}

BOOL WINAPI Hook_ClipCursor(const RECT* rect) {
    goh::runtime::HookGuard guard(g_CursorGrabGate);
    if (rect != nullptr && g_CursorGrabGate.active() && g_SuppressCursorGrab.load(std::memory_order_acquire))
        return TRUE;
    return g_OrigClipCursor ? g_OrigClipCursor(rect) : FALSE;
}

void WINAPI Hook_MouseEvent(DWORD flags, DWORD dx, DWORD dy, DWORD data, ULONG_PTR extra) {
    goh::runtime::HookGuard guard(g_CursorGrabGate);
    if (g_CursorGrabGate.active() && g_SuppressCursorGrab.load(std::memory_order_acquire)) return;
    if (g_OrigMouseEvent) g_OrigMouseEvent(flags, dx, dy, data, extra);
}

BOOL WINAPI Hook_SetForegroundWindow(HWND hwnd) {
    goh::runtime::HookGuard guard(g_CursorGrabGate);
    if (g_CursorGrabGate.active() && g_SuppressCursorGrab.load(std::memory_order_acquire)) return TRUE;
    return g_OrigSetForegroundWindow ? g_OrigSetForegroundWindow(hwnd) : FALSE;
}

BOOL WINAPI Hook_ShowWindow(HWND hwnd, int cmd) {
    goh::runtime::HookGuard guard(g_CursorGrabGate);
    if (g_CursorGrabGate.active() && g_SuppressCursorGrab.load(std::memory_order_acquire)) {
        if (cmd == SW_SHOWNORMAL || cmd == SW_SHOW || cmd == SW_RESTORE) cmd = SW_SHOWNOACTIVATE;
    }
    return g_OrigShowWindow ? g_OrigShowWindow(hwnd, cmd) : FALSE;
}

BOOL WINAPI Hook_SetWindowPos(HWND hwnd, HWND after, int x, int y, int cx, int cy, UINT flags) {
    goh::runtime::HookGuard guard(g_CursorGrabGate);
    if (g_CursorGrabGate.active() && g_SuppressCursorGrab.load(std::memory_order_acquire)) {
        flags |= SWP_NOACTIVATE;
    }
    return g_OrigSetWindowPos ? g_OrigSetWindowPos(hwnd, after, x, y, cx, cy, flags) : FALSE;
}

template <class Fn> void StageUser32Hook(HMODULE user32, const char* name, void* detour, Fn& orig) {
    if (!user32) return;
    auto* target = reinterpret_cast<void*>(GetProcAddress(user32, name));
    if (!target) {
        goh::log::write(goh::log::Level::summary, "Testing hook skipped: user32!%s not exported", name);
        return;
    }
    void* tramp = nullptr;
    if (MH_CreateHook(target, detour, &tramp) == MH_OK) {
        orig = reinterpret_cast<Fn>(tramp);
        goh::log::write(goh::log::Level::summary, "Testing hook staged: user32!%s", name);
    } else {
        goh::log::write(goh::log::Level::summary, "Testing hook skipped: MH_CreateHook(user32!%s) failed", name);
    }
}

void DeactivateAllHooks() noexcept {
    goh::runtime::global_accepting.store(false, std::memory_order_release);
    g_ActorSlotGate.deactivate();
    g_OpeningMainPageGate.deactivate();
    g_PageLoadingGate.deactivate();
    g_CursorGrabGate.deactivate();
    goh::tu_combat::deactivate();
    goh::tu_infantry::deactivate();
    goh::tu_tanks::deactivate();
}

void CleanupAfterHooksRemoved() {
    goh::coord::instance().clear_providers();
    goh::tu_tanks::after_hooks_removed();
    goh::tu_combat::after_hooks_removed();
    goh::tu_infantry::after_hooks_removed();
    goh::coord::instance().release_all();
    g_OrigActorSlot23 = nullptr;
    g_OrigOpeningMainPage = nullptr;
    g_OrigPageLoadingEvent = nullptr;
    g_OrigSetCursorPos = nullptr;
    g_OrigClipCursor = nullptr;
    g_OrigMouseEvent = nullptr;
    g_OrigSetForegroundWindow = nullptr;
    g_OrigShowWindow = nullptr;
    g_OrigSetWindowPos = nullptr;
    g_AutoMissionLaunched.store(false, std::memory_order_release);
    g_SuppressCursorGrab.store(false, std::memory_order_release);
}

bool DrainAllInflightHooks() {
    for (int wait = 0; wait < 50 && goh::runtime::global_inflight.load(std::memory_order_acquire) != 0; ++wait) {
        Sleep(10);
    }
    Sleep(50);
    return goh::runtime::global_inflight.load(std::memory_order_acquire) == 0;
}

DWORD WINAPI InitThread(LPVOID) {
    goh::log::attach_module(g_SelfModule);
    const bool loaded_ini = goh::config::load_beside_module(g_SelfModule);
    const auto& cfg = goh::config::active();
    goh::log::configure(cfg);

    constexpr unsigned kTotalHooks =
        1u + goh::tu_combat::kHookCount + goh::tu_infantry::kHookCount + goh::tu_tanks::kHookCount;
    goh::log::write(goh::log::Level::summary,
                    "=== goh_ai InitThread starting (%u hooks across 4 TUs; ini=%s log=%s) ===", kTotalHooks,
                    loaded_ini ? "loaded" : "defaults", LogLevelName(cfg.log_level));

    g_Base = reinterpret_cast<goh::runtime::Address>(GetModuleHandleW(nullptr));
    goh::log::write(goh::log::Level::summary, "Game module base: 0x%llx", static_cast<unsigned long long>(g_Base));

    char actual_sha[65]{};
    if (!goh::runtime::verify_game_exe_sha256(actual_sha)) {
        goh::log::write(goh::log::Level::error, "ABORT: PE SHA-256 mismatch! actual=%s expected=%s", actual_sha,
                        goh::runtime::kBuild26734796Sha256);
        return 1;
    }
    goh::log::write(goh::log::Level::summary, "PE SHA-256 verified: %s (build 26734796)", actual_sha);

    const auto rtti = goh::runtime::resolve_and_verify_vtables(g_Base);
    goh::log::write(goh::log::Level::summary,
                    "PE32+ live RTTI vtable audit: checked=%u matched=%u mismatched=%u missing=%u", rtti.checked,
                    rtti.matched, rtti.mismatched, rtti.missing);
    if (!rtti.ok()) {
        goh::log::write(goh::log::Level::error,
                        "ABORT: PE32+ RTTI vtable verification failed (matched=%u/%u mismatched=%u missing=%u)",
                        rtti.matched, rtti.checked, rtti.mismatched, rtti.missing);
        return 1;
    }

    const bool want_skip_intro = cfg.skip_intro || !cfg.auto_mission.empty();
    const bool want_auto_dismiss = cfg.auto_dismiss_loading || !cfg.auto_mission.empty();
    const bool want_auto_mission = !cfg.auto_mission.empty();
    const bool want_suppress_cursor = cfg.suppress_cursor_grab;
    g_SuppressCursorGrab.store(want_suppress_cursor, std::memory_order_release);
    if (want_skip_intro) {
        goh::runtime::write<std::uint8_t>(g_Base, goh::runtime::table().glob.show_start_video, 0u);
        goh::log::write(goh::log::Level::summary, "Testing: disabled start video (s_showStartVideo=0)");
    }

    if (!g_Observer.attach(g_SelfModule, actual_sha))
        goh::log::write(goh::log::Level::error, "Live observer could not attach to verified game module");
    goh::log::write(goh::log::Level::summary, "Initial state: remote=%u hostEmit=%u clock=%u L=%.1f G=%.7f",
                    static_cast<unsigned>(goh::runtime::is_remote_client(g_Base)),
                    static_cast<unsigned>(goh::runtime::is_host_emit(g_Base)), goh::runtime::read_clock(g_Base),
                    ReadSpatialScaleL(), ReadGravity());

    if (MH_Initialize() != MH_OK) {
        goh::log::write(goh::log::Level::error, "ERROR: MH_Initialize failed");
        return 1;
    }

    const goh::runtime::Address slot23_rva = goh::runtime::table().hook.actor_slot23;
    void* slot23_tramp = nullptr;
    bool ok = (MH_CreateHook(reinterpret_cast<void*>(g_Base + slot23_rva), reinterpret_cast<void*>(&Hook_ActorSlot23),
                             &slot23_tramp) == MH_OK);
    if (ok) {
        g_OrigActorSlot23 = reinterpret_cast<FnActorSlot23>(slot23_tramp);
        goh::log::write(goh::log::Level::summary, "Hook staged: eActor::slot23 @ 0x%llx",
                        static_cast<unsigned long long>(slot23_rva));
    } else {
        goh::log::write(goh::log::Level::error, "ERROR: MH_CreateHook(eActor::slot23 @ 0x%llx) failed",
                        static_cast<unsigned long long>(slot23_rva));
    }

    if (ok && want_auto_mission) {
        const goh::runtime::Address open_rva = goh::runtime::table().hook.opening_main_page;
        void* open_tramp = nullptr;
        if (MH_CreateHook(reinterpret_cast<void*>(g_Base + open_rva), reinterpret_cast<void*>(&Hook_OpeningMainPage),
                          &open_tramp) == MH_OK) {
            g_OrigOpeningMainPage = reinterpret_cast<FnOpeningMainPage>(open_tramp);
            goh::log::write(goh::log::Level::summary, "Testing hook staged: OpeningMainPage @ 0x%llx",
                            static_cast<unsigned long long>(open_rva));
        } else {
            ok = false;
            goh::log::write(goh::log::Level::error, "ERROR: MH_CreateHook(OpeningMainPage @ 0x%llx) failed",
                            static_cast<unsigned long long>(open_rva));
        }
    }
    if (ok && want_auto_dismiss) {
        const goh::runtime::Address load_rva = goh::runtime::table().hook.page_loading_event;
        void* load_tramp = nullptr;
        if (MH_CreateHook(reinterpret_cast<void*>(g_Base + load_rva), reinterpret_cast<void*>(&Hook_PageLoadingEvent),
                          &load_tramp) == MH_OK) {
            g_OrigPageLoadingEvent = reinterpret_cast<FnPageLoadingEvent>(load_tramp);
            goh::log::write(goh::log::Level::summary, "Testing hook staged: eGUI_PageLoading::OnEvent @ 0x%llx",
                            static_cast<unsigned long long>(load_rva));
        } else {
            ok = false;
            goh::log::write(goh::log::Level::error, "ERROR: MH_CreateHook(eGUI_PageLoading::OnEvent @ 0x%llx) failed",
                            static_cast<unsigned long long>(load_rva));
        }
    }
    if (ok && want_suppress_cursor) {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (!user32) user32 = LoadLibraryW(L"user32.dll");
        StageUser32Hook(user32, "SetCursorPos", reinterpret_cast<void*>(&Hook_SetCursorPos), g_OrigSetCursorPos);
        StageUser32Hook(user32, "ClipCursor", reinterpret_cast<void*>(&Hook_ClipCursor), g_OrigClipCursor);
        StageUser32Hook(user32, "mouse_event", reinterpret_cast<void*>(&Hook_MouseEvent), g_OrigMouseEvent);
        StageUser32Hook(user32, "SetForegroundWindow", reinterpret_cast<void*>(&Hook_SetForegroundWindow),
                        g_OrigSetForegroundWindow);
        StageUser32Hook(user32, "ShowWindow", reinterpret_cast<void*>(&Hook_ShowWindow), g_OrigShowWindow);
        StageUser32Hook(user32, "SetWindowPos", reinterpret_cast<void*>(&Hook_SetWindowPos), g_OrigSetWindowPos);
    }

    ok &= goh::tu_combat::stage_hooks(g_Base, actual_sha, &CreateHookAdapter, &RemoveHookAdapter);
    ok &= goh::tu_infantry::stage_hooks(g_Base, actual_sha, &CreateHookAdapter, &RemoveHookAdapter);
    ok &= goh::tu_tanks::stage_hooks(g_Base, actual_sha, &CreateHookAdapter, &RemoveHookAdapter);

    if (!ok) {
        if (MH_Uninitialize() != MH_OK) {
            goh::log::write(goh::log::Level::error, "Hook staging rollback failed; retaining trampolines for shutdown");
            return 1;
        }
        CleanupAfterHooksRemoved();
        return 1;
    }

    goh::tu_combat::connect();
    g_ActorSlotGate.activate();
    if (want_auto_mission) g_OpeningMainPageGate.activate();
    if (want_auto_dismiss) g_PageLoadingGate.activate();
    if (want_suppress_cursor) g_CursorGrabGate.activate();
    goh::runtime::global_accepting.store(true, std::memory_order_release);

    const MH_STATUS en = MH_EnableHook(MH_ALL_HOOKS);
    if (en != MH_OK) {
        DeactivateAllHooks();
        goh::log::write(goh::log::Level::error, "ERROR: MH_EnableHook(MH_ALL_HOOKS) failed: %d", static_cast<int>(en));
        if (MH_DisableHook(MH_ALL_HOOKS) != MH_OK) return 1;
        if (!DrainAllInflightHooks()) return 1;
        if (MH_Uninitialize() != MH_OK) {
            goh::log::write(goh::log::Level::error, "Retaining trampolines after partial activation until shutdown");
            return 1;
        }
        CleanupAfterHooksRemoved();
        return 1;
    }

    goh::tu_infantry::on_hooks_enabled(g_Base);
    goh::log::write(goh::log::Level::summary,
                    "=== %u hooks active (slot23=1 combat=%u infantry=%u tanks=%u runtime_allowed=%d) ===", kTotalHooks,
                    goh::tu_combat::kHookCount, goh::tu_infantry::kHookCount, goh::tu_tanks::kHookCount,
                    static_cast<int>(goh::tu_infantry::runtime_allowed_flag()));
    if (want_skip_intro || want_auto_dismiss || want_auto_mission || want_suppress_cursor) {
        goh::log::write(goh::log::Level::summary,
                        "=== testing automation active (skip_intro=%u auto_dismiss_loading=%u suppress_cursor_grab=%u "
                        "auto_mission=%s) ===",
                        static_cast<unsigned>(want_skip_intro), static_cast<unsigned>(want_auto_dismiss),
                        static_cast<unsigned>(want_suppress_cursor),
                        want_auto_mission ? cfg.auto_mission.c_str() : "off");
    }
    return 0;
}

} // namespace

// Clean hot-unload helper (unhooks and drains all 79/79 hooks outside loader lock before FreeLibraryAndExitThread).
extern "C" __declspec(dllexport) DWORD WINAPI ShutdownAndUnload(LPVOID) {
    if (g_InitThread && WaitForSingleObject(g_InitThread, INFINITE) != WAIT_OBJECT_0) return ERROR_BUSY;

    goh::tu_tanks::begin_shutdown();
    goh::tu_infantry::begin_shutdown();
    if (!goh::tu_combat::drain_quiescence()) {
        goh::log::write(goh::log::Level::summary, "Unload deferred: close grenade order or release still active");
        return ERROR_BUSY;
    }
    if (!goh::tu_tanks::drain_quiescence() || !goh::tu_infantry::drain_quiescence()) {
        goh::log::write(goh::log::Level::summary,
                        "Unload deferred: watched AI move, medic order or transfer still active");
        return ERROR_BUSY;
    }

    DeactivateAllHooks();
    const MH_STATUS disabled = MH_DisableHook(MH_ALL_HOOKS);
    if (disabled != MH_OK && disabled != MH_ERROR_NOT_INITIALIZED) return ERROR_BUSY;

    if (!DrainAllInflightHooks()) {
        goh::log::write(goh::log::Level::summary,
                        "Unload deferred: hooks still inflight (global=%u slot23=%u combat=%u infantry=%u tanks=%u)",
                        goh::runtime::global_inflight.load(std::memory_order_acquire), g_ActorSlotGate.count(),
                        goh::tu_combat::inflight_count(), goh::tu_infantry::inflight_count(),
                        goh::tu_tanks::inflight_count());
        return ERROR_BUSY;
    }

    const MH_STATUS removed = MH_Uninitialize();
    if (removed != MH_OK && removed != MH_ERROR_NOT_INITIALIZED) return ERROR_BUSY;

    const auto c = goh::tu_combat::snapshot_stats();
    const auto i = goh::tu_infantry::snapshot_stats();
    const auto t = goh::tu_tanks::snapshot_stats();
    const auto support = goh::tu_infantry::field_support_stats();
    const auto nontank_repairs = goh::tu_tanks::nontank_repair_dispatches();
    CleanupAfterHooksRemoved();

    goh::log::write(
        goh::log::Level::summary,
        "=== goh_ai ShutdownAndUnload complete (dodges=%u plEsc=%u softLOF=%u softRej=%u detInf=%u detVeh=%u "
        "smokeVeh=%u smokeInf=%u smokeMed=%u smokeOff=%u smokeRel=%u medClaim=%u medFetch=%u medSmoke=%u medRev=%u "
        "medBack=%u "
        "supTrip=%u supRefill=%u tankCtx=%u tankExp=%u tankDisc=%u tankRev=%u repFull=%u repTT=%u repEmit=%u "
        "repAssign=%u repChain=%u repBoard=%u repPick=%u crewFill=%u/%u/%u crewInternal=%u crewNonTank=%u tankFace=%u "
        "faceRetarget=%u faceTimeout=%u tactOrd=%u ovfCov=%u dismSpr=%u wedgeMv=%u atVolumes=%u atAmmo=%u atHolds=%u "
        "settleH=%u memH=%u objPursue=%u objCap=%u healRest=%u burnEsc=%u kitSave=%u winBrk=%u holdUp=%u lullRel=%u "
        "brdFix=%u eligFix=%u allyVeto=%u towSup=%u fastSwp=%u mslHold=%u ffVeto=%u mgDefer=%u covFrag=%u roleSpl=%u "
        "safeDr=%u "
        "trUnld=%u "
        "sqAmmo=%u sqBand=%u scav=%u mvNudge=%u mvRes=%u mvAbt=%u cvYld=%u cvPass=%u cvRes=%u "
        "advUnfrz=%u atkPrst=%u atkNoMel=%u trapHide=%u hitDirt=%u crshEsc=%u hazPrio=%u "
        "repNonTank=%u recrew=%u rearm=%u heavyPickup=%u corpseLoot=%u) ===",
        c.dodges, c.player_escapes, c.soft_lof, c.soft_rej, c.det_inf, c.det_veh, i.smoke_veh, i.smoke_inf, i.smoke_med,
        i.smoke_off, i.smoke_rel, i.med_claim, i.med_fetch, i.med_smoke, i.med_rev, i.med_backoff, i.sup_trip,
        i.sup_refill, t.tank_ctx, t.tank_exp, t.tank_disc, t.tank_rev, t.rep_full, t.rep_tt, t.rep_emit, t.rep_assign,
        t.rep_chain, t.rep_board, t.rep_pick, t.crew_total, t.crew_breed, t.crew_inf, t.crew_internal, t.crew_nontank,
        t.tank_face, t.tank_face_retargets, t.tank_face_timeouts, i.tact_ord, i.overflow_cover, i.dismount_spread,
        i.wedge_moves, c.at_volumes, c.at_ammo, c.at_holds, c.settle_holds, c.memory_holds, i.obj_pursue, i.obj_cap,
        i.heal, i.burn, i.kit, i.window, i.stance, i.reload, i.board, i.eligible, i.allegiance_veto,
        i.towed_gun_suppress, i.fast_swaps, i.missile_holds, c.ff_vetoes, c.mg_small_arms_defer, c.cover_frags,
        c.role_splits, t.safe_doors, t.transport_unloads, i.squad_ammo, i.squad_bandages, i.scavenge, i.move_nudge,
        i.move_resume, i.move_abort, t.convoy_yield, t.convoy_pass, t.convoy_resume, c.advance_unfreeze,
        c.attack_persists, c.melee_prevented, c.trapped_hides, c.hit_dirt, i.crush_escapes, i.hazard_priorities,
        nontank_repairs, support.recrews, support.rearm, support.heavy_pickup, support.corpse_loot);
    g_Observer.close();
    goh::log::close();
    if (g_InitThread) {
        CloseHandle(g_InitThread);
        g_InitThread = nullptr;
    }
    g_ShutdownDone.store(true, std::memory_order_release);
    FreeLibraryAndExitThread(g_SelfModule, 0);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID reserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        g_SelfModule = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
        g_InitThread = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        goh::tu_tanks::begin_shutdown();
        goh::tu_infantry::begin_shutdown();
        // Process termination or completed ShutdownAndUnload: never re-run unhooking/log writes under loader lock.
        if (reserved || g_ShutdownDone.load(std::memory_order_acquire)) return TRUE;
        DeactivateAllHooks();
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        CleanupAfterHooksRemoved();
        if (g_InitThread) {
            CloseHandle(g_InitThread);
            g_InitThread = nullptr;
        }
        goh::log::write(goh::log::Level::summary, "=== goh_ai unloaded ===");
        g_Observer.close();
        goh::log::close();
    }
    return TRUE;
}
