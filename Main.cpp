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

typedef LONG(NTAPI* _NtSuspendProcess) (IN HANDLE ProcessHandle);
typedef LONG(NTAPI* _NtResumeProcess)  (IN HANDLE ProcessHandle);

_NtSuspendProcess NtSuspendProcess = (_NtSuspendProcess)GetProcAddress(GetModuleHandle(L"ntdll"), "NtSuspendProcess");
_NtResumeProcess NtResumeProcess = (_NtResumeProcess)GetProcAddress(GetModuleHandle(L"ntdll"), "NtResumeProcess");

NOTIFYICONDATA G_TrayNotifyIconData;
HANDLE         G_Mutex;


// The WindowProc (callback) for WinMain's WindowClass.
// Basically the system tray does nothing except lets the user know that it's running.
// If the user clicks the tray icon it will ask if they want to exit the app.
LRESULT CALLBACK WindowClassCallback(_In_ HWND Window, _In_ UINT Message, _In_ WPARAM WParam, _In_ LPARAM LParam)
{
	LRESULT Result = 0;

	switch (Message)
	{
		case WM_TRAYICON:
		{
			if (LParam == WM_LBUTTONDOWN || LParam == WM_RBUTTONDOWN)
			{
				if (MessageBox(Window, L"Quit UniversalPauseButton?", L"Are you sure?", MB_YESNO | MB_ICONQUESTION) == IDYES)
				{
					Shell_NotifyIcon(NIM_DELETE, &G_TrayNotifyIconData);
					PostQuitMessage(0);
				}
			}
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

	wcscpy_s(G_TrayNotifyIconData.szTip, L"Universal Pause Button v1.0");

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
			if (ForegroundWindow)
			{
				DWORD ProcessID = 0;
				GetWindowThreadProcessId(ForegroundWindow, &ProcessID);
				if (ProcessID != 0)
				{
					HANDLE ProcessHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, ProcessID);
					if (ProcessHandle != 0)
					{
						if (PreviouslySuspendedProcessID == 0)
						{
							NtSuspendProcess(ProcessHandle);
							PreviouslySuspendedProcessID = ProcessID;
							GetWindowText(ForegroundWindow, PreviouslySuspendedProcessText, sizeof(PreviouslySuspendedProcessText) / sizeof(wchar_t));
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
						CloseHandle(ProcessHandle);
					}
					else
					{
						MessageBox(NULL, L"OpenProcess failed!", L"UniversalPauseButton Error", MB_OK | MB_ICONERROR);
					}
				}
				else
				{
					MessageBox(NULL, L"Unable to get process ID of foreground window!", L"UniversalPauseButton Error", MB_OK | MB_ICONERROR);
				}
			}
			else
			{
				MessageBox(NULL, L"Unable to detect foreground window!", L"UniversalPauseButton Error", MB_OK | MB_ICONERROR);
			}
		}

		PauseKeyWasDown = PauseKeyIsDown;

		Sleep(10);
	}

	return(S_OK);
}