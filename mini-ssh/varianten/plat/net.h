// =============================================================================
// net.h  -  Gedeelde netwerkcode voor de PLATTE variant (zonder versleuteling)
//
// Dit laat het hele proces rond "een bericht versturen" zien, net als
// sendVersleuteld in de grote mini-ssh, maar dan PLAT: de tekst gaat
// leesbaar over de lijn. Zo zie je duidelijk wat versleuteling later
// toevoegt. De functies heten hier sendPlat en recvPlat.
//
// Alleen 127.0.0.1, alleen om te leren.
// =============================================================================

#pragma once

#include <iostream>
#include <string>
#include <cstdint>

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

const int   PORT      = 3000;
const char* SERVER_IP = "127.0.0.1";

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
// Zo weet de ontvanger precies hoeveel bytes bij één bericht horen.
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

// --- PLAT versturen en ontvangen --------------------------------------------
// Vergelijk dit met sendVersleuteld in de grote mini-ssh: daar zit tussen de
// tekst en sendFrame nog een versleutel-stap. Hier NIET. De tekst gaat
// leesbaar de lijn op. We printen hem er ook bij, zodat je ziet dat een
// afluisteraar alles gewoon kan meelezen.
inline bool sendPlat(SOCKET s, const std::string& tekst) {
    std::cout << "  [over het netwerk, leesbaar: \"" << tekst << "\"]\n";
    return sendFrame(s, tekst);
}
inline bool recvPlat(SOCKET s, std::string& tekst) {
    return recvFrame(s, tekst);
}
