# Hoe koppelt SSH een command prompt aan de verbinding?

Als je met echte SSH inlogt, krijg je een command prompt van de andere
computer. Je typt `dir`, en je ziet de bestanden van de server. Hoe werkt
dat? Het antwoord bestaat uit drie ideeën: **standaard-kanalen**, **pipes**
en een **pseudo-terminal**. Daarna kijken we naar waarom dit zo'n gevoelig
stuk is.

---

## 1. Elk programma heeft drie "kanalen"

Elk programma dat start, krijgt automatisch drie kanalen mee:

| Kanaal | Nummer | In C++ | Normaal verbonden met |
|--------|--------|--------|-----------------------|
| **stdin** (standaard invoer) | 0 | `std::cin` | je toetsenbord |
| **stdout** (standaard uitvoer) | 1 | `std::cout` | je scherm |
| **stderr** (foutmeldingen) | 2 | `std::cerr` | je scherm |

Het belangrijke inzicht: een programma als `cmd.exe` **weet niet** waar die
kanalen echt naartoe gaan. Het leest gewoon van stdin en schrijft naar stdout.
Of daar een toetsenbord en scherm aan hangen, of een bestand, of een ander
programma, maakt het programma niet uit.

Dat heb je al gebruikt zonder het te weten. In mini-SSH schrijft de server
`2>&1` achter een opdracht: "stuur kanaal 2 (fouten) naar dezelfde plek als
kanaal 1 (uitvoer)".

## 2. Pipes: een buis tussen twee programma's

Een **pipe** is een buis met twee uiteinden. Wat je er aan de ene kant in
schrijft, komt er aan de andere kant uit. Het besturingssysteem regelt dat.

Je kent het misschien van de command prompt:

```
dir | sort
```

De `|` maakt een pipe: de stdout van `dir` wordt de stdin van `sort`.

En dit is precies wat `popen()` in mini-SSH doet. Bij `popen("hallo.exe", "r")`
maakt het besturingssysteem een pipe, start `hallo.exe`, en koppelt de
stdout van `hallo.exe` aan de pipe. De server leest aan de andere kant met
`fgets()`. Daarom komt alles wat `hallo.exe` met `std::cout` print bij de
server terecht.

Maar `popen` heeft twee beperkingen:

- Het werkt maar **één kant op**: lezen (`"r"`) óf schrijven (`"w"`), niet
  allebei.
- Het programma draait één keer en stopt dan. Voor een command prompt wil je
  juist dat het blijft draaien en dat je steeds nieuwe opdrachten kunt typen.

## 3. Wat een SSH-server doet: twee pipes en een doorgeefluik

Voor een echte shell maakt de server **twee pipes** (of eigenlijk drie, voor
stderr):

```
                 SERVER
   netwerk  ┌──────────────────┐   pipe 1    ┌─────────┐
 ─────────► │ ontsleutelen ────┼───────────► │ stdin   │
            │                  │             │         │
            │                  │   pipe 2    │ cmd.exe │
 ◄───────── │ versleutelen ◄───┼──────────── │ stdout  │
            └──────────────────┘             └─────────┘
```

1. De server maakt pipe 1 en pipe 2.
2. De server start `cmd.exe`, en zegt tegen het besturingssysteem: "geef dit
   programma niet het toetsenbord en het scherm, maar deze pipes".
3. Daarna doet de server de hele tijd twee dingen tegelijk:
   - **Van het netwerk naar de shell:** bytes van de client ontvangen,
     ontsleutelen, en in pipe 1 schrijven. Voor `cmd.exe` lijkt het alsof
     iemand typt.
   - **Van de shell naar het netwerk:** lezen wat `cmd.exe` in pipe 2
     schrijft, versleutelen, en naar de client sturen.
4. De client doet het spiegelbeeld: wat jij typt gaat het netwerk op, en wat
   binnenkomt zet hij op jouw scherm.

De server is dus eigenlijk een **doorgeefluik**: hij begrijpt zelf niets van
`dir` of `cd`. Hij schuift alleen bytes heen en weer, en versleutelt ze
onderweg.

"Twee dingen tegelijk" is het lastige deel. Als de server staat te wachten op
de client (`recv`), kan hij niet tegelijk de output van `cmd.exe` lezen. Daarom
gebruiken servers **threads** (twee stukjes programma die tegelijk lopen, in
C++ met `std::thread`) of een functie als `select()` die wacht tot er
*ergens* iets binnenkomt.

### Hoe dat er op Windows uitziet (schets)

Op Windows start je een programma met gekoppelde pipes via `CreatePipe` en
`CreateProcess`. Dit is een **schets** om het idee te zien, geen complete code:

```cpp
// Twee pipes maken. Elke pipe heeft een lees-kant en een schrijf-kant.
HANDLE shellIn_lees, shellIn_schrijf;    // pipe 1: server -> stdin van cmd
HANDLE shellUit_lees, shellUit_schrijf;  // pipe 2: stdout van cmd -> server
CreatePipe(&shellIn_lees,  &shellIn_schrijf,  &beveiliging, 0);
CreatePipe(&shellUit_lees, &shellUit_schrijf, &beveiliging, 0);

// Zeg tegen Windows: geef het nieuwe programma DEZE kanalen.
STARTUPINFOA si = {};
si.cb         = sizeof(si);
si.dwFlags    = STARTF_USESTDHANDLES;
si.hStdInput  = shellIn_lees;       // cmd leest van pipe 1
si.hStdOutput = shellUit_schrijf;   // cmd schrijft naar pipe 2
si.hStdError  = shellUit_schrijf;   // fouten ook naar pipe 2 (net als 2>&1)

PROCESS_INFORMATION pi = {};
CreateProcessA(NULL, (LPSTR)"cmd.exe", NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);

// Vanaf nu:
//   WriteFile(shellIn_schrijf, ...)  = "typen" in cmd
//   ReadFile(shellUit_lees, ...)     = lezen wat cmd print
```

Op Linux heet hetzelfde `pipe()`, `fork()`, `dup2()` en `exec()`. Het idee is
precies hetzelfde.

## 4. Waarom pipes alleen niet genoeg zijn: de pseudo-terminal

Als je dit bouwt met alleen pipes, werkt het, maar een beetje vreemd:

- Er komen geen **kleuren**, en soms zie je de **prompt** (`C:\>`) niet.
- **Pijltjestoetsen** en **Tab** voor aanvullen doen niets.
- **Ctrl+C** stopt niet het programma op de server, maar je client.
- Bij een **wachtwoordvraag** zie je je wachtwoord gewoon staan.
- Programma's weten niet **hoe groot je venster** is.

Dat komt doordat veel programma's controleren of ze met een echte terminal
praten (in C heet dat `isatty()`). Is het een pipe, dan denken ze: "ik schrijf
naar een bestand of een ander programma", en laten ze al die extra's weg.

De oplossing is een **pseudo-terminal**: iets wat het besturingssysteem levert
en wat er voor het programma uitziet als een echt terminalvenster, maar
waarvan de "toetsen" en het "scherm" eigenlijk in de handen van de SSH-server
zijn.

- Op **Linux** heet dat een **pty** (de bestanden in `/dev/pts/`, functies
  zoals `openpty()` en `forkpty()`).
- Op **Windows** heet dat **ConPTY** (functie `CreatePseudoConsole()`, sinds
  Windows 10 versie 1809). Daarvoor was SSH op Windows echt lastig.

Daarom stuurt een SSH-client, voordat hij om een shell vraagt, eerst een
verzoek "geef me een pseudo-terminal, ik ben type `xterm`, mijn venster is
120 bij 30". Als je je venster groter maakt, stuurt de client een berichtje
`window-change` zodat de server het doorgeeft.

## 5. De berichten in het echte SSH-protocol

Na het inloggen praten client en server over **kanalen**. Voor een shell gaat
het ongeveer zo:

| Client stuurt | Betekenis |
|---------------|-----------|
| `channel-open "session"` | "ik wil een sessie openen" |
| `pty-req` | "geef me een pseudo-terminal (type, venstergrootte)" |
| `shell` | "start mijn standaard-shell" |
| *of* `exec "dir"` | "voer alleen deze ene opdracht uit en stop" |
| `data` | wat de gebruiker typt (heen) en wat de shell print (terug) |
| `window-change` | "mijn venster is nu zo groot" |

De server stuurt aan het eind `exit-status` (de exit code) en sluit het kanaal.

Valt je iets op? `exec` is precies wat jouw `start`-commando doet: één
programma, output terug, klaar. En `ssh gebruiker@server dir` gebruikt
inderdaad `exec`.

## 6. Waarom dit het gevoeligste stuk van SSH is

Een gekoppelde shell betekent: **wie hem heeft, kan alles wat het
gebruikersaccount kan.** Bestanden lezen, wissen, programma's installeren.
Daarom:

- Krijg je de shell bij echte SSH pas **na** sterke versleuteling, een
  gecontroleerde host key, en een geslaagde login, het liefst met een
  sleutelpaar.
- Draait de shell **als de gebruiker die inlogt**, met precies diens rechten.
  De beveiliging zit dus niet in een lijstje toegestane opdrachten, maar in
  wat dat account op de computer mag.
- Houden beheerders nauwkeurig bij wie mag inloggen, en schakelen ze inloggen
  met alleen een wachtwoord vaak uit.

En daarom zit dit **niet** in mini-SSH. De versleuteling van mini-SSH is met
opzet zwak, het wachtwoord staat in de code, en er is geen host key. Een open
shell daaraan koppelen zou betekenen dat elk programma op je computer dat met
poort 2222 praat, je hele account in handen krijgt. De vaste lijst met
programma's is juist de veilige middenweg: je ziet het idee (programma
starten, output terug, versleuteld), zonder de deur helemaal open te zetten.

## 7. Probeer het zelf: `sessie`

In mini-SSH zit een veilige versie hiervan. Typ `sessie`, en de server start
de assistent met twee pipes (zie `pijpen.h`), precies zoals hierboven, maar
met de assistent in plaats van `cmd.exe`. Hoe het werkt staat in de README,
onder "Een gesprek via pipes".

---

## Samengevat

1. Elk programma heeft stdin, stdout en stderr, en weet niet waar die naartoe
   gaan.
2. Een SSH-server start de shell met **pipes** in plaats van toetsenbord en
   scherm.
3. Daarna is de server een **doorgeefluik**: netwerk → ontsleutelen → stdin,
   en stdout → versleutelen → netwerk, steeds in beide richtingen tegelijk.
4. Een **pseudo-terminal** (pty of ConPTY) zorgt dat de shell denkt dat hij in
   een echt venster draait, met kleuren, pijltjestoetsen en Ctrl+C.
5. Dit is het machtigste en dus gevoeligste stuk, en daarom komt het pas na
   sterke authenticatie.
