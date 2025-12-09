#include "test_utils.hpp"
#include <gtest/gtest.h>
#include <vector>

namespace embrace::test {

    class BtreeEdgeCasesTest : public BtreeTestFixture {};

    // ============================================================================
    // BOUNDARY CONDITIONS
    // ============================================================================

    TEST_F(BtreeEdgeCasesTest, SingleKeyOperations) {
        ASSERT_TRUE(tree_->put("singleton", "value").ok());
        EXPECT_TRUE(tree_->get("singleton").has_value());

        ASSERT_TRUE(tree_->remove("singleton").ok());
        EXPECT_FALSE(tree_->get("singleton").has_value());
    }

    TEST_F(BtreeEdgeCasesTest, MaxKeySize) {
        std::string max_key(core::MAX_KEY_SIZE, 'k');
        ASSERT_TRUE(tree_->put(max_key, "value").ok());

        auto result = tree_->get(max_key);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result.value(), "value");
    }

    TEST_F(BtreeEdgeCasesTest, MaxValueSize) {
        std::string max_value(core::MAX_VALUE_SIZE, 'v');
        ASSERT_TRUE(tree_->put("key", max_value).ok());

        auto result = tree_->get("key");
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result.value(), max_value);
    }

    TEST_F(BtreeEdgeCasesTest, MinMaxKeySorting) {
        ASSERT_TRUE(tree_->put("zzz", "last").ok());
        ASSERT_TRUE(tree_->put("aaa", "first").ok());
        ASSERT_TRUE(tree_->put("mmm", "middle").ok());

        EXPECT_EQ(tree_->get("aaa").value(), "first");
        EXPECT_EQ(tree_->get("mmm").value(), "middle");
        EXPECT_EQ(tree_->get("zzz").value(), "last");
    }

    // ============================================================================
    // SPECIAL CHARACTERS
    // ============================================================================

    TEST_F(BtreeEdgeCasesTest, SpecialCharactersInKeys) {
        ASSERT_TRUE(tree_->put("key!@#$%", "special").ok());
        ASSERT_TRUE(tree_->put("key\t\n\r", "whitespace").ok());
        ASSERT_TRUE(tree_->put("key 中文 ελληνικά", "unicode").ok());

        EXPECT_TRUE(tree_->get("key!@#$%").has_value());
        EXPECT_TRUE(tree_->get("key\t\n\r").has_value());
        EXPECT_TRUE(tree_->get("key 中文 ελληνικά").has_value());
    }

    TEST_F(BtreeEdgeCasesTest, NullByteInValue) {
        std::string value_with_null;
        value_with_null.push_back('a');
        value_with_null.push_back('\0');
        value_with_null.push_back('b');

        ASSERT_TRUE(tree_->put("null_byte_key", value_with_null).ok());
        auto result = tree_->get("null_byte_key");

        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result.value().size(), 3);
    }

    // ============================================================================
    // STRESS SCENARIOS
    // ============================================================================

    TEST_F(BtreeEdgeCasesTest, AlternatingInsertDelete) {
        for (int i = 0; i < 100; ++i) {
            auto key = fmt::format("alt_{:03d}", i);
            ASSERT_TRUE(tree_->put(key, "value").ok());

            if (i % 2 == 0) {
                ASSERT_TRUE(tree_->remove(key).ok());
            }
        }

        // Verify odd keys exist, even keys don't
        for (int i = 0; i < 100; ++i) {
            auto key = fmt::format("alt_{:03d}", i);
            EXPECT_EQ(tree_->get(key).has_value(), (i % 2 == 1)) << "Key: " << key;
        }
    }

    TEST_F(BtreeEdgeCasesTest, ReverseOrderInsertion) {
        for (int i = 99; i >= 0; --i) {
            ASSERT_TRUE(tree_->put(fmt::format("rev_{:02d}", i), "value").ok());
        }

        // Verify all keys
        for (int i = 0; i < 100; ++i) {
            EXPECT_TRUE(tree_->get(fmt::format("rev_{:02d}", i)).has_value());
        }
    }

    TEST_F(BtreeEdgeCasesTest, IdenticalPrefixKeys) {
        ASSERT_TRUE(tree_->put("prefix", "1").ok());
        ASSERT_TRUE(tree_->put("prefix_a", "2").ok());
        ASSERT_TRUE(tree_->put("prefix_ab", "3").ok());
        ASSERT_TRUE(tree_->put("prefix_abc", "4").ok());

        EXPECT_EQ(tree_->get("prefix").value(), "1");
        EXPECT_EQ(tree_->get("prefix_a").value(), "2");
        EXPECT_EQ(tree_->get("prefix_ab").value(), "3");
        EXPECT_EQ(tree_->get("prefix_abc").value(), "4");
    }

    TEST_F(BtreeEdgeCasesTest, DuplicateInsertionBehavior) {
        ASSERT_TRUE(tree_->put("dup", "first").ok());
        ASSERT_TRUE(tree_->put("dup", "second").ok());
        ASSERT_TRUE(tree_->put("dup", "third").ok());

        EXPECT_EQ(tree_->get("dup").value(), "third");
    }

    // ============================================================================
    // ITERATION EDGE CASES
    // ============================================================================

    TEST_F(BtreeEdgeCasesTest, IterateEmptyTree) {
        size_t count = 0;
        tree_->iterate_all([&count](const core::Key &, const core::Value &) { count++; });
        EXPECT_EQ(count, 0);
    }

    TEST_F(BtreeEdgeCasesTest, IterateSingleElement) {
        ASSERT_TRUE(tree_->put("singleton", "value").ok());

        size_t count = 0;
        std::string found_key, found_value;

        tree_->iterate_all([&](const core::Key &k, const core::Value &v) {
            found_key = k;
            found_value = v;
            count++;
        });

        EXPECT_EQ(count, 1);
        EXPECT_EQ(found_key, "singleton");
        EXPECT_EQ(found_value, "value");
    }

    TEST_F(BtreeEdgeCasesTest, IterationOrderIsSorted) {
        std::vector<std::string> keys = {"delta", "alpha", "charlie", "bravo"};
        for (const auto &key : keys) {
            ASSERT_TRUE(tree_->put(key, "value").ok());
        }

        std::vector<std::string> iterated_keys;
        tree_->iterate_all(
            [&](const core::Key &k, const core::Value &) { iterated_keys.push_back(k); });

        std::vector<std::string> expected = {"alpha", "bravo", "charlie", "delta"};
        EXPECT_EQ(iterated_keys, expected);
    }

} // namespace embrace::test
