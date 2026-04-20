/// @file functional_sim.cpp
/// @brief Lightweight ARM64 functional simulator implementation.
///
/// Executes ARM64 instructions functionally to determine the dynamic
/// execution trace with perfect branch prediction.

#include "arm_cpu/input/functional_sim.hpp"
#include "arm_cpu/elf/elf_loader.hpp"
#include "arm_cpu/decoder/capstone_decoder.hpp"

#include <capstone/capstone.h>
#include <capstone/arm64.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace arm_cpu {

// =====================================================================
// FunctionalSim::run — static entry point
// =====================================================================

Result<std::vector<Instruction>> FunctionalSim::run(
    const ElfLoader& elf, Config config)
{
    FunctionalSim sim(elf, std::move(config));

    auto error = sim.simulate();
    if (error.has_value()) {
        return Err(EmulatorError::trace_parse(std::move(*error)));
    }

    return Ok(std::move(sim.trace_));
}

// =====================================================================
// FunctionalSim::FunctionalSim — private constructor
// =====================================================================

FunctionalSim::FunctionalSim(const ElfLoader& elf, Config config)
    : elf_(elf)
    , config_(std::move(config))
{
    // Initialize Capstone
    cs_mode mode = CS_MODE_LITTLE_ENDIAN;
    if (cs_open(CS_ARCH_ARM64, mode, &cs_handle_) != CS_ERR_OK) {
        cs_handle_ = 0;
    } else {
        cs_option(cs_handle_, CS_OPT_DETAIL, CS_OPT_ON);
    }

    // Create CapstoneDecoder for producing Instruction objects
    decoder_ = std::make_unique<decoder::CapstoneDecoder>();
    decoder_->init();

    // Set initial SP
    sp_ = config_.initial_sp;

    // Set initial PC from ELF entry point
    pc_ = elf_.entry_point();

    // Initialize memory from all ELF segments (code + data + rodata)
    for (const auto& seg : elf_.segments()) {
        for (std::size_t i = 0; i < seg.data.size(); ++i) {
            memory_[seg.vaddr + i] = seg.data[i];
        }
    }

    // PLT interception for dynamic ELF
    plt_symbols_ = &elf_.plt_symbols();
    is_dynamic_ = elf_.is_dynamic();
}

// =====================================================================
// FunctionalSim::simulate — main loop
// =====================================================================

std::optional<std::string> FunctionalSim::simulate() {
    if (cs_handle_ == 0) {
        return "Failed to initialize Capstone disassembler";
    }

    uint64_t prev_pc = 0;
    uint64_t no_progress_count = 0;

    while (trace_.size() < config_.max_instructions) {
        // Detect infinite loops (same PC with no trace growth)
        if (pc_ == prev_pc) {
            no_progress_count++;
            if (no_progress_count > 100) {
                std::fprintf(stderr,
                    "FunctionalSim: infinite loop detected at PC=0x%llx, stopping\n",
                    (unsigned long long)pc_);
                break;
            }
        } else {
            prev_pc = pc_;
            no_progress_count = 0;
        }

        if (!step()) break;
    }

    if (trace_.empty()) {
        return "No instructions executed from entry point PC=0x" +
               std::to_string(pc_) + ". Entry point may not be in any executable segment.";
    }

    std::fprintf(stderr,
        "FunctionalSim: executed %zu instructions from PC=0x%llx\n",
        trace_.size(), (unsigned long long)elf_.entry_point());

    return {};  // success
}

// =====================================================================
// FunctionalSim::step — single instruction step
// =====================================================================

bool FunctionalSim::step() {
    // Fetch raw bytes from ELF
    auto raw_opt = elf_.read_instruction(pc_);
    if (!raw_opt.has_value()) {
        // No instruction at this PC — end of simulation
        return false;
    }
    uint32_t raw = raw_opt.value();

    // Decode with Capstone for detailed operand info
    uint8_t raw_bytes[4];
    raw_bytes[0] = raw & 0xFF;
    raw_bytes[1] = (raw >> 8) & 0xFF;
    raw_bytes[2] = (raw >> 16) & 0xFF;
    raw_bytes[3] = (raw >> 24) & 0xFF;

    cs_insn* insn = nullptr;
    size_t count = cs_disasm(cs_handle_, raw_bytes, 4, pc_, 1, &insn);

    if (count == 0 || !insn) {
        // Failed to decode — skip this instruction
        std::fprintf(stderr,
            "FunctionalSim: failed to decode at PC=0x%llx (raw=0x%08x)\n",
            (unsigned long long)pc_, (unsigned)raw);
        pc_ += 4;
        return true;
    }

    // Create Instruction for the trace (using our existing decoder)
    auto decoded = decoder_->decode(pc_, raw);
    auto instr = decoded.to_instruction(InstructionId(trace_.size()));

    // Execute functionally — may update pc_
    bool pc_updated = execute(insn);

    // Add to trace
    trace_.push_back(std::move(instr));

    // Advance PC if execute() didn't handle it (non-branch instructions)
    if (!pc_updated) {
        pc_ += 4;
    }

    cs_free(insn, count);

    // Check if a stub requested simulation stop (exit/abort)
    if (stop_simulation_) {
        return false;
    }

    return true;
}

// =====================================================================
// Register helpers
// =====================================================================

unsigned FunctionalSim::gpr_index(unsigned reg_id) const {
    if (reg_id >= ARM64_REG_X0 && reg_id <= ARM64_REG_X28)
        return reg_id - ARM64_REG_X0;
    if (reg_id >= ARM64_REG_W0 && reg_id <= ARM64_REG_W28)
        return reg_id - ARM64_REG_W0;
    if (reg_id == ARM64_REG_FP || reg_id == ARM64_REG_X29) return 29;
    if (reg_id == ARM64_REG_LR || reg_id == ARM64_REG_X30) return 30;
    return 31;  // SP, WSP, XZR, WZR
}

bool FunctionalSim::is_32bit_op(unsigned reg_id) const {
    return (reg_id >= ARM64_REG_W0 && reg_id <= ARM64_REG_W30) ||
           reg_id == ARM64_REG_WSP ||
           reg_id == ARM64_REG_WZR;
}

uint64_t FunctionalSim::read_gpr(unsigned reg_id) const {
    if (reg_id == ARM64_REG_XZR || reg_id == ARM64_REG_WZR) return 0;
    if (reg_id == ARM64_REG_SP || reg_id == ARM64_REG_WSP) {
        return is_32bit_op(reg_id) ? (sp_ & 0xFFFFFFFFULL) : sp_;
    }
    unsigned idx = gpr_index(reg_id);
    if (idx >= 31) return 0;
    return is_32bit_op(reg_id) ? (regs_[idx] & 0xFFFFFFFFULL) : regs_[idx];
}

void FunctionalSim::write_gpr(unsigned reg_id, uint64_t val) {
    if (reg_id == ARM64_REG_XZR || reg_id == ARM64_REG_WZR) return;
    if (reg_id == ARM64_REG_SP || reg_id == ARM64_REG_WSP) {
        sp_ = is_32bit_op(reg_id) ? (sp_ & ~0xFFFFFFFFULL) | (val & 0xFFFFFFFFULL) : val;
        return;
    }
    unsigned idx = gpr_index(reg_id);
    if (idx >= 31) return;
    regs_[idx] = is_32bit_op(reg_id) ? (val & 0xFFFFFFFFULL) : val;
}

// =====================================================================
// Condition evaluation
// =====================================================================

bool FunctionalSim::eval_condition(unsigned cc) const {
    switch (cc) {
        case ARM64_CC_EQ:  return z_;
        case ARM64_CC_NE:  return !z_;
        case ARM64_CC_HS:  return c_;       // unsigned >=
        case ARM64_CC_LO:  return !c_;      // unsigned <
        case ARM64_CC_MI:  return n_;
        case ARM64_CC_PL:  return !n_;
        case ARM64_CC_VS:  return v_;
        case ARM64_CC_VC:  return !v_;
        case ARM64_CC_HI:  return c_ && !z_;   // unsigned >
        case ARM64_CC_LS:  return !c_ || z_;   // unsigned <=
        case ARM64_CC_GE:  return n_ == v_;
        case ARM64_CC_LT:  return n_ != v_;
        case ARM64_CC_GT:  return !z_ && (n_ == v_);
        case ARM64_CC_LE:  return z_ || (n_ != v_);
        case ARM64_CC_AL:  return true;
        case ARM64_CC_NV:  return true;       // deprecated, treat as always
        default:           return true;
    }
}

// =====================================================================
// NZCV flag helpers
// =====================================================================

void FunctionalSim::set_nzcv_add(uint64_t a, uint64_t b, uint64_t result, bool is_32bit) {
    uint64_t mask = is_32bit ? 0xFFFFFFFFULL : ~0ULL;
    n_ = (result >> (is_32bit ? 31 : 63)) & 1;
    z_ = (result & mask) == 0;
    c_ = (a & mask) + (b & mask) > (mask);
    v_ = (((a ^ result) & (b ^ result)) >> (is_32bit ? 31 : 63)) & 1;
}

void FunctionalSim::set_nzcv_sub(uint64_t a, uint64_t b, uint64_t result, bool is_32bit) {
    uint64_t mask = is_32bit ? 0xFFFFFFFFULL : ~0ULL;
    n_ = (result >> (is_32bit ? 31 : 63)) & 1;
    z_ = (result & mask) == 0;
    c_ = (a & mask) >= (b & mask);   // carry = no borrow
    v_ = (((a ^ b) & (a ^ result)) >> (is_32bit ? 31 : 63)) & 1;
}

void FunctionalSim::set_nzcv_logic(uint64_t result, bool is_32bit) {
    n_ = (result >> (is_32bit ? 31 : 63)) & 1;
    z_ = (result & (is_32bit ? 0xFFFFFFFFULL : ~0ULL)) == 0;
    c_ = false;
    v_ = false;
}

// =====================================================================
// Memory helpers
// =====================================================================

uint8_t FunctionalSim::mem_read8(uint64_t addr) {
    auto it = memory_.find(addr);
    return it != memory_.end() ? it->second : 0;
}

uint16_t FunctionalSim::mem_read16(uint64_t addr) {
    return static_cast<uint16_t>(mem_read8(addr)) |
           (static_cast<uint16_t>(mem_read8(addr + 1)) << 8);
}

uint32_t FunctionalSim::mem_read32(uint64_t addr) {
    return static_cast<uint32_t>(mem_read8(addr)) |
           (static_cast<uint32_t>(mem_read8(addr + 1)) << 8) |
           (static_cast<uint32_t>(mem_read8(addr + 2)) << 16) |
           (static_cast<uint32_t>(mem_read8(addr + 3)) << 24);
}

uint64_t FunctionalSim::mem_read64(uint64_t addr) {
    return static_cast<uint64_t>(mem_read32(addr)) |
           (static_cast<uint64_t>(mem_read32(addr + 4)) << 32);
}

void FunctionalSim::mem_write8(uint64_t addr, uint8_t val) {
    memory_[addr] = val;
}

void FunctionalSim::mem_write16(uint64_t addr, uint16_t val) {
    mem_write8(addr, val & 0xFF);
    mem_write8(addr + 1, (val >> 8) & 0xFF);
}

void FunctionalSim::mem_write32(uint64_t addr, uint32_t val) {
    mem_write8(addr, val & 0xFF);
    mem_write8(addr + 1, (val >> 8) & 0xFF);
    mem_write8(addr + 2, (val >> 16) & 0xFF);
    mem_write8(addr + 3, (val >> 24) & 0xFF);
}

void FunctionalSim::mem_write64(uint64_t addr, uint64_t val) {
    mem_write32(addr, static_cast<uint32_t>(val));
    mem_write32(addr + 4, static_cast<uint32_t>(val >> 32));
}

// =====================================================================
// Address computation for load/store
// =====================================================================

uint64_t FunctionalSim::compute_mem_addr(const cs_insn* insn, int op_index,
                                         uint64_t* writeback_val, bool* has_writeback)
{
    *writeback_val = 0;
    *has_writeback = false;

    if (!insn->detail) return 0;

    const auto& op = insn->detail->arm64.operands[op_index];
    uint64_t base_val = read_gpr(op.mem.base);
    uint64_t addr = base_val;

    if (op.mem.index != ARM64_REG_INVALID) {
        // Register offset
        uint64_t index_val = read_gpr(op.mem.index);

        // Check for shift/extend on the index register
        // In Capstone, the shift/extend info is on the operand itself
        // For ARM64, post-index uses the immediate as the writeback offset
        // and pre-index uses mem.disp as the offset with writeback

        // Check for LSL/extend on index
        const auto& arch = insn->detail->arm64;
        // Find the second register operand (the index)
        for (int i = 0; i < arch.op_count; ++i) {
            if (i != op_index && arch.operands[i].type == ARM64_OP_REG &&
                arch.operands[i].reg == op.mem.index) {
                auto shifter = arch.operands[i].shift;
                switch (shifter.type) {
                    case ARM64_SFT_LSL:
                        index_val <<= shifter.value;
                        break;
                    case ARM64_EXT_SXTW:
                        index_val = static_cast<uint64_t>(
                            static_cast<int64_t>(static_cast<int32_t>(
                                static_cast<uint32_t>(index_val)))) << shifter.value;
                        break;
                    case ARM64_EXT_UXTW:
                        index_val = (index_val & 0xFFFFFFFFULL) << shifter.value;
                        break;
                    case ARM64_EXT_SXTX:
                        index_val = static_cast<int64_t>(index_val) << shifter.value;
                        break;
                    case ARM64_EXT_UXTX:
                        index_val <<= shifter.value;
                        break;
                    default:
                        break;
                }
                break;
            }
        }

        addr = base_val + index_val;
    } else {
        // Immediate offset
        addr = base_val + static_cast<int64_t>(op.mem.disp);
    }

    // Check for writeback (pre-index or post-index)
    if (insn->detail->arm64.writeback) {
        *has_writeback = true;
        // For pre-index: addr = base + disp, writeback = addr
        // For post-index: addr = base, writeback = base + offset
        // Capstone reports disp=0 for post-index; parse offset from op_str

        // Detect post-index by checking the assembly text: "], #" means post-index
        // Pre-index:  e.g., "[sp, #-48]!" — has "]!" pattern
        bool is_post_index = false;
        int64_t post_index_offset = 0;
        if (insn->op_str) {
            const char* close_bracket = std::strchr(insn->op_str, ']');
            if (close_bracket && (close_bracket[1] == ',' || close_bracket[1] == ' ')) {
                is_post_index = true;
                // Parse the offset after '#' in "], #0x30" or "], #-48"
                const char* hash = std::strchr(close_bracket, '#');
                if (hash) {
                    post_index_offset = std::strtoll(hash + 1, nullptr, 0);
                }
            }
        }

        if (is_post_index) {
            // Post-index: access at base address, writeback = base + offset
            addr = base_val;
            *writeback_val = base_val + post_index_offset;
        } else {
            // Pre-index: access at base + disp, writeback = addr
            *writeback_val = addr;
        }
    }

    return addr;
}

// =====================================================================
// FunctionalSim::execute — instruction execution
// =====================================================================

bool FunctionalSim::execute(const cs_insn* insn) {
    if (!insn || !insn->detail) {
        return false;  // no detail — just advance PC
    }

    const auto& arch = insn->detail->arm64;
    const char* mnem = insn->mnemonic;

    // --- Helper lambdas ---
    auto get_reg_val = [&](int op_idx) -> uint64_t {
        if (op_idx < arch.op_count && arch.operands[op_idx].type == ARM64_OP_REG) {
            return read_gpr(arch.operands[op_idx].reg);
        }
        return 0;
    };

    auto get_imm = [&](int op_idx) -> int64_t {
        if (op_idx < arch.op_count) {
            if (arch.operands[op_idx].type == ARM64_OP_IMM)
                return arch.operands[op_idx].imm;
            if (arch.operands[op_idx].type == ARM64_OP_CIMM)
                return arch.operands[op_idx].imm;
        }
        return 0;
    };

    auto dst_reg = [&]() -> unsigned {
        // Find first write destination register
        for (int i = 0; i < arch.op_count; ++i) {
            if (arch.operands[i].type == ARM64_OP_REG &&
                (arch.operands[i].access & CS_AC_WRITE)) {
                return arch.operands[i].reg;
            }
        }
        return ARM64_REG_XZR;
    };

    auto is_32bit = [&]() -> bool {
        unsigned dr = dst_reg();
        return is_32bit_op(dr);
    };

    // Apply shift from shifter field
    auto apply_shift = [&](uint64_t val, int op_idx) -> uint64_t {
        if (op_idx < arch.op_count) {
            auto sft = arch.operands[op_idx].shift;
            if (sft.type == ARM64_SFT_LSL && sft.value > 0) {
                val = is_32bit() ? (val << sft.value) & 0xFFFFFFFFULL : val << sft.value;
            } else if (sft.type == ARM64_SFT_LSR) {
                uint32_t amt = is_32bit() ? 31 : 63;
                val = val >> (sft.value ? sft.value : amt);
            } else if (sft.type == ARM64_SFT_ASR) {
                uint32_t bits = is_32bit() ? 32 : 64;
                uint32_t amt = sft.value ? sft.value : bits;
                uint64_t sv = is_32bit() ? static_cast<int64_t>(static_cast<int32_t>(val))
                                         : static_cast<int64_t>(val);
                val = static_cast<uint64_t>(sv >> amt);
            } else if (sft.type == ARM64_SFT_ROR) {
                uint32_t bits = is_32bit() ? 32 : 64;
                uint32_t amt = sft.value % bits;
                val = (val >> amt) | (val << (bits - amt));
            }
        }
        return val;
    };

    auto apply_extend = [&](uint64_t val, int op_idx) -> uint64_t {
        if (op_idx < arch.op_count) {
            auto sft = arch.operands[op_idx].shift;
            switch (sft.type) {
                case ARM64_EXT_UXTW:
                    val = (val & 0xFFFFFFFFULL) << sft.value;
                    break;
                case ARM64_EXT_SXTW:
                    val = static_cast<uint64_t>(
                        static_cast<int64_t>(static_cast<int32_t>(val))) << sft.value;
                    break;
                case ARM64_EXT_SXTX:
                    val = static_cast<int64_t>(val) << sft.value;
                    break;
                case ARM64_EXT_UXTX:
                case ARM64_SFT_LSL:
                    val = val << sft.value;
                    break;
                default:
                    break;
            }
        }
        return val;
    };

    auto mask32 = [](uint64_t v) -> uint64_t { return v & 0xFFFFFFFFULL; };

    auto to_signed32 = [](uint64_t v) -> int64_t {
        return static_cast<int64_t>(static_cast<int32_t>(v));
    };

    // ================================================================
    // Branch instructions — these return true (PC updated)
    // ================================================================

    // Unconditional branch immediate: b, bl
    if (std::strcmp(mnem, "b") == 0) {
        uint64_t target = static_cast<uint64_t>(get_imm(0));
        pc_ = target;
        return true;
    }

    if (std::strcmp(mnem, "bl") == 0) {
        uint64_t target = static_cast<uint64_t>(get_imm(0));

        // PLT interception for dynamic ELF
        if (is_plt_call(target)) {
            auto it = plt_symbols_->find(target);
            std::string name = (it != plt_symbols_->end()) ? it->second : "unknown";
            return handle_external_call(pc_ + 4, name);
        }

        write_gpr(ARM64_REG_LR, pc_ + 4);  // set link register
        call_stack_.push_back(pc_ + 4);
        pc_ = target;
        return true;
    }

    // Conditional branch: b.cond
    if (std::strncmp(mnem, "b.", 2) == 0) {
        // b.eq, b.ne, etc.
        uint64_t target = static_cast<uint64_t>(get_imm(0));
        if (eval_condition(arch.cc)) {
            pc_ = target;
        } else {
            pc_ += 4;
        }
        return true;
    }

    // CBZ / CBNZ
    if (std::strcmp(mnem, "cbz") == 0 || std::strcmp(mnem, "cbnz") == 0) {
        bool is_cbz = (mnem[2] == 'z');
        uint64_t reg_val = get_reg_val(0);
        uint64_t target = static_cast<uint64_t>(get_imm(1));
        bool zero = (reg_val == 0);
        if (zero == is_cbz) {
            pc_ = target;
        } else {
            pc_ += 4;
        }
        return true;
    }

    // TBZ / TBNZ
    if (std::strcmp(mnem, "tbz") == 0 || std::strcmp(mnem, "tbnz") == 0) {
        bool is_tbz = (mnem[2] == 'z');
        uint64_t reg_val = get_reg_val(0);
        uint64_t bit_no = static_cast<uint64_t>(get_imm(1));
        uint64_t target = static_cast<uint64_t>(get_imm(2));
        bool bit_set = (reg_val >> bit_no) & 1;
        if (bit_set == !is_tbz) {
            pc_ = target;
        } else {
            pc_ += 4;
        }
        return true;
    }

    // RET
    if (std::strcmp(mnem, "ret") == 0) {
        unsigned ret_reg = ARM64_REG_LR;
        if (arch.op_count > 0 && arch.operands[0].type == ARM64_OP_REG) {
            ret_reg = arch.operands[0].reg;
        }
        uint64_t ret_addr = read_gpr(ret_reg);
        if (!call_stack_.empty()) {
            call_stack_.pop_back();
        }
        pc_ = ret_addr;
        return true;
    }

    // BR (unconditional branch to register)
    if (std::strcmp(mnem, "br") == 0) {
        uint64_t target = get_reg_val(0);

        // PLT interception for dynamic ELF (indirect call through GOT)
        if (is_plt_call(target)) {
            auto it = plt_symbols_->find(target);
            std::string name = (it != plt_symbols_->end()) ? it->second : "unknown";
            return handle_external_call(pc_ + 4, name);
        }

        pc_ = target;
        return true;
    }

    // BLR (branch with link to register)
    if (std::strcmp(mnem, "blr") == 0) {
        uint64_t target = get_reg_val(0);

        // PLT interception for dynamic ELF (indirect call through GOT)
        if (is_plt_call(target)) {
            write_gpr(ARM64_REG_LR, pc_ + 4);
            auto it = plt_symbols_->find(target);
            std::string name = (it != plt_symbols_->end()) ? it->second : "unknown";
            return handle_external_call(pc_ + 4, name);
        }

        write_gpr(ARM64_REG_LR, pc_ + 4);
        call_stack_.push_back(pc_ + 4);
        pc_ = target;
        return true;
    }

    // ERET
    if (std::strcmp(mnem, "eret") == 0) {
        // Cannot simulate — stop
        return false;
    }

    // ================================================================
    // NOP / barriers / hints — just advance PC
    // ================================================================
    if (std::strcmp(mnem, "nop") == 0 ||
        std::strcmp(mnem, "yield") == 0 ||
        std::strcmp(mnem, "wfe") == 0 ||
        std::strcmp(mnem, "wfi") == 0 ||
        std::strcmp(mnem, "sev") == 0 ||
        std::strcmp(mnem, "sevl") == 0 ||
        std::strcmp(mnem, "dmb") == 0 ||
        std::strcmp(mnem, "dsb") == 0 ||
        std::strcmp(mnem, "isb") == 0 ||
        std::strcmp(mnem, "hint") == 0 ||
        std::strcmp(mnem, "clrex") == 0) {
        return false;
    }

    // ================================================================
    // System instructions (msr, mrs, sys, ic, dc, tlbi, prfm)
    // ================================================================
    if (std::strcmp(mnem, "msr") == 0) {
        // Write system register — we don't model most system registers,
        // but handle TPIDR_EL0/DAIF for basic functionality
        // For now, just skip (no architectural state change we care about)
        return false;
    }

    if (std::strcmp(mnem, "mrs") == 0) {
        // Read system register — return 0 for most
        unsigned dr = dst_reg();
        write_gpr(dr, 0);
        return false;
    }

    if (std::strncmp(mnem, "sys", 3) == 0 || std::strncmp(mnem, "sysl", 4) == 0) {
        return false;
    }

    if (std::strncmp(mnem, "dc ", 3) == 0 || std::strncmp(mnem, "ic ", 3) == 0 ||
        std::strncmp(mnem, "tlbi ", 5) == 0 || std::strncmp(mnem, "prfm", 4) == 0) {
        return false;
    }

    // ================================================================
    // BRK / HLT — stop simulation
    // ================================================================
    if (std::strcmp(mnem, "brk") == 0 || std::strcmp(mnem, "hlt") == 0) {
        return false;
    }

    // ================================================================
    // Integer arithmetic: add, adds, sub, subs, neg, negs
    // ================================================================
    if (std::strcmp(mnem, "add") == 0 || std::strcmp(mnem, "adds") == 0) {
        bool sets_flags = (mnem[3] == 's');
        uint64_t a = get_reg_val(1);
        uint64_t b;
        if (arch.operands[2].type == ARM64_OP_REG) {
            // Check if this is an extended register (e.g., add x0, x1, w2, sxtw #2)
            auto sft = arch.operands[2].shift;
            if (sft.type >= ARM64_EXT_UXTW && sft.type <= ARM64_EXT_SXTX) {
                b = apply_extend(get_reg_val(2), 2);
            } else {
                b = apply_shift(get_reg_val(2), 2);
            }
        } else if (arch.operands[2].type == ARM64_OP_IMM) {
            b = static_cast<uint64_t>(arch.operands[2].imm);
        } else {
            b = apply_extend(get_reg_val(2), 2);
        }

        bool w = is_32bit();
        uint64_t result;
        if (w) {
            result = mask32(a + b);
        } else {
            result = a + b;
        }

        write_gpr(dst_reg(), result);

        if (sets_flags) {
            set_nzcv_add(a, b, result, w);
        }
        return false;
    }

    if (std::strcmp(mnem, "sub") == 0 || std::strcmp(mnem, "subs") == 0) {
        bool sets_flags = (mnem[3] == 's');
        uint64_t a = get_reg_val(1);
        uint64_t b;
        if (arch.operands[2].type == ARM64_OP_REG) {
            auto sft = arch.operands[2].shift;
            if (sft.type >= ARM64_EXT_UXTW && sft.type <= ARM64_EXT_SXTX) {
                b = apply_extend(get_reg_val(2), 2);
            } else {
                b = apply_shift(get_reg_val(2), 2);
            }
        } else if (arch.operands[2].type == ARM64_OP_IMM) {
            b = static_cast<uint64_t>(arch.operands[2].imm);
        } else {
            b = apply_extend(get_reg_val(2), 2);
        }

        bool w = is_32bit();
        uint64_t result;
        if (w) {
            result = mask32(a - b);
        } else {
            result = a - b;
        }

        write_gpr(dst_reg(), result);

        if (sets_flags) {
            set_nzcv_sub(a, b, result, w);
        }
        return false;
    }

    // NEG / NEGS = SUB 0, Xn (or SUBS XZR, Xn)
    if (std::strcmp(mnem, "neg") == 0 || std::strcmp(mnem, "negs") == 0) {
        bool sets_flags = (mnem[3] == 's');
        uint64_t b = get_reg_val(1);
        bool w = is_32bit();
        uint64_t result = w ? mask32(-static_cast<int64_t>(b)) : static_cast<uint64_t>(-static_cast<int64_t>(b));
        write_gpr(dst_reg(), result);
        if (sets_flags) {
            set_nzcv_sub(0, b, result, w);
        }
        return false;
    }

    // ADC / ADCS
    if (std::strcmp(mnem, "adc") == 0 || std::strcmp(mnem, "adcs") == 0) {
        bool sets_flags = (mnem[3] == 's');
        uint64_t a = get_reg_val(1);
        uint64_t b = get_reg_val(2);
        uint64_t carry_in = c_ ? 1 : 0;
        bool w = is_32bit();
        uint64_t result = w ? mask32(a + b + carry_in) : a + b + carry_in;
        write_gpr(dst_reg(), result);
        if (sets_flags) {
            set_nzcv_add(a, b + carry_in, result, w);
        }
        return false;
    }

    // SBC / SBCS
    if (std::strcmp(mnem, "sbc") == 0 || std::strcmp(mnem, "sbcs") == 0) {
        bool sets_flags = (mnem[3] == 's');
        uint64_t a = get_reg_val(1);
        uint64_t b = get_reg_val(2);
        uint64_t carry_in = c_ ? 1 : 0;
        bool w = is_32bit();
        uint64_t result = w ? mask32(a - b - (1 - carry_in)) : a - b - (1 - carry_in);
        write_gpr(dst_reg(), result);
        if (sets_flags) {
            set_nzcv_sub(a, b + (1 - carry_in), result, w);
        }
        return false;
    }

    // ================================================================
    // Multiply: mul, madd, msub, mneg
    // ================================================================
    if (std::strcmp(mnem, "mul") == 0) {
        bool w = is_32bit();
        uint64_t a = get_reg_val(1), b = get_reg_val(2);
        uint64_t result = w ? mask32(a * b) : a * b;
        write_gpr(dst_reg(), result);
        return false;
    }

    if (std::strcmp(mnem, "madd") == 0) {
        bool w = is_32bit();
        uint64_t a = get_reg_val(1), b = get_reg_val(2), c = get_reg_val(3);
        uint64_t result = w ? mask32(a * b + c) : a * b + c;
        write_gpr(dst_reg(), result);
        return false;
    }

    if (std::strcmp(mnem, "msub") == 0) {
        bool w = is_32bit();
        uint64_t a = get_reg_val(1), b = get_reg_val(2), c = get_reg_val(3);
        uint64_t result = w ? mask32(c - a * b) : c - a * b;
        write_gpr(dst_reg(), result);
        return false;
    }

    if (std::strcmp(mnem, "mneg") == 0) {
        bool w = is_32bit();
        uint64_t a = get_reg_val(1), b = get_reg_val(2);
        uint64_t result = w ? mask32(-(a * b)) : -(a * b);
        write_gpr(dst_reg(), result);
        return false;
    }

    // SMULL / UMULL
    if (std::strcmp(mnem, "smull") == 0) {
        int64_t a = static_cast<int64_t>(static_cast<int32_t>(get_reg_val(1)));
        int64_t b = static_cast<int64_t>(static_cast<int32_t>(get_reg_val(2)));
        write_gpr(dst_reg(), static_cast<uint64_t>(a * b));
        return false;
    }

    if (std::strcmp(mnem, "umull") == 0) {
        uint64_t a = get_reg_val(1) & 0xFFFFFFFFULL;
        uint64_t b = get_reg_val(2) & 0xFFFFFFFFULL;
        write_gpr(dst_reg(), a * b);
        return false;
    }

    // SDIV / UDIV
    if (std::strcmp(mnem, "sdiv") == 0) {
        int64_t a = static_cast<int64_t>(get_reg_val(1));
        int64_t b = static_cast<int64_t>(get_reg_val(2));
        write_gpr(dst_reg(), b != 0 ? static_cast<uint64_t>(a / b) : 0);
        return false;
    }

    if (std::strcmp(mnem, "udiv") == 0) {
        uint64_t a = get_reg_val(1), b = get_reg_val(2);
        write_gpr(dst_reg(), b != 0 ? a / b : 0);
        return false;
    }

    // ================================================================
    // Logical: and, ands, orr, orrs, eor, eors, bic, bics
    // ================================================================
    if (std::strcmp(mnem, "and") == 0 || std::strcmp(mnem, "ands") == 0 ||
        std::strcmp(mnem, "orr") == 0 || std::strcmp(mnem, "orrs") == 0 ||
        std::strcmp(mnem, "eor") == 0 || std::strcmp(mnem, "eors") == 0 ||
        std::strcmp(mnem, "bic") == 0 || std::strcmp(mnem, "bics") == 0) {

        bool sets_flags = (std::strlen(mnem) >= 3 && mnem[std::strlen(mnem)-1] == 's');
        char op_char = mnem[0];
        uint64_t a = get_reg_val(1);
        uint64_t b;
        if (arch.operands[2].type == ARM64_OP_REG) {
            b = apply_shift(get_reg_val(2), 2);
        } else {
            b = static_cast<uint64_t>(arch.operands[2].imm);
        }

        // BIC/BICS: b = ~b
        bool is_bic = (op_char == 'b');
        if (is_bic) b = ~b;

        uint64_t result;
        switch (op_char) {
            case 'a': result = a & b; break;
            case 'o': result = a | b; break;
            case 'e': result = a ^ b; break;
            default:  result = a & b; break;
        }

        bool w = is_32bit();
        if (w) result = mask32(result);
        write_gpr(dst_reg(), result);

        if (sets_flags) {
            set_nzcv_logic(result, w);
        }
        return false;
    }

    // ================================================================
    // Shift/rotate: lsl, lsr, asr, ror
    // ================================================================
    if (std::strcmp(mnem, "lsl") == 0 || std::strcmp(mnem, "lslv") == 0) {
        bool w = is_32bit();
        uint64_t val = get_reg_val(1);
        uint64_t amt = get_reg_val(2) & (w ? 31 : 63);
        write_gpr(dst_reg(), w ? mask32(val << amt) : (val << amt));
        return false;
    }

    if (std::strcmp(mnem, "lsr") == 0 || std::strcmp(mnem, "lsrv") == 0) {
        bool w = is_32bit();
        uint64_t val = get_reg_val(1);
        uint64_t amt = get_reg_val(2) & (w ? 31 : 63);
        if (amt == 0) amt = w ? 32 : 64;
        write_gpr(dst_reg(), val >> amt);
        return false;
    }

    if (std::strcmp(mnem, "asr") == 0 || std::strcmp(mnem, "asrv") == 0) {
        bool w = is_32bit();
        uint64_t val = get_reg_val(1);
        uint64_t amt = get_reg_val(2) & (w ? 31 : 63);
        if (amt == 0) amt = w ? 32 : 64;
        int64_t sv = w ? static_cast<int32_t>(val) : static_cast<int64_t>(val);
        write_gpr(dst_reg(), static_cast<uint64_t>(sv >> amt));
        return false;
    }

    if (std::strcmp(mnem, "ror") == 0 || std::strcmp(mnem, "rorv") == 0) {
        bool w = is_32bit();
        uint64_t val = get_reg_val(1);
        uint64_t amt = get_reg_val(2) & (w ? 31 : 63);
        uint32_t bits = w ? 32 : 64;
        if (amt == 0) amt = bits;
        write_gpr(dst_reg(), (val >> amt) | (val << (bits - amt)));
        return false;
    }

    // ================================================================
    // Move: mov, movz, movk, movn, mvn, movi (for GPR)
    // ================================================================
    if (std::strcmp(mnem, "mov") == 0) {
        // MOV can be either register-to-register (ORR Xd, XZR, Xs)
        // or MOVZ alias (move immediate). Check operand type.
        unsigned dst = dst_reg();
        uint64_t val;
        if (arch.op_count > 1 && arch.operands[1].type == ARM64_OP_REG) {
            // Register-to-register move: mov x1, x19
            val = get_reg_val(1);
        } else {
            // Immediate move (MOVZ alias): mov x0, #10
            val = static_cast<uint64_t>(get_imm(1));
            for (int i = 0; i < arch.op_count; ++i) {
                if (arch.operands[i].shift.type == ARM64_SFT_LSL) {
                    val <<= arch.operands[i].shift.value;
                }
            }
        }
        write_gpr(dst, is_32bit() ? mask32(val) : val);
        return false;
    }

    if (std::strcmp(mnem, "movz") == 0) {
        uint64_t imm = static_cast<uint64_t>(get_imm(1));
        // Check for LSL #shift (movz with shift)
        for (int i = 0; i < arch.op_count; ++i) {
            if (arch.operands[i].shift.type == ARM64_SFT_LSL) {
                imm <<= arch.operands[i].shift.value;
            }
        }
        write_gpr(dst_reg(), is_32bit() ? mask32(imm) : imm);
        return false;
    }

    if (std::strcmp(mnem, "movk") == 0) {
        uint64_t imm = static_cast<uint64_t>(get_imm(1)) & 0xFFFFULL;
        unsigned shift = 0;
        for (int i = 0; i < arch.op_count; ++i) {
            if (arch.operands[i].shift.type == ARM64_SFT_LSL) {
                shift = arch.operands[i].shift.value;
            }
        }
        uint64_t mask = ~(0xFFFFULL << shift);
        uint64_t old_val = get_reg_val(0);
        write_gpr(dst_reg(), (old_val & mask) | (imm << shift));
        return false;
    }

    if (std::strcmp(mnem, "movn") == 0) {
        uint64_t imm = static_cast<uint64_t>(get_imm(1));
        for (int i = 0; i < arch.op_count; ++i) {
            if (arch.operands[i].shift.type == ARM64_SFT_LSL) {
                imm <<= arch.operands[i].shift.value;
            }
        }
        uint64_t result = ~(imm);
        write_gpr(dst_reg(), is_32bit() ? mask32(result) : result);
        return false;
    }

    if (std::strcmp(mnem, "mvn") == 0) {
        uint64_t b;
        if (arch.operands[1].type == ARM64_OP_REG) {
            b = apply_shift(get_reg_val(1), 1);
        } else {
            b = static_cast<uint64_t>(get_imm(1));
        }
        uint64_t result = ~b;
        write_gpr(dst_reg(), is_32bit() ? mask32(result) : result);
        return false;
    }

    // ================================================================
    // Compare: cmp, cmn, tst, ccmp
    // ================================================================
    if (std::strcmp(mnem, "cmp") == 0) {
        uint64_t a = get_reg_val(0);
        uint64_t b;
        bool w = is_32bit_op(arch.operands[0].reg);
        if (arch.operands[1].type == ARM64_OP_REG) {
            b = apply_shift(get_reg_val(1), 1);
        } else if (arch.operands[1].type == ARM64_OP_IMM) {
            b = static_cast<uint64_t>(arch.operands[1].imm);
        } else {
            b = apply_extend(get_reg_val(1), 1);
        }
        uint64_t result = w ? mask32(a - b) : a - b;
        set_nzcv_sub(a, b, result, w);
        return false;
    }

    if (std::strcmp(mnem, "cmn") == 0) {
        uint64_t a = get_reg_val(0);
        uint64_t b;
        bool w = is_32bit_op(arch.operands[0].reg);
        if (arch.operands[1].type == ARM64_OP_REG) {
            b = apply_shift(get_reg_val(1), 1);
        } else {
            b = static_cast<uint64_t>(arch.operands[1].imm);
        }
        uint64_t result = w ? mask32(a + b) : a + b;
        set_nzcv_add(a, b, result, w);
        return false;
    }

    if (std::strcmp(mnem, "tst") == 0) {
        uint64_t a = get_reg_val(0);
        uint64_t b;
        bool w = is_32bit_op(arch.operands[0].reg);
        if (arch.operands[1].type == ARM64_OP_REG) {
            b = apply_shift(get_reg_val(1), 1);
        } else {
            b = static_cast<uint64_t>(arch.operands[1].imm);
        }
        uint64_t result = a & b;
        if (w) result = mask32(result);
        set_nzcv_logic(result, w);
        return false;
    }

    // CCMP / CCMN
    if (std::strncmp(mnem, "ccmp", 4) == 0 || std::strncmp(mnem, "ccmn", 4) == 0) {
        bool is_cmn = (mnem[3] == 'n');
        bool cond_met = eval_condition(arch.cc);
        uint64_t a = get_reg_val(0);
        uint64_t b = static_cast<uint64_t>(get_imm(1));
        uint64_t nzcv = static_cast<uint64_t>(get_imm(2));
        bool w = is_32bit_op(arch.operands[0].reg);

        if (cond_met) {
            uint64_t result = is_cmn
                ? (w ? mask32(a + b) : a + b)
                : (w ? mask32(a - b) : a - b);
            if (is_cmn) set_nzcv_add(a, b, result, w);
            else set_nzcv_sub(a, b, result, w);
        } else {
            n_ = (nzcv >> 3) & 1;
            z_ = (nzcv >> 2) & 1;
            c_ = (nzcv >> 1) & 1;
            v_ = nzcv & 1;
        }
        return false;
    }

    // ================================================================
    // Conditional select: csel, cset, csetm, cinc, cinv, cneg
    // ================================================================
    if (std::strcmp(mnem, "csel") == 0) {
        uint64_t a = get_reg_val(1), b = get_reg_val(2);
        write_gpr(dst_reg(), eval_condition(arch.cc) ? a : b);
        return false;
    }

    if (std::strcmp(mnem, "cset") == 0 || std::strcmp(mnem, "csetm") == 0) {
        bool is_m = (mnem[4] == 'm');
        uint64_t val = eval_condition(arch.cc) ? 1 : 0;
        if (is_m) {
            bool w = is_32bit();
            val = eval_condition(arch.cc) ? (w ? 0xFFFFFFFFULL : ~0ULL) : 0;
        }
        write_gpr(dst_reg(), is_32bit() ? mask32(val) : val);
        return false;
    }

    if (std::strcmp(mnem, "cinc") == 0) {
        uint64_t a = get_reg_val(1);
        bool w = is_32bit();
        uint64_t result = eval_condition(arch.cc) ? a + 1 : a;
        write_gpr(dst_reg(), w ? mask32(result) : result);
        return false;
    }

    if (std::strcmp(mnem, "cinv") == 0) {
        uint64_t a = get_reg_val(1);
        bool w = is_32bit();
        uint64_t result = eval_condition(arch.cc) ? ~a : a;
        write_gpr(dst_reg(), w ? mask32(result) : result);
        return false;
    }

    if (std::strcmp(mnem, "cneg") == 0) {
        uint64_t a = get_reg_val(1);
        bool w = is_32bit();
        uint64_t result = eval_condition(arch.cc)
            ? (w ? mask32(-static_cast<int64_t>(a)) : static_cast<uint64_t>(-static_cast<int64_t>(a)))
            : a;
        write_gpr(dst_reg(), result);
        return false;
    }

    // ================================================================
    // Extend: sxtb, sxth, sxtw, uxtb, uxth
    // ================================================================
    if (std::strcmp(mnem, "sxtb") == 0) {
        int8_t v = static_cast<int8_t>(get_reg_val(1));
        write_gpr(dst_reg(), is_32bit() ? mask32(static_cast<uint64_t>(v)) : static_cast<uint64_t>(static_cast<int64_t>(v)));
        return false;
    }
    if (std::strcmp(mnem, "sxth") == 0) {
        int16_t v = static_cast<int16_t>(get_reg_val(1));
        write_gpr(dst_reg(), is_32bit() ? mask32(static_cast<uint64_t>(v)) : static_cast<uint64_t>(static_cast<int64_t>(v)));
        return false;
    }
    if (std::strcmp(mnem, "sxtw") == 0) {
        int32_t v = static_cast<int32_t>(get_reg_val(1));
        write_gpr(dst_reg(), static_cast<uint64_t>(static_cast<int64_t>(v)));
        return false;
    }
    if (std::strcmp(mnem, "uxtb") == 0) {
        write_gpr(dst_reg(), get_reg_val(1) & 0xFF);
        return false;
    }
    if (std::strcmp(mnem, "uxth") == 0) {
        write_gpr(dst_reg(), get_reg_val(1) & 0xFFFF);
        return false;
    }

    // CLS / CLZ
    if (std::strcmp(mnem, "clz") == 0) {
        bool w = is_32bit();
        uint64_t val = get_reg_val(1);
        int count = 0;
        int bits = w ? 32 : 64;
        uint64_t mask = w ? 0x80000000ULL : 0x8000000000000000ULL;
        while (count < bits && !(val & mask)) {
            val <<= 1;
            count++;
        }
        write_gpr(dst_reg(), static_cast<uint64_t>(count));
        return false;
    }

    if (std::strcmp(mnem, "cls") == 0) {
        bool w = is_32bit();
        int64_t val = w ? static_cast<int32_t>(get_reg_val(1)) : static_cast<int64_t>(get_reg_val(1));
        int count = 0;
        int bits = w ? 32 : 64;
        int sign = (val < 0) ? -1 : 0;
        while (count < bits - 1 && ((val < 0) ? sign == -1 : sign == 0)) {
            val <<= 1;
            if ((val < 0) != (sign == -1)) break;
            count++;
        }
        write_gpr(dst_reg(), static_cast<uint64_t>(count));
        return false;
    }

    // RBIT / REV
    if (std::strcmp(mnem, "rbit") == 0) {
        uint64_t val = get_reg_val(1);
        uint64_t result = 0;
        int bits = is_32bit() ? 32 : 64;
        for (int i = 0; i < bits; ++i) {
            if (val & (1ULL << i)) result |= 1ULL << (bits - 1 - i);
        }
        write_gpr(dst_reg(), result);
        return false;
    }

    if (std::strcmp(mnem, "rev") == 0 || std::strcmp(mnem, "rev32") == 0 ||
        std::strcmp(mnem, "rev16") == 0) {
        uint64_t val = get_reg_val(1);
        uint64_t result = 0;
        bool w = is_32bit();
        if (std::strcmp(mnem, "rev16") == 0) {
            // Reverse bytes in each halfword
            for (int hw = 0; hw < (w ? 2 : 4); ++hw) {
                uint64_t base = hw * 16;
                result |= ((val >> (base + 8)) & 0xFF) << base;
                result |= ((val >> base) & 0xFF) << (base + 8);
            }
        } else {
            // Reverse byte order
            int bytes = w ? 4 : 8;
            for (int i = 0; i < bytes; ++i) {
                result |= ((val >> (i * 8)) & 0xFF) << ((bytes - 1 - i) * 8);
            }
        }
        write_gpr(dst_reg(), w ? mask32(result) : result);
        return false;
    }

    // EXTR
    if (std::strcmp(mnem, "extr") == 0) {
        bool w = is_32bit();
        uint64_t a = get_reg_val(1), b = get_reg_val(2);
        uint64_t lsb = static_cast<uint64_t>(get_imm(3));
        uint64_t concat = (b << (w ? 32 : 64)) | a;
        uint64_t result = (concat >> lsb) & (w ? 0xFFFFFFFFULL : ~0ULL);
        write_gpr(dst_reg(), result);
        return false;
    }

    // ================================================================
    // ADR / ADRP
    // ================================================================
    if (std::strcmp(mnem, "adr") == 0) {
        int64_t imm = get_imm(1);
        write_gpr(dst_reg(), pc_ + static_cast<uint64_t>(imm));
        return false;
    }

    if (std::strcmp(mnem, "adrp") == 0) {
        int64_t imm = get_imm(1);
        uint64_t base = pc_ & ~0xFFFULL;  // align to 4KB page
        write_gpr(dst_reg(), base + static_cast<uint64_t>(imm));
        return false;
    }

    // ================================================================
    // Load instructions
    // ================================================================

    // LDR (register, 32/64-bit)
    if (std::strcmp(mnem, "ldr") == 0) {
        unsigned dr = dst_reg();
        bool w = is_32bit_op(dr);

        // Find the memory operand
        for (int i = 0; i < arch.op_count; ++i) {
            if (arch.operands[i].type == ARM64_OP_MEM) {
                uint64_t wb_val;
                bool has_wb;
                uint64_t addr = compute_mem_addr(insn, i, &wb_val, &has_wb);

                uint64_t val = w ? mem_read32(addr) : mem_read64(addr);
                write_gpr(dr, val);

                if (has_wb) {
                    write_gpr(arch.operands[i].mem.base, wb_val);
                }
                break;
            }
        }
        return false;
    }

    // LDRB
    if (std::strcmp(mnem, "ldrb") == 0) {
        for (int i = 0; i < arch.op_count; ++i) {
            if (arch.operands[i].type == ARM64_OP_MEM) {
                uint64_t wb_val;
                bool has_wb;
                uint64_t addr = compute_mem_addr(insn, i, &wb_val, &has_wb);
                write_gpr(dst_reg(), mem_read8(addr));
                if (has_wb) write_gpr(arch.operands[i].mem.base, wb_val);
                break;
            }
        }
        return false;
    }

    // LDRH
    if (std::strcmp(mnem, "ldrh") == 0) {
        for (int i = 0; i < arch.op_count; ++i) {
            if (arch.operands[i].type == ARM64_OP_MEM) {
                uint64_t wb_val;
                bool has_wb;
                uint64_t addr = compute_mem_addr(insn, i, &wb_val, &has_wb);
                write_gpr(dst_reg(), mem_read16(addr));
                if (has_wb) write_gpr(arch.operands[i].mem.base, wb_val);
                break;
            }
        }
        return false;
    }

    // LDRSB / LDRSH / LDRSW
    if (std::strcmp(mnem, "ldrsb") == 0) {
        for (int i = 0; i < arch.op_count; ++i) {
            if (arch.operands[i].type == ARM64_OP_MEM) {
                uint64_t wb_val; bool has_wb;
                uint64_t addr = compute_mem_addr(insn, i, &wb_val, &has_wb);
                int8_t v = static_cast<int8_t>(mem_read8(addr));
                write_gpr(dst_reg(), static_cast<uint64_t>(static_cast<int64_t>(v)));
                if (has_wb) write_gpr(arch.operands[i].mem.base, wb_val);
                break;
            }
        }
        return false;
    }

    if (std::strcmp(mnem, "ldrsh") == 0) {
        for (int i = 0; i < arch.op_count; ++i) {
            if (arch.operands[i].type == ARM64_OP_MEM) {
                uint64_t wb_val; bool has_wb;
                uint64_t addr = compute_mem_addr(insn, i, &wb_val, &has_wb);
                int16_t v = static_cast<int16_t>(mem_read16(addr));
                write_gpr(dst_reg(), is_32bit() ? mask32(static_cast<uint32_t>(v))
                                                : static_cast<uint64_t>(static_cast<int64_t>(v)));
                if (has_wb) write_gpr(arch.operands[i].mem.base, wb_val);
                break;
            }
        }
        return false;
    }

    if (std::strcmp(mnem, "ldrsw") == 0) {
        for (int i = 0; i < arch.op_count; ++i) {
            if (arch.operands[i].type == ARM64_OP_MEM) {
                uint64_t wb_val; bool has_wb;
                uint64_t addr = compute_mem_addr(insn, i, &wb_val, &has_wb);
                int32_t v = static_cast<int32_t>(mem_read32(addr));
                write_gpr(dst_reg(), static_cast<uint64_t>(static_cast<int64_t>(v)));
                if (has_wb) write_gpr(arch.operands[i].mem.base, wb_val);
                break;
            }
        }
        return false;
    }

    // LDUR / LDURB / LDURH (unscaled)
    if (std::strcmp(mnem, "ldur") == 0 || std::strcmp(mnem, "ldurb") == 0 ||
        std::strcmp(mnem, "ldurh") == 0) {
        unsigned dr = dst_reg();
        bool w = is_32bit_op(dr);
        for (int i = 0; i < arch.op_count; ++i) {
            if (arch.operands[i].type == ARM64_OP_MEM) {
                uint64_t addr = read_gpr(arch.operands[i].mem.base) +
                    static_cast<int64_t>(arch.operands[i].mem.disp);
                if (std::strcmp(mnem, "ldur") == 0)
                    write_gpr(dr, w ? mem_read32(addr) : mem_read64(addr));
                else if (std::strcmp(mnem, "ldurb") == 0)
                    write_gpr(dr, mem_read8(addr));
                else
                    write_gpr(dr, mem_read16(addr));
                break;
            }
        }
        return false;
    }

    // LDP (load pair)
    if (std::strcmp(mnem, "ldp") == 0) {
        unsigned rt = arch.operands[0].reg;
        unsigned rt2 = arch.operands[1].reg;
        bool w = is_32bit_op(rt);

        for (int i = 0; i < arch.op_count; ++i) {
            if (arch.operands[i].type == ARM64_OP_MEM) {
                uint64_t wb_val; bool has_wb;
                uint64_t addr = compute_mem_addr(insn, i, &wb_val, &has_wb);
                if (w) {
                    write_gpr(rt, mem_read32(addr));
                    write_gpr(rt2, mem_read32(addr + 4));
                } else {
                    write_gpr(rt, mem_read64(addr));
                    write_gpr(rt2, mem_read64(addr + 8));
                }
                if (has_wb) write_gpr(arch.operands[i].mem.base, wb_val);
                break;
            }
        }
        return false;
    }

    // LDXR / LDXP (exclusive load — treat as normal load)
    if (std::strcmp(mnem, "ldxr") == 0) {
        unsigned dr = dst_reg();
        bool w = is_32bit_op(dr);
        for (int i = 0; i < arch.op_count; ++i) {
            if (arch.operands[i].type == ARM64_OP_MEM) {
                uint64_t addr = read_gpr(arch.operands[i].mem.base);
                write_gpr(dr, w ? mem_read32(addr) : mem_read64(addr));
                break;
            }
        }
        return false;
    }

    if (std::strcmp(mnem, "ldxp") == 0) {
        unsigned rt = arch.operands[0].reg, rt2 = arch.operands[1].reg;
        bool w = is_32bit_op(rt);
        for (int i = 0; i < arch.op_count; ++i) {
            if (arch.operands[i].type == ARM64_OP_MEM) {
                uint64_t addr = read_gpr(arch.operands[i].mem.base);
                if (w) {
                    write_gpr(rt, mem_read32(addr));
                    write_gpr(rt2, mem_read32(addr + 4));
                } else {
                    write_gpr(rt, mem_read64(addr));
                    write_gpr(rt2, mem_read64(addr + 8));
                }
                break;
            }
        }
        return false;
    }

    // ================================================================
    // Store instructions
    // ================================================================

    // STR
    if (std::strcmp(mnem, "str") == 0) {
        for (int i = 0; i < arch.op_count; ++i) {
            if (arch.operands[i].type == ARM64_OP_MEM) {
                uint64_t wb_val; bool has_wb;
                uint64_t addr = compute_mem_addr(insn, i, &wb_val, &has_wb);
                unsigned sr = arch.operands[0].reg;
                bool w = is_32bit_op(sr);
                if (w) mem_write32(addr, static_cast<uint32_t>(read_gpr(sr)));
                else mem_write64(addr, read_gpr(sr));
                if (has_wb) write_gpr(arch.operands[i].mem.base, wb_val);
                break;
            }
        }
        return false;
    }

    // STRB
    if (std::strcmp(mnem, "strb") == 0) {
        for (int i = 0; i < arch.op_count; ++i) {
            if (arch.operands[i].type == ARM64_OP_MEM) {
                uint64_t wb_val; bool has_wb;
                uint64_t addr = compute_mem_addr(insn, i, &wb_val, &has_wb);
                mem_write8(addr, static_cast<uint8_t>(read_gpr(arch.operands[0].reg)));
                if (has_wb) write_gpr(arch.operands[i].mem.base, wb_val);
                break;
            }
        }
        return false;
    }

    // STRH
    if (std::strcmp(mnem, "strh") == 0) {
        for (int i = 0; i < arch.op_count; ++i) {
            if (arch.operands[i].type == ARM64_OP_MEM) {
                uint64_t wb_val; bool has_wb;
                uint64_t addr = compute_mem_addr(insn, i, &wb_val, &has_wb);
                mem_write16(addr, static_cast<uint16_t>(read_gpr(arch.operands[0].reg)));
                if (has_wb) write_gpr(arch.operands[i].mem.base, wb_val);
                break;
            }
        }
        return false;
    }

    // STUR / STURB / STURH
    if (std::strcmp(mnem, "stur") == 0 || std::strcmp(mnem, "sturb") == 0 ||
        std::strcmp(mnem, "sturh") == 0) {
        unsigned sr = arch.operands[0].reg;
        bool w = is_32bit_op(sr);
        for (int i = 0; i < arch.op_count; ++i) {
            if (arch.operands[i].type == ARM64_OP_MEM) {
                uint64_t addr = read_gpr(arch.operands[i].mem.base) +
                    static_cast<int64_t>(arch.operands[i].mem.disp);
                if (std::strcmp(mnem, "stur") == 0) {
                    if (w) mem_write32(addr, static_cast<uint32_t>(read_gpr(sr)));
                    else mem_write64(addr, read_gpr(sr));
                } else if (std::strcmp(mnem, "sturb") == 0) {
                    mem_write8(addr, static_cast<uint8_t>(read_gpr(sr)));
                } else {
                    mem_write16(addr, static_cast<uint16_t>(read_gpr(sr)));
                }
                break;
            }
        }
        return false;
    }

    // STP (store pair)
    if (std::strcmp(mnem, "stp") == 0) {
        unsigned rt = arch.operands[0].reg, rt2 = arch.operands[1].reg;
        bool w = is_32bit_op(rt);
        for (int i = 0; i < arch.op_count; ++i) {
            if (arch.operands[i].type == ARM64_OP_MEM) {
                uint64_t wb_val; bool has_wb;
                uint64_t addr = compute_mem_addr(insn, i, &wb_val, &has_wb);
                if (w) {
                    mem_write32(addr, static_cast<uint32_t>(read_gpr(rt)));
                    mem_write32(addr + 4, static_cast<uint32_t>(read_gpr(rt2)));
                } else {
                    mem_write64(addr, read_gpr(rt));
                    mem_write64(addr + 8, read_gpr(rt2));
                }
                if (has_wb) write_gpr(arch.operands[i].mem.base, wb_val);
                break;
            }
        }
        return false;
    }

    // STXR / STXP (exclusive store — treat as normal store)
    if (std::strcmp(mnem, "stxr") == 0) {
        unsigned sr = arch.operands[0].reg;
        bool w = is_32bit_op(sr);
        for (int i = 0; i < arch.op_count; ++i) {
            if (arch.operands[i].type == ARM64_OP_MEM) {
                uint64_t addr = read_gpr(arch.operands[i].mem.base);
                if (w) mem_write32(addr, static_cast<uint32_t>(read_gpr(sr)));
                else mem_write64(addr, read_gpr(sr));
                // STXR writes status to a register — find it
                for (int j = 0; j < arch.op_count; ++j) {
                    if (j != i && arch.operands[j].type == ARM64_OP_REG &&
                        arch.operands[j].reg != sr) {
                        write_gpr(arch.operands[j].reg, 0);  // always succeed
                    }
                }
                break;
            }
        }
        return false;
    }

    if (std::strcmp(mnem, "stxp") == 0) {
        unsigned rt = arch.operands[0].reg, rt2 = arch.operands[1].reg;
        bool w = is_32bit_op(rt);
        for (int i = 0; i < arch.op_count; ++i) {
            if (arch.operands[i].type == ARM64_OP_MEM) {
                uint64_t addr = read_gpr(arch.operands[i].mem.base);
                if (w) {
                    mem_write32(addr, static_cast<uint32_t>(read_gpr(rt)));
                    mem_write32(addr + 4, static_cast<uint32_t>(read_gpr(rt2)));
                } else {
                    mem_write64(addr, read_gpr(rt));
                    mem_write64(addr + 8, read_gpr(rt2));
                }
                // Find status register
                for (int j = 0; j < arch.op_count; ++j) {
                    if (j != i && arch.operands[j].type == ARM64_OP_REG &&
                        arch.operands[j].reg != rt && arch.operands[j].reg != rt2) {
                        write_gpr(arch.operands[j].reg, 0);
                    }
                }
                break;
            }
        }
        return false;
    }

    // CAS (compare and swap)
    if (std::strncmp(mnem, "cas", 3) == 0) {
        // Simplified: always succeed, write new value
        for (int i = 0; i < arch.op_count; ++i) {
            if (arch.operands[i].type == ARM64_OP_MEM) {
                uint64_t addr = read_gpr(arch.operands[i].mem.base);
                // Find the source register (new value to store)
                for (int j = 0; j < arch.op_count; ++j) {
                    if (arch.operands[j].type == ARM64_OP_REG &&
                        arch.operands[j].reg != arch.operands[i].mem.base) {
                        bool w = is_32bit_op(arch.operands[j].reg);
                        if (w) mem_write32(addr, static_cast<uint32_t>(read_gpr(arch.operands[j].reg)));
                        else mem_write64(addr, read_gpr(arch.operands[j].reg));
                        break;
                    }
                }
                break;
            }
        }
        return false;
    }

    // ================================================================
    // SIMD/NEON/FP/SVE/SME/Crypto — just advance PC
    // (These don't affect integer branch conditions in most programs)
    // ================================================================
    return false;
}

// =====================================================================
// PLT interception — dynamic linking support
// =====================================================================

bool FunctionalSim::is_plt_call(uint64_t target) const {
    if (!plt_symbols_ || plt_symbols_->empty()) return false;
    return plt_symbols_->find(target) != plt_symbols_->end();
}

// Helper: read a null-terminated string from simulator memory
static std::string read_cstring(const std::unordered_map<uint64_t, uint8_t>& mem, uint64_t addr) {
    std::string result;
    while (true) {
        auto it = mem.find(addr);
        if (it == mem.end() || it->second == 0) break;
        result += static_cast<char>(it->second);
        addr++;
        if (result.size() > 4096) break;  // safety limit
    }
    return result;
}

bool FunctionalSim::handle_external_call(uint64_t lr_save, const std::string& name) {
    // __libc_start_main: X0=main, X1=argc, X2=argv
    // Jump to main() directly, set LR so main's return goes to abort@plt
    if (name == "__libc_start_main" || name == "__libc_start_main_impl") {
        uint64_t main_addr = read_gpr(ARM64_REG_X0);
        uint64_t argc_val = read_gpr(ARM64_REG_X1);
        uint64_t argv_val = read_gpr(ARM64_REG_X2);
        // main(int argc, char *argv[]) expects: X0=argc, X1=argv
        // Default argc to 1 if stack was empty (no kernel setup)
        if (argc_val == 0 || argc_val > 1000) argc_val = 1;
        if (argv_val == 0) {
            // Create a minimal argv: argv[0] = NULL
            uint64_t argv_addr = sp_ - 16;
            mem_write64(argv_addr, 0);  // argv[0] = NULL
            argv_val = argv_addr;
        }
        write_gpr(ARM64_REG_X0, argc_val);
        write_gpr(ARM64_REG_X1, argv_val);
        write_gpr(ARM64_REG_LR, lr_save);  // main's return -> _start's abort call
        call_stack_.push_back(lr_save);
        pc_ = main_addr;
        return true;
    }

    // printf family
    if (name == "printf" || name == "fprintf" || name == "sprintf" || name == "snprintf") {
        stub_printf(name);
        pc_ = lr_save;
        return true;
    }
    if (name == "__printf_chk" || name == "__fprintf_chk" || name == "__sprintf_chk" || name == "__snprintf_chk") {
        // __printf_chk(flag, fmt, ...) — same as printf but X0=flag (ignored), X1=format
        stub_printf(name);
        pc_ = lr_save;
        return true;
    }
    if (name == "puts") {
        stub_puts();
        pc_ = lr_save;
        return true;
    }
    if (name == "putchar") {
        uint64_t ch = read_gpr(ARM64_REG_X0);
        std::fprintf(stderr, "%c", (int)ch);
        write_gpr(ARM64_REG_X0, ch);
        pc_ = lr_save;
        return true;
    }
    if (name == "malloc") {
        stub_malloc();
        pc_ = lr_save;
        return true;
    }
    if (name == "calloc") {
        stub_calloc();
        pc_ = lr_save;
        return true;
    }
    if (name == "realloc") {
        // Simple: just return the old pointer (no actual realloc)
        uint64_t old_ptr = read_gpr(ARM64_REG_X0);
        write_gpr(ARM64_REG_X0, old_ptr);
        pc_ = lr_save;
        return true;
    }
    if (name == "free") {
        // no-op
        pc_ = lr_save;
        return true;
    }
    if (name == "strlen") {
        stub_strlen();
        pc_ = lr_save;
        return true;
    }
    if (name == "strcpy" || name == "__strcpy_chk") {
        stub_strcpy(false);
        pc_ = lr_save;
        return true;
    }
    if (name == "strncpy" || name == "__strncpy_chk") {
        stub_strcpy(true);
        pc_ = lr_save;
        return true;
    }
    if (name == "strcat" || name == "__strcat_chk") {
        // __strcat_chk(dst, src, flag) — flag ignored
        // strcat(dst, src): X0=dst, X1=src
        // Same register layout for both
        uint64_t dst = read_gpr(ARM64_REG_X0);
        uint64_t src = read_gpr(ARM64_REG_X1);
        // Find end of dst
        uint64_t end = dst;
        while (true) {
            auto it = memory_.find(end);
            if (it == memory_.end() || it->second == 0) break;
            end++;
        }
        // Copy src
        uint64_t si = 0;
        while (true) {
            auto it = memory_.find(src + si);
            uint8_t ch = (it != memory_.end()) ? it->second : 0;
            memory_[end + si] = ch;
            si++;
            if (ch == 0) break;
        }
        write_gpr(ARM64_REG_X0, dst);
        pc_ = lr_save;
        return true;
    }
    if (name == "strncat" || name == "__strncat_chk") {
        // __strncat_chk(dst, src, n, flag) — flag ignored
        // strncat(dst, src, n): X0=dst, X1=src, X2=n
        uint64_t dst = read_gpr(ARM64_REG_X0);
        uint64_t src = read_gpr(ARM64_REG_X1);
        uint64_t n = read_gpr(ARM64_REG_X2);
        uint64_t end = dst;
        while (true) {
            auto it = memory_.find(end);
            if (it == memory_.end() || it->second == 0) break;
            end++;
        }
        uint64_t si = 0;
        while (si < n) {
            auto it = memory_.find(src + si);
            uint8_t ch = (it != memory_.end()) ? it->second : 0;
            memory_[end + si] = ch;
            si++;
            if (ch == 0) break;
        }
        if (si == n) memory_[end + si] = 0;
        write_gpr(ARM64_REG_X0, dst);
        pc_ = lr_save;
        return true;
    }
    if (name == "strcmp") {
        stub_strcmp(false);
        pc_ = lr_save;
        return true;
    }
    if (name == "strncmp") {
        stub_strcmp(true);
        pc_ = lr_save;
        return true;
    }
    if (name == "memcpy" || name == "memmove" || name == "__memcpy_chk" || name == "__memmove_chk") {
        stub_memcpy();
        pc_ = lr_save;
        return true;
    }
    if (name == "memset" || name == "__memset_chk") {
        stub_memset();
        pc_ = lr_save;
        return true;
    }
    if (name == "write") {
        stub_write();
        pc_ = lr_save;
        return true;
    }
    if (name == "exit" || name == "_exit" || name == "_Exit") {
        stop_simulation_ = true;
        write_gpr(ARM64_REG_X0, 0);
        pc_ = lr_save;
        return true;
    }
    if (name == "abort") {
        std::fprintf(stderr, "FunctionalSim: abort() called\n");
        stop_simulation_ = true;
        pc_ = lr_save;
        return true;
    }
    if (name == "atoi" || name == "atol") {
        auto s = read_cstring(memory_, read_gpr(ARM64_REG_X0));
        write_gpr(ARM64_REG_X0, static_cast<uint64_t>(std::atol(s.c_str())));
        pc_ = lr_save;
        return true;
    }
    if (name == "strtoul" || name == "strtol") {
        write_gpr(ARM64_REG_X0, 0);
        pc_ = lr_save;
        return true;
    }
    if (name == "mmap") {
        // Return a fake mapped page
        static uint64_t mmap_ptr = 0xB0000000ULL;
        uint64_t len = read_gpr(ARM64_REG_X1);
        write_gpr(ARM64_REG_X0, mmap_ptr);
        mmap_ptr += (len + 4095) & ~4095ULL;
        pc_ = lr_save;
        return true;
    }
    if (name == "munmap") {
        write_gpr(ARM64_REG_X0, 0);  // success
        pc_ = lr_save;
        return true;
    }
    if (name == "mprotect") {
        write_gpr(ARM64_REG_X0, 0);  // success
        pc_ = lr_save;
        return true;
    }
    if (name == "brk") {
        uint64_t new_brk = read_gpr(ARM64_REG_X0);
        static uint64_t brk_val = 0xC0000000ULL;
        if (new_brk != 0) {
            brk_val = new_brk;
        }
        write_gpr(ARM64_REG_X0, brk_val);
        pc_ = lr_save;
        return true;
    }
    if (name == "sbrk") {
        static uint64_t sbrk_val = 0xC0000000ULL;
        int64_t incr = static_cast<int64_t>(read_gpr(ARM64_REG_X0));
        uint64_t old = sbrk_val;
        sbrk_val += static_cast<uint64_t>(incr);
        write_gpr(ARM64_REG_X0, old);
        pc_ = lr_save;
        return true;
    }
    if (name == "setjmp" || name == "_setjmp") {
        // Just return 0 (direct return, no longjmp support)
        write_gpr(ARM64_REG_X0, 0);
        pc_ = lr_save;
        return true;
    }
    if (name == "longjmp" || name == "_longjmp") {
        // Cannot properly support — stop
        std::fprintf(stderr, "FunctionalSim: longjmp() not supported\n");
        stop_simulation_ = true;
        pc_ = lr_save;
        return true;
    }
    if (name == "signal" || name == "sigaction") {
        write_gpr(ARM64_REG_X0, 0);  // success
        pc_ = lr_save;
        return true;
    }
    if (name == "__stack_chk_fail") {
        std::fprintf(stderr, "FunctionalSim: stack canary check failed\n");
        stop_simulation_ = true;
        pc_ = lr_save;
        return true;
    }
    if (name == "__cxa_atexit" || name == "__cxa_finalize") {
        write_gpr(ARM64_REG_X0, 0);
        pc_ = lr_save;
        return true;
    }
    if (name == "__errno_location" || name == "__libc_errno") {
        static uint64_t errno_addr = 0xC0001000ULL;
        write_gpr(ARM64_REG_X0, errno_addr);
        pc_ = lr_save;
        return true;
    }
    if (name == "dlopen" || name == "dlsym" || name == "dlclose") {
        write_gpr(ARM64_REG_X0, 0);
        pc_ = lr_save;
        return true;
    }
    if (name == "getenv") {
        write_gpr(ARM64_REG_X0, 0);  // not found
        pc_ = lr_save;
        return true;
    }
    if (name == "clock_gettime" || name == "gettimeofday") {
        // Zero-fill the struct
        uint64_t buf = read_gpr(ARM64_REG_X1);
        if (name == "clock_gettime") {
            buf = read_gpr(ARM64_REG_X1);
        }
        for (int i = 0; i < 16; i++) {
            memory_[buf + i] = 0;
        }
        write_gpr(ARM64_REG_X0, 0);
        pc_ = lr_save;
        return true;
    }

    // Unknown function — warn and return 0
    std::fprintf(stderr, "FunctionalSim: unstubbed external call: %s\n", name.c_str());
    write_gpr(ARM64_REG_X0, 0);
    pc_ = lr_save;
    return true;
}

void FunctionalSim::stub_printf(const std::string& variant) {
    // For fprintf: X0=FILE*, X1=format
    // For sprintf/snprintf: X0=buf, X1/X2=format
    // For printf: X0=format
    // For __printf_chk: X0=flag, X1=format
    // For __fprintf_chk: X0=flag, X1=FILE*, X2=format
    // For __sprintf_chk: X0=flag, X1=buf, X2=format
    // For __snprintf_chk: X0=flag, X1=buf, X2=size, X3=format
    uint64_t fmt_addr = read_gpr(ARM64_REG_X0);
    if (variant == "fprintf" || variant == "sprintf" || variant == "snprintf") {
        fmt_addr = read_gpr(ARM64_REG_X1);
    } else if (variant == "__printf_chk") {
        fmt_addr = read_gpr(ARM64_REG_X1);
    } else if (variant == "__fprintf_chk") {
        fmt_addr = read_gpr(ARM64_REG_X2);
    } else if (variant == "__sprintf_chk") {
        fmt_addr = read_gpr(ARM64_REG_X2);
    } else if (variant == "__snprintf_chk") {
        fmt_addr = read_gpr(ARM64_REG_X3);
    }

    auto fmt = read_cstring(memory_, fmt_addr);

    // Determine which register format args start from
    // printf: X0=fmt, args in X1..X7
    // __printf_chk: X0=flag, X1=fmt, args in X2..X7
    unsigned arg_start_reg = ARM64_REG_X1;
    if (variant == "__printf_chk") {
        arg_start_reg = ARM64_REG_X2;
    } else if (variant == "__fprintf_chk") {
        arg_start_reg = ARM64_REG_X3;
    } else if (variant == "__sprintf_chk") {
        arg_start_reg = ARM64_REG_X3;
    } else if (variant == "__snprintf_chk") {
        arg_start_reg = ARM64_REG_X4;
    }

    // Format args are passed in registers, rest on stack
    uint64_t stack_args[8];
    for (int i = 0; i < 8; i++) {
        stack_args[i] = read_gpr(arg_start_reg + i);
    }

    int arg_idx = 0;
    int total_chars = 0;

    for (std::size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] != '%') {
            std::fprintf(stderr, "%c", fmt[i]);
            total_chars++;
            continue;
        }

        // Parse format specifier
        i++;
        if (i >= fmt.size()) break;

        // Handle %%
        if (fmt[i] == '%') {
            std::fprintf(stderr, "%%");
            total_chars++;
            continue;
        }

        // Skip flags, width, precision
        while (i < fmt.size() && (fmt[i] == '-' || fmt[i] == '+' || fmt[i] == ' ' ||
               fmt[i] == '#' || fmt[i] == '0' || fmt[i] == '*' ||
               (fmt[i] >= '1' && fmt[i] <= '9') || fmt[i] == '.')) {
            i++;
        }
        if (i >= fmt.size()) break;

        // Length modifier
        bool is_long = false;
        bool is_longlong = false;
        if (fmt[i] == 'l') {
            is_long = true;
            i++;
            if (i < fmt.size() && fmt[i] == 'l') {
                is_longlong = true;
                i++;
            }
        } else if (fmt[i] == 'h') {
            i++;
            if (i < fmt.size() && fmt[i] == 'h') i++;
        } else if (fmt[i] == 'z') {
            i++;
            is_long = true;
        }
        if (i >= fmt.size()) break;

        char spec = fmt[i];

        if (spec == 'd' || spec == 'i') {
            int64_t val;
            if (arg_idx < 8) {
                if (!is_long && !is_longlong) {
                    // %d without l/ll: argument is int (32-bit signed)
                    val = static_cast<int32_t>(static_cast<uint32_t>(stack_args[arg_idx]));
                } else {
                    val = static_cast<int64_t>(stack_args[arg_idx]);
                }
            } else {
                val = 0;
            }
            std::fprintf(stderr, "%lld", (long long)val);
            total_chars += (int)std::snprintf(nullptr, 0, "%lld", (long long)val);
            arg_idx++;
        } else if (spec == 'u') {
            uint64_t val;
            if (arg_idx < 8) {
                val = (!is_long && !is_longlong)
                    ? static_cast<uint32_t>(stack_args[arg_idx])
                    : stack_args[arg_idx];
            } else {
                val = 0;
            }
            std::fprintf(stderr, "%llu", (unsigned long long)val);
            total_chars += (int)std::snprintf(nullptr, 0, "%llu", (unsigned long long)val);
            arg_idx++;
        } else if (spec == 'x') {
            uint64_t val = (arg_idx < 8) ? stack_args[arg_idx] : 0;
            std::fprintf(stderr, "%llx", (unsigned long long)val);
            total_chars += (int)std::snprintf(nullptr, 0, "%llx", (unsigned long long)val);
            arg_idx++;
        } else if (spec == 'X') {
            uint64_t val = (arg_idx < 8) ? stack_args[arg_idx] : 0;
            std::fprintf(stderr, "%llX", (unsigned long long)val);
            total_chars += (int)std::snprintf(nullptr, 0, "%llX", (unsigned long long)val);
            arg_idx++;
        } else if (spec == 'p') {
            uint64_t val = (arg_idx < 8) ? stack_args[arg_idx] : 0;
            std::fprintf(stderr, "0x%llx", (unsigned long long)val);
            total_chars += (int)std::snprintf(nullptr, 0, "0x%llx", (unsigned long long)val);
            arg_idx++;
        } else if (spec == 'c') {
            int ch = (arg_idx < 8) ? (int)stack_args[arg_idx] : 0;
            std::fprintf(stderr, "%c", ch);
            total_chars++;
            arg_idx++;
        } else if (spec == 's') {
            uint64_t str_addr = (arg_idx < 8) ? stack_args[arg_idx] : 0;
            auto s = read_cstring(memory_, str_addr);
            std::fprintf(stderr, "%s", s.c_str());
            total_chars += (int)s.size();
            arg_idx++;
        } else if (spec == 'f' || spec == 'e' || spec == 'g') {
            // Floating point — read from X registers (we don't model FP regs well)
            double val = 0.0;
            std::fprintf(stderr, "%f", val);
            total_chars += (int)std::snprintf(nullptr, 0, "%f", val);
            arg_idx++;
        } else if (spec == 'n') {
            // %n — write chars written so far to *int
            if (arg_idx < 8) {
                uint64_t ptr = stack_args[arg_idx];
                mem_write32(ptr, static_cast<uint32_t>(total_chars));
            }
            arg_idx++;
        } else {
            std::fprintf(stderr, "%%%c", spec);
            total_chars += 2;
        }
    }

    // For sprintf/snprintf, also write the result to the buffer
    if (variant == "sprintf" || variant == "snprintf") {
        uint64_t buf = read_gpr(ARM64_REG_X0);
        auto fmt_str = read_cstring(memory_, fmt_addr);
        // Write the formatted string to memory
        for (std::size_t i = 0; i < fmt_str.size(); ++i) {
            memory_[buf + i] = static_cast<uint8_t>(fmt_str[i]);
        }
        memory_[buf + fmt_str.size()] = 0;
    }

    write_gpr(ARM64_REG_X0, static_cast<uint64_t>(total_chars));
}

void FunctionalSim::stub_puts() {
    uint64_t str_addr = read_gpr(ARM64_REG_X0);
    auto s = read_cstring(memory_, str_addr);
    std::fprintf(stderr, "%s\n", s.c_str());
    write_gpr(ARM64_REG_X0, static_cast<uint64_t>(s.size() + 1));  // puts returns non-negative
}

void FunctionalSim::stub_malloc() {
    uint64_t size = read_gpr(ARM64_REG_X0);
    uint64_t aligned = (size + 15) & ~15ULL;  // 16-byte alignment
    if (size == 0) aligned = 16;
    write_gpr(ARM64_REG_X0, heap_ptr_);
    heap_ptr_ += aligned;
}

void FunctionalSim::stub_calloc() {
    uint64_t count = read_gpr(ARM64_REG_X0);
    uint64_t size = read_gpr(ARM64_REG_X1);
    uint64_t total = count * size;
    uint64_t aligned = (total + 15) & ~15ULL;
    if (total == 0) aligned = 16;
    write_gpr(ARM64_REG_X0, heap_ptr_);
    // Zero-fill
    for (uint64_t i = 0; i < aligned; ++i) {
        memory_[heap_ptr_ + i] = 0;
    }
    heap_ptr_ += aligned;
}

void FunctionalSim::stub_strlen() {
    uint64_t str_addr = read_gpr(ARM64_REG_X0);
    auto s = read_cstring(memory_, str_addr);
    write_gpr(ARM64_REG_X0, s.size());
}

void FunctionalSim::stub_strcpy(bool is_strncpy) {
    uint64_t dst = read_gpr(ARM64_REG_X0);
    uint64_t src = read_gpr(ARM64_REG_X1);
    uint64_t n = is_strncpy ? read_gpr(ARM64_REG_X2) : UINT64_MAX;

    uint64_t i = 0;
    while (i < n) {
        auto it = memory_.find(src + i);
        uint8_t ch = (it != memory_.end()) ? it->second : 0;
        memory_[dst + i] = ch;
        i++;
        if (ch == 0) break;
    }
    // strncpy zero-pads if no null found
    if (is_strncpy && i < n) {
        for (; i < n; ++i) {
            memory_[dst + i] = 0;
        }
    }
    write_gpr(ARM64_REG_X0, dst);
}

void FunctionalSim::stub_strcmp(bool is_strncmp) {
    uint64_t s1 = read_gpr(ARM64_REG_X0);
    uint64_t s2 = read_gpr(ARM64_REG_X1);
    uint64_t n = is_strncmp ? read_gpr(ARM64_REG_X2) : UINT64_MAX;

    for (uint64_t i = 0; i < n; ++i) {
        uint8_t c1 = 0, c2 = 0;
        auto it1 = memory_.find(s1 + i);
        if (it1 != memory_.end()) c1 = it1->second;
        auto it2 = memory_.find(s2 + i);
        if (it2 != memory_.end()) c2 = it2->second;
        if (c1 != c2) {
            write_gpr(ARM64_REG_X0, static_cast<uint64_t>(static_cast<int64_t>(c1) - static_cast<int64_t>(c2)));
            return;
        }
        if (c1 == 0) break;
    }
    write_gpr(ARM64_REG_X0, 0);
}

void FunctionalSim::stub_memcpy() {
    uint64_t dst = read_gpr(ARM64_REG_X0);
    uint64_t src = read_gpr(ARM64_REG_X1);
    uint64_t n = read_gpr(ARM64_REG_X2);

    for (uint64_t i = 0; i < n; ++i) {
        auto it = memory_.find(src + i);
        uint8_t ch = (it != memory_.end()) ? it->second : 0;
        memory_[dst + i] = ch;
    }
    write_gpr(ARM64_REG_X0, dst);
}

void FunctionalSim::stub_memset() {
    uint64_t dst = read_gpr(ARM64_REG_X0);
    uint64_t val = read_gpr(ARM64_REG_X1) & 0xFF;
    uint64_t n = read_gpr(ARM64_REG_X2);

    for (uint64_t i = 0; i < n; ++i) {
        memory_[dst + i] = static_cast<uint8_t>(val);
    }
    write_gpr(ARM64_REG_X0, dst);
}

void FunctionalSim::stub_write() {
    uint64_t fd = read_gpr(ARM64_REG_X0);
    uint64_t buf = read_gpr(ARM64_REG_X1);
    uint64_t count = read_gpr(ARM64_REG_X2);

    // Only output for fd=1 (stdout) or fd=2 (stderr)
    if (fd == 1 || fd == 2) {
        for (uint64_t i = 0; i < count; ++i) {
            auto it = memory_.find(buf + i);
            uint8_t ch = (it != memory_.end()) ? it->second : 0;
            std::fprintf(stderr, "%c", ch);
        }
    }
    write_gpr(ARM64_REG_X0, count);
}

} // namespace arm_cpu
