#include <iostream>   // std::cout en std::cerr om tekst te printen
#include <string>     // std::string, de C++-versie van een tekst (char-array)
#include "net.h"

// -----------------------------------------------------------------------------
// Windows en Linux gebruiken (bijna) dezelfde socket-functies, maar ze zitten
// in andere header-bestanden. Met #ifdef kiezen we bij het compileren de
// juiste. _WIN32 is automatisch gedefinieerd als je voor Windows compileert.
// -----------------------------------------------------------------------------
#ifdef _WIN32
    #include <winsock2.h>   // De Windows socket-bibliotheek ("Winsock")
    #include <ws2tcpip.h>   // Extra hulpfuncties, zoals inet_pton
    // Bij MSVC zorgt deze regel dat ws2_32.lib automatisch wordt meegelinkt.
    // (Alleen voor MSVC; bij MinGW geef je -lws2_32 mee aan g++.)
    #ifdef _MSC_VER
        #pragma comment(lib, "ws2_32.lib")
    #endif
    typedef int socklen_t;  // Linux noemt dit type socklen_t, Windows int
#else
    #include <sys/socket.h> // socket(), bind(), listen(), accept() ...
    #include <netinet/in.h> // sockaddr_in (een IPv4-adres + poort)
    #include <arpa/inet.h>  // inet_pton, htons
    #include <unistd.h>     // close()
    // Op Linux is een socket gewoon een int. We noemen het SOCKET zodat
    // de rest van de code er op beide systemen hetzelfde uitziet.
    typedef int SOCKET;
    const int INVALID_SOCKET = -1;
    const int SOCKET_ERROR   = -1;
    #define closesocket close
#endif

int main() {
#ifdef _WIN32
    // Stap 0 (alleen Windows): Winsock opstarten.
    // Windows wil dat je dit doet voordat je ook maar één socket-functie
    // gebruikt. MAKEWORD(2, 2) betekent: "ik wil Winsock versie 2.2".
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup mislukt\n";
        return 1;
    }
#endif

    // Stap 1: een socket maken.
    //   AF_INET     = we gebruiken IPv4-adressen (zoals 127.0.0.1)
    //   SOCK_STREAM = we willen TCP: een betrouwbare stream van bytes
    //   0           = laat het systeem het juiste protocol kiezen (TCP)
    SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSocket == INVALID_SOCKET) {
        std::cerr << "Kon geen socket maken\n";
        return 1;
    }

    // Stap 2: het adres invullen waarop we willen luisteren.
    // sockaddr_in is een struct (net als in C) met adresfamilie, poort en IP.
    sockaddr_in address{};                 // {} zet alle velden op 0
    address.sin_family      = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;  // luister op alle netwerkkaarten
    // htons = "host to network short". Netwerken verwachten getallen in een
    // vaste bytevolgorde (big-endian); htons zet ons getal daarin om.
    address.sin_port        = htons(PORT);

    // Stap 3: bind() koppelt de socket aan dat adres + poort.
    // Daarna "hoort" poort 5000 bij ons programma.
    if (bind(listenSocket, (sockaddr*)&address, sizeof(address)) == SOCKET_ERROR) {
        std::cerr << "bind() mislukt. Is poort " << PORT << " al in gebruik?\n";
        closesocket(listenSocket);
        return 1;
    }

    // Stap 4: listen() zet de socket in "wachtstand" voor nieuwe verbindingen.
    // Het getal 1 is hoeveel bellers er maximaal in de wachtrij mogen staan.
    if (listen(listenSocket, 1) == SOCKET_ERROR) {
        std::cerr << "listen() mislukt\n";
        closesocket(listenSocket);
        return 1;
    }
    std::cout << "Server luistert op poort " << PORT << "...\n";

    while (true) {
        sockaddr_in clientAdres{};
        socklen_t lengte = sizeof(clientAdres);
        SOCKET client = accept(luisterSocket, reinterpret_cast<sockaddr*>(&clientAdres), &lengte);
        if (client == INVALID_SOCKET) {
            std::cerr << "accept() mislukt\n";
            continue;
        }
        std::cout << "--- Nieuwe verbinding ---\n";
        behandelClient(client);
        closesocket(client);
        std::cout << "--- Verbinding gesloten ---\n\n";
    }

#ifdef _WIN32
    WSACleanup();  // Winsock weer afsluiten (tegenhanger van WSAStartup)
#endif
    return 0;
}
