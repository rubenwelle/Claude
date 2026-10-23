# Hello world: client en server in C++

Een minimaal voorbeeld om te leren hoe client/server-programmeren werkt.
De **server** wacht op een verbinding, de **client** maakt verbinding en
stuurt `hello world`, en de server stuurt een antwoord terug.

```
client.exe  ── "hello world" ──▶  server.exe
client.exe  ◀── "Server zegt: ik heb 'hello world' ontvangen!" ──  server.exe
```

## Bestanden

| Bestand      | Wat het doet                                              |
|--------------|-----------------------------------------------------------|
| `server.cpp` | Luistert op poort 5000, leest een bericht, stuurt antwoord |
| `client.cpp` | Verbindt met 127.0.0.1:5000, stuurt `hello world`, print antwoord |

## De begrippen in gewone taal

**Client en server.** Een server is een programma dat *wacht* tot iemand iets
vraagt. Een client is het programma dat de vraag *stelt*. Je browser is een
client, de website waar je naartoe gaat draait op een server.

**IP-adres en poort.** Een IP-adres wijst een computer aan, een poort wijst
een programma op die computer aan. Vergelijk het met een flatgebouw (IP-adres)
en een huisnummer (poort). `127.0.0.1` (ook wel `localhost`) betekent altijd
"deze computer zelf", dus server en client kunnen op dezelfde pc draaien.

**Socket.** Een socket is het "stopcontact" waarmee je programma het netwerk
op gaat. Je maakt er een aan, en daarna kun je ermee verbinden, versturen en
ontvangen, een beetje zoals je met een bestand werkt (openen, lezen, schrijven,
sluiten).

**TCP.** TCP is de afspraak (het protocol) die zorgt dat data *betrouwbaar*
en *in de goede volgorde* aankomt. Voordat er data gaat, maken client en
server eerst een verbinding (de "handshake"), net als opnemen bij een
telefoongesprek. Daarna is het een stroom van bytes in beide richtingen.

**De stappen.** Dit is het vaste "recept" dat je in de code terugziet:

| Server                                | Client                     |
|---------------------------------------|----------------------------|
| `socket()` : socket maken             | `socket()` : socket maken  |
| `bind()` : koppelen aan poort 5000    |                            |
| `listen()` : in wachtstand            |                            |
| `accept()` : wacht op een client  ◀── | `connect()` : verbinden    |
| `recv()` : bericht ontvangen      ◀── | `send()` : bericht sturen  |
| `send()` : antwoord sturen        ──▶ | `recv()` : antwoord lezen  |
| `closesocket()` : ophangen            | `closesocket()` : ophangen |

**Winsock.** Op Windows heet de socket-bibliotheek *Winsock* (`ws2_32`).
Je moet hem aan het begin opstarten met `WSAStartup()` en aan het eind
afsluiten met `WSACleanup()`. Bij het compileren moet je hem ook "meelinken",
vandaar de `-lws2_32` hieronder.

**Compileren.** C++ is geen scripttaal zoals Python: je broncode (`.cpp`)
wordt eerst door een *compiler* omgezet naar een programma (`.exe`) dat je
daarna start. Elke keer dat je de code aanpast, moet je opnieuw compileren.

## Bouwen en draaien op Windows

Je hebt één van deze twee compilers nodig.

### Optie A: MinGW (g++)

1. Installeer MinGW-w64. Een makkelijke manier is via
   [MSYS2](https://www.msys2.org/): installeer het, open de "MSYS2 UCRT64"
   terminal en typ:
   ```
   pacman -S mingw-w64-ucrt-x86_64-gcc
   ```
   Voeg daarna `C:\msys64\ucrt64\bin` toe aan je `PATH` zodat `g++` overal werkt.
   Controleer met `g++ --version` in een nieuwe Opdrachtprompt.
2. Open een Opdrachtprompt (of PowerShell) in de map met deze bestanden.
3. Compileer beide programma's:
   ```
   g++ server.cpp -o server.exe -lws2_32
   g++ client.cpp -o client.exe -lws2_32
   ```
   `-o server.exe` is de naam van het programma dat eruit komt, en
   `-lws2_32` linkt de Winsock-bibliotheek mee.

### Optie B: Visual Studio (MSVC)

1. Installeer [Visual Studio Community](https://visualstudio.microsoft.com/)
   met de workload **"Desktop development with C++"**.
2. Open via het Startmenu de **"Developer Command Prompt for VS"** en ga naar
   de map met deze bestanden (`cd pad\naar\de\map`).
3. Compileer:
   ```
   cl /EHsc server.cpp ws2_32.lib
   cl /EHsc client.cpp ws2_32.lib
   ```
   Dit maakt `server.exe` en `client.exe`.

### Draaien

1. Open **twee** terminalvensters in deze map.
2. Start in het eerste venster de server:
   ```
   server.exe
   ```
   Je ziet: `Server luistert op poort 5000...` en hij blijft wachten.
   Windows Firewall kan vragen of je toegang wilt geven. Voor dit voorbeeld
   (alleen je eigen pc) is dat niet nodig, je mag het weigeren.
3. Start in het tweede venster de client:
   ```
   client.exe
   ```
4. In het client-venster zie je:
   ```
   Verbonden met de server!
   Verstuurd: hello world
   Antwoord van server: Server zegt: ik heb 'hello world' ontvangen!
   ```
   En in het server-venster:
   ```
   Client verbonden!
   Ontvangen van client: hello world
   Antwoord verstuurd.
   ```

Beide programma's stoppen daarna vanzelf.

## Op Linux (of macOS)

De code werkt daar ook: via `#ifdef _WIN32` worden de Windows-onderdelen
overgeslagen. Je hebt geen extra bibliotheek nodig:

```
g++ server.cpp -o server
g++ client.cpp -o client
./server        # in het eerste venster
./client        # in het tweede venster
```

## Problemen oplossen

- **`Kon geen verbinding maken. Draait de server wel?`** Start eerst de server,
  dan de client.
- **`bind() mislukt`** Er draait al een programma op poort 5000 (misschien een
  oude server). Sluit dat venster, of verander `PORT` in beide bestanden naar
  bijvoorbeeld `5050`.
- **`undefined reference to WSAStartup`** (MinGW) Je bent `-lws2_32` vergeten.
- **`'g++' is not recognized`** De compiler staat niet in je `PATH`, zie stap 1.

## Ideeën om verder te gaan

- Laat de server in een `while`-lus blijven draaien zodat er meerdere clients
  achter elkaar kunnen komen.
- Laat de client tekst van het toetsenbord lezen (`std::getline`) en die sturen.
- Draai de server op een andere pc in je netwerk en vul in de client diens
  IP-adres in plaats van `127.0.0.1` in.
