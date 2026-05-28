# procwatch

`procwatch` is a Linux process monitor daemon with a TCP streaming server and an ncurses client.

![procwatch screenshot](src/ChatGPT%20Image%20May%2028%2C%202026%2C%2008_38_51%20AM.png)

```
             /proc
               |
               v
        +--------------+
        |   Scraper    |
        +------+-------+
               |
               v
        +--------------+        +--------------+
        | ProcessStore |<-------+ AlertEngine  |
        +------+-------+        +------+-------+
               |                       |
               v                       v
        +-------------------------------+
        |            Broker             |
        +---------------+---------------+
                        |
                        v
              +------------------+
              | TCPServer :9876  |
              +--------+---------+
                       / \
                      /   \
                     v     v
             ncurses clients
```

## Build

```sh
mkdir build
cd build
cmake ..
make
```

The project uses C++17, CMake, pthreads, and ncurses. No JSON library or external process helpers are used.

## Run

Start the daemon:

```sh
./procwatchd -p 9876 -i 1000
```

Start the client:

```sh
./procwatch-client -a 127.0.0.1:9876
```

Client keys:

```text
q  quit
s  sort by CPU
m  sort by memory
p  sort by PID
r  reverse sort
```

## How It Works

The scraper lists numeric directories in `/proc`, then reads `/proc/[pid]/stat`, `/proc/[pid]/status`, and `/proc/[pid]/fd` directly. It skips processes that disappear during a scan.

CPU usage is calculated from deltas. Each scan records `utime`, `stime`, and monotonic wall time. On the next scan, `procwatch` divides the process CPU tick delta by elapsed wall seconds and `_SC_CLK_TCK`, then multiplies by 100.

`ProcessStore` keeps the latest snapshot per PID behind a `pthread_rwlock_t`. The broker serializes complete snapshot batches and alerts as newline-delimited JSON and fans them out to every connected TCP client. Broken connections are removed, and `SIGPIPE` is ignored by the daemon.

The ncurses client reads newline-delimited JSON on a reader pthread and renders a live table with CPU-based coloring, recent alerts, host information, uptime, sorting, and reverse ordering.
