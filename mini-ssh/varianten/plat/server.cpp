// =============================================================================
// server.cpp  -  PLATTE variant (zonder versleuteling)
//
// Dit is bijna hetzelfde als de grote mini-ssh server, maar dan zo kaal
// mogelijk, zodat je het hele proces rond "een bericht versturen" ziet:
//   1. wachten op een verbinding
//   2. een bericht ONTVANGEN met recvPlat   (= gewoon recvFrame, geen ontcijferen)
//   3. een antwoord TERUGSTUREN met sendPlat (= gewoon sendFrame, geen versleutelen)
//
// Er is GEEN login en GEEN sleuteluitwisseling. De tekst gaat leesbaar over
// de lijn. Vergelijk dit straks met de map ../vaste-sleutel, waar wel
// versleuteling in zit.
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
    // Op Windows moet je Winsock eerst "opstarten" (zie hello-world).
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup mislukt.\n";
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

    // 3. Het gesprek: lees een bericht, stuur een antwoord terug. Zo lang de
    //    client blijft praten. recvPlat/sendPlat doen GEEN versleuteling.
    std::string bericht;
    while (recvPlat(klant, bericht)) {
        std::cout << "Ontvangen van client: \"" << bericht << "\"\n";

        if (bericht == "exit") {
            std::cout << "Client wil stoppen.\n";
            break;
        }

        // Een heel simpel "antwoord": we sturen de tekst terug in hoofdletters,
        // zodat je ziet dat het echt heen en weer gaat.
        std::string antwoord = "server zegt: ";
        for (char c : bericht) {
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
            antwoord += c;
        }

        if (!sendPlat(klant, antwoord)) break;
    }

    std::cout << "\nVerbinding gesloten.\n";
    closesocket(klant);
    closesocket(luister);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
