#include "indexing/btree.hpp"
#include "log/logger.hpp"
#include <chrono>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace embrace::test {

    class FailureInjectionTest : public ::testing::Test {
      protected:
        void SetUp() override {
            test_wal_path_ = "test_failure.wal";
            test_snapshot_path_ = test_wal_path_ + ".snapshot";
            cleanup_files();

            log::LogConfig config;
            config.level = log::Level::Error;
            config.console_output = false;
            log::Logger::instance().init(config);
        }

        void TearDown() override {
            cleanup_files();
        }

        void cleanup_files() {
            fs::remove(test_wal_path_);
            fs::remove(test_snapshot_path_);
            fs::remove(test_wal_path_ + ".tmp");
        }

        void write_valid_wal_with_entries(size_t count) {
            indexing::Btree db(test_wal_path_);
            for (size_t i = 0; i < count; ++i) {
                (void)db.put("key_" + std::to_string(i), "value_" + std::to_string(i));
            }
            (void)db.flush_wal();
        }

        void corrupt_file_at_offset(const std::string &path, size_t offset,
                                    uint8_t xor_val = 0xFF) {
            std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
            if (!file)
                return;
            file.seekp(static_cast<std::streamoff>(offset));
            char byte;
            file.read(&byte, 1);
            file.seekp(static_cast<std::streamoff>(offset));
            byte ^= static_cast<char>(xor_val);
            file.write(&byte, 1);
        }

        void truncate_file(const std::string &path, size_t new_size) {
            if (truncate(path.c_str(), static_cast<off_t>(new_size)) != 0) {
                FAIL() << "Failed to truncate file";
            }
        }

        auto get_file_size(const std::string &path) -> size_t {
            return fs::file_size(path);
        }

        std::string test_wal_path_;
        std::string test_snapshot_path_;
    };

    TEST_F(FailureInjectionTest, CorruptedWalCrcMismatch) {
        write_valid_wal_with_entries(10);
        size_t file_size = get_file_size(test_wal_path_);
        ASSERT_GT(file_size, 20u);

        corrupt_file_at_offset(test_wal_path_, file_size / 2);

        indexing::Btree recovered(test_wal_path_);
        auto status = recovered.recover_from_wal();

        EXPECT_FALSE(status.ok());
        EXPECT_TRUE(status.to_string().find("CRC") != std::string::npos ||
                    status.to_string().find("Corruption") != std::string::npos);
    }

    TEST_F(FailureInjectionTest, TruncatedWalPartialRecord) {
        write_valid_wal_with_entries(10);
        size_t file_size = get_file_size(test_wal_path_);
        ASSERT_GT(file_size, 10u);

        truncate_file(test_wal_path_, file_size - 5);

        indexing::Btree recovered(test_wal_path_);
        auto status = recovered.recover_from_wal();

        EXPECT_FALSE(status.ok());
    }

    TEST_F(FailureInjectionTest, EmptyWalFile) {
        std::ofstream file(test_wal_path_);
        file.close();

        indexing::Btree recovered(test_wal_path_);
        auto status = recovered.recover_from_wal();

        EXPECT_TRUE(status.ok());
    }

    TEST_F(FailureInjectionTest, CorruptedSnapshotMagic) {
        {
            indexing::Btree db(test_wal_path_);
            for (int i = 0; i < 20; ++i) {
                (void)db.put("key_" + std::to_string(i), "value_" + std::to_string(i));
            }
            (void)db.create_checkpoint();
        }

        ASSERT_TRUE(fs::exists(test_snapshot_path_));

        corrupt_file_at_offset(test_snapshot_path_, 0);

        indexing::Btree recovered(test_wal_path_);
        auto status = recovered.recover_from_wal();

        EXPECT_FALSE(status.ok());
        EXPECT_TRUE(status.to_string().find("magic") != std::string::npos ||
                    status.to_string().find("Corruption") != std::string::npos);
    }

    TEST_F(FailureInjectionTest, CorruptedSnapshotEntryCrc) {
        {
            indexing::Btree db(test_wal_path_);
            for (int i = 0; i < 20; ++i) {
                (void)db.put("key_" + std::to_string(i), "value_" + std::to_string(i));
            }
            (void)db.create_checkpoint();
        }

        ASSERT_TRUE(fs::exists(test_snapshot_path_));
        size_t file_size = get_file_size(test_snapshot_path_);

        corrupt_file_at_offset(test_snapshot_path_, file_size - 10);

        indexing::Btree recovered(test_wal_path_);
        auto status = recovered.recover_from_wal();

        EXPECT_FALSE(status.ok());
    }

    TEST_F(FailureInjectionTest, TruncatedSnapshot) {
        {
            indexing::Btree db(test_wal_path_);
            for (int i = 0; i < 50; ++i) {
                (void)db.put("key_" + std::to_string(i), "value_" + std::to_string(i));
            }
            (void)db.create_checkpoint();
        }

        size_t file_size = get_file_size(test_snapshot_path_);
        truncate_file(test_snapshot_path_, file_size / 2);

        indexing::Btree recovered(test_wal_path_);
        auto status = recovered.recover_from_wal();

        EXPECT_FALSE(status.ok());
    }

    TEST_F(FailureInjectionTest, LargeValuesNearLimit) {
        std::string large_value(core::MAX_VALUE_SIZE - 1, 'x');
        std::string max_value(core::MAX_VALUE_SIZE, 'y');

        {
            indexing::Btree db(test_wal_path_);
            ASSERT_TRUE(db.put("large_key", large_value).ok());
            ASSERT_TRUE(db.put("max_key", max_value).ok());
            ASSERT_TRUE(db.flush_wal().ok());
        }

        indexing::Btree recovered(test_wal_path_);
        ASSERT_TRUE(recovered.recover_from_wal().ok());

        auto result1 = recovered.get("large_key");
        ASSERT_TRUE(result1.has_value());
        EXPECT_EQ(result1.value().size(), core::MAX_VALUE_SIZE - 1);

        auto result2 = recovered.get("max_key");
        ASSERT_TRUE(result2.has_value());
        EXPECT_EQ(result2.value().size(), core::MAX_VALUE_SIZE);
    }

    TEST_F(FailureInjectionTest, LargeKeysNearLimit) {
        std::string large_key(core::MAX_KEY_SIZE - 1, 'k');
        std::string max_key(core::MAX_KEY_SIZE, 'm');

        {
            indexing::Btree db(test_wal_path_);
            ASSERT_TRUE(db.put(large_key, "value1").ok());
            ASSERT_TRUE(db.put(max_key, "value2").ok());
            ASSERT_TRUE(db.flush_wal().ok());
        }

        indexing::Btree recovered(test_wal_path_);
        ASSERT_TRUE(recovered.recover_from_wal().ok());

        EXPECT_TRUE(recovered.get(large_key).has_value());
        EXPECT_TRUE(recovered.get(max_key).has_value());
    }

    TEST_F(FailureInjectionTest, EmptyValues) {
        {
            indexing::Btree db(test_wal_path_);
            ASSERT_TRUE(db.put("empty_value_key", "").ok());
            ASSERT_TRUE(db.put("normal_key", "normal_value").ok());
            ASSERT_TRUE(db.flush_wal().ok());
        }

        indexing::Btree recovered(test_wal_path_);
        ASSERT_TRUE(recovered.recover_from_wal().ok());

        auto result = recovered.get("empty_value_key");
        ASSERT_TRUE(result.has_value());
        EXPECT_TRUE(result.value().empty());

        EXPECT_TRUE(recovered.get("normal_key").has_value());
    }

    TEST_F(FailureInjectionTest, InterleavedSnapshotAndWal) {
        {
            indexing::Btree db(test_wal_path_);

            for (int i = 0; i < 25; ++i) {
                (void)db.put("batch1_" + std::to_string(i), "v1_" + std::to_string(i));
            }
            (void)db.create_checkpoint();

            for (int i = 0; i < 25; ++i) {
                (void)db.put("batch2_" + std::to_string(i), "v2_" + std::to_string(i));
            }
            (void)db.flush_wal();

            (void)db.create_checkpoint();

            for (int i = 0; i < 25; ++i) {
                (void)db.put("batch3_" + std::to_string(i), "v3_" + std::to_string(i));
            }
            (void)db.flush_wal();
        }

        indexing::Btree recovered(test_wal_path_);
        ASSERT_TRUE(recovered.recover_from_wal().ok());

        int found = 0;
        for (int i = 0; i < 25; ++i) {
            if (recovered.get("batch1_" + std::to_string(i)).has_value())
                found++;
            if (recovered.get("batch2_" + std::to_string(i)).has_value())
                found++;
            if (recovered.get("batch3_" + std::to_string(i)).has_value())
                found++;
        }

        EXPECT_GE(found, 50);
    }

    TEST_F(FailureInjectionTest, SnapshotWithSubsequentDeletes) {
        {
            indexing::Btree db(test_wal_path_);
            for (int i = 0; i < 50; ++i) {
                (void)db.put("key_" + std::to_string(i), "value_" + std::to_string(i));
            }
            (void)db.create_checkpoint();

            for (int i = 0; i < 25; ++i) {
                (void)db.remove("key_" + std::to_string(i));
            }
            (void)db.flush_wal();
        }

        indexing::Btree recovered(test_wal_path_);
        ASSERT_TRUE(recovered.recover_from_wal().ok());

        for (int i = 0; i < 25; ++i) {
            EXPECT_FALSE(recovered.get("key_" + std::to_string(i)).has_value());
        }
        for (int i = 25; i < 50; ++i) {
            EXPECT_TRUE(recovered.get("key_" + std::to_string(i)).has_value());
        }
    }

    TEST_F(FailureInjectionTest, RecoveryIdempotence) {
        {
            indexing::Btree db(test_wal_path_);
            for (int i = 0; i < 100; ++i) {
                (void)db.put("key_" + std::to_string(i), "value_" + std::to_string(i));
            }
            (void)db.flush_wal();
        }

        std::map<std::string, std::string> first_state;
        {
            indexing::Btree recovered(test_wal_path_);
            ASSERT_TRUE(recovered.recover_from_wal().ok());
            recovered.iterate_all([&](const auto &k, const auto &v) {
                first_state[std::string(k)] = std::string(v);
            });
        }

        for (int round = 0; round < 3; ++round) {
            std::map<std::string, std::string> current_state;
            indexing::Btree recovered(test_wal_path_);
            ASSERT_TRUE(recovered.recover_from_wal().ok());
            recovered.iterate_all([&](const auto &k, const auto &v) {
                current_state[std::string(k)] = std::string(v);
            });

            EXPECT_EQ(first_state, current_state) << "State diverged at round " << round;
        }
    }

    TEST_F(FailureInjectionTest, BinaryDataInValues) {
        std::string binary_value;
        for (int i = 0; i < 256; ++i) {
            binary_value.push_back(static_cast<char>(i));
        }

        {
            indexing::Btree db(test_wal_path_);
            ASSERT_TRUE(db.put("binary_key", binary_value).ok());
            ASSERT_TRUE(db.flush_wal().ok());
        }

        indexing::Btree recovered(test_wal_path_);
        ASSERT_TRUE(recovered.recover_from_wal().ok());

        auto result = recovered.get("binary_key");
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result.value(), binary_value);
    }

    TEST_F(FailureInjectionTest, RapidPutDeleteSameKey) {
        {
            indexing::Btree db(test_wal_path_);
            for (int i = 0; i < 50; ++i) {
                (void)db.put("volatile_key", "value_" + std::to_string(i));
                if (i % 2 == 1) {
                    (void)db.remove("volatile_key");
                }
            }
            (void)db.flush_wal();
        }

        indexing::Btree recovered(test_wal_path_);
        ASSERT_TRUE(recovered.recover_from_wal().ok());

        auto result = recovered.get("volatile_key");
        EXPECT_FALSE(result.has_value());
    }

    TEST_F(FailureInjectionTest, MissingSnapshotWithValidWal) {
        {
            indexing::Btree db(test_wal_path_);
            for (int i = 0; i < 50; ++i) {
                (void)db.put("key_" + std::to_string(i), "value_" + std::to_string(i));
            }
            (void)db.create_checkpoint();
            for (int i = 50; i < 100; ++i) {
                (void)db.put("key_" + std::to_string(i), "value_" + std::to_string(i));
            }
            (void)db.flush_wal();
        }

        fs::remove(test_snapshot_path_);

        indexing::Btree recovered(test_wal_path_);
        ASSERT_TRUE(recovered.recover_from_wal().ok());

        int count = 0;
        recovered.iterate_all([&](const auto &, const auto &) { count++; });
        EXPECT_GE(count, 50);
    }

    TEST_F(FailureInjectionTest, PerformanceBaseline_Recovery) {
        constexpr size_t NUM_ENTRIES = 5000;

        {
            indexing::Btree db(test_wal_path_);
            for (size_t i = 0; i < NUM_ENTRIES; ++i) {
                (void)db.put("key_" + std::to_string(i), "value_" + std::to_string(i));
            }
            (void)db.create_checkpoint();
        }

        auto start = std::chrono::steady_clock::now();

        indexing::Btree recovered(test_wal_path_);
        ASSERT_TRUE(recovered.recover_from_wal().ok());

        auto elapsed = std::chrono::steady_clock::now() - start;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

        EXPECT_LT(ms, 5000) << "Recovery took " << ms << "ms, expected < 5000ms";

        size_t count = 0;
        recovered.iterate_all([&](const auto &, const auto &) { count++; });
        EXPECT_EQ(count, NUM_ENTRIES);
    }

    TEST_F(FailureInjectionTest, PerformanceBaseline_Writes) {
        constexpr size_t NUM_OPS = 10000;

        indexing::Btree db(test_wal_path_);

        auto start = std::chrono::steady_clock::now();

        for (size_t i = 0; i < NUM_OPS; ++i) {
            (void)db.put("key_" + std::to_string(i), "value_" + std::to_string(i));
        }
        (void)db.flush_wal();

        auto elapsed = std::chrono::steady_clock::now() - start;
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

        EXPECT_LT(ms, 10000) << "10k writes took " << ms << "ms, expected < 10000ms";
    }

} // namespace embrace::test
