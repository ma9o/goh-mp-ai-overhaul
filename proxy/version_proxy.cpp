/**
 * version.dll Proxy for Gates of Hell: MP AI Overhaul
 *
 * Drop-in proxy for <GameDir>/binaries/x64/version.dll:
 *   1. Lazily loads the real System32\version.dll outside the loader lock and
 *      forwards all 17 version.dll exports to it.
 *   2. When hosted by call_to_arms.exe (skipping call_to_arms_ed.exe and other
 *      tools), loads goh_ai_mod.dll from the same folder on a background thread.
 *
 * Installation:
 *   Copy version.dll and goh_ai_mod.dll into binaries/x64/ and launch normally.
 *   On Proton/Wine, add Steam launch option: WINEDLLOVERRIDES="version=n,b" %command%
 */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
// Prevent <windows.h> from pulling in <winver.h> dllimport declarations.
#define _WINVER_
#include <windows.h>

#include <atomic>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace {

HMODULE g_SelfModule = nullptr;
HMODULE g_RealVersionDll = nullptr;
HMODULE g_ModDll = nullptr;
std::atomic<bool> g_StopWatcher{false};
wchar_t g_ShadowPath[MAX_PATH * 4]{};

INIT_ONCE g_RealVersionOnce = INIT_ONCE_STATIC_INIT;
SRWLOCK g_LogLock = SRWLOCK_INIT;
FILE* g_ProxyLog = nullptr;

FARPROC p_GetFileVersionInfoA = nullptr;
FARPROC p_GetFileVersionInfoByHandle = nullptr;
FARPROC p_GetFileVersionInfoExA = nullptr;
FARPROC p_GetFileVersionInfoExW = nullptr;
FARPROC p_GetFileVersionInfoSizeA = nullptr;
FARPROC p_GetFileVersionInfoSizeExA = nullptr;
FARPROC p_GetFileVersionInfoSizeExW = nullptr;
FARPROC p_GetFileVersionInfoSizeW = nullptr;
FARPROC p_GetFileVersionInfoW = nullptr;
FARPROC p_VerFindFileA = nullptr;
FARPROC p_VerFindFileW = nullptr;
FARPROC p_VerInstallFileA = nullptr;
FARPROC p_VerInstallFileW = nullptr;
FARPROC p_VerLanguageNameA = nullptr;
FARPROC p_VerLanguageNameW = nullptr;
FARPROC p_VerQueryValueA = nullptr;
FARPROC p_VerQueryValueW = nullptr;

const wchar_t* BaseNameW(const wchar_t* path) noexcept {
    if (!path) return L"";
    const wchar_t* last = path;
    for (const wchar_t* p = path; *p != L'\0'; ++p) {
        if (*p == L'\\' || *p == L'/') last = p + 1;
    }
    return last;
}

bool BuildSiblingPathW(HMODULE module, const wchar_t* filename, wchar_t* out, DWORD outCap) noexcept {
    if (!filename || !out || outCap == 0) return false;
    const DWORD len = GetModuleFileNameW(module, out, outCap);
    if (len == 0 || len >= outCap) return false;

    wchar_t* slash = std::wcsrchr(out, L'\\');
    wchar_t* fslash = std::wcsrchr(out, L'/');
    if (fslash && (!slash || fslash > slash)) slash = fslash;
    if (!slash) return false;

    const std::size_t prefixLen = static_cast<std::size_t>((slash + 1) - out);
    const std::size_t nameLen = std::wcslen(filename) + 1;
    if (prefixLen + nameLen > outCap) return false;

    std::wmemcpy(out + prefixLen, filename, nameLen);
    return true;
}

void WideToUtf8(const wchar_t* wide, char* out, int outCap) noexcept {
    if (!out || outCap <= 0) return;
    out[0] = '\0';
    if (!wide || !*wide) return;
    const int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, out, outCap, nullptr, nullptr);
    if (n <= 0) out[0] = '\0';
}

bool IsTargetGameProcess() noexcept {
    wchar_t exePath[MAX_PATH * 4]{};
    const DWORD len = GetModuleFileNameW(nullptr, exePath, static_cast<DWORD>(sizeof(exePath) / sizeof(exePath[0])));
    if (len == 0 || len >= static_cast<DWORD>(sizeof(exePath) / sizeof(exePath[0]))) return false;
    return _wcsicmp(BaseNameW(exePath), L"call_to_arms.exe") == 0;
}

#if defined(__MINGW_PRINTF_FORMAT)
__attribute__((format(__MINGW_PRINTF_FORMAT, 1, 2)))
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
void ProxyLog(const char* format, ...) {
    AcquireSRWLockExclusive(&g_LogLock);
    if (!g_ProxyLog) {
        wchar_t logPath[MAX_PATH * 4]{};
        if (BuildSiblingPathW(g_SelfModule, L"version_proxy.log", logPath,
                              static_cast<DWORD>(sizeof(logPath) / sizeof(logPath[0])))) {
            g_ProxyLog = _wfopen(logPath, L"w");
        }
    }
    if (g_ProxyLog) {
        va_list args;
        va_start(args, format);
        std::vfprintf(g_ProxyLog, format, args);
        std::fputc('\n', g_ProxyLog);
        std::fflush(g_ProxyLog);
        va_end(args);
    }
    ReleaseSRWLockExclusive(&g_LogLock);
}

BOOL CALLBACK InitRealVersionDllOnce(PINIT_ONCE /*initOnce*/, PVOID /*param*/, PVOID* /*ctx*/) {
    wchar_t systemPath[MAX_PATH * 2]{};
    const UINT cap = static_cast<UINT>(sizeof(systemPath) / sizeof(systemPath[0]));
    const UINT len = GetSystemDirectoryW(systemPath, cap);
    static constexpr wchar_t kSuffix[] = L"\\version.dll";
    const UINT suffixLen = static_cast<UINT>(sizeof(kSuffix) / sizeof(kSuffix[0]));
    if (len == 0 || len + suffixLen > cap) return FALSE;

    std::wmemcpy(systemPath + len, kSuffix, suffixLen);
    HMODULE hReal = LoadLibraryW(systemPath);
    if (!hReal) return FALSE;

    p_GetFileVersionInfoA = GetProcAddress(hReal, "GetFileVersionInfoA");
    p_GetFileVersionInfoByHandle = GetProcAddress(hReal, "GetFileVersionInfoByHandle");
    p_GetFileVersionInfoExA = GetProcAddress(hReal, "GetFileVersionInfoExA");
    p_GetFileVersionInfoExW = GetProcAddress(hReal, "GetFileVersionInfoExW");
    p_GetFileVersionInfoSizeA = GetProcAddress(hReal, "GetFileVersionInfoSizeA");
    p_GetFileVersionInfoSizeExA = GetProcAddress(hReal, "GetFileVersionInfoSizeExA");
    p_GetFileVersionInfoSizeExW = GetProcAddress(hReal, "GetFileVersionInfoSizeExW");
    p_GetFileVersionInfoSizeW = GetProcAddress(hReal, "GetFileVersionInfoSizeW");
    p_GetFileVersionInfoW = GetProcAddress(hReal, "GetFileVersionInfoW");
    p_VerFindFileA = GetProcAddress(hReal, "VerFindFileA");
    p_VerFindFileW = GetProcAddress(hReal, "VerFindFileW");
    p_VerInstallFileA = GetProcAddress(hReal, "VerInstallFileA");
    p_VerInstallFileW = GetProcAddress(hReal, "VerInstallFileW");
    p_VerLanguageNameA = GetProcAddress(hReal, "VerLanguageNameA");
    p_VerLanguageNameW = GetProcAddress(hReal, "VerLanguageNameW");
    p_VerQueryValueA = GetProcAddress(hReal, "VerQueryValueA");
    p_VerQueryValueW = GetProcAddress(hReal, "VerQueryValueW");

    g_RealVersionDll = hReal;
    return TRUE;
}

bool EnsureRealVersionDll() noexcept {
    return InitOnceExecuteOnce(&g_RealVersionOnce, InitRealVersionDllOnce, nullptr, nullptr) != FALSE &&
           g_RealVersionDll != nullptr;
}

struct FileStamp {
    std::uint64_t mtime{};
    DWORD size{};
    bool valid{};
};

FileStamp StatFileW(const wchar_t* path) noexcept {
    FileStamp st{};
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!path || !GetFileAttributesExW(path, GetFileExInfoStandard, &data)) return st;
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 || data.nFileSizeHigh != 0) return st;
    st.mtime = (static_cast<std::uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) |
               static_cast<std::uint64_t>(data.ftLastWriteTime.dwLowDateTime);
    st.size = data.nFileSizeLow;
    st.valid = true;
    return st;
}

bool FileExistsW(const wchar_t* path) noexcept {
    if (!path) return false;
    const DWORD attrs = GetFileAttributesW(path);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool CanReadFileExclusivelyW(const wchar_t* path) noexcept {
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

bool IsHotReloadEnabled() noexcept {
    wchar_t markerPath[MAX_PATH * 4]{};
    if (BuildSiblingPathW(g_SelfModule, L"goh_ai.hotreload", markerPath,
                          static_cast<DWORD>(sizeof(markerPath) / sizeof(markerPath[0]))) &&
        FileExistsW(markerPath)) {
        return true;
    }
    wchar_t iniPath[MAX_PATH * 4]{};
    if (!BuildSiblingPathW(g_SelfModule, L"goh_ai.ini", iniPath,
                           static_cast<DWORD>(sizeof(iniPath) / sizeof(iniPath[0])))) {
        return false;
    }
    FILE* f = _wfopen(iniPath, L"rb");
    if (!f) return false;
    char line[512]{};
    bool inTesting = false;
    bool enabled = false;
    while (std::fgets(line, static_cast<int>(sizeof(line)), f)) {
        char* p = line;
        if (static_cast<unsigned char>(p[0]) == 0xEF && static_cast<unsigned char>(p[1]) == 0xBB &&
            static_cast<unsigned char>(p[2]) == 0xBF) {
            p += 3;
        }
        for (char* c = p; *c; ++c) {
            if (*c == ';' || *c == '#' || *c == '\r' || *c == '\n') {
                *c = '\0';
                break;
            }
        }
        while (*p && std::isspace(static_cast<unsigned char>(*p)))
            ++p;
        std::size_t len = std::strlen(p);
        while (len > 0 && std::isspace(static_cast<unsigned char>(p[len - 1])))
            p[--len] = '\0';
        if (len == 0) continue;
        if (p[0] == '[' && p[len - 1] == ']') {
            char sec[64]{};
            std::size_t si = 0;
            for (std::size_t i = 1; i + 1 < len && si + 1 < sizeof(sec); ++i) {
                if (!std::isspace(static_cast<unsigned char>(p[i]))) {
                    sec[si++] = static_cast<char>(std::tolower(static_cast<unsigned char>(p[i])));
                }
            }
            sec[si] = '\0';
            inTesting = (std::strcmp(sec, "testing") == 0);
            continue;
        }
        if (!inTesting) continue;
        char* eq = std::strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char key[64]{};
        std::size_t ki = 0;
        for (const char* k = p; *k && ki + 1 < sizeof(key); ++k) {
            if (!std::isspace(static_cast<unsigned char>(*k))) {
                key[ki++] = static_cast<char>(std::tolower(static_cast<unsigned char>(*k)));
            }
        }
        key[ki] = '\0';
        if (std::strcmp(key, "hot_reload") != 0) continue;
        char val[32]{};
        std::size_t vi = 0;
        for (const char* v = eq + 1; *v && vi + 1 < sizeof(val); ++v) {
            if (!std::isspace(static_cast<unsigned char>(*v))) {
                val[vi++] = static_cast<char>(std::tolower(static_cast<unsigned char>(*v)));
            }
        }
        val[vi] = '\0';
        if (std::strcmp(val, "true") == 0 || std::strcmp(val, "1") == 0 || std::strcmp(val, "yes") == 0 ||
            std::strcmp(val, "on") == 0) {
            enabled = true;
        } else if (std::strcmp(val, "false") == 0 || std::strcmp(val, "0") == 0 || std::strcmp(val, "no") == 0 ||
                   std::strcmp(val, "off") == 0) {
            enabled = false;
        }
    }
    std::fclose(f);
    return enabled;
}

void CleanupStaleShadows() noexcept {
    wchar_t pattern[MAX_PATH * 4]{};
    if (!BuildSiblingPathW(g_SelfModule, L"goh_ai_mod.live.*.dll", pattern,
                           static_cast<DWORD>(sizeof(pattern) / sizeof(pattern[0])))) {
        return;
    }
    WIN32_FIND_DATAW fd{};
    HANDLE find = FindFirstFileW(pattern, &fd);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
        wchar_t candidate[MAX_PATH * 4]{};
        if (BuildSiblingPathW(g_SelfModule, fd.cFileName, candidate,
                              static_cast<DWORD>(sizeof(candidate) / sizeof(candidate[0])))) {
            DeleteFileW(candidate);
        }
    } while (FindNextFileW(find, &fd));
    FindClose(find);
}

bool LoadShadowMod(const wchar_t* sourcePath, unsigned generation) {
    wchar_t shadowName[128]{};
    std::swprintf(shadowName, sizeof(shadowName) / sizeof(shadowName[0]), L"goh_ai_mod.live.%lu.%u.dll",
                  static_cast<unsigned long>(GetCurrentProcessId()), generation);
    wchar_t shadowPath[MAX_PATH * 4]{};
    if (!BuildSiblingPathW(g_SelfModule, shadowName, shadowPath,
                           static_cast<DWORD>(sizeof(shadowPath) / sizeof(shadowPath[0])))) {
        ProxyLog("ERROR: Could not build shadow path for gen=%u", generation);
        return false;
    }
    if (!CopyFileW(sourcePath, shadowPath, FALSE)) {
        ProxyLog("WARNING: CopyFileW to shadow gen=%u failed (error %lu)", generation,
                 static_cast<unsigned long>(GetLastError()));
        return false;
    }
    HMODULE mod = LoadLibraryW(shadowPath);
    if (!mod) {
        ProxyLog("WARNING: LoadLibraryW on shadow gen=%u failed (error %lu)", generation,
                 static_cast<unsigned long>(GetLastError()));
        DeleteFileW(shadowPath);
        return false;
    }
    g_ModDll = mod;
    std::wmemcpy(g_ShadowPath, shadowPath, sizeof(g_ShadowPath) / sizeof(g_ShadowPath[0]));
    char utf8Shadow[MAX_PATH * 4]{};
    WideToUtf8(shadowPath, utf8Shadow, static_cast<int>(sizeof(utf8Shadow)));
    ProxyLog("Hot-reload loaded shadow gen=%u (%s)", generation, utf8Shadow);
    return true;
}

bool UnloadCurrentModForReload() {
    if (!g_ModDll) return true;
    auto shutdownFn = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(g_ModDll, "ShutdownAndUnload"));
    if (!shutdownFn) {
        ProxyLog("WARNING: ShutdownAndUnload export missing; cannot hot-unload safely");
        return false;
    }
    DWORD lastCode = ERROR_BUSY;
    for (int attempt = 0; attempt < 25 && !g_StopWatcher.load(std::memory_order_acquire); ++attempt) {
        HANDLE worker = CreateThread(nullptr, 0, shutdownFn, nullptr, 0, nullptr);
        if (!worker) {
            ProxyLog("WARNING: CreateThread(ShutdownAndUnload) failed (error %lu)",
                     static_cast<unsigned long>(GetLastError()));
            return false;
        }
        const DWORD wait = WaitForSingleObject(worker, 15000);
        DWORD exitCode = ERROR_GEN_FAILURE;
        if (wait == WAIT_OBJECT_0) GetExitCodeThread(worker, &exitCode);
        CloseHandle(worker);
        lastCode = exitCode;
        if (wait == WAIT_OBJECT_0 && exitCode == 0) {
            g_ModDll = nullptr;
            if (g_ShadowPath[0] != L'\0') {
                DeleteFileW(g_ShadowPath);
                g_ShadowPath[0] = L'\0';
            }
            return true;
        }
        if (exitCode != ERROR_BUSY) break;
        Sleep(250);
    }
    ProxyLog("WARNING: ShutdownAndUnload deferred/failed during hot-reload (code=%lu)",
             static_cast<unsigned long>(lastCode));
    return false;
}

void RunHotReloadWatcher(const wchar_t* modPath, FileStamp loadedStamp, unsigned generation) {
    wchar_t triggerPath[MAX_PATH * 4]{};
    const bool haveTrigger = BuildSiblingPathW(g_SelfModule, L"goh_ai.reload", triggerPath,
                                               static_cast<DWORD>(sizeof(triggerPath) / sizeof(triggerPath[0])));
    while (!g_StopWatcher.load(std::memory_order_acquire)) {
        Sleep(300);
        if (g_StopWatcher.load(std::memory_order_acquire)) break;
        const bool triggered = haveTrigger && FileExistsW(triggerPath);
        const FileStamp now = StatFileW(modPath);
        const bool changed = now.valid && now.size >= 4096 &&
                             (!loadedStamp.valid || now.mtime != loadedStamp.mtime || now.size != loadedStamp.size);
        if (!triggered && !changed) continue;

        // Wait until the replacement DLL stops changing and can be read without a concurrent writer.
        FileStamp stable = now;
        bool ready = false;
        for (int wait = 0; wait < 20 && !g_StopWatcher.load(std::memory_order_acquire); ++wait) {
            Sleep(200);
            const FileStamp again = StatFileW(modPath);
            if (again.valid && again.size >= 4096 && again.mtime == stable.mtime && again.size == stable.size &&
                CanReadFileExclusivelyW(modPath)) {
                stable = again;
                ready = true;
                break;
            }
            stable = again;
        }
        if (triggered) DeleteFileW(triggerPath);
        if (!ready) {
            ProxyLog("WARNING: Hot-reload skipped (goh_ai_mod.dll not ready/stable)");
            continue;
        }

        ProxyLog("Hot-reload detected updated goh_ai_mod.dll (size=%lu); unloading gen=%u",
                 static_cast<unsigned long>(stable.size), generation);
        if (!UnloadCurrentModForReload()) {
            loadedStamp = stable;
            continue;
        }
        CleanupStaleShadows();
        ++generation;
        if (LoadShadowMod(modPath, generation)) {
            loadedStamp = stable;
            ProxyLog("Hot-reload complete gen=%u (size=%lu)", generation, static_cast<unsigned long>(stable.size));
        } else {
            loadedStamp = stable;
        }
    }
}

DWORD WINAPI LoadModThread(LPVOID /*param*/) {
    // Only activate inside the game executable; leave the editor and helper tools untouched.
    if (!IsTargetGameProcess()) return 0;

    // Wait briefly so early CRT and static DLL initialization completes before hooking.
    Sleep(500);

    ProxyLog("=== Gates of Hell AI Overhaul version.dll proxy ===");
    if (EnsureRealVersionDll()) {
        ProxyLog("Forwarded System32\\version.dll ready");
    } else {
        ProxyLog("WARNING: Failed to load real System32\\version.dll (error %lu)",
                 static_cast<unsigned long>(GetLastError()));
    }

    CleanupStaleShadows();

    if (GetModuleHandleW(L"goh_ai_mod.dll")) {
        ProxyLog("goh_ai_mod.dll is already loaded; skipping duplicate load");
        return 0;
    }

    wchar_t modPath[MAX_PATH * 4]{};
    if (!BuildSiblingPathW(g_SelfModule, L"goh_ai_mod.dll", modPath,
                           static_cast<DWORD>(sizeof(modPath) / sizeof(modPath[0])))) {
        ProxyLog("ERROR: Could not resolve goh_ai_mod.dll path beside version.dll");
        return 1;
    }

    char utf8Path[MAX_PATH * 4]{};
    WideToUtf8(modPath, utf8Path, static_cast<int>(sizeof(utf8Path)));

    if (IsHotReloadEnabled()) {
        // Pin version.dll until ExitProcess so an explicit FreeLibrary(version.dll) during
        // shutdown cannot unmap RunHotReloadWatcher while it is sleeping.
        HMODULE pinned = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&LoadModThread), &pinned);
        const FileStamp initialStamp = StatFileW(modPath);
        if (!initialStamp.valid) {
            ProxyLog("WARNING: goh_ai_mod.dll not found at %s; running vanilla", utf8Path);
            return 1;
        }
        if (!LoadShadowMod(modPath, 1u)) {
            ProxyLog("WARNING: Shadow load failed; running vanilla");
            return 1;
        }
        ProxyLog("Hot-reload complete gen=1 (size=%lu)", static_cast<unsigned long>(initialStamp.size));
        RunHotReloadWatcher(modPath, initialStamp, 1u);
        return 0;
    }

    ProxyLog("Loading %s", utf8Path);

    g_ModDll = LoadLibraryW(modPath);
    if (!g_ModDll) {
        ProxyLog("WARNING: Failed to load goh_ai_mod.dll (error %lu); running vanilla",
                 static_cast<unsigned long>(GetLastError()));
        return 1;
    }

    ProxyLog("Loaded goh_ai_mod.dll successfully (see goh_ai.log for hook status)");
    return 0;
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH: {
            g_SelfModule = hinstDLL;
            DisableThreadLibraryCalls(hinstDLL);
            HANDLE thread = CreateThread(nullptr, 0, LoadModThread, nullptr, 0, nullptr);
            if (thread) CloseHandle(thread);
            break;
        }
        case DLL_PROCESS_DETACH:
            g_StopWatcher.store(true, std::memory_order_release);
            // On process termination (lpvReserved != nullptr), let the OS reclaim handles/DLLs safely.
            if (lpvReserved) return TRUE;
            if (g_ModDll) {
                FreeLibrary(g_ModDll);
                g_ModDll = nullptr;
            }
            if (g_ShadowPath[0] != L'\0') {
                DeleteFileW(g_ShadowPath);
                g_ShadowPath[0] = L'\0';
            }
            if (g_RealVersionDll) {
                FreeLibrary(g_RealVersionDll);
                g_RealVersionDll = nullptr;
            }
            AcquireSRWLockExclusive(&g_LogLock);
            if (g_ProxyLog) {
                std::fclose(g_ProxyLog);
                g_ProxyLog = nullptr;
            }
            ReleaseSRWLockExclusive(&g_LogLock);
            break;
        default: break;
    }
    return TRUE;
}

#ifdef _WIN64

extern "C" __declspec(dllexport) BOOL WINAPI GetFileVersionInfoA(LPCSTR lptstrFilename, DWORD dwHandle, DWORD dwLen,
                                                                 LPVOID lpData) {
    using Func = BOOL(WINAPI*)(LPCSTR, DWORD, DWORD, LPVOID);
    if (!EnsureRealVersionDll() || !p_GetFileVersionInfoA) return FALSE;
    return reinterpret_cast<Func>(p_GetFileVersionInfoA)(lptstrFilename, dwHandle, dwLen, lpData);
}

extern "C" __declspec(dllexport) BOOL WINAPI GetFileVersionInfoW(LPCWSTR lptstrFilename, DWORD dwHandle, DWORD dwLen,
                                                                 LPVOID lpData) {
    using Func = BOOL(WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID);
    if (!EnsureRealVersionDll() || !p_GetFileVersionInfoW) return FALSE;
    return reinterpret_cast<Func>(p_GetFileVersionInfoW)(lptstrFilename, dwHandle, dwLen, lpData);
}

extern "C" __declspec(dllexport) DWORD WINAPI GetFileVersionInfoSizeA(LPCSTR lptstrFilename, LPDWORD lpdwHandle) {
    using Func = DWORD(WINAPI*)(LPCSTR, LPDWORD);
    if (!EnsureRealVersionDll() || !p_GetFileVersionInfoSizeA) return 0;
    return reinterpret_cast<Func>(p_GetFileVersionInfoSizeA)(lptstrFilename, lpdwHandle);
}

extern "C" __declspec(dllexport) DWORD WINAPI GetFileVersionInfoSizeW(LPCWSTR lptstrFilename, LPDWORD lpdwHandle) {
    using Func = DWORD(WINAPI*)(LPCWSTR, LPDWORD);
    if (!EnsureRealVersionDll() || !p_GetFileVersionInfoSizeW) return 0;
    return reinterpret_cast<Func>(p_GetFileVersionInfoSizeW)(lptstrFilename, lpdwHandle);
}

extern "C" __declspec(dllexport) BOOL WINAPI GetFileVersionInfoExA(DWORD dwFlags, LPCSTR lpwstrFilename, DWORD dwHandle,
                                                                   DWORD dwLen, LPVOID lpData) {
    using Func = BOOL(WINAPI*)(DWORD, LPCSTR, DWORD, DWORD, LPVOID);
    if (!EnsureRealVersionDll() || !p_GetFileVersionInfoExA) return FALSE;
    return reinterpret_cast<Func>(p_GetFileVersionInfoExA)(dwFlags, lpwstrFilename, dwHandle, dwLen, lpData);
}

extern "C" __declspec(dllexport) BOOL WINAPI GetFileVersionInfoExW(DWORD dwFlags, LPCWSTR lpwstrFilename,
                                                                   DWORD dwHandle, DWORD dwLen, LPVOID lpData) {
    using Func = BOOL(WINAPI*)(DWORD, LPCWSTR, DWORD, DWORD, LPVOID);
    if (!EnsureRealVersionDll() || !p_GetFileVersionInfoExW) return FALSE;
    return reinterpret_cast<Func>(p_GetFileVersionInfoExW)(dwFlags, lpwstrFilename, dwHandle, dwLen, lpData);
}

extern "C" __declspec(dllexport) DWORD WINAPI GetFileVersionInfoSizeExA(DWORD dwFlags, LPCSTR lpwstrFilename,
                                                                        LPDWORD lpdwHandle) {
    using Func = DWORD(WINAPI*)(DWORD, LPCSTR, LPDWORD);
    if (!EnsureRealVersionDll() || !p_GetFileVersionInfoSizeExA) return 0;
    return reinterpret_cast<Func>(p_GetFileVersionInfoSizeExA)(dwFlags, lpwstrFilename, lpdwHandle);
}

extern "C" __declspec(dllexport) DWORD WINAPI GetFileVersionInfoSizeExW(DWORD dwFlags, LPCWSTR lpwstrFilename,
                                                                        LPDWORD lpdwHandle) {
    using Func = DWORD(WINAPI*)(DWORD, LPCWSTR, LPDWORD);
    if (!EnsureRealVersionDll() || !p_GetFileVersionInfoSizeExW) return 0;
    return reinterpret_cast<Func>(p_GetFileVersionInfoSizeExW)(dwFlags, lpwstrFilename, lpdwHandle);
}

extern "C" __declspec(dllexport) BOOL WINAPI VerQueryValueA(LPCVOID pBlock, LPCSTR lpSubBlock, LPVOID* lplpBuffer,
                                                            PUINT puLen) {
    using Func = BOOL(WINAPI*)(LPCVOID, LPCSTR, LPVOID*, PUINT);
    if (!EnsureRealVersionDll() || !p_VerQueryValueA) return FALSE;
    return reinterpret_cast<Func>(p_VerQueryValueA)(pBlock, lpSubBlock, lplpBuffer, puLen);
}

extern "C" __declspec(dllexport) BOOL WINAPI VerQueryValueW(LPCVOID pBlock, LPCWSTR lpSubBlock, LPVOID* lplpBuffer,
                                                            PUINT puLen) {
    using Func = BOOL(WINAPI*)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
    if (!EnsureRealVersionDll() || !p_VerQueryValueW) return FALSE;
    return reinterpret_cast<Func>(p_VerQueryValueW)(pBlock, lpSubBlock, lplpBuffer, puLen);
}

extern "C" __declspec(dllexport) DWORD WINAPI VerFindFileA(DWORD uFlags, LPSTR szFileName, LPSTR szWinDir,
                                                           LPSTR szAppDir, LPSTR szCurDir, PUINT puCurDirLen,
                                                           LPSTR szDestDir, PUINT puDestDirLen) {
    using Func = DWORD(WINAPI*)(DWORD, LPSTR, LPSTR, LPSTR, LPSTR, PUINT, LPSTR, PUINT);
    if (!EnsureRealVersionDll() || !p_VerFindFileA) return 0;
    return reinterpret_cast<Func>(p_VerFindFileA)(uFlags, szFileName, szWinDir, szAppDir, szCurDir, puCurDirLen,
                                                  szDestDir, puDestDirLen);
}

extern "C" __declspec(dllexport) DWORD WINAPI VerFindFileW(DWORD uFlags, LPWSTR szFileName, LPWSTR szWinDir,
                                                           LPWSTR szAppDir, LPWSTR szCurDir, PUINT puCurDirLen,
                                                           LPWSTR szDestDir, PUINT puDestDirLen) {
    using Func = DWORD(WINAPI*)(DWORD, LPWSTR, LPWSTR, LPWSTR, LPWSTR, PUINT, LPWSTR, PUINT);
    if (!EnsureRealVersionDll() || !p_VerFindFileW) return 0;
    return reinterpret_cast<Func>(p_VerFindFileW)(uFlags, szFileName, szWinDir, szAppDir, szCurDir, puCurDirLen,
                                                  szDestDir, puDestDirLen);
}

extern "C" __declspec(dllexport) DWORD WINAPI VerInstallFileA(DWORD uFlags, LPSTR szSrcFileName, LPSTR szDestFileName,
                                                              LPSTR szSrcDir, LPSTR szDestDir, LPSTR szCurDir,
                                                              LPSTR szTmpFile, PUINT puTmpFileLen) {
    using Func = DWORD(WINAPI*)(DWORD, LPSTR, LPSTR, LPSTR, LPSTR, LPSTR, LPSTR, PUINT);
    if (!EnsureRealVersionDll() || !p_VerInstallFileA) return 0;
    return reinterpret_cast<Func>(p_VerInstallFileA)(uFlags, szSrcFileName, szDestFileName, szSrcDir, szDestDir,
                                                     szCurDir, szTmpFile, puTmpFileLen);
}

extern "C" __declspec(dllexport) DWORD WINAPI VerInstallFileW(DWORD uFlags, LPWSTR szSrcFileName, LPWSTR szDestFileName,
                                                              LPWSTR szSrcDir, LPWSTR szDestDir, LPWSTR szCurDir,
                                                              LPWSTR szTmpFile, PUINT puTmpFileLen) {
    using Func = DWORD(WINAPI*)(DWORD, LPWSTR, LPWSTR, LPWSTR, LPWSTR, LPWSTR, LPWSTR, PUINT);
    if (!EnsureRealVersionDll() || !p_VerInstallFileW) return 0;
    return reinterpret_cast<Func>(p_VerInstallFileW)(uFlags, szSrcFileName, szDestFileName, szSrcDir, szDestDir,
                                                     szCurDir, szTmpFile, puTmpFileLen);
}

extern "C" __declspec(dllexport) DWORD WINAPI VerLanguageNameA(DWORD wLang, LPSTR szLang, DWORD cchLang) {
    using Func = DWORD(WINAPI*)(DWORD, LPSTR, DWORD);
    if (!EnsureRealVersionDll() || !p_VerLanguageNameA) return 0;
    return reinterpret_cast<Func>(p_VerLanguageNameA)(wLang, szLang, cchLang);
}

extern "C" __declspec(dllexport) DWORD WINAPI VerLanguageNameW(DWORD wLang, LPWSTR szLang, DWORD cchLang) {
    using Func = DWORD(WINAPI*)(DWORD, LPWSTR, DWORD);
    if (!EnsureRealVersionDll() || !p_VerLanguageNameW) return 0;
    return reinterpret_cast<Func>(p_VerLanguageNameW)(wLang, szLang, cchLang);
}

extern "C" __declspec(dllexport) int WINAPI GetFileVersionInfoByHandle(DWORD dwHandle, LPVOID lpData) {
    using Func = int(WINAPI*)(DWORD, LPVOID);
    if (!EnsureRealVersionDll() || !p_GetFileVersionInfoByHandle) return 0;
    return reinterpret_cast<Func>(p_GetFileVersionInfoByHandle)(dwHandle, lpData);
}

#else
#error "This proxy is designed for 64-bit builds only"
#endif
