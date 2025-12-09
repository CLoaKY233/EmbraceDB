#include "test_utils.hpp"
#include <gtest/gtest.h>

namespace embrace::test {

    class BtreeStructureTest : public BtreeTestFixture {};

    // ============================================================================
    // SPLITTING TESTS
    // ============================================================================

    TEST_F(BtreeStructureTest, LeafNodeSplitOnOverflow) {
        // Insert enough keys to trigger leaf split (max_degree = 4)
        for (int i = 0; i < 10; ++i) {
            auto key = fmt::format("key_{:02d}", i);
            ASSERT_TRUE(tree_->put(key, "value").ok());
        }

        // Verify all keys are still retrievable
        for (int i = 0; i < 10; ++i) {
            auto key = fmt::format("key_{:02d}", i);
            EXPECT_TRUE(tree_->get(key).has_value()) << "Key: " << key;
        }
    }

    TEST_F(BtreeStructureTest, InternalNodeSplitOnOverflow) {
        // Insert many keys to trigger multiple levels
        for (int i = 0; i < 50; ++i) {
            auto key = fmt::format("key_{:03d}", i);
            ASSERT_TRUE(tree_->put(key, "value").ok());
        }

        // Verify all keys exist
        for (int i = 0; i < 50; ++i) {
            auto key = fmt::format("key_{:03d}", i);
            EXPECT_TRUE(tree_->get(key).has_value());
        }
    }

    TEST_F(BtreeStructureTest, RootSplitCreatesNewRoot) {
        // Force root split by inserting beyond initial capacity
        for (int i = 0; i < 8; ++i) {
            auto key = fmt::format("root_{:02d}", i);
            ASSERT_TRUE(tree_->put(key, "value").ok());
        }

        // All keys should remain accessible
        for (int i = 0; i < 8; ++i) {
            auto key = fmt::format("root_{:02d}", i);
            EXPECT_TRUE(tree_->get(key).has_value());
        }
    }

    // ============================================================================
    // REBALANCING TESTS
    // ============================================================================

    TEST_F(BtreeStructureTest, BorrowFromLeftSibling) {
        // Create a scenario where borrowing is necessary
        for (int i = 0; i < 12; ++i) {
            tree_->put(fmt::format("key_{:02d}", i), "value");
        }

        // Delete to trigger underflow
        ASSERT_TRUE(tree_->remove("key_06").ok());
        ASSERT_TRUE(tree_->remove("key_07").ok());

        // Verify remaining keys
        for (int i = 0; i < 12; ++i) {
            if (i == 6 || i == 7)
                continue;
            auto key = fmt::format("key_{:02d}", i);
            EXPECT_TRUE(tree_->get(key).has_value()) << "Key: " << key;
        }
    }

    TEST_F(BtreeStructureTest, BorrowFromRightSibling) {
        for (int i = 0; i < 12; ++i) {
            tree_->put(fmt::format("key_{:02d}", i), "value");
        }

        // Delete from left side to trigger borrow from right
        ASSERT_TRUE(tree_->remove("key_01").ok());
        ASSERT_TRUE(tree_->remove("key_02").ok());

        // Verify structure integrity
        for (int i = 0; i < 12; ++i) {
            if (i == 1 || i == 2)
                continue;
            auto key = fmt::format("key_{:02d}", i);
            EXPECT_TRUE(tree_->get(key).has_value());
        }
    }

    TEST_F(BtreeStructureTest, MergeWithLeftSibling) {
        // Setup for merge scenario
        for (int i = 0; i < 15; ++i) {
            tree_->put(fmt::format("merge_{:02d}", i), "value");
        }

        // Delete enough to trigger merge
        for (int i = 5; i < 10; ++i) {
            ASSERT_TRUE(tree_->remove(fmt::format("merge_{:02d}", i)).ok());
        }

        // Verify remaining keys
        for (int i = 0; i < 15; ++i) {
            auto key = fmt::format("merge_{:02d}", i);
            bool should_exist = (i < 5 || i >= 10);
            EXPECT_EQ(tree_->get(key).has_value(), should_exist) << "Key: " << key;
        }
    }

    TEST_F(BtreeStructureTest, MergeWithRightSibling) {
        for (int i = 0; i < 15; ++i) {
            tree_->put(fmt::format("merge_{:02d}", i), "value");
        }

        // Delete from different positions to trigger right merge
        for (int i = 0; i < 5; ++i) {
            ASSERT_TRUE(tree_->remove(fmt::format("merge_{:02d}", i)).ok());
        }

        for (int i = 5; i < 15; ++i) {
            auto key = fmt::format("merge_{:02d}", i);
            EXPECT_TRUE(tree_->get(key).has_value()) << "Key: " << key;
        }
    }

    TEST_F(BtreeStructureTest, InternalNodeUnderflow) {
        // Create deep tree
        for (int i = 0; i < 100; ++i) {
            tree_->put(fmt::format("deep_{:03d}", i), "value");
        }

        // Delete many keys to trigger internal node underflow
        for (int i = 20; i < 80; ++i) {
            tree_->remove(fmt::format("deep_{:03d}", i));
        }

        // Verify remaining keys
        for (int i = 0; i < 20; ++i) {
            EXPECT_TRUE(tree_->get(fmt::format("deep_{:03d}", i)).has_value());
        }
        for (int i = 80; i < 100; ++i) {
            EXPECT_TRUE(tree_->get(fmt::format("deep_{:03d}", i)).has_value());
        }
    }

    // ============================================================================
    // ROOT HANDLING TESTS
    // ============================================================================

    TEST_F(BtreeStructureTest, RootRemainsSingleLeafWhenSmall) {
        tree_->put("foo", "bar");
        tree_->put("baz", "qux");

        ASSERT_TRUE(tree_->remove("foo").ok());

        EXPECT_TRUE(tree_->get("baz").has_value());
    }

    TEST_F(BtreeStructureTest, RootCollapseAfterDeletion) {
        // Build tree with multiple levels
        for (int i = 0; i < 20; ++i) {
            tree_->put(fmt::format("collapse_{:02d}", i), "value");
        }

        // Delete most keys to collapse root
        for (int i = 0; i < 18; ++i) {
            tree_->remove(fmt::format("collapse_{:02d}", i));
        }

        // Verify remaining keys
        EXPECT_TRUE(tree_->get("collapse_18").has_value());
        EXPECT_TRUE(tree_->get("collapse_19").has_value());
    }

    TEST_F(BtreeStructureTest, EmptyTreeAfterDeletingAllKeys) {
        tree_->put("foo", "bar");
        tree_->put("baz", "qux");

        ASSERT_TRUE(tree_->remove("foo").ok());
        ASSERT_TRUE(tree_->remove("baz").ok());

        EXPECT_FALSE(tree_->get("foo").has_value());
        EXPECT_FALSE(tree_->get("baz").has_value());
    }

} // namespace embrace::test
