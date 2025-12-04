#include "indexing/btree.hpp"
#include <fmt/core.h>

auto main() -> int {
    using namespace ::embrace;

    fmt::print("=== Embrace Database - Sprint 1: WAL & Recovery ===\n\n");

    {
        fmt::print("--- Test 1: Fresh Start with WAL ---\n");
        indexing::Btree tree("embrace.wal");

        auto s = tree.put("apple", "red");
        if (!s.ok()) {
            fmt::print("ERROR: put(apple) failed: {}\n", s.to_string());
            return 1;
        }

        s = tree.put("banana", "yellow");
        if (!s.ok()) {
            fmt::print("ERROR: put(banana) failed: {}\n", s.to_string());
            return 1;
        }

        s = tree.put("cherry", "red");
        if (!s.ok()) {
            fmt::print("ERROR: put(cherry) failed: {}\n", s.to_string());
            return 1;
        }

        s = tree.put("date", "brown");
        if (!s.ok()) {
            fmt::print("ERROR: put(date) failed: {}\n", s.to_string());
            return 1;
        }

        s = tree.put("elderberry", "purple");
        if (!s.ok()) {
            fmt::print("ERROR: put(elderberry) failed: {}\n", s.to_string());
            return 1;
        }

        s = tree.put("fig", "green");
        if (!s.ok()) {
            fmt::print("ERROR: put(fig) failed: {}\n", s.to_string());
            return 1;
        }

        fmt::print("Tree after inserts:\n\n\n");
        tree.print_tree();

        auto val = tree.get("banana");
        fmt::print("\nRetrieved banana: {}\n", val.value_or("NOT FOUND"));

        tree.flush_wal();
        fmt::print("WAL flushed to disk.\n\n");
    }

    // Test 2: Recovery from WAL
    {
        fmt::print("--- Test 2: Recovery from WAL ---\n");
        indexing::Btree tree("embrace.wal");

        auto status = tree.recover_from_wal();
        if (!status.ok()) {
            fmt::print("ERROR: Recovery failed: {}\n", status.to_string());
            return 1;
        }

        fmt::print("\nTree after recovery:\n");
        tree.print_tree();

        auto val = tree.get("banana");
        fmt::print("\nRetrieved banana after recovery: {}\n", val.value_or("NOT FOUND"));

        auto s = tree.put("grape", "purple");
        if (!s.ok()) {
            fmt::print("ERROR: put(grape) failed: {}\n", s.to_string());
            return 1;
        }

        s = tree.put("honeydew", "green");
        if (!s.ok()) {
            fmt::print("ERROR: put(honeydew) failed: {}\n", s.to_string());
            return 1;
        }

        fmt::print("\nTree after additional inserts:\n");
        tree.print_tree();
    }

    fmt::print("\n=== All tests passed! ===\n");
    return 0;
}
