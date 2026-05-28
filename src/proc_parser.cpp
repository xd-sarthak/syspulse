#include "proc_parser.hpp"

bool parse_proc_stat(int, ProcessSnapshot&)
{
    return false;
}

bool parse_proc_status(int, ProcessSnapshot&)
{
    return false;
}

int count_fds(int)
{
    return 0;
}

std::vector<int> list_pids()
{
    return {};
}
