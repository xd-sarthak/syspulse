#include "alert_engine.hpp"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>

namespace {

void sleep_ms_interruptible(int interval_ms, const std::atomic<bool>& running)
{
    int remaining = interval_ms;
    while (running && remaining > 0) {
        const int chunk = std::min(remaining, 100);
        usleep(static_cast<useconds_t>(chunk * 1000));
        remaining -= chunk;
    }
}

} // namespace

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
    if (thread_started_) {
        return true;
    }
    running_ = true;
    if (pthread_create(&thread_, nullptr, &AlertEngine::thread_entry, this) != 0) {
        running_ = false;
        return false;
    }
    thread_started_ = true;
    return true;
}

void AlertEngine::stop()
{
    running_ = false;
}

void AlertEngine::join()
{
    if (thread_started_) {
        pthread_join(thread_, nullptr);
        thread_started_ = false;
    }
}

void* AlertEngine::thread_entry(void* arg)
{
    AlertEngine* engine = static_cast<AlertEngine*>(arg);
    engine->run();
    return nullptr;
}

void AlertEngine::run()
{
    while (running_) {
        const std::vector<ProcessSnapshot> snapshots = store_.get_all();
        for (const ProcessSnapshot& snap : snapshots) {
            const bool high_cpu = snap.cpu_percent > cpu_threshold_;
            int& ticks = cpu_consecutive_ticks_[snap.pid];
            if (high_cpu) {
                ++ticks;
                if (ticks == 3) {
                    Alert alert {};
                    alert.pid = snap.pid;
                    snprintf(alert.name, sizeof(alert.name), "%s", snap.name);
                    snprintf(alert.rule, sizeof(alert.rule), "CPU>%.0f%%", cpu_threshold_);
                    alert.value = snap.cpu_percent;
                    alert.timestamp = time(nullptr);
                    broker_.publish_alert(alert);
                }
            } else {
                ticks = 0;
            }

            if (snap.mem_rss_kb > mem_threshold_kb_) {
                Alert alert {};
                alert.pid = snap.pid;
                snprintf(alert.name, sizeof(alert.name), "%s", snap.name);
                snprintf(alert.rule,
                         sizeof(alert.rule),
                         "MEM>%ldMB",
                         mem_threshold_kb_ / 1024L);
                alert.value = static_cast<double>(snap.mem_rss_kb);
                alert.timestamp = time(nullptr);
                broker_.publish_alert(alert);
            }
        }

        sleep_ms_interruptible(interval_ms_, running_);
    }
}
