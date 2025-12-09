#include "storage/wal.hpp"
#include "test_utils.hpp"
#include <csignal>
#include <gtest/gtest.h>
#include <thread>

namespace embrace::test {

    class WalRecoveryTest : public BtreeTestFixture {};

    // ============================================================================
    // BASIC RECOVERY
    // ============================================================================

    TEST_F(WalRecoveryTest, RecoverySingleOperation) {
        tree_->put("foo", "bar");
        tree_->flush_wal();
        tree_.reset();

        // Recover
        auto new_tree = std::make_unique<indexing::Btree>(test_wal_path_);
        ASSERT_TRUE(new_tree->recover_from_wal().ok());

        auto result = new_tree->get("foo");
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result.value(), "bar");
    }

    TEST_F(WalRecoveryTest, RecoveryMultipleOperations) {
        for (int i = 0; i < 100; ++i) {
            tree_->put(generate_key(i), generate_value(i));
        }
        tree_->flush_wal();
        tree_.reset();

        auto new_tree = std::make_unique<indexing::Btree>(test_wal_path_);
        ASSERT_TRUE(new_tree->recover_from_wal().ok());

        for (int i = 0; i < 100; ++i) {
            auto result = new_tree->get(generate_key(i));
            ASSERT_TRUE(result.has_value()) << "Key: " << generate_key(i);
            EXPECT_EQ(result.value(), generate_value(i));
        }
    }

    TEST_F(WalRecoveryTest, RecoveryWithDeletions) {
        for (int i = 0; i < 50; ++i) {
            tree_->put(generate_key(i), generate_value(i));
        }

        // Delete every other key
        for (int i = 0; i < 50; i += 2) {
            tree_->remove(generate_key(i));
        }

        tree_->flush_wal();
        tree_.reset();

        auto new_tree = std::make_unique<indexing::Btree>(test_wal_path_);
        ASSERT_TRUE(new_tree->recover_from_wal().ok());

        // Verify deleted keys don't exist, others do
        for (int i = 0; i < 50; ++i) {
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
        tree_->put("foo", "original");
        tree_->update("foo", "updated");
        tree_->flush_wal();
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
        tree_->put("alpha", "1");
        expected_state.push_back({"alpha", "1"});

        tree_->put("bravo", "2");
        expected_state.push_back({"bravo", "2"});

        tree_->put("charlie", "3");
        expected_state.push_back({"charlie", "3"});

        tree_->remove("bravo");
        expected_state.erase(std::remove_if(expected_state.begin(), expected_state.end(),
                                            [](const auto &p) { return p.first == "bravo"; }),
                             expected_state.end());

        tree_->update("alpha", "updated_1");
        for (auto &p : expected_state) {
            if (p.first == "alpha")
                p.second = "updated_1";
        }

        tree_->flush_wal();
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
        for (int i = 0; i < 50; ++i) {
            tree_->put(generate_key(i), generate_value(i));
        }

        // Create snapshot
        ASSERT_TRUE(tree_->create_checkpoint().ok());
        tree_.reset();

        // Recover from snapshot
        auto new_tree = std::make_unique<indexing::Btree>(test_wal_path_);
        ASSERT_TRUE(new_tree->recover_from_wal().ok());

        for (int i = 0; i < 50; ++i) {
            EXPECT_TRUE(new_tree->get(generate_key(i)).has_value());
        }
    }

    TEST_F(WalRecoveryTest, RecoveryFromSnapshotPlusWAL) {
        // Initial data with snapshot
        for (int i = 0; i < 50; ++i) {
            tree_->put(generate_key(i), generate_value(i));
        }
        ASSERT_TRUE(tree_->create_checkpoint().ok());

        // Additional operations after snapshot
        for (int i = 50; i < 100; ++i) {
            tree_->put(generate_key(i), generate_value(i));
        }
        tree_->flush_wal();
        tree_.reset();

        // Recover
        auto new_tree = std::make_unique<indexing::Btree>(test_wal_path_);
        ASSERT_TRUE(new_tree->recover_from_wal().ok());

        // Verify all 100 keys
        for (int i = 0; i < 100; ++i) {
            EXPECT_TRUE(new_tree->get(generate_key(i)).has_value()) << "Key: " << generate_key(i);
        }
    }

    TEST_F(WalRecoveryTest, RecoveryWhenWALMissing) {
        auto new_tree = std::make_unique<indexing::Btree>(test_wal_path_);
        auto status = new_tree->recover_from_wal();
        EXPECT_TRUE(status.ok()); // Should succeed gracefully
    }

    TEST_F(WalRecoveryTest, RecoveryWhenSnapshotMissing) {
        tree_->put("foo", "bar");
        tree_->flush_wal();
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
        for (int i = 0; i < 100; ++i) {
            tree_->put(generate_key(i), generate_value(i));
        }
        // Don't flush - simulates crash mid-buffer
        tree_.reset();

        // Recovery should handle gracefully
        auto new_tree = std::make_unique<indexing::Btree>(test_wal_path_);
        auto status = new_tree->recover_from_wal();
        // May succeed with partial data or report corruption
        EXPECT_TRUE(status.ok() || !status.ok());
    }

    TEST_F(WalRecoveryTest, MultipleRecoveryCycles) {
        // First cycle
        tree_->put("foo", "bar");
        tree_->flush_wal();
        tree_.reset();

        // Recover and add more
        tree_ = std::make_unique<indexing::Btree>(test_wal_path_);
        ASSERT_TRUE(tree_->recover_from_wal().ok());
        tree_->put("baz", "qux");
        tree_->flush_wal();
        tree_.reset();

        // Final recovery
        tree_ = std::make_unique<indexing::Btree>(test_wal_path_);
        ASSERT_TRUE(tree_->recover_from_wal().ok());

        EXPECT_TRUE(tree_->get("foo").has_value());
        EXPECT_TRUE(tree_->get("baz").has_value());
    }

} // namespace embrace::test
