#include <iostream>
#include <string>
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

int main() {
#ifdef _WIN32
    // Winsock opstarten (alleen Windows), zie uitleg in server.cpp.
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup mislukt\n";
        return 1;
    }
#endif

    // Stap 1: een TCP-socket maken.
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Kon geen socket maken\n";
        return 1;
    }

    // Stap 2: het adres van de SERVER invullen.
    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port   = htons(PORT);
    // inet_pton zet de tekst "127.0.0.1" om naar het binaire formaat
    // dat de socket-functies verwachten ("pton" = presentation to network).
    inet_pton(AF_INET, SERVER_IP, &serverAddress.sin_addr);

    // Stap 3: connect() Hier gebeurt de TCP-handshake:
    // client en server spreken af dat ze een verbinding hebben.
    if (connect(sock, (sockaddr*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR) {
        std::cerr << "Kon geen verbinding maken. Draait de server wel?\n";
        closesocket(sock);
        return 1;
    }
    std::cout << "Verbonden met de server!\n";

    // Stap 4: het bericht versturen.
    std::string message = "hello world";
    send(sock, message.c_str(), (int)message.size(), 0);
    std::cout << "Verstuurd: " << message << "\n";

    // Stap 5: wachten op het antwoord en het printen.
    char buffer[1024];
    int bytesReceived = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (bytesReceived > 0) {
        buffer[bytesReceived] = '\0';  // afsluitende nul toevoegen
        std::cout << "Antwoord van server: " << buffer << "\n";
    } else {
        std::cerr << "Geen antwoord ontvangen\n";
    }

    // Stap 6: opruimen.
    closesocket(sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
