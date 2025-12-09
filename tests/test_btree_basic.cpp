#include "core/status.hpp"
#include "indexing/btree.hpp"
#include "test_utils.hpp"
#include <gtest/gtest.h>

namespace embrace::test {

    class BtreeBasicTest : public BtreeTestFixture {};

    // ============================================================================
    // INSERTION TESTS
    // ============================================================================

    TEST_F(BtreeBasicTest, InsertSingleKeyValue) {
        auto status = tree_->put("foo", "bar");
        ASSERT_TRUE(status.ok());

        auto result = tree_->get("foo");
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result.value(), "bar");
    }

    TEST_F(BtreeBasicTest, InsertMultipleSequentialKeys) {
        const size_t count = 100;
        for (size_t i = 0; i < count; ++i) {
            auto key = generate_key(i);
            auto value = generate_value(i);
            ASSERT_TRUE(tree_->put(key, value).ok());
        }

        // Verify all keys exist
        for (size_t i = 0; i < count; ++i) {
            auto key = generate_key(i);
            auto result = tree_->get(key);
            ASSERT_TRUE(result.has_value()) << "Key not found: " << key;
            EXPECT_EQ(result.value(), generate_value(i));
        }
    }

    TEST_F(BtreeBasicTest, InsertDuplicateKeyUpdatesValue) {
        ASSERT_TRUE(tree_->put("foo", "bar").ok());
        ASSERT_TRUE(tree_->put("foo", "baz").ok());

        auto result = tree_->get("foo");
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result.value(), "baz");
    }

    TEST_F(BtreeBasicTest, InsertEmptyKey) {
        auto status = tree_->put("", "empty_key_value");
        ASSERT_TRUE(status.ok());

        auto result = tree_->get("");
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result.value(), "empty_key_value");
    }

    TEST_F(BtreeBasicTest, InsertEmptyValue) {
        auto status = tree_->put("foo", "");
        ASSERT_TRUE(status.ok());

        auto result = tree_->get("foo");
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result.value(), "");
    }

    TEST_F(BtreeBasicTest, InsertLargeValue) {
        std::string large_value(1000, 'x');
        auto status = tree_->put("foo", large_value);
        ASSERT_TRUE(status.ok());

        auto result = tree_->get("foo");
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result.value(), large_value);
    }

    // ============================================================================
    // RETRIEVAL TESTS
    // ============================================================================

    TEST_F(BtreeBasicTest, GetNonExistentKeyReturnsNullopt) {
        auto result = tree_->get("nonexistent");
        EXPECT_FALSE(result.has_value());
    }

    TEST_F(BtreeBasicTest, GetFromEmptyTree) {
        auto result = tree_->get("foo");
        EXPECT_FALSE(result.has_value());
    }

    TEST_F(BtreeBasicTest, GetAfterMultipleInserts) {
        tree_->put("foo", "bar");
        tree_->put("baz", "qux");
        tree_->put("quux", "corge");

        EXPECT_EQ(tree_->get("foo").value(), "bar");
        EXPECT_EQ(tree_->get("baz").value(), "qux");
        EXPECT_EQ(tree_->get("quux").value(), "corge");
    }

    // ============================================================================
    // UPDATE TESTS
    // ============================================================================

    TEST_F(BtreeBasicTest, UpdateExistingKey) {
        tree_->put("foo", "bar");

        auto status = tree_->update("foo", "baz");
        ASSERT_TRUE(status.ok());

        auto result = tree_->get("foo");
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result.value(), "baz");
    }

    TEST_F(BtreeBasicTest, UpdateNonExistentKeyFails) {
        auto status = tree_->update("nonexistent", "value");
        EXPECT_FALSE(status.ok());
        EXPECT_TRUE(status.is_not_found());
    }

    TEST_F(BtreeBasicTest, UpdateMultipleKeys) {
        tree_->put("foo", "bar");
        tree_->put("baz", "qux");

        ASSERT_TRUE(tree_->update("foo", "updated_foo").ok());
        ASSERT_TRUE(tree_->update("baz", "updated_baz").ok());

        EXPECT_EQ(tree_->get("foo").value(), "updated_foo");
        EXPECT_EQ(tree_->get("baz").value(), "updated_baz");
    }

    // ============================================================================
    // DELETION TESTS
    // ============================================================================

    TEST_F(BtreeBasicTest, DeleteExistingKey) {
        tree_->put("foo", "bar");

        auto status = tree_->remove("foo");
        ASSERT_TRUE(status.ok());

        auto result = tree_->get("foo");
        EXPECT_FALSE(result.has_value());
    }

    TEST_F(BtreeBasicTest, DeleteNonExistentKeyFails) {
        auto status = tree_->remove("nonexistent");
        EXPECT_FALSE(status.ok());
        EXPECT_TRUE(status.is_not_found());
    }

    TEST_F(BtreeBasicTest, DeleteFromEmptyTree) {
        auto status = tree_->remove("foo");
        EXPECT_FALSE(status.ok());
        EXPECT_TRUE(status.is_not_found());
    }

    TEST_F(BtreeBasicTest, DeleteAndReinsert) {
        tree_->put("foo", "bar");
        ASSERT_TRUE(tree_->remove("foo").ok());
        ASSERT_TRUE(tree_->put("foo", "baz").ok());

        auto result = tree_->get("foo");
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result.value(), "baz");
    }

    TEST_F(BtreeBasicTest, DeleteMultipleKeys) {
        tree_->put("foo", "bar");
        tree_->put("baz", "qux");
        tree_->put("quux", "corge");

        ASSERT_TRUE(tree_->remove("baz").ok());
        ASSERT_TRUE(tree_->remove("quux").ok());

        EXPECT_TRUE(tree_->get("foo").has_value());
        EXPECT_FALSE(tree_->get("baz").has_value());
        EXPECT_FALSE(tree_->get("quux").has_value());
    }

} // namespace embrace::test
