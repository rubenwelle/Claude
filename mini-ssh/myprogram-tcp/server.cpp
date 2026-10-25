// =============================================================================
// server.cpp  -  Runs MyFirstProgram and bridges it to a TCP client
//
// This combines two things you already saw:
//   * the TCP server (listen / accept, see the hello-world and mini-ssh code);
//   * starting a program through two pipes (ChildProcess from ../pipes.h, the
//     same helper the "sessie" command uses).
//
// When a client connects, we start MyFirstProgram ONCE and keep it running for
// the whole conversation. Each request from the client is written to the
// program's stdin; we read the program's stdout back until the <<DONE>> marker
// and send the whole answer to the client.
//
//      client  --TCP-->  server  --pipe-->  MyFirstProgram (stdin)
//      client  <--TCP--  server  <--pipe--  MyFirstProgram (stdout)
//
// WHY PIPES AND NOT ConPTY?  MyFirstProgram is a simple "read a line, print an
// answer" program. It does not need a terminal (no cursor, colours, or arrow
// keys), so plain pipes are the clean, simple fit. ConPTY is only for programs
// that expect a real terminal (like cmd.exe); it would add terminal escape
// codes that would only garble this tidy request/answer protocol.
//
// SAFETY: the server starts ONLY MyFirstProgram, by a fixed name. It never
// starts a shell (no cmd.exe, no /bin/sh), so a client can never run arbitrary
// commands on this computer. And it listens only on 127.0.0.1. This is a local
// learning demo.
//
// Build:  g++ -std=c++17 server.cpp -o server              (Linux)
//         g++ -std=c++17 server.cpp -o server.exe -lws2_32  (Windows)
// =============================================================================

#include "net.h"
#include "../pipes.h"   // ChildProcess: start a program with two pipes
#include <iostream>
#include <string>

// The ONLY program this server will ever start. Fixed on purpose.
#ifdef _WIN32
const std::string PROGRAM = "myfirstprogram.exe";
#else
const std::string PROGRAM = "./myfirstprogram";
#endif

int main() {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed.\n";
        return 1;
    }
#endif

    // --- set up the listening socket on 127.0.0.1:PORT ----------------------
    SOCKET listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener == INVALID_SOCKET) { std::cerr << "socket() failed.\n"; return 1; }

    int on = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&on), sizeof(on));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);

    if (bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "bind() failed (is the server already running?).\n";
        return 1;
    }
    if (listen(listener, 1) == SOCKET_ERROR) { std::cerr << "listen() failed.\n"; return 1; }

    std::cout << "=== MyFirstProgram-over-TCP server ===\n"
              << "Listening on " << SERVER_IP << ":" << PORT << ". Waiting for a client...\n"
              << "(only starts " << PROGRAM << ", never a shell; localhost only)\n\n";

    SOCKET client = accept(listener, nullptr, nullptr);
    if (client == INVALID_SOCKET) { std::cerr << "accept() failed.\n"; return 1; }
    std::cout << "Client connected. Starting " << PROGRAM << "...\n";

    // --- start MyFirstProgram, keep it running for this whole connection ----
    ChildProcess program;
    if (!program.start(PROGRAM, "")) {   // empty argument: no extra arguments
        std::cerr << "Could not start " << PROGRAM
                  << " (did you build it in this folder?).\n";
        closesocket(client);
        return 1;
    }

    // --- the conversation ----------------------------------------------------
    std::string request;
    while (recvFrame(client, request)) {
        std::cout << "Request from client: \"" << request << "\"\n";
        if (request == "exit") break;

        // Send the request to the program's stdin.
        if (!program.writeLine(request)) {
            sendFrame(client, "(the program has stopped)");
            break;
        }

        // Read the program's answer line by line until the <<DONE>> marker,
        // then send the whole answer back as one frame.
        std::string answer, line;
        bool programAlive = true;
        while (true) {
            if (!program.readLine(line)) { programAlive = false; break; }
            if (line == DONE_MARKER) break;
            if (!answer.empty()) answer += "\n";
            answer += line;
        }
        if (!sendFrame(client, answer)) break;
        if (!programAlive) break;
    }

    std::cout << "\nClosing. Stopping " << PROGRAM << ".\n";
    program.writeLine("exit");   // ask the program to stop politely
    program.stop();
    closesocket(client);
    closesocket(listener);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
