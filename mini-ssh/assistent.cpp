// =============================================================================
// assistent.cpp  -  Een klein "assistent"-programma voor op de server
//
// Dit programma kan verschillende ACTIES uitvoeren. Welke actie, dat geef je
// mee als argument achter de programmanaam:
//
//   assistent agenda       laat je agenda zien (agenda.txt)
//   assistent mail         overzicht van je mail (mail.txt, fictief)
//   assistent mail 2       lees mailbericht nummer 2
//   assistent              uitleg over wat het programma kan
//
// Via mini-SSH start je het met:  start assistent mail 2
// De server geeft "mail" en "2" door als argumenten, en alles wat dit
// programma print gaat versleuteld terug naar de client.
//
// Je kunt het ook gewoon zelf in een opdrachtprompt starten om te testen:
//   assistent.exe mail 2
//
// Bouwen op Windows (MinGW):  g++ assistent.cpp -o assistent.exe
// Bouwen op Linux:            g++ assistent.cpp -o assistent
// =============================================================================

#include <iostream>
#include <fstream>   // std::ifstream, bestanden lezen
#include <string>
#include <vector>

// De bestanden staan vast in de code, net als bij het agenda-commando van de
// server. De gebruiker kan dus nooit een ander bestand laten lezen.
const std::string AGENDA_BESTAND = "agenda.txt";
const std::string MAIL_BESTAND   = "mail.txt";

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
                  << "  start assistent mail <nr>   lees een bericht\n";
        return 0;
    }

    std::string actie = argv[1];
    if (actie == "agenda") return toonAgenda();
    if (actie == "mail")   return toonMail(argc, argv);

    // Ook hier: alleen acties die we kennen. Al het andere weigeren we.
    std::cout << "Onbekende actie '" << actie << "'. Kies 'agenda' of 'mail'.\n";
    return 1;
}
