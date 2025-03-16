#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>
#include <stdbool.h>
#include <windows.h>
#include <iphlpapi.h>
#include "Server.h"
#include "resource.h"

//for debugging msgs
#include "Main.h"

#pragma comment(lib, "iphlpapi.lib")
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

//welcome web page
char* welcomePage = NULL;

int webPort;

// Global buffer for storing the Unicode long string (WCHAR array)
#define MAX_BUFFER_SIZE 256
WCHAR g_unicodeBuffer[MAX_BUFFER_SIZE];

// Default Unicode string to return on failure (empty string)
WCHAR g_defaultUnicodeString[] = L"";

// Function to convert ASCII to Unicode long (WCHAR) format using the global buffer.
// Returns:
//   Pointer to the converted Unicode string (g_unicodeBuffer) on success.
//   Pointer to the default Unicode string (g_defaultUnicodeString) on failure.
WCHAR* AsciiToUnicodeLong(const char* asciiString) {
    size_t asciiLength = 0;

    // Check if the input string is NULL
    if (asciiString == NULL) {
        return g_defaultUnicodeString;
    }

    // Calculate the length of the ASCII string safely
    while (asciiString[asciiLength] != '\0') {
        asciiLength++;
        if (asciiLength >= MAX_BUFFER_SIZE) { // Prevent buffer overflow during length calculation
            return g_defaultUnicodeString; // String is too long
        }
    }

    // Check if the ASCII string is too long to fit in the Unicode buffer
    if (asciiLength >= MAX_BUFFER_SIZE) {
        return g_defaultUnicodeString; // String is too long to convert safely
    }

    // Clear the global buffer before use
    memset(g_unicodeBuffer, 0, sizeof(WCHAR) * MAX_BUFFER_SIZE);

    // Perform the conversion using MultiByteToWideChar
    int result = MultiByteToWideChar(CP_ACP, 0, asciiString, -1, g_unicodeBuffer, (int)MAX_BUFFER_SIZE);

    if (result == 0) {
        // Conversion failed.
        DWORD error = GetLastError();
        fprintf(stderr, "Error in MultiByteToWideChar: %lu\n", error);
        return g_defaultUnicodeString;
    }

    return g_unicodeBuffer; // Return pointer to the global buffer
}


/**
 * Writes formatted text to a buffer, updating the buffer pointer and remaining character count.
 * Similar to sprintf, but prevents buffer overflow.
 *
 * @param buf Pointer to the buffer pointer. Will be updated to point to the end of written text.
 * @param rem_chars Pointer to the remaining character count. Will be decreased by chars written.
 * @param format Format string, similar to printf format.
 * @param ... Additional arguments for the format string.
 *
 * @return Number of characters written (not including null terminator), or -1 on error.
 */
static int write_to_buf(char** buf, int* rem_chars, const char* format, ...) {
    if (!buf || !*buf || !rem_chars || *rem_chars <= 0 || !format) {
        return -1;  // Invalid parameters
    }

    va_list args;
    va_start(args, format);

    // Use vsnprintf to safely format and determine length
    int chars_written = vsnprintf(NULL, 0, format, args);
    va_end(args);

    if (chars_written < 0) {
        return -1;  // Format error
    }

    // If not enough space, limit to available space (minus 1 for null terminator)
    int space_to_use = (chars_written < *rem_chars - 1) ? chars_written : *rem_chars - 1;

    // Reset va_list for actual writing
    va_start(args, format);
    int actual_written = vsnprintf(*buf, space_to_use + 1, format, args);
    va_end(args);

    if (actual_written < 0) {
        return -1;  // Formatting error
    }

    // Update the buffer pointer and remaining characters
    *buf += actual_written;
    *rem_chars -= actual_written;

    return actual_written;
}

#define MAX_CONNECTION_INFO_BUFFER 2048

char conn_info_buffer[MAX_CONNECTION_INFO_BUFFER];

char* get_connection_info(int port) {
    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        DbgPrint(L"WSAStartup failed\n");
        return NULL;
    }

    ULONG outBufLen = 15000;   // Initial buffer size
    PIP_ADAPTER_ADDRESSES pAddresses = (IP_ADAPTER_ADDRESSES*)malloc(outBufLen);
    if (pAddresses == NULL) {
        DbgPrint(L"Memory allocation failed\n");
        WSACleanup();
        return NULL;
    }

    // Make the first call to GetAdaptersAddresses
    ULONG result = GetAdaptersAddresses(
        AF_UNSPEC,              // Both IPv4 and IPv6
        GAA_FLAG_INCLUDE_PREFIX, // Include prefixes
        NULL,
        pAddresses,
        &outBufLen
    );

    // If buffer was too small, reallocate with the required size
    if (result == ERROR_BUFFER_OVERFLOW) {
        free(pAddresses);
        pAddresses = (IP_ADAPTER_ADDRESSES*)malloc(outBufLen);
        if (pAddresses == NULL) {
            DbgPrint(L"Memory allocation failed\n");
            WSACleanup();
            return NULL;
        }

        result = GetAdaptersAddresses(
            AF_UNSPEC,
            GAA_FLAG_INCLUDE_PREFIX,
            NULL,
            pAddresses,
            &outBufLen
        );
    }

    if (result != NO_ERROR) {
        DbgPrint(L"GetAdaptersAddresses failed with error code %lu\n", result);
        free(pAddresses);
        WSACleanup();
        return NULL;
    }

    int rem = MAX_CONNECTION_INFO_BUFFER;
    char* buf = conn_info_buffer;
    write_to_buf(&buf, &rem, "%d", port);

    // Iterate through all adapters
    PIP_ADAPTER_ADDRESSES pCurrAddresses = pAddresses;
    while (pCurrAddresses) {
        // Skip virtual adapters and adapters that are not operational
        if (pCurrAddresses->IfType == IF_TYPE_SOFTWARE_LOOPBACK ||   // Loopback interface
            pCurrAddresses->OperStatus != IfOperStatusUp ||          // Interface not up
            //pCurrAddresses->ConnectionType == NET_IF_CONNECTION_DEDICATED || // Check for certain virtual types
            wcsstr(pCurrAddresses->Description, L"VPN") ||           // VPN adapters
            wcsstr(pCurrAddresses->Description, L"Hyper-V") ||       // Hyper-V adapters
            wcsstr(pCurrAddresses->Description, L"VMware") ||        // VMware adapters
            wcsstr(pCurrAddresses->Description, L"VirtualBox")) {    // VirtualBox adapters

            DbgPrint(L"Skipping adapter: %s\n", pCurrAddresses->FriendlyName);
            pCurrAddresses = pCurrAddresses->Next;
            continue;
        }

        DbgPrint(L"\nAdapter Name: %s\n", pCurrAddresses->FriendlyName);
        DbgPrint(L"Description: %s\n", pCurrAddresses->Description);

        bool not_added_ip4 = true;
        for (int i = 0; i < 2; i++) {
            bool is_adding_ip4 = (i == 0);

            // Print all unicast addresses (IPv4 and IPv6)
            PIP_ADAPTER_UNICAST_ADDRESS pUnicast = pCurrAddresses->FirstUnicastAddress;
            while (pUnicast != NULL) {
                char ipStr[INET6_ADDRSTRLEN] = { 0 };

                // Get the address family
                if (is_adding_ip4 && pUnicast->Address.lpSockaddr->sa_family == AF_INET) {
                    // IPv4
                    struct sockaddr_in* sa_in = (struct sockaddr_in*)pUnicast->Address.lpSockaddr;
                    inet_ntop(AF_INET, &(sa_in->sin_addr), ipStr, INET_ADDRSTRLEN);

                    // Skip localhost (127.0.0.1) and APIPA addresses (169.254.x.x)
                    if (strncmp(ipStr, "127.", 4) != 0 && strncmp(ipStr, "169.254.", 8) != 0) {
                        DbgPrint(L"  IPv4 Address: %s\n", AsciiToUnicodeLong(ipStr));
                        write_to_buf(&buf, &rem, ",%s", ipStr);

                        not_added_ip4 = false;
                    }
                    else {
                        DbgPrint(L"  Skipping IPv4 Address: %s\n", AsciiToUnicodeLong(ipStr));
                    }
                }
                else if (!is_adding_ip4 && pUnicast->Address.lpSockaddr->sa_family == AF_INET6 && not_added_ip4) {
                    // IPv6
                    struct sockaddr_in6* sa_in6 = (struct sockaddr_in6*)pUnicast->Address.lpSockaddr;
                    inet_ntop(AF_INET6, &(sa_in6->sin6_addr), ipStr, INET6_ADDRSTRLEN);

                    // Skip localhost (::1) and link-local addresses (fe80::)
                    if (strcmp(ipStr, "::1") != 0 && strncmp(ipStr, "fe80:", 5) != 0) {
                        DbgPrint(L"  IPv6 Address: %s\n", AsciiToUnicodeLong(ipStr));
                        write_to_buf(&buf, &rem, ",[%s]", ipStr);
                    }
                    else {
                        DbgPrint(L"  Skipping IPv6 Address: %s\n", AsciiToUnicodeLong(ipStr));
                    }
                }

                pUnicast = pUnicast->Next;
            }

            pCurrAddresses = pCurrAddresses->Next;
        }
    }

    free(pAddresses);
    WSACleanup();
    return conn_info_buffer;
}


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
            else if (strstr(buffer, "GET /connection-info") != NULL) {
                send_str(clientSocket, plainResponseHeader);
                isPaused = !isPaused;

                char *connection_info = get_connection_info(webPort);

                if (connection_info == NULL) {
                    DbgPrint(L"Unable to get connection_info\n");
                    send_str(clientSocket, "ERROR");
                }
                else
                    send_str(clientSocket, connection_info);
            }
            else if (strstr(buffer, "GET /welcome.htm") != NULL) {
                send_str(clientSocket, htmlResponseHeader);
                send_str(clientSocket, welcomePage);
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
    if (!(mainPage = read_html_page(IDR_HTML1)))
        return 1;

    if (!(welcomePage = read_html_page(IDR_HTML2)))
        return 1;

    return 0;
    
}

int serve_start(int port) {
    webPort = port;

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

#include <windows.h>
#include <shellapi.h>
#include <stdio.h>

/**
 * Opens the default web browser and navigates to http://localhost:<port>/welcome
 *
 * @param port The port number to connect to
 * @return 0 on success, non-zero on failure
 */
int openWelcomePageInBrowser() {
    char url[100];

    // Format the URL with the provided port
    snprintf(url, sizeof(url), "http://localhost:%d/welcome.htm", webPort);

    // ShellExecute returns a value greater than 32 if successful
    HINSTANCE result = ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);

    // Check if operation was successful
    if ((int)result <= 32) {
        // Error occurred
        switch ((int)result) {
        case 0:
            DbgPrint(L"openWelcomePageInBrowser Error: The operating system is out of memory or resources.\n");
            break;
        case ERROR_FILE_NOT_FOUND:
            DbgPrint(L"openWelcomePageInBrowser Error: The specified file was not found.\n");
            break;
        case ERROR_PATH_NOT_FOUND:
            DbgPrint(L"openWelcomePageInBrowser Error: The specified path was not found.\n");
            break;
        case ERROR_BAD_FORMAT:
            DbgPrint(L"openWelcomePageInBrowser Error: The .exe file is invalid.\n");
            break;
        case SE_ERR_ACCESSDENIED:
            DbgPrint(L"openWelcomePageInBrowser Error: Access denied.\n");
            break;
        case SE_ERR_ASSOCINCOMPLETE:
            DbgPrint(L"openWelcomePageInBrowser Error: The file name association is incomplete or invalid.\n");
            break;
        case SE_ERR_NOASSOC:
            DbgPrint(L"openWelcomePageInBrowser Error: No application is associated with the given file type.\n");
            break;
        default:
            DbgPrint(L"openWelcomePageInBrowser Error: Unknown error occurred (code: %d).\n", (int)result);
            break;
        }
        return 1;
    }

    return 0;
}
