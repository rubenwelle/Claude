// =============================================================================
// net.h  -  Netwerkcode voor de variant MET VASTE SLEUTEL
//
// Hier zit WEL versleuteling in, maar heel anders geregeld dan in de grote
// mini-ssh:
//   * GEEN login (geen wachtwoord).
//   * GEEN sleuteluitwisseling (geen Diffie-Hellman).
//   * In plaats daarvan staat de geheime sleutel HARD INGEBOUWD in zowel de
//     server als de client: de constante GEDEELDE_SLEUTEL hieronder. Omdat
//     beide programma's precies hetzelfde geheim kennen, kunnen ze elkaars
//     berichten versleutelen en ontsleutelen zonder eerst iets af te spreken.
//
// Vergelijk met ../plat: daar ging de tekst leesbaar over de lijn. Hier zie
// je op het netwerk alleen onleesbare bytes (we printen ze als hex).
//
// WAAROM IS DIT TOCH NIET VEILIG?  Twee grote problemen:
//   1. De sleutel staat gewoon in de broncode. Iedereen die het programma of
//      de bron heeft, kan alles ontsleutelen. Een echt geheim deel je niet zo.
//   2. Diffie-Hellman bestaat juist om dit op te lossen: twee partijen die
//      elkaar nog nooit spraken, spreken tóch een vers geheim af, zonder dat
//      ooit over de lijn te sturen. Daarom gebruikt de grote mini-ssh dat.
// Nog steeds: alleen 127.0.0.1, alleen om te leren.
// =============================================================================

#pragma once

#include <iostream>
#include <string>
#include <cstdint>
#include <sstream>
#include <iomanip>

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

const int   PORT      = 3001;
const char* SERVER_IP = "127.0.0.1";

// -----------------------------------------------------------------------------
// DE INGEBOUWDE GEHEIME SLEUTEL
// Dit getal staat in ZOWEL de server als de client (want ze delen net.h).
// Dat is precies het punt van deze variant: het geheim is niet afgesproken,
// het is gewoon van tevoren in beide programma's gezet.
// (In het echt zou je dit nooit zo doen: zie de uitleg bovenaan.)
// -----------------------------------------------------------------------------
const uint64_t GEDEELDE_SLEUTEL = 0x5AFE12345678C0DEULL;

// --- precies len bytes versturen/ontvangen (TCP mag in stukjes knippen) -----
inline bool sendAll(SOCKET s, const char* data, int len) {
    int verstuurd = 0;
    while (verstuurd < len) {
        int n = send(s, data + verstuurd, len - verstuurd, 0);
        if (n == SOCKET_ERROR || n == 0) return false;
        verstuurd += n;
    }
    return true;
}
inline bool recvAll(SOCKET s, char* data, int len) {
    int ontvangen = 0;
    while (ontvangen < len) {
        int n = recv(s, data + ontvangen, len - ontvangen, 0);
        if (n == SOCKET_ERROR || n == 0) return false;
        ontvangen += n;
    }
    return true;
}

// --- een frame = 4 bytes lengte (big-endian) + de inhoud --------------------
inline bool sendFrame(SOCKET s, const std::string& inhoud) {
    uint32_t lengte = static_cast<uint32_t>(inhoud.size());
    char kop[4] = {
        static_cast<char>((lengte >> 24) & 0xFF),
        static_cast<char>((lengte >> 16) & 0xFF),
        static_cast<char>((lengte >> 8)  & 0xFF),
        static_cast<char>( lengte        & 0xFF)
    };
    if (!sendAll(s, kop, 4)) return false;
    return sendAll(s, inhoud.data(), static_cast<int>(inhoud.size()));
}
inline bool recvFrame(SOCKET s, std::string& inhoud) {
    unsigned char kop[4];
    if (!recvAll(s, reinterpret_cast<char*>(kop), 4)) return false;
    uint32_t lengte = (uint32_t(kop[0]) << 24) | (uint32_t(kop[1]) << 16) |
                      (uint32_t(kop[2]) << 8)  |  uint32_t(kop[3]);
    if (lengte > 64 * 1024) return false;
    inhoud.assign(lengte, '\0');
    if (lengte == 0) return true;
    return recvAll(s, &inhoud[0], static_cast<int>(lengte));
}

// -----------------------------------------------------------------------------
// De (speelgoed)versleuteling: een XOR-sleutelstroom, net als in de grote
// mini-ssh. Het enige verschil is WAAR het geheim vandaan komt: niet uit een
// Diffie-Hellman-uitwisseling, maar uit de vaste GEDEELDE_SLEUTEL hierboven.
//
// We gebruiken twee stromen, één per richting, zodat client->server en
// server->client niet dezelfde sleutelbytes gebruiken.
// -----------------------------------------------------------------------------
class StroomVersleutelaar {
public:
    StroomVersleutelaar(uint64_t gedeeldGeheim, const std::string& richting) {
        uint64_t h = 1469598103934665603ULL;          // FNV-1a hash van de richting
        for (unsigned char c : richting) { h ^= c; h *= 1099511628211ULL; }
        toestand = gedeeldGeheim ^ h;
        if (toestand == 0) toestand = 1;              // xorshift mag nooit 0 zijn
    }
    // Versleutelen en ontsleutelen is dezelfde handeling: (x ^ k) ^ k == x.
    void toepassen(std::string& data) {
        for (char& c : data) c = static_cast<char>(c ^ volgendeByte());
    }
private:
    uint64_t toestand;
    unsigned char volgendeByte() {                    // xorshift64* (niet veilig!)
        toestand ^= toestand >> 12;
        toestand ^= toestand << 25;
        toestand ^= toestand >> 27;
        return static_cast<unsigned char>((toestand * 2685821657736338717ULL) >> 56);
    }
};

// Laat bytes zien als hex, zodat je ziet wat een afluisteraar op de lijn ziet.
inline std::string naarHex(const std::string& data, size_t maxBytes = 24) {
    std::ostringstream uit;
    for (size_t i = 0; i < data.size() && i < maxBytes; i++) {
        uit << std::hex << std::setw(2) << std::setfill('0')
            << (static_cast<unsigned int>(static_cast<unsigned char>(data[i]))) << ' ';
    }
    if (data.size() > maxBytes) uit << "...";
    return uit.str();
}

// Versleutel een tekst en verstuur hem als frame. We printen de bytes erbij
// zodat je ziet dat de tekst onleesbaar over de lijn gaat.
inline bool sendVersleuteld(SOCKET s, StroomVersleutelaar& versleutelaar,
                            const std::string& tekst) {
    std::string data = tekst;          // kopie; het origineel blijft leesbaar
    versleutelaar.toepassen(data);
    std::cout << "  [over het netwerk, versleuteld: " << naarHex(data) << "]\n";
    return sendFrame(s, data);
}

// Ontvang een frame en ontsleutel het.
inline bool recvVersleuteld(SOCKET s, StroomVersleutelaar& versleutelaar,
                            std::string& tekst) {
    if (!recvFrame(s, tekst)) return false;
    versleutelaar.toepassen(tekst);
    return true;
}
