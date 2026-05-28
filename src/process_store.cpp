#include "process_store.hpp"

ProcessStore::ProcessStore()
{
    pthread_rwlock_init(&lock_, nullptr);
}

ProcessStore::~ProcessStore()
{
    pthread_rwlock_destroy(&lock_);
}

void ProcessStore::upsert(const ProcessSnapshot&)
{
}

void ProcessStore::remove(int)
{
}

std::vector<ProcessSnapshot> ProcessStore::get_all()
{
    return {};
}

void ProcessStore::cleanup_dead(const std::vector<int>&)
{
}

bool ProcessStore::get(int, ProcessSnapshot&)
{
    return false;
}
