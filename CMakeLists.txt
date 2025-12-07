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

include(FetchContent)

# fmt
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        10.2.1
)
FetchContent_MakeAvailable(fmt)

# GoogleTest
enable_testing()
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(EMBRACE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(embrace_core INTERFACE)
target_include_directories(embrace_core INTERFACE ${EMBRACE_INCLUDE_DIR})
target_link_libraries(embrace_core INTERFACE fmt::fmt)

add_library(embrace_log src/log/logger.cpp)
target_include_directories(embrace_log PUBLIC ${EMBRACE_INCLUDE_DIR})
target_link_libraries(embrace_log PUBLIC embrace_core fmt::fmt)

add_library(embrace_storage src/storage/wal.cpp)
target_include_directories(embrace_storage PUBLIC ${EMBRACE_INCLUDE_DIR})
target_link_libraries(embrace_storage PUBLIC embrace_core embrace_log fmt::fmt)

add_library(embrace_indexing src/indexing/btree.cpp)
target_include_directories(embrace_indexing PUBLIC ${EMBRACE_INCLUDE_DIR})
target_link_libraries(embrace_indexing PUBLIC embrace_core embrace_storage embrace_log fmt::fmt)

add_executable(embrace src/main.cpp)
target_include_directories(embrace PRIVATE ${EMBRACE_INCLUDE_DIR})
target_link_libraries(embrace PRIVATE embrace_indexing)

add_subdirectory(tests)
