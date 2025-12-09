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

# Fetch external dependencies
include(FetchContent)

FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG        10.2.1
)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)

FetchContent_MakeAvailable(fmt googletest)

# Library target for reusable components
# Note: GLOB_RECURSE will not trigger CMake reconfigure when files are added/removed.
# Consider maintaining an explicit list or use CONFIGURE_DEPENDS if that trade-off is acceptable.
file(GLOB_RECURSE LIB_SOURCES
    "src/core/*.cpp"
    "src/indexing/*.cpp"
    "src/storage/*.cpp"
    "src/log/*.cpp"
)

add_library(embrace_lib STATIC ${LIB_SOURCES})
target_include_directories(embrace_lib PUBLIC src)
target_link_libraries(embrace_lib PUBLIC fmt::fmt)

# Main executable
add_executable(embrace src/main.cpp)
target_link_libraries(embrace PRIVATE embrace_lib)

# Enable testing
enable_testing()

# Test executable
file(GLOB_RECURSE TEST_SOURCES "tests/*.cpp")
add_executable(embrace_tests ${TEST_SOURCES})
target_link_libraries(embrace_tests PRIVATE
    embrace_lib
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
)
target_include_directories(embrace_tests PRIVATE src tests)

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(embrace_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Add a custom test target that runs embrace_tests directly
add_custom_target(run_tests
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/embrace_tests --gtest_color=yes
    DEPENDS embrace_tests
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Running all tests..."
)
