#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <iostream>
#include <fstream>
#include <vector>

static volatile std::sig_atomic_t g_stop = 0;
void handle_sigint(int) { g_stop = 1; }

using steady_clock = std::chrono::steady_clock;

struct PayloadHeader
{
    uint64_t seq;
    uint64_t send_ns; // steady_clock::now().time_since_epoch().count()
};

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        std::fprintf(stderr, "Usage: %s <bind_ip> <port> <out_csv>\n", argv[0]);
        std::fprintf(stderr, "Example: %s 0.0.0.0 9000 out.csv\n", argv[0]);
        return 1;
    }
    const char *bind_ip = argv[1];
    int port = std::stoi(argv[2]);
    const char *csv_path = argv[3];

    std::signal(SIGINT, handle_sigint);

    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
    {
        perror("socket");
        return 1;
    }

    // allow quick rebinding
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#ifdef SO_REUSEPORT
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes)); // helpful on macOS
#endif

    // make socket non-blocking so poll() controls blocking behavior
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (::inet_pton(AF_INET, bind_ip, &addr.sin_addr) != 1)
    {
        std::cerr << "Invalid bind IP\n";
        return 1;
    }
    if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
    {
        perror("bind");
        return 1;
    }

    std::ofstream csv(csv_path);
    if (!csv)
    {
        std::cerr << "Cannot open CSV for write\n";
        return 1;
    }
    csv << "seq,recv_bytes,latency_us,recv_ns\n";

    std::vector<char> buf(64 * 1024);

    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLIN;

    while (!g_stop)
    {
        int ret = ::poll(&pfd, 1, 200); // 200 ms timeout
        if (ret < 0)
        {
            if (errno == EINTR)
                break; // interrupted by Ctrl+C
            perror("poll");
            continue;
        }
        if (ret == 0)
            continue; // timeout; loop again and check g_stop

        if (pfd.revents & POLLIN)
        {
            // Drain all available packets without blocking
            while (!g_stop)
            {
                sockaddr_in src{};
                socklen_t slen = sizeof(src);
                ssize_t n = ::recvfrom(fd, buf.data(), buf.size(), 0,
                                       reinterpret_cast<sockaddr *>(&src), &slen);
                if (n < 0)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                        break; // no more to read
                    if (errno == EINTR)
                    {
                        g_stop = 1;
                        break;
                    }
                    perror("recvfrom");
                    break;
                }

                auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  steady_clock::now().time_since_epoch())
                                  .count();

                uint64_t seq = 0, send_ns = 0;
                if (n >= static_cast<ssize_t>(sizeof(PayloadHeader)))
                {
                    std::memcpy(&seq, buf.data(), sizeof(uint64_t));
                    std::memcpy(&send_ns, buf.data() + sizeof(uint64_t), sizeof(uint64_t));
                }

                long long latency_us = (n >= (ssize_t)sizeof(PayloadHeader))
                                           ? static_cast<long long>((now_ns - static_cast<long long>(send_ns)) / 1000)
                                           : -1;

                csv << seq << "," << n << "," << latency_us << "," << now_ns << "\n";
            }
        }
    }

    ::close(fd);
    std::cout << "Sniffer stopped. CSV written.\n";
    return 0;
}
