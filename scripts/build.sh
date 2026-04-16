#!/bin/bash
# build.sh - 一键构建 ARM CPU 仿真器
#
# 用法:
#   ./scripts/build.sh              # Release 构建
#   ./scripts/build.sh Debug        # Debug 构建
#   ./scripts/build.sh -j8          # 指定并行数

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

cd "$PROJECT_ROOT"

# 检测并行数
if [[ "$*" == *-j* ]]; then
    PARALLEL=""
else
    NPROC=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
    PARALLEL="-j$NPROC"
fi

# 构建类型
BUILD_TYPE="Release"
for arg in "$@"; do
    case "$arg" in
        Debug|Release|RelWithDebInfo|MinSizeRel)
            BUILD_TYPE="$arg"
            ;;
    esac
done

echo "========================================="
echo "  ARM CPU Emulator - 构建脚本"
echo "  构建类型: $BUILD_TYPE"
echo "  项目目录: $PROJECT_ROOT"
echo "========================================="

cmake -B build -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build build $PARALLEL

BINARY="$PROJECT_ROOT/build/arm_cpu_sim"
if [ -f "$BINARY" ]; then
    echo ""
    echo "构建成功: $BINARY"
    echo ""
    echo "快速测试:"
    echo "  $BINARY --help"
    echo "  $BINARY -f elf tests/data/test_elf_aarch64"
else
    echo "Error: 构建产物未找到" >&2
    exit 1
fi
