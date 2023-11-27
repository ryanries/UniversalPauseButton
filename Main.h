#pragma once

#pragma warning(disable:4820) // padding in structures
#pragma warning(disable:4711) // automatic inline expansion

#include "Globals.h"

// The Lord's data types.
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;
typedef unsigned long long u64;
typedef signed char i8;
typedef signed short i16;
typedef signed long i32;
typedef signed long long i64;
typedef float f32;
typedef double f64;

// WARNING: Undocumented Win32 API functions!
typedef LONG(NTAPI* _NtSuspendProcess) (IN HANDLE ProcessHandle);
typedef LONG(NTAPI* _NtResumeProcess) (IN HANDLE ProcessHandle);
typedef HWND(NTAPI* _HungWindowFromGhostWindow) (IN HWND GhostWindowHandle);

_NtSuspendProcess NtSuspendProcess;
_NtResumeProcess NtResumeProcess;
_HungWindowFromGhostWindow HungWindowFromGhostWindow;

// Configurable registry settings.
typedef struct _CONFIG
{
  u32 Debug;
  u32 TrayIcon;
  u32 PauseKey;
  wchar_t ProcessNameToPause[128];
  wchar_t ProcessNameListToPause[MAX_PROCESSES * 128];
} CONFIG;

// Function declarations.
u32 LoadRegistrySettings(void);
void MsgBox(const wchar_t* Message, const wchar_t* Caption, u32 Flags, ...);
void DbgPrint(const wchar_t* Message, ...);
LRESULT CALLBACK SysTrayCallback(_In_ HWND Window, _In_ UINT Message, _In_ WPARAM WParam, _In_ LPARAM LParam);
void HandlePauseKeyPress(void);
i8 ResumeProcess(u32 Pid);
u32 PidLookup(wchar_t* ProcessName);
i8 PauseProcess(u32 Pid);
u32 PidOfForegroundProcess(void);
void TrimWhitespaces(wchar_t* str);
void FindRegistryPids(void);
void FindForegroundPid(void);
void TogglePause(void);
void ShowDebugConsole(void);
