#pragma once

/// @file elf_loader.hpp
/// @brief ELF file loader for ARM64 executables.
///
/// Provides:
/// - ELF file loading and parsing (ELF64 LE only, ARM64 machine type)
/// - ARM64 instruction decoding
/// - Symbol table resolution
/// - Memory segment extraction
///
/// Ported from Rust src/elf/ (loader.rs, decoder.rs, symbols.rs)

#include "arm_cpu/types.hpp"
#include "arm_cpu/error.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace arm_cpu {

// =====================================================================
// ElfHeader
// =====================================================================
struct ElfHeader {
    uint64_t entry_point = 0;
    uint16_t machine = 0;
    uint16_t phnum = 0;
    uint16_t shnum = 0;
    uint64_t phoff = 0;
    uint64_t shoff = 0;
    uint16_t shstrndx = 0;
};

// =====================================================================
// ProgramHeader (segment)
// =====================================================================
struct ProgramHeader {
    uint32_t p_type = 0;
    uint32_t p_flags = 0;
    uint64_t p_offset = 0;
    uint64_t p_vaddr = 0;
    uint64_t p_filesz = 0;
    uint64_t p_memsz = 0;
    uint64_t p_align = 0;
};

// =====================================================================
// SectionHeader
// =====================================================================
struct SectionHeader {
    uint32_t sh_name = 0;
    uint32_t sh_type = 0;
    uint64_t sh_flags = 0;
    uint64_t sh_addr = 0;
    uint64_t sh_offset = 0;
    uint64_t sh_size = 0;
    std::optional<std::string> name;
};

// =====================================================================
// MemorySegment
// =====================================================================
struct MemorySegment {
    uint64_t vaddr = 0;
    std::size_t size = 0;
    std::vector<uint8_t> data;
    bool executable = false;
    bool writable = false;
    bool readable = false;
};

// =====================================================================
// DecodedInstruction
// =====================================================================
struct DecodedInstruction {
    uint64_t pc = 0;
    uint32_t raw = 0;
    OpcodeType opcode = OpcodeType::Other;
    std::vector<Reg> src_regs;
    std::vector<Reg> dst_regs;
    std::vector<VReg> src_vregs;
    std::vector<VReg> dst_vregs;
    std::optional<int64_t> immediate;
    std::optional<uint64_t> mem_addr;
    std::optional<uint8_t> mem_size;
    bool is_load = false;
    std::optional<uint64_t> branch_target;
    bool is_conditional = false;
    std::string disasm;

    DecodedInstruction() = default;
    DecodedInstruction(uint64_t pc_, uint32_t raw_) : pc(pc_), raw(raw_) {}
};

// =====================================================================
// Arm64Decoder
// =====================================================================
class Arm64Decoder {
public:
    /// Decode a single ARM64 instruction
    static DecodedInstruction decode(uint64_t pc, uint32_t raw);

private:
    static DecodedInstruction decode_data_processing(uint64_t pc, uint32_t raw);
    static DecodedInstruction decode_branch(uint64_t pc, uint32_t raw);
    static DecodedInstruction decode_load_store(uint64_t pc, uint32_t raw);
    static DecodedInstruction decode_system(uint64_t pc, uint32_t raw);
    static DecodedInstruction decode_crypto(uint64_t pc, uint32_t raw);
    static DecodedInstruction decode_simd(uint64_t pc, uint32_t raw);
};

// =====================================================================
// SymbolType
// =====================================================================
enum class SymbolType : uint8_t {
    NoType = 0,
    Object = 1,
    Func = 2,
    Section = 3,
    File = 4,
    Common = 5,
    Tls = 6,
    Other = 255,
};

/// Extract symbol type from st_info byte (lower 4 bits)
inline SymbolType symbol_type_from_info(uint8_t st_info) {
    switch (st_info & 0xF) {
        case 0: return SymbolType::NoType;
        case 1: return SymbolType::Object;
        case 2: return SymbolType::Func;
        case 3: return SymbolType::Section;
        case 4: return SymbolType::File;
        case 5: return SymbolType::Common;
        case 6: return SymbolType::Tls;
        default: return SymbolType::Other;
    }
}

// =====================================================================
// SymbolBinding
// =====================================================================
enum class SymbolBinding : uint8_t {
    Local = 0,
    Global = 1,
    Weak = 2,
    Other = 255,
};

/// Extract symbol binding from st_info byte (upper 4 bits)
inline SymbolBinding symbol_binding_from_info(uint8_t st_info) {
    switch (st_info >> 4) {
        case 0: return SymbolBinding::Local;
        case 1: return SymbolBinding::Global;
        case 2: return SymbolBinding::Weak;
        default: return SymbolBinding::Other;
    }
}

// =====================================================================
// Symbol
// =====================================================================
struct Symbol {
    std::string name;
    uint64_t address = 0;
    uint64_t size = 0;
    SymbolType sym_type = SymbolType::NoType;
    SymbolBinding binding = SymbolBinding::Local;
    uint16_t section_index = 0;
};

// =====================================================================
// SymbolTableStats
// =====================================================================
struct SymbolTableStats {
    std::size_t total_symbols = 0;
    std::size_t functions = 0;
    std::size_t objects = 0;
    std::size_t other = 0;
};

// =====================================================================
// SymbolTable
// =====================================================================
class SymbolTable {
public:
    SymbolTable() = default;

    /// Add a symbol to the table
    void add(const Symbol& symbol);

    /// Look up symbol by exact address
    const Symbol* lookup(uint64_t address) const;

    /// Find function containing an address
    std::string_view find_function(uint64_t address) const;

    /// Find symbol by name
    const Symbol* find_by_name(std::string_view name) const;

    /// Get all functions (start, end, name)
    const std::vector<std::tuple<uint64_t, uint64_t, std::string>>& functions() const;

    /// Get all symbols
    const std::unordered_map<uint64_t, Symbol>& symbols() const;

    /// Get symbol count
    std::size_t len() const;

    /// Check if table is empty
    bool is_empty() const;

    /// Get demangled name for a symbol
    std::string demangle(std::string_view name) const;

    /// Find symbols near an address (for debugging)
    std::vector<const Symbol*> find_nearby(uint64_t address, uint64_t range) const;

    /// Get statistics about the symbol table
    SymbolTableStats stats() const;

private:
    std::string demangle_cpp(std::string_view mangled) const;

    std::unordered_map<uint64_t, Symbol> by_address_;
    std::vector<std::tuple<uint64_t, uint64_t, std::string>> functions_;
    mutable std::unordered_map<std::string, std::string> demangled_cache_;
};

// =====================================================================
// ElfLoader
// =====================================================================
class ElfLoader {
public:
    /// Load an ELF file from a file path
    static Result<ElfLoader> load(const std::string& path);

    /// Parse ELF data from a byte buffer
    static Result<ElfLoader> parse(const std::vector<uint8_t>& data);
    static Result<ElfLoader> parse(std::span<const uint8_t> data);

    /// Get entry point address
    uint64_t entry_point() const;

    /// Get symbol name at address
    std::optional<std::string_view> get_symbol(uint64_t addr) const;

    /// Get function containing address
    std::optional<std::string_view> get_function_at(uint64_t addr) const;

    /// Read memory at address
    std::optional<std::vector<uint8_t>> read_memory(uint64_t addr, std::size_t size) const;

    /// Read instruction at address (4 bytes for ARM64)
    std::optional<uint32_t> read_instruction(uint64_t addr) const;

    /// Get all memory segments
    const std::vector<MemorySegment>& segments() const;

    /// Get all function symbols (start, end, name)
    const std::vector<std::tuple<uint64_t, uint64_t, std::string>>& functions() const;

    /// Get executable segments
    std::vector<const MemorySegment*> executable_segments() const;

    /// Get the ELF header
    const ElfHeader& header() const;

    /// Get program headers
    const std::vector<ProgramHeader>& program_headers() const;

    /// Get section headers
    const std::vector<SectionHeader>& section_headers() const;

    /// Get the symbol table
    const SymbolTable& symbol_table() const;

    /// Decode an instruction at the given address using the ARM64 decoder
    std::optional<DecodedInstruction> decode_at(uint64_t addr) const;

private:
    ElfLoader(std::vector<uint8_t> data, ElfHeader header,
              std::vector<ProgramHeader> phdrs,
              std::vector<SectionHeader> shdrs,
              std::vector<MemorySegment> segments,
              SymbolTable symbols);

    static Result<ElfHeader> parse_elf_header(std::span<const uint8_t> data);
    static Result<std::vector<ProgramHeader>> parse_program_headers(std::span<const uint8_t> data, const ElfHeader& header);
    static Result<std::vector<SectionHeader>> parse_section_headers(std::span<const uint8_t> data, const ElfHeader& header);
    static Result<std::vector<MemorySegment>> load_segments(std::span<const uint8_t> data, const std::vector<ProgramHeader>& phdrs);
    static Result<SymbolTable> parse_symbols(std::span<const uint8_t> data, const std::vector<SectionHeader>& shdrs);

    std::vector<uint8_t> data_;
    ElfHeader header_;
    std::vector<ProgramHeader> program_headers_;
    std::vector<SectionHeader> section_headers_;
    std::vector<MemorySegment> segments_;
    SymbolTable symbols_;
};

} // namespace arm_cpu
