#include "tui.hpp"

#include "process_snapshot.hpp"

#include <ncurses.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

namespace {

enum class SortMode {
    Cpu,
    Mem,
    Pid
};

struct ClientState {
    int fd;
    pthread_mutex_t lock;
    std::vector<ProcessSnapshot> snapshots;
    std::vector<Alert> alerts;
    std::atomic<bool> running;
    bool dirty;
};

class MutexLock {
public:
    explicit MutexLock(pthread_mutex_t& lock) : lock_(lock)
    {
        pthread_mutex_lock(&lock_);
    }

    ~MutexLock()
    {
        pthread_mutex_unlock(&lock_);
    }

private:
    pthread_mutex_t& lock_;
};

std::string read_uptime()
{
    FILE* file = fopen("/proc/uptime", "r");
    if (file == nullptr) {
        return "uptime n/a";
    }
    double seconds = 0.0;
    if (fscanf(file, "%lf", &seconds) != 1) {
        fclose(file);
        return "uptime n/a";
    }
    fclose(file);

    const long total = static_cast<long>(seconds);
    const long days = total / 86400L;
    const long hours = (total % 86400L) / 3600L;
    const long minutes = (total % 3600L) / 60L;
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "up %ldd %ldh %ldm", days, hours, minutes);
    return buffer;
}

std::string hostname_text()
{
    char hostname[128] {};
    if (gethostname(hostname, sizeof(hostname) - 1) != 0) {
        return "unknown-host";
    }
    return hostname;
}

bool extract_int(const char* object, const char* key, int& value)
{
    const char* found = strstr(object, key);
    if (found == nullptr) {
        return false;
    }
    return sscanf(found + strlen(key), "%d", &value) == 1;
}

bool extract_long(const char* object, const char* key, long& value)
{
    const char* found = strstr(object, key);
    if (found == nullptr) {
        return false;
    }
    return sscanf(found + strlen(key), "%ld", &value) == 1;
}

bool extract_double(const char* object, const char* key, double& value)
{
    const char* found = strstr(object, key);
    if (found == nullptr) {
        return false;
    }
    return sscanf(found + strlen(key), "%lf", &value) == 1;
}

bool extract_string(const char* object, const char* key, char* out, size_t out_size)
{
    const char* found = strstr(object, key);
    if (found == nullptr || out_size == 0) {
        return false;
    }
    found += strlen(key);

    size_t written = 0;
    for (const char* p = found; *p != '\0'; ++p) {
        if (*p == '"') {
            out[written] = '\0';
            return true;
        }
        char value = *p;
        if (*p == '\\') {
            ++p;
            if (*p == '\0') {
                return false;
            }
            switch (*p) {
            case 'n':
                value = '\n';
                break;
            case 'r':
                value = '\r';
                break;
            case 't':
                value = '\t';
                break;
            case '\\':
            case '"':
                value = *p;
                break;
            default:
                value = *p;
                break;
            }
        }
        if (written + 1 < out_size) {
            out[written++] = value;
        }
    }

    return false;
}

void parse_snapshot_batch(const std::string& line, ClientState& state)
{
    std::vector<ProcessSnapshot> snapshots;
    const char* cursor = line.c_str();
    while ((cursor = strstr(cursor, "{\"pid\":")) != nullptr) {
        const char* end = strchr(cursor + 1, '}');
        if (end == nullptr) {
            break;
        }
        std::string object(cursor, static_cast<size_t>(end - cursor + 1));
        ProcessSnapshot snap {};
        char state_text[8] {};
        if (extract_int(object.c_str(), "\"pid\":", snap.pid) &&
            extract_string(object.c_str(), "\"name\":\"", snap.name, sizeof(snap.name)) &&
            extract_string(object.c_str(), "\"state\":\"", state_text, sizeof(state_text)) &&
            extract_double(object.c_str(), "\"cpu\":", snap.cpu_percent) &&
            extract_long(object.c_str(), "\"mem_kb\":", snap.mem_rss_kb) &&
            extract_int(object.c_str(), "\"threads\":", snap.threads) &&
            extract_int(object.c_str(), "\"fds\":", snap.fd_count)) {
            snap.state = state_text[0];
            snapshots.push_back(snap);
        }
        cursor = end + 1;
    }

    MutexLock guard(state.lock);
    state.snapshots.swap(snapshots);
    state.dirty = true;
}

void parse_alert(const std::string& line, ClientState& state)
{
    Alert alert {};
    long ts = 0;
    if (!extract_int(line.c_str(), "\"pid\":", alert.pid) ||
        !extract_string(line.c_str(), "\"name\":\"", alert.name, sizeof(alert.name)) ||
        !extract_string(line.c_str(), "\"rule\":\"", alert.rule, sizeof(alert.rule)) ||
        !extract_double(line.c_str(), "\"value\":", alert.value) ||
        !extract_long(line.c_str(), "\"ts\":", ts)) {
        return;
    }
    alert.timestamp = static_cast<time_t>(ts);

    MutexLock guard(state.lock);
    state.alerts.push_back(alert);
    if (state.alerts.size() > 3) {
        state.alerts.erase(state.alerts.begin(), state.alerts.end() - 3);
    }
    state.dirty = true;
}

void parse_line(const std::string& line, ClientState& state)
{
    if (strstr(line.c_str(), "\"type\":\"snapshot_batch\"") != nullptr) {
        parse_snapshot_batch(line, state);
    } else if (strstr(line.c_str(), "\"type\":\"alert\"") != nullptr) {
        parse_alert(line, state);
    }
}

void* reader_thread_entry(void* arg)
{
    ClientState* state = static_cast<ClientState*>(arg);
    std::string pending;
    char buffer[4096];

    while (state->running) {
        const ssize_t bytes = recv(state->fd, buffer, sizeof(buffer), 0);
        if (bytes > 0) {
            pending.append(buffer, static_cast<size_t>(bytes));
            size_t newline = pending.find('\n');
            while (newline != std::string::npos) {
                const std::string line = pending.substr(0, newline);
                pending.erase(0, newline + 1);
                parse_line(line, *state);
                newline = pending.find('\n');
            }
            continue;
        }
        break;
    }

    state->running = false;
    return nullptr;
}

void draw_alerts(int row, const std::vector<Alert>& alerts)
{
    move(row, 0);
    clrtoeol();
    attron(A_BOLD);
    printw("Alerts: ");
    attroff(A_BOLD);
    if (alerts.empty()) {
        printw("none");
        return;
    }
    for (size_t i = 0; i < alerts.size(); ++i) {
        if (i > 0) {
            printw(" | ");
        }
        printw("%d %s %.1f", alerts[i].pid, alerts[i].rule, alerts[i].value);
    }
}

void draw_table(int start_row, const std::vector<ProcessSnapshot>& snapshots)
{
    mvprintw(start_row, 0, "%-7s %-28s %-5s %8s %9s %8s %5s",
             "PID",
             "NAME",
             "STATE",
             "CPU%",
             "MEM(MB)",
             "THREADS",
             "FDs");
    const int max_rows = LINES - start_row - 1;
    for (int i = 0; i < max_rows && i < static_cast<int>(snapshots.size()); ++i) {
        const ProcessSnapshot& snap = snapshots[static_cast<size_t>(i)];
        const int row = start_row + 1 + i;
        int color = 3;
        if (snap.cpu_percent > 50.0) {
            color = 1;
        } else if (snap.cpu_percent > 20.0) {
            color = 2;
        }
        attron(COLOR_PAIR(color));
        mvprintw(row,
                 0,
                 "%-7d %-28.28s %-5c %8.1f %9.1f %8d %5d",
                 snap.pid,
                 snap.name,
                 snap.state,
                 snap.cpu_percent,
                 static_cast<double>(snap.mem_rss_kb) / 1024.0,
                 snap.threads,
                 snap.fd_count);
        attroff(COLOR_PAIR(color));
    }
}

void sort_snapshots(std::vector<ProcessSnapshot>& snapshots, SortMode sort_mode, bool reverse)
{
    std::sort(snapshots.begin(), snapshots.end(), [sort_mode](const ProcessSnapshot& a,
                                                              const ProcessSnapshot& b) {
        switch (sort_mode) {
        case SortMode::Mem:
            return a.mem_rss_kb > b.mem_rss_kb;
        case SortMode::Pid:
            return a.pid < b.pid;
        case SortMode::Cpu:
        default:
            return a.cpu_percent > b.cpu_percent;
        }
    });
    if (reverse) {
        std::reverse(snapshots.begin(), snapshots.end());
    }
}

} // namespace

int run_tui(int socket_fd, const char* endpoint)
{
    ClientState state {};
    state.fd = socket_fd;
    state.running = true;
    state.dirty = true;
    pthread_mutex_init(&state.lock, nullptr);

    pthread_t reader {};
    if (pthread_create(&reader, nullptr, &reader_thread_entry, &state) != 0) {
        pthread_mutex_destroy(&state.lock);
        return 1;
    }

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    curs_set(0);
    start_color();
    use_default_colors();
    init_pair(1, COLOR_RED, -1);
    init_pair(2, COLOR_YELLOW, -1);
    init_pair(3, COLOR_GREEN, -1);

    SortMode sort_mode = SortMode::Cpu;
    bool reverse = false;
    const std::string host = hostname_text();

    while (state.running) {
        const int ch = getch();
        if (ch == 'q') {
            state.running = false;
            break;
        }
        if (ch == 's') {
            sort_mode = SortMode::Cpu;
        } else if (ch == 'm') {
            sort_mode = SortMode::Mem;
        } else if (ch == 'p') {
            sort_mode = SortMode::Pid;
        } else if (ch == 'r') {
            reverse = !reverse;
        }

        std::vector<ProcessSnapshot> snapshots;
        std::vector<Alert> alerts;
        {
            MutexLock guard(state.lock);
            snapshots = state.snapshots;
            alerts = state.alerts;
            state.dirty = false;
        }
        sort_snapshots(snapshots, sort_mode, reverse);

        erase();
        mvprintw(0,
                 0,
                 "procwatch-client  %s  %s  %s",
                 endpoint,
                 host.c_str(),
                 read_uptime().c_str());
        mvprintw(1,
                 0,
                 "Processes: %zu   Sort: %s%s",
                 snapshots.size(),
                 sort_mode == SortMode::Cpu ? "CPU" : (sort_mode == SortMode::Mem ? "MEM" : "PID"),
                 reverse ? " reversed" : "");
        draw_alerts(2, alerts);
        draw_table(4, snapshots);
        refresh();

        usleep(100000);
    }

    shutdown(socket_fd, SHUT_RDWR);
    pthread_join(reader, nullptr);
    endwin();
    pthread_mutex_destroy(&state.lock);
    return 0;
}
