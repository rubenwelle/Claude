// =============================================================================
// net.h  -  Shared networking code for the "MyFirstProgram over TCP" demo
//
// Same framing trick as the big mini-ssh: a message ("frame") is a 4-byte
// length (big-endian) followed by that many bytes. That way the receiver knows
// exactly how many bytes belong to one message, even though TCP may split or
// merge the stream.
//
// There is NO encryption and NO login here: this demo is only about running a
// fixed helper program over a TCP connection. Keep it to 127.0.0.1.
// =============================================================================

#pragma once

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

const int   PORT      = 2223;          // 2222 is the main mini-ssh; we use 2223
const char* SERVER_IP = "127.0.0.1";   // your own computer only!

// --- send/receive exactly len bytes (TCP may deliver in chunks) -------------
inline bool sendAll(SOCKET s, const char* data, int len) {
    int sent = 0;
    while (sent < len) {
        int n = send(s, data + sent, len - sent, 0);
        if (n == SOCKET_ERROR || n == 0) return false;
        sent += n;
    }
    return true;
}
inline bool recvAll(SOCKET s, char* data, int len) {
    int got = 0;
    while (got < len) {
        int n = recv(s, data + got, len - got, 0);
        if (n == SOCKET_ERROR || n == 0) return false;
        got += n;
    }
    return true;
}

// --- a frame = 4-byte length (big-endian) + the content ---------------------
inline bool sendFrame(SOCKET s, const std::string& content) {
    uint32_t length = static_cast<uint32_t>(content.size());
    char head[4] = {
        static_cast<char>((length >> 24) & 0xFF),
        static_cast<char>((length >> 16) & 0xFF),
        static_cast<char>((length >> 8)  & 0xFF),
        static_cast<char>( length        & 0xFF)
    };
    if (!sendAll(s, head, 4)) return false;
    return sendAll(s, content.data(), static_cast<int>(content.size()));
}
inline bool recvFrame(SOCKET s, std::string& content) {
    unsigned char head[4];
    if (!recvAll(s, reinterpret_cast<char*>(head), 4)) return false;
    uint32_t length = (uint32_t(head[0]) << 24) | (uint32_t(head[1]) << 16) |
                      (uint32_t(head[2]) << 8)  |  uint32_t(head[3]);
    if (length > 64 * 1024) return false;
    content.assign(length, '\0');
    if (length == 0) return true;
    return recvAll(s, &content[0], static_cast<int>(length));
}

// The marker the helper program prints after each complete answer, so the
// server knows when one answer is finished (same idea as <<KLAAR>> in the big
// mini-ssh "sessie" command).
const char* const DONE_MARKER = "<<DONE>>";
