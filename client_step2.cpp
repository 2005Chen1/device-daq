// 上位机练习 · 第二步：缓冲拼帧
// 编译： g++ -std=c++17 -O2 client_step2.cpp -o client_step2.exe -lws2_32
//
// 第一步证明了：recv 一次拿到的字节数，跟"帧"没有任何关系。
// 第二步要做的，就是把这些字节攒起来，自己在里面找出完整的帧。
//
// 思路只有四句话：
//   1. 把收到的字节全部追加到一个缓冲区
//   2. 在缓冲区里找帧头 AA 55
//   3. 帧头后面的长度字段告诉我们这一帧有多长
//   4. 缓冲区里攒够一整帧，才切出来处理，然后从缓冲区里删掉
//
// 剩下的代码，全是在处理"没找着帧头"和"还没攒够"这两种情况。

#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static const size_t HEAD_LEN = 2;    // 帧头 AA 55
static const size_t LEN_LEN = 2;     // 长度字段
static const size_t CRC_LEN = 2;     // 校验字段
static const size_t MAX_PAYLOAD = 1024;  // 协议允许的最大数据区，超过就认为帧头是假的
static const int MAX_FRAMES = 100;   // 收满这么多帧就退出，方便演示

int main(int argc, char **argv) {
    // 源文件是 UTF-8，而 Windows 控制台默认是 GBK(936)。
    // 不设这一句，中文注释里的字符串打出来就是乱码。
    ::SetConsoleOutputCP(CP_UTF8);

    std::string host = "127.0.0.1";
    int port = 9527;
    if (argc >= 3) {
        host = argv[1];
        port = std::atoi(argv[2]);
    }

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::fprintf(stderr, "[client] WSAStartup failed\n");
        return 1;
    }

    SOCKET sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        std::fprintf(stderr, "[client] socket failed\n");
        WSACleanup();
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = ::htons(static_cast<u_short>(port));
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        std::fprintf(stderr, "[client] bad address\n");
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
    std::printf("[client] connected\n\n");

    // 缓冲区：收到的字节先全部堆在这儿，拼出一帧才拿走
    std::vector<unsigned char> buf;
    buf.reserve(8192);

    char tmp[4096];
    long long frames = 0;         // 成功切出来的帧数
    long long dropped = 0;        // 扔掉的垃圾字节数
    long long resync = 0;         // 重新对齐的次数
    long long half_frames = 0;    // 遇到半帧的次数（攒着等下次）

    const auto t0 = std::chrono::steady_clock::now();

    while (frames < MAX_FRAMES) {
        int n = ::recv(sock, tmp, sizeof(tmp), 0);
        if (n == 0) {
            std::printf("[client] server closed\n");
            break;
        }
        if (n == SOCKET_ERROR) {
            std::fprintf(stderr, "[client] recv failed: %d\n", WSAGetLastError());
            break;
        }

        // 1. 新到的字节全部堆到缓冲区尾部
        buf.insert(buf.end(), tmp, tmp + n);

        // 2. 只要缓冲区里还可能有完整的帧，就一直切
        for (;;) {
            // 2.1 找帧头 AA 55
            size_t head = std::string::npos;
            for (size_t i = 0; i + 1 < buf.size(); ++i) {
                if (buf[i] == 0xAA && buf[i + 1] == 0x55) {
                    head = i;
                    break;
                }
            }

            if (head == std::string::npos) {
                // 整段都没有帧头。最后一个字节可能是"半个帧头"（0xAA 到了、0x55 还没来），
                // 留着它，其余全扔。
                if (buf.size() > 1) {
                    dropped += static_cast<long long>(buf.size()) - 1;
                    buf.erase(buf.begin(), buf.end() - 1);
                }
                break;
            }

            if (head > 0) {
                // 帧头前面有垃圾，扔掉，让帧头对齐到缓冲区开头
                dropped += static_cast<long long>(head);
                ++resync;
                buf.erase(buf.begin(), buf.begin() + head);
            }

            // 2.2 长度字段够不够读
            if (buf.size() < HEAD_LEN + LEN_LEN) break;  // 半个头，等下一批字节

            size_t payload_len = static_cast<size_t>(buf[2]) |
                                 (static_cast<size_t>(buf[3]) << 8);

            if (payload_len > MAX_PAYLOAD) {
                // 长度离谱，说明这个 AA 55 是数据里碰巧出现的假帧头，跳过这一个字节继续找
                ++dropped;
                buf.erase(buf.begin());
                continue;
            }

            size_t total = HEAD_LEN + LEN_LEN + payload_len + CRC_LEN;

            // 2.3 够一整帧了吗？不够就等着，别急着切
            if (buf.size() < total) {
                ++half_frames;
                break;
            }

            // 2.4 攒够了，切出来处理
            const unsigned char *p = buf.data();
            int16_t c0 = static_cast<int16_t>(p[4] | (p[5] << 8));
            int16_t c1 = static_cast<int16_t>(p[6] | (p[7] << 8));
            int16_t c2 = static_cast<int16_t>(p[8] | (p[9] << 8));
            int16_t c3 = static_cast<int16_t>(p[10] | (p[11] << 8));
            unsigned crc = static_cast<unsigned>(p[12] | (p[13] << 8));

            ++frames;
            std::printf("[frame %3lld] len=%zu seq=%-4d v=%-5d t=%-4d rpm=%-5d crc=0x%04X\n",
                        frames, payload_len, c0, c1, c2, c3, crc);

            // 处理完把这一帧从缓冲区里删掉
            buf.erase(buf.begin(), buf.begin() + total);
        }
    }

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - t0)
                        .count();

    std::printf("\n===== 统计 =====\n");
    std::printf("切出完整帧     : %lld\n", frames);
    std::printf("丢弃垃圾字节   : %lld\n", dropped);
    std::printf("重新对齐次数   : %lld\n", resync);
    std::printf("半帧等待次数   : %lld\n", half_frames);
    std::printf("剩余缓冲区字节 : %zu\n", buf.size());
    std::printf("耗时           : %lld ms\n", static_cast<long long>(ms));

    ::closesocket(sock);
    WSACleanup();
    return 0;
}
