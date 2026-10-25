// =============================================================================
// client.cpp  -  Talks to MyFirstProgram over TCP
//
// Connects to the server, lets you type a request (e.g. "reken 7x7" or
// "Liedje"), sends it, and prints the answer that comes back. Type "exit" to
// stop.
//
// The client does not know that a program is running on the other side; it just
// sends a line and reads an answer. All the "start the program / read until
// <<DONE>>" work happens in the server.
//
// Build:  g++ -std=c++17 client.cpp -o client              (Linux)
//         g++ -std=c++17 client.cpp -o client.exe -lws2_32  (Windows)
// =============================================================================

#include "net.h"
#include <iostream>
#include <string>

int main() {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed.\n";
        return 1;
    }
#endif

    SOCKET conn = socket(AF_INET, SOCK_STREAM, 0);
    if (conn == INVALID_SOCKET) { std::cerr << "socket() failed.\n"; return 1; }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);

    if (connect(conn, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "Could not connect (is the server running?).\n";
        return 1;
    }

    std::cout << "=== MyFirstProgram-over-TCP client ===\n"
              << "Connected to " << SERVER_IP << ":" << PORT << ".\n"
              << "Try:  reken 7x7   or   Liedje . Type 'exit' to stop.\n\n";

    std::string input;
    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, input)) break;   // Ctrl-D / Ctrl-Z = end

        if (!sendFrame(conn, input)) { std::cout << "Sending failed.\n"; break; }
        if (input == "exit") break;

        std::string answer;
        if (!recvFrame(conn, answer)) {
            std::cout << "The server closed the connection.\n";
            break;
        }
        std::cout << answer << "\n";
    }

    std::cout << "\nBye.\n";
    closesocket(conn);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
