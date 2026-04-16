# ARM CPU Emulator

ARMv8 时序模拟器（ESL 级别），支持乱序执行（Out-of-Order）、缓存层次建模、Konata 管线可视化。

## 特性

- **乱序执行引擎** — 可配置的指令窗口、发射/提交宽度、重排序缓冲区
- **缓存层次建模** — L1/L2/L3 缓存，支持 hit/miss 统计和 MPKI 计算
- **多格式输入** — text, binary, json, champsim, champsim_xz, **elf**（推荐）
- **Capstone v5 解码** — 支持 ARMv8 基础指令集 + SVE/SVE2/SME 扩展
- **Konata 管线可视化** — 导出 JSON 格式，配套交互式管线时间线查看器
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

本项目有两条独立的指令解码路径，指令覆盖范围不同：

### 输入格式与解码器对应关系

| 输入格式 | 解码器 | Capstone | 指令覆盖 |
|----------|--------|----------|----------|
| `-f elf` | `CapstoneDecoder`（`src/decoder/capstone_decoder.cpp`） | **必须** | ~500+ 助记符 |
| `-f text` | `TextTraceParser`（`src/input/text_trace.cpp`） | **不使用** | ~40 助记符 |

### ELF 格式（`-f elf`）— 完整指令覆盖

ELF 输入使用 Capstone v5 引擎进行二进制指令解码，覆盖：

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

- **需要完整指令集覆盖** → 用 `-f elf`（需要交叉编译测试程序）
- **快速调试管线行为** → 用 `-f text`（手写 trace，不依赖交叉编译，但指令集有限）
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

输出示例：
```json
{
  "total_instructions": 1234,
  "total_cycles": 567,
  "ipc": 2.176369,
  "cpi": 0.459484,
  "l1_hit_rate": 0.950000,
  "l2_hit_rate": 0.800000,
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

仿真运行后会产生两类输出：

1. **终端摘要** — IPC、CPI、缓存命中率、指令分布等性能指标
2. **Konata JSON 文件** — 默认保存到 `output/<输入文件名>_<时间戳>.json`

## 可视化

使用配套的 Konata 管线可视化工具查看仿真结果：

1. 用浏览器打开 `tools/viz_server/index.html`
2. 点击 **"Load JSON File"** 上传仿真输出的 JSON 文件
3. 管线时间线会自动渲染，显示每条指令在 Fetch → Decode → Rename → Dispatch → Issue → Execute → Commit 各阶段的执行情况

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
│   │   └── elf_trace.hpp       # ELF trace 解析器（调用 Capstone）
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
│       ├── test_elf_aarch64    # 预编译测试 ELF（需要 aarch64-elf-gcc 生成）
│       ├── text_trace_basic.txt      # 基础指令混合（20 条）
│       ├── text_trace_memory.txt     # 内存密集型（50 条）
│       ├── text_trace_branches.txt   # 分支密集型（30 条）
│       └── text_trace_long.txt       # 长序列压力测试（200+ 条）
├── tools/
│   └── viz_server/             # Konata 管线可视化
│       ├── index.html
│       ├── style.css
│       ├── pipeline_view.js
│       ├── app_static.js
│       └── konata/             # Konata 渲染器
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

## 许可证

MIT
