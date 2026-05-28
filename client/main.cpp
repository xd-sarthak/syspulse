#include "tui.hpp"

#include <arpa/inet.h>
#include <getopt.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <string>

namespace {

bool parse_endpoint(const std::string& endpoint, std::string& host, int& port)
{
    const size_t colon = endpoint.rfind(':');
    if (colon == std::string::npos) {
        return false;
    }
    host = endpoint.substr(0, colon);
    port = atoi(endpoint.substr(colon + 1).c_str());
    return !host.empty() && port > 0;
}

int connect_to_server(const std::string& host, int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        close(fd);
        return -1;
    }

    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }

    return fd;
}

void print_usage(const char* program)
{
    std::cerr << "Usage: " << program << " [-a address:port]\n";
}

} // namespace

int main(int argc, char** argv)
{
    std::string endpoint = "127.0.0.1:9876";

    int opt = 0;
    while ((opt = getopt(argc, argv, "a:h")) != -1) {
        switch (opt) {
        case 'a':
            endpoint = optarg;
            break;
        case 'h':
        default:
            print_usage(argv[0]);
            return opt == 'h' ? 0 : 1;
        }
    }

    std::string host;
    int port = 0;
    if (!parse_endpoint(endpoint, host, port)) {
        std::cerr << "invalid address: " << endpoint << "\n";
        return 1;
    }

    const int fd = connect_to_server(host, port);
    if (fd < 0) {
        std::cerr << "failed to connect to " << endpoint << "\n";
        return 1;
    }

    const int result = run_tui(fd, endpoint.c_str());
    close(fd);
    return result;
}
