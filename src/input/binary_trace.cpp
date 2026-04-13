/// @file binary_trace.cpp
/// @brief Implementation of the binary trace parser.

#include "arm_cpu/input/binary_trace.hpp"

#include <algorithm>
#include <cstring>

namespace arm_cpu {

// =====================================================================
// BinaryTraceParser::from_file
// =====================================================================

Result<BinaryTraceParser> BinaryTraceParser::from_file(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        return Err(EmulatorError::trace_parse(
            "Failed to open file: " + path));
    }

    auto header = read_header(stream);
    if (header.has_error()) return Err(std::move(header.error()));

    if (header.value().magic != MAGIC) {
        char expected[20], got[20];
        std::snprintf(expected, sizeof(expected), "0x%llx",
                      static_cast<unsigned long long>(MAGIC));
        std::snprintf(got, sizeof(got), "0x%llx",
                      static_cast<unsigned long long>(header.value().magic));
        return Err(EmulatorError::trace_parse(
            std::string("Invalid magic number: expected ") + expected +
            ", got " + got));
    }

    if (header.value().version > VERSION) {
        return Err(EmulatorError::trace_parse(
            "Unsupported version: " + std::to_string(header.value().version)));
    }

    return Ok(BinaryTraceParser(std::move(stream), header.value(), path));
}

// =====================================================================
// BinaryTraceParser::read_header
// =====================================================================

Result<TraceHeader> BinaryTraceParser::read_header(std::ifstream& reader) {
    uint8_t buf[HEADER_SIZE];

    if (!reader.read(reinterpret_cast<char*>(buf), HEADER_SIZE)) {
        return Err(EmulatorError::trace_parse("Failed to read header"));
    }

    TraceHeader header;
    std::memcpy(&header.magic, buf, 8);
    std::memcpy(&header.version, buf + 8, 8);
    std::memcpy(&header.instruction_count, buf + 16, 8);

    // Convert from little-endian if needed (most platforms are LE)
    // For portability, use explicit byte-level conversion:
    header.magic = 0;
    for (int i = 7; i >= 0; --i) header.magic = (header.magic << 8) | buf[i];
    header.version = 0;
    for (int i = 15; i >= 8; --i) header.version = (header.version << 8) | buf[i];
    header.instruction_count = 0;
    for (int i = 23; i >= 16; --i) header.instruction_count = (header.instruction_count << 8) | buf[i];

    return Ok(header);
}

// =====================================================================
// BinaryTraceParser::reset
// =====================================================================

Result<void> BinaryTraceParser::reset() {
    reader_.clear();
    reader_.seekg(HEADER_SIZE, std::ios::beg);
    if (!reader_.good()) {
        return Err(EmulatorError::trace_parse(
            "Failed to reset file: " + file_path_));
    }
    current_id_ = 0;
    instructions_read_ = 0;
    return Ok();
}

// =====================================================================
// BinaryTraceParser::next_impl
// =====================================================================

Result<std::optional<Instruction>> BinaryTraceParser::next_impl() {
    return read_instruction();
}

// =====================================================================
// BinaryTraceParser::read_instruction
// =====================================================================

Result<std::optional<Instruction>> BinaryTraceParser::read_instruction() {
    if (header_.instruction_count > 0 &&
        instructions_read_ >= header_.instruction_count) {
        return Ok(std::optional<Instruction>{std::nullopt});
    }

    // Read fixed fields: PC(8) + opcode(4) + opcode_type(4) + src_count(1) + dst_count(1) = 18 bytes
    uint8_t buf[18];
    if (!reader_.read(reinterpret_cast<char*>(buf), sizeof(buf))) {
        if (reader_.eof()) {
            return Ok(std::optional<Instruction>{std::nullopt});
        }
        return Err(EmulatorError::trace_parse("Read error in binary trace"));
    }

    // Parse little-endian fields
    auto read_u64_le = [](const uint8_t* p) -> uint64_t {
        uint64_t v = 0;
        for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
        return v;
    };
    auto read_u32_le = [](const uint8_t* p) -> uint32_t {
        uint32_t v = 0;
        for (int i = 3; i >= 0; --i) v = (v << 8) | p[i];
        return v;
    };

    uint64_t pc = read_u64_le(buf);
    uint32_t raw_opcode = read_u32_le(buf + 8);
    uint32_t opcode_type_u32 = read_u32_le(buf + 12);
    uint8_t src_count = buf[16];
    uint8_t dst_count = buf[17];

    auto opcode_type = u32_to_opcode_type(opcode_type_u32);

    // Read source registers
    std::vector<Reg> src_regs;
    if (src_count > 0) {
        std::vector<uint8_t> src_buf(src_count);
        if (!read_bytes(src_buf.data(), src_count)) {
            return Err(EmulatorError::trace_parse("Failed to read src regs"));
        }
        src_regs.reserve(src_count);
        for (auto r : src_buf) {
            src_regs.emplace_back(r);
        }
    }

    // Read destination registers
    std::vector<Reg> dst_regs;
    if (dst_count > 0) {
        std::vector<uint8_t> dst_buf(dst_count);
        if (!read_bytes(dst_buf.data(), dst_count)) {
            return Err(EmulatorError::trace_parse("Failed to read dst regs"));
        }
        dst_regs.reserve(dst_count);
        for (auto r : dst_buf) {
            dst_regs.emplace_back(r);
        }
    }

    // Read flags
    uint8_t flags = 0;
    if (!read_bytes(&flags, 1)) {
        return Err(EmulatorError::trace_parse("Failed to read flags"));
    }

    bool has_mem = (flags & 0x01) != 0;
    bool has_branch = (flags & 0x02) != 0;

    // Read memory access info
    std::optional<MemAccess> mem_access;
    if (has_mem) {
        uint8_t mem_buf[10]; // addr(8) + size(1) + is_load(1)
        if (!read_bytes(mem_buf, sizeof(mem_buf))) {
            return Err(EmulatorError::trace_parse("Failed to read mem info"));
        }
        uint64_t addr = read_u64_le(mem_buf);
        uint8_t size = mem_buf[8];
        bool is_load = mem_buf[9] != 0;
        mem_access = MemAccess{addr, size, is_load};
    }

    // Read branch info
    std::optional<BranchInfo> branch_info;
    if (has_branch) {
        uint8_t branch_buf[10]; // target(8) + is_cond(1) + is_taken(1)
        if (!read_bytes(branch_buf, sizeof(branch_buf))) {
            return Err(EmulatorError::trace_parse("Failed to read branch info"));
        }
        uint64_t target = read_u64_le(branch_buf);
        bool is_conditional = branch_buf[8] != 0;
        bool is_taken = branch_buf[9] != 0;
        branch_info = BranchInfo{is_conditional, target, is_taken};
    }

    // Build instruction
    Instruction instr(InstructionId(current_id_), pc, raw_opcode, opcode_type);
    ++current_id_;
    ++instructions_read_;

    for (const auto& r : src_regs) {
        instr.src_regs.push_back(r);
    }
    for (const auto& r : dst_regs) {
        instr.dst_regs.push_back(r);
    }
    instr.mem_access = mem_access;
    instr.branch_info = branch_info;

    return Ok(std::optional<Instruction>{std::move(instr)});
}

// =====================================================================
// BinaryTraceParser::read_bytes
// =====================================================================

Result<void> BinaryTraceParser::read_bytes(void* buf, std::size_t n) {
    if (!reader_.read(reinterpret_cast<char*>(buf), n)) {
        if (reader_.eof()) {
            return Err(EmulatorError::trace_parse("Unexpected end of file"));
        }
        return Err(EmulatorError::trace_parse("Read error in binary trace"));
    }
    return Ok();
}

// =====================================================================
// BinaryTraceParser::u32_to_opcode_type
// =====================================================================

OpcodeType BinaryTraceParser::u32_to_opcode_type(uint32_t v) {
    switch (v) {
        // Computational
        case 0:  return OpcodeType::Add;
        case 1:  return OpcodeType::Sub;
        case 2:  return OpcodeType::Mul;
        case 3:  return OpcodeType::Div;
        case 4:  return OpcodeType::And;
        case 5:  return OpcodeType::Orr;
        case 6:  return OpcodeType::Eor;
        case 7:  return OpcodeType::Lsl;
        case 8:  return OpcodeType::Lsr;
        case 9:  return OpcodeType::Asr;
        // Load/Store
        case 10: return OpcodeType::Load;
        case 11: return OpcodeType::Store;
        case 12: return OpcodeType::LoadPair;
        case 13: return OpcodeType::StorePair;
        // Branch
        case 14: return OpcodeType::Branch;
        case 15: return OpcodeType::BranchCond;
        case 16: return OpcodeType::BranchReg;
        // System
        case 17: return OpcodeType::Msr;
        case 18: return OpcodeType::Mrs;
        case 19: return OpcodeType::Sys;
        case 20: return OpcodeType::Nop;
        // Floating-point
        case 21: return OpcodeType::Fadd;
        case 22: return OpcodeType::Fsub;
        case 23: return OpcodeType::Fmul;
        case 24: return OpcodeType::Fdiv;
        // Cache Maintenance (25-31)
        case 25: return OpcodeType::DcZva;
        case 26: return OpcodeType::DcCivac;
        case 27: return OpcodeType::DcCvac;
        case 28: return OpcodeType::DcCsw;
        case 29: return OpcodeType::IcIvau;
        case 30: return OpcodeType::IcIallu;
        case 31: return OpcodeType::IcIalluis;
        // Cryptography (32-38)
        case 32: return OpcodeType::Aesd;
        case 33: return OpcodeType::Aese;
        case 34: return OpcodeType::Aesimc;
        case 35: return OpcodeType::Aesmc;
        case 36: return OpcodeType::Sha1H;
        case 37: return OpcodeType::Sha256H;
        case 38: return OpcodeType::Sha512H;
        // SIMD/Vector (39-47)
        case 39: return OpcodeType::Vadd;
        case 40: return OpcodeType::Vsub;
        case 41: return OpcodeType::Vmul;
        case 42: return OpcodeType::Vmla;
        case 43: return OpcodeType::Vmls;
        case 44: return OpcodeType::Vld;
        case 45: return OpcodeType::Vst;
        case 46: return OpcodeType::Vdup;
        case 47: return OpcodeType::Vmov;
        // FMA (48-51)
        case 48: return OpcodeType::Fmadd;
        case 49: return OpcodeType::Fmsub;
        case 50: return OpcodeType::Fnmadd;
        case 51: return OpcodeType::Fnmsub;
        // Other
        case 255: return OpcodeType::Other;
        default:  return OpcodeType::Other;
    }
}

} // namespace arm_cpu
