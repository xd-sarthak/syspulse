#include "tcp_server.hpp"

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
    return false;
}

void TCPServer::stop()
{
    running_ = false;
}

void TCPServer::join()
{
}

void* TCPServer::thread_entry(void*)
{
    return nullptr;
}

void* TCPServer::client_thread_entry(void*)
{
    return nullptr;
}

void TCPServer::run()
{
}
