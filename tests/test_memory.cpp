/// @file test_memory.cpp
/// @brief Unit tests for memory subsystem.

#include <gtest/gtest.h>
#include "arm_cpu/memory/cache.hpp"
#include "arm_cpu/memory/lsq.hpp"
#include "arm_cpu/memory/ddr_controller.hpp"
#include "arm_cpu/memory/controller.hpp"
#include "arm_cpu/memory/memory_subsystem.hpp"
#include "arm_cpu/config.hpp"
#include "arm_cpu/types.hpp"

using namespace arm_cpu;

// --- Cache Tests ---

TEST(Cache, Config) {
    CacheConfig config{64 * 1024, 4, 64, 4, "L1"};
    EXPECT_EQ(config.num_sets(), 256);
    EXPECT_EQ(config.get_set(0x1000), 64); // (0x1000 >> 6) & 0xFF = 64
}

TEST(Cache, Access) {
    auto cache = Cache::create({4 * 1024, 4, 64, 4, "L1"});
    ASSERT_TRUE(cache.ok());

    auto hit = (*cache)->access(0x1000, true);
    ASSERT_TRUE(hit.ok());
    EXPECT_FALSE(hit.value()); // First access = miss
    EXPECT_EQ((*cache)->stats().misses, 1);

    (*cache)->fill_line(0x1000);

    hit = (*cache)->access(0x1000, true);
    ASSERT_TRUE(hit.ok());
    EXPECT_TRUE(hit.value()); // Second = hit
    EXPECT_EQ((*cache)->stats().hits, 1);
}

TEST(CacheStats, Calculations) {
    CacheStats stats{"L1", 1000, 950, 50};
    EXPECT_NEAR(stats.hit_rate(), 0.95, 0.001);
    EXPECT_NEAR(stats.miss_rate(), 0.05, 0.001);
    EXPECT_NEAR(stats.mpki(10000), 5.0, 0.001);
}

// --- LSQ Tests ---

TEST(LSQ, Basic) {
    LoadStoreQueue lsq(16, 2, 1);
    EXPECT_TRUE(lsq.has_space());

    auto handle = lsq.add_load(InstructionId(0), 0x1000, 8);
    EXPECT_EQ(lsq.occupancy(), 1);

    lsq.complete(handle);
    EXPECT_TRUE(lsq.get_ready().empty()); // completed entries not returned as ready
}

TEST(LSQ, StoreConflict) {
    LoadStoreQueue lsq(16, 2, 1);
    lsq.add_store(InstructionId(0), 0x1000, 8);

    auto conflict = lsq.check_store_conflict(0x1004, 8);
    EXPECT_TRUE(conflict.has_value());

    auto no_conflict = lsq.check_store_conflict(0x2000, 8);
    EXPECT_FALSE(no_conflict.has_value());
}

TEST(LSQ, RetireCompleted) {
    LoadStoreQueue lsq(16, 2, 1);
    auto h0 = lsq.add_load(InstructionId(0), 0x1000, 8);
    auto h1 = lsq.add_load(InstructionId(1), 0x1008, 8);

    lsq.complete(h0);
    auto retired = lsq.retire_completed();
    EXPECT_EQ(retired.size(), 1);
    EXPECT_EQ(lsq.occupancy(), 1);
}

// --- DDR Controller Tests ---

TEST(DDRController, Basic) {
    DdrController ctrl(150, 30, 20, 8);
    ctrl.set_cycle(100);

    auto result = ctrl.access(0x1000);
    EXPECT_FALSE(result.row_hit);
    EXPECT_EQ(result.latency, 150);
    EXPECT_EQ(result.complete_cycle, 250);

    ctrl.set_cycle(300);
    auto result2 = ctrl.access(0x1200); // Same row, same bank (bank 0)
    EXPECT_TRUE(result2.row_hit);
    EXPECT_EQ(result2.latency, 120);
    EXPECT_EQ(result2.complete_cycle, 420);
}

TEST(DDRController, Stats) {
    DdrController ctrl(150, 30, 20, 8);
    ctrl.set_cycle(0);
    ctrl.access(0x1000);
    ctrl.access(0x1000);

    auto stats = ctrl.stats();
    EXPECT_EQ(stats.total_accesses, 2);
    EXPECT_EQ(stats.row_buffer_hits, 1);
    EXPECT_EQ(stats.row_buffer_misses, 1);
    EXPECT_NEAR(stats.hit_rate(), 0.5, 0.001);
}

// --- MemoryController Tests ---

TEST(MemoryController, Basic) {
    MemoryController ctrl(100, 16);
    EXPECT_TRUE(ctrl.can_accept());

    auto complete = ctrl.read(InstructionId(0), 0x1000, 8);
    EXPECT_TRUE(complete.has_value());
    EXPECT_EQ(complete.value(), 100);
    EXPECT_EQ(ctrl.pending_count(), 1);
}

TEST(MemoryController, PollCompleted) {
    MemoryController ctrl(100, 16);
    ctrl.read(InstructionId(0), 0x1000, 8);

    auto completed = ctrl.poll_completed(50);
    EXPECT_TRUE(completed.empty());

    completed = ctrl.poll_completed(100);
    EXPECT_EQ(completed.size(), 1);
    EXPECT_EQ(completed[0].instruction_id, InstructionId(0));
}

TEST(BandwidthTracker, Basic) {
    BandwidthTracker tracker(100, 10);
    tracker.record(1000);
    tracker.record(500);
    tracker.advance_window();

    tracker.record(2000);
    tracker.advance_window();

    EXPECT_NEAR(tracker.average_bandwidth(), 1750.0, 0.1);
    EXPECT_EQ(tracker.peak_bandwidth(), 2000);
}

// --- MemorySubsystem Tests ---

TEST(MemorySubsystem, Load) {
    auto config = CPUConfig::minimal();
    auto mem = MemorySubsystem::create(config);
    ASSERT_TRUE(mem.ok());

    MemAccess access{0x1000, 8, true};
    auto req = (*mem)->load(InstructionId(0), access);
    EXPECT_TRUE(req.is_completed());
}
