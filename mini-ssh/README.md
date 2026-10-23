# Mini-SSH: een leerversie van SSH in C++

> **Dit is speelgoed om te leren, geen echte SSH.**
> Het draait alleen op je eigen computer (`127.0.0.1`), de versleuteling is
> met opzet simpel gehouden en dus **niet veilig**, en de server voert geen
> echte opdrachten op je computer uit. Gebruik het nooit om echt iets te
> beveiligen. Voor echt werk gebruik je de echte `ssh` (zit in Windows 10/11).

Dit bouwt verder op het hello-world voorbeeld in de map erboven. Een client
maakt verbinding met een server, ze spreken samen een geheime sleutel af,
alles daarna is versleuteld, je logt in, en je kunt commando's sturen waar de
server op antwoordt.

Hoe echte SSH werkt staat stap voor stap uitgelegd in
**[SSH-UITLEG.md](SSH-UITLEG.md)**. Lees die eerst, of houd hem ernaast
terwijl je de code leest.

## Bestanden

| Bestand          | Wat het doet |
|------------------|--------------|
| `pijpen.h`       | Een programma starten met twee pipes (gebruikt door `sessie` en `cmd-pipe`) |
| `cmd-pipe.cpp`   | LOKALE demo (geen netwerk): een echte shell via pipes, om het verschil te voelen |
| `minissh.h`      | Gedeelde code: berichten (frames) versturen, Diffie-Hellman, de XOR-versleuteling |
| `server.cpp`     | Luistert op 127.0.0.1:2222, doet de handshake, controleert het wachtwoord, beantwoordt commando's |
| `client.cpp`     | Verbindt met de server, doet de handshake, logt in, laat jou commando's typen |
| `agenda.txt`     | Voorbeeld-agenda die de server voorleest met het commando `agenda` |
| `hallo.cpp`      | Een klein eigen programma dat de server kan starten met `start hallo` |
| `assistent.cpp`  | Een programma met acties: leest je agenda of je mail (`start assistent mail`) |
| `mail.txt`       | Een fictieve mailbox voor de assistent |
| `gedeeld/`       | Voorbeeldmap die de assistent mag laten zien (`start assistent map`) |
| `SSH-UITLEG.md`  | Uitleg over hoe SSH echt werkt, in gewone taal |
| `SHELL-UITLEG.md` | Hoe echte SSH een command prompt aan de verbinding koppelt (pipes, pty) |

## Wat er gebeurt

```
client                                         server
  |  ---- TCP connect (127.0.0.1:2222) ---->     |   stap 1
  |  <--- "MINISSH-1.0 server" -------------     |   stap 2  (leesbaar)
  |  ---- "MINISSH-1.0 client" ------------>     |
  |  <--- A = G^a mod P --------------------     |   stap 3  (leesbaar, maar
  |  ---- B = G^b mod P ------------------->     |            geeft geheim niet prijs)
  |      allebei: geheim = G^(a*b) mod P         |
  |  ==== "AUTH ruben geheim" =============>     |   stap 5  (versleuteld)
  |  <=== "AUTH_OK" ========================     |
  |  ==== "tijd" ===========================>    |   stap 6  (versleuteld)
  |  <=== "Servertijd: 23-10-2026 14:20" ===     |
```

(Stap 4, "vanaf nu versleutelen", is geen bericht maar iets dat beide kanten
zelf doen.)

## Bouwen op Windows (MinGW)

Open een opdrachtprompt in de map `mini-ssh` en typ:

```
g++ server.cpp -o server.exe -lws2_32
g++ client.cpp -o client.exe -lws2_32
g++ hallo.cpp -o hallo.exe
g++ -std=c++17 assistent.cpp -o assistent.exe
```

Je hoeft `minissh.h` en `pijpen.h` niet apart te bouwen; dat wordt via `#include`
automatisch meegenomen.

Op Linux of macOS laat je `-lws2_32` en `.exe` weg.

## Starten

1. Start in het eerste venster de server: `server.exe`. Doe dat vanuit de map
   `mini-ssh`, want daar zoekt de server naar `agenda.txt`.
2. Start in een tweede venster de client: `client.exe`
3. Log in met gebruikersnaam **ruben** en wachtwoord **geheim**.
4. Typ `help` om de commando's te zien, `agenda` om je agenda te lezen, of
   `start hallo` om een programma te starten.

Krijg je een melding van Windows Firewall? Dan mag je op "Annuleren" klikken:
de server luistert alleen op je eigen computer, dus hij heeft geen toegang tot
het netwerk nodig.

Zo ziet het eruit in de client:

```
Verbonden met 127.0.0.1:2222
Server zegt: MINISSH-1.0 server
Sleuteluitwisseling klaar.
  gedeeld geheim = 1687379724907436599   (vergelijk met de server: hetzelfde getal!)

Gebruikersnaam: ruben
Wachtwoord: geheim
  [over het netwerk: bf 8b e6 e6 5d 8f 4e 2e f8 52 f8 c6 d0 ea 45 b7 d6 ]
Ingelogd! Typ 'help' voor de commando's.

mini-ssh> echo hallo wereld
  [over het netwerk: b5 ce c6 39 ca ab 4f 12 84 00 34 0b a0 d7 46 54 18 ]
hallo wereld
mini-ssh> tijd
  [over het netwerk: 0d fd 94 3c ]
Servertijd: 23-10-2026 14:20:54
mini-ssh> exit
  [over het netwerk: bd 66 a7 8e ]
Tot ziens!
```

De regels `[over het netwerk: ...]` laten zien wat een afluisteraar zou zien:
alleen onleesbare bytes, ook van je wachtwoord. Wil je ze niet zien, zet dan
`TOON_VERSLEUTELD` in `minissh.h` op `false`. Het gedeelde geheim is elke keer
anders, omdat beide kanten elke keer nieuwe geheime getallen kiezen.

## Waarom is dit niet veilig?

Kort samengevat (de lange versie staat in de uitleg):

- **Te kleine getallen.** Diffie-Hellman met 61 bits is te kraken. Echte SSH
  gebruikt curve25519 of minstens 2048 bits.
- **Zelfgemaakte versleuteling.** De XOR-stroom komt uit een simpele
  random-generator die voorspelbaar is. Echte SSH gebruikt ChaCha20 of AES.
  Gouden regel in de echte wereld: maak nooit je eigen crypto, gebruik een
  bekende bibliotheek.
- **Geen host key.** De client controleert niet of hij met de echte server
  praat, dus iemand ertussen kan meelezen.
- **Geen controle op knoeien (MAC).** Berichten kunnen onderweg veranderd
  worden zonder dat iemand het merkt.
- **Wachtwoord in de code.** Een echte server bewaart alleen een hash.

## Zelf een commando toevoegen

Alle commando's zitten in één functie in `server.cpp`: `voerCommandoUit()`.
Zo werkt die:

1. De client stuurt een regel tekst, bijvoorbeeld `echo hallo wereld`.
2. De server splitst die in het **commando** (het eerste woord, `echo`) en de
   **rest** (`hallo wereld`).
3. Daarna volgt een rij `if`'s: `if (commando == "help")`,
   `if (commando == "echo")`, enzovoort.
4. Wat de functie teruggeeft met `return`, stuurt de server versleuteld naar
   de client, en de client print het.

Een nieuw commando is dus één nieuw `if`-blok. Bijvoorbeeld:

```cpp
if (commando == "hoi") {
    return "Hoi " + gebruiker + "!";
}
```

Zet het tussen de andere `if`'s, vóór de laatste regel ("Onbekend
commando"), voeg een regel toe aan de help-tekst, en compileer de server
opnieuw. De client hoef je niet aan te passen: die stuurt gewoon door wat je
typt.

### Voorbeeld: het commando `agenda`

`agenda` is zo'n zelfgemaakt commando. Het roept de functie `leesAgenda()` aan,
die het bestand `agenda.txt` opent met `std::ifstream`, het regel voor regel
leest, en de hele inhoud als één tekst teruggeeft. Bestaat het bestand niet,
dan krijg je een nette melding in plaats van een crash.

Je kunt `agenda.txt` gewoon in Kladblok aanpassen. Je hoeft de server daarna
niet opnieuw te starten: hij leest het bestand elke keer opnieuw als je
`agenda` typt.

Let op de bestandsnaam: die staat vast in de code (`AGENDA_BESTAND`). Je zou
het commando ook `lees <bestandsnaam>` kunnen maken, maar dan kan iedereen die
kan inloggen elk bestand op de server opvragen, bijvoorbeeld met
`lees ../../wachtwoorden.txt`. Een vaste naam is veiliger. Echte servers lossen
dit op met gebruikersrechten: je mag alleen de bestanden lezen waar je
account toegang toe heeft.

## Programma's starten met `start`

Met `start <naam>` laat je de server een programma draaien, en zie je in de
client alles wat dat programma print. Typ `programmas` om te zien welke namen
mogen:

```
mini-ssh> programmas
Programma's die je mag starten met 'start <naam>':
  bestanden   (dir /b)
  datum   (date /t)
  hallo   (hallo.exe)
  systeem   (ver)
mini-ssh> start hallo
Hallo! Ik ben hallo.exe, en ik draai op de server.
Alles wat ik print, gaat versleuteld terug naar de client.
1 + 2 + ... + 10 = 55
```

### Hoe werkt het?

De functie `startProgramma()` in `server.cpp` gebruikt `popen()` (op Windows
heet die `_popen()`). Die start een programma en geeft je een `FILE*` terug,
precies zoals `fopen()` in C. Het verschil: je leest niet uit een bestand,
maar uit wat het programma print. Met `fgets()` lees je die output stukje voor
stukje, en `pclose()` wacht tot het programma klaar is. De server plakt alle
output aan elkaar en stuurt hem versleuteld naar de client.

### Waarom een vaste lijst?

Je zou ook alles kunnen uitvoeren wat de client stuurt. Maar dan kan iedereen
die kan inloggen ook `del /s /q C:\*` sturen, en die opdracht voert de server
dan gewoon uit. Dat heet **command injection**. Daarom zoekt de server de naam
op in een vaste lijst, `TOEGESTANE_PROGRAMMAS`. De client stuurt alleen een
naam, en kan nooit zelf een opdracht in elkaar zetten: `start datum & del ...`
wordt geweigerd, omdat `datum & del ...` niet op de lijst staat.

Echte SSH kent zo'n lijst niet: je krijgt een volledige shell, en de
beveiliging zit in je gebruikersaccount. Je kunt alleen doen wat dat account
mag.

### Zelf een programma toevoegen

1. Schrijf en bouw je programma, bijvoorbeeld `g++ mijnprog.cpp -o mijnprog.exe`.
2. Zet het `.exe`-bestand in de map `mini-ssh`, waar ook de server draait.
3. Voeg in `server.cpp` een regel toe aan `TOEGESTANE_PROGRAMMAS`:
   `{ "mijnprog", "mijnprog.exe" },`
4. Bouw de server opnieuw en start hem opnieuw.

Je programma mag niet om invoer vragen (zoals `std::cin`), want er zit
niemand aan het toetsenbord van de server. De server wacht dan voor altijd.

## Een programma met acties: de assistent

`assistent.exe` is een programma dat verschillende dingen kan doen. Welke
actie, dat geef je mee als **argument** achter de naam:

```
mini-ssh> start assistent mail
Je hebt 3 berichten:
  1. Verjaardag zondag   (van Opa)
  2. Training gaat niet door   (van Voetbalclub)
  3. Hoe gaat het met C++?   (van Lisa)
Lees een bericht met: start assistent mail <nummer>
mini-ssh> start assistent mail 3
Van:       Lisa
Onderwerp: Hoe gaat het met C++?
...
mini-ssh> start assistent agenda
Agenda van Ruben
...
```

De mail is fictief: hij staat gewoon in `mail.txt`. Pas dat bestand gerust
aan. Berichten worden gescheiden door een regel met alleen `---`.

### Hoe komen de argumenten bij het programma?

1. Jij typt `start assistent mail 3`.
2. De server splitst dat in de naam `assistent` en de argumenten `mail` en `3`.
3. De server zoekt `assistent` op in de vaste lijst, en maakt er de opdracht
   `assistent.exe mail 3` van.
4. `popen()` start die opdracht. Het programma krijgt de woorden binnen in
   `main(int argc, char* argv[])`, precies zoals in C: `argv[1]` is `"mail"`
   en `argv[2]` is `"3"`.
5. Het programma kiest met een `if` welke actie het uitvoert, en print het
   resultaat. Dat gaat versleuteld terug naar jou.

Je kunt de assistent ook zonder server testen: typ in een opdrachtprompt
`assistent.exe mail 3`.

### Argumenten veilig doorgeven

Dit is het lastige deel. `popen()` geeft de opdracht aan de shell (`cmd` op
Windows), en de shell kent speciale tekens:

| Teken | Betekenis voor de shell |
|-------|-------------------------|
| `&`   | doe daarna nog een opdracht |
| `>`   | schrijf de output naar een bestand (en overschrijf het!) |
| `\|`  | stuur de output door naar een ander programma |

Als de server zomaar achter de opdracht plakt wat jij typt, dan maakt
`start assistent mail & del agenda.txt` er `assistent.exe mail & del agenda.txt`
van, en voert cmd **twee** opdrachten uit. Weg agenda.

Daarom controleert de server elk argument met `isVeiligArgument()`: alleen
letters en cijfers mogen, maximaal 32 tekens, en maximaal 3 argumenten.
Daar zit geen enkel speciaal teken tussen, dus de shell kan er niets anders
van maken. Let op het principe: we zeggen wat **mag** (letters en cijfers),
niet wat **niet** mag. Een lijst met verboden tekens is gevaarlijk, want je
vergeet er altijd één.

En de assistent controleert zelf ook nog eens alles: alleen de acties `agenda`
en `mail`, alleen de vaste bestanden `agenda.txt` en `mail.txt`, en een
mailnummer moet echt bestaan. Twee keer controleren (op de server én in het
programma) heet **defense in depth**: als één controle een fout bevat, houdt
de andere het nog tegen.

## Een map bekijken: `start assistent map`

De assistent kan ook laten zien wat er in een map staat, maar alleen in de
map `gedeeld` en de mappen daarin:

```
mini-ssh> start assistent map
Inhoud van gedeeld/
  [map]  fotos
  [map]  school
         boodschappen.txt  (36 bytes)
         ideeen.txt  (71 bytes)
Kijk in een map met: start assistent map <naam>
mini-ssh> start assistent map fotos
Inhoud van gedeeld/fotos/
         vakantie-2026.txt  (37 bytes)
         voetbal.txt  (38 bytes)
```

Zet gerust je eigen bestanden en mappen in `gedeeld`, dan zie je ze meteen.

### Hoe werkt het?

De functie `toonMap()` in `assistent.cpp` gebruikt **`std::filesystem`**, het
deel van C++ (sinds C++17) voor mappen en bestanden. Daarom bouw je de
assistent met `-std=c++17`. De belangrijkste onderdelen:

| Code | Wat het doet |
|------|--------------|
| `fs::path pad = "gedeeld";` | een pad naar een map of bestand |
| `basis / "fotos"` | plakt stukken pad aan elkaar: `gedeeld/fotos` |
| `fs::exists(pad)` | bestaat het? |
| `fs::is_directory(pad)` | is het een map (en geen bestand)? |
| `fs::directory_iterator(pad)` | loop door alles wat in de map staat |
| `item.file_size()` | grootte van een bestand in bytes |

De namen worden eerst in een `std::vector` verzameld en met `std::sort` op
alfabet gezet, omdat `directory_iterator` ze in een willekeurige volgorde geeft.

### Waarom alleen de map `gedeeld`?

In een pad betekent `..` "de map erboven". Zonder controle zou
`start assistent map ../../..` uit `gedeeld` klimmen, en dan kan iemand je hele
schijf bekijken. Dat heet **path traversal**, en het is een van de meest
gemaakte beveiligingsfouten in servers.

Er zijn daarom drie controles achter elkaar:

1. **De server** laat in argumenten alleen letters en cijfers door, dus `..`
   en `/` komen er niet eens door.
2. **De assistent** controleert de mapnaam zelf nog een keer met
   `isVeiligeMapnaam()`.
3. **De assistent** vraagt met `fs::canonical()` het echte, volledige pad op,
   en kijkt of dat nog steeds binnen `gedeeld` ligt. Dit vangt ook trucjes
   die niet met `..` werken, zoals een snelkoppeling in `gedeeld` die naar
   `C:\` wijst.

Elke controle zou in zijn eentje al genoeg moeten zijn. Maar als je er later
per ongeluk één verandert, houden de andere het nog tegen.

## Een gesprek via pipes: `sessie`

Met `start` draait een programma één keer en stopt het weer. Met `sessie`
blijft de assistent draaien, en praat je ermee alsof het een kleine command
prompt is:

```
mini-ssh> sessie
Hoi! Ik ben de assistent, en ik praat nu via pipes met de server.
Typ: agenda, mail, mail <nr>, map of map <naam>. Typ 'stop' om te stoppen.
mini-ssh> mail 1
Van:       Opa
Onderwerp: Verjaardag zondag
...
mini-ssh> map fotos
Inhoud van gedeeld/fotos/
...
mini-ssh> stop
Sessie gestopt. Je bent terug bij mini-ssh.
```

Dit is precies het idee uit [SHELL-UITLEG.md](SHELL-UITLEG.md), maar dan veilig.

### Hoe werkt het?

1. Je typt `sessie`. De server start `assistent.exe interactief` met de klasse
   `KindProces` uit `pijpen.h`. Die maakt twee pipes en koppelt ze aan de stdin
   en stdout van de assistent (op Windows met `CreatePipe` en `CreateProcess`).
2. De assistent draait nu in een lus: `std::getline(std::cin, ...)`, opdracht
   uitvoeren, antwoord printen met `std::cout`. Hij weet niet eens dat er een
   server tussen zit.
3. De server is een **doorgeefluik**. Wat jij typt, ontsleutelt hij en schrijft
   hij in pipe 1 (`schrijfRegel`). Wat de assistent print, leest hij uit pipe 2
   (`leesRegel`), versleutelt hij, en stuurt hij naar jou.
4. Met `stop` sluit de server pipe 1. De assistent krijgt "einde invoer", stopt,
   en je bent terug bij de gewone mini-ssh-commando's.

### Twee problemen die je onderweg tegenkomt

**Wanneer is een antwoord klaar?** De server leest regel voor regel, maar het
ene antwoord is 1 regel en het andere 15. Daarom print de assistent na elk
antwoord een vaste regel `<<KLAAR>>`. Herken je het? Het is hetzelfde probleem
als bij TCP, waar we het met frames oplossen.

**Waarom komt er niets aan?** Als stdout een pipe is in plaats van een scherm,
bewaart C++ wat je print in een buffer en stuurt het pas als de buffer vol is.
Dan wacht de server voor altijd. Daarom eindigt de assistent elk antwoord met
`std::endl`: dat is een enter én het legen van de buffer ("flush"). Haal die
`std::endl` maar eens weg en kijk wat er gebeurt.

### Waarom is dit veilig, en een echte shell niet?

De server start een **vast** programma met een **vast** argument, en
**zonder shell**: `CreateProcess` start `assistent.exe` direct, er komt geen
`cmd.exe` aan te pas. Alles wat je in de sessie typt, komt alleen bij de stdin
van de assistent terecht, en die kent alleen `agenda`, `mail` en `map`. Met
`cmd.exe` op die plek zou je alles kunnen wat je account kan.

### Verschil met echte SSH

- Echte SSH gebruikt **threads** of `select()`, omdat een shell op elk moment
  iets kan printen (denk aan een programma dat elke seconde iets laat zien).
  Wij kunnen zonder, omdat de assistent alleen antwoordt als jij iets vraagt,
  en altijd eindigt met `<<KLAAR>>`.
- Echte SSH gebruikt een **pseudo-terminal** (ConPTY op Windows), zodat
  kleuren, pijltjestoetsen en Ctrl+C werken. Wij gebruiken gewone pipes.

## Het verschil voelen: `cmd-pipe` (een echte shell)

De `sessie` praat met de assistent, een net programma dat precies antwoordt
wat jij vraagt. Maar hoe voelt het met een *echte* shell, zoals `cmd.exe`?
Daarvoor is er een los demo-programma, `cmd-pipe`.

> **Belangrijk:** `cmd-pipe` praat **niet** over het netwerk. Er luistert geen
> poort en er is geen login. Het start de shell alleen op jouw eigen computer
> en koppelt die aan je eigen toetsenbord en scherm. Daarom is het geen remote
> shell en geen backdoor. En precies daarom koppelen we een echte shell
> **nooit** aan de mini-SSH-server: zodra een shell aan een netwerkpoort hangt,
> kan iedereen die de poort bereikt alles doen wat jouw account kan.

Bouwen en starten:

```
g++ -std=c++17 cmd-pipe.cpp -o cmd-pipe.exe      (Windows)
g++ -std=c++17 -pthread cmd-pipe.cpp -o cmd-pipe  (Linux)
cmd-pipe.exe
```

Typ dan bijvoorbeeld `dir` (Windows) of `ls` (Linux), en `exit` om te stoppen.

### Wat je merkt, en waarom

Vergelijk het met de assistent-sessie, en let op deze dingen:

- **Een prompt en "echo".** `cmd.exe` print zijn eigen prompt (`C:\>`) en op
  Windows zie je ook je eigen getypte regel terug. De assistent deed dat niet.
  Een shell is gebouwd om met een mens te praten; de assistent met een
  programma.
- **Geen `<<KLAAR>>`.** De assistent zei na elk antwoord netjes "ik ben klaar".
  Een shell doet dat niet. Daarom weet een echte SSH-server nooit zeker wanneer
  een antwoord "af" is; hij stuurt gewoon door wat binnenkomt, zodra het
  binnenkomt. In `cmd-pipe` zie je dat: de uitvoer verschijnt in stukjes, niet
  als één keurig blok.
- **Twee dingen tegelijk.** Omdat de shell op elk moment iets kan printen
  (ook zonder dat jij iets vroeg), heeft `cmd-pipe` een aparte **thread** die
  de hele tijd leest. Bij de assistent kon de server simpel "vraag, dan
  antwoord" doen; bij een shell moet lezen en schrijven tegelijk.
- **Buffering.** Soms lijkt het of er niets gebeurt, en dan komt er ineens een
  lap tekst. Dat is de buffer. De assistent loste dat op met `std::endl`
  (flush); een shell die niet weet dat hij naar een pipe praat, doet dat niet
  altijd.
- **Rare dingen zonder pseudo-terminal.** Pijltjestoetsen, Tab, kleuren en
  Ctrl+C werken niet of raar. Dat komt doordat de shell merkt dat hij niet in
  een echte terminal draait (geen "tty"), maar in een kale pipe. Dit is precies
  het probleem dat **ConPTY** (Windows) en een **pty** (Linux) oplossen, en de
  reden dat echte SSH die gebruikt. Zie [SHELL-UITLEG.md](SHELL-UITLEG.md).
- **Op Linux: geen prompt.** Start je `/bin/sh` via een pipe, dan zie je zelfs
  helemaal geen prompt, want `sh` zet die alleen aan voor een echte terminal.
  Nog een bewijs dat programma's zich anders gedragen tegenover een pipe.

Kortom: de assistent is makkelijk netjes door de pipe te praten omdat hij
meewerkt. Een echte shell doet dat niet, en daarom moet een echte SSH-server
veel meer werk doen (threads, een pseudo-terminal, omgaan met losse stukjes).
En omdat die shell zoveel macht geeft, zit hij achter sterke versleuteling en
een goede login, en hangt hij nooit zomaar aan een open netwerkpoort.

## Dingen om zelf te proberen

- Voeg een eigen commando toe, bijvoorbeeld `hoofdletters <tekst>` dat de
  tekst in hoofdletters terugstuurt (tip: `std::toupper` uit `<cctype>`).
- Pas `hallo.cpp` aan, bouw het opnieuw en typ `start hallo`. Je hoeft de
  server daarvoor niet opnieuw te starten. Waarom niet?
- Maak `start assistent lees <map> <bestand>` die een tekstbestand uit
  `gedeeld` laat zien. Welke controles heb je dan nodig? (Tip: een
  bestandsnaam heeft een punt, zoals `ideeen.txt`. Hoe ga je daarmee om?)
- Voeg in `interactief()` in `assistent.cpp` een opdracht `tijd` toe die
  alleen in de sessie werkt.
- Geef de assistent een nieuwe actie, bijvoorbeeld `start assistent tel`
  die telt hoeveel afspraken en mails je hebt.
- Maak `agenda vandaag`: laat alleen de regels zien waar de dag van vandaag in
  staat (tip: zoek met `regel.find(...)`).
- Verander in `client.cpp` het label `"client->server"` in iets anders.
  Wat gebeurt er dan met wat de server ontvangt? Waarom?
- Kijk met Wireshark mee op de loopback-adapter (filter `tcp.port == 2222`)
  en zoek de versie-tekst en de getallen A en B op. Typ `agenda` en kijk of je
  iets van je afspraken terugziet. (Spoiler: nee.)
