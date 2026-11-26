// Standalone Host-Only Early Flying-Grenade Dodge DLL (Steam build 26734796).
// Thin wrapper around mods/grenade_dodge_module.hpp + shared runtime/config.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>

#include "MinHook.h"
#include "config_and_log.hpp"
#include "game_runtime.hpp"
#include "grenade_dodge_module.hpp"

namespace {

using FnActorSlot23 = void(__fastcall*)(goh::runtime::Address actor);

HMODULE g_SelfModule = nullptr;
HANDLE g_InitThread = nullptr;
goh::runtime::HookGate g_Slot23Gate{};
FnActorSlot23 g_OrigActorSlot23 = nullptr;

void __fastcall Hook_ActorSlot23(goh::runtime::Address actor) {
    goh::runtime::HookGuard guard(g_Slot23Gate);
    goh::grenade_dodge::on_actor_pre_quant(actor);
    g_OrigActorSlot23(actor);
}

DWORD WINAPI InitThread(LPVOID) {
    goh::log::attach_module(g_SelfModule);
    goh::config::load_beside_module(g_SelfModule);
    goh::log::configure(goh::config::active());
    goh::log::write(goh::log::Level::summary, "=== grenade_dodge.dll InitThread starting ===");

    const auto base = reinterpret_cast<goh::runtime::Address>(GetModuleHandleW(nullptr));
    char actual_sha[65]{};
    if (!goh::runtime::verify_game_exe_sha256(actual_sha)) {
        goh::log::write(goh::log::Level::error, "ABORT: PE SHA-256 mismatch! actual=%s expected=%s", actual_sha,
                        goh::runtime::kBuild26734796Sha256);
        return 1;
    }

    const auto rtti = goh::runtime::resolve_and_verify_vtables(base);
    goh::log::write(goh::log::Level::summary, "PE SHA-256 verified (%s); RTTI vtables matched=%u/%u mismatch=%u",
                    actual_sha, rtti.matched, rtti.checked, rtti.mismatched);

    if (MH_Initialize() != MH_OK) {
        goh::log::write(goh::log::Level::error, "ERROR: MH_Initialize failed");
        return 1;
    }

    void* slot23_target = reinterpret_cast<void*>(base + goh::runtime::table().hook.actor_slot23);
    void* slot23_orig = nullptr;
    bool ok = (MH_CreateHook(slot23_target, reinterpret_cast<void*>(Hook_ActorSlot23), &slot23_orig) == MH_OK);
    if (ok) g_OrigActorSlot23 = reinterpret_cast<FnActorSlot23>(slot23_orig);
    ok &= goh::grenade_dodge::prepare_hooks(base, actual_sha, &MH_CreateHook, &MH_RemoveHook);
    if (!ok) {
        MH_Uninitialize();
        goh::grenade_dodge::after_hooks_removed();
        goh::log::write(goh::log::Level::error, "ERROR: hook staging failed");
        return 1;
    }

    g_Slot23Gate.activate();
    goh::runtime::global_accepting.store(true, std::memory_order_release);
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        goh::runtime::global_accepting.store(false, std::memory_order_release);
        g_Slot23Gate.deactivate();
        goh::grenade_dodge::deactivate();
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        goh::grenade_dodge::after_hooks_removed();
        goh::log::write(goh::log::Level::error, "ERROR: MH_EnableHook failed");
        return 1;
    }

    goh::log::write(goh::log::Level::summary, "=== grenade_dodge.dll 6 hooks active ===");
    return 0;
}

} // namespace

extern "C" __declspec(dllexport) DWORD WINAPI ShutdownAndUnload(LPVOID) {
    if (g_InitThread && WaitForSingleObject(g_InitThread, INFINITE) != WAIT_OBJECT_0) return ERROR_BUSY;
    goh::runtime::global_accepting.store(false, std::memory_order_release);
    g_Slot23Gate.deactivate();
    goh::grenade_dodge::deactivate();
    const MH_STATUS disabled = MH_DisableHook(MH_ALL_HOOKS);
    if (disabled != MH_OK && disabled != MH_ERROR_NOT_INITIALIZED) return ERROR_BUSY;
    for (int wait = 0; wait < 100 && goh::runtime::global_inflight.load(std::memory_order_acquire) != 0; ++wait) {
        Sleep(10);
    }
    if (goh::runtime::global_inflight.load(std::memory_order_acquire) != 0) return ERROR_BUSY;
    MH_Uninitialize();
    goh::log::write(goh::log::Level::summary,
                    "=== grenade_dodge.dll ShutdownAndUnload complete (dodges=%u wakes=%u) ===",
                    goh::grenade_dodge::total_dodges(), goh::grenade_dodge::total_wakeups());
    goh::grenade_dodge::after_hooks_removed();
    goh::coord::instance().release_all();
    goh::log::close();
    FreeLibraryAndExitThread(g_SelfModule, 0);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID reserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        g_SelfModule = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
        g_InitThread = CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        if (reserved) return TRUE;
        goh::runtime::global_accepting.store(false, std::memory_order_release);
        g_Slot23Gate.deactivate();
        goh::grenade_dodge::deactivate();
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        goh::grenade_dodge::after_hooks_removed();
        goh::coord::instance().release_all();
        if (g_InitThread) CloseHandle(g_InitThread);
        goh::log::close();
    }
    return TRUE;
}
