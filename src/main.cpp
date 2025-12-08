#include "indexing/btree.hpp"
#include "log/logger.hpp"
#include <cstdio>

auto main() -> int {
    using namespace ::embrace;

    log::LogConfig config;
    config.level = log::Level::Info;
    config.console_output = true;
    config.file_path = "embrace.log";
    log::Logger::instance().init(config);

    LOG_INFO("=== EmbraceDB - Checkpointing Test ===\");

    std::remove("embrace.wal");
    std::remove("embrace.wal.snapshot");

    {
        LOG_INFO("--- Test 1: Auto Checkpointing ---");
        indexing::Btree tree("embrace.wal");
        tree.set_checkpoint_interval(50); // Checkpoint every 50 ops

        // Insert 200 keys (should trigger 4 checkpoints)
        for (int i = 0; i < 200; i++) {
            tree.put(fmt::format("key_{:04d}", i), fmt::format("value_{}", i));
        }

        tree.flush_wal();
        LOG_INFO("Inserted 200 keys with auto-checkpointing");
    }

    {
        LOG_INFO("--- Test 2: Fast Recovery from Snapshot ---");
        indexing::Btree tree("embrace.wal");

        auto status = tree.recover_from_wal();
        if (!status.ok()) {
            LOG_ERROR("Recovery failed: {}", status.to_string());
            return 1;
        }

        // Verify random keys
        auto v1 = tree.get("key_0050");
        auto v2 = tree.get("key_0100");
        auto v3 = tree.get("key_0199");

        if (v1 && v2 && v3) {
            LOG_INFO("Recovery successful - all keys present");
            LOG_INFO("  key_0050 = {}", v1.value());
            LOG_INFO("  key_0100 = {}", v2.value());
            LOG_INFO("  key_0199 = {}", v3.value());
        } else {
            LOG_ERROR("Recovery verification failed");
            return 1;
        }
    }

    {
        LOG_INFO("--- Test 3: Manual Checkpoint ---");
        indexing::Btree tree("embrace.wal");
        tree.set_checkpoint_interval(0); // Disable auto

        for (int i = 1000; i < 1100; i++) {
            tree.put(fmt::format("manual_{}", i), "data");
        }

        auto status = tree.create_checkpoint();
        if (!status.ok()) {
            LOG_ERROR("Manual checkpoint failed: {}", status.to_string());
            return 1;
        }

        LOG_INFO("Manual checkpoint successful");
    }

    LOG_INFO("=== All checkpointing tests passed! ===");
    log::Logger::instance().shutdown();
    return 0;
}
