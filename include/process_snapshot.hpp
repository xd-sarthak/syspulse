#pragma once

#include <ctime>

struct ProcessSnapshot {
    int pid;
    char name[256];
    char state;
    double cpu_percent;
    long mem_rss_kb;
    int threads;
    int fd_count;
    long utime_prev;
    long stime_prev;
    long wall_prev;
    time_t timestamp;
};

struct Alert {
    int pid;
    char name[256];
    char rule[64];
    double value;
    time_t timestamp;
};

struct Message {
    char type[16];
};
