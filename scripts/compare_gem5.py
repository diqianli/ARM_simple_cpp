#!/usr/bin/env python3
"""
Benchmark comparison: ARM CPU Emulator vs GEM5 O3_ARM_v7a

Runs both simulators on all benchmarks and produces a comparison table
with cycles, instructions, IPC, and wall-clock time.

Usage:
    python3 scripts/compare_gem5.py [--gem5 <path>] [--sim <path>] [--benchmarks-dir <dir>]
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)

# Default paths
DEFAULT_GEM5 = "/Users/mac/storage/gem5/build/ARM/gem5.opt"
DEFAULT_SIM = os.path.join(PROJECT_DIR, "build", "arm_cpu_sim")
DEFAULT_BENCHMARKS_DIR = os.path.join(PROJECT_DIR, "benchmarks")
GEM5_CONFIG = "/Users/mac/storage/gem5/configs/example/arm/starter_se.py"
OUTPUT_DIR = os.path.join(PROJECT_DIR, "output", "gem5_comparison")

# Known max instruction counts for each benchmark (from our simulator with gem5_o3_arm_v7a preset)
# Add 10% headroom to ensure both simulators see the same instructions
MAX_INSTRS = {
    "fibonacci": 320,
    "matmul": 840,
    "linkedlist": 580,
    "qsort": 1360,
    "dhrystone": 700,
    "memcpy_bench": 1510,
}

BENCHMARKS = list(MAX_INSTRS.keys())


# ---------------------------------------------------------------------------
# Simulator runners
# ---------------------------------------------------------------------------

def run_our_sim(sim_path: str, elf_path: str, output_dir: str) -> dict:
    """Run our ARM CPU simulator with gem5_o3_arm_v7a preset."""
    os.makedirs(output_dir, exist_ok=True)
    basename = os.path.splitext(os.path.basename(elf_path))[0]

    cmd = [
        sim_path,
        "-f", "elf",
        "--preset", "gem5_o3_arm_v7a",
        "-j",  # JSON output
        "-o", os.path.join(output_dir, f"{basename}_sim.json"),
        elf_path,
    ]

    print(f"  [Our Sim] Running: {' '.join(cmd)}")
    start = time.perf_counter()
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    elapsed = time.perf_counter() - start

    if result.returncode != 0:
        print(f"  [Our Sim] STDERR: {result.stderr[-500:]}")
        return {"error": f"exit code {result.returncode}"}

    # Parse JSON output from stderr or stdout
    output = result.stdout + result.stderr
    return parse_sim_output(output, elapsed)


def parse_sim_output(output: str, elapsed: float) -> dict:
    """Parse our simulator's JSON output.

    The simulator prints JSON in two parts:
      1. print_json_metrics(): { "total_instructions": N, "total_cycles": M, "ipc": X, ... "instructions": {...} }
      2. main():               , "wall_time_ms": Y, "instr_per_sec": Z, "cycles_per_sec": W }
    We use regex to extract key fields directly.
    """
    # Extract total_instructions
    m = re.search(r'"total_instructions"\s*:\s*(\d+)', output)
    instructions = int(m.group(1)) if m else 0

    # Extract total_cycles
    m = re.search(r'"total_cycles"\s*:\s*(\d+)', output)
    cycles = int(m.group(1)) if m else 0

    # Extract ipc
    m = re.search(r'"ipc"\s*:\s*([\d.]+)', output)
    ipc = float(m.group(1)) if m else (instructions / max(cycles, 1))

    # Extract wall_time_ms from simulator output
    m = re.search(r'"wall_time_ms"\s*:\s*([\d.]+)', output)
    wall_ms = float(m.group(1)) if m else (elapsed * 1000)

    if instructions > 0 or cycles > 0:
        return {
            "cycles": cycles,
            "instructions": instructions,
            "ipc": ipc,
            "wall_time_s": wall_ms / 1000.0,
        }

    # Fallback: parse text summary
    cycles_m = re.search(r'Cycles:\s*([\d,]+)', output)
    instr_m = re.search(r'Instructions:\s*([\d,]+)', output)
    ipc_m = re.search(r'IPC:\s*([\d.]+)', output)

    if cycles_m and instr_m:
        cycles = int(cycles_m.group(1).replace(",", ""))
        instrs = int(instr_m.group(1).replace(",", ""))
        ipc = float(ipc_m.group(1)) if ipc_m else instrs / max(cycles, 1)
        return {
            "cycles": cycles,
            "instructions": instrs,
            "ipc": ipc,
            "wall_time_s": elapsed,
        }

    return {"error": "could not parse output", "raw": output[-200:]}


def run_gem5(gem5_path: str, elf_path: str, max_instrs: int, output_dir: str) -> dict:
    """Run GEM5 SE mode with O3_ARM_v7a CPU."""
    os.makedirs(output_dir, exist_ok=True)
    basename = os.path.splitext(os.path.basename(elf_path))[0]

    cmd = [
        gem5_path,
        "--quiet",
        GEM5_CONFIG,
        "--cpu", "o3",
        "--cpu-freq", "2GHz",
        "--mem-size", "256MB",
        "-P", f"system.cpu_cluster.cpus[0].max_insts_all_threads={max_instrs}",
        elf_path,
    ]

    print(f"  [GEM5]    Running: {' '.join(cmd)}")
    start = time.perf_counter()
    result = subprocess.run(
        cmd, capture_output=True, text=True, timeout=600,
        cwd="/Users/mac/storage/gem5",  # GEM5 needs to run from its root
    )
    elapsed = time.perf_counter() - start

    if result.returncode != 0:
        print(f"  [GEM5]    STDERR (last 500): {result.stderr[-500:]}")
        return {"error": f"exit code {result.returncode}"}

    # GEM5 writes stats to m5out/stats.txt
    stats_path = os.path.join("/Users/mac/storage/gem5", "m5out", "stats.txt")
    if os.path.isfile(stats_path):
        with open(stats_path) as f:
            stats_text = f.read()
    else:
        stats_text = result.stdout + result.stderr

    return parse_gem5_stats(stats_text, elapsed)


def parse_gem5_stats(output: str, elapsed: float) -> dict:
    """Parse GEM5 statistics output (from m5out/stats.txt or stdout)."""
    stats = {}

    # GEM5 outputs statistics at the end
    lines = output.split("\n")
    in_stats = False
    for line in lines:
        if "Begin Simulation Statistics" in line:
            in_stats = True
            continue
        if "End Simulation Statistics" in line:
            break
        if in_stats:
            parts = line.split()
            if len(parts) >= 2:
                # GEM5 stats format: "key  value  # comment"
                key = parts[0]
                value = parts[1]
                try:
                    if "e" in value.lower() and "." in value:
                        stats[key] = float(value)
                    elif "." in value:
                        stats[key] = float(value)
                    else:
                        stats[key] = int(value.replace(",", ""))
                except ValueError:
                    stats[key] = value

    # GEM5 v25 uses keys without [0] index for single-core configs
    cycles = stats.get("system.cpu_cluster.cpus.numCycles", 0)
    instructions = stats.get("system.cpu_cluster.cpus.thread_0.numInsts", 0)
    if instructions == 0:
        instructions = stats.get("simInsts", 0)

    ipc = instructions / max(cycles, 1)

    return {
        "cycles": int(cycles),
        "instructions": int(instructions),
        "ipc": ipc,
        "wall_time_s": elapsed,
        "raw_stats": stats,
    }


# ---------------------------------------------------------------------------
# Comparison table
# ---------------------------------------------------------------------------

def print_comparison_table(results: dict):
    """Print a formatted comparison table."""
    # Header
    hdr = (
        f"{'Benchmark':<14} │ "
        f"{'Our Cycles':>11} {'Our IPC':>8} {'Our Time':>10} │ "
        f"{'GEM5 Cycles':>12} {'GEM5 IPC':>9} {'GEM5 Time':>11} │ "
        f"{'Cycle Δ%':>9} {'IPC Δ%':>8} {'Speed Δ%':>9}"
    )
    sep = "─" * len(hdr)

    print(f"\n{'═' * len(hdr)}")
    print("  ARM CPU Emulator  vs  GEM5 O3_ARM_v7a  (gem5_o3_arm_v7a preset)")
    print(f"{'═' * len(hdr)}")
    print(hdr)
    print(sep)

    for bench in BENCHMARKS:
        r = results.get(bench, {})
        sim = r.get("sim", {})
        gem5 = r.get("gem5", {})

        if "error" in sim and "error" in gem5:
            print(f"{bench:<14} │ {'ERROR':^32} │ {'ERROR':^34} │")
            continue

        if "error" in sim:
            print(f"{bench:<14} │ {'ERROR':^32} │ "
                  f"{gem5.get('cycles', '?'):>12} {gem5.get('ipc', '?'):>9.3f} "
                  f"{gem5.get('wall_time_s', 0):>10.2f}s │")
            continue

        if "error" in gem5:
            print(f"{bench:<14} │ "
                  f"{sim.get('cycles', '?'):>11} {sim.get('ipc', '?'):>8.3f} "
                  f"{sim.get('wall_time_s', 0):>10.2f}s │ {'ERROR':^34} │")
            continue

        sim_cyc = sim.get("cycles", 0)
        sim_ipc = sim.get("ipc", 0)
        sim_t = sim.get("wall_time_s", 0)
        g5_cyc = gem5.get("cycles", 0)
        g5_ipc = gem5.get("ipc", 0)
        g5_t = gem5.get("wall_time_s", 0)

        # Compute deltas (our sim vs GEM5)
        cyc_delta = ((sim_cyc - g5_cyc) / max(g5_cyc, 1)) * 100
        ipc_delta = ((sim_ipc - g5_ipc) / max(g5_ipc, 0.001)) * 100
        # Speed = time ratio (lower is faster for our sim)
        if g5_t > 0.001:
            speed_delta = ((sim_t - g5_t) / g5_t) * 100
        else:
            speed_delta = 0

        def sign(v):
            return f"+{v:.1f}" if v >= 0 else f"{v:.1f}"

        print(
            f"{bench:<14} │ "
            f"{sim_cyc:>11,} {sim_ipc:>8.3f} {sim_t:>9.3f}s │ "
            f"{g5_cyc:>12,} {g5_ipc:>9.3f} {g5_t:>10.3f}s │ "
            f"{sign(cyc_delta):>8}% {sign(ipc_delta):>7}% {sign(speed_delta):>8}%"
        )

    print(sep)
    print(f"{'═' * len(hdr)}")
    print("\n  Δ% = (Our Sim − GEM5) / GEM5 × 100")
    print("  Positive Δ% means our sim reports higher values (more cycles / higher IPC / slower)")


def save_results_json(results: dict, path: str):
    """Save full results as JSON."""
    clean = {}
    for bench, r in results.items():
        clean[bench] = {
            "sim": {k: v for k, v in r.get("sim", {}).items() if k != "raw_stats"},
            "gem5": {k: v for k, v in r.get("gem5", {}).items() if k != "raw_stats"},
        }
    with open(path, "w") as f:
        json.dump(clean, f, indent=2)
    print(f"\n  Full results saved to: {path}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description="Compare ARM CPU Emulator vs GEM5")
    parser.add_argument("--gem5", default=DEFAULT_GEM5, help="Path to gem5.opt binary")
    parser.add_argument("--sim", default=DEFAULT_SIM, help="Path to arm_cpu_sim binary")
    parser.add_argument("--benchmarks-dir", default=DEFAULT_BENCHMARKS_DIR)
    parser.add_argument("--output-dir", default=OUTPUT_DIR)
    parser.add_argument("--benchmarks", nargs="*", default=BENCHMARKS,
                        help="Benchmarks to run (default: all)")
    parser.add_argument("--skip-gem5", action="store_true", help="Only run our simulator")
    parser.add_argument("--skip-sim", action="store_true", help="Only run GEM5")
    args = parser.parse_args()

    # Verify binaries exist
    if not os.path.isfile(args.sim):
        print(f"Error: Simulator not found at {args.sim}")
        sys.exit(1)
    if not args.skip_gem5 and not os.path.isfile(args.gem5):
        print(f"Error: GEM5 not found at {args.gem5}")
        print("  Build with: cd /Users/mac/storage/gem5 && scons build/ARM/gem5.opt")
        sys.exit(1)

    os.makedirs(args.output_dir, exist_ok=True)
    results = {}

    for bench in args.benchmarks:
        print(f"\n{'─' * 60}")
        print(f"  Benchmark: {bench}")
        print(f"{'─' * 60}")
        results[bench] = {}

        elf_path = os.path.join(args.benchmarks_dir, f"{bench}_gem5_se")
        if not os.path.isfile(elf_path):
            print(f"  SKIP: {elf_path} not found")
            continue

        # Run our simulator
        if not args.skip_sim:
            results[bench]["sim"] = run_our_sim(args.sim, elf_path, args.output_dir)

        # Run GEM5
        if not args.skip_gem5:
            max_instrs = MAX_INSTRS.get(bench, 10000)
            results[bench]["gem5"] = run_gem5(args.gem5, elf_path, max_instrs, args.output_dir)

    # Print comparison
    print_comparison_table(results)
    save_results_json(results, os.path.join(args.output_dir, "comparison_results.json"))


if __name__ == "__main__":
    main()
