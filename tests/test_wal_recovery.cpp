#include "test_utils.hpp"
#include <algorithm>
#include <filesystem>
#include <gtest/gtest.h>
#include <utility>
#include <vector>

namespace embrace::test {

    class WalRecoveryTest : public BtreeTestFixture {};

    // ============================================================================
    // BASIC RECOVERY
    // ============================================================================

    TEST_F(WalRecoveryTest, RecoverySingleOperation) {
        ASSERT_TRUE(tree_->put("foo", "bar").ok());
        ASSERT_TRUE(tree_->flush_wal().ok());
        tree_.reset();

        // Recover
        auto new_tree = std::make_unique<indexing::Btree>(test_wal_path_);
        ASSERT_TRUE(new_tree->recover_from_wal().ok());

        auto result = new_tree->get("foo");
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result.value(), "bar");
    }

    TEST_F(WalRecoveryTest, RecoveryMultipleOperations) {
        for (size_t i = 0; i < 100; ++i) {
            ASSERT_TRUE(tree_->put(generate_key(i), generate_value(i)).ok());
        }
        ASSERT_TRUE(tree_->flush_wal().ok());
        tree_.reset();

        auto new_tree = std::make_unique<indexing::Btree>(test_wal_path_);
        ASSERT_TRUE(new_tree->recover_from_wal().ok());

        for (size_t i = 0; i < 100; ++i) {
            auto result = new_tree->get(generate_key(i));
            ASSERT_TRUE(result.has_value()) << "Key: " << generate_key(i);
            EXPECT_EQ(result.value(), generate_value(i));
        }
    }

    TEST_F(WalRecoveryTest, RecoveryWithDeletions) {
        for (size_t i = 0; i < 50; ++i) {
            ASSERT_TRUE(tree_->put(generate_key(i), generate_value(i)).ok());
        }

        // Delete every other key
        for (size_t i = 0; i < 50; i += 2) {
            ASSERT_TRUE(tree_->remove(generate_key(i)).ok());
        }

        ASSERT_TRUE(tree_->flush_wal().ok());
        tree_.reset();

        auto new_tree = std::make_unique<indexing::Btree>(test_wal_path_);
        ASSERT_TRUE(new_tree->recover_from_wal().ok());

        // Verify deleted keys don't exist, others do
        for (size_t i = 0; i < 50; ++i) {
            auto key = generate_key(i);
            auto result = new_tree->get(key);

            if (i % 2 == 0) {
                EXPECT_FALSE(result.has_value()) << "Key should be deleted: " << key;
            } else {
                ASSERT_TRUE(result.has_value()) << "Key should exist: " << key;
                EXPECT_EQ(result.value(), generate_value(i));
            }
        }
    }

    TEST_F(WalRecoveryTest, RecoveryWithUpdates) {
        ASSERT_TRUE(tree_->put("foo", "original").ok());
        ASSERT_TRUE(tree_->update("foo", "updated").ok());
        ASSERT_TRUE(tree_->flush_wal().ok());
        tree_.reset();

        auto new_tree = std::make_unique<indexing::Btree>(test_wal_path_);
        ASSERT_TRUE(new_tree->recover_from_wal().ok());

        auto result = new_tree->get("foo");
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result.value(), "updated");
    }

    // ============================================================================
    // RANDOM OPERATION SEQUENCES
    // ============================================================================

    TEST_F(WalRecoveryTest, RandomOperationSequence) {
        std::vector<std::pair<std::string, std::string>> expected_state;

        // Random operations
        ASSERT_TRUE(tree_->put("alpha", "1").ok());
        expected_state.push_back({"alpha", "1"});

        ASSERT_TRUE(tree_->put("bravo", "2").ok());
        expected_state.push_back({"bravo", "2"});

        ASSERT_TRUE(tree_->put("charlie", "3").ok());
        expected_state.push_back({"charlie", "3"});

        ASSERT_TRUE(tree_->remove("bravo").ok());
        expected_state.erase(std::remove_if(expected_state.begin(), expected_state.end(),
                                            [](const auto &p) { return p.first == "bravo"; }),
                             expected_state.end());

        ASSERT_TRUE(tree_->update("alpha", "updated_1").ok());
        for (auto &p : expected_state) {
            if (p.first == "alpha")
                p.second = "updated_1";
        }

        ASSERT_TRUE(tree_->flush_wal().ok());
        tree_.reset();

        // Recover and verify
        auto new_tree = std::make_unique<indexing::Btree>(test_wal_path_);
        ASSERT_TRUE(new_tree->recover_from_wal().ok());

        for (const auto &[key, value] : expected_state) {
            auto result = new_tree->get(key);
            ASSERT_TRUE(result.has_value()) << "Key: " << key;
            EXPECT_EQ(result.value(), value);
        }
    }

    // ============================================================================
    // SNAPSHOT + WAL RECOVERY
    // ============================================================================

    TEST_F(WalRecoveryTest, RecoveryFromSnapshotOnly) {
        for (size_t i = 0; i < 50; ++i) {
            ASSERT_TRUE(tree_->put(generate_key(i), generate_value(i)).ok());
        }

        // Create snapshot
        ASSERT_TRUE(tree_->create_checkpoint().ok());
        tree_.reset();

        // Recover from snapshot
        auto new_tree = std::make_unique<indexing::Btree>(test_wal_path_);
        ASSERT_TRUE(new_tree->recover_from_wal().ok());

        for (size_t i = 0; i < 50; ++i) {
            EXPECT_TRUE(new_tree->get(generate_key(i)).has_value());
        }
    }

    TEST_F(WalRecoveryTest, RecoveryFromSnapshotPlusWAL) {
        // Initial data with snapshot
        for (size_t i = 0; i < 50; ++i) {
            ASSERT_TRUE(tree_->put(generate_key(i), generate_value(i)).ok());
        }
        ASSERT_TRUE(tree_->create_checkpoint().ok());

        // Additional operations after snapshot
        for (size_t i = 50; i < 100; ++i) {
            ASSERT_TRUE(tree_->put(generate_key(i), generate_value(i)).ok());
        }
        ASSERT_TRUE(tree_->flush_wal().ok());
        tree_.reset();

        // Recover
        auto new_tree = std::make_unique<indexing::Btree>(test_wal_path_);
        ASSERT_TRUE(new_tree->recover_from_wal().ok());

        // Verify all 100 keys
        for (size_t i = 0; i < 100; ++i) {
            EXPECT_TRUE(new_tree->get(generate_key(i)).has_value()) << "Key: " << generate_key(i);
        }
    }

    TEST_F(WalRecoveryTest, RecoveryWhenWALMissing) {
        auto new_tree = std::make_unique<indexing::Btree>(test_wal_path_);
        auto status = new_tree->recover_from_wal();
        EXPECT_TRUE(status.ok()); // Should succeed gracefully
    }

    TEST_F(WalRecoveryTest, RecoveryWhenSnapshotMissing) {
        ASSERT_TRUE(tree_->put("foo", "bar").ok());
        ASSERT_TRUE(tree_->flush_wal().ok());
        tree_.reset();

        // Delete snapshot but keep WAL
        std::filesystem::remove(test_snapshot_path_);

        auto new_tree = std::make_unique<indexing::Btree>(test_wal_path_);
        ASSERT_TRUE(new_tree->recover_from_wal().ok());

        EXPECT_TRUE(new_tree->get("foo").has_value());
    }

    // ============================================================================
    // CRASH SIMULATION
    // ============================================================================

    TEST_F(WalRecoveryTest, CrashDuringWriteRecovery) {
        // Simulate incomplete write by creating partial WAL
        for (size_t i = 0; i < 100; ++i) {
            ASSERT_TRUE(tree_->put(generate_key(i), generate_value(i)).ok());
        }
        // Don't flush - simulates crash mid-buffer
        tree_.reset();

        // Recovery should handle gracefully
        auto new_tree = std::make_unique<indexing::Btree>(test_wal_path_);
        auto status = new_tree->recover_from_wal();
        // May succeed with partial data or report corruption; ensure no crash occurs
        (void)status;
    }

    TEST_F(WalRecoveryTest, MultipleRecoveryCycles) {
        // First cycle
        ASSERT_TRUE(tree_->put("foo", "bar").ok());
        ASSERT_TRUE(tree_->flush_wal().ok());
        tree_.reset();

        // Recover and add more
        tree_ = std::make_unique<indexing::Btree>(test_wal_path_);
        ASSERT_TRUE(tree_->recover_from_wal().ok());
        ASSERT_TRUE(tree_->put("baz", "qux").ok());
        ASSERT_TRUE(tree_->flush_wal().ok());
        tree_.reset();

        // Final recovery
        tree_ = std::make_unique<indexing::Btree>(test_wal_path_);
        ASSERT_TRUE(tree_->recover_from_wal().ok());

        EXPECT_TRUE(tree_->get("foo").has_value());
        EXPECT_TRUE(tree_->get("baz").has_value());
    }

} // namespace embrace::test
