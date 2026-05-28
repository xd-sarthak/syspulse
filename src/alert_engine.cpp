#include "alert_engine.hpp"

AlertEngine::AlertEngine(ProcessStore& store,
                         Broker& broker,
                         int interval_ms,
                         double cpu_threshold,
                         long mem_threshold_kb)
    : store_(store),
      broker_(broker),
      interval_ms_(interval_ms),
      cpu_threshold_(cpu_threshold),
      mem_threshold_kb_(mem_threshold_kb),
      running_(false),
      thread_(),
      thread_started_(false)
{
}

AlertEngine::~AlertEngine()
{
    stop();
    join();
}

bool AlertEngine::start()
{
    return false;
}

void AlertEngine::stop()
{
    running_ = false;
}

void AlertEngine::join()
{
}

void* AlertEngine::thread_entry(void*)
{
    return nullptr;
}

void AlertEngine::run()
{
}
