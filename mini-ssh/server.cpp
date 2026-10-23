// =============================================================================
// server.cpp  -  Een mini-SSH-server (SPEELGOED, alleen om te leren!)
//
// Wat doet dit programma? Precies de stappen die een echte SSH-server ook
// doet, maar dan sterk vereenvoudigd:
//   1. Wachten op een TCP-verbinding (alleen vanaf je eigen computer).
//   2. Versies uitwisselen ("ik spreek MINISSH-1.0").
//   3. Diffie-Hellman: samen een geheim getal afspreken.
//   4. Vanaf nu is alles versleuteld.
//   5. Inloggen: gebruikersnaam en wachtwoord controleren.
//   6. Opdrachten van de client uitvoeren en antwoord terugsturen.
//
// Om het veilig te houden voert deze server GEEN echte opdrachten op je
// computer uit. Hij kent alleen een paar ingebouwde commando's (help, echo,
// tijd, whoami, exit). Een echte SSH-server start op deze plek een shell.
//
// Bouwen op Windows (MinGW):  g++ server.cpp -o server.exe -lws2_32
// Bouwen op Linux:            g++ server.cpp -o server
// =============================================================================

#include "minissh.h"
#include <ctime>     // std::time, voor het "tijd"-commando

// De enige gebruiker die mag inloggen. In het echt staan wachtwoorden niet
// in de code, en ook niet als leesbare tekst: het systeem bewaart alleen een
// "hash" (een vingerafdruk) van je wachtwoord.
const std::string GEBRUIKER  = "ruben";
const std::string WACHTWOORD = "geheim";
const int MAX_POGINGEN = 3;

// -----------------------------------------------------------------------------
// Voer één (ingebouwd) commando uit en geef het antwoord terug.
// -----------------------------------------------------------------------------
std::string voerCommandoUit(const std::string& regel, const std::string& gebruiker) {
    // Splits de regel in het commando (eerste woord) en de rest.
    std::string commando = regel;
    std::string rest;
    size_t spatie = regel.find(' ');
    if (spatie != std::string::npos) {
        commando = regel.substr(0, spatie);
        rest     = regel.substr(spatie + 1);
    }

    if (commando == "help") {
        return "Beschikbare commando's:\n"
               "  help          dit overzicht\n"
               "  echo <tekst>  stuur de tekst terug\n"
               "  tijd          de tijd op de server\n"
               "  whoami        als wie ben je ingelogd\n"
               "  exit          verbinding verbreken";
    }
    if (commando == "echo") {
        return rest;
    }
    if (commando == "tijd") {
        std::time_t nu = std::time(nullptr);
        char buffer[64];
        std::strftime(buffer, sizeof(buffer), "%d-%m-%Y %H:%M:%S",
                      std::localtime(&nu));
        return std::string("Servertijd: ") + buffer;
    }
    if (commando == "whoami") {
        return gebruiker;
    }
    return "Onbekend commando: '" + commando + "'. Typ 'help' voor een overzicht.";
}

// -----------------------------------------------------------------------------
// Handel één verbonden client helemaal af, van handshake tot afsluiten.
// -----------------------------------------------------------------------------
void behandelClient(SOCKET client) {
    // --- Stap 2: versies uitwisselen (nog NIET versleuteld) -----------------
    // Zo weten beide kanten zeker dat ze dezelfde "taal" spreken.
    std::string clientVersie;
    if (!sendFrame(client, PROTOCOL_VERSIE + " server") ||
        !recvFrame(client, clientVersie)) {
        std::cerr << "Verbinding verbroken tijdens versie-uitwisseling\n";
        return;
    }
    std::cout << "Client zegt: " << clientVersie << "\n";
    if (clientVersie.rfind(PROTOCOL_VERSIE, 0) != 0) {  // begint het ermee?
        std::cerr << "Onbekende versie, ik hang op.\n";
        return;
    }

    // --- Stap 3: Diffie-Hellman (nog steeds leesbaar voor afluisteraars) ----
    uint64_t a = kiesGeheimGetal();              // ons geheim, gaat nooit het net op
    uint64_t A = powMod(DH_G, a, DH_P);          // dit mag iedereen zien
    std::string tekstB;
    if (!sendFrame(client, std::to_string(A)) || !recvFrame(client, tekstB)) {
        std::cerr << "Verbinding verbroken tijdens sleuteluitwisseling\n";
        return;
    }

    // Het getal van de client omzetten van tekst naar uint64_t.
    // stoull gooit een "exception" bij onzin; die vangen we op met try/catch.
    uint64_t B = 0;
    try {
        B = std::stoull(tekstB);
    } catch (...) {
        std::cerr << "Client stuurde geen geldig getal\n";
        return;
    }
    // Controle: 0, 1 en P-1 zijn "zwakke" waarden die het geheim voorspelbaar
    // maken. Echte implementaties doen dit soort controles ook.
    if (B < 2 || B > DH_P - 2) {
        std::cerr << "Ongeldige publieke sleutel van client\n";
        return;
    }

    uint64_t gedeeldGeheim = powMod(B, a, DH_P);
    std::cout << "Sleuteluitwisseling klaar.\n"
              << "  publiek: A = " << A << ", B = " << B << "\n"
              << "  gedeeld geheim = " << gedeeldGeheim
              << "   (alleen geprint om te leren! een echte server doet dit nooit)\n";

    // --- Stap 4: vanaf nu versleuteld --------------------------------------
    // Let op de richtingen: wat de server VERSTUURT is "server->client".
    StroomVersleutelaar versturen(gedeeldGeheim, "server->client");
    StroomVersleutelaar ontvangen(gedeeldGeheim, "client->server");

    // --- Stap 5: inloggen ---------------------------------------------------
    // De client stuurt "AUTH <gebruiker> <wachtwoord>". Omdat het al
    // versleuteld is, kan een afluisteraar het wachtwoord niet lezen.
    std::string gebruiker;
    bool ingelogd = false;
    for (int poging = 1; poging <= MAX_POGINGEN && !ingelogd; poging++) {
        std::string bericht;
        if (!recvVersleuteld(client, ontvangen, bericht)) return;

        std::istringstream stukken(bericht);   // splitst op spaties
        std::string woord, naam, wachtwoord;
        stukken >> woord >> naam >> wachtwoord;

        if (woord == "AUTH" && naam == GEBRUIKER && wachtwoord == WACHTWOORD) {
            ingelogd  = true;
            gebruiker = naam;
            std::cout << "Gebruiker '" << naam << "' is ingelogd.\n";
            if (!sendVersleuteld(client, versturen, "AUTH_OK")) return;
        } else {
            std::cout << "Mislukte inlogpoging " << poging << " voor '" << naam << "'\n";
            if (!sendVersleuteld(client, versturen, "AUTH_FOUT")) return;
        }
    }
    if (!ingelogd) {
        std::cout << "Te veel mislukte pogingen, verbinding wordt gesloten.\n";
        return;
    }

    // --- Stap 6: de "sessie": opdrachten ontvangen en beantwoorden ---------
    while (true) {
        std::string regel;
        if (!recvVersleuteld(client, ontvangen, regel)) {
            std::cout << "Client heeft de verbinding verbroken.\n";
            return;
        }
        std::cout << gebruiker << "> " << regel << "\n";

        if (regel == "exit") {
            sendVersleuteld(client, versturen, "Tot ziens!");
            return;
        }
        if (!sendVersleuteld(client, versturen, voerCommandoUit(regel, gebruiker))) {
            return;
        }
    }
}

int main() {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup mislukt\n";
        return 1;
    }
#endif

    // --- Stap 1: socket maken en luisteren (net als bij hello world) -------
    SOCKET luisterSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (luisterSocket == INVALID_SOCKET) {
        std::cerr << "Kon geen socket maken\n";
        return 1;
    }

    sockaddr_in adres{};
    adres.sin_family = AF_INET;
    adres.sin_port   = htons(PORT);
    // Verschil met hello world: we luisteren NIET op INADDR_ANY (alle
    // netwerkkaarten) maar alleen op 127.0.0.1. Andere computers kunnen deze
    // server dus niet bereiken, alleen programma's op je eigen pc.
    inet_pton(AF_INET, SERVER_IP, &adres.sin_addr);

    if (bind(luisterSocket, reinterpret_cast<sockaddr*>(&adres), sizeof(adres)) == SOCKET_ERROR) {
        std::cerr << "bind() mislukt. Draait de server misschien al?\n";
        closesocket(luisterSocket);
        return 1;
    }
    if (listen(luisterSocket, 1) == SOCKET_ERROR) {
        std::cerr << "listen() mislukt\n";
        closesocket(luisterSocket);
        return 1;
    }

    std::cout << "Mini-SSH-server luistert op " << SERVER_IP << ":" << PORT << "\n"
              << "(Stoppen met Ctrl+C)\n\n";

    // Anders dan bij hello world blijft deze server draaien: na elke client
    // wacht hij gewoon op de volgende. Wel één client tegelijk.
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

    // (We komen hier nooit, maar netjes is netjes.)
    closesocket(luisterSocket);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
