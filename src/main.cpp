#include "indexing/btree.hpp"
#include <cstdio>
#include <fmt/core.h>

auto main() -> int {
    using namespace ::embrace;

    fmt::print("=== EmbraceDB - Complete CRUD Implementation ===\n\n");

    std::remove("embrace.wal");

    {
        fmt::print("--- Test 1: Basic INSERT & READ ---\n");
        indexing::Btree tree("embrace.wal");

        tree.put("apple", "red");
        tree.put("banana", "yellow");
        tree.put("cherry", "red");
        tree.put("date", "brown");
        tree.put("elderberry", "purple");
        tree.put("fig", "green");
        tree.put("grape", "purple");
        tree.put("honeydew", "green");

        fmt::print("Tree after inserts:\n");
        tree.print_tree();

        auto val = tree.get("banana");
        fmt::print("\n✓ GET banana: {}\n", val.value_or("NOT FOUND"));
    }

    {
        fmt::print("\n--- Test 2: UPDATE Operation ---\n");
        indexing::Btree tree("embrace.wal");
        tree.recover_from_wal();

        // Update existing key
        auto status = tree.update("banana", "green-yellow");
        if (!status.ok()) {
            fmt::print("✗ UPDATE failed: {}\n", status.to_string());
            return 1;
        }

        auto val = tree.get("banana");
        fmt::print("✓ After UPDATE - banana: {}\n", val.value_or("NOT FOUND"));

        // Try updating non-existent key
        status = tree.update("mango", "orange");
        if (status.is_not_found()) {
            fmt::print("✓ UPDATE correctly rejected non-existent key\n");
        } else {
            fmt::print("✗ UPDATE should have failed for non-existent key\n");
            return 1;
        }

        tree.flush_wal();
    }

    {
        fmt::print("\n--- Test 3: DELETE Operation (No Underflow) ---\n");
        indexing::Btree tree("embrace.wal");
        tree.recover_from_wal();

        fmt::print("Before DELETE:\n");
        tree.print_tree();

        auto status = tree.remove("cherry");
        if (!status.ok()) {
            fmt::print("✗ DELETE failed: {}\n", status.to_string());
            return 1;
        }

        fmt::print("\nAfter DELETE cherry:\n");
        tree.print_tree();

        auto val = tree.get("cherry");
        if (!val.has_value()) {
            fmt::print("✓ cherry successfully deleted\n");
        } else {
            fmt::print("✗ cherry still exists after deletion\n");
            return 1;
        }

        tree.flush_wal();
    }

    {
        fmt::print("\n--- Test 4: DELETE with Rebalancing ---\n");
        indexing::Btree tree("embrace.wal");
        tree.recover_from_wal();

        // Delete multiple keys to trigger rebalancing
        tree.remove("apple");
        tree.remove("date");
        tree.remove("elderberry");

        fmt::print("After multiple DELETEs:\n");
        tree.print_tree();

        // Verify remaining keys
        auto val1 = tree.get("banana");
        auto val2 = tree.get("fig");
        auto val3 = tree.get("grape");

        if (val1.has_value() && val2.has_value() && val3.has_value()) {
            fmt::print("✓ Remaining keys intact after rebalancing\n");
        } else {
            fmt::print("✗ Some keys lost during rebalancing\n");
            return 1;
        }

        tree.flush_wal();
    }

    {
        fmt::print("\n--- Test 5: WAL Recovery with UPDATE & DELETE ---\n");
        indexing::Btree tree("embrace.wal");

        auto status = tree.recover_from_wal();
        if (!status.ok()) {
            fmt::print("✗ Recovery failed: {}\n", status.to_string());
            return 1;
        }

        fmt::print("Tree after full recovery:\n");
        tree.print_tree();

        // Add new data after recovery
        tree.put("kiwi", "brown");
        tree.put("lemon", "yellow");

        fmt::print("\nFinal tree state:\n");
        tree.print_tree();
    }

    {
        fmt::print("\n--- Test 6: Edge Cases ---\n");
        indexing::Btree tree;

        // Delete from empty tree
        auto status = tree.remove("nonexistent");
        if (status.is_not_found()) {
            fmt::print("✓ DELETE correctly handles empty tree\n");
        }

        // Update empty tree
        status = tree.update("nonexistent", "value");
        if (status.is_not_found()) {
            fmt::print("✓ UPDATE correctly handles empty tree\n");
        }

        // Insert then delete all
        tree.put("single", "value");
        tree.remove("single");

        auto val = tree.get("single");
        if (!val.has_value()) {
            fmt::print("✓ Single insert/delete works correctly\n");
        }
    }

    {
        fmt::print("\n--- Test 7: UPDATE vs PUT in WAL ---\n");
        std::remove("embrace.wal");

        {
            indexing::Btree tree("embrace.wal");

            // Initial PUT
            tree.put("versioned_key", "v1");

            // UPDATE (should log as Update type)
            auto status = tree.update("versioned_key", "v2");
            if (!status.ok()) {
                fmt::print("✗ UPDATE failed: {}\n", status.to_string());
                return 1;
            }

            // Another UPDATE
            status = tree.update("versioned_key", "v3");
            if (!status.ok()) {
                fmt::print("✗ Second UPDATE failed: {}\n", status.to_string());
                return 1;
            }

            tree.flush_wal();
            fmt::print("✓ Written: 1 PUT + 2 UPDATEs to WAL\n");
        }

        {
            // Recovery should replay correctly
            indexing::Btree tree("embrace.wal");
            auto status = tree.recover_from_wal();
            if (!status.ok()) {
                fmt::print("✗ Recovery failed: {}\n", status.to_string());
                return 1;
            }

            auto val = tree.get("versioned_key");
            if (val.has_value() && val.value() == "v3") {
                fmt::print("✓ Recovery replayed PUT+UPDATEs correctly: {}\n", val.value());
            } else {
                fmt::print("✗ Recovery produced wrong value: {}\n", val.value_or("MISSING"));
                return 1;
            }
        }
    }

    fmt::print("\n=== All CRUD tests passed! ===\n");
    return 0;
}
