// =============================================================================
// conpty-demo.cpp  -  Een LOKALE demo: cmd.exe in een echte pseudo-console
//
// Waarvoor is dit? In cmd-pipe.cpp zag je dat een shell via "rauwe" pipes
// zich raar gedraagt: de uitvoer loopt achter, de prompt verschijnt niet
// netjes, en er zijn geen kleuren of cursorbesturing. Dat komt doordat de
// shell dan denkt dat hij NIET met een terminal praat.
//
// De oplossing die Windows daarvoor heeft is ConPTY: een "pseudo-console"
// (pseudo-terminal). Je programma doet alsof het een echt terminalvenster is.
// De shell gedraagt zich dan normaal: prompt op tijd, kleuren, de cursor die
// beweegt. Zie SHELL-UITLEG.md voor het idee; dit is dat idee, maar dan echt.
//
// BELANGRIJK - lees dit even:
//   * Dit programma praat NIET over het netwerk. Er luistert geen poort en
//     er is geen login. Het start cmd.exe alleen op JOUW computer en koppelt
//     die aan je eigen toetsenbord en scherm.
//   * Koppel een shell NOOIT aan een netwerkverbinding. Dan kan iedereen die
//     de poort bereikt alles op je computer doen. Daarom doet mini-SSH dat
//     niet, en daarom blijft deze demo strikt lokaal.
//
// ALLEEN WINDOWS: ConPTY bestaat sinds Windows 10 versie 1809 (oktober 2018).
// Op Linux werkt dit niet (daar heet het equivalent een pty, andere API).
// Bouwen op Windows (MinGW):
//     g++ -std=c++17 conpty-demo.cpp -o conpty-demo.exe
// (Op Linux compileert het tot een klein programma dat alleen uitlegt dat je
//  Windows nodig hebt.)
// =============================================================================

#ifdef _WIN32

// ConPTY vraagt om een recente Windows-API. 0x0A00 = Windows 10.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
// windows.h neemt standaard de oude winsock.h mee; die hebben we niet nodig.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <iostream>
#include <thread>

// Sommige (oudere) MinGW-headers kennen de ConPTY-namen nog niet. We lenen de
// functies daarom tijdens het draaien uit kernel32.dll (met GetProcAddress) in
// plaats van ze direct aan te roepen. Twee voordelen:
//   1. De code compileert ook met oudere headers die CreatePseudoConsole niet
//      kennen (zoals sommige MinGW-versies).
//   2. Draait het programma op een oude Windows zónder ConPTY, dan vinden we de
//      functie simpelweg niet en zeggen we dat netjes, in plaats van te crashen.
// Dit is ook hoe Microsofts eigen voorbeeld het doet. Een HPCON (de "handle"
// naar de pseudo-console) is gewoon een ondoorzichtige pointer; we gebruiken
// hier void* zodat we niet van een header-definitie afhangen.
#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

typedef HRESULT (WINAPI *CreatePseudoConsoleFn)(COORD, HANDLE, HANDLE, DWORD, void**);
typedef void    (WINAPI *ClosePseudoConsoleFn)(void*);

int main() {
    // -------------------------------------------------------------------------
    // 0. De ConPTY-functies uit kernel32.dll ophalen. Lukt dat niet, dan heeft
    //    deze Windows nog geen pseudo-console (ouder dan versie 1809).
    // -------------------------------------------------------------------------
    // (De tussenstap via void* houdt de compiler tevreden bij het omzetten van
    //  de algemene functie-pointer die GetProcAddress teruggeeft.)
    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    auto maakConsole  = reinterpret_cast<CreatePseudoConsoleFn>(
        reinterpret_cast<void*>(GetProcAddress(kernel32, "CreatePseudoConsole")));
    auto sluitConsole = reinterpret_cast<ClosePseudoConsoleFn>(
        reinterpret_cast<void*>(GetProcAddress(kernel32, "ClosePseudoConsole")));
    if (!maakConsole || !sluitConsole) {
        std::cerr << "Deze Windows heeft geen ConPTY (nodig: Windows 10 1809+).\r\n";
        return 1;
    }

    // -------------------------------------------------------------------------
    // 1. Ons eigen scherm "VT-klaar" maken. ConPTY stuurt de uitvoer als een
    //    stroom met kleur- en cursor-codes (zogeheten VT-escape-codes, net als
    //    een echte terminal). Met deze vlag laat Windows die codes netjes zien
    //    in plaats van ze als rare tekens te printen.
    // -------------------------------------------------------------------------
    HANDLE hOnzeUit = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hOnzeIn  = GetStdHandle(STD_INPUT_HANDLE);
    DWORD oudeUitModus = 0;
    GetConsoleMode(hOnzeUit, &oudeUitModus);
    SetConsoleMode(hOnzeUit, oudeUitModus | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    std::cout << "=== LOKALE ConPTY-demo met cmd.exe ===\r\n"
                 "Dit draait alleen op je eigen computer, zonder netwerk.\r\n"
                 "Je praat nu met een echte pseudo-console: let op dat de prompt\r\n"
                 "en de uitvoer wel netjes op tijd verschijnen (anders dan bij\r\n"
                 "cmd-pipe). Typ 'exit' om te stoppen.\r\n\r\n";

    // -------------------------------------------------------------------------
    // 2. Twee pipes aanmaken: één voor wat WIJ naar de console sturen (invoer)
    //    en één voor wat de console TERUGstuurt (uitvoer). De pseudo-console
    //    krijgt de andere kant van elk pipe.
    //       wij schrijven -> naarConsole ----> consoleLeest   (stdin van cmd)
    //       wij lezen   <- vanConsole   <---- consoleSchrijft (stdout van cmd)
    // -------------------------------------------------------------------------
    HANDLE naarConsole = NULL, consoleLeest = NULL;    // pipe 1 (onze invoer)
    HANDLE vanConsole  = NULL, consoleSchrijft = NULL; // pipe 2 (console-uitvoer)
    if (!CreatePipe(&consoleLeest, &naarConsole, NULL, 0) ||
        !CreatePipe(&vanConsole, &consoleSchrijft, NULL, 0)) {
        std::cerr << "Kon de pipes niet aanmaken.\r\n";
        return 1;
    }

    // -------------------------------------------------------------------------
    // 3. De pseudo-console zelf maken. We geven hem de kant van de pipes die
    //    HIJ moet gebruiken (lezen = onze invoer, schrijven = zijn uitvoer),
    //    plus een grootte in kolommen x regels.
    // -------------------------------------------------------------------------
    void* hConsole = NULL;
    COORD grootte = { 100, 30 };   // 100 kolommen breed, 30 regels hoog
    HRESULT hr = maakConsole(grootte, consoleLeest, consoleSchrijft, 0, &hConsole);
    // De console heeft nu zijn eigen kopie van die twee handles; wij hebben ze
    // zelf niet meer nodig. Niet sluiten zou betekenen dat de uitvoer-pipe nooit
    // "einde" meldt wanneer cmd stopt.
    CloseHandle(consoleLeest);
    CloseHandle(consoleSchrijft);
    if (FAILED(hr)) {
        std::cerr << "CreatePseudoConsole mislukt (heb je Windows 10 1809+?).\r\n";
        return 1;
    }

    // -------------------------------------------------------------------------
    // 4. cmd.exe starten en hém aan deze pseudo-console koppelen. Dat laatste
    //    gebeurt via een "attribuutlijst" bij het starten: we stoppen daar het
    //    PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE-attribuut in met onze console.
    // -------------------------------------------------------------------------
    STARTUPINFOEXA si{};
    si.StartupInfo.cb = sizeof(STARTUPINFOEXA);

    // Eerst uitvragen hoeveel geheugen de attribuutlijst nodig heeft (voor 1
    // attribuut), dan ruimte reserveren en de lijst initialiseren.
    SIZE_T nodig = 0;
    InitializeProcThreadAttributeList(NULL, 1, 0, &nodig);
    si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
        HeapAlloc(GetProcessHeap(), 0, nodig));
    if (!si.lpAttributeList ||
        !InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &nodig)) {
        std::cerr << "Kon de attribuutlijst niet maken.\r\n";
        sluitConsole(hConsole);
        return 1;
    }
    if (!UpdateProcThreadAttribute(si.lpAttributeList, 0,
                                   PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   hConsole, sizeof(hConsole), NULL, NULL)) {
        std::cerr << "Kon de console niet aan het proces koppelen.\r\n";
        DeleteProcThreadAttributeList(si.lpAttributeList);
        HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
        sluitConsole(hConsole);
        return 1;
    }

    PROCESS_INFORMATION pi{};
    char commando[] = "cmd.exe";   // CreateProcess mag deze buffer aanpassen
    BOOL ok = CreateProcessA(
        NULL, commando, NULL, NULL,
        FALSE,                        // handles erven: niet nodig bij ConPTY
        EXTENDED_STARTUPINFO_PRESENT, // we gebruiken de uitgebreide startupinfo
        NULL, NULL, &si.StartupInfo, &pi);
    if (!ok) {
        std::cerr << "Kon cmd.exe niet starten.\r\n";
        DeleteProcThreadAttributeList(si.lpAttributeList);
        HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
        sluitConsole(hConsole);
        return 1;
    }

    // -------------------------------------------------------------------------
    // 5. Twee richtingen tegelijk pompen, dus twee threads:
    //    - lezer-thread: alles wat de console uitvoert, byte voor byte op ons
    //      scherm. We schrijven de rauwe bytes door (inclusief de VT-codes),
    //      zodat kleuren en cursorbewegingen blijven werken.
    //    - hoofd-thread: alles wat jij typt, door naar de console-invoer.
    // -------------------------------------------------------------------------
    std::thread lezer([&]() {
        char buffer[4096];
        DWORD gelezen = 0;
        while (ReadFile(vanConsole, buffer, sizeof(buffer), &gelezen, NULL) && gelezen > 0) {
            DWORD geschreven = 0;
            WriteFile(hOnzeUit, buffer, gelezen, &geschreven, NULL);
        }
    });

    // De hoofd-thread: lees je toetsaanslagen en stuur ze naar de console.
    {
        char buffer[4096];
        DWORD gelezen = 0;
        while (ReadFile(hOnzeIn, buffer, sizeof(buffer), &gelezen, NULL) && gelezen > 0) {
            DWORD geschreven = 0;
            if (!WriteFile(naarConsole, buffer, gelezen, &geschreven, NULL)) break;
            // Is cmd intussen gestopt (bijv. na 'exit')? Dan stoppen wij ook.
            if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) break;
        }
    }

    // -------------------------------------------------------------------------
    // 6. Netjes opruimen. ClosePseudoConsole stopt de console; daarna meldt de
    //    uitvoer-pipe "einde" en stopt de lezer-thread vanzelf.
    // -------------------------------------------------------------------------
    sluitConsole(hConsole);
    if (lezer.joinable()) lezer.join();

    CloseHandle(naarConsole);
    CloseHandle(vanConsole);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    DeleteProcThreadAttributeList(si.lpAttributeList);
    HeapFree(GetProcessHeap(), 0, si.lpAttributeList);

    SetConsoleMode(hOnzeUit, oudeUitModus);   // ons scherm terugzetten
    std::cout << "\r\n=== demo gestopt ===\r\n";
    return 0;
}

#else   // ---------------------------------------------------------------------

#include <iostream>
int main() {
    std::cout <<
        "conpty-demo werkt alleen op Windows 10 versie 1809 of nieuwer.\n"
        "ConPTY (de pseudo-console-API) bestaat niet op Linux/macOS.\n"
        "Het Linux-equivalent is een pty (pseudo-terminal, zoals bij 'ssh');\n"
        "zie SHELL-UITLEG.md voor het idee. Gebruik op Linux voor nu cmd-pipe\n"
        "(rauwe pipes) om het verschil te zien.\n";
    return 0;
}

#endif
