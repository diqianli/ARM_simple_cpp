/// @file test_ooo.cpp
/// @brief Unit tests for OoO execution engine.

#include <gtest/gtest.h>
#include "arm_cpu/ooo/ooo_engine.hpp"
#include "arm_cpu/ooo/dependency.hpp"
#include "arm_cpu/ooo/window.hpp"
#include "arm_cpu/ooo/scheduler.hpp"
#include "arm_cpu/ooo/parallel.hpp"
#include "arm_cpu/config.hpp"
#include "arm_cpu/types.hpp"

using namespace arm_cpu;

// --- DependencyTracker Tests ---

TEST(DependencyTracker, RegisterDependency) {
    DependencyTracker tracker;

    auto instr0 = Instruction(InstructionId(0), 0x1000, 0, OpcodeType::Add)
        .with_src_reg(Reg(0)).with_src_reg(Reg(1)).with_dst_reg(Reg(2));
    auto instr1 = Instruction(InstructionId(1), 0x1004, 0, OpcodeType::Add)
        .with_src_reg(Reg(2)).with_src_reg(Reg(0)).with_dst_reg(Reg(3));

    tracker.register_instruction(instr0, InstructionId(0), 0);
    tracker.register_instruction(instr1, InstructionId(1), 0);

    EXPECT_TRUE(tracker.is_ready(InstructionId(0)));
    EXPECT_FALSE(tracker.is_ready(InstructionId(1)));

    tracker.release_dependencies(InstructionId(0));
    EXPECT_TRUE(tracker.is_ready(InstructionId(1)));
}

TEST(DependencyTracker, MemoryDependency) {
    DependencyTracker tracker;

    auto store = Instruction(InstructionId(0), 0x1000, 0, OpcodeType::Store)
        .with_src_reg(Reg(0)).with_mem_access(0x1000, 8, false);
    auto load = Instruction(InstructionId(1), 0x1004, 0, OpcodeType::Load)
        .with_dst_reg(Reg(1)).with_mem_access(0x1000, 8, true);

    tracker.register_instruction(store, InstructionId(0), 0);
    tracker.register_instruction(load, InstructionId(1), 0);

    EXPECT_TRUE(tracker.is_ready(InstructionId(0)));
    EXPECT_FALSE(tracker.is_ready(InstructionId(1)));

    tracker.release_dependencies(InstructionId(0));
    EXPECT_TRUE(tracker.is_ready(InstructionId(1)));
}

TEST(DependencyTracker, IndependentInstructions) {
    DependencyTracker tracker;

    auto instr0 = Instruction(InstructionId(0), 0x1000, 0, OpcodeType::Add)
        .with_src_reg(Reg(0)).with_dst_reg(Reg(1));
    auto instr1 = Instruction(InstructionId(1), 0x1004, 0, OpcodeType::Add)
        .with_src_reg(Reg(2)).with_dst_reg(Reg(3));

    tracker.register_instruction(instr0, InstructionId(0), 0);
    tracker.register_instruction(instr1, InstructionId(1), 0);

    EXPECT_TRUE(tracker.is_ready(InstructionId(0)));
    EXPECT_TRUE(tracker.is_ready(InstructionId(1)));
}

// --- InstructionWindow Tests ---

TEST(InstructionWindow, Basic) {
    InstructionWindow window(16);
    auto instr = Instruction(InstructionId(0), 0x1000, 0, OpcodeType::Add)
        .with_src_reg(Reg(0)).with_dst_reg(Reg(1));

    EXPECT_TRUE(window.has_space());
    auto result = window.insert(instr);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(window.len(), 1);

    auto* entry = window.get_entry(InstructionId(0));
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->status, InstrStatus::Waiting);
}

TEST(InstructionWindow, StatusTransitions) {
    InstructionWindow window(16);
    auto instr = Instruction(InstructionId(0), 0x1000, 0, OpcodeType::Add);
    window.insert(instr);

    window.mark_ready(InstructionId(0));
    EXPECT_EQ(window.get_entry(InstructionId(0))->status, InstrStatus::Ready);

    window.mark_executing(InstructionId(0));
    EXPECT_EQ(window.get_entry(InstructionId(0))->status, InstrStatus::Executing);

    window.set_complete_cycle(InstructionId(0), 10);
    EXPECT_EQ(window.get_entry(InstructionId(0))->complete_cycle, 10);
    EXPECT_EQ(window.get_entry(InstructionId(0))->status, InstrStatus::Executing);

    window.set_status_completed(InstructionId(0));
    EXPECT_EQ(window.get_entry(InstructionId(0))->status, InstrStatus::Completed);

    window.remove(InstructionId(0));
    EXPECT_TRUE(window.is_empty());
}

TEST(InstructionWindow, Capacity) {
    InstructionWindow window(4);
    for (std::size_t i = 0; i < 4; i++) {
        auto instr = Instruction(InstructionId(i), 0x1000 + i * 4, 0, OpcodeType::Nop);
        ASSERT_TRUE(window.insert(instr).ok());
    }
    EXPECT_FALSE(window.has_space());

    auto instr = Instruction(InstructionId(4), 0x1010, 0, OpcodeType::Nop);
    EXPECT_FALSE(window.insert(instr).ok());
}

// --- Scheduler Tests ---

TEST(Scheduler, Basic) {
    Scheduler scheduler(4, 4);
    InstructionWindow window(16);

    auto instr = Instruction(InstructionId(0), 0x1000, 0, OpcodeType::Add)
        .with_src_reg(Reg(0)).with_dst_reg(Reg(1));
    window.insert(instr);
    window.mark_ready(InstructionId(0));
    scheduler.add_ready(InstructionId(0));

    auto ready = scheduler.get_ready(window);
    EXPECT_EQ(ready.size(), 1);
    EXPECT_EQ(ready[0].first, InstructionId(0));
}

TEST(Scheduler, IssueWidth) {
    Scheduler scheduler(2, 4);
    InstructionWindow window(16);

    for (std::size_t i = 0; i < 4; i++) {
        auto instr = InstructionId(i);
        window.insert(Instruction(InstructionId(i), 0x1000 + i * 4, 0, OpcodeType::Nop));
        window.mark_ready(InstructionId(i));
        scheduler.add_ready(InstructionId(i));
    }

    auto ready = scheduler.get_ready(window);
    EXPECT_EQ(ready.size(), 2);
}

TEST(Scheduler, ExecutionUnit) {
    auto add = Instruction(InstructionId(0), 0x1000, 0, OpcodeType::Add);
    EXPECT_EQ(execution_unit_for(add), ExecutionUnit::IntAlu);

    auto load = Instruction(InstructionId(1), 0x1004, 0, OpcodeType::Load);
    EXPECT_EQ(execution_unit_for(load), ExecutionUnit::Load);

    auto branch = Instruction(InstructionId(2), 0x1008, 0, OpcodeType::Branch);
    EXPECT_EQ(execution_unit_for(branch), ExecutionUnit::Branch);
}

TEST(ExecutionPipeline, Basic) {
    ExecutionPipeline pipeline(ExecutionUnit::IntAlu);
    EXPECT_TRUE(pipeline.has_capacity());
    EXPECT_TRUE(pipeline.issue(InstructionId(0), 10));
    EXPECT_TRUE(pipeline.issue(InstructionId(1), 12));

    auto completed = pipeline.complete_by(10);
    EXPECT_EQ(completed.size(), 1);
    EXPECT_EQ(completed[0], InstructionId(0));
    EXPECT_EQ(pipeline.executing_count(), 1);
}

// --- OoOEngine Tests ---

TEST(OoOEngine, Basic) {
    auto config = CPUConfig::minimal();
    auto engine = OoOEngine::create(config);
    ASSERT_TRUE(engine.ok());

    auto instr1 = Instruction(InstructionId(0), 0x1000, 0, OpcodeType::Add)
        .with_src_reg(Reg(0)).with_src_reg(Reg(1)).with_dst_reg(Reg(2));
    auto instr2 = Instruction(InstructionId(1), 0x1004, 0, OpcodeType::Add)
        .with_src_reg(Reg(2)).with_dst_reg(Reg(3));

    ASSERT_TRUE((*engine)->dispatch(std::move(instr1)).ok());
    ASSERT_TRUE((*engine)->dispatch(std::move(instr2)).ok());
    EXPECT_EQ((*engine)->window_size(), 2);

    auto ready = (*engine)->get_ready_instructions();
    EXPECT_EQ(ready.size(), 1);
    EXPECT_EQ(ready[0].first, InstructionId(0));
}

// --- Parallel Tests ---

TEST(BatchSimulator, BatchCreation) {
    std::vector<Instruction> instructions;
    for (std::size_t i = 0; i < 250; i++) {
        instructions.push_back(Instruction(InstructionId(i), 0x1000 + i * 4, 0, OpcodeType::Add)
            .with_src_reg(Reg(i % 30)).with_dst_reg(Reg((i + 1) % 30)));
    }
    auto batches = create_batches(instructions, 100);
    EXPECT_EQ(batches.size(), 3);
    EXPECT_EQ(batches[0].len(), 100);
    EXPECT_EQ(batches[1].len(), 100);
    EXPECT_EQ(batches[2].len(), 50);
}

TEST(DependencyAnalysis, Basic) {
    std::vector<Instruction> instructions = {
        Instruction(InstructionId(0), 0x1000, 0, OpcodeType::Add).with_src_reg(Reg(0)).with_dst_reg(Reg(1)),
        Instruction(InstructionId(1), 0x1004, 0, OpcodeType::Add).with_src_reg(Reg(1)).with_dst_reg(Reg(2)),
        Instruction(InstructionId(2), 0x1008, 0, OpcodeType::Add).with_src_reg(Reg(0)).with_dst_reg(Reg(3)),
    };
    auto deps = analyze_dependencies_batch(instructions);
    EXPECT_EQ(deps.size(), 3);
    EXPECT_TRUE(deps[0].producers.empty());
    EXPECT_EQ(deps[1].producers.size(), 1);
    EXPECT_EQ(deps[1].producers[0], 0);
    EXPECT_TRUE(deps[2].producers.empty());
}

TEST(BatchSimulator, ProcessBatch) {
    auto config = CPUConfig::default_config();
    BatchSimulator simulator(config);

    InstructionBatch batch(0, 100);
    for (std::size_t i = 0; i < 100; i++) {
        batch.push(Instruction(InstructionId(i), 0x1000 + i * 4, 0, OpcodeType::Add)
            .with_src_reg(Reg(i % 30)).with_dst_reg(Reg((i + 1) % 30)));
    }
    auto result = simulator.process_batch(batch);
    EXPECT_EQ(result.instr_count, 100);
    EXPECT_GT(result.cycles, 0);
    EXPECT_GT(result.ipc, 0.0);
}
