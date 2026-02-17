# Embrace — Codebase Reference

## Architecture (Quick View)

```
Application (get/put/delete)
        │
   Embrace Engine
        │
   B+Tree Index (in-memory, degree-4)
        │
   ┌────┴────┐
  WAL    Snapshotter
   └────┬────┘
     Disk (.wal, .snapshot)
```

**Key facts:**
- Single-threaded, no MVCC yet
- WAL buffer: 4KB, flushed on full or explicit `flush_wal()`
- Snapshots triggered every N ops (default 10,000), written atomically via temp-file rename
- Recovery: load latest `.snapshot` → replay `.wal` from checkpoint LSN
- CRC32 on every WAL record and every snapshot entry

---

## .zed Tasks

Open via `Ctrl+Shift+R` in Zed.

| Task | Command | Notes |
|---|---|---|
| **Build (Debug)** | `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build` | Full symbols, no optimization. Use while developing. |
| **Build (Release)** | Same with `Release` | Optimized. Use before benchmarking. |
| **Run Database** | `./build/embrace` | Launches the interactive DB binary. Opens a new terminal. |
| **Test (All)** | `./build/embrace_tests` | Runs all 76 tests. |
| **Test (Filter)** | `./build/embrace_tests --gtest_filter="$ZED_SYMBOL*"` | Runs the suite under your cursor. Position cursor on e.g. `BtreeBasicTest` then invoke. |
| **Sanitizers** | `./scripts/run_sanitizers.sh` | Builds with ASAN + UBSan + LeakSan, runs all tests, parses output for violations. |
| **Coverage** | `./scripts/coverage.sh` | Builds with `--coverage`, runs tests, checks ≥85% line coverage. |
| **Coverage (Open)** | `./scripts/coverage.sh open` | Same + opens HTML report in browser. Hides terminal on success. |
| **Format Code** | `cmake --build build --target format` | Applies `clang-format` (LLVM style, 100-col) to `src/` and `tests/`. |
| **Clean** | `rm -rf build build_asan build_coverage build_fuzz coverage_report` | Wipes all build artifacts. |

---

## Test Suites (76 tests total)

### `BtreeBasicTest` — 17 tests — `tests/test_btree_basic.cpp`
Core CRUD on the B+Tree. First line of defence.

| Group | What's tested |
|---|---|
| Insert | Single key, 100 sequential keys, duplicate key overwrites, empty key, empty value, large value (1000 bytes) |
| Get | Non-existent → nullopt, empty tree, after multiple inserts |
| Update | Existing key, non-existent → NotFound, multiple keys |
| Delete | Existing key, non-existent → NotFound, empty tree, delete + reinsert, multiple keys |

---

### `BtreeEdgeCasesTest` — 13 tests — `tests/test_btree_edge_cases.cpp`
Boundary conditions and unusual inputs.

- Max key size (128 bytes), max value size (1024 bytes)
- Sort order with min/max key values
- Special characters and null bytes in values
- Alternating insert/delete, reverse-order insertion, identical-prefix keys
- Iteration: empty tree, single element, sorted order guarantee

---

### `BtreeStructureTest` — 11 tests — `tests/test_btree_structure.cpp`
Verifies the B+Tree's internal structural invariants under pressure.

| Group | What's tested |
|---|---|
| Splits | Leaf overflow → leaf split; many inserts → internal node split; root split creating new root level |
| Rebalancing | Borrow from left sibling; borrow from right; merge with left; merge with right; internal node underflow |
| Root lifecycle | Stays single leaf when small; collapses after bulk deletion; empties fully |

---

### `WalRecoveryTest` — 11 tests — `tests/test_wal_recovery.cpp`
End-to-end WAL + snapshot recovery. The engine's durability contract.

- Recover 1 op, 100 ops, ops with deletions, ops with updates
- Recover from **snapshot only** (no WAL tail)
- Recover from **snapshot + WAL tail** (normal production path)
- Missing WAL → succeed gracefully (empty DB)
- Missing snapshot → fall back to WAL-only recovery
- Crash mid-buffer (no flush) → no hang, no assert-fail
- Multiple recovery cycles: recover → add data → recover again

---

### `WalRecoveryPropertyTest` — 7 tests — `tests/test_wal_recovery_property.cpp`
Property-based style. Runs a `StateTracker` model alongside the real DB, compares after recovery.

| Test | What it checks |
|---|---|
| `RandomOperationSequences_Small` | 100 random ops (seed 12345): model vs DB match after recovery |
| `RandomOperationSequences_Large` | 5000 ops with periodic checkpoints: full model equivalence |
| `CrashDuringWrite_EarlyStage` | `fork()` child crashes at op 20, parent recovers, verifies first 20 ops |
| `CrashDuringCheckpoint` | Crash mid-checkpoint; recovery handles incomplete checkpoint gracefully |
| `StateConsistency_MultipleRecoveries` | Recover 3× from same files → state identical every time |
| `Property_Durability` | Flushed data survives close + reopen |
| `Property_Atomicity` | Partial (unflushed) writes don't corrupt state |
| `Property_Consistency_NoDuplicates` | 10 puts on same key → exactly 1 key, last value wins |

Crash simulation uses real `fork()` + `_exit(137)` — actual process death, not simulated.

---

### `StateMachinePropertyTest` — 8 tests — `tests/test_state_machine_properties.cpp`
Model-equivalence testing. A `StateMachine` drives both a `std::map` (reference model) and the real `Btree` with the same random Put/Update/Delete commands, then asserts they match.

| Test | What it checks |
|---|---|
| `ModelEquivalence` | 5 seeds × 200 ops: model and DB always agree after recovery |
| `LastWriteWins` | 100 puts on same key → only last value survives |
| `DeleteRemoves` | Put then delete → key absent |
| `RecoveryPreservesState` | 50 puts, flush, reopen → all present |
| `CheckpointEquivalence` | 100 puts + checkpoint + 50 more → all 150 survive recovery |
| `UpdateOnlyAffectsExisting` | Update non-existent → fails; update existing → works |
| `RecoveryIdempotent` | Recover 3 times from same WAL → result identical each round |
| `OperationOrderMatters` | put/put/delete/put sequence → final state is the last put |

---

### `FailureInjectionTest` — 16 tests — `tests/test_failure_injection.cpp`
Adversarial. Directly corrupts or truncates files on disk, then asserts the engine handles it correctly.

| Test | Failure injected | Expected outcome |
|---|---|---|
| `CorruptedWalCrcMismatch` | XOR a byte in the middle of WAL | CRC or Corruption error |
| `TruncatedWalPartialRecord` | Remove last 5 bytes from WAL | Error (partial record) |
| `EmptyWalFile` | Zero-byte WAL file | Ok (empty DB, no crash) |
| `CorruptedSnapshotMagic` | XOR first byte of snapshot | Magic or Corruption error |
| `CorruptedSnapshotEntryCrc` | XOR last 10 bytes of snapshot | Corruption error |
| `TruncatedSnapshot` | Cut snapshot in half | Corruption error |
| `LargeValuesNearLimit` | Values at 1023 and 1024 bytes | Round-trip correctly |
| `LargeKeysNearLimit` | Keys at 127 and 128 bytes | Round-trip correctly |
| `EmptyValues` | Empty string as value | Survives WAL round-trip |
| `InterleavedSnapshotAndWal` | 3 batches, 2 checkpoints interleaved | At least 50 of 75 keys present |
| `SnapshotWithSubsequentDeletes` | 50 puts + checkpoint + 25 deletes | Deleted keys absent, rest present |
| `RecoveryIdempotence` | 100 puts, recover 3× | State identical every round |
| `BinaryDataInValues` | All 256 byte values (0x00–0xFF) in one value | Exact round-trip |
| `RapidPutDeleteSameKey` | 50 alternating put/delete on same key | Key absent (last op was delete) |
| `MissingSnapshotWithValidWal` | Delete snapshot, keep WAL | At least 50 keys recovered from WAL |
| `LargeCheckpointRecovery` | 5000 entries + checkpoint, then recover | All 5000 present |

---

### `CrashSimulationStress` — 1 test — `tests/test_crash_simulation_stress.cpp`
10 crash-recovery cycles. Each cycle: random ops on a 50-key pool, alternating clean flush vs dirty crash (destructor only). Final recovery must succeed.

---

## Test Quality Notes

**Strengths:**
- Property/model-equivalence tests catch bugs hand-written scenarios miss
- `fork()`-based crash is real — actual process death, not a simulated close
- Failure injection hits actual CRC and magic-byte paths on disk
- Structural rebalancing tests (splits, merges, borrow) cover the hardest B+Tree logic
- `BtreeTestFixture` auto-cleans WAL/snapshot files — no test pollution between runs

**Known gaps:**
- No concurrency tests (engine is single-threaded by design — MVCC planned)
- No range query tests (API doesn't exist yet, but leaf linkage is implemented)
- `CrashSimulationStress` is light on assertions — mostly "doesn't crash" not "data is correct"
- `Property_Atomicity` assertion is intentionally lenient (`ok() || is_not_found()`) — doesn't fully verify the atomicity claim

---

## Quick Command Reference

```bash
# Build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build

# Run all tests
./build/embrace_tests

# Run one suite
./build/embrace_tests --gtest_filter="BtreeBasicTest.*"

# Sanitizers
./scripts/run_sanitizers.sh

# Coverage (requires lcov)
./scripts/coverage.sh
./scripts/coverage.sh open   # open HTML report

# Format
cmake --build build --target format

# Fuzzer (requires clang++)
cmake -B build_fuzz -G Ninja -DENABLE_FUZZING=ON -DCMAKE_CXX_COMPILER=clang++
cmake --build build_fuzz --target fuzz_wal_parser
./build_fuzz/fuzz_wal_parser corpus/ -max_total_time=60
```
