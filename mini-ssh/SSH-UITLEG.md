# Hoe werkt SSH? Uitleg in gewone taal

SSH ("Secure Shell") is een programma waarmee je vanaf je eigen computer
veilig kunt inloggen op een andere computer, en daar opdrachten kunt typen
alsof je er zelf achter zit. Beheerders gebruiken het de hele dag om servers
te besturen. Ook `git push` naar GitHub kan via SSH lopen.

Het mooie: SSH is eigenlijk gewoon een client en een server die over een
TCP-socket praten, precies zoals je hello-world programma. Het verschil zit
in **wat** ze naar elkaar sturen, en vooral in hoe ze ervoor zorgen dat
niemand kan meelezen of zich kan voordoen als de ander.

Hieronder gaan we stap voor stap door een SSH-verbinding. Bij elke stap staat
wat jouw mini-versie (`server.cpp` en `client.cpp` in deze map) doet, en wat
de echte SSH anders doet.

---

## Het probleem dat SSH oplost

Stel je voor dat je met je hello-world client een wachtwoord naar de server
stuurt. Dat gaat als gewone bytes over het netwerk, via je router, je
provider, misschien tien andere computers. Iedereen die op één van die
plekken meekijkt (bijvoorbeeld met het programma Wireshark), ziet gewoon
`wachtwoord123` staan.

Vroeger werkte inloggen op afstand precies zo, met een programma dat
**Telnet** heette. SSH is in 1995 bedacht om dat op te lossen. Het moet drie
dingen regelen:

1. **Geheimhouding.** Niemand mag kunnen lezen wat je stuurt.
2. **Echtheid van de server.** Je moet zeker weten dat je met de echte
   server praat, en niet met iemand die ertussen zit en zich voordoet als
   de server (een "man-in-the-middle").
3. **Echtheid van de gebruiker.** De server moet zeker weten dat jij het
   bent (inloggen).

En daarbovenop: niemand mag berichten onderweg ongemerkt kunnen aanpassen.

---

## Stap 1: de TCP-verbinding

Dit ken je al. De SSH-server luistert (meestal op poort 22), de client doet
`connect()`, en TCP doet zijn eigen handshake. Vanaf nu is er een
betrouwbare stroom bytes in twee richtingen.

**In jouw mini-versie:** poort 2222, en de server luistert alleen op
`127.0.0.1`, zodat andere computers er niet bij kunnen.

### Tussendoor: berichten in een stroom

TCP geeft je een stroom bytes, geen losse berichten. Als je twee keer
`send()` doet, kan de ander dat in één `recv()` binnenkrijgen, of juist in
drie stukjes. Bij hello world merkte je dat niet omdat er maar één bericht
was. SSH (en jouw mini-versie) lost dit op door vóór elk bericht de lengte te
zetten. Zo'n pakketje "lengte + inhoud" heet een **frame** of **packet**. Kijk
naar `sendFrame()` en `recvFrame()` in `minissh.h`.

---

## Stap 2: versies uitwisselen

Het eerste wat beide kanten sturen is een regel tekst met hun versie, zoals:

```
SSH-2.0-OpenSSH_9.6
```

Zo weten ze dat ze dezelfde taal spreken. Daarna sturen ze elkaar een lijstje
met welke versleutelingsmethoden ze kennen, en kiezen ze de beste die ze
allebei ondersteunen.

**In jouw mini-versie:** `MINISSH-1.0 server` en `MINISSH-1.0 client`. Er is
maar één methode, dus kiezen hoeft niet.

---

## Stap 3: sleuteluitwisseling (Diffie-Hellman)

Dit is het slimste deel, en het voelt eerst als toverij.

Client en server willen versleutelen, en daarvoor hebben ze allebei dezelfde
geheime sleutel nodig. Maar ze kunnen die sleutel niet zomaar naar elkaar
sturen: iedereen luistert mee! Hoe spreek je een geheim af terwijl iedereen
meekijkt?

### De verfvergelijking

Stel je voor dat geheimen kleuren verf zijn, en dat je verf wel kunt mengen
maar niet meer uit elkaar kunt halen.

1. Client en server spreken openlijk een kleur af: **geel**. Iedereen mag dat
   weten.
2. De server kiest in het geheim **rood**, mengt het met geel, en stuurt de
   oranje mix naar de client.
3. De client kiest in het geheim **blauw**, mengt het met geel, en stuurt de
   groene mix naar de server.
4. De server doet zijn geheime rood bij het groen. De client doet zijn
   geheime blauw bij het oranje.
5. Allebei hebben ze nu **geel + rood + blauw**: precies dezelfde bruine kleur!

De afluisteraar heeft geel, oranje en groen gezien. Maar om bruin te maken
heeft hij rood of blauw nodig, en die kun je niet meer uit de mix halen.

### Met getallen

In plaats van verf gebruikt Diffie-Hellman machtsverheffen met "modulo"
(`%` in C, de rest na delen):

| | Server | Afluisteraar ziet | Client |
|---|---|---|---|
| Publiek | P en G | P en G | P en G |
| Geheim | kiest `a` | | kiest `b` |
| Versturen | `A = G^a % P` | A en B | `B = G^b % P` |
| Uitrekenen | `B^a % P` | | `A^b % P` |

Allebei komen ze uit op `G^(a*b) % P`, hetzelfde getal. Om dat uit A en B te
berekenen moet je `a` of `b` terugvinden, en dat heet het **discrete
logaritme**-probleem. Met getallen van 2048 bits (meer dan 600 cijfers) kan
geen enkele computer ter wereld dat.

**In jouw mini-versie:** `powMod()` en `kiesGeheimGetal()` in `minissh.h`, met
`P = 2^61 - 1`. Dat is een getal van maar 19 cijfers, zodat het in een
`uint64_t` past. Een laptop kan dat in korte tijd kraken, dus het is alleen om
het idee te zien. Beide programma's printen het gedeelde geheim, zodat je
kunt zien dat het echt hetzelfde getal is.

**Echte SSH** gebruikt tegenwoordig meestal een variant op elliptische krommen
(`curve25519`), wat hetzelfde idee is met andere wiskunde, en nieuwere versies
mengen er ook een methode doorheen die bestand is tegen toekomstige
quantumcomputers.

---

## Stap 4: de server bewijst wie hij is (host key)

Diffie-Hellman heeft één zwak punt. Stel dat iemand **tussen** jou en de server
zit. Hij doet Diffie-Hellman met jou (en doet alsof hij de server is) én met
de server (en doet alsof hij jou is). Nu heeft hij twee geheimen, en kan hij
alles ontsleutelen, lezen en weer doorsturen. Dit heet een
**man-in-the-middle**-aanval.

Daarom heeft elke SSH-server een vast sleutelpaar, de **host key**. Tijdens de
sleuteluitwisseling zet de server een **digitale handtekening** onder de
getallen met zijn geheime sleutel. De client controleert die handtekening met
de publieke sleutel van de server.

Maar hoe weet de client wat de echte publieke sleutel is? De eerste keer
dat je verbindt, vraagt SSH:

```
The authenticity of host 'server (1.2.3.4)' can't be established.
ED25519 key fingerprint is SHA256:abc123...
Are you sure you want to continue connecting (yes/no)?
```

Als je `yes` zegt, wordt de sleutel bewaard in het bestand `known_hosts`. Bij
elke volgende keer controleert SSH of de sleutel nog hetzelfde is. Is hij
ineens anders, dan krijg je een grote waarschuwing.

**In jouw mini-versie:** dit zit er niet in. Daardoor zou een
man-in-the-middle bij jouw versie wel werken. Dat is een van de redenen dat
het speelgoed is.

---

## Stap 5: versleutelen

Nu hebben client en server hetzelfde geheim. Daaruit maken ze sleutels voor
een **symmetrische** versleuteling: dezelfde sleutel om te versleutelen en te
ontsleutelen. Dat is veel sneller dan de wiskunde van Diffie-Hellman, dus
geschikt voor alle data die nog volgt.

Een veelgebruikte methode heet een **stroomversleuteling**. Uit de sleutel
maak je een eindeloze rij bytes die er willekeurig uitzien (de
sleutelstroom). Elke byte van je bericht doe je XOR (`^` in C) met de volgende
byte uit die stroom. De ontvanger maakt dezelfde stroom en doet nog een keer
XOR, want `(x ^ k) ^ k == x`.

Twee belangrijke details:

- **Een sleutel per richting.** Client-naar-server en server-naar-client
  krijgen elk hun eigen sleutel. Zou je dezelfde sleutelstroom twee keer
  gebruiken, dan kan een afluisteraar de twee versleutelde berichten tegen
  elkaar XOR-en, en vallen de sleutelbytes weg.
- **Controle op knoeien (MAC).** Bij XOR kan een aanvaller een bitje in de
  versleutelde data omdraaien, en dan draait datzelfde bitje om in de tekst
  die bij de ontvanger uitkomt, zonder dat hij de tekst kan lezen. Daarom
  zit er bij echte SSH achter elk bericht een soort cryptografische
  controlesom (een **MAC**, "message authentication code"). Klopt die niet,
  dan wordt de verbinding direct verbroken.

**In jouw mini-versie:** `StroomVersleutelaar` in `minissh.h`. De sleutelstroom
komt uit een simpele random-generator (`xorshift64*`), die voorspelbaar is als
je een stukje ervan kent. Er zijn wel twee richtingen, maar geen MAC. De
client en server printen elk verstuurd bericht als hex, zodat je ziet wat een
afluisteraar zou zien.

**Echte SSH** gebruikt bijvoorbeeld `chacha20-poly1305` (een cryptografisch
sterke stroomversleuteling met ingebouwde MAC) of `aes256-gcm`.

---

## Stap 6: inloggen (authenticatie)

Pas nu, als alles versleuteld is, vraagt de server wie je bent. Er zijn twee
gangbare manieren.

### Met een wachtwoord

De client stuurt gebruikersnaam en wachtwoord. Omdat de verbinding al
versleuteld is, kan niemand het wachtwoord onderweg lezen. De server
vergelijkt het met wat hij heeft opgeslagen. Dat is niet het wachtwoord zelf,
maar een **hash** ervan, een soort vingerafdruk waaruit je het wachtwoord niet
terug kunt rekenen.

**In jouw mini-versie:** `AUTH ruben geheim`, en de server vergelijkt dat met
twee vaste teksten in de code. Je mag 3 keer proberen.

### Met een sleutelpaar (public key)

Dit is veiliger en wordt het meest gebruikt. Je maakt op je eigen computer een
sleutelpaar met `ssh-keygen`:

- De **geheime sleutel** (`id_ed25519`) blijft altijd op jouw computer.
- De **publieke sleutel** (`id_ed25519.pub`) zet je op de server, in het
  bestand `authorized_keys`. Die mag iedereen zien.

Bij het inloggen zet de client met de geheime sleutel een handtekening onder
een stukje data dat uniek is voor deze verbinding. De server controleert die
handtekening met de publieke sleutel. Klopt hij, dan weet de server dat jij de
geheime sleutel hebt, zonder dat die ooit over het netwerk is gegaan. Zo werkt
het ook als je een SSH-sleutel aan GitHub toevoegt.

---

## Stap 7: de sessie en de shell

Als je bent ingelogd, opent de client een **kanaal** en vraagt de server om
een **shell** te starten. Een shell is het programma waarin je commando's
typt (`cmd` of PowerShell op Windows, `bash` op Linux). De server start die
shell en koppelt hem aan het kanaal:

- Alles wat jij typt, gaat versleuteld naar de server, en de server stopt het
  in de shell alsof jij het op het toetsenbord typte.
- Alles wat de shell print, gaat versleuteld terug en verschijnt op jouw
  scherm.

Hoe dat koppelen precies werkt (pipes, `CreateProcess`, pseudo-terminals)
staat uitgebreid in [SHELL-UITLEG.md](SHELL-UITLEG.md).

Over één SSH-verbinding kunnen trouwens meerdere kanalen tegelijk lopen. Zo
werken ook bestanden kopiëren (`scp`, `sftp`) en "port forwarding" via SSH.

**In jouw mini-versie:** de server start bewust **geen** echte shell. Hij kent
alleen een paar ingebouwde commando's (`help`, `echo`, `tijd`, `agenda`,
`programmas`, `start`, `whoami`, `exit`). Het commando `agenda` leest een
tekstbestand op de server en stuurt de inhoud versleuteld terug. Bij echte SSH
zou je daarvoor gewoon `type agenda.txt` (Windows) of `cat agenda.txt` (Linux)
in de shell typen.

Met `start` kan de server wel een programma draaien en de output terugsturen,
maar alleen programma's van een vaste lijst. Hij gebruikt daarvoor `popen()`,
wat in feite hetzelfde is als wat een echte SSH-server doet, maar dan voor
één programma in plaats van een hele shell. Door de vaste lijst kan het
programma niets onverwachts op je computer doen, ook niet als je een foutje
maakt. Wel zie je het belangrijkste idee: commando heen, antwoord
terug, alles versleuteld.

---

## Overzicht: echte SSH tegenover jouw mini-versie

| Onderdeel | Echte SSH | Mini-SSH |
|---|---|---|
| Verbinding | TCP, poort 22 | TCP, poort 2222, alleen 127.0.0.1 |
| Berichten | packets met lengte | frames met lengte |
| Versie | `SSH-2.0-...` + methoden kiezen | `MINISSH-1.0`, één vaste methode |
| Sleuteluitwisseling | curve25519 (of DH 2048+ bits) | DH met 61 bits (kraakbaar) |
| Server bewijst identiteit | host key + `known_hosts` | ontbreekt |
| Versleuteling | ChaCha20 of AES | XOR met simpele random-generator |
| Knoeien ontdekken | MAC | ontbreekt |
| Inloggen | wachtwoord (hash) of sleutelpaar | wachtwoord als tekst in de code |
| Sessie | echte shell, meerdere kanalen | ingebouwde commando's + programma's van een vaste lijst |

## Volgende stappen als je verder wilt

- **Met Wireshark meekijken.** Installeer Wireshark, luister op de
  "loopback"-adapter en filter op `tcp.port == 2222`. Je ziet de versie en de
  Diffie-Hellman-getallen leesbaar voorbijkomen, en daarna alleen nog
  onleesbare bytes. Vergelijk dat met je hello-world programma, waar alles
  leesbaar is.
- **Zelf echte SSH proberen.** Windows 10 en 11 hebben `ssh` en `ssh-keygen`
  ingebouwd. Typ `ssh -v gebruiker@server` en je ziet alle stappen hierboven
  (versie, sleuteluitwisseling, host key, authenticatie) in de uitvoer staan.
- **Een echte crypto-bibliotheek.** Als je C++ beter kent, kun je de
  zelfgemaakte versleuteling vervangen door een echte bibliotheek zoals
  **libsodium**. Daarmee kun je curve25519 en ChaCha20-Poly1305 gebruiken,
  dezelfde bouwstenen als echte SSH.
