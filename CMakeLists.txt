cmake_minimum_required(VERSION 3.15)
project(procwatch)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
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
