#include "log/logger.hpp"
#include <gtest/gtest.h>

int main(int argc, char **argv) {
    // Initialize logging for tests (quiet mode)
    embrace::log::LogConfig config;
    config.level = embrace::log::Level::Error;
    config.console_output = false;
    embrace::log::Logger::instance().init(config);

    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();

    embrace::log::Logger::instance().shutdown();
    return result;
}
