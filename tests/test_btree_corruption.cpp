#include <gtest/gtest.h>

#include "indexing/btree.hpp"
#include "core/common.hpp"

#include <filesystem>
#include <fstream>
#include <string>

using namespace embrace;

class BtreeCorruptionTest : public ::testing::Test {
  protected:
    std::string test_wal_path = "test_corrupt.wal";

    void TearDown() override {
        std::filesystem::remove(test_wal_path);
    }
};

TEST_F(BtreeCorruptionTest, MaxKeySizeEnforced) {
    indexing::Btree tree(test_wal_path);

    std::string huge_key(core::MAX_KEY_SIZE * 2, 'x');
    auto status = tree.put(huge_key, "value");

    EXPECT_FALSE(status.ok());
}

TEST_F(BtreeCorruptionTest, MaxValueSizeEnforced) {
    indexing::Btree tree(test_wal_path);

    std::string huge_value(core::MAX_VALUE_SIZE * 2, 'x');
    auto status = tree.put("key", huge_value);

    EXPECT_FALSE(status.ok());
}

TEST_F(BtreeCorruptionTest, CorruptWalRecoveryFailsGracefully) {
    {
        std::ofstream wal(test_wal_path, std::ios::binary);
        wal.write("bad", 3); // incomplete record
    }

    indexing::Btree tree(test_wal_path);
    auto status = tree.recover_from_wal();

    EXPECT_FALSE(status.ok());
}
