#include <chrono>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <iostream>
#include <vector>
#include <thread>

using steady_clock = std::chrono::steady_clock;

struct PayloadHeader
{
    uint64_t seq;
    uint64_t send_ns; // steady_clock epoch
};

int main(int argc, char **argv)
{
    if (argc < 6)
    {
        std::fprintf(stderr,
                     "Usage: %s <dest_ip> <port> <num_packets> <packet_bytes> <interval_us>\n"
                     "Example: %s 127.0.0.1 9000 10000 512 1000\n",
                     argv[0], argv[0]);
        return 1;
    }
    const char *ip = argv[1];
    int port = std::stoi(argv[2]);
    uint64_t total = std::stoull(argv[3]);
    size_t pkt_bytes = std::stoull(argv[4]);
    int interval_us = std::stoi(argv[5]); // inter-packet gap

    if (pkt_bytes < sizeof(PayloadHeader))
        pkt_bytes = sizeof(PayloadHeader);

    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
    {
        perror("socket");
        return 1;
    }

    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(static_cast<uint16_t>(port));
    if (::inet_pton(AF_INET, ip, &dst.sin_addr) != 1)
    {
        std::cerr << "Bad IP\n";
        return 1;
    }

    std::vector<char> buf(pkt_bytes, 0);
    for (uint64_t seq = 1; seq <= total; ++seq)
    {
        auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          steady_clock::now().time_since_epoch())
                          .count();
        std::memcpy(buf.data(), &seq, sizeof(uint64_t));
        std::memcpy(buf.data() + sizeof(uint64_t), &now_ns, sizeof(uint64_t));
        // rest of payload is just zeros (or you can fill with pattern)

        ssize_t s = ::sendto(fd, buf.data(), buf.size(), 0,
                             reinterpret_cast<sockaddr *>(&dst), sizeof(dst));
        if (s < 0)
        {
            perror("sendto");
            break;
        }

        if (interval_us > 0)
            std::this_thread::sleep_for(std::chrono::microseconds(interval_us));
    }

    ::close(fd);
    std::cout << "Sender done.\n";
    return 0;
}
