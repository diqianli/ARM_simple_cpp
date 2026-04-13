/// @file config.cpp
/// @brief CPUConfig validation implementation.

#include "arm_cpu/config.hpp"

#include <bit>

namespace arm_cpu {

namespace {

bool is_power_of_two(std::size_t n) {
    return n > 0 && (n & (n - 1)) == 0;
}

} // anonymous namespace

Result<void> CPUConfig::validate() const {
    if (window_size < 4 || window_size > 512) {
        return EmulatorError::config("window_size must be between 4 and 512");
    }
    if (fetch_width < 1 || fetch_width > 16) {
        return EmulatorError::config("fetch_width must be between 1 and 16");
    }
    if (issue_width < 1 || issue_width > 16) {
        return EmulatorError::config("issue_width must be between 1 and 16");
    }
    if (commit_width < 1 || commit_width > 16) {
        return EmulatorError::config("commit_width must be between 1 and 16");
    }
    if (l1_line_size != 32 && l1_line_size != 64 && l1_line_size != 128) {
        return EmulatorError::config("l1_line_size must be 32, 64, or 128 bytes");
    }
    if (l2_line_size != 32 && l2_line_size != 64 && l2_line_size != 128) {
        return EmulatorError::config("l2_line_size must be 32, 64, or 128 bytes");
    }
    if (l3_line_size != 64 && l3_line_size != 128) {
        return EmulatorError::config("l3_line_size must be 64 or 128 bytes");
    }
    if (!is_power_of_two(l1_size)) {
        return EmulatorError::config("l1_size must be a power of 2");
    }
    if (!is_power_of_two(l2_size)) {
        return EmulatorError::config("l2_size must be a power of 2");
    }
    if (!is_power_of_two(l3_size)) {
        return EmulatorError::config("l3_size must be a power of 2");
    }
    if (!is_power_of_two(l1_sets())) {
        return EmulatorError::config("L1 cache configuration results in non-power-of-2 sets");
    }
    if (!is_power_of_two(l2_sets())) {
        return EmulatorError::config("L2 cache configuration results in non-power-of-2 sets");
    }
    if (!is_power_of_two(l3_sets())) {
        return EmulatorError::config("L3 cache configuration results in non-power-of-2 sets");
    }
    return {};
}

} // namespace arm_cpu
