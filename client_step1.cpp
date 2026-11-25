// 上位机练习 · 第一步：连上服务器，把收到的字节打印出来
// 编译： g++ -std=c++17 -O2 client_step1.cpp -o client_step1.exe -lws2_32
// 运行： client_step1.exe            （默认 127.0.0.1 9527）
//        client_step1.exe 127.0.0.1 9527

#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

#pragma comment(lib, "ws2_32.lib")

static const char *kHost = "127.0.0.1";
static const int kPort = 9527;

int main(int argc, char **argv) {
    std::string host = kHost;
    int port = kPort;
    if (argc >= 3) {
        host = argv[1];
        port = std::atoi(argv[2]);
    }

    // 1. Winsock 初始化：在 Windows 上玩 socket，第一件事就是这个
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::fprintf(stderr, "[client] WSAStartup failed: %d\n", WSAGetLastError());
        return 1;
    }

    // 2. 建一个 TCP socket
    SOCKET sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        std::fprintf(stderr, "[client] socket failed: %d\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }

    // 3. 把 "127.0.0.1" 这串字转成地址，再连过去
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = ::htons(static_cast<u_short>(port));
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        std::fprintf(stderr, "[client] bad address: %s\n", host.c_str());
        ::closesocket(sock);
        WSACleanup();
        return 1;
    }

    std::printf("[client] connecting to %s:%d ...\n", host.c_str(), port);
    if (::connect(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        std::fprintf(stderr, "[client] connect failed: %d\n", WSAGetLastError());
        ::closesocket(sock);
        WSACleanup();
        return 1;
    }
    std::printf("[client] connected\n");

    // 4. 收字节。recv 不保证一次收一整帧，收到多少算多少——这是后面拼帧的根
    char buf[4096];
    long long total_bytes = 0;
    int packets = 0;

    const auto t0 = std::chrono::steady_clock::now();

    for (;;) {
        int n = ::recv(sock, buf, sizeof(buf), 0);
        if (n == 0) {
            std::printf("[client] server closed the connection\n");
            break;
        }
        if (n == SOCKET_ERROR) {
            std::fprintf(stderr, "[client] recv failed: %d\n", WSAGetLastError());
            break;
        }

        ++packets;
        total_bytes += n;

        // 把这一包按十六进制打出来，看着就像设备吐数据的样子
        std::printf("[client] recv #%d, %d bytes: ", packets, n);
        for (int i = 0; i < n && i < 24; ++i) {
            std::printf("%02X ", static_cast<unsigned char>(buf[i]));
        }
        if (n > 24) std::printf("...");
        std::printf("\n");

        if (packets >= 30) {
            std::printf("[client] enough for step 1, exiting\n");
            break;
        }
    }

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
    std::printf("\n[client] packets=%d total_bytes=%lld elapsed=%lldms\n", packets, total_bytes,
                static_cast<long long>(ms));

    ::closesocket(sock);
    WSACleanup();
    return 0;
}
