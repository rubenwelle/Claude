// =============================================================================
// server.cpp  -  Variant MET VASTE SLEUTEL (ingebouwde versleuteling)
//
// Dezelfde opzet als ../plat/server.cpp, met één groot verschil: de berichten
// worden versleuteld met sendVersleuteld / recvVersleuteld. De sleutel komt
// NIET uit een login of een uitwisseling, maar uit de vaste GEDEELDE_SLEUTEL
// in net.h, die server en client allebei kennen.
//
// Omdat er geen uitwisseling is, maken we de twee sleutelstromen meteen bij
// het starten aan, allebei uit GEDEELDE_SLEUTEL:
//   - "client->server": waarmee de client versleutelt en wij ontsleutelen
//   - "server->client": waarmee wij versleutelen en de client ontsleutelt
//
// Bouwen op Linux:    g++ -std=c++17 server.cpp -o server
// Bouwen op Windows:  g++ -std=c++17 server.cpp -o server.exe -lws2_32
//
// Alleen 127.0.0.1, alleen om te leren.
// =============================================================================

#include "net.h"
#include <iostream>
#include <string>

int main() {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup mislukt.\n";
        return 1;
    }
#endif

    SOCKET luister = socket(AF_INET, SOCK_STREAM, 0);
    if (luister == INVALID_SOCKET) { std::cerr << "socket() mislukt.\n"; return 1; }

    int aan = 1;
    setsockopt(luister, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&aan), sizeof(aan));

    sockaddr_in adres{};
    adres.sin_family = AF_INET;
    adres.sin_port   = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &adres.sin_addr);

    if (bind(luister, reinterpret_cast<sockaddr*>(&adres), sizeof(adres)) == SOCKET_ERROR) {
        std::cerr << "bind() mislukt (draait de server al?).\n";
        return 1;
    }
    if (listen(luister, 1) == SOCKET_ERROR) { std::cerr << "listen() mislukt.\n"; return 1; }

    std::cout << "=== Server MET VASTE SLEUTEL ===\n"
              << "Luistert op " << SERVER_IP << ":" << PORT << ". Wacht op een client...\n"
              << "(geen login, geen uitwisseling: de sleutel staat ingebouwd)\n\n";

    SOCKET klant = accept(luister, nullptr, nullptr);
    if (klant == INVALID_SOCKET) { std::cerr << "accept() mislukt.\n"; return 1; }
    std::cout << "Client verbonden!\n\n";

    // Twee sleutelstromen, allebei uit de vaste GEDEELDE_SLEUTEL. De client
    // maakt precies dezelfde twee, met dezelfde labels, dus ze passen op elkaar.
    StroomVersleutelaar vanClient(GEDEELDE_SLEUTEL, "client->server");  // ontsleutelen
    StroomVersleutelaar naarClient(GEDEELDE_SLEUTEL, "server->client"); // versleutelen

    std::string bericht;
    while (recvVersleuteld(klant, vanClient, bericht)) {
        std::cout << "Ontsleuteld van client: \"" << bericht << "\"\n";

        if (bericht == "exit") {
            std::cout << "Client wil stoppen.\n";
            break;
        }

        std::string antwoord = "server zegt: ";
        for (char c : bericht) {
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
            antwoord += c;
        }

        if (!sendVersleuteld(klant, naarClient, antwoord)) break;
    }

    std::cout << "\nVerbinding gesloten.\n";
    closesocket(klant);
    closesocket(luister);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
