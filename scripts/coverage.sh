#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build_coverage"
COVERAGE_DIR="$PROJECT_DIR/coverage_report"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

check_tools() {
    for tool in lcov genhtml; do
        if ! command -v "$tool" &> /dev/null; then
            echo -e "${RED}Missing: $tool. Install with: sudo apt install lcov${NC}"
            exit 1
        fi
    done
}

build() {
    echo -e "${YELLOW}Building with coverage instrumentation...${NC}"
    cmake -B "$BUILD_DIR" -S "$PROJECT_DIR" \
        -DCMAKE_BUILD_TYPE=Debug \
        -DENABLE_COVERAGE=ON
    cmake --build "$BUILD_DIR" -j"$(nproc)" --target embrace_tests
}

run_tests() {
    echo -e "${YELLOW}Running tests...${NC}"
    cd "$PROJECT_DIR"
    "$BUILD_DIR/embrace_tests"
}

generate_report() {
    echo -e "${YELLOW}Generating coverage report...${NC}"

    rm -rf "$COVERAGE_DIR"
    mkdir -p "$COVERAGE_DIR"

    lcov --capture \
        --directory "$BUILD_DIR" \
        --output-file "$COVERAGE_DIR/coverage.info" \
        --ignore-errors mismatch,inconsistent \
        --rc branch_coverage=1

    lcov --remove "$COVERAGE_DIR/coverage.info" \
        '*/build_coverage/*' \
        '/usr/*' \
        '*/tests/*' \
        '*/fuzz/*' \
        --output-file "$COVERAGE_DIR/coverage.info" \
        --ignore-errors unused,inconsistent \
        --rc branch_coverage=1

    genhtml "$COVERAGE_DIR/coverage.info" \
        --output-directory "$COVERAGE_DIR/html" \
        --ignore-errors inconsistent \
        --rc branch_coverage=1

    echo -e "${GREEN}Report: $COVERAGE_DIR/html/index.html${NC}"
}

check_threshold() {
    local threshold="${1:-85}"
    local coverage
    coverage=$(lcov --summary "$COVERAGE_DIR/coverage.info" 2>&1 | \
        grep 'lines' | grep -oE '[0-9]+\.[0-9]+' | head -1)

    if [ -z "$coverage" ]; then
        echo -e "${RED}Failed to parse coverage${NC}"
        return 1
    fi

    echo -e "${YELLOW}Line coverage: ${coverage}%${NC}"

    if awk "BEGIN {exit !($coverage < $threshold)}"; then
        echo -e "${RED}❌ Coverage ${coverage}% below threshold ${threshold}%${NC}"
        return 1
    fi

    echo -e "${GREEN}✅ Coverage ${coverage}% meets threshold ${threshold}%${NC}"
}

usage() {
    echo "Usage: $0 [command] [threshold]"
    echo ""
    echo "Commands:"
    echo "  run       Build, test, and generate report (default)"
    echo "  check N   Check coverage meets N% threshold (default: 85)"
    echo "  report    Generate report only (assumes tests already ran)"
    echo "  open      Open HTML report in browser"
}

case "${1:-run}" in
    run)
        check_tools
        build
        run_tests
        generate_report
        check_threshold "${2:-85}"
        ;;
    check)
        check_threshold "${2:-85}"
        ;;
    report)
        check_tools
        generate_report
        ;;
    open)
        xdg-open "$COVERAGE_DIR/html/index.html" 2>/dev/null || \
            open "$COVERAGE_DIR/html/index.html" 2>/dev/null || \
            echo "Open: $COVERAGE_DIR/html/index.html"
        ;;
    -h|--help)
        usage
        ;;
    *)
        echo -e "${RED}Unknown: $1${NC}"
        usage
        exit 1
        ;;
esac
