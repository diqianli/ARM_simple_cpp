/// @file champsim_trace.cpp
/// @brief Implementation of the ChampSim trace parser.

#include "arm_cpu/input/champsim_trace.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace arm_cpu {

// =====================================================================
// ChampSimInstr::from_bytes
// =====================================================================

ChampSimInstr ChampSimInstr::from_bytes(const std::array<uint8_t, 64>& buf) {
    auto read_u64_le = [](const uint8_t* p) -> uint64_t {
        uint64_t v = 0;
        for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
        return v;
    };

    ChampSimInstr instr;
    instr.ip = read_u64_le(buf.data());
    instr.is_branch = buf[8] != 0;
    instr.branch_taken = buf[9] != 0;
    instr.destination_registers = {buf[10], buf[11]};
    instr.source_registers = {buf[12], buf[13], buf[14], buf[15]};
    instr.destination_memory = {
        read_u64_le(buf.data() + 16),
        read_u64_le(buf.data() + 24)
    };
    instr.source_memory = {
        read_u64_le(buf.data() + 32),
        read_u64_le(buf.data() + 40),
        read_u64_le(buf.data() + 48),
        read_u64_le(buf.data() + 56)
    };
    return instr;
}

// =====================================================================
// ChampSimInstr::has_memory_access
// =====================================================================

bool ChampSimInstr::has_memory_access() const {
    for (auto x : source_memory) if (x != 0) return true;
    for (auto x : destination_memory) if (x != 0) return true;
    return false;
}

// =====================================================================
// ChampSimTraceParser::from_file
// =====================================================================

Result<ChampSimTraceParser> ChampSimTraceParser::from_file(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        return Err(EmulatorError::trace_parse(
            "Failed to open file: " + path));
    }
    return Ok(ChampSimTraceParser(std::move(stream), path));
}

// =====================================================================
// ChampSimTraceParser::reset
// =====================================================================

Result<void> ChampSimTraceParser::reset() {
    reader_.clear();
    reader_.seekg(0, std::ios::beg);
    if (!reader_.good()) {
        return Err(EmulatorError::trace_parse(
            "Failed to reset file: " + file_path_));
    }
    current_id_ = 0;
    instructions_read_ = 0;
    return Ok();
}

// =====================================================================
// ChampSimTraceParser::next_impl
// =====================================================================

Result<std::optional<Instruction>> ChampSimTraceParser::next_impl() {
    auto cs_result = read_instruction();
    if (cs_result.has_error()) return Err(std::move(cs_result.error()));
    auto& cs_opt = cs_result.value();
    if (!cs_opt.has_value()) {
        return Ok(std::optional<Instruction>{std::nullopt});
    }
    return Ok(std::optional<Instruction>{convert_instruction(cs_opt.value())});
}

// =====================================================================
// ChampSimTraceParser::read_instruction
// =====================================================================

Result<std::optional<ChampSimInstr>> ChampSimTraceParser::read_instruction() {
    if (!reader_.read(reinterpret_cast<char*>(buffer_.data()), INSTR_SIZE)) {
        if (reader_.eof()) {
            return Ok(std::optional<ChampSimInstr>{std::nullopt});
        }
        return Err(EmulatorError::trace_parse("Read error in ChampSim trace"));
    }

    auto instr = ChampSimInstr::from_bytes(buffer_);
    ++instructions_read_;
    return Ok(std::optional<ChampSimInstr>{instr});
}

// =====================================================================
// ChampSimTraceParser::convert_instruction
// =====================================================================

Instruction ChampSimTraceParser::convert_instruction(const ChampSimInstr& cs_instr) {
    // Determine opcode type based on instruction characteristics
    OpcodeType opcode_type;
    if (cs_instr.is_branch) {
        opcode_type = OpcodeType::Branch;
    } else if (cs_instr.has_memory_access()) {
        if (cs_instr.destination_memory[0] != 0 || cs_instr.destination_memory[1] != 0) {
            opcode_type = OpcodeType::Store;
        } else {
            opcode_type = OpcodeType::Load;
        }
    } else {
        // Infer instruction type from register usage patterns
        std::size_t src_count = 0;
        for (auto r : cs_instr.source_registers) if (r != 0) ++src_count;
        std::size_t dst_count = 0;
        for (auto r : cs_instr.destination_registers) if (r != 0) ++dst_count;

        if (src_count >= 1 && dst_count == 0) {
            opcode_type = OpcodeType::Cmp;
        } else if (src_count == 1 && dst_count == 1) {
            opcode_type = OpcodeType::Mov;
        } else if (src_count == 2 && dst_count == 1) {
            opcode_type = OpcodeType::Add;
        } else if (src_count == 3 && dst_count == 1) {
            opcode_type = OpcodeType::Mul;
        } else if (src_count == 4 && dst_count == 1) {
            opcode_type = OpcodeType::Mul;
        } else if (src_count == 0 && dst_count == 1) {
            opcode_type = OpcodeType::Mov;
        } else if (src_count == 0 && dst_count == 0) {
            opcode_type = OpcodeType::Nop;
        } else if (dst_count == 1) {
            opcode_type = OpcodeType::Add;
        } else if (dst_count == 2) {
            opcode_type = OpcodeType::LoadPair;
        } else {
            opcode_type = OpcodeType::Nop;
        }
    }

    Instruction instr(InstructionId(current_id_), cs_instr.ip, 0, opcode_type);
    ++current_id_;

    // Add source registers (filter out 0s which mean "no register")
    for (auto reg : cs_instr.source_registers) {
        if (reg != 0) {
            instr.src_regs.push_back(Reg(reg));
        }
    }

    // Add destination registers
    for (auto reg : cs_instr.destination_registers) {
        if (reg != 0) {
            instr.dst_regs.push_back(Reg(reg));
        }
    }

    // Add memory accesses
    // Source memory = loads
    for (auto addr : cs_instr.source_memory) {
        if (addr != 0) {
            instr.mem_access = MemAccess{addr, 8, true};
            break;
        }
    }

    // Destination memory = stores
    for (auto addr : cs_instr.destination_memory) {
        if (addr != 0) {
            instr.mem_access = MemAccess{addr, 8, false};
            break;
        }
    }

    // Add branch info
    if (cs_instr.is_branch) {
        instr.branch_info = BranchInfo{true, 0, cs_instr.branch_taken};
    }

    return instr;
}

// =====================================================================
// ChampSimXzTraceParser::from_file
// =====================================================================

Result<ChampSimXzTraceParser> ChampSimXzTraceParser::from_file(const std::string& path) {
    // Use popen with "xz -dc" as a portable fallback for XZ decompression
    std::string cmd = "xz -dc '" + path + "' 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return Err(EmulatorError::trace_parse(
            "Failed to open XZ-compressed file: " + path));
    }

    auto parser = ChampSimXzTraceParser(path);
    parser.pipe_ = pipe;
    return Ok(std::move(parser));
}

// =====================================================================
// ChampSimXzTraceParser::reset
// =====================================================================

Result<void> ChampSimXzTraceParser::reset() {
    // Close existing pipe and re-open
    if (pipe_) {
        pclose(static_cast<FILE*>(pipe_));
        pipe_ = nullptr;
    }

    std::string cmd = "xz -dc '" + file_path_ + "' 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return Err(EmulatorError::trace_parse(
            "Failed to reopen XZ-compressed file: " + file_path_));
    }

    pipe_ = pipe;
    current_id_ = 0;
    instructions_read_ = 0;
    return Ok();
}

// =====================================================================
// ChampSimXzTraceParser::next_impl
// =====================================================================

Result<std::optional<Instruction>> ChampSimXzTraceParser::next_impl() {
    auto cs_result = read_instruction();
    if (cs_result.has_error()) return Err(std::move(cs_result.error()));
    auto& cs_opt = cs_result.value();
    if (!cs_opt.has_value()) {
        return Ok(std::optional<Instruction>{std::nullopt});
    }
    return Ok(std::optional<Instruction>{convert_instruction(cs_opt.value())});
}

// =====================================================================
// ChampSimXzTraceParser::read_instruction
// =====================================================================

Result<std::optional<ChampSimInstr>> ChampSimXzTraceParser::read_instruction() {
    FILE* fp = static_cast<FILE*>(pipe_);
    std::array<uint8_t, INSTR_SIZE> buf{};
    std::size_t total_read = 0;

    while (total_read < INSTR_SIZE) {
        auto n = std::fread(buf.data() + total_read, 1, INSTR_SIZE - total_read, fp);
        if (n == 0) {
            if (total_read == 0) {
                return Ok(std::optional<ChampSimInstr>{std::nullopt});
            } else {
                return Err(EmulatorError::trace_parse(
                    "Unexpected end of compressed file"));
            }
        }
        total_read += n;
    }

    auto instr = ChampSimInstr::from_bytes(buf);
    ++instructions_read_;
    return Ok(std::optional<ChampSimInstr>{instr});
}

// =====================================================================
// ChampSimXzTraceParser::convert_instruction
// =====================================================================

Instruction ChampSimXzTraceParser::convert_instruction(const ChampSimInstr& cs_instr) {
    // Determine opcode type (same simplified logic as the XZ variant in Rust)
    OpcodeType opcode_type;
    if (cs_instr.is_branch) {
        opcode_type = OpcodeType::Branch;
    } else if (cs_instr.has_memory_access()) {
        if (cs_instr.destination_memory[0] != 0 || cs_instr.destination_memory[1] != 0) {
            opcode_type = OpcodeType::Store;
        } else {
            opcode_type = OpcodeType::Load;
        }
    } else {
        opcode_type = OpcodeType::Other;
    }

    Instruction instr(InstructionId(current_id_), cs_instr.ip, 0, opcode_type);
    ++current_id_;

    for (auto reg : cs_instr.source_registers) {
        if (reg != 0) instr.src_regs.push_back(Reg(reg));
    }
    for (auto reg : cs_instr.destination_registers) {
        if (reg != 0) instr.dst_regs.push_back(Reg(reg));
    }

    for (auto addr : cs_instr.source_memory) {
        if (addr != 0) {
            instr.mem_access = MemAccess{addr, 8, true};
            break;
        }
    }
    for (auto addr : cs_instr.destination_memory) {
        if (addr != 0) {
            instr.mem_access = MemAccess{addr, 8, false};
            break;
        }
    }

    if (cs_instr.is_branch) {
        instr.branch_info = BranchInfo{true, 0, cs_instr.branch_taken};
    }

    return instr;
}

} // namespace arm_cpu
