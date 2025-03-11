#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>
#include <stdbool.h>
#include <windows.h>
#include "Server.h"
#include "resource.h"

//for debugging msgs
#include "Main.h"

#pragma comment(lib, "ws2_32.lib")

#define BUFFER_SIZE 4096

// Global variables
bool serverRunning = true;
HANDLE serverThreadHandle;

static SOCKET serverSocket = INVALID_SOCKET;

// Response when app is paused
const char* pauseResponse = "1";

// Response when app is unpaused
const char* unpauseResponse = "0";

const char* htmlResponseHeader =
"HTTP/1.1 200 OK\r\n"
"Content-Type: text/html\r\n"
"Connection: close\r\n"
"\r\n";

const char* plainResponseHeader =
"HTTP/1.1 200 OK\r\n"
"Content-Type: text/plain\r\n"
"Connection: close\r\n"
"\r\n";

//main web page
char* mainPage = NULL;


static void send_str(SOCKET clientSocket, const char* txt)
{
    send(clientSocket, txt, (int)strlen(txt), 0);
}

//#define send_str(clientSocket, txt) {send(clientSocket, txt, (int)strlen(txt), 0);}

//run this in main loop to accept requests from a client
//isPaused -- status of application, true if already paused
//return -- true if paused status *changed* (ie. went from unpause to pause or pause to unpause
bool serve_request(bool isPaused) {
    SOCKET clientSocket;
    struct sockaddr_in clientAddr;
    int clientAddrSize = sizeof(clientAddr);

    bool pause_status_changed = false;

    // Accept a client socket (non-blocking)
    clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientAddrSize);

    if (clientSocket != INVALID_SOCKET) {
        char buffer[BUFFER_SIZE];
        int bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE - 1, 0);

        if (bytesReceived > 0) {
            buffer[bytesReceived] = '\0';

            // Check if this is a pause command
            if (strstr(buffer, "POST /pause") != NULL) {
                send_str(clientSocket, plainResponseHeader);
                isPaused = !isPaused;

                send_str(clientSocket, isPaused ? pauseResponse : unpauseResponse);

                pause_status_changed = true;
            }
            else {
                send_str(clientSocket, htmlResponseHeader);
                send_str(clientSocket, mainPage);
            }
        }

        closesocket(clientSocket);
    }
    else {
        // Check if there was a real error or just no connection available
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) {
            DbgPrint(L"Accept failed: %d\n", err);
        }
    }

    return pause_status_changed;
}

//returns an html page from the rc file
// id - (IDR_HTML1,IDR_HTML2...)
static char* read_html_page(int id) {
    HMODULE hModule = GetModuleHandle(NULL);

    // Load the resource
    HRSRC hResource = FindResource(hModule, MAKEINTRESOURCE(id), RT_HTML);
    if (hResource == NULL) {
        DbgPrint(L"Failed to find resource. Error: %d\n", GetLastError());
        return NULL;
    }

    // Load the resource data
    HGLOBAL hGlobal = LoadResource(hModule, hResource);
    if (hGlobal == NULL) {
        DbgPrint(L"Failed to load resource. Error: %d\n", GetLastError());
        return NULL;
    }

    // Get a pointer to the resource data
    // This is read-only access
    const void* pData = LockResource(hGlobal);
    if (pData == NULL) {
        DbgPrint(L"Failed to lock resource. Error: %d\n", GetLastError());
        return NULL;
    }

    // Get the size of the resource
    DWORD dwSize = SizeofResource(hModule, hResource);

    char* cData = malloc(dwSize + sizeof(char)); //we want to null terminate the html page, so we need an extra char

    if (cData == NULL) {
        DbgPrint(L"Failed to allocate memory, wanted %d bytes", dwSize + sizeof(char));
        return NULL;
    }

    memcpy(cData, pData, dwSize);
    cData[dwSize] = '\0';

    FreeResource(hGlobal);


    return cData;
}


static int read_resources(void) {
    if(!(mainPage = read_html_page(IDR_HTML1)))
        return 1;

    return 0;
    
}

int serve_start(int port) {

    if (read_resources()) {
        DbgPrint(L"Read resources failed.\n");
        return 1;
    }

    WSADATA wsaData;
    struct addrinfo* result = NULL, hints;
    int iResult;

    // Initialize Winsock
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        DbgPrint(L"WSAStartup failed: %d\n", iResult);
        return 1;
    }

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    char s_port[10];

    sprintf_s(s_port, 10, "%d", port);

    // Resolve the server address and port
    iResult = getaddrinfo(NULL, s_port, &hints, &result);
    if (iResult != 0) {
        DbgPrint(L"getaddrinfo failed: %d\n", iResult);
        WSACleanup();
        return 1;
    }

    // Create a socket for the server
    serverSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (serverSocket == INVALID_SOCKET) {
        DbgPrint(L"Error creating socket: %d\n", WSAGetLastError());
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }

    // Bind the socket
    iResult = bind(serverSocket, result->ai_addr, (int)result->ai_addrlen);
    if (iResult == SOCKET_ERROR) {
        DbgPrint(L"Bind failed: %d\n", WSAGetLastError());
        closesocket(serverSocket);
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }

    freeaddrinfo(result);

    // Start listening for client connections
    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        DbgPrint(L"Listen failed: %d\n", WSAGetLastError());
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    DbgPrint(L"Web server started. Open your browser and navigate to http://localhost:%d\n", port);

    // Set up non-blocking socket mode for accept
    u_long mode = 1;  // 1 = non-blocking
    ioctlsocket(serverSocket, FIONBIO, &mode);

    return 0;
}

int serve_stop(void) {
    if (serverRunning) {
        // Shutdown the server thread
        serverRunning = false;

        // Wait for server thread to finish (with timeout)
        WaitForSingleObject(serverThreadHandle, 5000);
        CloseHandle(serverThreadHandle);

        // Clean up
        closesocket(serverSocket);
        WSACleanup();
        DbgPrint(L"Web server thread exiting cleanly\n");
        return 0;
    }

    return 0;
}
