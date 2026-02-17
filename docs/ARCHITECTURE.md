# Embrace Architecture

## Overview

Embrace is a persistent key-value storage engine designed for simplicity and performance. It combines a B+Tree index for in-memory operations with a WAL (Write-Ahead Log) for durability and periodic snapshots for efficient crash recovery.

```
┌──────────────────────────────────────┐
│     Application                      │
│  (get, put, update, delete)          │
└──────────────┬───────────────────────┘
               │
┌──────────────┴───────────────────────┐
│         Embrace Engine               │
│  ┌──────────────────────────────┐    │
│  │  B+Tree Index (In-Memory)    │    │
│  │  • Leaf nodes linked         │    │
│  │  • Balanced insertion/delete │    │
│  └─────────────┬────────────────┘    │
└────────────────┼────────────────────┘
                 │
        ┌────────┴─────────┐
        │                  │
   ┌────▼────┐      ┌──────▼──────┐
   │ WAL      │      │ Snapshotter  │
   │ Writer   │      │              │
   └────┬────┘      └──────┬───────┘
        │                  │
        └────────┬─────────┘
                 │
        ┌────────▼────────┐
        │ Persistent Disk │
        │ • .wal file     │
        │ • .snapshot     │
        └─────────────────┘
```

---

## Core Components

### 1. B+Tree Index

**File**: `src/indexing/btree.hpp`, `src/indexing/btree.cpp`

The primary data structure. All key-value pairs are stored in a B+Tree with automatic balancing.

#### Structure
- **Internal Nodes**: Contain keys (separators) and child pointers
- **Leaf Nodes**: Contain key-value pairs; doubly linked for range queries
- **Degree**: Currently fixed at 4 (B+Tree order = 4)
  - Max keys per node: 3
  - Min keys per node: 2 (except root)

#### Operations

**Get**
```cpp
auto value = tree.get(key);  // Returns std::optional<Value>
```
- Traverses from root to leaf via binary search on keys
- O(log N) time complexity
- No disk I/O (all in-memory)

**Put**
```cpp
auto status = tree.put(key, value);  // Insert or update
```
1. Find appropriate leaf via traversal
2. If key exists → update value
3. If key new → insert into leaf (maintaining sort order)
4. If leaf exceeds max keys → split leaf
5. Write to WAL for durability

**Delete**
```cpp
auto status = tree.remove(key);  // Delete key
```
1. Find leaf containing key
2. Remove key-value pair
3. If leaf underflows → rebalance (borrow or merge)
4. Write to WAL

#### Rebalancing

**Splits**: When a node exceeds max degree
- Divide keys into two halves
- Promote middle key to parent
- Create new sibling node
- Recursively split parent if needed

**Merges**: When a node underflows (during deletion)
- Check siblings for borrowing opportunity
- If no sibling has spare keys → merge with neighbor
- Recursively rebalance parent

#### Leaf Linkage

Leaf nodes maintain `prev` and `next` pointers for efficient range iteration:
```cpp
// Scan from key1 to key2
auto current = find_leaf(key1);
while (current && current->keys[0] <= key2) {
    for (auto& k : current->keys) { /* process */ }
    current = current->next;
}
```

### 2. Write-Ahead Log (WAL)

**File**: `src/storage/wal.hpp`, `src/storage/wal.cpp`

Ensures durability by logging every operation to disk *before* applying to memory.

#### Format

Each WAL record:
```
[Type:1B] [KeyLen:4B] [Key:?B] [ValLen:4B] [Value:?B] [CRC32:4B]
```

**Types**:
- `Put` (1): Insert or update
- `Delete` (2): Delete key
- `Update` (3): Update existing key
- `Checkpoint` (4): Marker for snapshot completion

#### Recovery Process

On startup, if `.wal` and `.snapshot` files exist:

1. **Load latest snapshot** → populate B+Tree (fast path)
2. **Scan WAL from start** → replay all operations
3. **Stop at Checkpoint marker** → truncate WAL
4. **Verify via CRC32** → detect corruption early

This hybrid approach is faster than replaying entire WAL from scratch.

#### Buffering

Writes are buffered (4KB default) and flushed in batches to reduce syscall overhead.

```cpp
// Internal buffer
std::vector<char> buffer_;  // 4KB

// On put/delete
write_record(record);  // Appends to buffer_

// When full or on flush()
write(fd_, buffer_.data(), buffer_.size());
```

### 3. Snapshots

**File**: `src/storage/snapshot.hpp`, `src/storage/snapshot.cpp`

Periodic checkpoints of the entire tree state for fast recovery.

#### Snapshot File Format

```
[Magic:4B][Version:4B][EntryCount:4B][HeaderCRC:4B]
[Entry1 ...]
[Entry N: KeyLen:4B][Key][ValLen:4B][Value][CRC32:4B]]
```

**Magic**: `0x454D4252` (ASCII: "EMBR")  
**Version**: 1 (current)

#### Checkpointing

Triggered automatically after N operations (configurable):

```cpp
tree.set_checkpoint_interval(10000);  // Every 10k ops
```

Checkpoint process:
1. **Iterate entire tree** (via B+Tree leaf linkage)
2. **Write to temp file** with full CRC protection
3. **Atomic rename** → atomicity guarantee
4. **Truncate WAL** → cleanup

#### Recovery Speed

- **No snapshot**: Replay entire WAL → slow
- **With snapshot**: Load snapshot (fast) + replay recent WAL → fast
- Typical recovery for 10GB: <5 seconds

### 4. Checksums

**File**: `src/storage/checksum.hpp`, `src/storage/checksum.cpp`

CRC32 validation ensures data integrity.

#### Implementation

Standard CRC32 polynomial: `0x04C11DB7` (IEEE 802.3)

```cpp
auto crc = compute_crc32(data, len);
```

Applied to:
- Every WAL record (detects write corruption)
- Snapshot header (version/magic validation)
- Snapshot entries (detects bit flips)

#### Performance

Constexpr CRC32 lookup table (256 entries) → ~1GB/s throughput

### 5. Structured Logging

**File**: `src/log/logger.hpp`, `src/log/logger.cpp`

Async, non-blocking logger for observability.

#### Features

- **Async worker thread**: Logs don't block application
- **Log levels**: Trace, Debug, Info, Warn, Error, Fatal
- **Dual output**: Console + file simultaneously
- **Colored output**: Info (green), Warn (yellow), Error (red)

#### Usage

```cpp
#include "log/logger.hpp"

// Initialize once
embrace::log::LogConfig config;
config.level = embrace::log::Level::Info;
config.console_output = true;
config.file_path = "embrace.log";
embrace::log::Logger::instance().init(config);

// Log from anywhere
LOG_INFO("Starting database recovery");
LOG_ERROR("Operation failed: {}", status.to_string());

// Shutdown on exit
embrace::log::Logger::instance().shutdown();
```

#### Implementation

Queue-based design:
```cpp
std::deque<LogEntry> queue_;  // Thread-safe queue
std::thread worker_thread;     // Background writer
std::condition_variable cv;    // Notify on new entry
```

Worker thread writes in batches to minimize I/O.

---

## Data Flow

### Write Path (Put/Update/Delete)

```
1. Application calls tree.put(key, value)
   │
2. WAL Writer → Write record to buffer
   │         → If buffer full, flush to disk + fsync
   │
3. B+Tree → Find leaf, insert/update value
   │      → Rebalance if needed
   │
4. Periodic → If operation_count % interval == 0
   │       → Create snapshot (full tree dump)
   │       → Truncate WAL
   │
5. Return Status::Ok() to application
```

### Read Path (Get)

```
1. Application calls tree.get(key)
   │
2. B+Tree → Traverse root to leaf (binary search)
   │      → O(log N) comparisons
   │
3. Leaf node → Binary search for key
   │         → Return value if found
   │
4. Return std::optional<Value> to application
```

### Recovery Path (On Startup)

```
1. Check if .snapshot file exists
   │
2. If snapshot exists
   │  → Load snapshot → populate tree (fast)
   │  → Parse WAL from beginning
   │
3. Replay WAL records
   │  → tree.put() each Put/Update record
   │  → tree.delete() each Delete record
   │  → Stop at Checkpoint marker (if found)
   │
4. Return recovered tree to application
```

---

## Memory Layout

### Node Structure (Leaf)

```cpp
struct LeafNode {
    std::vector<Key> keys;        // Sorted keys
    std::vector<Value> values;    // Corresponding values
    LeafNode* next;               // Right sibling
    LeafNode* prev;               // Left sibling
    Node* parent;                 // Parent pointer
};
```

**Typical size**: 32 keys × ~16 bytes (key+value pointers) ≈ 512 bytes per leaf

### Node Structure (Internal)

```cpp
struct InternalNode {
    std::vector<Key> keys;        // Separator keys
    std::vector<unique_ptr<Node>> children;  // Child pointers
    Node* parent;
};
```

---

## Performance Characteristics

### Time Complexity

| Operation | Complexity | Notes |
|-----------|-----------|-------|
| Get | O(log N) | Binary search on keys |
| Put | O(log N) | Usually, O(N) worst case if tree rebalance |
| Delete | O(log N) | May trigger rebalancing |
| Range scan | O(k + log N) | k = result size; leaf linkage |
| Checkpoint | O(N) | Full tree iteration |

### Write Amplification

B+Tree: **1-2x** write amplification
- Each operation writes 1 WAL record
- Optional snapshot (infrequent)

vs LSM-Tree: **5-10x** amplification (multi-level compaction)

### Memory Overhead

- B+Tree nodes: ~512 bytes each (degree 4)
- WAL buffer: 4KB
- Snapshot temp: negligible (streaming write)

For 1M keys: ~16MB tree + overhead ≈ ~20MB resident set

---

## Thread Safety (Current v0.1.0)

**Current**: Single-threaded (no synchronization)

**v1.0 Plan** (Sprint 2):
- MVCC (Multi-Version Concurrency Control)
- Per-leaf `std::shared_mutex` for read-heavy workloads
- Lock-free reads via RCU-style snapshots

---

## Configuration

### Compile-Time Constants

```cpp
// src/core/common.hpp
constexpr uint32_t PAGE_SIZE = 4096;        // For future use
constexpr uint32_t MAX_KEY_SIZE = 128;      // Key limit
constexpr uint32_t MAX_VALUE_SIZE = 1024;   // Value limit
```

### Runtime Configuration

```cpp
tree.set_checkpoint_interval(10000);  // Ops between snapshots
logger.set_level(log::Level::Debug);  // Log verbosity
```

**Future** (v1.0):
- YAML config files
- B+Tree degree tuning
- Compression policy

---

## Error Handling

### Status Codes

```cpp
enum class StatusCode {
    Ok = 0,
    NotFound = 1,
    Corruption = 2,
    NotSupported = 3,
    InvalidArgument = 4,
    IOError = 5
};
```

### Recovery from Errors

Most errors are transient:
- **IOError** → Retry after a brief delay
- **Corruption** → Log and skip corrupted snapshot; use WAL
- **NotFound** → Return nullptr (expected for missing keys)

---

## Future Enhancements

### MVCC (Sprint 2)
- Versioned values in leaf nodes
- Transaction IDs for snapshot isolation
- Garbage collection of old versions

### Compression (Sprint 4)
- Prefix encoding for keys
- LZ4 for values
- Delta snapshots

### Advanced (Post-v1.0)
- Column families
- Secondary indexes
- Distributed replication (Raft)

---

## Debugging

### Print Tree Structure

```cpp
tree.print_tree();  // Logs B+Tree layout (DEBUG level)
```

Output:
```
B+tree structure:
[ 1 2 ] [ 3 4 ] [ 5 ]
[ 1 ] [ 2 ] [ 3 ] [ 4 ] [ 5 ]
```

### Log Levels for Debugging

```cpp
log::Logger::instance().set_level(log::Level::Trace);
```

Logs per-operation details (verbose; production-only if needed)

---

## References

- B+Tree algorithms: [Cormen et al., "Introduction to Algorithms"](https://mitpress.mit.edu/9780262033848/)
- WAL patterns: [SQLite Documentation](https://www.sqlite.org/wal.html)
- CRC32: [IEEE 802.3](https://en.wikipedia.org/wiki/Cyclic_redundancy_check)

