/// @file test_types.cpp
/// @brief Unit tests for core types (Reg, OpcodeType, Instruction).

#include <gtest/gtest.h>
#include "arm_cpu/types.hpp"

using namespace arm_cpu;

TEST(RegTest, Display) {
    EXPECT_EQ(Reg(0).name(), "X0");
    EXPECT_EQ(Reg(30).name(), "X30");
    EXPECT_EQ(Reg(31).name(), "SP/XZR");
    EXPECT_TRUE(Reg(0).is_valid());
    EXPECT_TRUE(Reg(31).is_valid());
    EXPECT_FALSE(Reg(32).is_valid());
}

TEST(RegTest, Equality) {
    EXPECT_EQ(Reg(5), Reg(5));
    EXPECT_NE(Reg(5), Reg(6));
    EXPECT_EQ(Reg::SP, Reg::XZR);
}

TEST(VRegTest, Basic) {
    EXPECT_TRUE(VReg(0).is_valid());
    EXPECT_TRUE(VReg(31).is_valid());
    EXPECT_FALSE(VReg(32).is_valid());
    EXPECT_EQ(VReg(0), VReg::V0);
}

TEST(OpcodeTest, Classification) {
    EXPECT_TRUE(is_memory_op(OpcodeType::Load));
    EXPECT_TRUE(is_memory_op(OpcodeType::Store));
    EXPECT_TRUE(is_memory_op(OpcodeType::Vld));
    EXPECT_FALSE(is_memory_op(OpcodeType::Add));

    EXPECT_TRUE(is_branch(OpcodeType::Branch));
    EXPECT_TRUE(is_branch(OpcodeType::BranchCond));
    EXPECT_TRUE(is_branch(OpcodeType::BranchReg));
    EXPECT_FALSE(is_branch(OpcodeType::Add));

    EXPECT_TRUE(is_compute(OpcodeType::Add));
    EXPECT_TRUE(is_compute(OpcodeType::Vadd));
    EXPECT_TRUE(is_compute(OpcodeType::Fmadd));
    EXPECT_FALSE(is_compute(OpcodeType::Load));

    EXPECT_TRUE(is_cache_maintenance(OpcodeType::DcZva));
    EXPECT_FALSE(is_cache_maintenance(OpcodeType::Add));

    EXPECT_TRUE(is_crypto(OpcodeType::Aesd));
    EXPECT_FALSE(is_crypto(OpcodeType::Add));

    EXPECT_TRUE(is_simd(OpcodeType::Vadd));
    EXPECT_FALSE(is_simd(OpcodeType::Add));

    EXPECT_TRUE(is_fma(OpcodeType::Fmadd));
    EXPECT_FALSE(is_fma(OpcodeType::Fadd));
}

TEST(OpcodeTest, Latency) {
    EXPECT_EQ(latency(OpcodeType::Add), 1);
    EXPECT_EQ(latency(OpcodeType::Mul), 3);
    EXPECT_EQ(latency(OpcodeType::Div), 3);
    EXPECT_EQ(latency(OpcodeType::Fdiv), 8);
    EXPECT_EQ(latency(OpcodeType::Fmadd), 4);
    EXPECT_EQ(latency(OpcodeType::DcZva), 20);
    EXPECT_EQ(latency(OpcodeType::Sha512H), 16);
}

TEST(InstructionTest, Builder) {
    auto instr = Instruction(InstructionId(1), 0x1000, 0x8B000000, OpcodeType::Add)
        .with_src_reg(Reg(0))
        .with_src_reg(Reg(1))
        .with_dst_reg(Reg(2))
        .with_disasm("ADD X2, X0, X1");

    EXPECT_EQ(instr.src_regs.size(), 2);
    EXPECT_EQ(instr.dst_regs.size(), 1);
    EXPECT_TRUE(instr.reads_reg(Reg(0)));
    EXPECT_TRUE(instr.writes_reg(Reg(2)));
    EXPECT_FALSE(instr.reads_reg(Reg(5)));
    EXPECT_EQ(instr.instr_latency(), 1);
}

TEST(InstructionTest, DuplicateRegs) {
    auto instr = Instruction(InstructionId(1), 0x0, 0, OpcodeType::Add)
        .with_src_reg(Reg(0))
        .with_src_reg(Reg(0)); // duplicate
    EXPECT_EQ(instr.src_regs.size(), 1);
}

TEST(InstructionTest, MemAccess) {
    auto instr = Instruction(InstructionId(1), 0x100, 0, OpcodeType::Load)
        .with_mem_access(0x8000, 8, true);
    EXPECT_TRUE(instr.mem_access.has_value());
    EXPECT_EQ(instr.mem_access->addr, 0x8000);
    EXPECT_EQ(instr.mem_access->size, 8);
    EXPECT_TRUE(instr.mem_access->is_load);
}

TEST(InstructionTest, BranchInfo) {
    auto instr = Instruction(InstructionId(1), 0x100, 0, OpcodeType::BranchCond)
        .with_branch(0x200, true, true);
    EXPECT_TRUE(instr.branch_info.has_value());
    EXPECT_EQ(instr.branch_info->target, 0x200);
    EXPECT_TRUE(instr.branch_info->is_conditional);
    EXPECT_TRUE(instr.branch_info->is_taken);
}
