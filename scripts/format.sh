#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_ROOT"

# Find all C++ source files
mapfile -t FILES < <(find src tests -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" \) 2>/dev/null)

if [ ${#FILES[@]} -eq 0 ]; then
    echo "❌ No C++ files found in src/ or tests/"
    exit 1
fi

echo "Found ${#FILES[@]} files to format"

if [ "${1:-}" = "--check" ]; then
    echo "🔍 Checking formatting (dry-run)..."
    clang-format-18 --dry-run --Werror "${FILES[@]}"
    echo "✅ All files are properly formatted"
else
    echo "✨ Formatting files..."
    clang-format-18 -i "${FILES[@]}"
    echo "✅ Formatting complete"
fi
