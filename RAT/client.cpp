#ifdef _WIN32
// ConPTY needs a recent Windows API. 0x0A00 = Windows 10.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
// windows.h pulls in the old winsock.h by default; we don't need it.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <iostream>
#include <thread>
#include <string>

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

typedef HRESULT (WINAPI *CreatePseudoConsoleFn)(COORD, HANDLE, HANDLE, DWORD, void**);
typedef void    (WINAPI *ClosePseudoConsoleFn)(void*);

// Zelfde truc als in server.cpp: de juiste headers per besturingssysteem.
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    // Alleen MSVC kent deze regel; bij MinGW gebruik je -lws2_32.
    #ifdef _MSC_VER
        #pragma comment(lib, "ws2_32.lib")
    #endif
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    typedef int SOCKET;
    const int INVALID_SOCKET = -1;
    const int SOCKET_ERROR   = -1;
    #define closesocket close
#endif

#include "pipes.h"
#include "net.h"

// Welke shell starten we? Op Windows cmd.exe, op Linux /bin/sh.
#ifdef _WIN32
const std::string SHELL = "cmd.exe";
#else
const std::string SHELL = "/bin/sh";
#endif

int main() {
#ifdef _WIN32
    // Winsock opstarten (alleen Windows), zie uitleg in server.cpp.
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup mislukt\n";
        return 1;
    }
#endif

    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    auto createConsole = reinterpret_cast<CreatePseudoConsoleFn>(
        reinterpret_cast<void*>(GetProcAddress(kernel32, "CreatePseudoConsole")));
    auto closeConsole  = reinterpret_cast<ClosePseudoConsoleFn>(
        reinterpret_cast<void*>(GetProcAddress(kernel32, "ClosePseudoConsole")));
    if (!createConsole || !closeConsole) {
        std::cerr << "This Windows has no ConPTY (needs Windows 10 1809+).\r\n";
        return 1;
    }

    HANDLE hOurOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hOurIn  = GetStdHandle(STD_INPUT_HANDLE);
    DWORD oldOutMode = 0;
    GetConsoleMode(hOurOut, &oldOutMode);
    SetConsoleMode(hOurOut, oldOutMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);  

    HANDLE toConsole = NULL, consoleReads = NULL;    // pipe 1 (our input)
    HANDLE fromConsole = NULL, consoleWrites = NULL; // pipe 2 (console output)
    if (!CreatePipe(&consoleReads, &toConsole, NULL, 0) ||
        !CreatePipe(&fromConsole, &consoleWrites, NULL, 0)) {
        std::cerr << "Could not create the pipes.\r\n";
        return 1;
    }

    // -------------------------------------------------------------------------
    // 3. Create the pseudo-console itself. We give it the ends of the pipes it
    //    should use (read = our input, write = its output), plus a size in
    //    columns x rows.
    // -------------------------------------------------------------------------
    void* hConsole = NULL;
    COORD size = { 100, 30 };   // 100 columns wide, 30 rows high
    HRESULT hr = createConsole(size, consoleReads, consoleWrites, 0, &hConsole);
    // The console now has its own copy of those two handles; we no longer need
    // them ourselves. Not closing them would mean the output pipe never reports
    // "end" when cmd stops.
    CloseHandle(consoleReads);
    CloseHandle(consoleWrites);
    if (FAILED(hr)) {
        std::cerr << "CreatePseudoConsole failed (do you have Windows 10 1809+?).\r\n";
        return 1;
    }

    // -------------------------------------------------------------------------
    // 4. Start cmd.exe and attach IT to this pseudo-console. That last part is
    //    done through an "attribute list" at startup: we put the
    //    PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE attribute in it with our console.
    // -------------------------------------------------------------------------
    STARTUPINFOEXA si{};
    si.StartupInfo.cb = sizeof(STARTUPINFOEXA);

    // First ask how much memory the attribute list needs (for 1 attribute),
    // then reserve space and initialize the list.
    SIZE_T needed = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &needed);
    si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
        HeapAlloc(GetProcessHeap(), 0, needed));
    if (!si.lpAttributeList ||
        !InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &needed)) {
        std::cerr << "Could not create the attribute list.\r\n";
        closeConsole(hConsole);
        return 1;
    }
    if (!UpdateProcThreadAttribute(si.lpAttributeList, 0,
                                   PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   hConsole, sizeof(hConsole), NULL, NULL)) {
        std::cerr << "Could not attach the console to the process.\r\n";
        DeleteProcThreadAttributeList(si.lpAttributeList);
        HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
        closeConsole(hConsole);
        return 1;
    }

    PROCESS_INFORMATION pi{};
    char command[] = "cmd.exe";   // CreateProcess may modify this buffer
    BOOL ok = CreateProcessA(
        NULL, command, NULL, NULL,
        FALSE,                        // inherit handles: not needed with ConPTY
        EXTENDED_STARTUPINFO_PRESENT, // we use the extended startup info
        NULL, NULL, &si.StartupInfo, &pi);
    if (!ok) {
        std::cerr << "Could not start cmd.exe.\r\n";
        DeleteProcThreadAttributeList(si.lpAttributeList);
        HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
        closeConsole(hConsole);
        return 1;
    }  
  
    // 1. Maak een socket en verbind met de server op 127.0.0.1:3000.
    SOCKET verbinding = socket(AF_INET, SOCK_STREAM, 0);
    if (verbinding == INVALID_SOCKET) { std::cerr << "socket() mislukt.\n"; return 1; }

    sockaddr_in adres{};
    adres.sin_family = AF_INET;
    adres.sin_port   = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &adres.sin_addr);

    if (connect(verbinding, reinterpret_cast<sockaddr*>(&adres), sizeof(adres)) == SOCKET_ERROR) {
        std::cerr << "Kon niet verbinden (draait de server?).\n";
        return 1;
    }
  
    std::cout << "Verbonden met de server!\n";

    std::string bericht;  
    while (recvPlat(verbinding, bericht)) {
        std::cout << "Ontvangen van client: \"" << bericht << "\"\n";      
      
        if (bericht == "exit") {
            std::cout << "Client wil stoppen.\n";
            break;
        }      
      
    // -------------------------------------------------------------------------
    // 5. Pump two directions at once, so two threads:
    //    - reader thread: everything the console outputs, byte by byte to our
    //      screen. We pass the raw bytes through (including the VT codes), so
    //      colours and cursor movements keep working.
    //    - main thread: everything you type, on to the console input.
    // -------------------------------------------------------------------------
    std::thread reader([&]() {
        char buffer[4096];
        DWORD read = 0;
        while (ReadFile(fromConsole, buffer, sizeof(buffer), &read, NULL) && read > 0) {
            //DWORD written = 0;
            //WriteFile(hOurOut, buffer, read, &written, NULL);
            if (!sendFrame(verbinding, std::string(buffer, read))) break;
        }
    });

    // The main thread: read your keystrokes and send them to the console.
    {
        //std::string commands = "echo hello\r\ndir\r\nexit\r\n";
        DWORD written = 0;
        WriteFile(toConsole, bericht.c_str(), (DWORD)bericht.size(), &written, NULL);
        WaitForSingleObject(pi.hProcess, INFINITE);
    }

    // -------------------------------------------------------------------------
    // 6. Clean up neatly. ClosePseudoConsole stops the console; after that the
    //    output pipe reports "end" and the reader thread stops by itself.
    // -------------------------------------------------------------------------
    closeConsole(hConsole);
    if (reader.joinable()) reader.join();

    CloseHandle(toConsole);
    CloseHandle(fromConsole);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    DeleteProcThreadAttributeList(si.lpAttributeList);
    HeapFree(GetProcessHeap(), 0, si.lpAttributeList);

    SetConsoleMode(hOurOut, oldOutMode);   // restore our screen
    std::cout << "\r\n=== demo stopped ===\r\n";
    return 0;      
    }

    // Stap 6: opruimen.
    closesocket(verbinding);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
#endif
