#include "scraper.hpp"

Scraper::Scraper(ProcessStore& store, Broker& broker, int interval_ms)
    : store_(store),
      broker_(broker),
      interval_ms_(interval_ms),
      running_(false),
      thread_(),
      thread_started_(false)
{
}

Scraper::~Scraper()
{
    stop();
    join();
}

bool Scraper::start()
{
    return false;
}

void Scraper::stop()
{
    running_ = false;
}

void Scraper::join()
{
}

void* Scraper::thread_entry(void*)
{
    return nullptr;
}

void Scraper::run()
{
}
