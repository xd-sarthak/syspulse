#include "process_store.hpp"

#include <algorithm>
#include <unordered_set>

namespace {

class ReadLock {
public:
    explicit ReadLock(pthread_rwlock_t& lock) : lock_(lock)
    {
        pthread_rwlock_rdlock(&lock_);
    }

    ~ReadLock()
    {
        pthread_rwlock_unlock(&lock_);
    }

private:
    pthread_rwlock_t& lock_;
};

class WriteLock {
public:
    explicit WriteLock(pthread_rwlock_t& lock) : lock_(lock)
    {
        pthread_rwlock_wrlock(&lock_);
    }

    ~WriteLock()
    {
        pthread_rwlock_unlock(&lock_);
    }

private:
    pthread_rwlock_t& lock_;
};

} // namespace

ProcessStore::ProcessStore()
{
    pthread_rwlock_init(&lock_, nullptr);
}

ProcessStore::~ProcessStore()
{
    pthread_rwlock_destroy(&lock_);
}

void ProcessStore::upsert(const ProcessSnapshot& snap)
{
    WriteLock guard(lock_);
    store_[snap.pid] = snap;
}

void ProcessStore::remove(int pid)
{
    WriteLock guard(lock_);
    store_.erase(pid);
}

std::vector<ProcessSnapshot> ProcessStore::get_all()
{
    ReadLock guard(lock_);
    std::vector<ProcessSnapshot> snapshots;
    snapshots.reserve(store_.size());
    for (const auto& item : store_) {
        snapshots.push_back(item.second);
    }
    return snapshots;
}

void ProcessStore::cleanup_dead(const std::vector<int>& live_pids)
{
    WriteLock guard(lock_);
    const std::unordered_set<int> live(live_pids.begin(), live_pids.end());
    for (auto it = store_.begin(); it != store_.end();) {
        if (live.find(it->first) == live.end()) {
            it = store_.erase(it);
        } else {
            ++it;
        }
    }
}

bool ProcessStore::get(int pid, ProcessSnapshot& snap)
{
    ReadLock guard(lock_);
    const auto it = store_.find(pid);
    if (it == store_.end()) {
        return false;
    }
    snap = it->second;
    return true;
}
