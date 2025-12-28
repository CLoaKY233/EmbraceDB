#include "indexing/btree.hpp"
#include "log/logger.hpp"
#include <filesystem>
#include <gtest/gtest.h>
#include <map>
#include <random>

namespace fs = std::filesystem;

namespace embrace::test {

    class StateMachinePropertyTest : public ::testing::Test {
      protected:
        std::string wal_path_ = "test_sm_prop.wal";

        void SetUp() override {
            cleanup();
            log::LogConfig config;
            config.level = log::Level::Error;
            config.console_output = false;
            log::Logger::instance().init(config);
        }

        void TearDown() override {
            cleanup();
        }

        void cleanup() {
            fs::remove(wal_path_);
            fs::remove(wal_path_ + ".snapshot");
        }
    };

    class StateMachine {
      public:
        enum class Op { Put, Update, Delete };

        struct Command {
            Op op;
            std::string key;
            std::string value;
        };

        explicit StateMachine(unsigned seed) : rng_(seed) {}

        auto generate(size_t n) -> std::vector<Command> {
            std::vector<Command> cmds;
            cmds.reserve(n);
            std::set<std::string> keys;

            for (size_t i = 0; i < n; ++i) {
                Command cmd;
                int choice = op_dist_(rng_);

                if (keys.empty() || choice == 0) {
                    cmd.op = Op::Put;
                    cmd.key = "k" + std::to_string(key_dist_(rng_));
                    keys.insert(cmd.key);
                } else {
                    auto it = keys.begin();
                    std::advance(it, static_cast<size_t>(key_dist_(rng_)) % keys.size());
                    cmd.key = *it;

                    if (choice == 1) {
                        cmd.op = Op::Update;
                    } else {
                        cmd.op = Op::Delete;
                        keys.erase(it);
                    }
                }
                cmd.value = "v" + std::to_string(i);
                cmds.push_back(cmd);
            }
            return cmds;
        }

        static auto apply_to_db(indexing::Btree &db, const Command &cmd) -> void {
            switch (cmd.op) {
            case Op::Put:
                (void)db.put(cmd.key, cmd.value);
                break;
            case Op::Update:
                (void)db.update(cmd.key, cmd.value);
                break;
            case Op::Delete:
                (void)db.remove(cmd.key);
                break;
            }
        }

        static auto apply_to_model(std::map<std::string, std::string> &model,
                                   const Command &cmd) -> void {
            switch (cmd.op) {
            case Op::Put:
                model[cmd.key] = cmd.value;
                break;
            case Op::Update:
                if (model.count(cmd.key)) {
                    model[cmd.key] = cmd.value;
                }
                break;
            case Op::Delete:
                model.erase(cmd.key);
                break;
            }
        }

      private:
        std::mt19937 rng_;
        std::uniform_int_distribution<int> key_dist_{0, 100};
        std::uniform_int_distribution<int> op_dist_{0, 2};
    };

    TEST_F(StateMachinePropertyTest, ModelEquivalence) {
        for (unsigned seed = 1; seed <= 5; ++seed) {
            cleanup();

            StateMachine sm(seed);
            auto cmds = sm.generate(200);

            std::map<std::string, std::string> model;
            {
                indexing::Btree db(wal_path_);
                for (const auto &cmd : cmds) {
                    StateMachine::apply_to_db(db, cmd);
                    StateMachine::apply_to_model(model, cmd);
                }
                (void)db.flush_wal();
            }

            indexing::Btree recovered(wal_path_);
            ASSERT_TRUE(recovered.recover_from_wal().ok()) << "seed=" << seed;

            for (const auto &[k, v] : model) {
                auto result = recovered.get(k);
                ASSERT_TRUE(result.has_value()) << "missing key=" << k << " seed=" << seed;
                EXPECT_EQ(result.value(), v) << "key=" << k << " seed=" << seed;
            }
        }
    }

    TEST_F(StateMachinePropertyTest, LastWriteWins) {
        indexing::Btree db(wal_path_);

        std::string last_value;
        for (int i = 0; i < 100; ++i) {
            last_value = "value_" + std::to_string(i);
            ASSERT_TRUE(db.put("contested_key", last_value).ok());
        }

        auto result = db.get("contested_key");
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result.value(), last_value);
    }

    TEST_F(StateMachinePropertyTest, DeleteRemoves) {
        indexing::Btree db(wal_path_);

        ASSERT_TRUE(db.put("ephemeral", "exists").ok());
        ASSERT_TRUE(db.get("ephemeral").has_value());

        ASSERT_TRUE(db.remove("ephemeral").ok());
        EXPECT_FALSE(db.get("ephemeral").has_value());
    }

    TEST_F(StateMachinePropertyTest, RecoveryPreservesState) {
        std::map<std::string, std::string> expected;

        {
            indexing::Btree db(wal_path_);
            for (int i = 0; i < 50; ++i) {
                std::string k = "key_" + std::to_string(i);
                std::string v = "val_" + std::to_string(i);
                ASSERT_TRUE(db.put(k, v).ok());
                expected[k] = v;
            }
            (void)db.flush_wal();
        }

        indexing::Btree recovered(wal_path_);
        ASSERT_TRUE(recovered.recover_from_wal().ok());

        for (const auto &[k, v] : expected) {
            auto result = recovered.get(k);
            ASSERT_TRUE(result.has_value()) << "key=" << k;
            EXPECT_EQ(result.value(), v);
        }
    }

    TEST_F(StateMachinePropertyTest, CheckpointEquivalence) {
        std::map<std::string, std::string> expected;

        {
            indexing::Btree db(wal_path_);
            for (int i = 0; i < 100; ++i) {
                std::string k = "k" + std::to_string(i);
                std::string v = "v" + std::to_string(i);
                (void)db.put(k, v);
                expected[k] = v;
            }
            ASSERT_TRUE(db.create_checkpoint().ok());

            for (int i = 0; i < 50; ++i) {
                std::string k = "post_" + std::to_string(i);
                std::string v = "pv_" + std::to_string(i);
                (void)db.put(k, v);
                expected[k] = v;
            }
            (void)db.flush_wal();
        }

        indexing::Btree recovered(wal_path_);
        ASSERT_TRUE(recovered.recover_from_wal().ok());

        size_t count = 0;
        recovered.iterate_all([&](const auto &, const auto &) { count++; });
        EXPECT_EQ(count, expected.size());

        for (const auto &[k, v] : expected) {
            auto result = recovered.get(k);
            ASSERT_TRUE(result.has_value()) << "key=" << k;
            EXPECT_EQ(result.value(), v);
        }
    }

    TEST_F(StateMachinePropertyTest, UpdateOnlyAffectsExisting) {
        indexing::Btree db(wal_path_);

        auto status = db.update("nonexistent", "value");
        EXPECT_FALSE(status.ok());
        EXPECT_FALSE(db.get("nonexistent").has_value());

        ASSERT_TRUE(db.put("exists", "original").ok());
        ASSERT_TRUE(db.update("exists", "modified").ok());

        auto result = db.get("exists");
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result.value(), "modified");
    }

    TEST_F(StateMachinePropertyTest, RecoveryIdempotent) {
        {
            indexing::Btree db(wal_path_);
            for (int i = 0; i < 100; ++i) {
                (void)db.put("k" + std::to_string(i), "v" + std::to_string(i));
            }
            (void)db.flush_wal();
        }

        std::map<std::string, std::string> baseline;
        {
            indexing::Btree db(wal_path_);
            ASSERT_TRUE(db.recover_from_wal().ok());
            db.iterate_all([&](const auto &k, const auto &v) { baseline[std::string(k)] = v; });
        }

        for (int round = 0; round < 3; ++round) {
            std::map<std::string, std::string> current;
            indexing::Btree db(wal_path_);
            ASSERT_TRUE(db.recover_from_wal().ok());
            db.iterate_all([&](const auto &k, const auto &v) { current[std::string(k)] = v; });

            EXPECT_EQ(baseline, current) << "diverged at round " << round;
        }
    }

    TEST_F(StateMachinePropertyTest, OperationOrderMatters) {
        {
            indexing::Btree db(wal_path_);
            ASSERT_TRUE(db.put("key", "first").ok());
            ASSERT_TRUE(db.put("key", "second").ok());
            ASSERT_TRUE(db.remove("key").ok());
            ASSERT_TRUE(db.put("key", "third").ok());
            (void)db.flush_wal();
        }

        indexing::Btree recovered(wal_path_);
        ASSERT_TRUE(recovered.recover_from_wal().ok());

        auto result = recovered.get("key");
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result.value(), "third");
    }

} // namespace embrace::test
