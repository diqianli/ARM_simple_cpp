#pragma once

/// @file functional_sim.hpp
/// @brief Lightweight ARM64 functional simulator for PC-based trace generation.
///
/// Performs a single-pass functional simulation of an ELF binary to determine
/// the actual dynamic instruction execution order (with perfect branch prediction).
/// The resulting trace is then fed to the OoO timing simulator.
///
/// Supports: integer arithmetic/logical/shift/move, compare, conditional select,
/// load/store (base+imm, base+reg, pre/post-index), direct & indirect branches.

#include "arm_cpu/types.hpp"
#include "arm_cpu/error.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct cs_insn;
using csh = std::size_t;

namespace arm_cpu {

class ElfLoader;

namespace decoder { class CapstoneDecoder; }

/// Lightweight ARM64 functional simulator.
///
/// Runs a simplified functional simulation to determine the dynamic execution
/// trace of an ELF binary. Uses perfect branch prediction (always correct).
///
/// Usage:
/// @code
///   auto result = FunctionalSim::run(elf_loader);
///   if (result.has_error()) { ... }
///   auto& trace = result.value();  // vector<Instruction> in dynamic order
/// @endcode
class FunctionalSim {
public:
    struct Config {
        uint64_t max_instructions = 10'000'000;  // Safety limit
        uint64_t initial_sp = 0x80000000ULL;     // Default stack pointer
    };

    /// Run functional simulation on an ELF binary.
    /// Returns the dynamic instruction trace (in execution order).
    static Result<std::vector<Instruction>> run(
        const ElfLoader& elf, Config config);

private:
    FunctionalSim(const ElfLoader& elf, Config config);

    /// Main simulation loop. Returns error string on failure, empty on success.
    std::optional<std::string> simulate();

    /// Single-step: fetch, decode, execute one instruction.
    /// Returns false if simulation should stop (end of code / error).
    bool step();

    // --- Operand helpers ---
    uint64_t read_gpr(unsigned reg_id) const;
    void write_gpr(unsigned reg_id, uint64_t val);
    bool is_32bit_op(unsigned reg_id) const;
    unsigned gpr_index(unsigned reg_id) const;

    // --- Condition evaluation ---
    bool eval_condition(unsigned cc) const;

    // --- NZCV flag helpers ---
    void set_nzcv_add(uint64_t a, uint64_t b, uint64_t result, bool is_32bit);
    void set_nzcv_sub(uint64_t a, uint64_t b, uint64_t result, bool is_32bit);
    void set_nzcv_logic(uint64_t result, bool is_32bit);

    // --- Memory helpers ---
    uint8_t  mem_read8(uint64_t addr);
    uint16_t mem_read16(uint64_t addr);
    uint32_t mem_read32(uint64_t addr);
    uint64_t mem_read64(uint64_t addr);
    void mem_write8(uint64_t addr, uint8_t val);
    void mem_write16(uint64_t addr, uint16_t val);
    void mem_write32(uint64_t addr, uint32_t val);
    void mem_write64(uint64_t addr, uint64_t val);

    // --- Instruction execution (returns true if PC was updated by branch) ---
    bool execute(const cs_insn* insn);

    // --- PLT interception (dynamic linking support) ---
    bool is_plt_call(uint64_t target) const;
    bool handle_external_call(uint64_t lr_save, const std::string& func_name);
    void stub_printf(const std::string& variant);
    void stub_puts();
    void stub_malloc();
    void stub_calloc();
    void stub_strlen();
    void stub_strcpy(bool is_strncpy);
    void stub_strcmp(bool is_strncmp);
    void stub_memcpy();
    void stub_memset();
    void stub_write();

    // --- Addressing mode helpers ---
    uint64_t compute_mem_addr(const cs_insn* insn, int op_index,
                              uint64_t* writeback_val, bool* has_writeback);

    // --- Data ---
    const ElfLoader& elf_;
    Config config_;

    // Capstone handle (owned)
    csh cs_handle_ = 0;

    // Architectural state
    uint64_t regs_[31] = {};   // x0-x30
    uint64_t sp_ = 0;
    uint64_t pc_ = 0;
    bool n_ = false, z_ = false, c_ = false, v_ = false;  // NZCV flags

    // Simple memory: addr -> byte
    std::unordered_map<uint64_t, uint8_t> memory_;

    // Call stack for bl/ret tracking
    std::vector<uint64_t> call_stack_;

    // Output: dynamic instruction trace
    std::vector<Instruction> trace_;

    // CapstoneDecoder for creating Instruction objects
    std::unique_ptr<decoder::CapstoneDecoder> decoder_;

    // PLT interception (dynamic linking)
    const std::unordered_map<uint64_t, std::string>* plt_symbols_ = nullptr;
    bool is_dynamic_ = false;

    // Heap bump allocator for malloc/calloc stubs
    static constexpr uint64_t kHeapStart = 0xA0000000ULL;
    uint64_t heap_ptr_ = kHeapStart;

    // Signal from external call stubs to stop simulation
    bool stop_simulation_ = false;
};

} // namespace arm_cpu
