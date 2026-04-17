#!/bin/bash
# test_all.sh - 一键全量测试脚本
#
# 运行所有测试并输出结构化结果，方便 agent 快速定位问题。
# 每个阶段独立，失败不中断后续阶段。
#
# 用法:
#   ./scripts/test_all.sh              # 全量测试
#   ./scripts/test_all.sh --build-only # 只构建，不测试
#   ./scripts/test_all.sh --fast       # 跳过压力测试

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
SIM_BIN="$BUILD_DIR/arm_cpu_sim"
DATA_DIR="$PROJECT_ROOT/tests/data"

PASS_COUNT=0
FAIL_COUNT=0
SKIP_COUNT=0
RESULTS=()

FAST=false
if [[ "${1:-}" == "--fast" ]]; then
    FAST=true
fi

# =====================================================================
# Helpers
# =====================================================================

log_stage() {
    echo ""
    echo "========================================="
    echo "  $1"
    echo "========================================="
}

record() {
    local name="$1" status="$2" detail="${3:-}"
    RESULTS+=("$name|$status|$detail")
    if [[ "$status" == "PASS" ]]; then
        PASS_COUNT=$((PASS_COUNT + 1))
        echo "  [PASS] $name"
    elif [[ "$status" == "FAIL" ]]; then
        FAIL_COUNT=$((FAIL_COUNT + 1))
        echo "  [FAIL] $name — $detail"
    else
        SKIP_COUNT=$((SKIP_COUNT + 1))
        echo "  [SKIP] $name — $detail"
    fi
}

check_file() {
    if [ -f "$1" ]; then
        record "$2" "PASS"
    else
        record "$2" "FAIL" "$1 not found"
    fi
}

run_cmd() {
    local name="$1"
    shift
    local output
    output=$("$@" 2>&1)
    local rc=$?
    if [ $rc -eq 0 ]; then
        record "$name" "PASS"
    else
        record "$name" "FAIL" "exit code $rc — $(echo "$output" | tail -3)"
    fi
}

# =====================================================================
# 1. BUILD
# =====================================================================

log_stage "BUILD"

cd "$PROJECT_ROOT"

NPROC=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

cmake -B build -DCMAKE_BUILD_TYPE=Release 2>&1 | tail -3
if [ $? -ne 0 ]; then
    record "cmake configure" "FAIL" "cmake configure failed"
else
    record "cmake configure" "PASS"
fi

cmake --build build "-j$NPROC" 2>&1 | tail -5
if [ $? -ne 0 ]; then
    record "cmake build" "FAIL" "build failed"
else
    record "cmake build" "PASS"
fi

check_file "$SIM_BIN" "arm_cpu_sim binary"

# =====================================================================
# 2. UNIT TESTS (ctest)
# =====================================================================

log_stage "UNIT TESTS"

cd "$BUILD_DIR"
ctest --output-on-failure --timeout 60 2>&1 | tail -20
CTEST_RC=$?
cd "$PROJECT_ROOT"

if [ $CTEST_RC -eq 0 ]; then
    record "ctest" "PASS"
else
    record "ctest" "FAIL" "exit code $CTEST_RC"
fi

# =====================================================================
# 3. TEXT TRACE SIMULATION
# =====================================================================

log_stage "TEXT TRACE SIMULATION"

if [ ! -f "$SIM_BIN" ]; then
    record "text_trace_basic" "SKIP" "binary not built"
    record "text_trace_memory" "SKIP" "binary not built"
    record "text_trace_branches" "SKIP" "binary not built"
else
    # Basic trace
    if [ -f "$DATA_DIR/text_trace_basic.txt" ]; then
        run_cmd "text_trace_basic" "$SIM_BIN" -f text "$DATA_DIR/text_trace_basic.txt" -n 50 -c 10000
    else
        record "text_trace_basic" "FAIL" "data file missing"
    fi

    # Memory trace
    if [ -f "$DATA_DIR/text_trace_memory.txt" ]; then
        run_cmd "text_trace_memory" "$SIM_BIN" -f text "$DATA_DIR/text_trace_memory.txt" -n 100 -c 10000
    else
        record "text_trace_memory" "FAIL" "data file missing"
    fi

    # Branch trace
    if [ -f "$DATA_DIR/text_trace_branches.txt" ]; then
        run_cmd "text_trace_branches" "$SIM_BIN" -f text "$DATA_DIR/text_trace_branches.txt" -n 50 -c 10000
    else
        record "text_trace_branches" "FAIL" "data file missing"
    fi
fi

# =====================================================================
# 4. ELF SIMULATION (optional — needs cross-compiled test ELF)
# =====================================================================

log_stage "ELF SIMULATION"

if [ -f "$SIM_BIN" ] && [ -f "$DATA_DIR/test_elf_aarch64" ]; then
    run_cmd "elf_sim" "$SIM_BIN" -f elf "$DATA_DIR/test_elf_aarch64" -n 200 -c 100000
else
    record "elf_sim" "SKIP" "cross-compiled ELF not available (run scripts/compile_test_elf.sh)"
fi

# Dynamic ELF simulation (requires aarch64-linux-gnu-gcc with glibc sysroot)
if [ -f "$SIM_BIN" ] && [ -f "$DATA_DIR/dynamic_test_aarch64" ]; then
    run_cmd "elf_sim_dynamic" "$SIM_BIN" -f elf "$DATA_DIR/dynamic_test_aarch64" -n 200 -c 500000
else
    record "elf_sim_dynamic" "SKIP" "dynamic test ELF not available (needs aarch64-linux-gnu-gcc)"
fi

# =====================================================================
# 5. MULTI-CONFIG
# =====================================================================

log_stage "MULTI-CONFIG"

if [ -f "$SIM_BIN" ] && [ -f "$DATA_DIR/text_trace_basic.txt" ]; then
    # Minimal config
    run_cmd "config_minimal" "$SIM_BIN" -f text "$DATA_DIR/text_trace_basic.txt" \
        --window-size 16 --issue-width 2 --commit-width 2 -c 10000

    # High-performance config
    run_cmd "config_highperf" "$SIM_BIN" -f text "$DATA_DIR/text_trace_basic.txt" \
        --window-size 256 --issue-width 6 --commit-width 6 -c 10000

    # Tiny L1 cache
    run_cmd "config_tiny_l1" "$SIM_BIN" -f text "$DATA_DIR/text_trace_basic.txt" \
        --l1-size 4 --l2-size 16 -c 10000

    # Large cache
    run_cmd "config_large_cache" "$SIM_BIN" -f text "$DATA_DIR/text_trace_basic.txt" \
        --l1-size 128 --l2-size 1024 --l3-size 16384 -c 10000
else
    record "config_minimal" "SKIP" "prerequisites not met"
    record "config_highperf" "SKIP" "prerequisites not met"
    record "config_tiny_l1" "SKIP" "prerequisites not met"
    record "config_large_cache" "SKIP" "prerequisites not met"
fi

# =====================================================================
# 6. STRESS (long trace, can be skipped with --fast)
# =====================================================================

log_stage "STRESS"

if $FAST; then
    record "stress_long_trace" "SKIP" "--fast mode"
    record "stress_text_trace_long" "SKIP" "--fast mode"
else
    if [ -f "$SIM_BIN" ] && [ -f "$DATA_DIR/text_trace_long.txt" ]; then
        run_cmd "stress_text_trace_long" "$SIM_BIN" -f text "$DATA_DIR/text_trace_long.txt" \
            -c 1000000
    else
        record "stress_text_trace_long" "SKIP" "prerequisites not met"
    fi

    # Stress: repeat basic trace 10x via CLI
    if [ -f "$SIM_BIN" ] && [ -f "$DATA_DIR/text_trace_memory.txt" ]; then
        run_cmd "stress_memory_trace" "$SIM_BIN" -f text "$DATA_DIR/text_trace_memory.txt" \
            -n 1000 -c 500000
    else
        record "stress_memory_trace" "SKIP" "prerequisites not met"
    fi
fi

# =====================================================================
# SUMMARY
# =====================================================================

log_stage "SUMMARY"

echo ""
echo "  Total:  $((PASS_COUNT + FAIL_COUNT + SKIP_COUNT))"
echo "  Pass:   $PASS_COUNT"
echo "  Fail:   $FAIL_COUNT"
echo "  Skip:   $SKIP_COUNT"
echo ""

if [ $FAIL_COUNT -gt 0 ]; then
    echo "  Failed tests:"
    for r in "${RESULTS[@]}"; do
        if [[ "$r" == *"|FAIL|"* ]]; then
            echo "    - $r"
        fi
    done
    echo ""
fi

if [ $FAIL_COUNT -eq 0 ]; then
    echo "  ALL TESTS PASSED"
    echo ""
    exit 0
else
    echo "  SOME TESTS FAILED"
    echo ""
    exit 1
fi
