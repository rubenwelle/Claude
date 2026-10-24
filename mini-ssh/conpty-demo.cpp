// =============================================================================
// conpty-demo.cpp  -  A LOCAL demo: cmd.exe inside a real pseudo-console
//
// What is this for? In cmd-pipe.cpp you saw that a shell behaves oddly through
// "raw" pipes: the output lags a line behind, the prompt doesn't appear on
// time, no colours. That happens because the shell notices it is NOT talking
// to a terminal.
//
// The solution Windows has for this is ConPTY: a "pseudo-console"
// (pseudo-terminal). Your program pretends to be a real terminal window. The
// shell then behaves normally: prompt on time, colours, the cursor that moves.
// See SHELL-UITLEG.md for the idea; this is that idea, for real.
//
// IMPORTANT - please read:
//   * This program does NOT talk over the network. No port is listening and
//     there is no login. It starts cmd.exe only on YOUR computer and connects
//     it to your own keyboard and screen.
//   * NEVER connect a shell to a network connection. Then anyone who reaches
//     the port can do anything on your computer. That is why mini-SSH does not
//     do it, and why this demo stays strictly local.
//
// WINDOWS ONLY: ConPTY exists since Windows 10 version 1809 (October 2018).
// On Linux this does not work (there the equivalent is a pty, a different API).
// Build on Windows (MinGW):
//     g++ -std=c++17 conpty-demo.cpp -o conpty-demo.exe
// (On Linux it compiles to a small program that only explains you need
//  Windows.)
// =============================================================================

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

// Some (older) MinGW headers don't know the ConPTY names yet. So we borrow the
// functions at run time from kernel32.dll (with GetProcAddress) instead of
// calling them directly. Two benefits:
//   1. The code also compiles with older headers that don't know
//      CreatePseudoConsole (like some MinGW versions).
//   2. If the program runs on an old Windows WITHOUT ConPTY, we simply don't
//      find the function and say so cleanly, instead of crashing.
// This is also how Microsoft's own sample does it. An HPCON (the "handle" to
// the pseudo-console) is just an opaque pointer; we use void* here so we don't
// depend on a header definition.
#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

typedef HRESULT (WINAPI *CreatePseudoConsoleFn)(COORD, HANDLE, HANDLE, DWORD, void**);
typedef void    (WINAPI *ClosePseudoConsoleFn)(void*);

int main() {
    // -------------------------------------------------------------------------
    // 0. Fetch the ConPTY functions from kernel32.dll. If that fails, this
    //    Windows doesn't have a pseudo-console yet (older than version 1809).
    // -------------------------------------------------------------------------
    // (The detour via void* keeps the compiler happy when converting the
    //  generic function pointer that GetProcAddress returns.)
    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    auto createConsole = reinterpret_cast<CreatePseudoConsoleFn>(
        reinterpret_cast<void*>(GetProcAddress(kernel32, "CreatePseudoConsole")));
    auto closeConsole  = reinterpret_cast<ClosePseudoConsoleFn>(
        reinterpret_cast<void*>(GetProcAddress(kernel32, "ClosePseudoConsole")));
    if (!createConsole || !closeConsole) {
        std::cerr << "This Windows has no ConPTY (needs Windows 10 1809+).\r\n";
        return 1;
    }

    // -------------------------------------------------------------------------
    // 1. Make our own screen "VT-ready". ConPTY sends the output as a stream
    //    with colour and cursor codes (so-called VT escape codes, just like a
    //    real terminal). With this flag Windows renders those codes nicely
    //    instead of printing them as strange characters.
    // -------------------------------------------------------------------------
    HANDLE hOurOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hOurIn  = GetStdHandle(STD_INPUT_HANDLE);
    DWORD oldOutMode = 0;
    GetConsoleMode(hOurOut, &oldOutMode);
    SetConsoleMode(hOurOut, oldOutMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    std::cout << "=== LOCAL ConPTY demo with cmd.exe ===\r\n"
                 "This runs only on your own computer, without a network.\r\n"
                 "You now talk to a real pseudo-console: notice that the prompt\r\n"
                 "and the output appear nicely on time (unlike cmd-pipe). Type\r\n"
                 "'exit' to stop.\r\n\r\n";

    // -------------------------------------------------------------------------
    // 2. Create two pipes: one for what WE send to the console (input) and one
    //    for what the console sends BACK (output). The pseudo-console gets the
    //    other end of each pipe.
    //       we write -> toConsole ------> consoleReads   (cmd's stdin)
    //       we read  <- fromConsole <---- consoleWrites  (cmd's stdout)
    // -------------------------------------------------------------------------
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
            DWORD written = 0;
            WriteFile(hOurOut, buffer, read, &written, NULL);
        }
    });

    // The main thread: read your keystrokes and send them to the console.
    {
        char buffer[4096];
        DWORD read = 0;
        while (ReadFile(hOurIn, buffer, sizeof(buffer), &read, NULL) && read > 0) {
            DWORD written = 0;
            if (!WriteFile(toConsole, buffer, read, &written, NULL)) break;
            // Has cmd stopped in the meantime (e.g. after 'exit')? Then we stop too.
            if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) break;
        }
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

#else   // ---------------------------------------------------------------------

#include <iostream>
int main() {
    std::cout <<
        "conpty-demo only works on Windows 10 version 1809 or newer.\n"
        "ConPTY (the pseudo-console API) does not exist on Linux/macOS.\n"
        "The Linux equivalent is a pty (pseudo-terminal, like with 'ssh');\n"
        "see SHELL-UITLEG.md for the idea. On Linux, use cmd-pipe (raw pipes)\n"
        "for now to see the difference.\n";
    return 0;
}

#endif
