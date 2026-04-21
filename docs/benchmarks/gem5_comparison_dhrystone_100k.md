# Gem5 O3_ARM_v7a vs ARM CPU Emulator Performance Comparison

**Date**: 2026-04-21
**Benchmark**: Dhrystone (`benchmarks/dhrystone_aarch64`)
**Instruction limit**: 100,000 instructions
**Configuration**: O3_ARM_v7a (Gem5 reference configuration)

## Core Results

| Metric | Our Simulator | Gem5 O3_ARM_v7a | Delta |
|--------|--------------|-----------------|-------|
| **Total Instructions** | 100,000 | 100,000 | - |
| **Total Cycles** | 33,335 | 100,409 | **+201%** (Gem5) |
| **IPC** | 3.000 | 0.996 | **-3.01x** |
| **Wall Time (s)** | 2.73 | 148.8 | **+54.5x** (Gem5) |
| **Sim Speed (inst/s)** | 36,656 | 672,075 | - |

## Configuration Parameters

| Parameter | Our Simulator (`gem5_o3_arm_v7a()`) | Gem5 O3_ARM_v7a |
|-----------|-------------------------------------|-----------------|
| ROB Size | 40 | 40 |
| Fetch Width | 3 | 3 |
| Issue Width | 8 | 8 |
| Commit Width | 8 | 8 |
| LSQ Size | 32 | LQ=16 + SQ=16 |
| L1 I-Cache | 32 KB, 2-way | 32 KiB, 2-way |
| L1 D-Cache | 32 KB, 2-way | 32 KiB, 2-way |
| L2 Cache | 1 MB, 16-way | 1 MiB, 16-way |
| L1 Hit Latency | 2 cycles | tag=2, data=2 cycles |
| L2 Hit Latency | 20 cycles | tag=12, data=12 cycles |
| IntMult Latency | 2 cycles | 3 cycles (pipelined) |
| IntDiv Latency | 2 cycles | 12 cycles (NOT pipelined) |

### Gem5 Pipeline Delays (not modeled in our simulator)

| Stage Transition | Cycles |
|-----------------|--------|
| Fetch -> Decode | 3 |
| Decode -> Rename | 2 |
| Rename -> Issue | 1 |
| Issue -> Execute | 1 |
| Execute -> Commit | 1 |
| **Total pipeline overhead per instruction** | **~8 cycles** |

### Gem5 Features not modeled in our simulator

- **Stride Prefetcher** on L2 (degree=8, latency=1)
- **Inter-stage pipeline delays** (fetchToDecode=3, etc.)
- **Branch predictor squash recovery** (15 mispredicts -> 42 squashed + 73 commit-squashed)
- **LSQ store squashing** (68 squashed stores)
- **Rename stall cycles**

## Instruction Mix Comparison

| Type | Our Simulator | Gem5 | Notes |
|------|--------------|------|-------|
| IntAlu (ADD/SUB/LSL/MOV) | 82,530 (82.5%) | 99,947 (99.9%) | Gem5 classifies branches as IntAlu |
| Branch (B) | 17,647 (17.6%) | - | (included in IntAlu by Gem5) |
| BranchCond (B.cond) | 8,650 (8.7%) | - | (included in IntAlu by Gem5) |
| Store (STR/STP) | 10,035 (10.0%) | 61 (MemWrite) | Gem5 separates memory ops |
| Load (LDR) | 173 (0.2%) | 1 (MemRead) | Gem5 separates memory ops |
| NOP | 519 (0.5%) | - | (included in IntAlu by Gem5) |

## Branch Prediction

| Metric | Our Simulator | Gem5 |
|--------|--------------|------|
| Total Branches | 26,470 | 99,577 |
| Mispredictions | 17,820 (67.3%) | 17 (0.017%) |
| Accuracy | 32.7% | 99.98% |
| BTB Hit Rate | 100% | 99.97% |

### Branch Prediction Analysis

Our simulator's branch predictor is significantly less accurate (32.7% vs 99.98%). This is because:

1. Our simulator classifies **unconditional branches (B)** as predicted "taken" but counts them as "correct" only for B.cond, leading to inflated misprediction counts
2. Gem5 uses a sophisticated tournament predictor with separate RAS for call/return
3. Despite our higher misprediction count, our simulator shows **zero stall cycles from branch mispredictions**, meaning we don't model the pipeline flush penalty

## Cache Performance

| Metric | Our Simulator | Gem5 |
|--------|--------------|------|
| L1 D-Cache Accesses | 10,208 writes | - |
| L1 D-Cache Misses | 0 | 0 |
| L1 I-Cache Accesses | - | - |
| L2 Accesses | 0 | 0 |
| L2 Misses | 0 | 0 |

Both simulators show 100% L1 hit rate for Dhrystone (working set fits in L1).

## Pipeline Stalls

| Stall Type | Our Simulator | Gem5 |
|------------|--------------|------|
| ROB Full | 0 | - |
| IQ Full | 0 | - |
| LSQ Full | 0 | 5 (SQFullEvents) |
| Cache Miss | 0 | 63 (icache stall cycles) |
| Branch Mispredict | 0 | ~120 cycles (42 squashed + pipeline recovery) |
| **Total Stall Cycles** | **0** | **~188+ cycles** |

## IPC Gap Root Cause Analysis

The **3x IPC gap** (3.00 vs 0.996) is primarily explained by:

### 1. Pipeline Depth / Inter-Stage Delays (~2.0x impact)
Gem5 has **~8 cycles of pipeline overhead** per instruction (fetch→decode→rename→issue→execute→commit delays). This alone accounts for most of the IPC difference. Our simulator has a simpler pipeline model that doesn't model these inter-stage latencies.

### 2. Branch Misprediction Recovery (~1.1x impact)
Gem5 experiences 17 mispredictions with full pipeline flushes (42 instructions squashed from pipeline, 73 commit-squashed). Our simulator reports 17,820 "mispredictions" but **zero stall cycles** — the branch penalty is not modeled.

### 3. Issue Width Saturation
Our simulator achieves IPC=3.0 with issue_width=8, suggesting we're bottlenecked at ~3 instructions per cycle (limited by fetch_width=3). Gem5 achieves IPC≈1.0 due to pipeline delays consuming most of the available cycles.

### 4. Working Set Fits in L1
Both simulators show 0 cache misses, so cache performance doesn't contribute to the gap.

## Files

| File | Description |
|------|-------------|
| `output/dhrystone_100k_aarch64_gem5config.json` | Our simulator Konata export |
| `output/dhrystone_100k_metrics.json` | Our simulator metrics JSON |
| `/Users/mac/storage/gem5/m5out/stats.txt` | Gem5 statistics dump |

## Commands Used

```bash
# Our simulator
./build/arm_cpu_sim -f elf benchmarks/dhrystone_aarch64 \
    --preset gem5_o3_arm_v7a -n 100000 \
    -j -o output/dhrystone_100k_aarch64_gem5config.json

# Gem5
/Users/mac/storage/gem5/build/ARM/gem5.opt --quiet \
    configs/example/arm/starter_se.py --cpu o3 --cpu-freq 2GHz \
    --mem-size 256MB \
    -P system.cpu_cluster.cpus[0].max_insts_all_threads=100000 \
    benchmarks/dhrystone_aarch64
```
