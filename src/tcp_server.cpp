#include "tcp_server.hpp"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
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

} // namespace

TCPServer::TCPServer(Broker& broker, int port)
    : broker_(broker),
      port_(port),
      listen_fd_(-1),
      running_(false),
      thread_(),
      thread_started_(false)
{
    pthread_mutex_init(&client_lock_, nullptr);
}

TCPServer::~TCPServer()
{
    stop();
    join();
    pthread_mutex_destroy(&client_lock_);
}

bool TCPServer::start()
{
    if (thread_started_) {
        return true;
    }
    running_ = true;
    if (pthread_create(&thread_, nullptr, &TCPServer::thread_entry, this) != 0) {
        running_ = false;
        return false;
    }
    thread_started_ = true;
    return true;
}

void TCPServer::stop()
{
    running_ = false;
    if (listen_fd_ >= 0) {
        shutdown(listen_fd_, SHUT_RDWR);
        close(listen_fd_);
        listen_fd_ = -1;
    }

    MutexLock guard(client_lock_);
    for (const ClientThread& client : client_threads_) {
        if (client.fd >= 0) {
            shutdown(client.fd, SHUT_RDWR);
        }
    }
}

void TCPServer::join()
{
    if (thread_started_) {
        pthread_join(thread_, nullptr);
        thread_started_ = false;
    }

    std::vector<ClientThread> clients;
    {
        MutexLock guard(client_lock_);
        clients.swap(client_threads_);
    }
    for (const ClientThread& client : clients) {
        pthread_join(client.thread, nullptr);
    }
}

void* TCPServer::thread_entry(void* arg)
{
    TCPServer* server = static_cast<TCPServer*>(arg);
    server->run();
    return nullptr;
}

void* TCPServer::client_thread_entry(void* arg)
{
    ClientContext* context = static_cast<ClientContext*>(arg);
    TCPServer* server = context->server;
    const int fd = context->fd;
    delete context;

    server->broker_.register_client(fd);

    char buffer[256];
    while (server->running_) {
        const ssize_t bytes = read(fd, buffer, sizeof(buffer));
        if (bytes > 0) {
            continue;
        }
        if (bytes < 0 && errno == EINTR) {
            continue;
        }
        break;
    }

    server->broker_.unregister_client(fd);
    return nullptr;
}

void TCPServer::run()
{
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        running_ = false;
        return;
    }

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        running_ = false;
        return;
    }

    if (listen(listen_fd_, 10) != 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        running_ = false;
        return;
    }

    while (running_) {
        sockaddr_in client_addr {};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (!running_) {
                break;
            }
            continue;
        }

        ClientContext* context = new ClientContext {this, client_fd};
        pthread_t client_thread {};
        if (pthread_create(&client_thread, nullptr, &TCPServer::client_thread_entry, context) != 0) {
            delete context;
            close(client_fd);
            continue;
        }

        MutexLock guard(client_lock_);
        client_threads_.push_back(ClientThread {client_thread, client_fd});
    }
}
