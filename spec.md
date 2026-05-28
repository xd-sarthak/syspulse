Build a C++17 project called "procwatch" — a Linux process monitor daemon with a TCP streaming server and ncurses client. Use CMake as the build system. No external libraries except ncurses and pthreads.

## Project Structure
procwatch/
├── CMakeLists.txt
├── README.md
├── include/
│   ├── process_snapshot.hpp   # POD struct for process data
│   ├── proc_parser.hpp        # /proc parsing declarations
│   ├── process_store.hpp      # thread-safe store declarations
│   ├── broker.hpp             # fan-out broker declarations
│   ├── tcp_server.hpp         # TCP server declarations
│   ├── alert_engine.hpp       # alerting engine declarations
│   └── scraper.hpp            # scraper thread declarations
├── src/
│   ├── main.cpp               # daemon entrypoint
│   ├── proc_parser.cpp        # /proc parsing implementation
│   ├── process_store.cpp      # thread-safe store implementation
│   ├── broker.cpp             # broker implementation
│   ├── tcp_server.cpp         # TCP server implementation
│   ├── alert_engine.cpp       # alert engine implementation
│   └── scraper.cpp            # scraper thread implementation
└── client/
    ├── CMakeLists.txt
    ├── main.cpp               # client entrypoint
    └── tui.cpp                # ncurses TUI implementation

## Data Structures

### process_snapshot.hpp
```cpp
struct ProcessSnapshot {
    int     pid;
    char    name[256];
    char    state;          // R, S, D, Z, T
    double  cpu_percent;    // delta utime+stime / delta wall_time * 100
    long    mem_rss_kb;     // VmRSS from /proc/[pid]/status
    int     threads;        // Threads from /proc/[pid]/status
    int     fd_count;       // count of /proc/[pid]/fd entries
    long    utime_prev;     // for CPU delta calculation
    long    stime_prev;
    long    wall_prev;
    time_t  timestamp;
};

struct Alert {
    int    pid;
    char   name[256];
    char   rule[64];        // e.g. "CPU>80%" or "MEM>500MB"
    double value;
    time_t timestamp;
};

// Wire message — serialize to JSON manually
struct Message {
    char type[16];          // "snapshot_batch" or "alert"
    // payload is either vector<ProcessSnapshot> or Alert
};
```

## proc_parser.cpp
Implement these functions (NO use of popen, system(), or any external process):
- `bool parse_proc_stat(int pid, ProcessSnapshot& snap)` — open and parse /proc/[pid]/stat. Fields: pid(1), comm(2), state(3), utime(14), stime(15). CPU% = (delta_utime + delta_stime) / (sysconf(_SC_CLK_TCK) * delta_wall_seconds) * 100
- `bool parse_proc_status(int pid, ProcessSnapshot& snap)` — parse /proc/[pid]/status for VmRSS (kB) and Threads
- `int count_fds(int pid)` — opendir /proc/[pid]/fd, count entries, closedir
- `std::vector<int> list_pids()` — opendir /proc, return all numeric directory names as ints

## process_store.cpp
Thread-safe store using pthread_rwlock_t:
- `void upsert(const ProcessSnapshot& snap)` — write lock, insert or update map
- `void remove(int pid)` — write lock, erase from map
- `std::vector<ProcessSnapshot> get_all()` — read lock, return copy of all values
- `void cleanup_dead(const std::vector<int>& live_pids)` — write lock, remove PIDs not in live_pids
Internal: `std::unordered_map<int, ProcessSnapshot> store_` protected by `pthread_rwlock_t lock_`

## scraper.cpp
- Single pthread that loops every `interval_ms` milliseconds
- Each iteration: call list_pids(), for each PID call parse_proc_stat + parse_proc_status + count_fds
- Handle ENOENT gracefully (process died mid-scan — just skip)
- After full scan: call store_.cleanup_dead(), then broker_.publish_snapshots(store_.get_all())
- Stoppable via atomic bool `running_`
- CPU% must be computed as a delta from the previous tick (store utime_prev, stime_prev, wall_prev per PID)

## broker.cpp
Fan-out broker:
- Maintains `std::vector<int> client_fds_` protected by `pthread_mutex_t`
- `void register_client(int fd)` — add fd to list
- `void unregister_client(int fd)` — remove fd from list, close(fd)
- `void publish_snapshots(const std::vector<ProcessSnapshot>& snaps)` — serialize ALL snapshots to a single JSON string, write to every client fd. On write error (broken pipe), mark fd for removal
- `void publish_alert(const Alert& alert)` — serialize alert to JSON, fan-out same way
- JSON format for snapshot_batch:
  `{"type":"snapshot_batch","data":[{"pid":123,"name":"bash","state":"S","cpu":1.2,"mem_kb":4096,"threads":1,"fds":5},...]}\n`
- JSON format for alert:
  `{"type":"alert","pid":123,"name":"bash","rule":"CPU>80%","value":85.3,"ts":1234567890}\n`
- Hand-roll the JSON serialization using snprintf/std::string — do NOT use any JSON library

## tcp_server.cpp
- `socket(AF_INET, SOCK_STREAM, 0)` → `setsockopt SO_REUSEADDR` → `bind` → `listen(backlog=10)`
- Accept loop: `accept()` → `pthread_create` for each client connection
- Client handler thread: just calls `broker_.register_client(fd)` then blocks until client disconnects (read returns 0), then `broker_.unregister_client(fd)`
- Port configurable via constructor arg (default 9876)
- Graceful shutdown: close listen socket, set running_ = false

## alert_engine.cpp
- Separate pthread, wakes every `interval_ms`
- Reads `store_.get_all()` each tick
- Rules (configurable via constructor, with defaults):
  - CPU > 80.0% for 3 consecutive ticks → fire alert
  - MemRSS > 500 * 1024 KB for 1 tick → fire alert
- Stateful: `std::unordered_map<int, int> cpu_consecutive_ticks_` per PID
- On alert: `broker_.publish_alert(alert)`
- Reset counter when condition clears

## main.cpp (daemon)
- CLI flags using getopt:
  - `-p <port>` — TCP port (default 9876)
  - `-i <ms>` — scrape interval in ms (default 1000)
  - `-c <float>` — CPU alert threshold % (default 80.0)
  - `-m <mb>` — memory alert threshold MB (default 500)
- Instantiate ProcessStore, Broker, Scraper, AlertEngine, TCPServer
- Start all threads
- Install SIGINT/SIGTERM handler: set all running_ flags to false, join threads
- Print startup banner: "procwatch daemon started on port XXXX"

## client/tui.cpp (ncurses client)
- Connect to 127.0.0.1:9876 via TCP (addr configurable via -a flag)
- Spawn a reader pthread that reads newline-delimited JSON from socket
- Parse JSON manually (sscanf / strstr based) — no JSON lib
- Main thread runs ncurses loop:
  - Top section (3 lines): hostname, uptime, total processes, alert banner (last 3 alerts)
  - Main table: PID | NAME | STATE | CPU% | MEM(MB) | THREADS | FDs
  - Sort by CPU% by default
  - Keybindings: q=quit, s=sort CPU, m=sort MEM, p=sort PID, r=reverse
  - Refresh display on every new snapshot_batch received
  - Color: processes >50% CPU in red, >20% in yellow, rest green
- Use initscr(), cbreak(), noecho(), keypad(), nodelay(), start_color()

## CMakeLists.txt
```cmake
cmake_minimum_required(VERSION 3.15)
project(procwatch)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2 -pthread")

add_executable(procwatchd
    src/main.cpp
    src/proc_parser.cpp
    src/process_store.cpp
    src/broker.cpp
    src/tcp_server.cpp
    src/alert_engine.cpp
    src/scraper.cpp
)
target_include_directories(procwatchd PRIVATE include)
target_link_libraries(procwatchd pthread)

add_subdirectory(client)
```

```cmake
# client/CMakeLists.txt
add_executable(procwatch-client main.cpp tui.cpp)
target_include_directories(procwatch-client PRIVATE ../include)
target_link_libraries(procwatch-client pthread ncurses)
```

## README.md
Include:
- ASCII architecture diagram matching the system design above
- Build instructions: `mkdir build && cd build && cmake .. && make`
- Run instructions: `./procwatchd -p 9876 -i 1000` and `./procwatch-client -a 127.0.0.1:9876`
- A "How it works" section explaining /proc parsing, CPU delta calculation, TCP fan-out

## Constraints
- No global variables — pass all dependencies via constructor injection
- All threads must exit cleanly (no detached threads, use pthread_join on shutdown)
- No use of popen(), system(), or shell commands anywhere
- No C-style casts — use static_cast/reinterpret_cast
- All /proc file reads use open()/read()/close() or fopen()/fclose() directly
- SIGPIPE must be ignored (SIG_IGN) to handle broken client connections gracefully
- Every pthread_mutex_lock must have a corresponding unlock (prefer RAII wrapper or careful pairing)
- Compile cleanly with -Wall -Wextra -O2