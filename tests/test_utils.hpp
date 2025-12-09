#pragma once
#include "core/common.hpp"
#include "indexing/btree.hpp"
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

namespace embrace::test {

    class BtreeTestFixture : public ::testing::Test {
      protected:
        void SetUp() override {
            test_wal_path_ = "test_embrace.wal";
            test_snapshot_path_ = test_wal_path_ + ".snapshot";
            cleanup_test_files();

            tree_ = std::make_unique<indexing::Btree>(test_wal_path_);
            tree_->set_checkpoint_interval(0); // Disable auto-checkpoints
        }

        void TearDown() override {
            tree_.reset();
            cleanup_test_files();
        }

        void cleanup_test_files() {
            std::filesystem::remove(test_wal_path_);
            std::filesystem::remove(test_snapshot_path_);
        }

        auto create_tree_with_entries(size_t count) -> void {
            for (size_t i = 0; i < count; ++i) {
                auto key = fmt::format("key_{:06d}", i);
                auto value = fmt::format("value_{:06d}", i);
                ASSERT_TRUE(tree_->put(key, value).ok());
            }
        }

        std::unique_ptr<indexing::Btree> tree_;
        std::string test_wal_path_;
        std::string test_snapshot_path_;
    };

    inline auto generate_key(size_t index) -> std::string {
        return fmt::format("foo_{:08d}", index);
    }

    inline auto generate_value(size_t index) -> std::string {
        return fmt::format("bar_{:08d}_payload_data", index);
    }

    inline auto generate_large_value(size_t index) -> std::string {
        return fmt::format("baz_{:08d}_{}", index, std::string(512, 'x'));
    }

} // namespace embrace::test
