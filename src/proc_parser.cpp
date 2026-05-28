#include "proc_parser.hpp"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <string>

namespace {

long monotonic_ms()
{
    timespec ts {};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (ts.tv_sec * 1000L) + (ts.tv_nsec / 1000000L);
}

bool read_file(const std::string& path, std::string& out)
{
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return false;
    }

    char buffer[4096];
    out.clear();
    while (true) {
        ssize_t bytes = read(fd, buffer, sizeof(buffer));
        if (bytes > 0) {
            out.append(buffer, static_cast<size_t>(bytes));
            continue;
        }
        if (bytes == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        close(fd);
        return false;
    }

    close(fd);
    return true;
}

bool is_numeric_name(const char* name)
{
    if (name == nullptr || *name == '\0') {
        return false;
    }
    for (const char* p = name; *p != '\0'; ++p) {
        if (!isdigit(static_cast<unsigned char>(*p))) {
            return false;
        }
    }
    return true;
}

} // namespace

bool parse_proc_stat(int pid, ProcessSnapshot& snap)
{
    std::string content;
    if (!read_file("/proc/" + std::to_string(pid) + "/stat", content)) {
        return false;
    }

    const size_t open_paren = content.find('(');
    const size_t close_paren = content.rfind(')');
    if (open_paren == std::string::npos || close_paren == std::string::npos ||
        close_paren <= open_paren) {
        return false;
    }

    const std::string pid_text = content.substr(0, open_paren);
    char* end = nullptr;
    const long parsed_pid = strtol(pid_text.c_str(), &end, 10);
    if (end == pid_text.c_str()) {
        return false;
    }

    const std::string name = content.substr(open_paren + 1, close_paren - open_paren - 1);
    const std::string rest = content.substr(close_paren + 2);
    char state = '\0';
    long values[64] {};
    int parsed = sscanf(rest.c_str(),
                        "%c %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld",
                        &state,
                        &values[0],
                        &values[1],
                        &values[2],
                        &values[3],
                        &values[4],
                        &values[5],
                        &values[6],
                        &values[7],
                        &values[8],
                        &values[9],
                        &values[10],
                        &values[11]);
    if (parsed != 13) {
        return false;
    }

    memset(&snap, 0, sizeof(snap));
    snap.pid = static_cast<int>(parsed_pid);
    snprintf(snap.name, sizeof(snap.name), "%s", name.c_str());
    snap.state = state;
    snap.cpu_percent = 0.0;
    snap.utime_prev = values[10];
    snap.stime_prev = values[11];
    snap.wall_prev = monotonic_ms();
    snap.timestamp = time(nullptr);
    return true;
}

bool parse_proc_status(int pid, ProcessSnapshot& snap)
{
    std::string content;
    if (!read_file("/proc/" + std::to_string(pid) + "/status", content)) {
        return false;
    }

    snap.mem_rss_kb = 0;
    snap.threads = 0;

    size_t pos = 0;
    while (pos < content.size()) {
        size_t line_end = content.find('\n', pos);
        if (line_end == std::string::npos) {
            line_end = content.size();
        }
        const std::string line = content.substr(pos, line_end - pos);
        long value = 0;
        if (sscanf(line.c_str(), "VmRSS: %ld kB", &value) == 1) {
            snap.mem_rss_kb = value;
        } else if (sscanf(line.c_str(), "Threads: %ld", &value) == 1) {
            snap.threads = static_cast<int>(value);
        }
        pos = line_end + 1;
    }

    return true;
}

int count_fds(int pid)
{
    DIR* dir = opendir(("/proc/" + std::to_string(pid) + "/fd").c_str());
    if (dir == nullptr) {
        return 0;
    }

    int count = 0;
    while (dirent* entry = readdir(dir)) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            ++count;
        }
    }

    closedir(dir);
    return count;
}

std::vector<int> list_pids()
{
    std::vector<int> pids;
    DIR* dir = opendir("/proc");
    if (dir == nullptr) {
        return pids;
    }

    while (dirent* entry = readdir(dir)) {
        if (is_numeric_name(entry->d_name)) {
            pids.push_back(static_cast<int>(strtol(entry->d_name, nullptr, 10)));
        }
    }

    closedir(dir);
    std::sort(pids.begin(), pids.end());
    return pids;
}
