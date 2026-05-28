#include "broker.hpp"

Broker::Broker()
{
    pthread_mutex_init(&lock_, nullptr);
}

Broker::~Broker()
{
    pthread_mutex_destroy(&lock_);
}

void Broker::register_client(int)
{
}

void Broker::unregister_client(int)
{
}

void Broker::publish_snapshots(const std::vector<ProcessSnapshot>&)
{
}

void Broker::publish_alert(const Alert&)
{
}

void Broker::publish_message(const std::string&)
{
}
