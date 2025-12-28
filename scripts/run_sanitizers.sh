#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build_asan"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

cleanup() {
    rm -f "$BUILD_DIR/asan_output.log"
}

run_sanitizers() {
    echo -e "${YELLOW}Building with AddressSanitizer + UBSan + LeakSanitizer...${NC}"

    cmake -B "$BUILD_DIR" -S "$PROJECT_DIR" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DENABLE_SANITIZERS=ON \
        -DCMAKE_CXX_COMPILER="${CXX:-g++}"

    cmake --build "$BUILD_DIR" -j"$(nproc)" --target embrace_tests

    echo -e "${YELLOW}Running tests with sanitizers...${NC}"

    cd "$PROJECT_DIR"
    "$BUILD_DIR/embrace_tests" 2>&1 | tee "$BUILD_DIR/asan_output.log"

    local failed=0

    if grep -qE "(LeakSanitizer|detected memory leaks)" "$BUILD_DIR/asan_output.log"; then
        echo -e "${RED}❌ MEMORY LEAKS DETECTED${NC}"
        failed=1
    fi

    if grep -qE "(heap-use-after-free|heap-buffer-overflow)" "$BUILD_DIR/asan_output.log"; then
        echo -e "${RED}❌ HEAP CORRUPTION DETECTED${NC}"
        failed=1
    fi

    if grep -qE "(stack-buffer-overflow|stack-use-after)" "$BUILD_DIR/asan_output.log"; then
        echo -e "${RED}❌ STACK CORRUPTION DETECTED${NC}"
        failed=1
    fi

    if grep -qE "runtime error:" "$BUILD_DIR/asan_output.log"; then
        echo -e "${RED}❌ UNDEFINED BEHAVIOR DETECTED${NC}"
        failed=1
    fi

    if [ $failed -eq 0 ]; then
        echo -e "${GREEN}✅ All sanitizer checks passed${NC}"
    fi

    return $failed
}

run_fuzzer() {
    local duration="${1:-60}"

    if ! command -v clang++ &> /dev/null; then
        echo -e "${RED}Fuzzing requires clang++${NC}"
        return 1
    fi

    local clang_version
    clang_version=$(clang++ --version | grep -oP '\d+' | head -1)

    if [ "$clang_version" -ge 18 ]; then
        echo -e "${YELLOW}Warning: clang $clang_version may have compatibility issues with fmt library${NC}"
        echo -e "${YELLOW}If build fails, try clang 15-17 or update fmt to 11.x${NC}"
    fi

    echo -e "${YELLOW}Building fuzzer with clang...${NC}"

    local fuzz_build="$PROJECT_DIR/build_fuzz"

    if ! cmake -B "$fuzz_build" -S "$PROJECT_DIR" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DENABLE_FUZZING=ON \
        -DCMAKE_CXX_COMPILER=clang++ 2>&1; then
        echo -e "${RED}CMake configuration failed${NC}"
        return 1
    fi

    if ! cmake --build "$fuzz_build" -j"$(nproc)" --target fuzz_wal_parser 2>&1; then
        echo -e "${RED}Fuzzer build failed. This may be due to clang/fmt compatibility.${NC}"
        echo -e "${YELLOW}Try: export CXX=clang++-17 and retry${NC}"
        return 1
    fi

    echo -e "${YELLOW}Running fuzzer for ${duration}s...${NC}"

    mkdir -p "$fuzz_build/corpus"
    "$fuzz_build/fuzz_wal_parser" "$fuzz_build/corpus" -max_total_time="$duration"

    echo -e "${GREEN}✅ Fuzzing completed${NC}"
}

usage() {
    echo "Usage: $0 [command] [options]"
    echo ""
    echo "Commands:"
    echo "  sanitize    Run tests with AddressSanitizer (default)"
    echo "  fuzz [sec]  Run WAL parser fuzzer for specified seconds (default: 60)"
    echo "  all         Run both sanitizers and fuzzer"
    echo ""
    echo "Examples:"
    echo "  $0                  # Run sanitizer tests"
    echo "  $0 fuzz 120         # Fuzz for 2 minutes"
    echo "  $0 all              # Run everything"
}

case "${1:-sanitize}" in
    sanitize)
        run_sanitizers
        ;;
    fuzz)
        run_fuzzer "${2:-60}"
        ;;
    all)
        run_sanitizers
        run_fuzzer "${2:-60}"
        ;;
    -h|--help)
        usage
        ;;
    *)
        echo -e "${RED}Unknown command: $1${NC}"
        usage
        exit 1
        ;;
esac
