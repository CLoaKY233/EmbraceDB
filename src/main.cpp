#include "indexing/btree.hpp"
#include <fmt/core.h>

auto main() -> int {
    using namespace ::embrace;

    fmt::print("Initializing Embrace (Sprint 0)\n");

    indexing::Btree tree;

    // Test1: Basic Insertion
    fmt::print("--- Inserting Keys ---\n");

    auto status = tree.put("apple", "red");
    if (!status.ok()) {
        fmt::print("ERROR: Failed to insert apple: {}\n", status.to_string());
        return 1;
    }

    status = tree.put("banana", "yellow");
    if (!status.ok()) {
        fmt::print("ERROR: Failed to insert banana: {}\n", status.to_string());
        return 1;
    }

    status = tree.put("cherry", "red");
    if (!status.ok()) {
        fmt::print("ERROR: Failed to insert cherry: {}\n", status.to_string());
        return 1;
    }

    // Force a split (max_degree is 4)
    status = tree.put("date", "brown");
    if (!status.ok()) {
        fmt::print("ERROR: Failed to insert date: {}\n", status.to_string());
        return 1;
    }

    status = tree.put("elderberry", "purple");
    if (!status.ok()) {
        fmt::print("ERROR: Failed to insert elderberry: {}\n", status.to_string());
        return 1;
    }

    status = tree.put("fig", "green");
    if (!status.ok()) {
        fmt::print("ERROR: Failed to insert fig: {}\n", status.to_string());
        return 1;
    }

    tree.print_tree();

    // Test 2: Retrieval
    fmt::print("\n--- Retrieving Keys ---\n");
    auto val = tree.get("banana");
    if (val) {
        fmt::print("Found banana: {}\n", *val);
    } else {
        fmt::print("Banana NOT found!\n");
    }

    // Test 3: Not Found
    auto missing = tree.get("zucchini");
    if (!missing) {
        fmt::print("Zucchini correctly not found.\n");
    }

    return 0;
}
