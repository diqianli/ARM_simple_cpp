#pragma once

/// @file binary_format.hpp
/// @brief Binary trace format definitions for high-performance storage and streaming.
///
/// File Structure:
///   File Header (64 bytes)
///   Instruction Stream ([InstrHeader | Operands | Deps]*)
///   Index Table (optional)
///   String Table

#include "arm_cpu/types.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace arm_cpu {

// =====================================================================
// Constants
// =====================================================================
inline constexpr std::array<uint8_t, 8> MAGIC = {'A', 'R', 'M', 'T', 'R', 'A', 'C', 'E'};
inline constexpr uint16_t VERSION = 1;

// =====================================================================
// FileFlags
// =====================================================================
struct FileFlags {
    uint16_t value = 0;

    static constexpr uint16_t NONE = 0;
    static constexpr uint16_t HAS_INDEX = 1 << 0;
    static constexpr uint16_t EXTENDED_TIMING = 1 << 1;
    static constexpr uint16_t COMPRESSED = 1 << 2;

    bool has_index() const { return value & HAS_INDEX; }
    bool has_extended_timing() const { return value & EXTENDED_TIMING; }
    bool is_compressed() const { return value & COMPRESSED; }
};

// =====================================================================
// FileHeader (64 bytes)
// =====================================================================
#pragma pack(push, 1)
struct FileHeader {
    std::array<uint8_t, 8> magic;
    uint16_t version;
    uint16_t flags;
    uint64_t instr_count;
    uint64_t string_table_offset;
    uint32_t string_table_size;
    uint64_t index_table_offset;
    uint32_t index_table_size;
    std::array<uint8_t, 24> reserved;

    static constexpr std::size_t SIZE = 64;

    static FileHeader default_header() {
        FileHeader h;
        h.magic = MAGIC;
        h.version = VERSION;
        h.flags = FileFlags::NONE;
        h.instr_count = 0;
        h.string_table_offset = 0;
        h.string_table_size = 0;
        h.index_table_offset = 0;
        h.index_table_size = 0;
        h.reserved = {};
        return h;
    }

    static FileHeader create(uint64_t count) {
        auto h = default_header();
        h.instr_count = count;
        return h;
    }

    bool validate() const;
    bool has_index() const { return flags & FileFlags::HAS_INDEX; }
};

// =====================================================================
// InstrHeader (9 bytes)
// =====================================================================
struct InstrHeader {
    uint32_t id;
    uint8_t opcode;
    uint8_t flags;
    uint16_t pc_delta;
    uint8_t operand_count;

    static constexpr std::size_t SIZE = 9;
};
#pragma pack(pop)

// =====================================================================
// InstrFlags
// =====================================================================
struct InstrFlags {
    uint8_t value = 0;

    static constexpr uint8_t NONE = 0;
    static constexpr uint8_t HAS_MEM = 1 << 0;
    static constexpr uint8_t HAS_BRANCH = 1 << 1;
    static constexpr uint8_t EXTENDED_PC = 1 << 2;
    static constexpr uint8_t HAS_DISASM = 1 << 3;
    static constexpr uint8_t HAS_DEPS = 1 << 4;

    bool has_mem() const { return value & HAS_MEM; }
    bool has_branch() const { return value & HAS_BRANCH; }
    bool has_extended_pc() const { return value & EXTENDED_PC; }
    bool has_disasm() const { return value & HAS_DISASM; }
    bool has_deps() const { return value & HAS_DEPS; }
};

// =====================================================================
// OperandType — operand type for binary encoding
// =====================================================================
enum class OperandType : uint8_t {
    Reg,
    VReg,
};

// =====================================================================

// =====================================================================
// IndexEntry (16 bytes)
// =====================================================================
#pragma pack(push, 1)
struct IndexEntry {
    uint64_t instr_id;
    uint64_t offset;

    static constexpr std::size_t SIZE = 16;
};
#pragma pack(pop)

// =====================================================================
// Opcode encoding/decoding
// =====================================================================
uint8_t encode_opcode(OpcodeType opcode);
OpcodeType decode_opcode(uint8_t code);

} // namespace arm_cpu
