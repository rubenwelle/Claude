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

    // 1. Maak een luister-socket en koppel hem aan poort 3000 op 127.0.0.1.
    SOCKET luister = socket(AF_INET, SOCK_STREAM, 0);
    if (luister == INVALID_SOCKET) { std::cerr << "socket() mislukt.\n"; return 1; }

    // SO_REUSEADDR: na het stoppen mag je de poort meteen weer gebruiken.
    int aan = 1;
    setsockopt(luister, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&aan), sizeof(aan));

    sockaddr_in adres{};
    adres.sin_family      = AF_INET;
    adres.sin_port        = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &adres.sin_addr);

    if (bind(luister, reinterpret_cast<sockaddr*>(&adres), sizeof(adres)) == SOCKET_ERROR) {
        std::cerr << "bind() mislukt (draait de server al?).\n";
        return 1;
    }
    if (listen(luister, 1) == SOCKET_ERROR) { std::cerr << "listen() mislukt.\n"; return 1; }

    std::cout << "=== PLATTE server (zonder versleuteling) ===\n"
              << "Luistert op " << SERVER_IP << ":" << PORT << ". Wacht op een client...\n\n";

    // 2. Wacht tot er een client verbindt. accept() geeft een NIEUWE socket
    //    voor het gesprek met die ene client.
    SOCKET klant = accept(luister, nullptr, nullptr);
    if (klant == INVALID_SOCKET) { std::cerr << "accept() mislukt.\n"; return 1; }
    std::cout << "Client verbonden! (geen login, geen sleuteluitwisseling)\n\n";

    while (recvPlat(klant, bericht)) {

    }
    
    std::cout << "\nVerbinding gesloten.\n";
    closesocket(klant);
    closesocket(luister);
    
#ifdef _WIN32
    WSACleanup();  // Winsock weer afsluiten (tegenhanger van WSAStartup)
#endif
    return 0;
}
