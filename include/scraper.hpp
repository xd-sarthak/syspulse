#pragma once

#include "broker.hpp"
#include "process_store.hpp"

#include <atomic>
#include <pthread.h>

class Scraper {
public:
    Scraper(ProcessStore& store, Broker& broker, int interval_ms = 1000);
    ~Scraper();

    Scraper(const Scraper&) = delete;
    Scraper& operator=(const Scraper&) = delete;

    bool start();
    void stop();
    void join();

private:
    static void* thread_entry(void* arg);
    void run();

    ProcessStore& store_;
    Broker& broker_;
    int interval_ms_;
    std::atomic<bool> running_;
    pthread_t thread_;
    bool thread_started_;
};
