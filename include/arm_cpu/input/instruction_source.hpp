#pragma once

/// @file instruction_source.hpp
/// @brief Abstract base class for instruction trace sources.
///
/// Analogous to the Rust trait `InstructionSource` which extends
/// `Iterator<Item = Result<Instruction>>`.

#include "arm_cpu/config.hpp"
#include "arm_cpu/error.hpp"
#include "arm_cpu/types.hpp"

#include <cstddef>
#include <memory>
#include <optional>

namespace arm_cpu {

/// Abstract instruction source: produces a stream of Instruction objects.
///
/// In C++20 we model the Rust Iterator + trait as a class with `next()`.
/// Derived classes implement `next_impl()` and `reset()`.
class InstructionSource {
public:
    virtual ~InstructionSource() = default;

    /// Return the next instruction, or std::nullopt at EOF.
    /// On parse error, returns an EmulatorError inside the Result.
    Result<std::optional<Instruction>> next() {
        return next_impl();
    }

    /// Get the total number of instructions, if known (e.g. from a header).
    virtual std::optional<std::size_t> total_count() const { return std::nullopt; }

    /// Reset the source to the beginning.
    virtual Result<void> reset() = 0;

protected:
    /// Derived classes implement this to produce the next instruction.
    virtual Result<std::optional<Instruction>> next_impl() = 0;
};

/// Factory: create an InstructionSource from a TraceInputConfig.
///
/// This mirrors the Rust `create_source()` function.
std::unique_ptr<InstructionSource> create_source(const TraceInputConfig& config);

} // namespace arm_cpu
