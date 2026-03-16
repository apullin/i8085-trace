cmake_minimum_required(VERSION 3.13)

project(i8085-trace C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(i8085-trace
    src/main.cpp
    src/io_runtime.cpp
    src/disk_emu.cpp
    src/gdb_stub.cpp
    src/i8085_exec.c
    src/i8085_state.c
    src/i8085_disasm.c
)

target_include_directories(i8085-trace PRIVATE include)

if(UNIX AND NOT APPLE)
    target_link_libraries(i8085-trace PRIVATE dl)
endif()
