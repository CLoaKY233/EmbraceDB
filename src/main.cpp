#include "indexing/btree.hpp"
#include "log/logger.hpp"
#include <cstdio>

auto main() -> int {
    using namespace ::embrace;

    log::LogConfig config;
    config.level = log::Level::Debug;
    config.console_output = true;
    config.file_path = "embrace.log";
    log::Logger::instance().init(config);

    LOG_INFO("=== EmbraceDB - Complete CRUD Implementation ===");

    std::remove("embrace.wal");

    {
        LOG_INFO("--- Test 1: Basic INSERT & READ ---");
        indexing::Btree tree("embrace.wal");

        tree.put("apple", "red");
        tree.put("banana", "yellow");
        tree.put("cherry", "red");
        tree.put("date", "brown");
        tree.put("elderberry", "purple");
        tree.put("fig", "green");
        tree.put("grape", "purple");
        tree.put("honeydew", "green");

        LOG_DEBUG("Tree after inserts:");
        tree.print_tree();

        auto val = tree.get("banana");
        LOG_INFO("GET banana: {}", val.value_or("NOT FOUND"));
    }

    {
        LOG_INFO("--- Test 2: UPDATE Operation ---");
        indexing::Btree tree("embrace.wal");
        tree.recover_from_wal();

        // Update existing key
        auto status = tree.update("banana", "green-yellow");
        if (!status.ok()) {
            LOG_ERROR("UPDATE failed: {}", status.to_string());
            return 1;
        }

        auto val = tree.get("banana");
        LOG_INFO("After UPDATE - banana: {}", val.value_or("NOT FOUND"));

        // Try updating non-existent key
        status = tree.update("mango", "orange");
        if (status.is_not_found()) {
            LOG_DEBUG("UPDATE correctly rejected non-existent key");
        } else {
            LOG_ERROR("UPDATE should have failed for non-existent key");
            return 1;
        }

        tree.flush_wal();
    }

    {
        LOG_INFO("--- Test 3: DELETE Operation (No Underflow) ---");
        indexing::Btree tree("embrace.wal");
        tree.recover_from_wal();

        LOG_DEBUG("Before DELETE:");
        tree.print_tree();

        auto status = tree.remove("cherry");
        if (!status.ok()) {
            LOG_ERROR("DELETE failed: {}", status.to_string());
            return 1;
        }

        LOG_DEBUG("After DELETE cherry:");
        tree.print_tree();

        auto val = tree.get("cherry");
        if (!val.has_value()) {
            LOG_INFO("cherry successfully deleted");
        } else {
            LOG_ERROR("cherry still exists after deletion");
            return 1;
        }

        tree.flush_wal();
    }

    {
        LOG_INFO("--- Test 4: DELETE with Rebalancing ---");
        indexing::Btree tree("embrace.wal");
        tree.recover_from_wal();

        // Delete multiple keys to trigger rebalancing
        tree.remove("apple");
        tree.remove("date");
        tree.remove("elderberry");

        LOG_DEBUG("After multiple DELETEs:");
        tree.print_tree();

        // Verify remaining keys
        auto val1 = tree.get("banana");
        auto val2 = tree.get("fig");
        auto val3 = tree.get("grape");

        if (val1.has_value() && val2.has_value() && val3.has_value()) {
            LOG_INFO("Remaining keys intact after rebalancing");
        } else {
            LOG_ERROR("Some keys lost during rebalancing");
            return 1;
        }

        tree.flush_wal();
    }

    {
        LOG_INFO("--- Test 5: WAL Recovery with UPDATE & DELETE ---");
        indexing::Btree tree("embrace.wal");

        auto status = tree.recover_from_wal();
        if (!status.ok()) {
            LOG_ERROR("Recovery failed: {}", status.to_string());
            return 1;
        }

        LOG_DEBUG("Tree after full recovery:");
        tree.print_tree();

        // Add new data after recovery
        tree.put("kiwi", "brown");
        tree.put("lemon", "yellow");

        LOG_DEBUG("Final tree state:");
        tree.print_tree();
    }

    {
        LOG_INFO("--- Test 6: Edge Cases ---");
        indexing::Btree tree;

        // Delete from empty tree
        auto status = tree.remove("nonexistent");
        if (status.is_not_found()) {
            LOG_DEBUG("DELETE correctly handles empty tree");
        }

        // Update empty tree
        status = tree.update("nonexistent", "value");
        if (status.is_not_found()) {
            LOG_DEBUG("UPDATE correctly handles empty tree");
        }

        // Insert then delete all
        tree.put("single", "value");
        tree.remove("single");

        auto val = tree.get("single");
        if (!val.has_value()) {
            LOG_DEBUG("Single insert/delete works correctly");
        }
    }

    {
        LOG_INFO("--- Test 7: UPDATE vs PUT in WAL ---");
        std::remove("embrace.wal");

        {
            indexing::Btree tree("embrace.wal");

            // Initial PUT
            tree.put("versioned_key", "v1");

            // UPDATE (should log as Update type)
            auto status = tree.update("versioned_key", "v2");
            if (!status.ok()) {
                LOG_ERROR("UPDATE failed: {}", status.to_string());
                return 1;
            }

            // Another UPDATE
            status = tree.update("versioned_key", "v3");
            if (!status.ok()) {
                LOG_ERROR("Second UPDATE failed: {}", status.to_string());
                return 1;
            }

            tree.flush_wal();
            LOG_INFO("Written: 1 PUT + 2 UPDATEs to WAL");
        }

        {
            // Recovery should replay correctly
            indexing::Btree tree("embrace.wal");
            auto status = tree.recover_from_wal();
            if (!status.ok()) {
                LOG_ERROR("Recovery failed: {}", status.to_string());
                return 1;
            }

            auto val = tree.get("versioned_key");
            if (val.has_value() && val.value() == "v3") {
                LOG_INFO("Recovery replayed PUT+UPDATEs correctly: {}", val.value());
            } else {
                LOG_ERROR("Recovery produced wrong value: {}", val.value_or("MISSING"));
                return 1;
            }
        }
    }

    LOG_INFO("=== All CRUD tests passed! ===");
    log::Logger::instance().shutdown();
    return 0;
}
