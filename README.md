# ARM CPU Emulator

ARMv8 时序模拟器（ESL 级别），支持乱序执行（Out-of-Order）、缓存层次建模、Konata 管线可视化。

## 特性

- **乱序执行引擎** — 可配置的指令窗口、发射/提交宽度、重排序缓冲区
- **缓存层次建模** — L1/L2/L3 缓存，支持 hit/miss 统计和 MPKI 计算
- **FunctionalSim 完美分支预测** — 预执行 ELF 生成动态 trace，所有分支在 trace 阶段即已解析，管线模拟器不会遇到预测错误的分支
- **多格式输入** — text, binary, json, champsim, champsim_xz, **elf**（推荐）
- **Capstone v5 解码** — 支持 ARMv8 基础指令集 + SVE/SVE2/SME 扩展
- **Konata 管线可视化** — 导出 JSON 格式（含正确 PC 地址），配套交互式管线时间线查看器
- **GEM5 风格性能分析** — IPC/Cache/分支预测/管线停顿的区间采样时间序列，含交互式图表仪表板
- **仿真速度实时采样** — 每秒采样真实墙钟时间指令吞吐量，导出为 JSON 供前端渲染
- **Benchmark 套件** — 内置 Dhrystone 等基准测试 ELF + 循环回放机制，支持任意指令数量仿真
- **多实例并行** — 支持多核仿真场景并行运行
- **零手动依赖** — 所有第三方库通过 CMake FetchContent 自动下载

## 环境要求

| 依赖 | 最低版本 | 说明 |
|------|----------|------|
| CMake | >= 3.20 | 构建系统 |
| C++ 编译器 | C++20 | GCC 11+ / Clang 14+ / Apple Clang 15+ |
| Git | 任意 | FetchContent 下载依赖 |
| **网络** | — | 首次构建需联网下载第三方库（~200MB） |

**可选依赖：**

| 依赖 | 说明 |
|------|------|
| aarch64-elf-gcc 或 aarch64-linux-gnu-gcc | 交叉编译测试 ELF 程序（仅 ELF 输入需要） |
| liblzma | ChampSim XZ 压缩 trace 支持（大多数系统已安装） |

## 快速开始

```bash
# 1. 克隆仓库
git clone <repo-url>
cd arm_cpu_emulator_cpp

# 2. 构建
./scripts/build.sh

# 3. 全量验证（一条命令完成构建 + 单元测试 + 仿真测试 + 压力测试）
./scripts/test_all.sh

# 4. 运行仿真
./build/arm_cpu_sim -f elf tests/data/test_elf_aarch64
```

**在其他机器上复现**，只需上面 4 步。构建脚本会自动下载所有依赖，`test_all.sh` 会逐项报告哪一步失败。详见 [全量自动测试](#全量自动测试)。

## 指令解码架构（重要）

### FunctionalSim — 完美分支预测引擎

ELF 输入路径使用 **FunctionalSim**（`src/input/functional_sim.cpp`）对 ELF 二进制进行**预执行**，在 trace 生成阶段完成所有分支解析。这是本项目分支预测的核心机制。

**工作流程：**

```
ELF 文件 → ElfLoader 加载 → FunctionalSim 预执行 → 动态指令 trace → 管线模拟器
                              (PC-based, 逐条执行)
```

**FunctionalSim 的作用：**

1. **PC-based 逐条执行** — 从 ELF 入口点开始，逐条解码、执行每条指令，维护完整的 ARM64 寄存器状态（X0-X30, V0-V31, SP, PC, NZCV 标志）
2. **完美分支解析** — 所有条件分支（B.cond）根据实际 NZCV 标志计算跳转方向，无条件分支直接跳转，**不会产生错误路径的指令**
3. **无限循环检测** — 检测到同一 PC 连续执行超过阈值时自动停止，防止 `while(1)` 导致无限执行
4. **生成动态 trace** — 输出的是程序实际执行路径的指令序列（而非静态二进制的线性扫描），每条指令携带正确的 PC 地址

**支持的指令（FunctionalSim 层）：**

FunctionalSim 需要实际执行指令来推进 PC 和寄存器状态，目前支持的指令子集：

| 类别 | 指令 |
|------|------|
| 整数运算 | ADD, SUB, MUL, SDIV, UDIV, AND, ORR, EOR, LSL, LSR, ASR, MOV, MOVZ, MOVK |
| 比较 | CMP, CMN, CCMP |
| 条件选择 | CSEL, CSET, CSETM |
| 访存 | LDR, STR, LDP, STP, LDUR, STUR, LDURSW, LDRSW, LDXR, STXR |
| 分支 | B, BL, BR, B.cond (全条件), CBZ, CBNZ, TBZ, TBNZ, RET |
| 浮点 | FADD, FSUB, FMUL, FDIV, FMOV, FCMP, FCVT |
| NEON/SIMD | DUP, INS, MOV (vector), FADD (vector), EXT |
| 屏障 | DMB, DSB, ISB, NOP |
| 系统 | MSR, MRS, MRS (NZCV), SVC |

> **注意**：FunctionalSim 不支持的指令会被跳过（PC+4），但 Capstone 解码器仍会正确识别其助记符和操作数。这意味着管线模拟器能看到这些指令的名称和寄存器依赖，但 FunctionalSim 不会实际执行它们。对于不包含自修改代码或复杂控制流的测试程序，这通常不影响仿真结果。

**验证分支预测正确性：**

项目包含一个专门的分支验证测试（`tests/data/branch_verify_source.c`），通过 3 个模式验证分支解析：

```bash
# 编译（需要 aarch64-elf-gcc）
aarch64-elf-gcc -static -O0 -nostdlib -o tests/data/branch_verify_aarch64 tests/data/branch_verify_source.c

# 运行仿真
./build/arm_cpu_sim -f elf tests/data/branch_verify_aarch64
```

| 模式 | 分支类型 | 条件 | 预期行为 |
|------|----------|------|----------|
| Pattern 1 | B.LE (条件) | x19=10 > v3=5 → LE=false → **NOT taken** | MUL 出现在 trace，SUB 不出现 |
| Pattern 2 | B.LE (条件) | x20=3 ≤ v4=100 → LE=true → **TAKEN** | AND 出现在 trace，ORR 不出现 |
| Pattern 3 | B (无条件) | 直接跳转 | NOP 不出现在 trace |

验证方法：检查 Konata JSON 输出中错误路径指令（SUB, ORR, NOP）是否被完全排除。

### 两条独立的指令解码路径

本项目有两条独立的指令解码路径，指令覆盖范围不同：

### 输入格式与解码器对应关系

| 输入格式 | 解码器 | Capstone | 分支处理 | 指令覆盖 |
|----------|--------|----------|----------|----------|
| `-f elf` | `CapstoneDecoder` + `FunctionalSim` | **必须** | FunctionalSim 完美预测 | ~500+ 助记符 |
| `-f text` | `TextTraceParser`（`src/input/text_trace.cpp`） | **不使用** | 静态顺序扫描 | ~40 助记符 |

### ELF 格式（`-f elf`）— 完整指令覆盖

ELF 输入的处理流程：

1. **ElfLoader**（`src/elf/elf_loader.cpp`）加载 ELF 文件，解析 program headers，映射代码段和数据段到内存
2. **FunctionalSim**（`src/input/functional_sim.cpp`）从入口点开始 PC-based 预执行，生成动态指令 trace（含完美分支解析）
3. **CapstoneDecoder**（`src/decoder/capstone_decoder.cpp`）对每条指令进行解码，映射为 `OpcodeType`，提取寄存器操作数和内存访问信息

指令覆盖范围：

- ARMv8.0-A 基础整数指令（ADD, SUB, MUL, LDR, STR, B, ...）
- ARMv8.0-A 浮点与 NEON/SIMD（FADD, VLD, VMLA, FCVT, ...）
- ARMv8.1+ 原子操作（CAS, LDADD, LDSET, LDCLR, SWP, ...）
- ARMv8.4+ SVE 扩展（sve_add, sve_ld1, sve_sel, sve_zip, ...）
- ARMv8.6+ SVE2 扩展（sve_sdot, sve_umlal, ...）
- ARMv8.7+ SME 扩展（sme_fmopa, sme_ld1, smstart, ...）
- ARMv8.5+ Crypto 扩展（AESD, AESE, SHA1H, SHA256H, PMULL, ...）
- Cache 维护与屏障（DC ZVA, DC CIVAC, IC IVAU, DMB, DSB, ISB）
- 系统寄存器（MSR, MRS, SYS）
- 条件选择（CSEL, CSET, CSETM）

助记符映射表定义在 `src/decoder/capstone_decoder.cpp` 的 `kArm64Mappings[]` 中。
对于表中未覆盖的助记符，Capstone 仍能正确反汇编但会映射为 `OpcodeType::Other`。

### Text 格式（`-f text`）— 有限子集

Text trace 输入使用内置的文本解析器，**不经过 Capstone**，仅支持以下助记符：

```
整数:  ADD, ADDS, ADC, SUB, SUBS, SBC, MUL, SMULL, UMULL, DIV, SDIV, UDIV
逻辑:  AND, ANDS, ORR, EOR, XOR
移位:  LSL, LSR, ASR
访存:  LDR, LDUR, LDP, LDXR, STR, STUR, STP, STXR, LDPSW, LDRSW
分支:  B, BL, BR, RET, B.EQ, B.NE, B.LT, B.GT, B.LE, B.GE,
       B.HI, B.LS, B.CC, B.CS, B.PL, B.MI, CBZ, CBNZ, TBZ, TBNZ
系统:  MSR, MRS, SYS, NOP, YIELD, WFE, WFI
浮点:  FADD, FSUB, FMUL, FDIV
```

**不支持的类别**：NEON/SIMD、SVE、SME、Crypto、Atomic、FMA、CSEL、Cache 维护等。

助记符映射定义在 `src/input/text_trace.cpp` 的 `parse_opcode_type()` 中。
Text trace 格式说明见下文 [Text Trace 格式](#text-trace-格式)。

### 如何选择输入格式

- **需要完整指令集覆盖 + 正确分支行为** → 用 `-f elf`（FunctionalSim 完美分支预测，需要交叉编译测试程序）
- **快速调试管线行为** → 用 `-f text`（手写 trace，不依赖交叉编译，但指令集有限且无分支解析）
- **两者都跑** → `./scripts/test_all.sh` 会分别测试两种格式

### Capstone 编译依赖

Capstone 通过 CMake FetchContent 自动下载（v5，含 AArch64 支持），无需手动安装。
如果 FetchContent 失败（网络问题），构建会报链接错误——此时 ELF 输入不可用，但 text 输入仍可正常使用。
CMake 编译宏 `HAS_CAPSTONE=1` 仅用于标记是否成功链接 Capstone，代码中无运行时条件分支。

#### FetchContent 失败时手动安装 Capstone

如果目标机器无法访问 GitHub（国内网络、离线环境等），可手动安装 Capstone 后让 CMake 使用系统版本：

**步骤 1 — 下载 Capstone v5 源码**

```bash
# 方式 A：从 GitHub 下载（需要网络）
git clone --depth 1 --branch v5 https://github.com/capstone-engine/capstone.git

# 方式 B：从 GitHub Release 页面下载 ZIP
# 访问 https://github.com/capstone-engine/capstone/releases/tag/v5
# 下载 capstone-v5.zip 并解压

# 方式 C：通过代理/镜像下载（如果 GitHub 直连失败）
git clone --depth 1 --branch v5 https://ghproxy.com/https://github.com/capstone-engine/capstone.git
```

**步骤 2 — 编译安装 Capstone**

```bash
cd capstone
mkdir build && cd build

# Linux / macOS
cmake .. -DCAPSTONE_AARCH64_SUPPORT=ON \
         -DCAPSTONE_BUILD_TESTS=OFF \
         -DCAPSTONE_BUILD_CSTOOL=OFF
make -j$(nproc)
sudo make install

# Windows (MSVC)
cmake .. -DCAPSTONE_AARCH64_SUPPORT=ON ^
         -DCAPSTONE_BUILD_TESTS=OFF ^
         -DCAPSTONE_BUILD_CSTOOL=OFF ^
         -DCMAKE_INSTALL_PREFIX=C:\capstone
cmake --build . --config Release
cmake --install .
```

**步骤 3 — 使用系统 Capstone 构建仿真器**

安装完成后，禁用 FetchContent，让 CMake 使用系统已安装的版本：

```bash
# Linux / macOS
cd arm_cpu_emulator_cpp
mkdir build && cd build
cmake .. -DUSE_CAPSTONE=OFF \
         -DCMAKE_PREFIX_PATH=/usr/local  # 如果安装到非默认路径
make -j$(nproc)

# Windows (MSVC)
cmake .. -DUSE_CAPSTONE=OFF ^
         -DCMAKE_PREFIX_PATH=C:\capstone
cmake --build . --config Release
```

> **注意**：`-DUSE_CAPSTONE=OFF` 禁用 FetchContent 自动下载，但只要系统中安装了 Capstone（`capstone/capstone.h` 头文件 + `libcapstone` 库），构建仍会自动找到并链接它。如果找不到，构建会成功但 ELF 输入不可用（仅 text 输入可用）。

**验证 Capstone 是否启用**：构建或运行仿真器时，如果 Capstone 成功链接，启动会打印：

```
Capstone: initialized AArch64 disassembler (SVE/SME enabled)
```

未启用时不会打印此行。

## 自动下载的依赖

所有依赖通过 CMake FetchContent 自动下载和构建，无需手动安装：

| 库 | 版本 | 用途 |
|----|------|------|
| nlohmann/json | v3.11.3 | JSON 解析/生成 |
| spdlog | v1.14.1 | 日志输出 |
| GoogleTest | v1.14.0 | 单元测试 |
| ankerl::unordered_dense | v4.4.0 | 高性能哈希表 |
| Capstone | v5 | ARM64 指令解码（仅 ELF 输入使用） |

## Text Trace 格式

Text trace 文件是纯文本，每行一条指令，支持两种格式：

### 格式 1 — 反汇编风格（推荐）

```
0x400000: ADD X0, X1, X2
0x400004: SUB X3, X4, X5
0x400008: LDR X6, [X7, #16]
0x40000c: B 0x400020
0x400010: B.EQ 0x400030
```

- `PC:` 后跟助记符和操作数
- 助记符大小写不敏感
- 内存操作自动识别 `[Xn, #offset]` 模式
- 分支目标从立即数提取

### 格式 2 — 结构化格式

```
0x400000 ADD R:1,2 W:0
0x400004 LDR R:0,1 W:2 mem=0x1000 size=8
0x400008 B target=0x400020 taken=true
```

- 显式指定源寄存器（`R:`/`X:`/`src=`）和目标寄存器（`W:`/`dst=`）
- 内存地址（`mem=`/`addr=`）和分支目标（`target=`/`br=`）可精确控制

### 格式通用规则

- `#` 开头的行为注释，空行自动跳过
- 地址格式：`0x400000` 或 `0X400000`
- 完整支持的助记符列表见 [Text 格式（`-f text`）— 有限子集](#text-格式--f-text----有限子集)

## 仿真器使用

### 基本用法

```bash
# ELF 输入（推荐，完整指令集覆盖）
./build/arm_cpu_sim -f elf tests/data/test_elf_aarch64

# Text 输入（快速调试，有限指令集）
./build/arm_cpu_sim -f text tests/data/text_trace_basic.txt

# 指定输出路径
./build/arm_cpu_sim -f elf program.elf -o result.json

# 限制指令数 + verbose 输出
./build/arm_cpu_sim -f elf program.elf -n 100 -v
```

### CPU 配置

```bash
# 自定义窗口大小和发射宽度
./build/arm_cpu_sim -f elf program.elf --window-size 256 --issue-width 6

# 自定义缓存大小（单位 KB）
./build/arm_cpu_sim -f elf program.elf --l1-size 128 --l2-size 1024 --l3-size 16384

# 设置 CPU 频率
./build/arm_cpu_sim -f elf program.elf --frequency 3000
```

### JSON 指标输出

```bash
./build/arm_cpu_sim -f elf program.elf -j
```

输出示例（`*_perf.json`）：
```json
{
  "total_instructions": 1234,
  "total_cycles": 567,
  "ipc": 2.176369,
  "cpi": 0.459484,
  "l1_hit_rate": 0.950000,
  "l2_hit_rate": 0.800000,
  "wall_time_ms": 2750,
  "instr_per_sec": 448727,
  "time_series": {
    "sample_interval_cycles": 1000,
    "samples": [
      {"cycle_start": 0, "ipc": 2.1, "cache_miss_rate": 0.05, ...},
      {"cycle_start": 1000, "ipc": 2.3, "cache_miss_rate": 0.03, ...}
    ]
  },
  "wall_time_series": {
    "sample_interval_sec": 1.0,
    "samples": [
      {"wall_time_sec": 1.0, "total_instructions": 450000, "instr_per_sec": 450000},
      {"wall_time_sec": 2.0, "total_instructions": 900000, "instr_per_sec": 450000}
    ]
  },
  "instructions": { ... }
}
```

### 多实例并行

```bash
# 运行 4 个实例，使用 8 线程
./build/arm_cpu_sim -m 4 -t 8 -f elf program.elf
```

### CLI 参数完整列表

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `-f, --format <fmt>` | Trace 格式：text, binary, json, champsim, champsim_xz, elf | text |
| `-n, --max-instr <n>` | 最大模拟指令数（0 = 无限制） | 0 |
| `-s, --skip <n>` | 跳过前 N 条指令 | 0 |
| `-c, --max-cycles <n>` | 最大模拟周期数 | 1000000000 |
| `-e, --engine` | 使用 SimulationEngine（替代 CPUEmulator） | off |
| `-v, --verbose` | 详细输出 | off |
| `-j, --json` | JSON 格式输出性能指标 | off |
| `--window-size <n>` | 指令窗口大小 | 128 |
| `--fetch-width <n>` | 取指宽度 | 8 |
| `--issue-width <n>` | 发射宽度 | 4 |
| `--commit-width <n>` | 提交宽度 | 4 |
| `--l1-size <kb>` | L1 缓存大小 (KB) | 64 |
| `--l2-size <kb>` | L2 缓存大小 (KB) | 512 |
| `--l3-size <kb>` | L3 缓存大小 (KB) | 8192 |
| `--frequency <mhz>` | CPU 频率 (MHz) | 2000 |
| `-m, --multi <n>` | 并行运行 N 个实例 | 1 |
| `-t, --threads <n>` | 线程数（默认硬件并发数） | auto |
| `-o, --output <file>` | Konata JSON 输出路径 | output/\<stem\>_YYYYMMDD_HHMM.json |
| `--save-trace <file>` | 保存执行 trace | - |
| `-h, --help` | 显示帮助 | - |
| `--version` | 显示版本 | - |

## 输出说明

仿真运行后会产生三类输出：

1. **终端摘要** — IPC、CPI、缓存命中率、指令分布等性能指标
2. **Konata JSON 文件** — 默认保存到 `output/<输入文件名>_<时间戳>.json`（管线时间线数据）
3. **性能统计 JSON** — 保存到 `output/profiling/<输入文件名>_<时间戳>_perf.json`（含时间序列和墙钟采样数据）

## 可视化

使用配套的可视化工具查看仿真结果：

1. 用浏览器打开 `tools/viz_server/index.html`
2. 点击 **"Load JSON File"** 上传仿真输出的 JSON 文件
3. **Pipeline Timeline 标签页** — 管线时间线自动渲染，显示每条指令在 Fetch → Decode → Rename → Dispatch → Issue → Execute → Commit 各阶段的执行情况
4. **Performance Stats 标签页** — 上传性能统计 JSON（`*_perf.json`），查看：
   - **汇总卡片** — IPC、分支误预测率、Cache Miss 率、停顿率、总指令数、总仿真时间
   - **时间序列折线图** — IPC / Cache Miss Rate / Branch Mispred Rate / Stall Rate 随仿真周期变化（每 1000 cycle 采样一次）
   - **仿真速度柱状图** — 每秒真实时间指令吞吐量（Instructions/sec），含平均值参考线

**性能统计 JSON 输出路径**：`output/profiling/<输入文件名>_<时间戳>_perf.json`

**拖拽上传**：也可以直接将 JSON 文件拖放到页面上，自动识别管线 JSON 或性能统计 JSON。

## 编译测试 ELF

如果需要自行编译测试输入（用于验证仿真器）：

```bash
# 使用编译脚本（自动检测交叉编译器）
./scripts/compile_test_elf.sh

# 或手动指定编译器
./scripts/compile_test_elf.sh aarch64-linux-gnu-gcc

# 查看源码
cat scripts/test_elf_source.c
```

输出文件：`tests/data/test_elf_aarch64`

## 项目结构

```
arm_cpu_emulator_cpp/
├── CMakeLists.txt              # 构建配置
├── README.md                   # 本文件
├── scripts/
│   ├── build.sh                # 一键构建脚本
│   ├── test_all.sh             # 全量测试脚本（构建 + 单元测试 + 仿真 + 压力测试）
│   ├── compile_test_elf.sh     # 编译测试 ELF
│   └── test_elf_source.c       # 测试 ELF 源码
├── include/arm_cpu/
│   ├── cpu.hpp                 # CPUEmulator 主类
│   ├── config.hpp              # CPUConfig 默认配置
│   ├── types.hpp               # 核心数据类型（OpcodeType, Instruction 等）
│   ├── decoder/
│   │   ├── capstone_decoder.hpp # Capstone 解码器（ELF 路径，~500 助记符）
│   │   └── aarch64_decoder.hpp # DecodedInstruction 数据结构
│   ├── input/
│   │   ├── instruction_source.hpp # 输入源工厂（create_source 分发）
│   │   ├── text_trace.hpp      # Text trace 解析器（~40 助记符）
│   │   ├── elf_trace.hpp       # ELF trace 解析器（调用 FunctionalSim + Capstone）
│   │   └── functional_sim.hpp  # FunctionalSim 完美分支预测引擎
│   ├── ooo/                    # 乱序执行引擎
│   ├── memory/                 # 内存子系统与缓存层次
│   ├── simulation/             # SimulationEngine
│   ├── stats/                  # 性能指标收集
│   ├── visualization/          # Konata 导出
│   └── multi_instance/         # 多实例并行管理
├── src/                        # 实现文件
├── tests/
│   ├── test_config.cpp         # 配置测试
│   ├── test_ooo.cpp            # 乱序引擎测试
│   ├── test_types.cpp          # 类型测试
│   ├── test_memory.cpp         # 内存子系统测试
│   ├── test_elf_e2e.cpp        # ELF 端到端测试（需要 Capstone + 交叉编译 ELF）
│   ├── test_simulation.cpp     # Text trace 仿真集成测试（不依赖交叉编译）
│   └── data/
│       ├── test_elf_aarch64          # 预编译测试 ELF（ALU/访存/分支/FP/SIMD 混合）
│       ├── complex_test_aarch64      # 复杂指令混合测试 ELF
│       ├── branch_verify_aarch64     # 分支预测验证 ELF（3 种分支模式）
│       ├── branch_verify_source.c    # 分支验证源码
│       ├── text_trace_basic.txt      # 基础指令混合（20 条）
│       ├── text_trace_memory.txt     # 内存密集型（50 条）
│       ├── text_trace_branches.txt   # 分支密集型（30 条）
│       └── text_trace_long.txt       # 长序列压力测试（200+ 条）
├── tools/
│   └── viz_server/             # 可视化工具（管线时间线 + 性能统计）
│       ├── index.html
│       ├── style.css
│       ├── pipeline_view.js
│       ├── perf_stats_view.js  # 性能统计图表（Chart.js）
│       ├── app_static.js
│       └── konata/             # Konata 渲染器
├── benchmarks/                 # 基准测试 ELF（dhrystone 等）
└── output/                     # 仿真输出（.gitignore）
```

## 运行测试

### 单元测试

```bash
cd build && ctest --output-on-failure
```

测试文件：
- `test_config.cpp` — CPU 配置默认值与自定义
- `test_ooo.cpp` — 乱序引擎依赖检测、重排序缓冲区
- `test_types.cpp` — 指令类型、操作码映射
- `test_memory.cpp` — 缓存层次模拟
- `test_elf_e2e.cpp` — ELF 端到端测试（需要 Capstone + 交叉编译 ELF）
- `test_simulation.cpp` — Text trace 仿真集成测试（不依赖交叉编译）

### 全量自动测试

使用 `scripts/test_all.sh` 一键运行所有测试，包括构建、单元测试、多输入仿真和多配置压力测试：

```bash
./scripts/test_all.sh              # 全量测试（含压力测试）
./scripts/test_all.sh --fast       # 跳过压力测试（快速模式，~30 秒）
```

测试阶段：

| 阶段 | 内容 | 依赖交叉编译 |
|------|------|:---:|
| BUILD | cmake configure + build | - |
| UNIT TESTS | ctest 全部单元测试 | 部分（test_elf_e2e） |
| TEXT TRACE SIM | 用 text trace 运行仿真 | 否 |
| ELF SIM | 用 ELF 运行仿真 | 是 |
| MULTI-CONFIG | 不同 CPU 配置组合（minimal / high-perf / tiny-cache / large-cache） | 否 |
| STRESS | 长序列和大指令数压力测试 | 否 |

每个阶段独立运行，失败不中断后续阶段，最终汇总 PASS/FAIL。
退出码：全部通过 = 0，有失败 = 1。

### 在其他机器上复现

```bash
# 克隆
git clone <repo-url>
cd arm_cpu_emulator_cpp

# 构建 + 全量验证
./scripts/test_all.sh

# 如果 ELF SIM 阶段 SKIP（缺少交叉编译器），可选：
./scripts/compile_test_elf.sh      # 自动检测或手动指定编译器
./scripts/test_all.sh              # 重新运行，ELF SIM 应 PASS
```

`test_all.sh` 的每一步都有明确的 PASS/FAIL/SKIP 输出，能快速定位问题出现在构建、单元测试还是仿真阶段。

## 变更日志

### 2026-04-20 — 墙钟时间采样 + 仿真速度可视化

**新增：仿真速度实时采样（Wall-Clock Time Sampling）**

- `StatsCollector` 新增基于墙钟时间的采样机制，每 1 秒真实时间采样一次当前指令吞吐量
- 新增 `WallTimeSample` 结构体（`performance_metrics.hpp`）：记录 wall_time_sec、total_instructions、total_cycles、instr_per_sec
- 新增 `start_wall_timer()` / `sample_wall_time_if_needed()` / `wall_time_samples()` 接口（`stats_collector.hpp/cpp`）
- `CPUEmulator::advance_cycle()` 中自动调用采样，无需用户干预
- JSON 输出新增 `wall_time_series` 字段，包含 `sample_interval_sec` 和 `samples` 数组

**新增：仿真速度图表**

- `perf_stats_view.js` 新增 `_renderWallTimeChart()` 方法，使用 Chart.js 柱状图渲染每秒指令吞吐量
- 新增 "Total Instructions" 和 "Total Simulation Time" 汇总卡片

**关键文件**：

| 文件 | 改动 |
|------|------|
| `include/arm_cpu/stats/performance_metrics.hpp` | 新增 `WallTimeSample` |
| `include/arm_cpu/stats/stats_collector.hpp` | 新增 wall time 采样方法/成员 |
| `src/stats/stats_collector.cpp` | 实现 `start_wall_timer()` / `sample_wall_time_if_needed()` |
| `src/cpu.cpp` | 主循环调用 wall time 采样 |
| `src/main.cpp` | JSON 序列化 `wall_time_series` |
| `tools/viz_server/perf_stats_view.js` | 仿真速度图表 + 新卡片 |
| `tools/viz_server/index.html` | 新增 `chart-sim-speed` canvas |

### 2026-04-19 — ELF 循环回放 + 时间序列重构 + 管线可视化修复

**新增：ELF Trace 循环回放**

- 当 FunctionalSim 检测到无限循环（程序自然终止）后，ELF trace 解析器自动循环回放已生成的指令序列
- 配合 `-n <指令数>` 参数，可对 dhrystone 等基准测试执行任意数量的指令仿真
- 文件：`src/input/elf_trace.cpp`

**重构：性能统计区间采样时间序列**

- `StatsCollector` 每 1000 个仿真 cycle 采样一次，生成 `IntervalSample`（IPC、cache miss rate、branch mispred rate、stall rate）
- 仿真结束后序列化为 JSON 中的 `time_series` 字段
- 替换原有的 IPC 单点统计，支持前端渲染时间序列折线图
- 文件：`include/arm_cpu/stats/performance_metrics.hpp`（`IntervalSample`）、`src/stats/stats_collector.cpp`

**修复：管线可视化阶段重叠**

- 修复 Issue/Execute 阶段重叠和 1-cycle bubble 问题（`pipeline_tracker.cpp`）
- 修复管线阶段条遮挡指令标签区域的问题（`pipeline_tracker_viz.cpp`）

### 2026-04-18 — GEM5 风格性能分析 + Benchmark 套件

**新增：GEM5 风格性能分析框架**

- `PerformanceMetrics` 结构体（`performance_metrics.hpp`）：完整的微架构性能指标
  - `BranchPredictorMetrics`：分支预测统计（总分支数、误预测数、准确率、BTB/RAS 命中率）
  - `FUUtilization`：功能单元利用率（整数/浮点/访存/分支/NEON 各 FU 的发射数和利用率）
  - `PipelineStallMetrics`：管线停顿分析（ROB/IQ/LSQ 满停顿、cache miss 停顿、分支误预测停顿）
  - `DetailedCacheMetrics`：缓存详细统计（L1/L2 的 read/write/hit/miss/eviction/writeback）
- `main.cpp` 新增 `build_json_metrics_string()` 序列化完整性能指标为 JSON
- 性能统计 JSON 自动保存到 `output/profiling/` 目录

**新增：可视化仪表板**

- `tools/viz_server/index.html` 新增 Performance Stats 标签页
- `perf_stats_view.js`：使用 Chart.js 渲染 4 个时间序列折线图（IPC、Cache Miss Rate、Branch Mispred Rate、Stall Rate）
- 汇总卡片显示关键性能指标

**新增：Benchmark 套件 + Kanata 日志导出**

- `benchmarks/` 目录新增 dhrystone_aarch64 基准测试 ELF
- `kanata_log_exporter.cpp`：导出 Konata 格式日志文件
- `gem5_comparison.html`：gem5 对比分析工具

### 2026-04-17 — 动态 ELF 支持 + 32 位寄存器修复

- `ElfLoader` 新增动态 ELF（DYN 类型）支持
- 修复 FunctionalSim 中 32 位寄存器（W0-W30）处理错误
- 修复 post-index 寻址模式（LDR/STR 带写回）

### 2026-04-16 — FunctionalSim 完美分支预测 + Bug 修复

**新增：FunctionalSim 引擎**

- 新增 `src/input/functional_sim.cpp` / `include/arm_cpu/input/functional_sim.hpp`
- ARM64 功能模拟器，预执行 ELF 二进制生成动态指令 trace
- 实现完整的 ARM64 寄存器文件（X0-X30, V0-V31, SP, PC, NZCV）
- 支持 40+ 种常见指令的实际执行（整数、浮点、访存、分支、NEON、屏障等）
- 完美分支预测：所有条件分支在 trace 阶段已解析，管线模拟器不会看到错误路径指令
- 无限循环自动检测（同一 PC 连续执行超阈值时停止）

**重构：ELF trace 解析**

- `src/input/elf_trace.cpp` 从线性扫描可执行段改为调用 FunctionalSim 生成 PC-based 动态 trace
- 管线模拟器现在只接收程序实际执行路径上的指令

**修复：FunctionalSim MOV 指令 bug**

- `mov x1, x19`（register-to-register MOV，实际上是 `ORR Xd, XZR, Xs`）之前被当作立即数 MOV 处理，`get_imm(1)` 对寄存器操作数返回 0
- 修复：检查 `arch.operands[1].type == ARM64_OP_REG` 区分寄存器 MOV 和立即数 MOVZ
- 影响：修复前所有 register-to-register MOV 的目标寄存器值被错误设为 0，导致后续 CMP/分支判断错误

**修复：Konata JSON PC 字段全为 0**

- `PipelineTrackerViz::export_all_konata_ops()` 中 `KonataOp` 构造时 PC 硬编码为 `0`
- 修复：在 `record_fetch` 中将 `instr.pc` 存入 `pc_map_`，`export_all_konata_ops` 从 `pc_map_` 读取
- 文件：`include/arm_cpu/visualization/pipeline_tracker_viz.hpp`、`src/visualization/pipeline_tracker_viz.cpp`

**新增：Capstone 未映射助记符诊断**

- `src/decoder/capstone_decoder.cpp`：当助记符映射为 `OpcodeType::Other` 时，输出 stderr warning（前 10 种不重复的）

**新增：分支预测验证测试**

- `tests/data/branch_verify_source.c`：3 种分支模式（条件 taken/not-taken + 无条件）
- `tests/data/branch_verify_aarch64`：预编译二进制
- 验证方法：确认错误路径指令不出现在执行 trace 中

**新增：测试基础设施**

- `scripts/build.sh`：一键构建脚本
- `scripts/compile_test_elf.sh`：自动检测/手动指定交叉编译器
- `tests/test_elf_e2e.cpp`：ELF 端到端单元测试
- `tests/data/complex_test_source.c` / `complex_test_aarch64`：复杂指令混合测试

### 2026-04-14 — 项目初始化

- 从 Rust 版本移植到 C++20
- 基础乱序执行引擎、缓存层次、Konata 可视化
- Text trace 输入支持

## 移植指南（目标机器快速集成）

将本项目移植到新的目标机器时，大部分改动集中在少量文件中。以下按模块列出关键集成点。

### 构建系统

| 文件 | 说明 | 改动频率 |
|------|------|:---:|
| `CMakeLists.txt` | 依赖版本、编译选项、目标定义 | 低 |
| `scripts/build.sh` | 构建脚本（cmake 路径、线程数） | 低 |

常见问题：
- **编译器版本不足**：需要 C++20 支持（GCC 11+ / Clang 14+ / Apple Clang 15+）
- **FetchContent 网络失败**：参考 [手动安装 Capstone](#fetchcontent-失败时手动安装-capstone)，使用 `-DUSE_CAPSTONE=OFF` + 系统库
- **链接错误**：检查 `find_package` 路径，可能需要 `-DCMAKE_PREFIX_PATH`

### 性能统计与时间序列

最近的改动引入了两个采样系统，如果需要修改采样行为：

| 采样类型 | 间隔 | 配置位置 | 用途 |
|----------|------|----------|------|
| Cycle-based `IntervalSample` | 每 1000 cycle | `stats_collector.hpp` `kSampleInterval` | IPC/Cache/Branch/Stall 时间序列 |
| Wall-time `WallTimeSample` | 每 1 秒真实时间 | `stats_collector.hpp` `kWallSampleIntervalSec` | 仿真速度（instr/sec）图表 |

**关键文件**：

| 文件 | 角色 |
|------|------|
| `include/arm_cpu/stats/performance_metrics.hpp` | `IntervalSample` / `WallTimeSample` 结构体定义 |
| `include/arm_cpu/stats/stats_collector.hpp` | `StatsCollector` 接口（采样方法、accessor） |
| `src/stats/stats_collector.cpp` | 采样逻辑实现 |
| `src/cpu.cpp` | `advance_cycle()` 调用 `sample_interval()` + `sample_wall_time_if_needed()` |
| `src/main.cpp` | `build_json_metrics_string()` 序列化为 JSON |

**修改采样间隔**：只需改 `stats_collector.hpp` 中的常量值：
```cpp
static constexpr uint64_t kSampleInterval = 1000;          // cycle-based 间隔
static constexpr double kWallSampleIntervalSec = 1.0;      // wall-time 间隔（秒）
```

**JSON 输出格式**：`build_json_metrics_string()` 输出包含 `time_series` 和 `wall_time_series` 两个字段。如果需要增加新的采样指标：
1. 在 `IntervalSample` 或 `WallTimeSample` 中添加字段
2. 在 `sample_interval()` 或 `sample_wall_time_if_needed()` 中计算新值
3. 在 `build_json_metrics_string()` 中序列化新字段

### 前端可视化

| 文件 | 角色 |
|------|------|
| `tools/viz_server/index.html` | 页面结构、canvas 元素、脚本引用 |
| `tools/viz_server/perf_stats_view.js` | 性能统计图表渲染（Chart.js） |
| `tools/viz_server/style.css` | 样式 |
| `tools/viz_server/app_static.js` | 主入口、文件加载、拖拽上传 |

**新增图表**：
1. 在 `index.html` 的 `stats-charts` div 中添加 `<canvas id="chart-xxx">`
2. 在 `perf_stats_view.js` 的 `render()` 方法中调用渲染函数
3. 可参考 `_renderTimeSeriesChart()`（折线图）或 `_renderWallTimeChart()`（柱状图）的实现

**依赖**：Chart.js 通过 CDN 加载（`index.html` 中的 `<script>` 标签），离线环境需下载到本地。

### FunctionalSim 与指令解码

如果目标平台有不同的指令集扩展或需要调整分支预测行为：

| 文件 | 角色 |
|------|------|
| `src/input/functional_sim.cpp` | 功能模拟器实现（指令执行 + 分支解析） |
| `include/arm_cpu/input/functional_sim.hpp` | FunctionalSim 接口 |
| `src/input/elf_trace.cpp` | ELF trace 解析（调用 FunctionalSim + Capstone） |
| `src/decoder/capstone_decoder.cpp` | Capstone 助记符 → OpcodeType 映射表 `kArm64Mappings[]` |

**扩展 FunctionalSim 支持的指令**：在 `functional_sim.cpp` 的 `execute_instruction()` switch 中添加新的 `cs_detail` 判断分支。

**扩展 Capstone 映射**：在 `kArm64Mappings[]` 数组中添加 `{助记符, OpcodeType, 延迟}` 条目。

### ELF 循环回放

对于需要长时间运行的仿真（如 SPEC benchmark），ELF trace 解析器在程序自然终止后自动循环回放已生成的指令序列：

- **配置**：无需额外配置，自动启用
- **使用**：`./build/arm_cpu_sim -f elf benchmarks/dhrystone_aarch64 -n 1000000`
- **文件**：`src/input/elf_trace.cpp`（`ElfTraceParser::parse_next()`）
- **关闭**：如需禁用，在 `elf_trace.cpp` 中将循环回放逻辑注释掉

### 快速移植检查清单

```bash
# 1. 构建
./scripts/build.sh

# 2. 单元测试
cd build && ctest --output-on-failure

# 3. Text trace 仿真（不依赖交叉编译）
./build/arm_cpu_sim -f text tests/data/text_trace_basic.txt -n 100

# 4. ELF 仿真（需要交叉编译器）
./build/arm_cpu_sim -f elf tests/data/test_elf_aarch64 -n 1000

# 5. 性能统计 JSON 输出
./build/arm_cpu_sim -f elf benchmarks/dhrystone_aarch64 -n 100000 -j
# 检查 output/profiling/ 目录下是否生成 *_perf.json
# 确认 JSON 包含 time_series 和 wall_time_series 字段

# 6. 可视化验证
# 用浏览器打开 tools/viz_server/index.html，加载 JSON 文件
# 切换到 Performance Stats 标签页，确认图表正常渲染
```

## 许可证

MIT
