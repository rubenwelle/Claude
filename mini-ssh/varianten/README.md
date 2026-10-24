# Varianten: van plat naar versleuteld

Twee kleine, kale varianten van de mini-SSH client/server, naast elkaar, zodat
je precies één stap tegelijk ziet. Beide zijn zo eenvoudig mogelijk gehouden:
geen commando's, geen bestanden, geen programma's starten. Alleen: typ een
regel, de server stuurt hem in hoofdletters terug.

Beide draaien **alleen op 127.0.0.1** en zijn puur om te leren. De
versleuteling is speelgoed (zie de uitleg verderop).

| | map `plat/` | map `vaste-sleutel/` |
|---|---|---|
| Versleuteling | nee | ja (XOR-sleutelstroom) |
| Login | nee | nee |
| Sleutel afspreken | n.v.t. | nee, sleutel staat ingebouwd |
| Poort | 3000 | 3001 |
| Op de lijn te zien | leesbare tekst | onleesbare bytes |
| Verstuur-functie | `sendPlat` | `sendVersleuteld` |

## 1. `plat/` — het hele proces, zonder versleuteling

Dit laat zien wat `sendVersleuteld` uit de grote mini-ssh doet, maar dan
**zonder** de versleutel-stap. De functies heten hier `sendPlat` en `recvPlat`,
en die geven de tekst gewoon door aan `sendFrame` / `recvFrame`:

```cpp
inline bool sendPlat(SOCKET s, const std::string& tekst) {
    std::cout << "  [over het netwerk, leesbaar: \"" << tekst << "\"]\n";
    return sendFrame(s, tekst);   // geen versleuteling, zo de lijn op
}
```

Je ziet bij elk bericht de regel `[over het netwerk, leesbaar: "..."]`. Dat is
precies wat een afluisteraar zou meelezen: alles staat er leesbaar.

**Bouwen en draaien (Linux):**
```bash
cd plat
g++ -std=c++17 server.cpp -o server
g++ -std=c++17 client.cpp -o client
./server        # in de ene terminal
./client        # in de andere terminal
```
**Op Windows (MinGW):** voeg `-lws2_32` toe en noem de uitvoer `server.exe` /
`client.exe`.

## 2. `vaste-sleutel/` — wél versleuteld, met een ingebouwde sleutel

Hetzelfde programma, maar nu versleuteld. Er is nog steeds **geen login** en
**geen uitwisseling**. In plaats daarvan staat het geheim hard in de code, in
`net.h`, en server én client gebruiken diezelfde constante:

```cpp
const uint64_t GEDEELDE_SLEUTEL = 0x5AFE12345678C0DEULL;
```

Omdat beide programma's hetzelfde geheim kennen, maken ze dezelfde
sleutelstroom en kunnen ze elkaars berichten ontcijferen — zonder eerst iets af
te spreken. Nu zie je op de lijn `[over het netwerk, versleuteld: 3f a1 07 ...]`:
onleesbare bytes.

**Bouwen en draaien:** net als hierboven, maar in de map `vaste-sleutel`.
Poort is hier 3001, dus je kunt beide varianten tegelijk draaien.

## 3. Waarom dit nog steeds niet veilig is

De `vaste-sleutel`-variant is een goede tussenstap om te zien wat
versleuteling toevoegt, maar hij heeft twee grote problemen die de echte SSH
juist oplost:

1. **De sleutel staat in de broncode.** Iedereen die het programma of de bron
   heeft, kan alles ontsleutelen. Een echt geheim deel je nooit zo.
2. **Hoe spreek je een geheim af met iemand die je nog nooit sprak?** Je kunt
   de sleutel niet gewoon over de lijn sturen (dan leest de afluisteraar mee).
   Daarvoor bestaat **Diffie-Hellman**: twee partijen spreken tóch een vers,
   uniek geheim af, zonder dat dat geheim ooit over de lijn gaat. Dat is wat de
   grote mini-ssh in `../minissh.h` doet.

En zoals altijd: de XOR-stroom zelf is speelgoed (geen AES/ChaCha20, geen
controle of een bericht onderweg is aangepast). Dus: alleen 127.0.0.1, alleen
om te oefenen.

## De weg die je nu hebt gezien

```
plat/            geen versleuteling          (leesbaar op de lijn)
   │  + XOR met een ingebouwd geheim
vaste-sleutel/   wél versleuteld             (onleesbaar op de lijn)
   │  + het geheim vers afspreken i.p.v. inbouwen (Diffie-Hellman) + login
../ (mini-ssh)   de volledige toy-SSH
```
