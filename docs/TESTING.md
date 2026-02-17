# Testing Guide

## Quick Start

```bash
# Build and run all tests
cmake -B build && cmake --build build
./build/embrace_tests

# Run with sanitizers
./scripts/run_sanitizers.sh

# Generate coverage report
./scripts/coverage.sh
```

## Test Categories

| Category | File | Purpose |
|----------|------|---------|
| Unit | `test_btree_*.cpp` | Core B+Tree operations |
| Recovery | `test_wal_recovery.cpp` | WAL/snapshot recovery |
| Property | `test_state_machine_properties.cpp` | State machine invariants |
| Failure | `test_failure_injection.cpp` | Corruption/edge cases |
| Stress | `test_crash_simulation_stress.cpp` | Repeated crash cycles |

## Running Tests

### All Tests
```bash
./build/embrace_tests
```

### Specific Suite
```bash
./build/embrace_tests --gtest_filter="StateMachinePropertyTest.*"
```

### Parallel Execution
```bash
ctest --test-dir build -j$(nproc)
```

## Sanitizers

Detect memory bugs and undefined behavior:

```bash
# Build with sanitizers
cmake -B build -DENABLE_SANITIZERS=ON
cmake --build build
./build/embrace_tests

# Or use the script
./scripts/run_sanitizers.sh
```

Detects:
- Memory leaks (LeakSanitizer)
- Use-after-free, buffer overflow (AddressSanitizer)
- Undefined behavior (UBSan)

## Code Coverage

Generate coverage report (requires `lcov`):

```bash
./scripts/coverage.sh

# View report
./scripts/coverage.sh open
```

Threshold: **85%** line coverage required for CI.

## Fuzzing

Test WAL parser with random inputs (requires clang):

```bash
cmake -B build_fuzz -DENABLE_FUZZING=ON -DCMAKE_CXX_COMPILER=clang++
cmake --build build_fuzz --target fuzz_wal_parser
./build_fuzz/fuzz_wal_parser corpus/ -max_total_time=60
```

## CI Pipeline

On every push/PR:

1. **build-and-test**: Debug + Release builds
2. **sanitizers**: ASAN/UBSan/LeakSan checks
3. **coverage**: 85% threshold gate
4. **format-check**: clang-format validation

## Adding Tests

1. Add test file to `tests/` directory
2. Tests are auto-discovered via CMake glob
3. Use `BtreeTestFixture` for standard setup:

```cpp
#include "test_utils.hpp"

namespace embrace::test {

TEST_F(BtreeTestFixture, YourTestName) {
    ASSERT_TRUE(tree_->put("key", "value").ok());
    // ...
}

} // namespace embrace::test
```

## Test Properties

State machine tests verify:

- **Model equivalence**: DB matches expected state
- **Recovery idempotence**: Multiple recoveries = same result
- **Last write wins**: Repeated puts keep final value
- **Operation ordering**: Correct sequence semantics