#pragma once

#include "broker.hpp"
#include "process_store.hpp"

#include <atomic>
#include <pthread.h>
#include <unordered_map>

class AlertEngine {
public:
    AlertEngine(ProcessStore& store,
                Broker& broker,
                int interval_ms = 1000,
                double cpu_threshold = 80.0,
                long mem_threshold_kb = 500L * 1024L);
    ~AlertEngine();

    AlertEngine(const AlertEngine&) = delete;
    AlertEngine& operator=(const AlertEngine&) = delete;

    bool start();
    void stop();
    void join();

private:
    static void* thread_entry(void* arg);
    void run();

    ProcessStore& store_;
    Broker& broker_;
    int interval_ms_;
    double cpu_threshold_;
    long mem_threshold_kb_;
    std::atomic<bool> running_;
    pthread_t thread_;
    bool thread_started_;
    std::unordered_map<int, int> cpu_consecutive_ticks_;
};
