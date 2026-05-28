#pragma once

#include "process_snapshot.hpp"

#include <pthread.h>

#include <unordered_map>
#include <vector>

class ProcessStore {
public:
    ProcessStore();
    ~ProcessStore();

    ProcessStore(const ProcessStore&) = delete;
    ProcessStore& operator=(const ProcessStore&) = delete;

    void upsert(const ProcessSnapshot& snap);
    void remove(int pid);
    std::vector<ProcessSnapshot> get_all();
    void cleanup_dead(const std::vector<int>& live_pids);
    bool get(int pid, ProcessSnapshot& snap);

private:
    std::unordered_map<int, ProcessSnapshot> store_;
    pthread_rwlock_t lock_;
};
