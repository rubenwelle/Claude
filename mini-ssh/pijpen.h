// =============================================================================
// pijpen.h  -  Een programma starten met twee pipes (voor de "sessie")
//
// Hier zit het idee uit SHELL-UITLEG.md in het echt: we starten een
// programma, en koppelen zijn stdin en stdout niet aan toetsenbord en
// scherm, maar aan twee pipes die de server zelf vasthoudt.
//
//      server                          kindprogramma (assistent)
//   schrijfRegel() ──── pipe 1 ────►  stdin   (std::cin)
//   leesRegel()    ◄─── pipe 2 ─────  stdout  (std::cout)
//
// Het programma dat we starten noemen we het "kind" (child process), de
// server is de "ouder" (parent). Zo heten ze ook in de documentatie.
//
// Windows en Linux doen dit heel anders, dus er staan twee versies in:
//   Windows: CreatePipe + CreateProcess
//   Linux:   pipe + fork + dup2 + exec
// Je hoeft niet elke regel te snappen; het gaat om het idee.
//
// Dit bestand wordt alleen door server.cpp gebruikt (#include "pijpen.h").
// =============================================================================

#pragma once

#include <string>

#ifdef _WIN32
    // windows.h neemt standaard de oude winsock.h (versie 1) mee. Gebruikt een
    // ander bestand óók winsock2.h (zoals minissh.h), dan botsen die twee en
    // waarschuwt de compiler: "Please include winsock2.h before windows.h".
    // WIN32_LEAN_AND_MEAN zegt tegen windows.h: laat winsock.h weg. De pipe-
    // functies (CreatePipe, CreateProcess, ...) blijven gewoon beschikbaar, en
    // nu maakt de volgorde van de #includes niet meer uit.
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <unistd.h>     // pipe, fork, dup2, execl, read, write, close
    #include <sys/wait.h>   // waitpid
    #include <csignal>      // signal, SIGPIPE
#endif

class KindProces {
public:
    KindProces() = default;

    // Een KindProces mag niet gekopieerd worden: er hoort maar één eigenaar
    // bij elk programma en elke pipe. (Anders sluiten twee kopieën dezelfde
    // pipe twee keer.)
    KindProces(const KindProces&) = delete;
    KindProces& operator=(const KindProces&) = delete;

    // De destructor ruimt automatisch op als het object ophoudt te bestaan,
    // ook als we halverwege een functie met return stoppen. Dit heet RAII:
    // "opruimen gebeurt vanzelf aan het einde van de levensduur".
    ~KindProces() { stop(); }

    // Start 'programma' met één vast argument. Let op: we gebruiken GEEN
    // shell (geen cmd.exe, geen popen). Wat de gebruiker later typt, gaat
    // alleen naar de stdin van het programma, en kan dus nooit een andere
    // opdracht starten.
    bool start(const std::string& programma, const std::string& argument);

    // Stuur één regel tekst naar de stdin van het kind (met een enter erachter).
    bool schrijfRegel(const std::string& regel);

    // Lees één regel uit de stdout van het kind (zonder de enter).
    // Geeft false als het kind gestopt is.
    bool leesRegel(std::string& regel);

    // Sluit ALLEEN onze schrijfkant (pipe 1). Het kind krijgt dan "einde
    // invoer" en stopt meestal vanzelf, terwijl wij zijn laatste uitvoer nog
    // uit pipe 2 kunnen lezen. Handig vóór stop(): eerst dit, dan de uitvoer
    // leeglezen, dan pas stop().
    void sluitInvoer();

    // Sluit de pipes en wacht tot het kind gestopt is.
    void stop();

private:
    bool gestart = false;
#ifdef _WIN32
    HANDLE naarKind  = NULL;   // onze kant van pipe 1 (wij schrijven)
    HANDLE vanKind   = NULL;   // onze kant van pipe 2 (wij lezen)
    PROCESS_INFORMATION proces{};
#else
    int naarKind = -1;
    int vanKind  = -1;
    pid_t pid    = -1;
#endif

    // Lees één byte. Simpel (en niet supersnel), maar makkelijk te volgen.
    bool leesByte(char& c);
};

// =============================================================================
// Windows-versie
// =============================================================================
#ifdef _WIN32

inline bool KindProces::start(const std::string& programma, const std::string& argument) {
    // bInheritHandle = TRUE: het kind mag deze pipe-uiteinden "erven".
    SECURITY_ATTRIBUTES sa{};
    sa.nLength        = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE kindLeest, kindSchrijft;   // de kant die het kind krijgt
    // Pipe 1: wij schrijven (naarKind), het kind leest (kindLeest = zijn stdin).
    if (!CreatePipe(&kindLeest, &naarKind, &sa, 0)) return false;
    // Pipe 2: het kind schrijft (kindSchrijft = zijn stdout), wij lezen (vanKind).
    if (!CreatePipe(&vanKind, &kindSchrijft, &sa, 0)) {
        CloseHandle(kindLeest);
        CloseHandle(naarKind);
        return false;
    }
    // Onze eigen uiteinden mag het kind NIET erven, anders gaat de pipe
    // nooit dicht (het kind zou dan zelf ook nog een "schrijfkant" hebben).
    SetHandleInformation(naarKind, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(vanKind,  HANDLE_FLAG_INHERIT, 0);

    // Vertel Windows welke kanalen het kind krijgt.
    STARTUPINFOA si{};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdInput  = kindLeest;      // stdin  = pipe 1
    si.hStdOutput = kindSchrijft;   // stdout = pipe 2
    si.hStdError  = kindSchrijft;   // stderr ook naar pipe 2

    // CreateProcess wil de opdrachtregel in een buffer die het mag aanpassen.
    // Een leeg argument betekent: start het programma zonder argument.
    std::string opdracht = argument.empty() ? programma : (programma + " " + argument);
    BOOL gelukt = CreateProcessA(
        NULL, &opdracht[0], NULL, NULL,
        TRUE,               // handles erven: ja (alleen die we toestaan)
        CREATE_NO_WINDOW,   // geen zwart venster openen
        NULL, NULL, &si, &proces);

    // De kant van het kind hebben wij niet meer nodig: die heeft het kind nu.
    CloseHandle(kindLeest);
    CloseHandle(kindSchrijft);

    if (!gelukt) {
        CloseHandle(naarKind);
        CloseHandle(vanKind);
        naarKind = vanKind = NULL;
        return false;
    }
    gestart = true;
    return true;
}

inline bool KindProces::schrijfRegel(const std::string& regel) {
    if (!gestart) return false;
    std::string data = regel + "\n";
    DWORD geschreven = 0;
    return WriteFile(naarKind, data.data(), static_cast<DWORD>(data.size()),
                     &geschreven, NULL) && geschreven == data.size();
}

inline bool KindProces::leesByte(char& c) {
    DWORD gelezen = 0;
    return ReadFile(vanKind, &c, 1, &gelezen, NULL) && gelezen == 1;
}

inline void KindProces::sluitInvoer() {
    if (naarKind != NULL) {
        CloseHandle(naarKind);   // het kind krijgt "einde invoer"
        naarKind = NULL;
    }
}

inline void KindProces::stop() {
    if (!gestart) return;
    gestart = false;
    sluitInvoer();   // pipe 1 dicht (als dat nog niet gebeurd is)
    // Geef het kind 2 seconden; daarna zetten we het zelf uit.
    if (WaitForSingleObject(proces.hProcess, 2000) == WAIT_TIMEOUT) {
        TerminateProcess(proces.hProcess, 1);
    }
    CloseHandle(vanKind);
    CloseHandle(proces.hProcess);
    CloseHandle(proces.hThread);
}

// =============================================================================
// Linux-versie
// =============================================================================
#else

inline bool KindProces::start(const std::string& programma, const std::string& argument) {
    // Als het kind stopt en wij schrijven nog naar pipe 1, stuurt Linux een
    // SIGPIPE-signaal dat de hele server zou stoppen. Dat zetten we uit;
    // write() geeft dan gewoon een foutmelding terug.
    signal(SIGPIPE, SIG_IGN);

    int pijp1[2], pijp2[2];          // [0] = leeskant, [1] = schrijfkant
    if (pipe(pijp1) != 0) return false;
    if (pipe(pijp2) != 0) {
        close(pijp1[0]); close(pijp1[1]);
        return false;
    }

    // fork() maakt een kopie van ons eigen programma. Daarna lopen er twee:
    // in het kind geeft fork() 0 terug, in de ouder het nummer van het kind.
    pid = fork();
    if (pid < 0) {
        close(pijp1[0]); close(pijp1[1]); close(pijp2[0]); close(pijp2[1]);
        return false;
    }
    if (pid == 0) {
        // --- Dit stuk draait in het KIND ---
        // dup2 zet een pipe-uiteinde op de plek van stdin (0) of stdout (1).
        dup2(pijp1[0], 0);   // stdin  = leeskant van pipe 1
        dup2(pijp2[1], 1);   // stdout = schrijfkant van pipe 2
        dup2(pijp2[1], 2);   // stderr ook
        close(pijp1[0]); close(pijp1[1]); close(pijp2[0]); close(pijp2[1]);
        // exec vervangt dit kind door het echte programma.
        // Een leeg argument betekent: start het programma zonder argument.
        if (argument.empty())
            execl(programma.c_str(), programma.c_str(), (char*)NULL);
        else
            execl(programma.c_str(), programma.c_str(), argument.c_str(), (char*)NULL);
        _exit(127);          // alleen als exec mislukt (programma niet gevonden)
    }

    // --- Dit stuk draait in de OUDER (de server) ---
    close(pijp1[0]);         // de kanten van het kind houden we niet
    close(pijp2[1]);
    naarKind = pijp1[1];
    vanKind  = pijp2[0];
    gestart  = true;
    return true;
}

inline bool KindProces::schrijfRegel(const std::string& regel) {
    if (!gestart) return false;
    std::string data = regel + "\n";
    size_t verstuurd = 0;
    while (verstuurd < data.size()) {
        ssize_t n = write(naarKind, data.data() + verstuurd, data.size() - verstuurd);
        if (n <= 0) return false;
        verstuurd += static_cast<size_t>(n);
    }
    return true;
}

inline bool KindProces::leesByte(char& c) {
    return read(vanKind, &c, 1) == 1;
}

inline void KindProces::sluitInvoer() {
    if (naarKind != -1) {
        close(naarKind);     // "einde invoer" voor het kind
        naarKind = -1;
    }
}

inline void KindProces::stop() {
    if (!gestart) return;
    gestart = false;
    sluitInvoer();           // pipe 1 dicht (als dat nog niet gebeurd is)
    close(vanKind);
    waitpid(pid, nullptr, 0);
}

#endif

// =============================================================================
// Gedeeld: een regel lezen = bytes lezen tot de enter.
// =============================================================================
inline bool KindProces::leesRegel(std::string& regel) {
    if (!gestart) return false;
    regel.clear();
    char c;
    while (leesByte(c)) {
        if (c == '\n') {
            // Windows zet "\r\n" aan het eind van een regel; de \r halen we weg.
            if (!regel.empty() && regel.back() == '\r') regel.pop_back();
            return true;
        }
        regel += c;
        if (regel.size() > 4096) return true;   // extreem lange regel: knip af
    }
    return false;   // pipe dicht: het kind is gestopt
}
