#!/bin/bash
# compile_test_elf.sh - 编译 ARM64 测试 ELF
#
# 用法:
#   ./scripts/compile_test_elf.sh                  # 自动检测交叉编译器
#   ./scripts/compile_test_elf.sh aarch64-elf-gcc  # 指定编译器
#   ./scripts/compile_test_elf.sh aarch64-linux-gnu-gcc
#
# 输出: tests/data/test_elf_aarch64

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUT_DIR="$PROJECT_ROOT/tests/data"
SRC_FILE="$SCRIPT_DIR/test_elf_source.c"
OUT_FILE="$OUT_DIR/test_elf_aarch64"

# 查找交叉编译器
CROSS="${1:-}"
if [ -z "$CROSS" ]; then
    for cc in aarch64-elf-gcc aarch64-linux-gnu-gcc aarch64-none-elf-gcc; do
        if command -v "$cc" &>/dev/null; then
            CROSS="$cc"
            break
        fi
    done
fi

if [ -z "$CROSS" ]; then
    echo "Error: 未找到 ARM64 交叉编译器。请安装以下之一：" >&2
    echo "  - aarch64-elf-gcc        (brew install aarch64-elf-gcc on macOS)" >&2
    echo "  - aarch64-linux-gnu-gcc  (apt install gcc-aarch64-linux-gnu on Linux)" >&2
    echo "或手动指定: $0 <compiler>" >&2
    exit 1
fi

echo "使用编译器: $CROSS"
mkdir -p "$OUT_DIR"

$CROSS -static -O2 -nostdlib -o "$OUT_FILE" "$SRC_FILE"

echo "编译成功: $OUT_FILE"
file "$OUT_FILE"
