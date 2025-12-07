#include <gtest/gtest.h>

#include "storage/wal.hpp"

#include <filesystem>
#include <string>

using namespace embrace;

class WalTest : public ::testing::Test {
  protected:
    std::string test_path = "test.wal";

    void SetUp() override {
        std::filesystem::remove(test_path);
    }

    void TearDown() override {
        std::filesystem::remove(test_path);
    }
};

TEST_F(WalTest, WriteAndReadRecord) {
    storage::WalWriter writer(test_path);
    ASSERT_TRUE(writer.is_open());

    auto status = writer.write_put("key1", "value1");
    EXPECT_TRUE(status.ok());
    EXPECT_TRUE(writer.sync().ok());

    storage::WalReader reader(test_path);
    ASSERT_TRUE(reader.is_open());

    storage::WalRecord record;
    status = reader.read_next(record);
    EXPECT_TRUE(status.ok());
    EXPECT_EQ(record.type, storage::WalRecordType::Put);
    EXPECT_EQ(record.key, "key1");
    EXPECT_EQ(record.value, "value1");
}
