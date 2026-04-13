/// @file test_config.cpp
/// @brief Unit tests for CPUConfig validation and presets.

#include <gtest/gtest.h>
#include "arm_cpu/config.hpp"

using namespace arm_cpu;

TEST(ConfigTest, DefaultValid) {
    auto config = CPUConfig::default_config();
    auto result = config.validate();
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(config.window_size, 128);
    EXPECT_EQ(config.l1_size, 64 * 1024);
}

TEST(ConfigTest, HighPerformance) {
    auto config = CPUConfig::high_performance();
    EXPECT_EQ(config.window_size, 256);
    EXPECT_EQ(config.issue_width, 6);
    EXPECT_TRUE(config.validate().ok());
}

TEST(ConfigTest, Minimal) {
    auto config = CPUConfig::minimal();
    EXPECT_EQ(config.window_size, 16);
    EXPECT_TRUE(config.validate().ok());
}

TEST(ConfigTest, InvalidWindowSize) {
    auto config = CPUConfig::default_config();
    config.window_size = 1024;
    EXPECT_FALSE(config.validate().ok());
    EXPECT_EQ(config.validate().error().code(), EmulatorErrorCode::ConfigError);
}

TEST(ConfigTest, InvalidFetchWidth) {
    auto config = CPUConfig::default_config();
    config.fetch_width = 0;
    EXPECT_FALSE(config.validate().ok());
}

TEST(ConfigTest, InvalidLineSize) {
    auto config = CPUConfig::default_config();
    config.l1_line_size = 16; // not 32, 64, or 128
    EXPECT_FALSE(config.validate().ok());
}

TEST(ConfigTest, L1Sets) {
    auto config = CPUConfig::default_config();
    // 64KB / (4 * 64) = 256 sets
    EXPECT_EQ(config.l1_sets(), 256);
}

TEST(ConfigTest, L2Sets) {
    auto config = CPUConfig::default_config();
    // 512KB / (8 * 64) = 1024 sets
    EXPECT_EQ(config.l2_sets(), 1024);
}

TEST(ConfigTest, L3Sets) {
    auto config = CPUConfig::default_config();
    // 8MB / (16 * 64) = 8192 sets
    EXPECT_EQ(config.l3_sets(), 8192);
}

TEST(ConfigTest, CyclePeriod) {
    auto config = CPUConfig::default_config();
    EXPECT_NEAR(config.cycle_period_ns(), 0.5, 0.001);
}
