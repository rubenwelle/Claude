// =============================================================================
// client.cpp  -  PLATTE variant (zonder versleuteling)
//
// De client verbindt met de server, en dan mag jij regels typen. Elke regel
// gaat met sendPlat naar de server; het antwoord komt met recvPlat terug.
// Er is GEEN login en GEEN sleuteluitwisseling.
//
// Let op de regel die sendPlat erbij print: "[over het netwerk, leesbaar: ...]".
// Dat is precies wat een afluisteraar zou zien. In de map ../vaste-sleutel
// zie je in plaats daarvan onleesbare hex-bytes.
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

    std::cout << "=== PLATTE client (zonder versleuteling) ===\n"
              << "Verbonden met " << SERVER_IP << ":" << PORT << ".\n"
              << "Typ een regel en druk op Enter. Typ 'exit' om te stoppen.\n\n";

    // 2. Het gesprek: lees wat jij typt, stuur het, toon het antwoord.
    std::string invoer;
    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, invoer)) break;   // Ctrl-D / Ctrl-Z = einde

        if (!sendPlat(verbinding, invoer)) {
            std::cout << "Verzenden mislukt.\n";
            break;
        }
        if (invoer == "exit") break;

        std::string antwoord;
        if (!recvPlat(verbinding, antwoord)) {
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
