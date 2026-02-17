# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Embrace is a C++23 embedded key-value storage engine built on a B+Tree index with WAL-based durability. It has zero external dependencies beyond `fmt` for logging. The library targets update-heavy workloads with 1-2x write amplification (vs 5-10x for LSM trees).

## Build Commands

Requires CMake 3.28+ and a C++23-capable compiler. Ninja is preferred.

```bash
# Release build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Debug build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# With sanitizers (ASAN, UBSan, LeakSan)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_SANITIZERS=ON
cmake --build build

# With code coverage
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON
cmake --build build

# Fuzzing (requires clang++)
cmake -B build_fuzz -G Ninja -DENABLE_FUZZING=ON -DCMAKE_CXX_COMPILER=clang++
cmake --build build_fuzz --target fuzz_wal_parser
```

## Testing

```bash
# Run all tests
./build/embrace_tests
ctest --test-dir build --output-on-failure

# Run a specific test suite
./build/embrace_tests --gtest_filter="BtreeBasicTest.*"
./build/embrace_tests --gtest_filter="WalRecoveryTest.*"
./build/embrace_tests --gtest_filter="StateMachinePropertyTest.*"

# Run with sanitizers
./scripts/run_sanitizers.sh

# Generate coverage report (75% line coverage required)
./scripts/coverage.sh
./scripts/coverage.sh open   # Open HTML report

# Run fuzzer
./build_fuzz/fuzz_wal_parser corpus/ -max_total_time=60
```

## Code Formatting

Uses `clang-format` (LLVM style, 100-column limit). Format checks run in CI on every PR.

```bash
cmake --build build --target format        # Apply formatting
cmake --build build --target format-check  # Check without modifying
./scripts/format.sh                        # Alternative: apply
./scripts/format.sh --check                # Alternative: check
```

## Architecture

```
Application (get/put/delete)
        │
   Embrace Engine
        │
   B+Tree Index (in-memory)
        │
   ┌────┴────┐
  WAL    Snapshotter
   └────┬────┘
     Disk (.wal, .snapshot)
```

### Key Components

**B+Tree** (`src/indexing/btree.hpp`, `btree.cpp`): Fixed degree-4 tree. Leaf nodes are doubly linked for future range queries. All operations are O(log N). Max 3 keys per node; min 2 (except root). Automatic splits and merges on insert/delete.

**WAL** (`src/storage/wal.hpp/cpp`): Write-ahead log using a 4KB buffer. Record format: `[Type:1B][KeyLen:4B][Key][ValLen:4B][Value][CRC32:4B]`. Types: Put, Delete, Update, Checkpoint. `WalWriter` handles writes; `WalReader` handles recovery replay.

**Snapshotter** (`src/storage/snapshot.hpp/cpp`): Full tree dumps triggered every N operations (default 10,000). Uses atomic rename (temp file → final). Recovery loads the latest snapshot then replays WAL from that checkpoint. Snapshot format: magic `0x454D4252` ("EMBR"), version, entry count, header CRC.

**Checksum** (`src/storage/checksum.hpp/cpp`): CRC32 (IEEE 802.3) applied to WAL records, snapshot headers, and snapshot entries. Constexpr lookup table for ~1GB/s throughput.

**Logger** (`src/log/logger.hpp/cpp`): Async structured logger with a background worker thread. Queue-based to avoid blocking the write path. Levels: Trace/Debug/Info/Warn/Error/Fatal.

**Core types** (`src/core/`): `status.hpp` defines `Status` (Ok, NotFound, Corruption, IOError, InvalidArgument). `common.hpp` defines constants: `PAGE_SIZE=4096`, `MAX_KEY_SIZE=128`, `MAX_VALUE_SIZE=1024`.

### Write Path
1. Log record to WAL buffer (flushed at 4KB or explicit flush)
2. Apply mutation to B+Tree in memory
3. Periodically write snapshot checkpoint

### Recovery Path
1. Load most recent `.snapshot` file into B+Tree
2. Replay `.wal` records from the checkpoint LSN forward
3. Discard records with invalid CRC32

### Test Infrastructure
`tests/test_utils.hpp` provides `BtreeTestFixture` (auto-creates/destroys `.wal` and `.snapshot` files) and helpers `generate_key()`, `generate_value()`, `generate_large_value()`.

Test categories: unit (btree operations, structure, edge cases), recovery (WAL + snapshot replay), property-based (state machine invariants, model equivalence), failure injection (corruption), and crash simulation/stress.

## Current Limitations (v0.1.0)

- Single-threaded — MVCC is planned
- No range query API yet (leaf linkage exists, iteration not exposed)
- Fixed B+Tree degree of 4
- No compression
