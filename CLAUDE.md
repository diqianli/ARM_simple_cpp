# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ARMv8 out-of-order CPU timing simulator (ESL-level), ported from Rust to C++20. Simulates instruction pipelines, cache hierarchies, and CHI interconnect protocols with Konata visualization export.

## Build Commands

```bash
# Release build (default)
./scripts/build.sh

# Debug build
./scripts/build.sh Debug

# Manual CMake
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Binary: `build/arm_cpu_sim`

## Running Tests

```bash
# All unit tests
cd build && ctest --output-on-failure

# Single test binary
./build/test_config
./build/test_ooo
./build/test_simulation

# Full validation (build + unit tests + sim + stress)
./scripts/test_all.sh
./scripts/test_all.sh --fast   # skip stress tests
```

## Running Simulations

```bash
# ELF input (recommended, full ISA coverage via Capstone)
./build/arm_cpu_sim -f elf tests/data/test_elf_aarch64

# Text trace input (limited ~40 mnemonics, no Capstone needed)
./build/arm_cpu_sim -f text tests/data/text_trace_basic.txt

# Custom output path
./build/arm_cpu_sim -f elf program.elf -o result.json

# SimulationEngine mode (event-based, no Konata export)
./build/arm_cpu_sim -e -f elf program.elf

# JSON metrics output
./build/arm_cpu_sim -f elf program.elf -j

# Custom CPU config
./build/arm_cpu_sim -f elf program.elf --window-size 256 --issue-width 6 --l1-size 128
```

## Architecture

### Two Simulation Paths

- **CPUEmulator** (`cpu.hpp`): Full path with CHI interconnect, Konata visualization export, and trace output. Default mode.
- **SimulationEngine** (`simulation/simulation_engine.hpp`): Event-based path emitting `SimulationEvent` to pluggable sinks (`SimulationEventSink`). No Konata export. Enabled with `-e`.

Both share the same OoO engine, memory subsystem, and stats collector.

### Pipeline Flow

`InstructionSource` (trace input) -> Fetch/Dispatch -> **OoOEngine** (rename, issue, execute) -> **MemorySubsystem** (LSQ, L1/L2/L3, DDR) -> Commit -> Stats/Visualization

### Key Subsystems

| Subsystem | Header | Role |
|-----------|--------|------|
| OoO Engine | `ooo/ooo_engine.hpp` | Instruction window, dependency tracking, reorder buffer |
| Memory | `memory/memory_subsystem.hpp` | L1/L2/L3 caches, LSQ, DDR controller |
| CHI | `chi/chi_manager.hpp` | AMBA CHI protocol (RNF, HNF, SNF nodes) |
| Decoder | `decoder/capstone_decoder.hpp` | Capstone v5 ARM64 disassembly (~500 mnemonics) |
| Input | `input/instruction_source.hpp` | Factory `create_source()` dispatching to format-specific parsers |
| Stats | `stats/stats_collector.hpp` | IPC, CPI, cache hit rates, MPKI, instruction mix |
| Visualization | `visualization/visualization_state.hpp` | Konata JSON export for pipeline timeline viewer |
| Analysis | `analysis/topdown.hpp` | TopDown microarchitectural analysis |

### Input Format -> Decoder Mapping

| `-f` format | Decoder | ISA Coverage |
|-------------|---------|-------------|
| `elf` | `CapstoneDecoder` | ~500 mnemonics (ARMv8 + SVE/SME/Crypto) |
| `text` | `TextTraceParser` | ~40 mnemonics (integer, load/store, branch, basic FP) |
| `champsim` | ChampSim trace parser | ChampSim format |
| `champsim_xz` | ChampSim + liblzma | Compressed ChampSim |

### Error Handling Pattern

Uses Rust-style `Result<T>` (wrapping `std::variant<T, EmulatorError>`) with a `TRY()` macro for early return on error. See `error.hpp`.

```cpp
Result<Instruction> parse_next() {
    auto data = TRY(read_bytes());  // returns Err on failure
    return Ok(decode(data));
}
```

### Key Type Conventions

- `OpcodeType` enum in `types.hpp`: instruction classification (Add, Load, SveAdd, etc.)
- `Instruction` struct: uses builder pattern (`with_src_reg()`, `with_mem_access()`)
- `Reg` (X0-X30), `VReg` (V0-V31), `PReg` (P0-P15) for ARM register files
- `StaticVector<T, N>`: stack-allocated small vector (avoids heap allocation)
- `CPUConfig`: all microarchitectural parameters, with `default_config()`, `high_performance()`, `minimal()` presets

## Dependencies

All fetched via CMake FetchContent (no manual install needed):
- nlohmann/json v3.11.3, spdlog v1.14.1, GoogleTest v1.14.0
- ankerl::unordered_dense v4.4.0
- Capstone v5 (ARM64 disassembly, optional — enabled by default)

Optional system dependency: liblzma (ChampSim XZ support).

## Visualization

Konata pipeline timeline viewer at `tools/viz_server/index.html`. Open in browser, load the JSON output file.

## Important Rules

- Always use ELF input (`-f elf`) for simulations unless text trace is explicitly needed for debugging
- Compile test ELF programs with `aarch64-elf-gcc` (use `scripts/compile_test_elf.sh`)
- When reporting simulation results, always specify input file path and output file path
- The project is ported from Rust; some patterns (Result<T>, factory functions) follow Rust conventions
