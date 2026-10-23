// =============================================================================
// hallo.cpp  -  Een klein programma om te starten via mini-SSH
//
// Dit is een heel gewoon programma: het print een paar regels en stopt.
// De mini-SSH-server kan het starten met "start hallo", en stuurt dan alles
// wat dit programma print versleuteld terug naar de client.
//
// Pas het gerust aan, bouw het opnieuw, en kijk wat je in de client ziet.
//
// Bouwen op Windows (MinGW):  g++ hallo.cpp -o hallo.exe
// Bouwen op Linux:            g++ hallo.cpp -o hallo
// =============================================================================

#include <iostream>

int main() {
    std::cout << "Hallo! Ik ben hallo.exe, en ik draai op de server.\n";
    std::cout << "Alles wat ik print, gaat versleuteld terug naar de client.\n";

    // Een klein beetje rekenwerk, om te laten zien dat het echt een programma is.
    int som = 0;
    for (int i = 1; i <= 10; i++) {
        som += i;
    }
    std::cout << "1 + 2 + ... + 10 = " << som << "\n";
    return 0;
}
