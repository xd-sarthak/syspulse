add_executable(procwatch-client
    main.cpp
    tui.cpp
)
target_include_directories(procwatch-client PRIVATE ../include)
target_link_libraries(procwatch-client pthread ncurses)
