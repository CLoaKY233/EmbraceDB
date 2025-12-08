# EmbraceDB

[![Status](https://img.shields.io/badge/status-alpha-orange)](https://github.com/cloaky233/EmbraceDB)
[![Language](https://img.shields.io/badge/C%2B%2B-23-blue)](https://en.cppreference.com/w/cpp/23)
[![Build](https://img.shields.io/badge/CI-coming--soon-lightgrey)](https://github.com/cloaky233/EmbraceDB/actions)
[![Platform](https://img.shields.io/badge/platform-linux%20%7C%20posix-brightgreen)](https://github.com/cloaky233/EmbraceDB)

A performant, embeddable key-value storage engine built for simplicity and speed. Embrace trades complexity for clarity—no legacy baggage, no over-engineering. Just a fast, durable B+Tree with WAL recovery and snapshot checkpointing.

## Why Embrace?

| What | RocksDB | Redis | Embrace | Why It Matters |
|------|---------|-------|---------|----------------|
| **Write Speed** | Fast (batched) | Fastest | Good | LSM batches writes; we don't |
| **Read Speed** | Slow (multiple levels) | Fastest (in-memory) | Fast (single lookup) | B+Tree = O(log N), LSM = O(levels × log N) |
| **Update Speed** | Slow (rewrite) | Fastest | Fast (in-place) | B+Tree updates in-place; LSM rewrites |
| **Write Amplification** | 5-10x | 1x | 1-2x | **Key advantage**: Less disk wear |
| **Durability** | Strong | Optional | Strong | We're persistent like RocksDB |
| **Complexity** | Very High | Low | Low | **Simple codebase**, easy to audit |
| **Embedding** | C++ lib | Separate process | C++23 lib | Zero-copy, same process |

**Embrace** is optimized for workloads with frequent updates and range queries, where write amplification matters. Built in modern C++23, it integrates seamlessly as an embedded library with zero external dependencies (besides `fmt` for logging).



## When to Use Embrace?

✅ **Use Embrace when**:
- Frequent updates to existing keys (in-place modification wins)
- Write amplification is a concern (SSD wear, cloud I/O costs)
- You want simple, auditable code
- Durability is non-negotiable
- Embedding in your process (zero serialization overhead)

❌ **Don't use Embrace when**:
- Pure sequential writes dominate (LSM batches better)
- Hot data fits in RAM (Redis is faster)
- You need distributed replication (TiKV/Consul)

## Features (v0.1.0 - Alpha)

### Currently Implemented
- ✅ **B+Tree Index**: Efficient insertion, deletion, and lookup with automatic rebalancing
- ✅ **Write-Ahead Log (WAL)**: CRC32-protected records for crash recovery
- ✅ **Snapshot Checkpointing**: Fast recovery with periodic snapshots
- ✅ **Structured Logging**: Async, non-blocking logger with file + console output
- ✅ **CRUD Operations**: `get()`, `put()`, `update()`, `delete()`
- ✅ **Status Codes**: Rich error handling with descriptive messages

### Roadmap
See [ROADMAP.md](./ROADMAP.md) for the complete development plan across 6 sprints.

## Quick Start

### Build

```bash
# Prerequisites
cmake --version  # >= 3.28
clang++ --version  # C++23 support

# Compile
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Run example
./build/embrace
```

### Usage

```cpp
#include "indexing/btree.hpp"
#include "log/logger.hpp"

int main() {
    // Initialize logging
    embrace::log::LogConfig config;
    config.level = embrace::log::Level::Info;
    config.console_output = true;
    config.file_path = "embrace.log";
    embrace::log::Logger::instance().init(config);

    // Create persistent B+Tree with WAL
    embrace::indexing::Btree db("embrace.wal");
    
    // Recover from previous crash
    auto status = db.recover_from_wal();
    if (!status.ok()) {
        return 1;  // Handle error
    }

    // Insert data
    db.put("user:1", R"({"name": "Alice", "age": 30})");
    db.put("user:2", R"({"name": "Bob", "age": 25})");

    // Retrieve data
    auto value = db.get("user:1");
    if (value) {
        std::cout << value.value() << std::endl;
    }

    // Update
    db.update("user:1", R"({"name": "Alice", "age": 31})");

    // Delete
    db.remove("user:2");

    // Persist to disk
    db.flush_wal();

    return 0;
}
```

## Architecture

```
┌─────────────────────────────────────┐
│     Client Application              │
└─────────────────┬───────────────────┘
                  │
┌─────────────────┴───────────────────┐
│     B+Tree Index (Memory)           │
│  • Insertion, Deletion, Rebalance   │
│  • Page-based node management       │
└─────────────────┬───────────────────┘
                  │
        ┌─────────┴──────────┐
        │                    │
   ┌────┴────────┐   ┌───────┴────────┐
   │ WAL Writer  │   │ Snapshotter    │
   │ (durability)│   │ (recovery)     │
   └─────────────┘   └────────────────┘
        │                    │
        └─────────┬──────────┘
                  │
        ┌─────────┴──────────┐
        │   Persistent Storage
        │  • .wal file       │
        │  • .snapshot file  │
        └────────────────────┘
```

## Design Principles

1. **Simplicity Over Features**: No multi-level compaction, no distributed consensus. Just a clean B+Tree.
2. **Performance by Default**: CRC32 checksums, efficient WAL buffering, snapshot-based recovery.
3. **Durability Tuning**: Checkpoint intervals are configurable; choose crash-safety vs. throughput.

## Configuration

```cpp
embrace::indexing::Btree db("data.wal");

// Configure checkpoint interval (operations between snapshots)
db.set_checkpoint_interval(10000);  // Every 10k ops

// Set log level
embrace::log::Logger::instance().set_level(embrace::log::Level::Debug);
```

## Performance Notes

- **Write Performance**: B+Trees have lower write amplification (1-2x) vs LSM trees (5-10x), ideal for update-heavy workloads
- **Read Performance**: In-memory tree traversal is extremely fast; snapshots enable quick recovery without scanning the entire WAL
- **Storage Overhead**: Minimal; snapshot CRC32 validation catches corruption early

### Benchmark Targets (vs RocksDB)

- Write throughput: >80%
- Read latency p99: <1ms
- Crash recovery: <5s for 10GB database
- Storage ratio: 1:1

## Testing

```bash
# Build + run tests (coming in Sprint 1)
cmake --build build --target test
```

## Development

### Project Structure

```
Embrace/
├── src/
│   ├── core/           # Common types, status codes
│   ├── indexing/       # B+Tree implementation
│   ├── storage/        # WAL, snapshots, checksums
│   ├── log/            # Async structured logging
│   └── main.cpp        # Example usage
├── CMakeLists.txt      # Build configuration
├── .clang-format       # Code style
├── .clang-tidy         # Linting rules
└── README.md
```

### Code Style

- **Formatter**: `clang-format` (LLVM style, 100 column limit)
- **Linter**: `clang-tidy` (enabled in CI)
- **C++ Standard**: C++23 (modules, ranges, span)

### Contributing

Embrace is currently a solo project. If you're interested in contributing, please open an issue first to discuss the feature or fix.

## Roadmap

Version 1.0 in ~3 weeks across 6 sprints:

1. **Sprint 1**: Testing & Stability (3 days)
2. **Sprint 2**: MVCC Concurrency (5 days)
3. **Sprint 3**: Range Queries & Iteration (2 days)
4. **Sprint 4**: Compression (3 days)
5. **Sprint 5**: Transactions & Bloom Filters (4 days)
6. **Sprint 6**: Production Readiness (3 days)

See [ROADMAP.md](./ROADMAP.md) for detailed breakdown by sprint.

## Known Limitations

- **Single-threaded** (MVCC concurrency in Sprint 2)
- **No range queries yet** (coming Sprint 3)
- **No compression** (coming Sprint 4)
- **Fixed B+Tree degree** (hardcoded to 4; configurable in future)
- **No distributed replication** (post-v1.0 consideration)

## License

MIT License - see LICENSE file

## Community

Questions or feedback? Open an issue on GitHub. I actively review and respond to all issues.

---

**Current Status**: Foundation complete. Building towards v1.0 with test infrastructure and concurrency support. All welcome to watch and provide feedback.
