// Main.cpp
// UniversalPauseButton
// Ryan Ries, 2015
// ryan@myotherpcisacloud.com
//
// Must compile in Unicode.

#include <Windows.h>
#include <Psapi.h>
#include <stdio.h>
#include "resource.h"

#define WM_TRAYICON (WM_USER + 1)

// WARNING: Undocumented Win32 API functions!
// Microsoft may change these at any time; they are not guaranteed to work on the next version of Windows.
typedef LONG(NTAPI* _NtSuspendProcess) (IN HANDLE ProcessHandle);
typedef LONG(NTAPI* _NtResumeProcess)  (IN HANDLE ProcessHandle);

_NtSuspendProcess NtSuspendProcess = (_NtSuspendProcess)GetProcAddress(GetModuleHandle(L"ntdll"), "NtSuspendProcess");
_NtResumeProcess NtResumeProcess = (_NtResumeProcess)GetProcAddress(GetModuleHandle(L"ntdll"), "NtResumeProcess");

NOTIFYICONDATA G_TrayNotifyIconData;
HANDLE         G_Mutex;

// NOTE(Ryan): This function returns true if the string ends with the specified Suffix/substring.
// Uses wide characters. Not case sensitive.
int StringEndsWithW(_In_ const wchar_t *Str, _In_ const wchar_t *Suffix)
{
	if (Str == NULL || Suffix == NULL)
	{
		return 0;
	}

	size_t str_len = wcslen(Str);
	size_t suffix_len = wcslen(Suffix);

	if (suffix_len > str_len)
	{
		return 0;
	}
	
	return 0 == _wcsnicmp(Str + str_len - suffix_len, Suffix, suffix_len);
}

// The WindowProc (callback) for WinMain's WindowClass.
// Basically the system tray does nothing except lets the user know that it's running.
// If the user clicks the tray icon it will ask if they want to exit the app.
LRESULT CALLBACK WindowClassCallback(_In_ HWND Window, _In_ UINT Message, _In_ WPARAM WParam, _In_ LPARAM LParam)
{
	LRESULT Result = 0;
	static BOOL QuitMessageBoxIsShowing = FALSE;

	switch (Message)
	{
		case WM_TRAYICON:
		{
			if (!QuitMessageBoxIsShowing && (LParam == WM_LBUTTONDOWN || LParam == WM_RBUTTONDOWN || LParam == WM_MBUTTONDOWN))
			{
				QuitMessageBoxIsShowing = TRUE;
				if (MessageBox(Window, L"Quit UniversalPauseButton?", L"Are you sure?", MB_YESNO | MB_ICONQUESTION | MB_SYSTEMMODAL) == IDYES)
				{
					Shell_NotifyIcon(NIM_DELETE, &G_TrayNotifyIconData);
					PostQuitMessage(0);
				}
				else
				{
					QuitMessageBoxIsShowing = FALSE;
				}
			}
			break;
		}
		default:
		{
			Result = DefWindowProc(Window, Message, WParam, LParam);
			break;
		}
	}
	return(Result);
}

// Entry point.
int CALLBACK WinMain(_In_ HINSTANCE Instance, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int)
{
	G_Mutex = CreateMutex(NULL, FALSE, L"UniversalPauseButton");
	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		MessageBox(NULL, L"An instance of the program is already running.", L"UniversalPauseButton Error", MB_OK | MB_ICONERROR);
		return(ERROR_ALREADY_EXISTS);
	}

	WNDCLASS SysTrayWindowClass = { 0 };

	SysTrayWindowClass.style         = CS_HREDRAW | CS_VREDRAW;
	SysTrayWindowClass.hInstance     = Instance;
	SysTrayWindowClass.lpszClassName = L"UniversalPauseButton_Systray_WindowClass";
	SysTrayWindowClass.hbrBackground = CreateSolidBrush(RGB(255, 0, 255));
	SysTrayWindowClass.lpfnWndProc   = WindowClassCallback;

	if (RegisterClass(&SysTrayWindowClass) == 0)
	{
		MessageBox(NULL, L"Failed to register WindowClass!", L"UniversalPauseButton Error", MB_OK | MB_ICONERROR);
		return(E_FAIL);
	}

	HWND SystrayWindow = CreateWindowEx(
		WS_EX_TOOLWINDOW,
		SysTrayWindowClass.lpszClassName,
		L"UniversalPauseButton_Systray_Window",
		WS_ICONIC,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		0,
		0,
		Instance,
		0);

	if (SystrayWindow == 0)
	{
		MessageBox(NULL, L"Failed to create SystrayWindow!", L"UniversalPauseButton Error", MB_OK | MB_ICONERROR);
		return(E_FAIL);
	}

	G_TrayNotifyIconData.cbSize           = sizeof(NOTIFYICONDATA);
	G_TrayNotifyIconData.hWnd             = SystrayWindow;
	G_TrayNotifyIconData.uID              = 1982;
	G_TrayNotifyIconData.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	G_TrayNotifyIconData.uCallbackMessage = WM_TRAYICON;

	wcscpy_s(G_TrayNotifyIconData.szTip, L"Universal Pause Button v1.0.1");

	G_TrayNotifyIconData.hIcon = (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON1), IMAGE_ICON, 0, 0, NULL);

	if (G_TrayNotifyIconData.hIcon == NULL)
	{
		MessageBox(NULL, L"Failed to load systray icon resource!", L"UniversalPauseButton Error", MB_OK | MB_ICONERROR);
		return(E_FAIL);
	}

	if (Shell_NotifyIcon(NIM_ADD, &G_TrayNotifyIconData) == FALSE)
	{
		MessageBox(NULL, L"Failed to register systray icon!", L"UniversalPauseButton Error", MB_OK | MB_ICONERROR);
		return(E_FAIL);
	}

	MSG        SysTrayWindowMessage                 = { 0 };
	static int PauseKeyWasDown                      = 0;
	DWORD      PreviouslySuspendedProcessID         = 0;	
	wchar_t    PreviouslySuspendedProcessText[256]  = { 0 };	
	HANDLE     ProcessHandle                        = 0;

	while (SysTrayWindowMessage.message != WM_QUIT)
	{
		while (PeekMessage(&SysTrayWindowMessage, SystrayWindow, 0, 0, PM_REMOVE))
		{
			DispatchMessage(&SysTrayWindowMessage);
		}

		int PauseKeyIsDown = GetAsyncKeyState(VK_PAUSE);

		if (PauseKeyIsDown && !PauseKeyWasDown)
		{
			HWND ForegroundWindow = GetForegroundWindow();
			if (!ForegroundWindow)
			{
				MessageBox(NULL, L"Unable to detect foreground window!", L"UniversalPauseButton Error", MB_OK | MB_ICONERROR);
				goto EndOfLoop;
			}

			DWORD ProcessID = 0;
			GetWindowThreadProcessId(ForegroundWindow, &ProcessID);
			if (ProcessID == 0)
			{
				MessageBox(NULL, L"Unable to get process ID of foreground window!", L"UniversalPauseButton Error", MB_OK | MB_ICONERROR);
				goto EndOfLoop;
			}
			
			ProcessHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, ProcessID);
			if (ProcessHandle == 0)
			{
				MessageBox(NULL, L"OpenProcess failed!", L"UniversalPauseButton Error", MB_OK | MB_ICONERROR);
				goto EndOfLoop;
			}

			if (PreviouslySuspendedProcessID == 0)
			{
				// I won't let you pause your shell. Nothing good will come of that.
				// Later I will get the user's shell from the registry, just to cover the 0.001% case
				// that the user has a custom shell.
				wchar_t ImageFileName[MAX_PATH] = { 0 };
				GetProcessImageFileName(ProcessHandle, ImageFileName, sizeof(ImageFileName) / sizeof(wchar_t));
				if (!StringEndsWithW(ImageFileName, L"explorer.exe"))
				{
					NtSuspendProcess(ProcessHandle);
					PreviouslySuspendedProcessID = ProcessID;
					GetWindowText(ForegroundWindow, PreviouslySuspendedProcessText, sizeof(PreviouslySuspendedProcessText) / sizeof(wchar_t));
				}
				else
				{
					MessageBox(NULL, L"You cannot pause your shell.", L"UniversalPauseButton Error", MB_OK | MB_ICONWARNING | MB_SYSTEMMODAL);
				}
			}
			else if (PreviouslySuspendedProcessID == ProcessID)
			{
				NtResumeProcess(ProcessHandle);
				PreviouslySuspendedProcessID = 0;
				memset(PreviouslySuspendedProcessText, 0, sizeof(PreviouslySuspendedProcessText));
			}
			else
			{
				// The user pressed the pause button while focused on another process than what was
				// originally paused and the first process is still paused.
				DWORD AllProcesses[2048] = { 0 };
				DWORD BytesReturned = 0;
				BOOL PreviouslySuspendedProcessIsStillRunning = FALSE;

				if (EnumProcesses(AllProcesses, sizeof(AllProcesses), &BytesReturned) != 0)
				{
					for (DWORD Counter = 0; Counter < (BytesReturned / sizeof(DWORD)); Counter++)
					{
						if ((AllProcesses[Counter] != 0) && AllProcesses[Counter] == PreviouslySuspendedProcessID)
						{
							PreviouslySuspendedProcessIsStillRunning = TRUE;										
						}
					}
					if (PreviouslySuspendedProcessIsStillRunning)
					{
						wchar_t MessageBoxBuffer[1024] = { 0 };
						_snwprintf_s(MessageBoxBuffer, sizeof(MessageBoxBuffer), L"You must first unpause %s (PID %d) before pausing another program.", PreviouslySuspendedProcessText, PreviouslySuspendedProcessID);
						MessageBox(ForegroundWindow, MessageBoxBuffer, L"Universal Pause Button", MB_OK | MB_ICONINFORMATION | MB_SYSTEMMODAL);
					}
					else
					{
						// The paused process is no more, so reset.
						PreviouslySuspendedProcessID = 0;									
						memset(PreviouslySuspendedProcessText, 0, sizeof(PreviouslySuspendedProcessText));
					}
				}
				else
				{
					MessageBox(NULL, L"EnumProcesses failed!", L"UniversalPauseButton Error", MB_OK | MB_ICONERROR);
				}
			}
		}
		
		// http://stackoverflow.com/questions/245742/examples-of-good-gotos-in-c-or-c
	
	EndOfLoop:
		if (ProcessHandle)
		{
			CloseHandle(ProcessHandle);
		}
		
		PauseKeyWasDown = PauseKeyIsDown;
		Sleep(10);
	}

	return(S_OK);
}