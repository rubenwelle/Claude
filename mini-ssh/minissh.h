// =============================================================================
// minissh.h  -  Gedeelde code voor de mini-SSH server en client
//
// LET OP: dit is SPEELGOED om te leren hoe SSH werkt. Het is NIET veilig.
//   - De getallen voor Diffie-Hellman zijn veel te klein (61 bits in plaats
//     van 2048+ bits), een computer kan ze kraken.
//   - De "versleuteling" is een zelfgemaakte XOR-stroom met een simpele
//     random-generator. Echte SSH gebruikt AES of ChaCha20.
//   - Er is geen controle of berichten onderweg zijn aangepast (geen MAC)
//     en geen controle of je met de echte server praat (geen host key).
// Gebruik dit dus alleen op je eigen computer (127.0.0.1) om te oefenen.
//
// Dit bestand bevat alles wat server EN client allebei nodig hebben:
//   Deel 1: de socket-headers (zelfde truc als in de hello-world code)
//   Deel 2: hulpfuncties om berichten ("frames") te versturen en ontvangen
//   Deel 3: de wiskunde voor de sleuteluitwisseling (Diffie-Hellman)
//   Deel 4: de (speelgoed)versleuteling
//
// Je hoeft dit bestand niet apart te compileren: server.cpp en client.cpp
// doen #include "minissh.h", en dan plakt de compiler het er gewoon in.
// =============================================================================

#pragma once   // zorgt dat dit bestand maar één keer wordt ingelezen

#include <iostream>
#include <string>
#include <cstdint>   // vaste getaltypes, zoals uint64_t (64-bits, zonder minteken)
#include <random>    // random getallen
#include <chrono>    // de klok, om de random-generator extra te "husselen"
#include <sstream>   // std::ostringstream, om tekst in elkaar te zetten
#include <iomanip>   // std::setw, std::setfill (voor hex-uitvoer)

// -----------------------------------------------------------------------------
// Deel 1: socket-headers per besturingssysteem (zie hello-world server.cpp)
// -----------------------------------------------------------------------------
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #ifdef _MSC_VER
        #pragma comment(lib, "ws2_32.lib")
    #endif
    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    typedef int SOCKET;
    const int INVALID_SOCKET = -1;
    const int SOCKET_ERROR   = -1;
    #define closesocket close
#endif

// Echte SSH gebruikt poort 22. Wij nemen 2222 zodat we niet in de weg zitten
// van een echte SSH-server, en omdat poorten onder 1024 vaak beheerdersrechten
// vragen.
const int   PORT      = 2222;
const char* SERVER_IP = "127.0.0.1";   // alleen je eigen computer!

// Net als echte SSH sturen beide kanten eerst een versie-tekst. Echte SSH
// stuurt bijvoorbeeld "SSH-2.0-OpenSSH_9.6".
const std::string PROTOCOL_VERSIE = "MINISSH-1.0";

// Zet dit op false als je de versleutelde bytes niet wilt zien.
const bool TOON_VERSLEUTELD = true;


// -----------------------------------------------------------------------------
// Deel 2: berichten versturen en ontvangen
//
// Probleem: TCP is een STROOM van bytes, geen losse berichten. Als je twee
// keer send() doet, kan de ander dat in één recv() krijgen, of juist in
// stukjes. Daarom zetten we vóór elk bericht de lengte (4 bytes). De
// ontvanger leest eerst die 4 bytes, en weet dan precies hoeveel er volgt.
// Zo'n "lengte + inhoud" pakketje heet een frame. Echte SSH doet hetzelfde.
// -----------------------------------------------------------------------------

// Stuur precies 'len' bytes. send() mag namelijk minder versturen dan je
// vraagt, dus we blijven het proberen tot alles weg is.
inline bool sendAll(SOCKET s, const char* data, int len) {
    int verstuurd = 0;
    while (verstuurd < len) {
        int n = send(s, data + verstuurd, len - verstuurd, 0);
        if (n == SOCKET_ERROR || n == 0) return false;
        verstuurd += n;
    }
    return true;
}

// Ontvang precies 'len' bytes (zelfde idee: recv() kan minder geven).
inline bool recvAll(SOCKET s, char* data, int len) {
    int ontvangen = 0;
    while (ontvangen < len) {
        int n = recv(s, data + ontvangen, len - ontvangen, 0);
        if (n == SOCKET_ERROR || n == 0) return false;  // 0 = ander heeft opgehangen
        ontvangen += n;
    }
    return true;
}

// Stuur één frame: 4 bytes lengte (big-endian, de "netwerkvolgorde"),
// en daarna de inhoud zelf.
inline bool sendFrame(SOCKET s, const std::string& inhoud) {
    uint32_t lengte = static_cast<uint32_t>(inhoud.size());
    char kop[4];
    kop[0] = static_cast<char>((lengte >> 24) & 0xFF);  // hoogste byte eerst
    kop[1] = static_cast<char>((lengte >> 16) & 0xFF);
    kop[2] = static_cast<char>((lengte >> 8)  & 0xFF);
    kop[3] = static_cast<char>( lengte        & 0xFF);
    if (!sendAll(s, kop, 4)) return false;
    return sendAll(s, inhoud.data(), static_cast<int>(inhoud.size()));
}

// Ontvang één frame. Geeft false terug als de verbinding weg is.
inline bool recvFrame(SOCKET s, std::string& inhoud) {
    unsigned char kop[4];
    if (!recvAll(s, reinterpret_cast<char*>(kop), 4)) return false;
    uint32_t lengte = (uint32_t(kop[0]) << 24) | (uint32_t(kop[1]) << 16) |
                      (uint32_t(kop[2]) << 8)  |  uint32_t(kop[3]);
    // Veiligheidsgrens: weiger gekke lengtes, anders kan iemand ons
    // laten proberen 4 gigabyte geheugen te reserveren.
    if (lengte > 64 * 1024) return false;
    inhoud.assign(lengte, '\0');   // maak de string zo lang als nodig
    if (lengte == 0) return true;
    return recvAll(s, &inhoud[0], static_cast<int>(lengte));
}


// -----------------------------------------------------------------------------
// Deel 3: Diffie-Hellman sleuteluitwisseling
//
// Het doel: server en client willen allebei hetzelfde geheime getal hebben,
// terwijl iedereen kan meeluisteren. Ze sturen dat getal dus NOOIT zelf.
//
// Publiek (iedereen mag dit weten):   P (een priemgetal) en G (een grondgetal)
// Server kiest geheim a, stuurt A = G^a mod P
// Client kiest geheim b, stuurt B = G^b mod P
// Server rekent B^a mod P,  client rekent A^b mod P.
// Dat is allebei G^(a*b) mod P: hetzelfde getal! Een afluisteraar ziet alleen
// A en B, en om daaruit a of b terug te rekenen moet je het "discrete
// logaritme" oplossen. Met echt grote getallen kan geen computer dat.
//
// "mod P" betekent: de rest na delen door P (in C/C++ de % operator).
// -----------------------------------------------------------------------------

// P = 2^61 - 1, een bekend priemgetal. VEEL te klein voor echte veiligheid
// (echte SSH gebruikt priemgetallen van 2048 bits of elliptische krommen),
// maar het past in een uint64_t, en dat houdt de code leesbaar.
const uint64_t DH_P = 2305843009213693951ULL;
const uint64_t DH_G = 5;

// (a * b) mod m, zonder dat het tussenresultaat te groot wordt.
// Twee 61-bits getallen vermenigvuldigen geeft 122 bits, en dat past niet
// in 64 bits. Daarom doen we het als "optellen en verdubbelen", net zoals je
// 13 * 7 kunt uitrekenen als 7 + 7*4 + 7*8 (13 = 1 + 4 + 8 in binair).
inline uint64_t mulMod(uint64_t a, uint64_t b, uint64_t m) {
    uint64_t resultaat = 0;
    a %= m;
    while (b > 0) {
        if (b & 1) resultaat = (resultaat + a) % m;  // dit bitje van b staat aan
        a = (a * 2) % m;                             // a verdubbelen
        b >>= 1;                                     // naar het volgende bitje
    }
    return resultaat;
}

// (grondtal ^ exponent) mod m, met "kwadrateren en vermenigvuldigen".
// Zo hoef je niet a keer te vermenigvuldigen (a is een gigantisch getal),
// maar maar ongeveer 61 keer.
inline uint64_t powMod(uint64_t grondtal, uint64_t exponent, uint64_t m) {
    uint64_t resultaat = 1;
    grondtal %= m;
    while (exponent > 0) {
        if (exponent & 1) resultaat = mulMod(resultaat, grondtal, m);
        grondtal = mulMod(grondtal, grondtal, m);
        exponent >>= 1;
    }
    return resultaat;
}

// Kies een geheim getal tussen 2 en P-2.
// std::random_device vraagt het besturingssysteem om toeval. Op sommige
// oudere MinGW-versies is dat niet echt willekeurig, dus mengen we de klok
// erdoorheen. (Voor echte crypto zou je een cryptografische bron gebruiken.)
inline uint64_t kiesGeheimGetal() {
    std::random_device rd;
    uint64_t zaadje = (uint64_t(rd()) << 32) ^ rd() ^
        static_cast<uint64_t>(std::chrono::high_resolution_clock::now()
                                  .time_since_epoch().count());
    std::mt19937_64 generator(zaadje);
    std::uniform_int_distribution<uint64_t> verdeling(2, DH_P - 2);
    return verdeling(generator);
}


// -----------------------------------------------------------------------------
// Deel 4: (speelgoed)versleuteling met XOR
//
// Het idee: uit het gedeelde geheim maken we een lange rij "willekeurige"
// bytes, de sleutelstroom. Elke byte van je bericht doe je XOR (^) met de
// volgende byte uit die stroom. De ontvanger maakt precies dezelfde stroom
// (want hij heeft hetzelfde geheim) en doet nog een keer XOR. Omdat
// (x ^ k) ^ k == x krijg je het origineel terug.
//
// Echte SSH werkt met hetzelfde principe (bijv. ChaCha20 is ook een
// sleutelstroom + XOR), maar dan met een generator die wiskundig bewezen
// moeilijk te voorspellen is. Onze xorshift-generator is dat NIET.
//
// Belangrijk: we gebruiken twee aparte stromen, één per richting
// (client->server en server->client). Als beide kanten dezelfde stroom
// zouden gebruiken, zouden ze dezelfde sleutelbytes hergebruiken, en dan kan
// een afluisteraar de twee berichten tegen elkaar wegstrepen.
// -----------------------------------------------------------------------------
class StroomVersleutelaar {
public:
    // Maak een stroom uit het gedeelde geheim plus een label voor de richting.
    StroomVersleutelaar(uint64_t gedeeldGeheim, const std::string& richting) {
        // Meng het label door het geheim (FNV-1a, een simpele hashfunctie),
        // zodat "client->server" en "server->client" een andere stroom geven.
        uint64_t h = 1469598103934665603ULL;
        for (unsigned char c : richting) {
            h ^= c;
            h *= 1099511628211ULL;
        }
        toestand = gedeeldGeheim ^ h;
        if (toestand == 0) toestand = 1;   // xorshift mag nooit 0 zijn
    }

    // Versleutel (of ontsleutel: dat is precies dezelfde handeling!)
    // De toestand loopt door van bericht naar bericht. Daarom moeten beide
    // kanten alle berichten in dezelfde volgorde verwerken. TCP garandeert
    // die volgorde gelukkig.
    void toepassen(std::string& data) {
        for (char& c : data) {
            c = static_cast<char>(c ^ volgendeByte());
        }
    }

private:
    uint64_t toestand;

    // xorshift64*: een snelle, simpele random-generator (niet veilig!).
    unsigned char volgendeByte() {
        toestand ^= toestand >> 12;
        toestand ^= toestand << 25;
        toestand ^= toestand >> 27;
        return static_cast<unsigned char>((toestand * 2685821657736338717ULL) >> 56);
    }
};

// Laat bytes zien als hex (bijv. "3f a1 07 ..."), zodat je kunt zien wat
// een afluisteraar op het netwerk zou zien.
inline std::string naarHex(const std::string& data, size_t maxBytes = 24) {
    std::ostringstream uit;
    for (size_t i = 0; i < data.size() && i < maxBytes; i++) {
        uit << std::hex << std::setw(2) << std::setfill('0')
            << (static_cast<unsigned int>(static_cast<unsigned char>(data[i])))
            << ' ';
    }
    if (data.size() > maxBytes) uit << "...";
    return uit.str();
}

// Versleutel een tekst en verstuur hem als frame.
inline bool sendVersleuteld(SOCKET s, StroomVersleutelaar& versleutelaar,
                            const std::string& tekst) {
    std::string data = tekst;          // kopie, het origineel blijft leesbaar
    versleutelaar.toepassen(data);
    if (TOON_VERSLEUTELD) {
        std::cout << "  [over het netwerk: " << naarHex(data) << "]\n";
    }
    return sendFrame(s, data);
}

// Ontvang een frame en ontsleutel het.
inline bool recvVersleuteld(SOCKET s, StroomVersleutelaar& versleutelaar,
                            std::string& tekst) {
    if (!recvFrame(s, tekst)) return false;
    versleutelaar.toepassen(tekst);
    return true;
}
