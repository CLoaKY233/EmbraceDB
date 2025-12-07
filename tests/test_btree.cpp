#include <gtest/gtest.h>

#include "indexing/btree.hpp"
#include "core/status.hpp"

#include <filesystem>
#include <string>

using namespace embrace;

class BtreeTest : public ::testing::Test {
  protected:
    std::string test_wal_path = "test_btree.wal";

    void SetUp() override {
        std::filesystem::remove(test_wal_path);
    }

    void TearDown() override {
        std::filesystem::remove(test_wal_path);
    }
};

// ============================================================================
// BASIC CRUD TESTS
// ============================================================================

TEST_F(BtreeTest, PutAndGet) {
    indexing::Btree tree(test_wal_path);

    auto status = tree.put("key1", "value1");
    EXPECT_TRUE(status.ok());

    auto result = tree.get("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "value1");
}

TEST_F(BtreeTest, GetNonExistentKey) {
    indexing::Btree tree(test_wal_path);

    auto result = tree.get("nonexistent");
    EXPECT_FALSE(result.has_value());
}

TEST_F(BtreeTest, UpdateExistingKey) {
    indexing::Btree tree(test_wal_path);

    ASSERT_TRUE(tree.put("key1", "old_value").ok());

    auto status = tree.update("key1", "new_value");
    EXPECT_TRUE(status.ok());

    auto result = tree.get("key1");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "new_value");
}

TEST_F(BtreeTest, UpdateNonExistentKey) {
    indexing::Btree tree(test_wal_path);

    auto status = tree.update("nonexistent", "value");
    EXPECT_TRUE(status.is_not_found());
}

TEST_F(BtreeTest, DeleteExistingKey) {
    indexing::Btree tree(test_wal_path);

    ASSERT_TRUE(tree.put("key1", "value1").ok());

    auto status = tree.remove("key1");
    EXPECT_TRUE(status.ok());

    auto result = tree.get("key1");
    EXPECT_FALSE(result.has_value());
}

TEST_F(BtreeTest, DeleteNonExistentKey) {
    indexing::Btree tree(test_wal_path);

    auto status = tree.remove("nonexistent");
    EXPECT_TRUE(status.is_not_found());
}

// ============================================================================
// B+TREE STRUCTURE TESTS (Splitting, Merging, Balancing)
// ============================================================================

TEST_F(BtreeTest, NodeSplitting) {
    indexing::Btree tree(test_wal_path);

    for (int i = 0; i < 10; i++) {
        auto status = tree.put("key" + std::to_string(i), "value" + std::to_string(i));
        EXPECT_TRUE(status.ok()) << "Failed to insert key" << i;
    }

    for (int i = 0; i < 10; i++) {
        auto result = tree.get("key" + std::to_string(i));
        ASSERT_TRUE(result.has_value()) << "Key" << i << " not found";
        EXPECT_EQ(result.value(), "value" + std::to_string(i));
    }
}

TEST_F(BtreeTest, NodeMergingAfterDeletes) {
    indexing::Btree tree(test_wal_path);

    for (int i = 0; i < 10; i++) {
        ASSERT_TRUE(tree.put("key" + std::to_string(i), "value" + std::to_string(i)).ok());
    }

    ASSERT_TRUE(tree.remove("key2").ok());
    ASSERT_TRUE(tree.remove("key5").ok());
    ASSERT_TRUE(tree.remove("key7").ok());

    EXPECT_TRUE(tree.get("key0").has_value());
    EXPECT_TRUE(tree.get("key1").has_value());
    EXPECT_FALSE(tree.get("key2").has_value());
    EXPECT_TRUE(tree.get("key3").has_value());
}

TEST_F(BtreeTest, BorrowFromSibling) {
    indexing::Btree tree(test_wal_path);

    ASSERT_TRUE(tree.put("a", "1").ok());
    ASSERT_TRUE(tree.put("b", "2").ok());
    ASSERT_TRUE(tree.put("c", "3").ok());
    ASSERT_TRUE(tree.put("d", "4").ok());
    ASSERT_TRUE(tree.put("e", "5").ok());

    ASSERT_TRUE(tree.remove("a").ok());

    EXPECT_FALSE(tree.get("a").has_value());
    EXPECT_TRUE(tree.get("b").has_value());
    EXPECT_TRUE(tree.get("e").has_value());
}

// ============================================================================
// EDGE CASES
// ============================================================================

TEST_F(BtreeTest, EmptyTreeOperations) {
    indexing::Btree tree(test_wal_path);

    EXPECT_FALSE(tree.get("key").has_value());
    EXPECT_TRUE(tree.remove("key").is_not_found());
    EXPECT_TRUE(tree.update("key", "value").is_not_found());
}

TEST_F(BtreeTest, SingleKeyInsertDelete) {
    indexing::Btree tree(test_wal_path);

    ASSERT_TRUE(tree.put("only_key", "only_value").ok());
    EXPECT_TRUE(tree.get("only_key").has_value());

    ASSERT_TRUE(tree.remove("only_key").ok());
    EXPECT_FALSE(tree.get("only_key").has_value());
}

TEST_F(BtreeTest, DuplicateInsert) {
    indexing::Btree tree(test_wal_path);

    ASSERT_TRUE(tree.put("key", "value1").ok());
    ASSERT_TRUE(tree.put("key", "value2").ok());

    auto result = tree.get("key");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "value2");
}

TEST_F(BtreeTest, LargeDataset) {
    indexing::Btree tree(test_wal_path);

    const int COUNT = 1000;

    for (int i = 0; i < COUNT; i++) {
        auto status = tree.put("key" + std::to_string(i), "value" + std::to_string(i));
        EXPECT_TRUE(status.ok());
    }

    for (int i = 0; i < COUNT; i++) {
        auto result = tree.get("key" + std::to_string(i));
        EXPECT_TRUE(result.has_value()) << "Missing key" << i;
    }
}

// ============================================================================
// WAL RECOVERY TESTS
// ============================================================================

TEST_F(BtreeTest, WalRecovery) {
    {
        indexing::Btree tree(test_wal_path);
        ASSERT_TRUE(tree.put("key1", "value1").ok());
        ASSERT_TRUE(tree.put("key2", "value2").ok());
        auto status = tree.flush_wal();
        ASSERT_TRUE(status.ok());
    }

    indexing::Btree recovered_tree(test_wal_path);
    auto status = recovered_tree.recover_from_wal();
    EXPECT_TRUE(status.ok());

    EXPECT_EQ(recovered_tree.get("key1").value(), "value1");
    EXPECT_EQ(recovered_tree.get("key2").value(), "value2");
}

TEST_F(BtreeTest, WalRecoveryWithUpdates) {
    {
        indexing::Btree tree(test_wal_path);
        ASSERT_TRUE(tree.put("key", "v1").ok());
        ASSERT_TRUE(tree.update("key", "v2").ok());
        ASSERT_TRUE(tree.update("key", "v3").ok());
        auto status = tree.flush_wal();
        ASSERT_TRUE(status.ok());
    }

    indexing::Btree recovered_tree(test_wal_path);
    recovered_tree.recover_from_wal();

    EXPECT_EQ(recovered_tree.get("key").value(), "v3");
}

TEST_F(BtreeTest, WalRecoveryWithDeletes) {
    {
        indexing::Btree tree(test_wal_path);
        ASSERT_TRUE(tree.put("key1", "value1").ok());
        ASSERT_TRUE(tree.put("key2", "value2").ok());
        ASSERT_TRUE(tree.remove("key1").ok());
        auto status = tree.flush_wal();
        ASSERT_TRUE(status.ok());
    }

    indexing::Btree recovered_tree(test_wal_path);
    recovered_tree.recover_from_wal();

    EXPECT_FALSE(recovered_tree.get("key1").has_value());
    EXPECT_TRUE(recovered_tree.get("key2").has_value());
}
