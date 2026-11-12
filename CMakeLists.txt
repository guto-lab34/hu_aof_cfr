cmake_minimum_required(VERSION 3.18)
project(hu_aof_cfr LANGUAGES CXX)

find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 CONFIG REQUIRED)

set(CPP hu_aof_cfr/cpp)
pybind11_add_module(_core
    ${CPP}/bindings.cpp
    ${CPP}/cfr.cpp
    ${CPP}/game.cpp
    ${CPP}/equity_table.cpp
)
target_compile_features(_core PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(_core PRIVATE /W4)
else()
    target_compile_options(_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
install(TARGETS _core LIBRARY DESTINATION hu_aof_cfr RUNTIME DESTINATION hu_aof_cfr)
