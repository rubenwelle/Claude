// =============================================================================
// client.cpp  -  Variant MET VASTE SLEUTEL (ingebouwde versleuteling)
//
// Dezelfde opzet als ../plat/client.cpp, maar nu versleuteld. Er is geen login
// en geen uitwisseling: de sleutel staat ingebouwd (GEDEELDE_SLEUTEL in net.h).
//
// De client maakt precies dezelfde twee sleutelstromen als de server, met
// dezelfde labels. "client->server" gebruiken wij om te versturen; de server
// gebruikt diezelfde stroom om te ontsleutelen. En andersom voor de antwoorden.
//
// Let op wat sendVersleuteld erbij print: onleesbare hex-bytes. Dat is wat een
// afluisteraar nu ziet, in plaats van de leesbare tekst uit de map ../plat.
//
// Bouwen op Linux:    g++ -std=c++17 client.cpp -o client
// Bouwen op Windows:  g++ -std=c++17 client.cpp -o client.exe -lws2_32
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

    std::cout << "=== Client MET VASTE SLEUTEL ===\n"
              << "Verbonden met " << SERVER_IP << ":" << PORT << ".\n"
              << "(geen login, geen uitwisseling: de sleutel staat ingebouwd)\n"
              << "Typ een regel en druk op Enter. Typ 'exit' om te stoppen.\n\n";

    // Dezelfde twee stromen als de server, met dezelfde labels en sleutel.
    StroomVersleutelaar naarServer(GEDEELDE_SLEUTEL, "client->server");  // versleutelen
    StroomVersleutelaar vanServer(GEDEELDE_SLEUTEL, "server->client");   // ontsleutelen

    std::string invoer;
    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, invoer)) break;

        if (!sendVersleuteld(verbinding, naarServer, invoer)) {
            std::cout << "Verzenden mislukt.\n";
            break;
        }
        if (invoer == "exit") break;

        std::string antwoord;
        if (!recvVersleuteld(verbinding, vanServer, antwoord)) {
            std::cout << "Server heeft de verbinding gesloten.\n";
            break;
        }
        std::cout << antwoord << "\n";
    }

    std::cout << "\nTot ziens.\n";
    closesocket(verbinding);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
