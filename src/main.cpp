#include "alert_engine.hpp"
#include "broker.hpp"
#include "process_store.hpp"
#include "scraper.hpp"
#include "tcp_server.hpp"

#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>

#include <iostream>

namespace {

void print_usage(const char* program)
{
    std::cerr << "Usage: " << program
              << " [-p port] [-i scrape_ms] [-c cpu_threshold] [-m mem_mb]\n";
}

} // namespace

int main(int argc, char** argv)
{
    int port = 9876;
    int interval_ms = 1000;
    double cpu_threshold = 80.0;
    long mem_threshold_mb = 500;

    int opt = 0;
    while ((opt = getopt(argc, argv, "p:i:c:m:h")) != -1) {
        switch (opt) {
        case 'p':
            port = atoi(optarg);
            break;
        case 'i':
            interval_ms = atoi(optarg);
            break;
        case 'c':
            cpu_threshold = atof(optarg);
            break;
        case 'm':
            mem_threshold_mb = atol(optarg);
            break;
        case 'h':
        default:
            print_usage(argv[0]);
            return opt == 'h' ? 0 : 1;
        }
    }

    signal(SIGPIPE, SIG_IGN);

    sigset_t signal_set;
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGINT);
    sigaddset(&signal_set, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &signal_set, nullptr);

    ProcessStore store;
    Broker broker;
    Scraper scraper(store, broker, interval_ms);
    AlertEngine alerts(store, broker, interval_ms, cpu_threshold, mem_threshold_mb * 1024L);
    TCPServer server(broker, port);

    if (!server.start()) {
        std::cerr << "failed to start TCP server\n";
        return 1;
    }
    if (!scraper.start()) {
        std::cerr << "failed to start scraper\n";
        server.stop();
        server.join();
        return 1;
    }
    if (!alerts.start()) {
        std::cerr << "failed to start alert engine\n";
        scraper.stop();
        server.stop();
        scraper.join();
        server.join();
        return 1;
    }

    std::cout << "procwatch daemon started on port " << port << std::endl;

    int received = 0;
    sigwait(&signal_set, &received);

    alerts.stop();
    scraper.stop();
    server.stop();
    alerts.join();
    scraper.join();
    server.join();

    return 0;
}
