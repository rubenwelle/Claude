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
| `minissh.h`      | Gedeelde code: berichten (frames) versturen, Diffie-Hellman, de XOR-versleuteling |
| `server.cpp`     | Luistert op 127.0.0.1:2222, doet de handshake, controleert het wachtwoord, beantwoordt commando's |
| `client.cpp`     | Verbindt met de server, doet de handshake, logt in, laat jou commando's typen |
| `SSH-UITLEG.md`  | Uitleg over hoe SSH echt werkt, in gewone taal |

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
```

Je hoeft `minissh.h` niet apart te bouwen; dat wordt via `#include`
automatisch meegenomen.

Op Linux of macOS laat je `-lws2_32` en `.exe` weg.

## Starten

1. Start in het eerste venster de server: `server.exe`
2. Start in een tweede venster de client: `client.exe`
3. Log in met gebruikersnaam **ruben** en wachtwoord **geheim**.
4. Typ `help` om de commando's te zien.

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

## Dingen om zelf te proberen

- Voeg een eigen commando toe aan `voerCommandoUit()` in `server.cpp`,
  bijvoorbeeld `hoofdletters <tekst>` dat de tekst in hoofdletters terugstuurt.
- Verander in `client.cpp` het label `"client->server"` in iets anders.
  Wat gebeurt er dan met wat de server ontvangt? Waarom?
- Kijk met Wireshark mee op de loopback-adapter (filter `tcp.port == 2222`)
  en zoek de versie-tekst en de getallen A en B op.
