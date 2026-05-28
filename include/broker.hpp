#pragma once

#include "process_snapshot.hpp"

#include <pthread.h>

#include <string>
#include <vector>

class Broker {
public:
    Broker();
    ~Broker();

    Broker(const Broker&) = delete;
    Broker& operator=(const Broker&) = delete;

    void register_client(int fd);
    void unregister_client(int fd);
    void publish_snapshots(const std::vector<ProcessSnapshot>& snaps);
    void publish_alert(const Alert& alert);

private:
    void publish_message(const std::string& message);

    std::vector<int> client_fds_;
    pthread_mutex_t lock_;
};
