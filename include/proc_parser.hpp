#pragma once

#include "process_snapshot.hpp"

#include <vector>

bool parse_proc_stat(int pid, ProcessSnapshot& snap);
bool parse_proc_status(int pid, ProcessSnapshot& snap);
int count_fds(int pid);
std::vector<int> list_pids();
