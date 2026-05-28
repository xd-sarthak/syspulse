#include "broker.hpp"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>

namespace {

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

std::string json_escape(const char* text)
{
    std::string escaped;
    for (const char* p = text; p != nullptr && *p != '\0'; ++p) {
        switch (*p) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += *p;
            break;
        }
    }
    return escaped;
}

bool write_all(int fd, const std::string& message)
{
    const char* data = message.c_str();
    size_t remaining = message.size();
    while (remaining > 0) {
        ssize_t written = write(fd, data, remaining);
        if (written > 0) {
            data += written;
            remaining -= static_cast<size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

} // namespace

Broker::Broker()
{
    pthread_mutex_init(&lock_, nullptr);
}

Broker::~Broker()
{
    {
        MutexLock guard(lock_);
        for (int fd : client_fds_) {
            close(fd);
        }
        client_fds_.clear();
    }
    pthread_mutex_destroy(&lock_);
}

void Broker::register_client(int fd)
{
    MutexLock guard(lock_);
    client_fds_.push_back(fd);
}

void Broker::unregister_client(int fd)
{
    MutexLock guard(lock_);
    auto it = std::remove(client_fds_.begin(), client_fds_.end(), fd);
    if (it != client_fds_.end()) {
        client_fds_.erase(it, client_fds_.end());
    }
    close(fd);
}

void Broker::publish_snapshots(const std::vector<ProcessSnapshot>& snaps)
{
    std::string message = "{\"type\":\"snapshot_batch\",\"data\":[";
    bool first = true;
    char buffer[512];
    for (const ProcessSnapshot& snap : snaps) {
        if (!first) {
            message += ",";
        }
        first = false;
        const std::string name = json_escape(snap.name);
        snprintf(buffer,
                 sizeof(buffer),
                 "{\"pid\":%d,\"name\":\"%s\",\"state\":\"%c\",\"cpu\":%.2f,"
                 "\"mem_kb\":%ld,\"threads\":%d,\"fds\":%d}",
                 snap.pid,
                 name.c_str(),
                 snap.state,
                 snap.cpu_percent,
                 snap.mem_rss_kb,
                 snap.threads,
                 snap.fd_count);
        message += buffer;
    }
    message += "]}\n";
    publish_message(message);
}

void Broker::publish_alert(const Alert& alert)
{
    const std::string name = json_escape(alert.name);
    const std::string rule = json_escape(alert.rule);
    char buffer[512];
    snprintf(buffer,
             sizeof(buffer),
             "{\"type\":\"alert\",\"pid\":%d,\"name\":\"%s\",\"rule\":\"%s\","
             "\"value\":%.2f,\"ts\":%ld}\n",
             alert.pid,
             name.c_str(),
             rule.c_str(),
             alert.value,
             static_cast<long>(alert.timestamp));
    publish_message(buffer);
}

void Broker::publish_message(const std::string& message)
{
    std::vector<int> failed;
    {
        MutexLock guard(lock_);
        for (int fd : client_fds_) {
            if (!write_all(fd, message)) {
                failed.push_back(fd);
            }
        }
        for (int fd : failed) {
            auto it = std::remove(client_fds_.begin(), client_fds_.end(), fd);
            if (it != client_fds_.end()) {
                client_fds_.erase(it, client_fds_.end());
            }
            shutdown(fd, SHUT_RDWR);
        }
    }
}
