#include "indexing/btree.hpp"
#include <filesystem>
#include <gtest/gtest.h>
#include <random>
#include <sys/mman.h>

// Stress test: multiple crash-recovery cycles
TEST(CrashSimulationStress, RepeatedCrashRecoveryCycles) {
    const std::string wal_path = "stress_crash_test.wal";
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> op_dist(1, 100);

    for (int cycle = 0; cycle < 10; ++cycle) {
        embrace::indexing::Btree db(wal_path);
        if (cycle > 0) {
            // Recover from previous cycle
            ASSERT_TRUE(db.recover_from_wal().ok()) << "Cycle " << cycle << " recovery failed";
        }

        // Perform random operations
        int num_ops = op_dist(rng);
        for (int i = 0; i < num_ops; ++i) {
            std::string key = "stress_key_" + std::to_string(i % 50);
            std::string value = "value_cycle_" + std::to_string(cycle);

            if (i % 3 == 0) {
                (void)db.put(key, value);
            } else if (i % 3 == 1) {
                (void)db.update(key, value);
            } else {
                (void)db.remove(key);
            }
        }

        // Randomly flush or not (simulate crash vs clean shutdown)
        if (cycle % 2 == 0) {
            (void)db.flush_wal();
        }

        // Destructor triggers flush attempt
    }

    // Final recovery should always work
    embrace::indexing::Btree final_db(wal_path);
    EXPECT_TRUE(final_db.recover_from_wal().ok());

    // Cleanup
    std::filesystem::remove(wal_path);
    std::filesystem::remove(wal_path + ".snapshot");
}
