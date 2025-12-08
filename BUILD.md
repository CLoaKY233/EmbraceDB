# Build Guide

Complete instructions for building and developing Embrace.

## Prerequisites

### System Requirements

- **OS**: Linux or POSIX-compatible (macOS, WSL2)
- **Compiler**: clang++ 16+ with C++23 support
- **Build system**: CMake 3.28+
- **Package manager**: apt/brew (for dependencies)

### Dependencies

**Required**:
- `cmake` >= 3.28
- `clang++` >= 16

**Optional** (for development):
- `clang-format` (code formatting)
- `clang-tidy` (linting)
- `valgrind` (memory profiling)
- `gtest` (unit testing, auto-fetched)
- `google-benchmark` (benchmarking, auto-fetched)

## Installation

### Linux (Ubuntu/Debian)

```bash
# Update package lists
sudo apt update

# Install build tools and clang
sudo apt install -y \
    cmake \
    clang \
    clang-format \
    clang-tools \
    ninja-build

# Verify versions
cmake --version  # >= 3.28
clang++ --version  # >= 16.0
```

### macOS

```bash
# Using Homebrew
brew install cmake clang-format llvm ninja

# Verify versions
cmake --version
clang++ --version
```

### Windows (WSL2)

Follow Linux instructions within WSL2 Ubuntu environment.

## Building Embrace

### Release Build

```bash
# Configure
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER=clang++

# Build
cmake --build build

# Binary location
./build/embrace
```

### Debug Build

```bash
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_COMPILER=clang++

cmake --build build

# Run with debug symbols
gdb ./build/embrace
```

### Build Variants

**With Address Sanitizer** (detect memory leaks):
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined"

cmake --build build
./build/embrace  # Sanitizer output on stdout
```

**With Thread Sanitizer** (detect races):
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="-fsanitize=thread"

cmake --build build
./build/embrace
```

**With Code Coverage**:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS="--coverage"

cmake --build build
./build/embrace
ctest

# Generate report
lcov --capture --directory build --output-file coverage.info
genhtml coverage.info --output-directory coverage_html
open coverage_html/index.html
```

## Running the Code

### Basic Example

```bash
# Build
cmake -B build && cmake --build build

# Run
./build/embrace

# Expected output
=== EmbraceDB - Checkpointing Test ===
--- Test 1: Auto Checkpointing ---
[...INFO...] Inserted 200 keys with auto-checkpointing
--- Test 2: Fast Recovery from Snapshot ---
[...INFO...] Recovery successful - all keys present
--- Test 3: Manual Checkpoint ---
[...INFO...] Manual checkpoint successful
=== All checkpointing tests passed! ===
```

### Custom Application

```cpp
// my_app.cpp
#include "indexing/btree.hpp"
#include "log/logger.hpp"

int main() {
    // Initialize
    embrace::log::LogConfig log_config;
    log_config.level = embrace::log::Level::Info;
    embrace::log::Logger::instance().init(log_config);

    // Create database
    embrace::indexing::Btree db("my_data.wal");
    db.recover_from_wal();

    // Use it
    db.put("name", "Embrace");
    db.put("version", "0.1.0");
    
    auto name = db.get("name");
    if (name) {
        std::cout << "Database: " << name.value() << std::endl;
    }

    db.flush_wal();
    return 0;
}
```

**Build and link**:
```bash
# Compile (requires include path)
clang++ -std=c++23 -I./src -I./build/_deps/fmt-src/include \
    -c my_app.cpp -o my_app.o

# Link against built libraries
clang++ -std=c++23 \
    my_app.o ./build/embrace.o ./build/embrace_storage.o \
    -L./build/_deps/fmt-build -lfmt \
    -o my_app

./my_app
```

*Note: For production, use CMake-based projects and link against Embrace properly.*

## Testing

### Running Tests (Sprint 1+)

```bash
# Build with tests enabled
cmake -B build -DBUILD_TESTS=ON

# Run all tests
ctest --output-on-failure

# Run specific test
ctest -R BtreeTest --verbose

# Run with timeout
ctest -I 0,10 --timeout 5  # First 10 tests, 5s timeout each
```

### Writing Tests

See [CONTRIBUTING.md](./CONTRIBUTING.md#testing) for test structure.

## Code Quality

### Formatting

```bash
# Format all source files
find src -name "*.cpp" -o -name "*.hpp" | xargs clang-format -i

# Check formatting without modifying
clang-format --dry-run src/indexing/btree.hpp

# Format in editor (Zed)
# Automatically on save (see .zed/settings.json)
```

### Linting

```bash
# Run clang-tidy on a file
clang-tidy src/indexing/btree.cpp -- -std=c++23 -Isrc

# For all files
find src -name "*.cpp" | xargs clang-tidy -- -std=c++23 -Isrc
```

### Static Analysis

```bash
# Enable in CMake
cmake -B build -DCMAKE_CXX_CLANG_TIDY="clang-tidy;-checks=modernize-*"

cmake --build build  # Runs clang-tidy during compilation
```

## Performance Testing

### Benchmarking

```bash
# Build benchmarks (Sprint 1+)
cmake -B build -DBUILD_BENCHMARKS=ON
cmake --build build

# Run benchmarks
./build/embrace_benchmark

# Expected output
Benchmark Name              Time             CPU  Iterations
BM_InsertSequential      1.23 ms         1.22 ms        500
BM_RandomLookup          0.45 ms         0.44 ms       1000
...
```

### Profiling

```bash
# Using perf (Linux)
perf record ./build/embrace
perf report

# Using valgrind
valgrind --tool=callgrind ./build/embrace
kcachegrind callgrind.out.<pid>
```

## Troubleshooting

### CMake Errors

**"Could not find CMake < 3.28"**
```bash
# Update CMake
wget https://github.com/Kitware/CMake/releases/download/v3.28.0/cmake-3.28.0-linux-x86_64.tar.gz
tar -xzf cmake-3.28.0-linux-x86_64.tar.gz
sudo mv cmake-3.28.0-linux-x86_64/bin/* /usr/local/bin/
```

**"clang++ not found"**
```bash
# Set compiler explicitly
cmake -B build -DCMAKE_CXX_COMPILER=/usr/bin/clang++
```

### Compilation Errors

**"error: variable has incomplete type 'struct LogEntry'**
- Usually indicates missing include; check header guards
- Verify circular dependencies: `LogEntry` should be in `logger.hpp`

**"undefined reference to 'embrace::log::Logger::instance()'"**
- Linker error; ensure all `.cpp` files are compiled
- Check CMake `add_executable()` includes all sources

### Runtime Errors

**"terminate called after throwing an exception"**
- Enable debug symbols: `-DCMAKE_BUILD_TYPE=Debug`
- Use AddressSanitizer: `-DCMAKE_CXX_FLAGS="-fsanitize=address"`

**"Segmentation fault"**
- Use GDB: `gdb ./build/embrace`
- Or Valgrind: `valgrind ./build/embrace`

## Clean Build

```bash
# Remove build directory
rm -rf build

# Rebuild from scratch
cmake -B build && cmake --build build
```

## IDE Integration

### Visual Studio Code

```bash
# Install extensions
# - C/C++ (ms-vscode.cpptools)
# - Clangd (llvm-vs-code-extensions.vscode-clangd)
# - CMake (twxs.cmake)

# Configure workspace
# .vscode/settings.json:
{
  "C_Cpp.default.compilerPath": "/usr/bin/clang++",
  "C_Cpp.default.cppStandard": "c++23",
  "clangd.arguments": ["--compile-commands-dir=build"]
}
```

### Zed (Configured)

Already configured in `.zed/`:
- Auto-formatting on save
- LSP (clangd) integration
- Build tasks

Just open the project:
```bash
zed ./
```

### Neovim/Vim

Uses clangd LSP (install `clangd` and configure for your setup).

## CI/CD (GitHub Actions)

```yaml
# .github/workflows/build.yml (coming in Sprint 6)
name: Build & Test

on: [push, pull_request]

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Install dependencies
        run: sudo apt-get install -y cmake clang ninja-build
      - name: Build
        run: cmake -B build && cmake --build build
      - name: Test
        run: ctest --output-on-failure
```

## Next Steps

1. **Build**: `cmake -B build && cmake --build build`
2. **Run**: `./build/embrace`
3. **Develop**: Make changes, format with `clang-format`, rebuild
4. **Test**: Write tests for new features (Sprint 1+)
5. **Benchmark**: Profile changes to ensure no regressions

For more information:
- Architecture details: [ARCHITECTURE.md](./ARCHITECTURE.md)
- Development roadmap: [ROADMAP.md](./ROADMAP.md)
- Contributing: [CONTRIBUTING.md](./CONTRIBUTING.md)

