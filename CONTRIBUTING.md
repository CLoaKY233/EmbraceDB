# Contributing to Embrace

Thank you for your interest in contributing to Embrace! This document provides guidelines for getting involved.

## Current Status

Embrace is currently a solo project in active development (Sprint 0 complete, sprints 1-6 planned). All contributions are welcome, but please coordinate beforehand to avoid duplicate work.

## How to Contribute

### Reporting Issues

- **Bug reports**: Open an issue with:
  - Clear description of the problem
  - Steps to reproduce
  - Expected vs actual behavior
  - Environment (OS, compiler, C++ version)
  - Relevant logs (if applicable)

- **Feature requests**: Open an issue with:
  - Motivation and use case
  - Proposed API (if applicable)
  - How it fits into the roadmap

### Code Contributions

1. **Check the roadmap** ([ROADMAP.md](./ROADMAP.md))
   - Understand where the project is heading
   - Pick a sprint task or open issue

2. **Create an issue** (if none exists)
   - Discuss the change before implementing
   - Get feedback on approach

3. **Fork and branch**
   ```bash
   git checkout -b feature/your-feature
   # or
   git checkout -b fix/issue-number
   ```

4. **Write code**
   - Follow the [Code Style](#code-style) guidelines
   - Add tests for new functionality
   - Update documentation

5. **Test thoroughly**
   ```bash
   cmake -B build -DCMAKE_BUILD_TYPE=Debug
   cmake --build build --target test
   ```

6. **Submit a pull request**
   - Link the related issue
   - Describe what changed and why
   - Reference any benchmark results if performance-related

## Code Style

### Formatting

We use `clang-format` (LLVM style) with a 100-column limit.

```bash
# Format a file
clang-format -i src/myfile.cpp

# Check formatting (CI will enforce)
clang-format --dry-run src/myfile.cpp
```

**Style rules** (see `.clang-format`):
- Indent: 4 spaces
- Column limit: 100
- Namespace indentation: all
- Brace style: Allman (opening brace on new line)

### Code Organization

```cpp
// src/mymodule/myfile.hpp

#pragma once

#include <cstdint>
#include "core/status.hpp"

namespace embrace::mymodule {

    class MyClass {
      public:
        // Public methods
        auto public_method() -> void;

      private:
        // Private members
        int private_member_;

        // Private helpers
        auto private_helper() -> void;
    };

} // namespace embrace::mymodule
```

### Naming Conventions

- **Classes**: `PascalCase` (e.g., `LeafNode`, `WalWriter`)
- **Functions**: `snake_case` (e.g., `compute_crc32`, `find_leaf`)
- **Variables**: `snake_case` (e.g., `buffer_size`, `fd_`)
- **Constants**: `UPPER_CASE` (e.g., `MAX_KEY_SIZE`, `PAGE_SIZE`)
- **Member variables**: Suffix with `_` (e.g., `root_`, `wal_path_`)

### Modern C++23 Features

We actively use:
- **Structured bindings**: `auto [status, value] = read_le32(fd);`
- **std::optional**: For nullable returns
- **std::unique_ptr**: For memory safety
- **std::source_location**: For logging context
- **Ranges**: `std::ranges::upper_bound()` instead of `std::upper_bound()`
- **Concepts**: Use type constraints where applicable

Avoid:
- Raw pointers (use `std::unique_ptr`, `std::shared_ptr`)
- `new`/`delete` (use smart pointers)
- C-style casts (use `static_cast`, `reinterpret_cast`)
- Global variables (except singletons like `Logger`)

## Testing

### Test Coverage Expectations

- New features: ≥80% line coverage
- Bug fixes: Test case that fails before fix, passes after
- Performance changes: Include benchmark results

### Test Structure

```cpp
// tests/test_btree.cpp

#include <gtest/gtest.h>
#include "indexing/btree.hpp"

TEST(BtreeTest, InsertAndRetrieve) {
    embrace::indexing::Btree tree;
    tree.put("key1", "value1");
    
    auto result = tree.get("key1");
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "value1");
}

TEST(BtreeTest, DeleteRemovesKey) {
    embrace::indexing::Btree tree;
    tree.put("key1", "value1");
    tree.remove("key1");
    
    auto result = tree.get("key1");
    EXPECT_FALSE(result.has_value());
}
```

### Running Tests

```bash
# Build with tests
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Run tests
ctest --output-on-failure

# Run specific test
ctest -R BtreeTest --output-on-failure
```

## Documentation

### API Documentation

All public APIs should have Doxygen-style comments:

```cpp
/**
 * @brief Insert or update a key-value pair.
 *
 * @param key The key to insert (max 128 bytes)
 * @param value The value to store (max 1024 bytes)
 * @return Status::Ok() on success, IOError if WAL write fails
 */
auto put(const core::Key& key, const core::Value& value) -> core::Status;
```

### Documentation Files

- **README.md**: Feature overview, quick start
- **ARCHITECTURE.md**: Design rationale, data structures, algorithms
- **ROADMAP.md**: Development plan with sprint details
- **CONTRIBUTING.md** (this file): How to contribute

## Performance Considerations

### Benchmarking

If your change affects performance, include benchmarks:

```bash
# Run benchmarks
cmake --build build --target benchmark

# Compare before/after
# Document results in PR
```

### Guidelines

- **Memory**: Avoid allocations in hot paths
- **Concurrency**: Use lock-free patterns where possible (Sprint 2)
- **I/O**: Batch writes to reduce syscalls
- **Caching**: Consider CPU cache alignment for frequently accessed data

## Git Workflow

### Commit Messages

```
[Sprint#] Brief description (50 chars max)

Longer explanation if needed (72 chars per line).

- Bullet points for multiple changes
- Reference issues: Closes #123
```

### Branch Naming

```
feature/btree-range-queries    # New feature
fix/wal-crc-validation         # Bug fix
docs/architecture              # Documentation
refactor/btree-rebalance       # Code cleanup
```

### Pull Request Etiquette

- Keep PRs focused (one feature per PR)
- Rebase on `main` before submitting
- Squash commits if history is messy
- Link related issues
- Wait for CI to pass

## Development Setup

### Prerequisites

- **CMake**: >= 3.28
- **Compiler**: clang++ >= 16 (C++23 support)
- **Tools**: clang-format, clang-tidy
- **Optional**: Google Test, Google Benchmark

### Quick Setup

```bash
# Clone and build
git clone https://github.com/cloaky233/EmbraceDB.git
cd EmbraceDB
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Run example
./build/embrace

# Check formatting
clang-format --dry-run src/**/*.cpp src/**/*.hpp
```

### Zed Setup (Recommended)

The project includes Zed editor config (`.zed/`):
- Auto-format on save (clang-format)
- LSP integration (clangd)
- Pre-configured build tasks

## Roadmap Alignment

When contributing, refer to [ROADMAP.md](./ROADMAP.md):

| Sprint | Focus | Duration |
|--------|-------|----------|
| **Sprint 1** | Testing & Stability | 3 days |
| **Sprint 2** | Concurrency (MVCC) | 5 days |
| **Sprint 3** | Range Queries | 2 days |
| **Sprint 4** | Compression | 3 days |
| **Sprint 5** | Transactions & Bloom | 4 days |
| **Sprint 6** | Production Readiness | 3 days |

Contributions that accelerate these sprints are especially valued.

## Questions?

- Open a GitHub issue for discussion
- Ask in issue comments before starting work
- Tag `@cloaky233` for visibility

---

**Thank you for contributing to Embrace!** 🚀

I'm building a storage engine that prioritizes clarity and performance. Your contributions help achieve that goal.


