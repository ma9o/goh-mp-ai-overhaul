// Drive the local Vehicle Control menu in the verified game directory.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static DWORD game_pid;
static HWND game_window;
static HWND exception_window;

static DWORD find_game(void) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W entry = {0};
    entry.dwSize = sizeof(entry);
    // Wine may report C: for a game started through a Z: alias. Compare file IDs.
    BY_HANDLE_FILE_INFORMATION ours, theirs;
    HANDLE exe = CreateFileW(L"call_to_arms.exe", 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                             OPEN_EXISTING, 0, NULL);
    DWORD found = 0;
    if (exe != INVALID_HANDLE_VALUE && GetFileInformationByHandle(exe, &ours) && Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"call_to_arms.exe") != 0) continue;
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
            WCHAR path[4096];
            DWORD length = sizeof(path) / sizeof(path[0]);
            if (process && QueryFullProcessImageNameW(process, 0, path, &length)) {
                HANDLE file = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                          OPEN_EXISTING, 0, NULL);
                if (file != INVALID_HANDLE_VALUE) {
                    if (GetFileInformationByHandle(file, &theirs) &&
                        ours.dwVolumeSerialNumber == theirs.dwVolumeSerialNumber &&
                        ours.nFileIndexHigh == theirs.nFileIndexHigh && ours.nFileIndexLow == theirs.nFileIndexLow)
                        found = entry.th32ProcessID;
                    CloseHandle(file);
                }
            }
            if (process) CloseHandle(process);
        } while (!found && Process32NextW(snapshot, &entry));
    }
    if (exe != INVALID_HANDLE_VALUE) CloseHandle(exe);
    CloseHandle(snapshot);
    return found;
}

static BOOL CALLBACK windows(HWND w, LPARAM unused) {
    (void)unused;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    if (pid != game_pid || !IsWindowVisible(w)) return TRUE;
    WCHAR title[256], cls[64];
    GetWindowTextW(w, title, 256);
    GetClassNameW(w, cls, 64);
    if (!_wcsicmp(title, L"CALL TO ARMS - GATES OF HELL")) game_window = w;
    if (!_wcsicmp(cls, L"#32770") && !_wcsicmp(title, L"Exception")) exception_window = w;
    return TRUE;
}

static int key_input(WORD code) {
    INPUT event = {0};
    event.type = INPUT_KEYBOARD;
    event.ki.wVk = code;
    if (!SendInput(1, &event, sizeof(event))) return 1;
    Sleep(65);
    event.ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(1, &event, sizeof(event)) == 1 ? 0 : 1;
}

static int coordinate(const char* arg, long limit, long* value) {
    char* end;
    *value = strtol(arg, &end, 10);
    return end != arg && !*end && *value >= 0 && *value < limit;
}

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    game_pid = find_game();
    if (!game_pid) return 3;
    if (!strcmp(argv[1], "present")) return 0;
    EnumWindows(windows, 0);
    if (!strcmp(argv[1], "dismiss"))
        return exception_window && PostMessageW(exception_window, WM_COMMAND, IDOK, 0) ? 0 : 3;
    if (!game_window) return 3;
    if (!strcmp(argv[1], "close")) return PostMessageW(game_window, WM_CLOSE, 0, 0) ? 0 : 1;
    SetForegroundWindow(game_window);
    Sleep(100);
    if (GetForegroundWindow() != game_window) return 4;
    if (!strcmp(argv[1], "space")) return key_input(VK_SPACE);
    if (!strcmp(argv[1], "esc")) return key_input(VK_ESCAPE);
    long x, y;
    if (argc != 4 || strcmp(argv[1], "click") || !coordinate(argv[2], 900, &x) || !coordinate(argv[3], 584, &y))
        return 2;
    RECT r;
    if (!GetClientRect(game_window, &r) || r.right <= 0 || r.bottom <= 0) return 1;
    POINT p = {x * r.right / 900, y * r.bottom / 584};
    if (!ClientToScreen(game_window, &p) || !SetCursorPos(p.x, p.y)) return 1;
    Sleep(100);
    if (GetForegroundWindow() != game_window) return 4;
    INPUT event = {0};
    event.type = INPUT_MOUSE;
    event.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    if (!SendInput(1, &event, sizeof(event))) return 1;
    Sleep(70);
    event.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    return SendInput(1, &event, sizeof(event)) == 1 ? 0 : 1;
}
