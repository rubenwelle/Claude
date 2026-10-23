// =============================================================================
// cmd-pipe.cpp  -  Een LOKALE demo: een echte shell via twee pipes
//
// Waarvoor is dit? Om het verschil te VOELEN tussen de nette assistent uit de
// "sessie" en een echte shell (cmd.exe op Windows, /bin/sh op Linux). Je ziet
// hier precies waarom echte SSH zoveel extra werk doet.
//
// BELANGRIJK - lees dit even:
//   * Dit programma praat NIET over het netwerk. Er luistert geen poort,
//     er is geen login. Het start de shell alleen op JOUW computer en
//     koppelt die aan je eigen toetsenbord en scherm, via pipes.
//   * Daarom is dit geen "remote shell" en geen backdoor: niemand anders
//     kan erbij. Het is puur om te zien hoe pipes + een shell samenwerken.
//   * Koppel een shell NOOIT aan een netwerkverbinding (zoals de mini-SSH
//     server). Dan geef je iedereen die de poort bereikt volledige toegang
//     tot je computer. Dat is precies waarom mini-SSH dat niet doet.
//
// Wat gebeurt er? Twee dingen tegelijk, dus met twee threads:
//   - een "lezer"-thread: alles wat de shell print, direct op je scherm;
//   - de hoofd-thread: alles wat jij typt, naar de stdin van de shell.
// Dat "tegelijk" is hetzelfde probleem dat een echte SSH-server oplost.
//
// Bouwen op Windows (MinGW):  g++ -std=c++17 cmd-pipe.cpp -o cmd-pipe.exe
// Bouwen op Linux:            g++ -std=c++17 -pthread cmd-pipe.cpp -o cmd-pipe
// =============================================================================

#include "pijpen.h"
#include <iostream>
#include <string>
#include <thread>    // std::thread, om twee dingen tegelijk te doen
#include <atomic>    // std::atomic, een vlaggetje dat je veilig vanuit twee
                     // threads tegelijk mag lezen en schrijven

// Welke shell starten we? Op Windows cmd.exe, op Linux /bin/sh.
#ifdef _WIN32
const std::string SHELL = "cmd.exe";
#else
const std::string SHELL = "/bin/sh";
#endif

int main() {
    std::cout << "=== LOKALE pipe-demo met een echte shell ===\n"
              << "Dit draait alleen op je eigen computer, zonder netwerk.\n"
              << "Typ shell-opdrachten (bijv. 'dir' of 'ls'). Typ 'exit' om te stoppen.\n"
              << "Let op het verschil met de assistent-sessie!\n\n";

    KindProces shell;
    if (!shell.start(SHELL, "")) {   // leeg argument = shell zonder argument
        std::cerr << "Kon " << SHELL << " niet starten.\n";
        return 1;
    }

    // Dit vlaggetje staat op true zolang de shell leeft. Zodra de lezer-thread
    // merkt dat de pipe dicht is (de shell is gestopt), zet hij het op false.
    std::atomic<bool> loopt{true};

    // De lezer-thread: lees regel voor regel wat de shell print en zet het op
    // het scherm. Hier zie je meteen een verschil: er komt GEEN <<KLAAR>>.
    // De shell zegt nooit "ik ben klaar met antwoorden", dus de enige manier
    // om te weten dat er iets is, is gewoon blijven lezen.
    std::thread lezer([&]() {
        std::string regel;
        while (shell.leesRegel(regel)) {
            std::cout << regel << "\n";
            std::cout.flush();   // meteen tonen, niet in een buffer bewaren
        }
        loopt = false;   // pipe dicht: de shell is gestopt
    });

    // De hoofd-thread: lees wat jij typt en stuur het naar de shell.
    std::string invoer;
    while (loopt && std::getline(std::cin, invoer)) {
        if (!shell.schrijfRegel(invoer)) break;   // pipe dicht
        if (invoer == "exit" || invoer == "quit") break;
    }

    // Opruimen, in de juiste volgorde:
    //   1. Sluit alleen onze schrijfkant. De shell krijgt "einde invoer" en
    //      stopt, maar wij kunnen zijn laatste uitvoer nog lezen.
    //   2. Wacht tot de lezer-thread klaar is (die stopt vanzelf zodra de
    //      shell zijn uitvoer-pipe sluit). join() = wacht tot de thread stopt.
    //   3. Pas daarna de rest opruimen.
    // Zouden we meteen stop() doen, dan knipten we de uitvoer-pipe door
    // voordat de laatste output gelezen is, en zag je het antwoord nooit.
    shell.sluitInvoer();
    if (lezer.joinable()) lezer.join();
    shell.stop();

    std::cout << "\n=== demo gestopt ===\n";
    return 0;
}
