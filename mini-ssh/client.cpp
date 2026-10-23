// =============================================================================
// client.cpp  -  Een mini-SSH-client (SPEELGOED, alleen om te leren!)
//
// Wat doet dit programma?
//   1. Verbinding maken met de mini-SSH-server op 127.0.0.1:2222.
//   2. Versies uitwisselen.
//   3. Diffie-Hellman: samen met de server een geheim getal afspreken.
//   4. Vanaf nu is alles versleuteld.
//   5. Inloggen met gebruikersnaam en wachtwoord (ruben / geheim).
//   6. Jij typt commando's, de server stuurt antwoorden terug.
//
// Start EERST server.exe, en daarna in een tweede venster client.exe.
//
// Bouwen op Windows (MinGW):  g++ client.cpp -o client.exe -lws2_32
// Bouwen op Linux:            g++ client.cpp -o client
// =============================================================================

#include "minissh.h"

int main() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup mislukt\n";
        return 1;
    }
#endif

    // --- Stap 1: verbinden (net als bij hello world) -----------------------
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Kon geen socket maken\n";
        return 1;
    }
    sockaddr_in serverAdres{};
    serverAdres.sin_family = AF_INET;
    serverAdres.sin_port   = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &serverAdres.sin_addr);

    if (connect(sock, reinterpret_cast<sockaddr*>(&serverAdres), sizeof(serverAdres)) == SOCKET_ERROR) {
        std::cerr << "Kon niet verbinden. Draait server.exe al?\n";
        closesocket(sock);
        return 1;
    }
    std::cout << "Verbonden met " << SERVER_IP << ":" << PORT << "\n";

    // --- Stap 2: versies uitwisselen ---------------------------------------
    std::string serverVersie;
    if (!recvFrame(sock, serverVersie) ||
        !sendFrame(sock, PROTOCOL_VERSIE + " client")) {
        std::cerr << "Verbinding verbroken tijdens versie-uitwisseling\n";
        closesocket(sock);
        return 1;
    }
    std::cout << "Server zegt: " << serverVersie << "\n";

    // --- Stap 3: Diffie-Hellman --------------------------------------------
    // De server stuurt eerst zijn publieke getal A, dan sturen wij B.
    std::string tekstA;
    if (!recvFrame(sock, tekstA)) {
        std::cerr << "Verbinding verbroken tijdens sleuteluitwisseling\n";
        closesocket(sock);
        return 1;
    }
    uint64_t A = 0;
    try {
        A = std::stoull(tekstA);
    } catch (...) {
        std::cerr << "Server stuurde geen geldig getal\n";
        closesocket(sock);
        return 1;
    }
    if (A < 2 || A > DH_P - 2) {
        std::cerr << "Ongeldige publieke sleutel van server\n";
        closesocket(sock);
        return 1;
    }

    uint64_t b = kiesGeheimGetal();          // ons geheim
    uint64_t B = powMod(DH_G, b, DH_P);      // publiek
    if (!sendFrame(sock, std::to_string(B))) {
        closesocket(sock);
        return 1;
    }
    uint64_t gedeeldGeheim = powMod(A, b, DH_P);
    std::cout << "Sleuteluitwisseling klaar.\n"
              << "  gedeeld geheim = " << gedeeldGeheim
              << "   (vergelijk met de server: hetzelfde getal!)\n\n";

    // Echte SSH controleert hier ook de "host key" van de server: een
    // digitale handtekening die bewijst dat je met de ECHTE server praat en
    // niet met iemand die ertussen zit. Dat slaan wij over. Zie de uitleg.

    // --- Stap 4: versleutelde kanalen, één per richting --------------------
    StroomVersleutelaar versturen(gedeeldGeheim, "client->server");
    StroomVersleutelaar ontvangen(gedeeldGeheim, "server->client");

    // --- Stap 5: inloggen ---------------------------------------------------
    bool ingelogd = false;
    while (!ingelogd) {
        std::string naam, wachtwoord;
        std::cout << "Gebruikersnaam: ";
        if (!std::getline(std::cin, naam)) break;
        // (Een echte client laat het wachtwoord niet zien terwijl je typt.)
        std::cout << "Wachtwoord: ";
        if (!std::getline(std::cin, wachtwoord)) break;

        // Kijk naar de hex-uitvoer: je wachtwoord is onleesbaar op het net.
        if (!sendVersleuteld(sock, versturen, "AUTH " + naam + " " + wachtwoord)) break;

        std::string antwoord;
        if (!recvVersleuteld(sock, ontvangen, antwoord)) {
            std::cout << "De server heeft de verbinding verbroken.\n";
            break;
        }
        if (antwoord == "AUTH_OK") {
            ingelogd = true;
            std::cout << "Ingelogd! Typ 'help' voor de commando's.\n\n";
        } else {
            std::cout << "Onjuiste gebruikersnaam of wachtwoord.\n";
        }
    }

    // --- Stap 6: de sessie --------------------------------------------------
    while (ingelogd) {
        std::cout << "mini-ssh> ";
        std::string regel;
        if (!std::getline(std::cin, regel)) regel = "exit";  // bijv. Ctrl+Z
        if (regel.empty()) continue;

        if (!sendVersleuteld(sock, versturen, regel)) break;

        std::string antwoord;
        if (!recvVersleuteld(sock, ontvangen, antwoord)) {
            std::cout << "Verbinding verbroken.\n";
            break;
        }
        std::cout << antwoord << "\n";
        if (regel == "exit") break;
    }

    closesocket(sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
