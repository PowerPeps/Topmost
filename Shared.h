#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define TOPMOST_PROP        L"Topmost.Menu"
#define TOPMOST_MSG_ADD     L"Topmost.Add"
#define TOPMOST_MSG_REMOVE  L"Topmost.Remove"
#define TOPMOST_HOST_CLASS  L"Topmost.Host"

#if defined(_M_ARM64)
#define TOPMOST_HOOK_DLL    L"TopmostHookArm64.dll"
#elif defined(_M_X64)
#define TOPMOST_HOOK_DLL    L"TopmostHook64.dll"
#else
#define TOPMOST_HOOK_DLL    L"TopmostHook32.dll"
#endif

typedef BOOL (WINAPI* HookStartFn)();
typedef void (WINAPI* HookStopFn)();
