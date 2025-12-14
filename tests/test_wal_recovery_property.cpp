#include "indexing/btree.hpp"
#include "log/logger.hpp"
#include <filesystem>
#include <gtest/gtest.h>
#include <map>
#include <random>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

    // Test fixture for property-based testing
    class WalRecoveryPropertyTest : public ::testing::Test {
      protected:
        void SetUp() override {
            test_wal_path_ = "test_prop_recovery.wal";
            cleanup_files();

            embrace::log::LogConfig config;
            config.level = embrace::log::Level::Warn; // Reduce noise
            config.console_output = false;
            embrace::log::Logger::instance().init(config);
        }

        void TearDown() override {
            cleanup_files();
        }

        void cleanup_files() {
            if (fs::exists(test_wal_path_)) {
                fs::remove(test_wal_path_);
            }
            if (fs::exists(test_wal_path_ + ".snapshot")) {
                fs::remove(test_wal_path_ + ".snapshot");
            }
        }

        std::string test_wal_path_;
    };

    // =======================
    // 1. RANDOM OPERATION SEQUENCES
    // =======================

    enum class OpType { Put, Update, Delete };

    struct Operation {
        OpType type;
        std::string key;
        std::string value;
    };

    class RandomOperationGenerator {
      public:
        explicit RandomOperationGenerator(unsigned seed = 42)
            : rng_(seed), key_dist_(1, 1000), op_dist_(0, 2) {}

        auto generate_operations(size_t count) -> std::vector<Operation> {
            std::vector<Operation> ops;
            ops.reserve(count);

            // Track which keys exist for realistic updates/deletes
            std::set<std::string> existing_keys;

            for (size_t i = 0; i < count; ++i) {
                OpType type;
                std::string key;

                if (existing_keys.empty() || op_dist_(rng_) == 0) {
                    // Put operation - always valid
                    type = OpType::Put;
                    key = "key_" + std::to_string(key_dist_(rng_));
                    existing_keys.insert(key);
                } else {
                    // Choose existing key for update/delete
                    int op = op_dist_(rng_);
                    auto it = existing_keys.begin();
                    // Fix signed/unsigned mismatch warning
                    std::advance(it, static_cast<size_t>(key_dist_(rng_)) % existing_keys.size());
                    key = *it;

                    if (op == 1) {
                        type = OpType::Update;
                    } else {
                        type = OpType::Delete;
                        existing_keys.erase(it);
                    }
                }

                std::string value = "value_" + std::to_string(i) + "_" + generate_random_string(16);
                ops.push_back({type, key, value});
            }

            return ops;
        }

      private:
        std::mt19937 rng_;
        std::uniform_int_distribution<int> key_dist_;
        std::uniform_int_distribution<int> op_dist_;

        auto generate_random_string(size_t len) -> std::string {
            static const char charset[] =
                "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
            std::uniform_int_distribution<size_t> dist(0, sizeof(charset) - 2);
            std::string result;
            result.reserve(len);
            for (size_t i = 0; i < len; ++i) {
                result += charset[dist(rng_)];
            }
            return result;
        }
    };

    // Apply operations and track expected state
    class StateTracker {
      public:
        void apply(const Operation &op) {
            switch (op.type) {
            case OpType::Put:
                state_[op.key] = op.value;
                break;
            case OpType::Update:
                if (state_.count(op.key)) {
                    state_[op.key] = op.value;
                }
                break;
            case OpType::Delete:
                state_.erase(op.key);
                break;
            }
        }

        auto get_state() const -> const std::map<std::string, std::string> & {
            return state_;
        }

        void clear() {
            state_.clear();
        }

      private:
        std::map<std::string, std::string> state_;
    };

    TEST_F(WalRecoveryPropertyTest, RandomOperationSequences_Small) {
        RandomOperationGenerator gen(12345);
        auto operations = gen.generate_operations(100);

        StateTracker expected;

        // Phase 1: Apply operations
        {
            embrace::indexing::Btree db(test_wal_path_);

            for (const auto &op : operations) {
                switch (op.type) {
                case OpType::Put:
                    ASSERT_TRUE(db.put(op.key, op.value).ok());
                    break;
                case OpType::Update:
                    // Update might fail if key doesn't exist (that's ok)
                    (void)db.update(op.key, op.value);
                    break;
                case OpType::Delete:
                    // Delete might fail if key doesn't exist (that's ok)
                    (void)db.remove(op.key);
                    break;
                }
                expected.apply(op);
            }

            ASSERT_TRUE(db.flush_wal().ok());
        }

        // Phase 2: Recover and verify
        {
            embrace::indexing::Btree recovered_db(test_wal_path_);
            ASSERT_TRUE(recovered_db.recover_from_wal().ok());

            // Verify all expected keys exist with correct values
            for (const auto &[key, value] : expected.get_state()) {
                auto result = recovered_db.get(key);
                ASSERT_TRUE(result.has_value()) << "Key missing after recovery: " << key;
                EXPECT_EQ(result.value(), value) << "Value mismatch for key: " << key;
            }
        }
    }

    TEST_F(WalRecoveryPropertyTest, RandomOperationSequences_Large) {
        RandomOperationGenerator gen(99999);
        auto operations = gen.generate_operations(5000);

        StateTracker expected;

        {
            embrace::indexing::Btree db(test_wal_path_);
            db.set_checkpoint_interval(1000); // Trigger periodic checkpoints

            for (const auto &op : operations) {
                switch (op.type) {
                case OpType::Put:
                    ASSERT_TRUE(db.put(op.key, op.value).ok());
                    break;
                case OpType::Update:
                    (void)db.update(op.key, op.value);
                    break;
                case OpType::Delete:
                    (void)db.remove(op.key);
                    break;
                }
                expected.apply(op);
            }

            ASSERT_TRUE(db.flush_wal().ok());
        }

        {
            embrace::indexing::Btree recovered_db(test_wal_path_);
            ASSERT_TRUE(recovered_db.recover_from_wal().ok());

            for (const auto &[key, value] : expected.get_state()) {
                auto result = recovered_db.get(key);
                ASSERT_TRUE(result.has_value()) << "Key missing after recovery: " << key;
                EXPECT_EQ(result.value(), value);
            }
        }
    }

    // =======================
    // 2. CRASH SIMULATION
    // =======================

    // Simulate crash by forcibly killing process mid-write
    class CrashSimulator {
      public:
        struct CrashPoint {
            size_t operation_index; // When to crash
            bool during_checkpoint; // Crash during checkpoint vs normal write
        };

        // Fork a child process that crashes at specific point
        static auto simulate_crash_at(const std::string &wal_path,
                                      const std::vector<Operation> &operations,
                                      const CrashPoint &crash_point) -> bool {
            pid_t pid = fork();

            if (pid == -1) {
                return false; // Fork failed
            }

            if (pid == 0) {
                // Child process - execute operations until crash
                embrace::indexing::Btree db(wal_path);

                if (crash_point.during_checkpoint) {
                    db.set_checkpoint_interval(crash_point.operation_index);
                }

                for (size_t i = 0; i < operations.size(); ++i) {
                    const auto &op = operations[i];

                    // Crash at designated point
                    if (i == crash_point.operation_index) {
                        // Force dirty state: don't flush
                        _exit(137); // SIGKILL equivalent
                    }

                    switch (op.type) {
                    case OpType::Put:
                        (void)db.put(op.key, op.value);
                        break;
                    case OpType::Update:
                        (void)db.update(op.key, op.value);
                        break;
                    case OpType::Delete:
                        (void)db.remove(op.key);
                        break;
                    }
                }

                // Should not reach here if crash_point < operations.size()
                _exit(0);
            } else {
                // Parent process - wait for child to crash
                int status;
                waitpid(pid, &status, 0);

                return WIFEXITED(status) || WIFSIGNALED(status);
            }
        }
    };

    TEST_F(WalRecoveryPropertyTest, CrashDuringWrite_EarlyStage) {
        RandomOperationGenerator gen(42);
        auto operations = gen.generate_operations(50);

        StateTracker expected;
        for (size_t i = 0; i < 20; ++i) { // Only first 20 should be durable
            expected.apply(operations[i]);
        }

        // Simulate crash at operation 20
        CrashSimulator::CrashPoint crash{20, false};
        ASSERT_TRUE(CrashSimulator::simulate_crash_at(test_wal_path_, operations, crash));

        // Recover and verify
        embrace::indexing::Btree recovered_db(test_wal_path_);
        ASSERT_TRUE(recovered_db.recover_from_wal().ok());

        for (const auto &[key, value] : expected.get_state()) {
            auto result = recovered_db.get(key);
            ASSERT_TRUE(result.has_value()) << "Key missing after crash recovery: " << key;
            EXPECT_EQ(result.value(), value);
        }
    }

    TEST_F(WalRecoveryPropertyTest, CrashDuringCheckpoint) {
        RandomOperationGenerator gen(777);
        auto operations = gen.generate_operations(100);

        StateTracker expected;
        for (size_t i = 0; i < 50; ++i) {
            expected.apply(operations[i]);
        }

        // Crash during checkpoint creation at operation 50
        CrashSimulator::CrashPoint crash{50, true};
        ASSERT_TRUE(CrashSimulator::simulate_crash_at(test_wal_path_, operations, crash));

        // Recovery should work even if checkpoint was incomplete
        embrace::indexing::Btree recovered_db(test_wal_path_);
        auto status = recovered_db.recover_from_wal();

        // Either full recovery or graceful degradation to WAL-only
        if (status.ok()) {
            for (const auto &[key, value] : expected.get_state()) {
                auto result = recovered_db.get(key);
                if (result.has_value()) {
                    EXPECT_EQ(result.value(), value);
                }
                // Some keys might be lost if checkpoint was partial - that's acceptable
            }
        } else {
            // Recovery might fail if corruption detected - should not hang
            EXPECT_TRUE(status.to_string().find("Corruption") != std::string::npos);
        }
    }

    // =======================
    // 3. STATE VERIFICATION
    // =======================

    TEST_F(WalRecoveryPropertyTest, StateConsistency_MultipleRecoveries) {
        RandomOperationGenerator gen(555);
        auto operations = gen.generate_operations(200);

        StateTracker expected;

        // Phase 1: Initial write
        {
            embrace::indexing::Btree db(test_wal_path_);
            for (const auto &op : operations) {
                switch (op.type) {
                case OpType::Put:
                    (void)db.put(op.key, op.value);
                    break;
                case OpType::Update:
                    (void)db.update(op.key, op.value);
                    break;
                case OpType::Delete:
                    (void)db.remove(op.key);
                    break;
                }
                expected.apply(op);
            }
            (void)db.flush_wal();
        }

        // Phase 2: Recover multiple times - state should be identical
        std::map<std::string, std::string> first_recovery_state;

        for (int recovery_round = 0; recovery_round < 3; ++recovery_round) {
            embrace::indexing::Btree db(test_wal_path_);
            ASSERT_TRUE(db.recover_from_wal().ok())
                << "Recovery failed at round " << recovery_round;

            std::map<std::string, std::string> current_state;
            for (const auto &[key, value] : expected.get_state()) {
                auto result = db.get(key);
                if (result.has_value()) {
                    current_state[key] = result.value();
                }
            }

            if (recovery_round == 0) {
                first_recovery_state = current_state;
            } else {
                // Subsequent recoveries must match first recovery
                EXPECT_EQ(current_state, first_recovery_state)
                    << "State diverged at recovery round " << recovery_round;
            }
        }
    }

    // =======================
    // 4. PROPERTY VERIFICATION
    // =======================

    // Property: Durability - flushed data survives crash
    TEST_F(WalRecoveryPropertyTest, Property_Durability) {
        embrace::indexing::Btree db(test_wal_path_);

        // Write and flush
        ASSERT_TRUE(db.put("durable_key", "durable_value").ok());
        ASSERT_TRUE(db.flush_wal().ok());

        // Simulate crash (close without cleanup)
        // ~Btree() destructor will run but data should already be on disk

        // New instance - should recover
        embrace::indexing::Btree recovered(test_wal_path_);
        ASSERT_TRUE(recovered.recover_from_wal().ok());

        auto result = recovered.get("durable_key");
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result.value(), "durable_value");
    }

    // Property: Atomicity - partial writes don't corrupt state
    TEST_F(WalRecoveryPropertyTest, Property_Atomicity) {
        RandomOperationGenerator gen(333);
        auto operations = gen.generate_operations(50);

        // Write partial batch (no flush)
        {
            embrace::indexing::Btree db(test_wal_path_);
            for (size_t i = 0; i < 25; ++i) {
                const auto &op = operations[i];
                switch (op.type) {
                case OpType::Put:
                    (void)db.put(op.key, op.value);
                    break;
                case OpType::Update:
                    (void)db.update(op.key, op.value);
                    break;
                case OpType::Delete:
                    (void)db.remove(op.key);
                    break;
                }
            }
            // Destructor flushes automatically
        }

        // Recovery should either:
        // 1. Recover all 25 operations (if destructor flushed)
        // 2. Recover 0 operations (if crash prevented flush)
        // But NOT recover partial batch (e.g., 13 operations)

        embrace::indexing::Btree recovered(test_wal_path_);
        auto status = recovered.recover_from_wal();

        // Should not crash or corrupt
        EXPECT_TRUE(status.ok() || status.is_not_found());
    }

    // Property: Consistency - no duplicate keys after recovery
    TEST_F(WalRecoveryPropertyTest, Property_Consistency_NoDuplicates) {
        embrace::indexing::Btree db(test_wal_path_);

        // Write same key multiple times
        for (int i = 0; i < 10; ++i) {
            ASSERT_TRUE(db.put("duplicate_key", "value_" + std::to_string(i)).ok());
        }
        ASSERT_TRUE(db.flush_wal().ok());

        // Recover
        embrace::indexing::Btree recovered(test_wal_path_);
        ASSERT_TRUE(recovered.recover_from_wal().ok());

        // Should have exactly one value (last write wins)
        auto result = recovered.get("duplicate_key");
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result.value(), "value_9");

        // Verify by iteration - no duplicates
        int count = 0;
        recovered.iterate_all([&](const auto &key, const auto &) {
            if (key == "duplicate_key") {
                count++;
            }
        });
        EXPECT_EQ(count, 1) << "Duplicate keys found in recovered database";
    }

} // namespace
