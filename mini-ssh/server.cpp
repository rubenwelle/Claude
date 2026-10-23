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
// Om het veilig te houden start deze server GEEN shell waarin je alles kunt
// typen. Hij kent alleen een paar ingebouwde commando's (help, echo, tijd,
// agenda, programmas, start, whoami, exit). Met "start" kun je een programma
// laten draaien, maar alleen als het op een vaste lijst staat.
// Een echte SSH-server start op deze plek een shell.
//
// Bouwen op Windows (MinGW):  g++ server.cpp -o server.exe -lws2_32
// Bouwen op Linux:            g++ server.cpp -o server
// =============================================================================

#include "minissh.h"
#include <ctime>     // std::time, voor het "tijd"-commando
#include <fstream>   // std::ifstream, om een bestand te lezen (voor "agenda")
#include <cstdio>    // popen/_popen, fgets: een programma starten en lezen
#include <map>       // std::map, een "woordenboek" van naam -> waarde
#include <vector>    // std::vector, een lijst die kan groeien (voor argumenten)

// Het agenda-bestand op de server. Dit is een vaste naam: de client kan NIET
// zelf kiezen welk bestand de server leest. Zou je dat wel toestaan (bijv.
// "lees <bestandsnaam>"), dan kan iemand met "lees ../../geheim.txt" elk
// bestand op de server opvragen. Een vaste naam houdt het veilig.
// Het bestand wordt gezocht in de map waarin je server.exe start.
const std::string AGENDA_BESTAND = "agenda.txt";

// -----------------------------------------------------------------------------
// Lees het hele agenda-bestand en geef de inhoud terug als één tekst.
// -----------------------------------------------------------------------------
std::string leesAgenda() {
    // std::ifstream ("input file stream") opent een bestand om te lezen,
    // ongeveer zoals fopen(..., "r") in C.
    std::ifstream bestand(AGENDA_BESTAND);
    if (!bestand) {   // kon het bestand niet openen (bestaat niet?)
        return "Geen agenda gevonden. Zet een bestand '" + AGENDA_BESTAND +
               "' in de map waar de server draait.";
    }

    // Regel voor regel lezen en aan elkaar plakken.
    std::string inhoud;
    std::string regel;
    while (std::getline(bestand, regel)) {
        if (!inhoud.empty()) inhoud += "\n";
        inhoud += regel;
    }
    // Het bestand wordt automatisch gesloten als 'bestand' hier ophoudt te
    // bestaan (aan het einde van de functie). Geen fclose() nodig!

    if (inhoud.empty()) return "Je agenda is leeg.";
    // Een frame mag maximaal 64 KB zijn (zie recvFrame in minissh.h).
    if (inhoud.size() > 60000) {
        inhoud = inhoud.substr(0, 60000) + "\n... (agenda te lang, rest weggelaten)";
    }
    return inhoud;
}

// -----------------------------------------------------------------------------
// Programma's starten: de lijst van wat MAG.
//
// Dit is een "whitelist": een lijst van toegestane programma's. De client
// stuurt alleen een korte naam (bijv. "datum"), en de server zoekt in deze
// lijst op welk programma daarbij hoort. Staat de naam er niet in, dan
// gebeurt er niets.
//
// Waarom zo? Als de server zomaar uitvoert wat de client stuurt, kan iemand
// die kan inloggen ook "del /s /q C:\*" sturen. Dat heet "command
// injection" en is een van de bekendste beveiligingsfouten. Met een vaste
// lijst kan de client nooit zelf een opdracht in elkaar zetten.
//
// Windows en Linux hebben andere programma's, dus kiezen we met #ifdef.
// Een eigen programma toevoegen = één regel erbij in deze lijst.
// -----------------------------------------------------------------------------
const std::map<std::string, std::string> TOEGESTANE_PROGRAMMAS = {
#ifdef _WIN32
    //  naam die je typt    programma dat de server echt start
    { "datum",      "date /t" },     // de datum van vandaag
    { "bestanden",  "dir /b" },      // de bestanden in de map van de server
    { "systeem",    "ver" },         // welke Windows-versie
    { "hallo",      "hallo.exe" },   // je eigen programma (zie hallo.cpp)
    { "assistent",  "assistent.exe" },  // leest agenda en mail (assistent.cpp)
#else
    { "datum",      "date" },
    { "bestanden",  "ls" },
    { "systeem",    "uname -a" },
    { "hallo",      "./hallo" },
    { "assistent",  "./assistent" },
#endif
};

// Op Windows heten deze functies _popen en _pclose, op Linux popen en pclose.
// Deze truc zorgt dat we in de rest van de code één naam kunnen gebruiken.
#ifdef _WIN32
    #define popen  _popen
    #define pclose _pclose
#else
    #include <sys/wait.h>   // WEXITSTATUS, zie startProgramma()
#endif

// -----------------------------------------------------------------------------
// Is dit argument veilig om aan een programma mee te geven?
//
// Een argument is extra informatie achter de programmanaam, zoals "mail" en
// "2" in "start assistent mail 2". Het programma krijgt die binnen via
// argc/argv (zie assistent.cpp).
//
// Het gevaar: popen() geeft de opdracht aan de shell (cmd op Windows). En de
// shell kent speciale tekens: & betekent "doe daarna nog een opdracht",
// > betekent "schrijf naar een bestand", | koppelt programma's aan elkaar.
// Als iemand "start assistent mail & del agenda.txt" stuurt en we plakken dat
// zomaar achter de opdracht, voert cmd ook "del agenda.txt" uit!
//
// De oplossing: we laten alleen letters en cijfers toe. Daar zit geen enkel
// speciaal teken tussen, dus de shell kan er niets anders van maken. Dit heet
// ook wel een whitelist voor tekens: we zeggen wat MAG, niet wat niet mag.
// (Een lijst van verboden tekens is gevaarlijk: je vergeet er altijd één.)
// -----------------------------------------------------------------------------
bool isVeiligArgument(const std::string& argument) {
    if (argument.empty() || argument.size() > 32) return false;
    for (char c : argument) {
        bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        bool cijfer = (c >= '0' && c <= '9');
        if (!letter && !cijfer) return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
// Start een programma en vang alles op wat het print (de "output").
//
// popen() start het programma en geeft je een FILE*, net als fopen() in C.
// Alleen lees je nu niet uit een bestand, maar uit wat het programma print.
// "r" betekent: wij willen LEZEN wat het programma schrijft.
//
// 'regel' is alles na "start", bijvoorbeeld "assistent mail 2". Het eerste
// woord is de programmanaam, de rest zijn argumenten.
// -----------------------------------------------------------------------------
std::string startProgramma(const std::string& regel) {
    // Splits op spaties. std::istringstream leest woord voor woord, net als
    // std::cin, maar dan uit een string.
    std::istringstream woorden(regel);
    std::string naam;
    woorden >> naam;
    std::vector<std::string> argumenten;
    std::string woord;
    while (woorden >> woord) {
        argumenten.push_back(woord);
    }

    // Zoek de naam op in de lijst. find() geeft end() als hij er niet is.
    auto gevonden = TOEGESTANE_PROGRAMMAS.find(naam);
    if (gevonden == TOEGESTANE_PROGRAMMAS.end()) {
        return "Programma '" + naam + "' staat niet op de lijst. "
               "Typ 'programmas' om te zien wat mag.";
    }

    // Controleer elk argument VOORDAT we iets starten.
    if (argumenten.size() > 3) {
        return "Te veel argumenten (maximaal 3).";
    }
    for (const std::string& argument : argumenten) {
        if (!isVeiligArgument(argument)) {
            return "Argument '" + argument + "' geweigerd: alleen letters en "
                   "cijfers zijn toegestaan.";
        }
    }

    // Nu pas de opdracht in elkaar zetten: het echte programma uit de lijst,
    // plus de (gecontroleerde) argumenten met een spatie ertussen.
    // gevonden->first is de naam, gevonden->second het echte programma.
    std::string opdracht = gevonden->second;
    for (const std::string& argument : argumenten) {
        opdracht += " " + argument;
    }
    // " 2>&1" betekent: stuur ook foutmeldingen naar dezelfde output, zodat
    // je bijvoorbeeld "hallo.exe niet gevonden" ook in de client ziet.
    opdracht += " 2>&1";

    FILE* uitvoer = popen(opdracht.c_str(), "r");
    if (uitvoer == nullptr) {
        return "Kon het programma niet starten.";
    }

    // Lees de output stukje voor stukje, net als een bestand met fgets().
    std::string resultaat;
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), uitvoer) != nullptr) {
        resultaat += buffer;
        if (resultaat.size() > 60000) {   // een frame mag max. 64 KB zijn
            resultaat += "\n... (te veel output, rest weggelaten)";
            break;
        }
    }
    // pclose() wacht tot het programma klaar is en geeft de "exit code":
    // 0 betekent meestal "alles ging goed".
    int exitCode = pclose(uitvoer);
#ifndef _WIN32
    // Op Linux zit de exit code "ingepakt" in een groter getal (1 wordt 256).
    // WEXITSTATUS pakt hem weer uit. Windows geeft hem meteen goed terug.
    if (WIFEXITED(exitCode)) exitCode = WEXITSTATUS(exitCode);
#endif

    // Haal de laatste enter weg, die zet de client er zelf al achter.
    while (!resultaat.empty() && (resultaat.back() == '\n' || resultaat.back() == '\r')) {
        resultaat.pop_back();
    }
    if (resultaat.empty()) resultaat = "(het programma printte niets)";
    if (exitCode != 0) {
        resultaat += "\n(exit code: " + std::to_string(exitCode) + ")";
    }
    return resultaat;
}

// De lijst van toegestane programma's als tekst, voor het commando "programmas".
std::string lijstProgrammas() {
    std::string tekst = "Programma's die je mag starten met 'start <naam> [argumenten]':";
    // Loop door de map: elk element heeft een .first (naam) en .second.
    for (const auto& programma : TOEGESTANE_PROGRAMMAS) {
        tekst += "\n  " + programma.first + "   (" + programma.second + ")";
    }
    return tekst;
}

// De enige gebruiker die mag inloggen. In het echt staan wachtwoorden niet
// in de code, en ook niet als leesbare tekst: het systeem bewaart alleen een
// "hash" (een vingerafdruk) van je wachtwoord.
const std::string GEBRUIKER  = "ruben";
const std::string WACHTWOORD = "geheim";
const int MAX_POGINGEN = 3;

// -----------------------------------------------------------------------------
// Voer één (ingebouwd) commando uit en geef het antwoord terug.
//
// Zo werkt het: de regel van de client wordt gesplitst in het eerste woord
// (het commando) en de rest (bijv. bij "echo hallo" is de rest "hallo").
// Daarna is het gewoon een rij if's: is het "help"? is het "echo"? enz.
// Wat je teruggeeft (return), stuurt de server versleuteld naar de client.
//
// Zelf een commando toevoegen? Zet een nieuw blok erbij, bijvoorbeeld:
//     if (commando == "hoi") {
//         return "Hoi " + gebruiker + "!";
//     }
// en vergeet niet het ook in de help-tekst te zetten.
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
               "  agenda        je agenda (agenda.txt op de server)\n"
               "  programmas    welke programma's je mag starten\n"
               "  start <naam> [argumenten]\n"
               "                start een programma en toon de output\n"
               "                (bijv. 'start assistent mail')\n"
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
    if (commando == "agenda") {
        return leesAgenda();
    }
    if (commando == "programmas") {
        return lijstProgrammas();
    }
    if (commando == "start") {
        return startProgramma(rest);
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
