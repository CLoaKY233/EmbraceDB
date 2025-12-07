include(GoogleTest)

file(GLOB TEST_SOURCES "test_*.cpp")

add_executable(embrace_tests ${TEST_SOURCES})

target_link_libraries(embrace_tests
    PRIVATE
        embrace_indexing
        embrace_storage
        embrace_core
        GTest::gtest_main
)

gtest_discover_tests(embrace_tests WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
