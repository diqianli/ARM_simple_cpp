/// @file viz_demo.cpp
/// @brief Generates a Konata-format JSON file for pipeline visualization demo.
///
/// Compiles with: g++ -std=c++20 -I../include tools/viz_demo.cpp -o viz_demo
/// Then run: ./viz_demo && open viz_demo.html

#include "arm_cpu/visualization/konata_format.hpp"

#include <cstdio>
#include <fstream>

using namespace arm_cpu;

int main() {
    // Simulate a pipeline trace with diverse instruction types
    // including cache misses at different levels
    KonataSnapshot snapshot = KonataSnapshot::create(100, 12);
    snapshot.metadata.config = KonataConfigInfo{128, 4, 4};
    snapshot.metadata.timestamp = 100;

    // Instruction 0: ADD X0, X1, X2 — compute, no cache
    {
        KonataOp op(0, 0, 0x1000, "ADD X0, X1, X2");
        op.fetched_cycle = 0;
        op.retired_cycle = 9;
        op.src_regs = {1, 2};
        op.dst_regs = {0};
        op.add_stage(StageId::IF, 0, 1);
        op.add_stage(StageId::DE, 1, 2);
        op.add_stage(StageId::RN, 2, 3);
        op.add_stage(StageId::DI, 3, 4);
        op.add_stage(StageId::IS, 4, 5);
        op.add_stage(StageId::EX, 5, 7);
        op.add_stage(StageId::WB, 7, 8);
        op.add_stage(StageId::RR, 8, 9);
        snapshot.add_op(std::move(op));
    }

    // Instruction 1: MUL X3, X4, X5 — compute, 3-cycle latency
    {
        KonataOp op(1, 1, 0x1004, "MUL X3, X4, X5");
        op.fetched_cycle = 0;
        op.retired_cycle = 12;
        op.src_regs = {4, 5};
        op.dst_regs = {3};
        op.add_dependency(0, KonataDependencyType::Register);
        op.add_stage(StageId::IF, 0, 1);
        op.add_stage(StageId::DE, 1, 2);
        op.add_stage(StageId::RN, 2, 3);
        op.add_stage(StageId::DI, 3, 4);
        op.add_stage(StageId::IS, 9, 10);  // waited for ADD (reg dep)
        op.add_stage(StageId::EX, 10, 13); // 3-cycle multiply latency
        op.add_stage(StageId::WB, 13, 14);
        op.add_stage(StageId::RR, 14, 15);
        snapshot.add_op(std::move(op));
    }

    // Instruction 2: LDR X6, [X7] — L1 cache hit
    {
        KonataOp op(2, 2, 0x1008, "LDR X6, [X7]");
        op.fetched_cycle = 1;
        op.retired_cycle = 12;
        op.src_regs = {7};
        op.dst_regs = {6};
        op.is_memory = true;
        op.mem_addr = 0x80000;
        op.add_stage(StageId::IF, 1, 2);
        op.add_stage(StageId::DE, 2, 3);
        op.add_stage(StageId::RN, 3, 4);
        op.add_stage(StageId::DI, 4, 5);
        op.add_stage(StageId::IS, 5, 6);
        op.add_stage(StageId::ME, 6, 7);
        op.add_stage_with_name("ME:L1", 6, 7);  // L1 hit
        op.add_stage(StageId::WB, 7, 8);
        op.add_stage(StageId::RR, 11, 12);
        snapshot.add_op(std::move(op));
    }

    // Instruction 3: ADD X8, X6, X9 — depends on LDR (reg dep)
    {
        KonataOp op(3, 3, 0x100C, "ADD X8, X6, X9");
        op.fetched_cycle = 1;
        op.retired_cycle = 16;
        op.src_regs = {6, 9};
        op.dst_regs = {8};
        op.add_dependency(2, KonataDependencyType::Register);
        op.add_stage(StageId::IF, 1, 2);
        op.add_stage(StageId::DE, 2, 3);
        op.add_stage(StageId::RN, 3, 4);
        op.add_stage(StageId::DI, 4, 5);
        op.add_stage(StageId::IS, 12, 13);  // waited for LDR
        op.add_stage(StageId::EX, 13, 14);
        op.add_stage(StageId::WB, 14, 15);
        op.add_stage(StageId::RR, 15, 16);
        snapshot.add_op(std::move(op));
    }

    // Instruction 4: LDR X10, [X11] — L2 cache hit (L1 miss)
    {
        KonataOp op(4, 4, 0x1010, "LDR X10, [X11]");
        op.fetched_cycle = 2;
        op.retired_cycle = 22;
        op.src_regs = {11};
        op.dst_regs = {10};
        op.is_memory = true;
        op.mem_addr = 0x100000;
        op.add_stage(StageId::IF, 2, 3);
        op.add_stage(StageId::DE, 3, 4);
        op.add_stage(StageId::RN, 4, 5);
        op.add_stage(StageId::DI, 5, 6);
        op.add_stage(StageId::IS, 6, 7);
        op.add_stage(StageId::ME, 7, 17);    // 10-cycle memory
        op.add_stage_with_name("ME:L1", 7, 8);  // L1 miss (1 cycle probe)
        op.add_stage_with_name("ME:L2", 8, 12); // L2 hit (4 cycles)
        op.add_stage_with_name("ME:L1-fill", 12, 13); // L1 fill
        op.add_stage(StageId::WB, 17, 18);
        op.add_stage(StageId::RR, 21, 22);
        snapshot.add_op(std::move(op));
    }

    // Instruction 5: LDR X12, [X13] — L3 cache hit (L1+L2 miss)
    {
        KonataOp op(5, 5, 0x1014, "LDR X12, [X13]");
        op.fetched_cycle = 3;
        op.retired_cycle = 32;
        op.src_regs = {13};
        op.dst_regs = {12};
        op.is_memory = true;
        op.mem_addr = 0x800000;
        op.add_stage(StageId::IF, 3, 4);
        op.add_stage(StageId::DE, 4, 5);
        op.add_stage(StageId::RN, 5, 6);
        op.add_stage(StageId::DI, 6, 7);
        op.add_stage(StageId::IS, 7, 8);
        op.add_stage(StageId::ME, 8, 27);    // 19-cycle memory
        op.add_stage_with_name("ME:L1", 8, 9);   // L1 miss
        op.add_stage_with_name("ME:L2", 9, 13);  // L2 miss
        op.add_stage_with_name("ME:L3", 13, 19); // L3 hit (6 cycles)
        op.add_stage_with_name("ME:L2-fill", 19, 20);
        op.add_stage_with_name("ME:L1-fill", 20, 21);
        op.add_stage(StageId::WB, 27, 28);
        op.add_stage(StageId::RR, 31, 32);
        snapshot.add_op(std::move(op));
    }

    // Instruction 6: STR X14, [X15] — Memory access, L1 hit
    {
        KonataOp op(6, 6, 0x1018, "STR X14, [X15]");
        op.fetched_cycle = 4;
        op.retired_cycle = 16;
        op.src_regs = {14, 15};
        op.dst_regs = {};
        op.is_memory = true;
        op.mem_addr = 0x80020;
        op.add_stage(StageId::IF, 4, 5);
        op.add_stage(StageId::DE, 5, 6);
        op.add_stage(StageId::RN, 6, 7);
        op.add_stage(StageId::DI, 7, 8);
        op.add_stage(StageId::IS, 8, 9);
        op.add_stage(StageId::ME, 9, 10);
        op.add_stage_with_name("ME:L1", 9, 10);  // L1 hit
        op.add_stage(StageId::WB, 10, 11);
        op.add_stage(StageId::RR, 15, 16);
        snapshot.add_op(std::move(op));
    }

    // Instruction 7: LDR X16, [X17] — Full memory access (L1+L2+L3 miss)
    {
        KonataOp op(7, 7, 0x101C, "LDR X16, [X17]");
        op.fetched_cycle = 5;
        op.retired_cycle = 52;
        op.src_regs = {17};
        op.dst_regs = {16};
        op.is_memory = true;
        op.mem_addr = 0x4000000;
        op.add_stage(StageId::IF, 5, 6);
        op.add_stage(StageId::DE, 6, 7);
        op.add_stage(StageId::RN, 7, 8);
        op.add_stage(StageId::DI, 8, 9);
        op.add_stage(StageId::IS, 9, 10);
        op.add_stage(StageId::ME, 10, 46);   // 36-cycle memory
        op.add_stage_with_name("ME:L1", 10, 11);   // L1 miss
        op.add_stage_with_name("ME:L2", 11, 15);  // L2 miss
        op.add_stage_with_name("ME:L3", 15, 21);  // L3 miss
        op.add_stage_with_name("ME:Memory", 21, 40); // DDR access (19 cycles)
        op.add_stage_with_name("ME:L3-fill", 40, 41);
        op.add_stage_with_name("ME:L2-fill", 41, 42);
        op.add_stage_with_name("ME:L1-fill", 42, 43);
        op.add_stage(StageId::WB, 46, 47);
        op.add_stage(StageId::RR, 51, 52);
        snapshot.add_op(std::move(op));
    }

    // Instruction 8: FADD V0, V1, V2 — FP compute
    {
        KonataOp op(8, 8, 0x1020, "FADD V0.4S, V1.4S, V2.4S");
        op.fetched_cycle = 6;
        op.retired_cycle = 18;
        op.src_regs = {1, 2};
        op.dst_regs = {0};
        op.add_stage(StageId::IF, 6, 7);
        op.add_stage(StageId::DE, 7, 8);
        op.add_stage(StageId::RN, 8, 9);
        op.add_stage(StageId::DI, 9, 10);
        op.add_stage(StageId::IS, 10, 11);
        op.add_stage(StageId::EX, 11, 13); // 2-cycle FADD
        op.add_stage(StageId::WB, 13, 14);
        op.add_stage(StageId::RR, 17, 18);
        snapshot.add_op(std::move(op));
    }

    // Instruction 9: FDIV V3, V4, V5 — FP divide, long latency
    {
        KonataOp op(9, 9, 0x1024, "FDIV V3.4S, V4.4S, V5.4S");
        op.fetched_cycle = 7;
        op.retired_cycle = 28;
        op.src_regs = {4, 5};
        op.dst_regs = {3};
        op.add_stage(StageId::IF, 7, 8);
        op.add_stage(StageId::DE, 8, 9);
        op.add_stage(StageId::RN, 9, 10);
        op.add_stage(StageId::DI, 10, 11);
        op.add_stage(StageId::IS, 11, 12);
        op.add_stage(StageId::EX, 12, 20); // 8-cycle FDIV
        op.add_stage(StageId::WB, 20, 21);
        op.add_stage(StageId::RR, 27, 28);
        snapshot.add_op(std::move(op));
    }

    // Instruction 10: B label — branch
    {
        KonataOp op(10, 10, 0x1028, "B #0x2000");
        op.fetched_cycle = 8;
        op.retired_cycle = 14;
        op.add_stage(StageId::IF, 8, 9);
        op.add_stage(StageId::DE, 9, 10);
        op.add_stage(StageId::RN, 10, 11);
        op.add_stage(StageId::DI, 11, 12);
        op.add_stage(StageId::IS, 12, 12);
        op.add_stage(StageId::EX, 12, 12);
        op.add_stage(StageId::WB, 12, 13);
        op.add_stage(StageId::RR, 13, 14);
        snapshot.add_op(std::move(op));
    }

    // Instruction 11: ADD X18, X0, X6 — depends on both instr 0 and 2
    {
        KonataOp op(11, 11, 0x102C, "ADD X18, X0, X6");
        op.fetched_cycle = 2;
        op.retired_cycle = 24;
        op.src_regs = {0, 6};
        op.dst_regs = {18};
        op.add_dependency(0, KonataDependencyType::Register);
        op.add_dependency(2, KonataDependencyType::Register);
        op.add_stage(StageId::IF, 2, 3);
        op.add_stage(StageId::DE, 3, 4);
        op.add_stage(StageId::RN, 4, 5);
        op.add_stage(StageId::DI, 5, 6);
        op.add_stage(StageId::IS, 16, 17);  // waited for both ADD and LDR
        op.add_stage(StageId::EX, 17, 18);
        op.add_stage(StageId::WB, 18, 19);
        op.add_stage(StageId::RR, 23, 24);
        snapshot.add_op(std::move(op));
    }

    // Write JSON output (legacy snapshot format)
    auto json_path = "viz_demo_data.json";
    bool ok = snapshot.write_to_file(json_path, true);
    if (!ok) {
        std::fprintf(stderr, "Error: could not write %s\n", json_path);
        return 1;
    }

    std::printf("Generated %s (%zu ops, cycle %llu, %llu committed)\n",
        json_path, snapshot.ops.size(),
        static_cast<unsigned long long>(snapshot.cycle),
        static_cast<unsigned long long>(snapshot.committed_count));

    // Write KonataExport format for the Canvas-based frontend
    KonataExport exp;
    exp.total_cycles = snapshot.cycle;
    exp.total_instructions = snapshot.committed_count;
    exp.ops_count = snapshot.ops.size();
    exp.ops = std::move(snapshot.ops);

    auto export_path = "arm_cpu_emulator_cpp/tools/viz_server/viz_demo_data.json";
    ok = exp.write_to_file(export_path, true);
    if (!ok) {
        std::fprintf(stderr, "Error: could not write %s\n", export_path);
        return 1;
    }

    std::printf("Generated %s (%zu ops, %llu cycles, %llu instructions)\n",
        export_path, exp.ops_count,
        static_cast<unsigned long long>(exp.total_cycles),
        static_cast<unsigned long long>(exp.total_instructions));
    return 0;
}
