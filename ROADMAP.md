# Embrace Database Engine - Development Roadmap

**Target**: v1.0 Release in ~3 weeks | **Current**: v0.1.0-alpha (Foundation)

## Overview

This roadmap tracks Embrace's development path from foundation to production-ready release. Each sprint has clear deliverables and success criteria. We prioritize **shipping** over perfection—focus is on core functionality that delivers real value.

---

## Sprint 0: Foundation (COMPLETED)

**Duration**: 1 day | **Status**: ✅ Done

### Deliverables
- [x] B+Tree implementation (insert, split, delete, rebalance)
- [x] WAL with CRC32 integrity checks
- [x] Snapshot-based checkpointing
- [x] Structured async logging system
- [x] Basic CRUD operations (Get, Put, Update, Delete)
- [x] Status/error handling codes
- [x] CMake build system with C++23 support

### What We Have
- Functional B+Tree for persistent storage
- Crash recovery via WAL + snapshots
- Example test program demonstrating checkpointing

### Known Gaps
- No tests or benchmarks
- No concurrency (single-threaded)
- No transactions or MVCC
- No compression or range queries
- Fixed B+Tree degree (no configurability)

---

## Sprint 1: Correctness & Stability

**Duration**: 3 days | **Target**: End of Week 1 | **Status**: 🚧 Planned

### Goal
Ensure the current implementation is production-ready with comprehensive testing and baseline performance measurements.

### Tasks

#### 1.1 Test Infrastructure
- [ ] Google Test integration (CMake setup)
- [ ] Unit test suite for B+Tree operations
  - [ ] Insert/update/delete correctness
  - [ ] Rebalancing under various patterns
  - [ ] Edge cases: empty tree, single keys, large values
  - [ ] Boundary conditions (min/max keys)
- [ ] Property-based tests for WAL recovery
  - [ ] Random operation sequences
  - [ ] Crash simulation (kill mid-write)
  - [ ] Verify state matches pre-crash snapshot
- [ ] Fuzzing setup (libFuzzer or AFL++)
  - [ ] WAL record fuzzing
  - [ ] Snapshot corruption injection

#### 1.2 Stress Testing
- [ ] Random insert/delete workload (1M+ operations)
- [ ] Large value stress test (up to MAX_VALUE_SIZE)
- [ ] Concurrent reader/writer patterns (preparation for Sprint 2)
- [ ] Memory leak detection (Valgrind/ASan)

#### 1.3 Crash Recovery Validation
- [ ] Test scenarios:
  - [ ] Crash during WAL write
  - [ ] Crash during snapshot creation
  - [ ] Missing snapshot (WAL-only recovery)
  - [ ] Corrupted snapshot (skip, use WAL)
  - [ ] Interleaved snapshots and WAL
- [ ] Verify data consistency after each crash scenario

#### 1.4 Performance Baseline
- [ ] Google Benchmark integration
- [ ] Measure:
  - [ ] Insert throughput (ops/sec)
  - [ ] Read throughput (ops/sec)
  - [ ] Update/delete throughput
  - [ ] Latency percentiles (p50, p99, p99.9)
  - [ ] WAL flush overhead
  - [ ] Snapshot creation time
  - [ ] Recovery time (cold start)
- [ ] Compare against:
  - [ ] RocksDB (same workload)
  - [ ] LevelDB (baseline)
- [ ] Document in `BENCHMARKS.md`

### Deliverables
- Test suite with >90% code coverage
- Reproducible benchmark suite (CI-ready)
- Baseline performance documentation
- Zero memory leaks (verified by ASan/Valgrind)

### Success Criteria
- [ ] All tests pass
- [ ] No data corruption in crash recovery tests
- [ ] Write throughput within 80-90% of RocksDB
- [ ] Coverage report in CI

---

## Sprint 2: Concurrency (MVCC)

**Duration**: 5 days | **Target**: End of Week 2 | **Status**: 🚧 Planned

### Goal
Enable multi-threaded reads and writes with snapshot isolation via MVCC, achieving 10x read throughput.

### Architecture Design

**Multi-Version Concurrency Control**:
- Each write gets a monotonic `TransactionId`
- Leaf nodes store versioned values: `std::vector<std::pair<TransactionId, Value>>`
- Readers see snapshot at their start timestamp
- Writers append new versions without blocking readers

**Latch Strategy**:
- RCU-style pointer updates for tree structure changes
- `std::shared_mutex` per leaf for concurrent reads

### Tasks

#### 2.1 Version Management
- [ ] Refactor `LeafNode` storage
  - [ ] Change from `std::vector<Value>` to `std::vector<std::vector<VersionedValue>>`
  - [ ] Implement version lookup with binary search
- [ ] Transaction ID generator (atomic counter)
- [ ] Garbage collection for old versions
  - [ ] Mark-and-sweep approach
  - [ ] Configurable retention policy

#### 2.2 Transaction Manager
- [ ] `Transaction` class with `begin()`, `commit()`, `abort()`
- [ ] Read snapshots (snapshot ID assignment)
- [ ] Write buffering (batch commits)
- [ ] WAL integration: write txn begin/end markers
- [ ] Conflict detection (optimistic locking)

#### 2.3 Synchronization
- [ ] `std::shared_mutex` for leaf nodes
- [ ] RCU-based tree restructuring (during splits/merges)
- [ ] Atomic pointer swaps for root updates
- [ ] No locks on critical path for reads

#### 2.4 Testing
- [ ] Concurrent read/write stress tests
  - [ ] 100+ concurrent readers
  - [ ] 4 concurrent writers
  - [ ] 10s+ sustained load
- [ ] Isolation verification
  - [ ] No phantom reads
  - [ ] No dirty reads
  - [ ] Snapshot consistency

#### 2.5 Documentation
- [ ] MVCC algorithm explanation
- [ ] Concurrency guarantees document
- [ ] Example: multi-threaded usage

### Deliverables
- Thread-safe B+Tree with snapshot isolation
- MVCC implementation with garbage collection
- Concurrent read/write benchmarks
- Architecture document

### Success Criteria
- [ ] 10x read throughput improvement
- [ ] Zero race conditions (thread sanitizer passes)
- [ ] Snapshot isolation verified
- [ ] Documentation complete

---

## Sprint 3: Range Queries & Iteration

**Duration**: 2 days | **Target**: Mid Week 2 | **Status**: 🚧 Planned

### Goal
Support efficient scanning and range queries by exploiting B+Tree leaf linkage.

### Tasks

#### 3.1 Range Query API
- [ ] `auto scan(const Key& start, const Key& end) -> std::vector<std::pair<Key, Value>>`
- [ ] `auto prefix_scan(const Key& prefix) -> std::vector<std::pair<Key, Value>>`
- [ ] Lazy evaluation (iterator-based) for large result sets

#### 3.2 Iterator Implementation
```cpp
class Iterator {
  auto seek(const Key& key) -> void;    // Position at key
  auto next() -> bool;                  // Advance to next entry
  auto key() -> const Key&;
  auto value() -> const Value&;
};
```
- [ ] Forward iteration
- [ ] Backward iteration (bonus)
- [ ] Snapshot consistency within iterator lifetime

#### 3.3 Optimizations
- [ ] Exploit B+Tree leaf chain (prev/next pointers)
- [ ] Prefetch next leaf during iteration
- [ ] SIMD-friendly key comparison
- [ ] Cache-aligned iteration

#### 3.4 Benchmarks
- [ ] Range query throughput vs RocksDB
- [ ] Latency for variable result sizes
- [ ] Iterator overhead

### Deliverables
- Range query API with documentation
- Iterator pattern for lazy evaluation
- Benchmark comparison vs RocksDB

### Success Criteria
- [ ] API complete and documented
- [ ] Performance within 90% of RocksDB for range queries
- [ ] Iterator overhead minimal (<5%)

---

## Sprint 4: Compression & Space Efficiency

**Duration**: 3 days | **Target**: Week 3 | **Status**: 🚧 Planned

### Goal
Reduce storage footprint by 60%+ without sacrificing read performance.

### Strategy
- **Dictionary compression** for keys (exploit prefix commonality)
- **LZ4** for values (3-4x decompression speed)
- **Delta snapshots** (store only changes since last snapshot)

### Tasks

#### 4.1 Key Compression
- [ ] Prefix encoding in leaf nodes
  - [ ] Store only suffix for consecutive keys
  - [ ] Dictionary of common prefixes
- [ ] On-the-fly decompression during lookups
- [ ] Maintain sorted order for binary search

#### 4.2 Value Compression
- [ ] LZ4 integration (header-only library)
- [ ] Compression policy:
  - [ ] Compress values >256 bytes (configurable threshold)
  - [ ] Skip compression for already-compressed data (magic byte check)
- [ ] WAL format extension:
  - [ ] Add compression flag per value
  - [ ] Decompress on recovery
- [ ] Update snapshot format:
  - [ ] Store compressed data directly
  - [ ] Validate decompression during load

#### 4.3 Snapshot Optimization
- [ ] Delta snapshots:
  - [ ] Full snapshots every N checkpoints
  - [ ] Delta snapshots reference previous full snapshot
  - [ ] On recovery, replay full + deltas
- [ ] Per-snapshot CRC (not per-entry) for speed
- [ ] Compression ratio measurement

#### 4.4 Testing
- [ ] Round-trip tests (compress → decompress → verify)
- [ ] Compression ratio benchmarks
- [ ] Recovery time with delta snapshots
- [ ] Compatibility with MVCC from Sprint 2

### Deliverables
- Compression subsystem (pluggable policy)
- Delta snapshot mechanism
- Compression benchmarks

### Success Criteria
- [ ] 3:1 compression ratio for typical workloads
- [ ] Decompression throughput >2GB/s
- [ ] Recovery time increase <5%

---

## Sprint 5: Advanced Features

**Duration**: 4 days | **Target**: Week 3 | **Status**: 🚧 Planned

### Goal
Match enterprise-grade KV store capabilities.

### Tasks

#### 5.1 ACID Transactions
- [ ] Batch write API:
  ```cpp
  auto batch = db.create_batch();
  batch.put("key1", "val1");
  batch.put("key2", "val2");
  batch.commit();  // Atomic
  ```
- [ ] Rollback on failure (undo log)
- [ ] Serializable isolation (optimistic locking)
- [ ] Conflict detection

#### 5.2 Bloom Filters
- [ ] Per-snapshot bloom filter
  - [ ] 1% false positive rate target
  - [ ] Embedded in snapshot file
- [ ] Negative lookups (skip disk reads for missing keys)
- [ ] Benchmark: false lookup reduction (target 90%)

#### 5.3 Write Buffer (Memtable)
- [ ] In-memory skiplist for recent writes
- [ ] Flush to B+Tree when threshold reached
- [ ] Reduces tree mutations during high write load
- [ ] Configuration: buffer size, flush interval

#### 5.4 Compaction
- [ ] Background compaction thread
  - [ ] Merge old snapshots
  - [ ] Reclaim space from deleted keys
  - [ ] Tunable frequency
- [ ] Space reclamation benchmarks

#### 5.5 Testing & Benchmarks
- [ ] Transaction isolation verification
- [ ] Bloom filter false positive rate
- [ ] Write buffer impact on tree stability
- [ ] Compaction overhead

### Deliverables
- Transaction API with ACID guarantees
- Bloom filters integrated
- Write buffer implementation
- Compaction mechanism

### Success Criteria
- [ ] 100% transaction consistency in tests
- [ ] Bloom filter FP rate <1%
- [ ] Write buffer reduces tree mutations by 50%+

---

## Sprint 6: Production Readiness

**Duration**: 3 days | **Target**: End of Week 3 | **Status**: 🚧 Planned

### Goal
Ship-quality reliability, observability, and documentation.

### Tasks

#### 6.1 Metrics & Monitoring
- [ ] Prometheus-style metrics:
  - [ ] `embrace_ops_total` (counter)
  - [ ] `embrace_latency_ms` (histogram)
  - [ ] `embrace_cache_hit_rate` (gauge)
  - [ ] `embrace_wal_size_bytes` (gauge)
- [ ] Metrics export:
  - [ ] HTTP endpoint (`/metrics`)
  - [ ] File-based export (JSON/Prometheus text)
- [ ] Internal statistics (ops/sec, memory usage)

#### 6.2 Configuration System
- [ ] YAML/TOML config file support:
  ```yaml
  embrace:
    btree_degree: 4
    checkpoint_interval: 10000
    compression: true
    bloom_filter_enabled: true
  ```
- [ ] Runtime tuning (without recompilation)
- [ ] Validation and defaults

#### 6.3 Documentation
- [ ] API reference (Doxygen)
  - [ ] All public classes/functions
  - [ ] Code examples inline
- [ ] Architecture guide
  - [ ] Design decisions
  - [ ] Data structure diagrams
  - [ ] Recovery mechanism explanation
- [ ] Performance tuning guide
  - [ ] Configuration recommendations
  - [ ] Workload profiling
  - [ ] Benchmarking methodology

#### 6.4 Packaging & Distribution
- [ ] CMake install target
  - [ ] Headers to `include/embrace/`
  - [ ] Docs to `share/embrace/`
- [ ] Conan/vcpkg recipe
- [ ] GitHub release with artifacts
- [ ] Build badge in README (working CI)

#### 6.5 CI/CD Setup
- [ ] GitHub Actions workflow
  - [ ] Build (Release + Debug)
  - [ ] Test (unit + integration)
  - [ ] Sanitizers (ASan, UBSan, TSan)
  - [ ] Benchmarks (tracked over time)
  - [ ] Code coverage report
- [ ] Pre-commit hooks (clang-format, clang-tidy)

#### 6.6 Final QA
- [ ] End-to-end integration test
- [ ] Longevity test (48h+ sustained load)
- [ ] Upgrade/downgrade safety
- [ ] Documentation review

### Deliverables
- v1.0.0 release candidate
- Full documentation (API + architecture + tuning)
- Working CI/CD pipeline
- Metrics & monitoring infrastructure
- Packaging (CMake install, Conan)

### Success Criteria
- [ ] All tests passing (CI green)
- [ ] Documentation complete (>95% API covered)
- [ ] Benchmarks stable (day-to-day variance <5%)
- [ ] Ready for production use

---
<!--
## Beyond v1.0

### v1.1 Candidates
- **SIMD Optimizations**: AVX-512 for key comparisons, CRC32
- **io_uring**: Async I/O for 2x+ throughput
- **Distributed Replication**: Raft consensus (TiKV-style)
- **Column Families**: Separate namespaces (RocksDB-style)

### v2.0+ Vision
- **SQL Layer**: Query engine on top of KV store
- **Secondary Indexes**: Full-text search, geospatial
- **Time-series Support**: TTL, retention policies
- **Embedded Analytics**: Aggregations, grouping-->

---

## Tracking

### Definition of Done (per sprint)
- [ ] All planned tasks completed
- [ ] Tests written and passing
- [ ] Documentation updated
- [ ] Code review (self-review for solo dev)
- [ ] Benchmark suite runs without regression

### Metrics
- **Code Coverage**: Target >85%
- **Benchmark Stability**: p99 latency variance <10%
- **Test Pass Rate**: 100% required
- **Memory Leaks**: Zero (verified by ASan)

---


**Last Updated**: December 2025 | **Version**: 0.1.0-alpha


