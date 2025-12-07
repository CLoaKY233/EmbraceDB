#include <gtest/gtest.h>

#include "core/status.hpp"

using namespace embrace;

TEST(StatusTest, DefaultIsOk) {
    core::Status status;
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(status.to_string(), "OK");
}

TEST(StatusTest, NotFoundHelper) {
    auto status = core::Status::NotFound("missing key");
    EXPECT_TRUE(status.is_not_found());
    EXPECT_EQ(status.to_string(), "NotFound: missing key");
}

TEST(StatusTest, InvalidArgumentToString) {
    auto status = core::Status::InvalidArgument("bad argument");
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.to_string(), "InvalidArgument: bad argument");
}
