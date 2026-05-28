#pragma once

#include "broker.hpp"

#include <atomic>
#include <pthread.h>
#include <vector>

class TCPServer {
public:
    TCPServer(Broker& broker, int port = 9876);
    ~TCPServer();

    TCPServer(const TCPServer&) = delete;
    TCPServer& operator=(const TCPServer&) = delete;

    bool start();
    void stop();
    void join();

private:
    static void* thread_entry(void* arg);
    static void* client_thread_entry(void* arg);
    void run();

    Broker& broker_;
    int port_;
    int listen_fd_;
    std::atomic<bool> running_;
    pthread_t thread_;
    bool thread_started_;
    pthread_mutex_t client_lock_;
    std::vector<pthread_t> client_threads_;
};
