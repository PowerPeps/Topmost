#include "Shared.h"
#include "resource.h"
#include <shellapi.h>
#include <string>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

static const UINT WM_TRAY = WM_APP + 1;
static const UINT IDM_AUTOSTART = 1;
static const UINT IDM_EXIT = 2;
static const wchar_t* RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* RUN_VALUE = L"Topmost";
static const wchar_t* HELPERS[] = { L"Topmost32.exe", L"Topmost64.exe" };

static HINSTANCE g_hinst = nullptr;
static HMODULE g_hook = nullptr;
static HookStopFn g_hookStop = nullptr;
static UINT g_taskbarCreated = 0;
static std::vector<PROCESS_INFORMATION> g_helpers;
static NOTIFYICONDATAW g_nid = { sizeof(g_nid) };

static const wchar_t* Tr(const wchar_t* fr, const wchar_t* en) {
    static const bool french = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_FRENCH;
    return french ? fr : en;
}

static std::wstring ModulePath() {
    std::vector<wchar_t> buf(MAX_PATH);
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
        if (n == 0) return std::wstring();
        if (n < buf.size() - 1) return std::wstring(buf.data(), n);
        if (buf.size() >= 32768) return std::wstring();
        buf.resize(buf.size() * 2);
    }
}

static std::wstring ModuleDir() {
    std::wstring s = ModulePath();
    size_t pos = s.find_last_of(L"\\/");
    return pos == std::wstring::npos ? std::wstring() : s.substr(0, pos + 1);
}

static bool IsHelperName(const std::wstring& path) {
    const std::wstring leaf = path.substr(path.find_last_of(L"\\/") + 1);
    for (const wchar_t* name : HELPERS) {
        if (CompareStringOrdinal(leaf.c_str(), -1, name, -1, TRUE) == CSTR_EQUAL) return true;
    }
    return false;
}

static bool StartHook() {
    const std::wstring path = ModuleDir() + TOPMOST_HOOK_DLL;
    g_hook = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!g_hook) return false;
    HookStartFn start = (HookStartFn)GetProcAddress(g_hook, "TopmostHookStart");
    g_hookStop = (HookStopFn)GetProcAddress(g_hook, "TopmostHookStop");
    if (start && g_hookStop && start()) return true;
    FreeLibrary(g_hook);
    g_hook = nullptr;
    g_hookStop = nullptr;
    return false;
}

static void StopHook() {
    if (g_hookStop) g_hookStop();
    if (g_hook) FreeLibrary(g_hook);
    g_hook = nullptr;
    g_hookStop = nullptr;
}

static BOOL CALLBACK AddProc(HWND hwnd, LPARAM msg) {
    if (GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_SYSMENU) SendNotifyMessageW(hwnd, (UINT)msg, 0, 0);
    return TRUE;
}

static BOOL CALLBACK RemoveProc(HWND hwnd, LPARAM msg) {
    if (GetPropW(hwnd, TOPMOST_PROP))
        SendMessageTimeoutW(hwnd, (UINT)msg, 0, 0, SMTO_ABORTIFHUNG | SMTO_NORMAL, 500, nullptr);
    return TRUE;
}

static void AddToExistingWindows() {
    EnumWindows(AddProc, RegisterWindowMessageW(TOPMOST_MSG_ADD));
}

static void RemoveFromAllWindows() {
    EnumWindows(RemoveProc, RegisterWindowMessageW(TOPMOST_MSG_REMOVE));
}

static void StartHelpers() {
    const std::wstring dir = ModuleDir();
    const std::wstring self = ModulePath();
    for (const wchar_t* name : HELPERS) {
        const std::wstring path = dir + name;
        if (CompareStringOrdinal(path.c_str(), -1, self.c_str(), -1, TRUE) == CSTR_EQUAL) continue;
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        std::wstring cmd = L"\"" + path + L"\" /helper " + std::to_wstring(GetCurrentProcessId());
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        if (CreateProcessW(path.c_str(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
            g_helpers.push_back(pi);
    }
}

static void StopHelpers() {
    for (PROCESS_INFORMATION& pi : g_helpers) {
        PostThreadMessageW(pi.dwThreadId, WM_QUIT, 0, 0);
        if (WaitForSingleObject(pi.hProcess, 3000) != WAIT_OBJECT_0) TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    g_helpers.clear();
}

static void DeleteStaleFiles() {
    const std::wstring dir = ModuleDir();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"*.old").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((dir + fd.cFileName).c_str());
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static std::wstring RunCommand() {
    return L"\"" + ModulePath() + L"\"";
}

static bool IsAutostart() {
    wchar_t buf[MAX_PATH + 3];
    DWORD cb = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, RUN_KEY, RUN_VALUE, RRF_RT_REG_SZ, nullptr, buf, &cb) != ERROR_SUCCESS)
        return false;
    return CompareStringOrdinal(buf, -1, RunCommand().c_str(), -1, TRUE) == CSTR_EQUAL;
}

static void SetAutostart(bool on) {
    if (on) {
        const std::wstring cmd = RunCommand();
        RegSetKeyValueW(HKEY_CURRENT_USER, RUN_KEY, RUN_VALUE, REG_SZ, cmd.c_str(),
                        (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteKeyValueW(HKEY_CURRENT_USER, RUN_KEY, RUN_VALUE);
    }
}

static void AddTrayIcon(HWND hwnd) {
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    g_nid.uCallbackMessage = WM_TRAY;
    if (!g_nid.hIcon)
        g_nid.hIcon = (HICON)LoadImageW(g_hinst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    lstrcpynW(g_nid.szTip, L"Topmost", ARRAYSIZE(g_nid.szTip));
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
}

static void ShowTrayMenu(HWND hwnd, int x, int y) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING | (IsAutostart() ? MF_CHECKED : MF_UNCHECKED), IDM_AUTOSTART,
                Tr(L"Démarrer avec Windows", L"Start with Windows"));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, Tr(L"Quitter", L"Exit"));
    SetForegroundWindow(hwnd);
    const UINT align = GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
    TrackPopupMenuEx(menu, TPM_RIGHTBUTTON | align, x, y, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

static void Shutdown() {
    RemoveFromAllWindows();
    StopHelpers();
    StopHook();
    if (g_nid.hWnd) Shell_NotifyIconW(NIM_DELETE, &g_nid);
    g_nid.hWnd = nullptr;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_TRAY:
        switch (LOWORD(lp)) {
        case WM_CONTEXTMENU:
        case NIN_SELECT:
        case NIN_KEYSELECT:
            ShowTrayMenu(hwnd, (short)LOWORD(wp), (short)HIWORD(wp));
            break;
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_AUTOSTART: SetAutostart(!IsAutostart()); break;
        case IDM_EXIT: DestroyWindow(hwnd); break;
        }
        return 0;
    case WM_DESTROY:
        Shutdown();
        PostQuitMessage(0);
        return 0;
    }
    if (msg == g_taskbarCreated && g_taskbarCreated) {
        AddTrayIcon(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static int RunMain() {
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\Topmost.Host");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mutex) CloseHandle(mutex);
        return 0;
    }
    DeleteStaleFiles();

    if (!StartHook()) {
        std::wstring text = Tr(L"Impossible d'installer le hook : ", L"Cannot install the hook: ");
        text += TOPMOST_HOOK_DLL;
        MessageBoxW(nullptr, text.c_str(), L"Topmost", MB_ICONERROR | MB_OK);
        CloseHandle(mutex);
        return 1;
    }
    StartHelpers();
    AddToExistingWindows();

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_hinst;
    wc.lpszClassName = TOPMOST_HOST_CLASS;
    RegisterClassExW(&wc);
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    HWND hwnd = CreateWindowExW(0, TOPMOST_HOST_CLASS, L"Topmost", WS_POPUP, 0, 0, 0, 0,
                                nullptr, nullptr, g_hinst, nullptr);
    if (!hwnd) {
        Shutdown();
        CloseHandle(mutex);
        return 1;
    }
    AddTrayIcon(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CloseHandle(mutex);
    return 0;
}

static int RunHelper(DWORD parentPid) {
    MSG msg;
    PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE);
    HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, parentPid);
    if (!parent) return 1;
    if (!StartHook()) {
        CloseHandle(parent);
        return 1;
    }
    AddToExistingWindows();

    bool quit = false;
    while (!quit) {
        if (MsgWaitForMultipleObjects(1, &parent, FALSE, INFINITE, QS_ALLINPUT) != WAIT_OBJECT_0 + 1) break;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { quit = true; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    StopHook();
    CloseHandle(parent);
    return 0;
}

static int QuitRunning() {
    HWND hwnd = FindWindowW(TOPMOST_HOST_CLASS, nullptr);
    if (!hwnd) return 0;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    HANDLE proc = OpenProcess(SYNCHRONIZE, FALSE, pid);
    PostMessageW(hwnd, WM_CLOSE, 0, 0);
    if (!proc) return 1;
    const DWORD r = WaitForSingleObject(proc, 10000);
    CloseHandle(proc);
    return r == WAIT_OBJECT_0 ? 0 : 1;
}

int WINAPI wWinMain(HINSTANCE hinst, HINSTANCE, PWSTR, int) {
    g_hinst = hinst;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int rc = 0;
    if (argv && argc >= 3 && lstrcmpiW(argv[1], L"/helper") == 0)
        rc = RunHelper(wcstoul(argv[2], nullptr, 10));
    else if (argv && argc >= 2 && lstrcmpiW(argv[1], L"/quit") == 0)
        rc = QuitRunning();
    else if (!IsHelperName(ModulePath()))
        rc = RunMain();
    if (argv) LocalFree(argv);
    return rc;
}
