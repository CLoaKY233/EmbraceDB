#include <gtest/gtest.h>

#include "indexing/btree.hpp"

#include <filesystem>
#include <map>
#include <random>
#include <string>

using namespace embrace;

class BtreeStressTest : public ::testing::Test {
  protected:
    std::string test_wal_path = "test_stress.wal";

    void TearDown() override {
        std::filesystem::remove(test_wal_path);
    }
};

TEST_F(BtreeStressTest, ReverseOrderInsertion) {
    indexing::Btree tree(test_wal_path);

    for (int i = 99; i >= 0; --i) {
        auto status = tree.put("key" + std::to_string(i), "value" + std::to_string(i));
        ASSERT_TRUE(status.ok());
    }

    for (int i = 0; i < 100; ++i) {
        auto result = tree.get("key" + std::to_string(i));
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result.value(), "value" + std::to_string(i));
    }
}

TEST_F(BtreeStressTest, InterleavedInsertDelete) {
    indexing::Btree tree(test_wal_path);
    constexpr int count = 50;

    for (int i = 0; i < count; ++i) {
        ASSERT_TRUE(tree.put("pair" + std::to_string(i) + "a", "val_a").ok());
        ASSERT_TRUE(tree.put("pair" + std::to_string(i) + "b", "val_b").ok());
    }

    for (int i = 0; i < count; i += 2) {
        auto status = tree.remove("pair" + std::to_string(i) + "a");
        ASSERT_TRUE(status.ok());
    }

    for (int i = 0; i < count; ++i) {
        auto has_a = tree.get("pair" + std::to_string(i) + "a").has_value();
        auto has_b = tree.get("pair" + std::to_string(i) + "b").has_value();

        if (i % 2 == 0) {
            EXPECT_FALSE(has_a);
        } else {
            EXPECT_TRUE(has_a);
        }
        EXPECT_TRUE(has_b);
    }
}

TEST_F(BtreeStressTest, RandomOperations) {
    indexing::Btree tree(test_wal_path);
    std::map<std::string, std::string> reference;

    std::mt19937 rng(42);
    std::uniform_int_distribution<int> op_dist(0, 2); // 0=put, 1=update, 2=delete

    for (int i = 0; i < 200; ++i) {
        std::string key = "key" + std::to_string(i % 50);
        int op = op_dist(rng);

        if (op == 0) {
            std::string value = "val" + std::to_string(i);
            ASSERT_TRUE(tree.put(key, value).ok());
            reference[key] = value;
        } else if (op == 1) {
            std::string value = "updated" + std::to_string(i);
            auto status = tree.update(key, value);
            if (status.ok()) {
                reference[key] = value;
            }
        } else {
            auto status = tree.remove(key);
            if (status.ok()) {
                reference.erase(key);
            }
        }
    }

    for (int i = 0; i < 50; ++i) {
        std::string key = "key" + std::to_string(i);
        auto result = tree.get(key);
        auto ref_it = reference.find(key);

        if (ref_it != reference.end()) {
            ASSERT_TRUE(result.has_value());
            EXPECT_EQ(result.value(), ref_it->second);
        } else {
            EXPECT_FALSE(result.has_value());
        }
    }
}
