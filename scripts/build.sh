#!/bin/bash
# build.sh - Build the ARM CPU emulator and run tests.
#
# Usage:
#   ./scripts/build.sh            # Release build + tests
#   ./scripts/build.sh Debug      # Debug build + tests

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"

BUILD_TYPE="${1:-Release}"

NPROC=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo "========================================="
echo "  Building ARM CPU Emulator ($BUILD_TYPE)"
echo "========================================="

cd "$PROJECT_ROOT"

cmake -B build -DCMAKE_BUILD_TYPE="$BUILD_TYPE" 2>&1 | tail -3
cmake --build build "-j$NPROC" 2>&1 | tail -5

echo ""
echo "构建成功"

echo ""
echo "运行测试..."
echo "========================================="
cd "$BUILD_DIR"
if ctest --output-on-failure; then
    echo ""
    echo "所有测试通过"
else
    echo ""
    echo "测试失败！请检查上方错误信息。" >&2
    exit 1
fi
