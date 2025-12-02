cmake_minimum_required(VERSION 3.28)

project(embrace VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow
    -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
    -Wunused -Woverloaded-virtual -Wpedantic -Wmisleading-indentation
    -Wnull-dereference -Wdouble-promotion -Wformat=2
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(embrace src/main.cpp)

include(FetchContent)
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        10.2.1
)

FetchContent_MakeAvailable(fmt)

target_link_libraries(embrace PRIVATE fmt::fmt)
