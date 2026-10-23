// =============================================================================
// assistent.cpp  -  Een klein "assistent"-programma voor op de server
//
// Dit programma kan verschillende ACTIES uitvoeren. Welke actie, dat geef je
// mee als argument achter de programmanaam:
//
//   assistent agenda       laat je agenda zien (agenda.txt)
//   assistent mail         overzicht van je mail (mail.txt, fictief)
//   assistent mail 2       lees mailbericht nummer 2
//   assistent map          laat zien wat er in de map "gedeeld" staat
//   assistent map fotos    laat zien wat er in "gedeeld/fotos" staat
//   assistent interactief  blijf draaien en lees opdrachten van stdin
//                          (dit gebruikt de server voor het commando "sessie")
//   assistent              uitleg over wat het programma kan
//
// Via mini-SSH start je het met:  start assistent mail 2
// De server geeft "mail" en "2" door als argumenten, en alles wat dit
// programma print gaat versleuteld terug naar de client.
//
// Je kunt het ook gewoon zelf in een opdrachtprompt starten om te testen:
//   assistent.exe mail 2
//
// Bouwen op Windows (MinGW):  g++ -std=c++17 assistent.cpp -o assistent.exe
// Bouwen op Linux:            g++ assistent.cpp -o assistent
// =============================================================================

#include <iostream>
#include <fstream>   // std::ifstream, bestanden lezen
#include <string>
#include <vector>
#include <filesystem> // std::filesystem: mappen en bestanden bekijken (C++17)
#include <algorithm>  // std::sort, om de lijst op alfabet te zetten
#include <sstream>    // std::istringstream, om een regel in woorden te splitsen

// std::filesystem is een lange naam; zo mogen we gewoon fs:: schrijven.
namespace fs = std::filesystem;

// De bestanden staan vast in de code, net als bij het agenda-commando van de
// server. De gebruiker kan dus nooit een ander bestand laten lezen.
const std::string AGENDA_BESTAND = "agenda.txt";
const std::string MAIL_BESTAND   = "mail.txt";

// De enige map die de assistent mag laten zien (plus de mappen daarin).
// Alles daarbuiten, zoals C:\Windows of je Documenten, blijft verborgen.
const std::string BASIS_MAP = "gedeeld";

// Een mailbericht: een struct, net als in C.
struct Mail {
    std::string van;
    std::string onderwerp;
    std::string tekst;
};

// -----------------------------------------------------------------------------
// Actie "agenda": print het hele agenda-bestand.
// -----------------------------------------------------------------------------
int toonAgenda() {
    std::ifstream bestand(AGENDA_BESTAND);
    if (!bestand) {
        std::cout << "Kon " << AGENDA_BESTAND << " niet openen.\n";
        return 1;   // exit code 1 = er ging iets mis
    }
    std::string regel;
    while (std::getline(bestand, regel)) {
        std::cout << regel << "\n";
    }
    return 0;
}

// -----------------------------------------------------------------------------
// Lees mail.txt en maak er een lijst van Mail-structs van.
//
// Het bestand ziet er zo uit (berichten gescheiden door een regel "---"):
//   Van: Opa
//   Onderwerp: Verjaardag
//   Hoi Ruben, ...
//   ---
//   Van: ...
// -----------------------------------------------------------------------------
std::vector<Mail> leesMail() {
    std::vector<Mail> berichten;
    std::ifstream bestand(MAIL_BESTAND);
    if (!bestand) return berichten;   // lege lijst

    Mail huidig;
    std::string regel;
    while (std::getline(bestand, regel)) {
        if (regel == "---") {
            // Einde van een bericht: bewaar het en begin een nieuw.
            berichten.push_back(huidig);
            huidig = Mail();
        } else if (regel.rfind("Van: ", 0) == 0) {          // begint met "Van: "
            huidig.van = regel.substr(5);
        } else if (regel.rfind("Onderwerp: ", 0) == 0) {    // begint met "Onderwerp: "
            huidig.onderwerp = regel.substr(11);
        } else {
            huidig.tekst += regel + "\n";
        }
    }
    // Het laatste bericht heeft geen "---" erachter.
    if (!huidig.van.empty() || !huidig.onderwerp.empty()) {
        berichten.push_back(huidig);
    }
    return berichten;
}

// -----------------------------------------------------------------------------
// Actie "mail": zonder nummer een overzicht, met nummer één bericht.
// -----------------------------------------------------------------------------
int toonMail(int argc, char* argv[]) {
    std::vector<Mail> berichten = leesMail();
    if (berichten.empty()) {
        std::cout << "Geen mail gevonden in " << MAIL_BESTAND << ".\n";
        return 1;
    }

    // Geen nummer meegegeven? Dan het overzicht.
    if (argc < 3) {
        std::cout << "Je hebt " << berichten.size() << " berichten:\n";
        for (size_t i = 0; i < berichten.size(); i++) {
            std::cout << "  " << (i + 1) << ". " << berichten[i].onderwerp
                      << "   (van " << berichten[i].van << ")\n";
        }
        std::cout << "Lees een bericht met: start assistent mail <nummer>\n";
        return 0;
    }

    // argv[2] is het nummer, maar als tekst. std::stoi maakt er een int van,
    // en gooit een fout als het geen getal is. Die vangen we op.
    int nummer = 0;
    try {
        nummer = std::stoi(argv[2]);
    } catch (...) {
        std::cout << "'" << argv[2] << "' is geen nummer.\n";
        return 1;
    }
    // Controleer of het nummer bestaat. Zonder deze controle zou
    // berichten[nummer - 1] buiten de lijst lezen: een klassieke bug.
    if (nummer < 1 || nummer > static_cast<int>(berichten.size())) {
        std::cout << "Bericht " << nummer << " bestaat niet. Kies 1 t/m "
                  << berichten.size() << ".\n";
        return 1;
    }

    const Mail& mail = berichten[nummer - 1];   // de lijst begint bij 0!
    std::cout << "Van:       " << mail.van << "\n"
              << "Onderwerp: " << mail.onderwerp << "\n\n"
              << mail.tekst;
    return 0;
}

// -----------------------------------------------------------------------------
// Is deze mapnaam veilig? Alleen letters en cijfers.
//
// De server controleert dit ook al, maar de assistent doet het zelf nog een
// keer ("defense in depth"). Waarom is dit zo belangrijk? Omdat ".." in een
// pad "de map erboven" betekent. Met "map ../../.." zou iemand anders uit
// de map "gedeeld" kunnen klimmen en je hele schijf bekijken. Dat heet
// "path traversal". Zonder punten en schuine strepen kan dat niet.
// -----------------------------------------------------------------------------
bool isVeiligeMapnaam(const std::string& naam) {
    if (naam.empty() || naam.size() > 32) return false;
    for (char c : naam) {
        bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        bool cijfer = (c >= '0' && c <= '9');
        if (!letter && !cijfer) return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
// Actie "map": laat zien wat er in een map staat.
//
// std::filesystem (C++17) is de standaard manier om met mappen te werken:
//   fs::path              een pad, zoals "gedeeld/fotos"
//   fs::exists(pad)       bestaat het?
//   fs::is_directory(pad) is het een map (en geen bestand)?
//   fs::directory_iterator(pad)  loop door alles wat in de map staat
// -----------------------------------------------------------------------------
int toonMap(int argc, char* argv[]) {
    fs::path basis = BASIS_MAP;
    fs::path pad   = basis;

    // Is er een submap gevraagd, zoals "map fotos"?
    if (argc >= 3) {
        std::string submap = argv[2];
        if (!isVeiligeMapnaam(submap)) {
            std::cout << "Mapnaam '" << submap << "' geweigerd: alleen letters en cijfers.\n";
            return 1;
        }
        pad = basis / submap;   // de / plakt twee stukken pad aan elkaar
    }

    if (!fs::exists(pad) || !fs::is_directory(pad)) {
        std::cout << "De map '" << pad.generic_string() << "' bestaat niet.\n";
        return 1;
    }

    // Tweede controle: waar komen we ECHT uit? canonical() maakt er een
    // volledig pad van (zoals C:\Users\Ruben\mini-ssh\gedeeld\fotos) en lost
    // trucjes als ".." en snelkoppelingen op. Dat pad moet beginnen met het
    // volledige pad van de basismap, anders weigeren we.
    std::error_code fout;
    fs::path echtPad    = fs::canonical(pad, fout);
    fs::path echteBasis = fs::canonical(basis, fout);
    if (fout) {
        std::cout << "Kon de map niet controleren.\n";
        return 1;
    }
    auto [eindeBasis, eindePad] = std::mismatch(echteBasis.begin(), echteBasis.end(),
                                                echtPad.begin(), echtPad.end());
    (void)eindePad;   // die hebben we niet nodig
    if (eindeBasis != echteBasis.end()) {
        std::cout << "Die map ligt buiten '" << BASIS_MAP << "', dat mag niet.\n";
        return 1;
    }

    // Verzamel eerst alle namen, zodat we ze op alfabet kunnen zetten.
    // (directory_iterator geeft ze in een willekeurige volgorde.)
    std::vector<std::string> mappen;
    std::vector<std::string> bestanden;
    for (const fs::directory_entry& item : fs::directory_iterator(pad)) {
        std::string naam = item.path().filename().string();
        if (item.is_directory()) {
            mappen.push_back(naam);
        } else {
            // file_size geeft de grootte in bytes.
            bestanden.push_back(naam + "  (" + std::to_string(item.file_size()) + " bytes)");
        }
    }
    std::sort(mappen.begin(), mappen.end());
    std::sort(bestanden.begin(), bestanden.end());

    std::cout << "Inhoud van " << pad.generic_string() << "/\n";
    for (const std::string& m : mappen)    std::cout << "  [map]  " << m << "\n";
    for (const std::string& b : bestanden) std::cout << "         " << b << "\n";
    if (mappen.empty() && bestanden.empty()) std::cout << "  (leeg)\n";
    if (!mappen.empty() && argc < 3) {
        std::cout << "Kijk in een map met: start assistent map <naam>\n";
    }
    return 0;
}

// -----------------------------------------------------------------------------
// Interactieve modus: blijf draaien en lees opdrachten van stdin.
//
// Zo gebruikt de server de assistent bij het commando "sessie". De server
// heeft onze stdin en stdout aan twee pipes gekoppeld (zie pijpen.h). Voor
// ons maakt dat niets uit: we lezen gewoon met std::getline(std::cin, ...)
// en printen met std::cout, net als altijd.
//
// Twee dingen zijn wel belangrijk:
//
// 1. EINDMARKERING. De server leest regel voor regel uit de pipe, maar hoe
//    weet hij dat een antwoord klaar is? Het ene antwoord is 1 regel, het
//    andere 15. Daarom printen we na elk antwoord een vaste regel EINDE.
//    Dit is hetzelfde probleem als bij TCP (een stroom bytes, geen losse
//    berichten), en de oplossing lijkt op de frames in minissh.h.
//
// 2. FLUSHEN. Als stdout een pipe is (en geen scherm), bewaart C++ wat je
//    print eerst in een buffer, en stuurt het pas als de buffer vol is. Dan
//    zou de server eeuwig wachten. std::endl stuurt een enter EN leegt de
//    buffer ("flush"), zodat alles direct de pipe in gaat.
// -----------------------------------------------------------------------------
const std::string EINDE = "<<KLAAR>>";

int interactief() {
    std::cout << "Hoi! Ik ben de assistent, en ik praat nu via pipes met de server.\n"
              << "Typ: agenda, mail, mail <nr>, map of map <naam>. Typ 'stop' om te stoppen.\n"
              << EINDE << std::endl;   // std::endl = enter + flush

    std::string regel;
    // getline geeft false als stdin dicht gaat (de server sluit pipe 1).
    while (std::getline(std::cin, regel)) {
        if (!regel.empty() && regel.back() == '\r') regel.pop_back();
        if (regel == "stop") break;

        // Splits de regel in woorden, en maak er een argv van zoals main()
        // die zou krijgen. Dan kunnen we dezelfde functies hergebruiken.
        std::istringstream woorden(regel);
        std::vector<std::string> stukken = { "assistent" };   // argv[0]
        std::string woord;
        while (woorden >> woord) stukken.push_back(woord);

        if (stukken.size() < 2) {
            std::cout << "Typ een opdracht, bijvoorbeeld 'mail'.\n";
        } else if (stukken.size() > 4) {
            std::cout << "Te veel woorden (maximaal 3).\n";
        } else {
            // argv is een lijst van char*, dus wijzen we naar onze strings.
            std::vector<char*> args;
            for (std::string& s : stukken) args.push_back(&s[0]);
            int argc = static_cast<int>(args.size());
            char** argv = args.data();

            std::string actie = stukken[1];
            if      (actie == "agenda") toonAgenda();
            else if (actie == "mail")   toonMail(argc, argv);
            else if (actie == "map")    toonMap(argc, argv);
            else std::cout << "Onbekende actie '" << actie << "'.\n";
        }
        std::cout << EINDE << std::endl;   // antwoord klaar, en flush!
    }
    return 0;
}

// -----------------------------------------------------------------------------
// main krijgt de argumenten binnen via argc en argv, precies zoals in C:
//   argc = hoeveel woorden er zijn (de programmanaam telt mee!)
//   argv = de woorden zelf
// Bij "assistent mail 2" is argc 3, en:
//   argv[0] = "assistent"   argv[1] = "mail"   argv[2] = "2"
// -----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Ik ben de assistent. Wat kan ik doen?\n"
                  << "  start assistent agenda      je agenda\n"
                  << "  start assistent mail        overzicht van je mail\n"
                  << "  start assistent mail <nr>   lees een bericht\n"
                  << "  start assistent map         inhoud van de map 'gedeeld'\n"
                  << "  start assistent map <naam>  inhoud van een map daarin\n";
        return 0;
    }

    std::string actie = argv[1];
    if (actie == "agenda") return toonAgenda();
    if (actie == "mail")   return toonMail(argc, argv);
    if (actie == "map")    return toonMap(argc, argv);
    if (actie == "interactief") return interactief();

    // Ook hier: alleen acties die we kennen. Al het andere weigeren we.
    std::cout << "Onbekende actie '" << actie << "'. Kies 'agenda', 'mail' of 'map'.\n";
    return 1;
}
