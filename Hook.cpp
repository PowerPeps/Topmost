#include "Shared.h"

#pragma comment(lib, "user32.lib")

static const UINT IDM_TOPMOST = 0x5A70;
static const UINT IDM_SEPARATOR = 0x5A80;
static const UINT WM_POPUPSYSTEMMENU = 0x0313;  // undocumented: sent by the taskbar to show a window menu

static HINSTANCE g_hinst = nullptr;
static HHOOK g_callWndProc = nullptr;
static HHOOK g_getMessage = nullptr;

static UINT AddMessage() {
    static const UINT msg = RegisterWindowMessageW(TOPMOST_MSG_ADD);
    return msg;
}

static UINT RemoveMessage() {
    static const UINT msg = RegisterWindowMessageW(TOPMOST_MSG_REMOVE);
    return msg;
}

static const wchar_t* Label() {
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_FRENCH ? L"Garder au premier plan"
                                                                    : L"Always on top";
}

static bool IsEligible(HWND hwnd) {
    if (!hwnd || GetWindowThreadProcessId(hwnd, nullptr) != GetCurrentThreadId()) return false;
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    return (style & WS_SYSMENU) && !(style & WS_CHILD);
}

static bool IsTopmost(HWND hwnd) {
    return (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
}

static bool IsCommand(WPARAM wp) {
    return (wp & 0xFFF0) == IDM_TOPMOST;
}

static bool HasItem(HMENU menu) {
    return GetMenuState(menu, IDM_TOPMOST, MF_BYCOMMAND) != (UINT)-1;
}

static void InsertItem(HMENU menu) {
    if (HasItem(menu)) return;
    const int count = GetMenuItemCount(menu);
    if (count < 0) return;

    int pos = count;
    for (int i = 0; i < count; ++i) {
        if (GetMenuItemID(menu, i) == SC_CLOSE) { pos = i; break; }
    }
    if (pos < count && pos > 0 && (GetMenuState(menu, pos - 1, MF_BYPOSITION) & MF_SEPARATOR)) --pos;

    MENUITEMINFOW mii = { sizeof(mii) };
    if (pos > 0) {
        mii.fMask = MIIM_FTYPE | MIIM_ID;
        mii.fType = MFT_SEPARATOR;
        mii.wID = IDM_SEPARATOR;
        InsertMenuItemW(menu, pos++, TRUE, &mii);
    }
    mii.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STRING;
    mii.fType = MFT_STRING;
    mii.wID = IDM_TOPMOST;
    mii.dwTypeData = const_cast<wchar_t*>(Label());
    InsertMenuItemW(menu, pos, TRUE, &mii);
}

static void EnsureItem(HWND hwnd) {
    hwnd = GetAncestor(hwnd, GA_ROOT);
    if (!IsEligible(hwnd) || GetPropW(hwnd, TOPMOST_PROP)) return;
    HMENU menu = GetSystemMenu(hwnd, FALSE);
    if (!menu) return;
    InsertItem(menu);
    SetPropW(hwnd, TOPMOST_PROP, (HANDLE)1);
}

static void RemoveItem(HWND hwnd) {
    if (!RemovePropW(hwnd, TOPMOST_PROP)) return;
    HMENU menu = GetSystemMenu(hwnd, FALSE);
    if (!menu) return;
    DeleteMenu(menu, IDM_TOPMOST, MF_BYCOMMAND);
    DeleteMenu(menu, IDM_SEPARATOR, MF_BYCOMMAND);
}

static void PrepareMenu(HWND hwnd, HMENU menu) {
    if (!IsEligible(hwnd)) return;
    if (!HasItem(menu)) {
        // Only the window's own copy may be edited: the default window menu is shared.
        if (menu != GetSystemMenu(hwnd, FALSE)) return;
        InsertItem(menu);
        SetPropW(hwnd, TOPMOST_PROP, (HANDLE)1);
    }
    CheckMenuItem(menu, IDM_TOPMOST, MF_BYCOMMAND | (IsTopmost(hwnd) ? MF_CHECKED : MF_UNCHECKED));
}

static void Toggle(HWND hwnd) {
    SetWindowPos(hwnd, IsTopmost(hwnd) ? HWND_NOTOPMOST : HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

static LRESULT CALLBACK CallWndProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        const CWPSTRUCT* m = reinterpret_cast<const CWPSTRUCT*>(lp);
        switch (m->message) {
        case WM_ACTIVATE:
        case WM_NCACTIVATE:
        case WM_INITMENU:
        case WM_CONTEXTMENU:
        case WM_POPUPSYSTEMMENU:
            EnsureItem(m->hwnd);
            break;
        case WM_INITMENUPOPUP:
            if (HIWORD(m->lParam)) PrepareMenu(m->hwnd, reinterpret_cast<HMENU>(m->wParam));
            break;
        case WM_SYSCOMMAND:
            if (IsCommand(m->wParam) && IsEligible(m->hwnd)) Toggle(m->hwnd);
            break;
        case WM_NCDESTROY:
            RemovePropW(m->hwnd, TOPMOST_PROP);
            break;
        default:
            if (m->message >= 0xC000) {
                if (m->message == AddMessage()) EnsureItem(m->hwnd);
                else if (m->message == RemoveMessage()) RemoveItem(m->hwnd);
            }
            break;
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

static LRESULT CALLBACK GetMsgProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION) {
        MSG* m = reinterpret_cast<MSG*>(lp);
        switch (m->message) {
        case WM_NCLBUTTONDOWN:
        case WM_NCRBUTTONDOWN:
        case WM_SYSKEYDOWN:
        case WM_POPUPSYSTEMMENU:
            EnsureItem(m->hwnd);
            break;
        case WM_SYSCOMMAND:
            if (wp == PM_REMOVE && IsCommand(m->wParam) && IsEligible(m->hwnd)) {
                Toggle(m->hwnd);
                m->message = WM_NULL;
            }
            break;
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

extern "C" void WINAPI TopmostHookStop() {
    if (g_getMessage) UnhookWindowsHookEx(g_getMessage);
    if (g_callWndProc) UnhookWindowsHookEx(g_callWndProc);
    g_getMessage = g_callWndProc = nullptr;
}

extern "C" BOOL WINAPI TopmostHookStart() {
    if (g_callWndProc) return TRUE;
    g_callWndProc = SetWindowsHookExW(WH_CALLWNDPROC, CallWndProc, g_hinst, 0);
    g_getMessage = SetWindowsHookExW(WH_GETMESSAGE, GetMsgProc, g_hinst, 0);
    if (g_callWndProc && g_getMessage) return TRUE;
    TopmostHookStop();
    return FALSE;
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hinst = hinst;
        DisableThreadLibraryCalls(hinst);
    }
    return TRUE;
}
