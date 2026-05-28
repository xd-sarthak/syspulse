#include "scraper.hpp"

#include "proc_parser.hpp"

#include <errno.h>
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
    if (thread_started_) {
        return true;
    }
    running_ = true;
    if (pthread_create(&thread_, nullptr, &Scraper::thread_entry, this) != 0) {
        running_ = false;
        return false;
    }
    thread_started_ = true;
    return true;
}

void Scraper::stop()
{
    running_ = false;
}

void Scraper::join()
{
    if (thread_started_) {
        pthread_join(thread_, nullptr);
        thread_started_ = false;
    }
}

void* Scraper::thread_entry(void* arg)
{
    Scraper* scraper = static_cast<Scraper*>(arg);
    scraper->run();
    return nullptr;
}

void Scraper::run()
{
    const long ticks_per_second = sysconf(_SC_CLK_TCK);

    while (running_) {
        const std::vector<int> pids = list_pids();
        for (int pid : pids) {
            ProcessSnapshot snap {};
            if (!parse_proc_stat(pid, snap)) {
                continue;
            }
            if (!parse_proc_status(pid, snap)) {
                continue;
            }
            snap.fd_count = count_fds(pid);

            ProcessSnapshot previous {};
            if (store_.get(pid, previous) && previous.wall_prev > 0 && snap.wall_prev > previous.wall_prev) {
                const long cpu_ticks = (snap.utime_prev - previous.utime_prev) +
                                       (snap.stime_prev - previous.stime_prev);
                const double elapsed_seconds =
                    static_cast<double>(snap.wall_prev - previous.wall_prev) / 1000.0;
                if (elapsed_seconds > 0.0 && ticks_per_second > 0 && cpu_ticks >= 0) {
                    snap.cpu_percent =
                        (static_cast<double>(cpu_ticks) /
                         (static_cast<double>(ticks_per_second) * elapsed_seconds)) *
                        100.0;
                }
            }

            store_.upsert(snap);
        }

        store_.cleanup_dead(pids);
        broker_.publish_snapshots(store_.get_all());
        sleep_ms_interruptible(interval_ms_, running_);
    }
}
