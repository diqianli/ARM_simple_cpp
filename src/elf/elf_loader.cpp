/// @file elf_loader.cpp
/// @brief ELF file loader implementation for ARM64 executables.
///
/// Ported from Rust src/elf/loader.rs, src/elf/decoder.rs, src/elf/symbols.rs

#include "arm_cpu/elf/elf_loader.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <span>
#include <sstream>

namespace arm_cpu {

// =====================================================================
// Helper: format address as hex string
// =====================================================================
namespace {

std::string format_addr(uint64_t addr) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "0x%llx", static_cast<unsigned long long>(addr));
    return std::string(buf);
}

// Helper: read little-endian integers from byte span
template<typename T>
T read_le(std::span<const uint8_t> data, std::size_t offset) {
    T val = 0;
    for (std::size_t i = 0; i < sizeof(T) && (offset + i) < data.size(); ++i) {
        val |= static_cast<uint64_t>(data[offset + i]) << (i * 8);
    }
    return val;
}

} // anonymous namespace

// =====================================================================
// Arm64Decoder implementation
// =====================================================================

DecodedInstruction Arm64Decoder::decode(uint64_t pc, uint32_t raw) {
    DecodedInstruction instr(pc, raw);

    // Top-level decode based on bits [28:25]
    uint32_t op0 = (raw >> 25) & 0xF;

    switch (op0) {
        case 0x4:  // 0100 - Loads and stores
        case 0x5:  // 0101
        case 0x6:  // 0110
        case 0x7:  // 0111
            return decode_load_store(pc, raw);
        case 0x8:  // 1000 - Data processing (register)
            return decode_data_processing(pc, raw);
        case 0x9:  // 1001 - Data processing (register) / SIMD
            return decode_data_processing(pc, raw);
        case 0xA:  // 1010 - Branches, exception generating, system
            return decode_branch(pc, raw);
        case 0xB:  // 1011 - Branches, exception generating, system
            return decode_branch(pc, raw);
        default:
            break;
    }

    instr.opcode = OpcodeType::Other;
    instr.disasm = "UNKNOWN";
    return instr;
}

DecodedInstruction Arm64Decoder::decode_data_processing(uint64_t pc, uint32_t raw) {
    DecodedInstruction instr(pc, raw);

    uint32_t op0 = (raw >> 25) & 0xF;
    uint32_t op1 = (raw >> 20) & 0x1F;
    uint32_t op2 = (raw >> 15) & 0x1F;  // used for crypto detection
    uint32_t rd = raw & 0x1F;
    uint32_t rn = (raw >> 5) & 0x1F;

    (void)op1; (void)op2; // may be used in extended decoding

    // Check for SIMD/FP operations (bit 28 set, bit 26 set for 0x9 pattern)
    if (op0 == 0x9 && (raw & (1u << 28)) == 0) {
        return decode_simd(pc, raw);
    }

    // Check for crypto (op1 bits indicate AESE/AESD/SHA)
    uint32_t op1_top = (raw >> 29) & 0x3;
    if (op0 == 0x8 && op1_top == 0x2) {
        return decode_crypto(pc, raw);
    }

    // Data processing - register
    instr.dst_regs.push_back(Reg(static_cast<uint8_t>(rd)));
    instr.src_regs.push_back(Reg(static_cast<uint8_t>(rn)));

    uint32_t rm = (raw >> 16) & 0x1F;
    instr.src_regs.push_back(Reg(static_cast<uint8_t>(rm)));

    // Determine specific operation
    if ((raw & (0x3Fu << 21)) == 0x0u << 21) {
        // ADD (shifted register)
        instr.opcode = OpcodeType::Add;
        instr.disasm = "ADD X" + std::to_string(rd) + ", X" + std::to_string(rn) + ", X" + std::to_string(rm);
    } else if ((raw & (0x3Fu << 21)) == 0x4u << 21) {
        // SUB (shifted register)
        instr.opcode = OpcodeType::Sub;
        instr.disasm = "SUB X" + std::to_string(rd) + ", X" + std::to_string(rn) + ", X" + std::to_string(rm);
    } else if ((raw & (0x3Fu << 21)) == 0x0Au << 21) {
        // AND (shifted register)
        instr.opcode = OpcodeType::And;
        instr.disasm = "AND X" + std::to_string(rd) + ", X" + std::to_string(rn) + ", X" + std::to_string(rm);
    } else if ((raw & (0x3Fu << 21)) == 0x20u << 21) {
        // ORR (shifted register)
        instr.opcode = OpcodeType::Orr;
        instr.disasm = "ORR X" + std::to_string(rd) + ", X" + std::to_string(rn) + ", X" + std::to_string(rm);
    } else if ((raw & (0x3Fu << 21)) == 0x24u << 21) {
        // EOR (shifted register)
        instr.opcode = OpcodeType::Eor;
        instr.disasm = "EOR X" + std::to_string(rd) + ", X" + std::to_string(rn) + ", X" + std::to_string(rm);
    } else if ((raw & (0xFFu << 21)) == 0x1Au << 21) {
        // MADD
        instr.opcode = OpcodeType::Fmadd;
        instr.disasm = "MADD X" + std::to_string(rd) + ", X" + std::to_string(rn) + ", X" + std::to_string(rm);
    } else if ((raw & (0xFFu << 21)) == 0x1Bu << 21) {
        // MSUB
        instr.opcode = OpcodeType::Fmsub;
        instr.disasm = "MSUB X" + std::to_string(rd) + ", X" + std::to_string(rn) + ", X" + std::to_string(rm);
    } else if ((raw & (0x1F << 24)) == (0x1Cu << 24)) {
        // SDIV / UDIV
        instr.opcode = OpcodeType::Div;
        instr.disasm = "DIV X" + std::to_string(rd) + ", X" + std::to_string(rn) + ", X" + std::to_string(rm);
    } else if ((raw & (0x3F << 21)) == (0x0C << 21)) {
        // MADD
        instr.opcode = OpcodeType::Fmadd;
        instr.disasm = "MADD X" + std::to_string(rd) + ", X" + std::to_string(rn) + ", X" + std::to_string(rm);
    } else {
        instr.opcode = OpcodeType::Other;
        instr.disasm = "DATA_PROC";
    }

    return instr;
}

DecodedInstruction Arm64Decoder::decode_branch(uint64_t pc, uint32_t raw) {
    DecodedInstruction instr(pc, raw);

    uint32_t op0 = (raw >> 25) & 0xF;

    if (op0 == 0xA || op0 == 0xB) {
        uint32_t op1 = (raw >> 26) & 0x7;

        if (op1 == 0x4) {
            // Conditional branch (B.cond)
            uint32_t imm19 = (raw >> 5) & 0x7FFFF;
            // Sign extend 19-bit
            if (imm19 & (1u << 18)) imm19 |= ~0x7FFFF;
            int64_t offset = static_cast<int64_t>(static_cast<int32_t>(imm19 << 13) >> 13);
            uint64_t target = pc + static_cast<uint64_t>(offset);
            instr.opcode = OpcodeType::BranchCond;
            instr.branch_target = target;
            instr.is_conditional = true;
            instr.disasm = "B.cond " + format_addr(target);
        } else if (op1 == 0x0 || op1 == 0x2) {
            // Unconditional branch immediate (B / BL)
            uint32_t imm26 = raw & 0x3FFFFFF;
            if (imm26 & (1u << 25)) imm26 |= ~0x3FFFFFF;
            int64_t offset = static_cast<int64_t>(static_cast<int32_t>(imm26 << 6) >> 6);
            uint64_t target = pc + static_cast<uint64_t>(offset);
            instr.opcode = OpcodeType::Branch;
            instr.branch_target = target;
            instr.is_conditional = false;
            if (op1 == 0x2) {
                instr.disasm = "BL " + format_addr(target);
            } else {
                instr.disasm = "B " + format_addr(target);
            }
        } else if (op1 == 0x5) {
            // Unconditional branch register (BR / BLR / RET)
            uint32_t rn = (raw >> 5) & 0x1F;
            uint32_t op2 = (raw >> 21) & 0x7FF;
            instr.src_regs.push_back(Reg(static_cast<uint8_t>(rn)));
            instr.opcode = OpcodeType::BranchReg;
            instr.is_conditional = false;
            if (op2 == 0x3F) {
                instr.disasm = "BLR X" + std::to_string(rn);
            } else if (op2 == 0x1F) {
                instr.disasm = "BR X" + std::to_string(rn);
            } else {
                instr.disasm = "RET X" + std::to_string(rn);
            }
        } else if (op1 == 0x6) {
            // System instructions
            return decode_system(pc, raw);
        } else {
            instr.opcode = OpcodeType::Other;
            instr.disasm = "SYSTEM";
        }
    }

    return instr;
}

DecodedInstruction Arm64Decoder::decode_load_store(uint64_t pc, uint32_t raw) {
    DecodedInstruction instr(pc, raw);

    uint32_t size = (raw >> 30) & 0x3;
    uint32_t v = (raw >> 26) & 0x1;
    uint32_t opc = (raw >> 22) & 0x3;
    uint32_t rn = (raw >> 5) & 0x1F;
    uint32_t rt = raw & 0x1F;
    uint32_t rt2 = (raw >> 10) & 0x1F;

    static constexpr uint64_t size_bytes[] = {1, 2, 4, 8};

    // Determine load vs store
    bool is_load = (opc & 0x1) == 0;
    bool is_pair = ((raw >> 23) & 0x1) == 1 && ((raw >> 21) & 0x1) == 0;

    instr.is_load = is_load;

    if (v) {
        // SIMD load/store
        instr.src_vregs.push_back(VReg(static_cast<uint8_t>(rt)));
        if (is_load) {
            instr.opcode = OpcodeType::Vld;
            instr.disasm = "VLD V" + std::to_string(rt);
        } else {
            instr.opcode = OpcodeType::Vst;
            instr.disasm = "VST V" + std::to_string(rt);
        }
    } else if (is_pair) {
        // Load/Store pair
        instr.src_regs.push_back(Reg(static_cast<uint8_t>(rn)));
        instr.dst_regs.push_back(Reg(static_cast<uint8_t>(rt)));
        instr.dst_regs.push_back(Reg(static_cast<uint8_t>(rt2)));
        if (is_load) {
            instr.opcode = OpcodeType::LoadPair;
            instr.disasm = "LDP X" + std::to_string(rt) + ", X" + std::to_string(rt2);
        } else {
            instr.opcode = OpcodeType::StorePair;
            instr.disasm = "STP X" + std::to_string(rt) + ", X" + std::to_string(rt2);
        }
    } else {
        instr.src_regs.push_back(Reg(static_cast<uint8_t>(rn)));
        instr.dst_regs.push_back(Reg(static_cast<uint8_t>(rt)));
        if (is_load) {
            instr.opcode = OpcodeType::Load;
            instr.disasm = "LDR X" + std::to_string(rt) + ", [X" + std::to_string(rn) + "]";
        } else {
            instr.opcode = OpcodeType::Store;
            instr.disasm = "STR X" + std::to_string(rt) + ", [X" + std::to_string(rn) + "]";
        }
    }

    if (size < 4) {
        instr.mem_size = static_cast<uint8_t>(size_bytes[size]);
    } else {
        instr.mem_size = 8;
    }

    return instr;
}

DecodedInstruction Arm64Decoder::decode_system(uint64_t pc, uint32_t raw) {
    DecodedInstruction instr(pc, raw);

    uint32_t l = (raw >> 21) & 0x7;
    uint32_t op0 = (raw >> 19) & 0x3;
    uint32_t crn = (raw >> 12) & 0xF;
    uint32_t crm = (raw >> 8) & 0xF;
    uint32_t op2 = (raw >> 5) & 0x7;
    uint32_t rt = raw & 0x1F;

    (void)crn; (void)crm; (void)op2; // may be used in extended decoding

    instr.dst_regs.push_back(Reg(static_cast<uint8_t>(rt)));

    // Cache maintenance
    uint32_t cm_op = (raw >> 19) & 0x1F;
    switch (cm_op) {
        case 0x04: // DC ZVA
            instr.opcode = OpcodeType::DcZva;
            instr.disasm = "DC ZVA, X" + std::to_string(rt);
            return instr;
        case 0x0B: // DC CIVAC
            instr.opcode = OpcodeType::DcCivac;
            instr.disasm = "DC CIVAC, X" + std::to_string(rt);
            return instr;
        case 0x0C: // DC CVAC
            instr.opcode = OpcodeType::DcCvac;
            instr.disasm = "DC CVAC, X" + std::to_string(rt);
            return instr;
        case 0x18: // IC IVAU
            instr.opcode = OpcodeType::IcIvau;
            instr.disasm = "IC IVAU, X" + std::to_string(rt);
            return instr;
        case 0x1A: // IC IALLU
            instr.opcode = OpcodeType::IcIallu;
            instr.disasm = "IC IALLU";
            return instr;
        case 0x1B: // IC IALLUIS
            instr.opcode = OpcodeType::IcIalluis;
            instr.disasm = "IC IALLUIS";
            return instr;
        default:
            break;
    }

    // MSR/MRS
    if (l == 3 && op0 == 2) {
        instr.opcode = OpcodeType::Mrs;
        instr.disasm = "MRS X" + std::to_string(rt);
    } else if (l == 3 && op0 == 3) {
        instr.opcode = OpcodeType::Msr;
        instr.disasm = "MSR X" + std::to_string(rt);
    } else if (l == 0) {
        instr.opcode = OpcodeType::Sys;
        instr.disasm = "SYS";
    } else {
        instr.opcode = OpcodeType::Other;
        instr.disasm = "SYSTEM";
    }

    return instr;
}

DecodedInstruction Arm64Decoder::decode_crypto(uint64_t pc, uint32_t raw) {
    DecodedInstruction instr(pc, raw);

    uint32_t rd = raw & 0x1F;
    uint32_t rn = (raw >> 5) & 0x1F;

    instr.dst_vregs.push_back(VReg(static_cast<uint8_t>(rd)));
    instr.src_vregs.push_back(VReg(static_cast<uint8_t>(rn)));

    // Simplified crypto decode
    if ((raw & 0xFF00FC00) == 0x4E284800) {
        instr.opcode = OpcodeType::Aese;
        instr.disasm = "AESE V" + std::to_string(rd) + ".16B, V" + std::to_string(rn) + ".16B";
    } else if ((raw & 0xFF00FC00) == 0x4E285800) {
        instr.opcode = OpcodeType::Aesd;
        instr.disasm = "AESD V" + std::to_string(rd) + ".16B, V" + std::to_string(rn) + ".16B";
    } else if ((raw & 0xFF00FC00) == 0x4E286800) {
        instr.opcode = OpcodeType::Aesmc;
        instr.disasm = "AESMC V" + std::to_string(rd) + ".16B, V" + std::to_string(rn) + ".16B";
    } else if ((raw & 0xFF00FC00) == 0x4E287800) {
        instr.opcode = OpcodeType::Aesimc;
        instr.disasm = "AESIMC V" + std::to_string(rd) + ".16B, V" + std::to_string(rn) + ".16B";
    } else if ((raw & 0xFFFFFC00) == 0x5E280800) {
        instr.opcode = OpcodeType::Sha1H;
        instr.disasm = "SHA1H V" + std::to_string(rd) + ".4S";
    } else if ((raw & 0xFF00FC00) == 0x5E004000) {
        instr.opcode = OpcodeType::Sha256H;
        instr.disasm = "SHA256H V" + std::to_string(rd);
    } else if ((raw & 0xFFFFFC00) == 0xCE608000) {
        instr.opcode = OpcodeType::Sha512H;
        instr.disasm = "SHA512H V" + std::to_string(rd);
    } else {
        instr.opcode = OpcodeType::Other;
        instr.disasm = "CRYPTO";
    }

    return instr;
}

DecodedInstruction Arm64Decoder::decode_simd(uint64_t pc, uint32_t raw) {
    DecodedInstruction instr(pc, raw);

    uint32_t rd = raw & 0x1F;
    uint32_t rn = (raw >> 5) & 0x1F;
    uint32_t rm = (raw >> 16) & 0x1F;

    instr.dst_vregs.push_back(VReg(static_cast<uint8_t>(rd)));
    instr.src_vregs.push_back(VReg(static_cast<uint8_t>(rn)));
    instr.src_vregs.push_back(VReg(static_cast<uint8_t>(rm)));

    // Simplified SIMD decode based on opcode fields
    if ((raw & 0xFF20FC00) == 0x4E208400) {
        instr.opcode = OpcodeType::Vadd;
        instr.disasm = "ADD V" + std::to_string(rd) + ".4S, V" + std::to_string(rn) + ".4S, V" + std::to_string(rm) + ".4S";
    } else if ((raw & 0xFF20FC00) == 0x4E208C00) {
        instr.opcode = OpcodeType::Vsub;
        instr.disasm = "SUB V" + std::to_string(rd) + ".4S, V" + std::to_string(rn) + ".4S, V" + std::to_string(rm) + ".4S";
    } else if ((raw & 0xFF20FC00) == 0x6F008400) {
        instr.opcode = OpcodeType::Vmul;
        instr.disasm = "MUL V" + std::to_string(rd) + ".4S, V" + std::to_string(rn) + ".4S, V" + std::to_string(rm) + ".4S";
    } else if ((raw & 0xFF20FC00) == 0x0F008000) {
        instr.opcode = OpcodeType::Fadd;
        instr.disasm = "FADD V" + std::to_string(rd) + ".4S";
    } else if ((raw & 0xFF20FC00) == 0x0F008400) {
        instr.opcode = OpcodeType::Fsub;
        instr.disasm = "FSUB V" + std::to_string(rd) + ".4S";
    } else if ((raw & 0xFF20FC00) == 0x0F008C00) {
        instr.opcode = OpcodeType::Fmul;
        instr.disasm = "FMUL V" + std::to_string(rd) + ".4S";
    } else if ((raw & 0xFF20FC00) == 0x0F008400) {
        instr.opcode = OpcodeType::Fdiv;
        instr.disasm = "FDIV V" + std::to_string(rd) + ".4S";
    } else {
        instr.opcode = OpcodeType::Other;
        instr.disasm = "SIMD";
    }

    return instr;
}

// =====================================================================
// SymbolTable implementation
// =====================================================================

void SymbolTable::add(const Symbol& symbol) {
    by_address_[symbol.address] = symbol;

    if (symbol.sym_type == SymbolType::Func && symbol.size > 0) {
        uint64_t end = symbol.address + symbol.size;
        functions_.emplace_back(symbol.address, end, symbol.name);
        // Keep sorted by address
        std::sort(functions_.begin(), functions_.end(),
            [](const auto& a, const auto& b) { return std::get<0>(a) < std::get<0>(b); });
    }
}

const Symbol* SymbolTable::lookup(uint64_t address) const {
    auto it = by_address_.find(address);
    if (it != by_address_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::string_view SymbolTable::find_function(uint64_t address) const {
    // Binary search for function containing address
    auto it = std::upper_bound(functions_.begin(), functions_.end(), address,
        [](uint64_t addr, const auto& entry) {
            return addr < std::get<0>(entry);
        });

    if (it != functions_.begin()) {
        --it;
        const auto& [start, end, name] = *it;
        if (address >= start && address < end) {
            return name;
        }
    }

    return {};
}

const Symbol* SymbolTable::find_by_name(std::string_view name) const {
    for (const auto& [addr, sym] : by_address_) {
        if (sym.name == name) {
            return &sym;
        }
    }
    return nullptr;
}

const std::vector<std::tuple<uint64_t, uint64_t, std::string>>& SymbolTable::functions() const {
    return functions_;
}

const std::unordered_map<uint64_t, Symbol>& SymbolTable::symbols() const {
    return by_address_;
}

std::size_t SymbolTable::len() const {
    return by_address_.size();
}

bool SymbolTable::is_empty() const {
    return by_address_.empty();
}

std::string SymbolTable::demangle(std::string_view name) const {
    // Check cache
    std::string key(name);
    auto it = demangled_cache_.find(key);
    if (it != demangled_cache_.end()) {
        return it->second;
    }

    std::string result;
    if (name.size() >= 2 && name[0] == '_' && name[1] == 'Z') {
        result = demangle_cpp(name);
    } else {
        result = std::string(name);
    }

    demangled_cache_[key] = result;
    return result;
}

std::string SymbolTable::demangle_cpp(std::string_view mangled) const {
    if (mangled.size() < 2 || mangled[0] != '_' || mangled[1] != 'Z') {
        return std::string(mangled);
    }

    std::string_view rest = mangled.substr(2);

    // Handle nested names like _ZN3foo3barEv
    if (!rest.empty() && rest[0] == 'N') {
        std::vector<std::string_view> parts;
        std::size_t pos = 1; // Skip 'N'

        while (pos < rest.size()) {
            // Read length prefix
            std::size_t start = pos;
            while (pos < rest.size() && rest[pos] >= '0' && rest[pos] <= '9') {
                ++pos;
            }

            if (pos > start) {
                std::size_t len = 0;
                for (std::size_t i = start; i < pos; ++i) {
                    len = len * 10 + (rest[i] - '0');
                }
                if (pos + len <= rest.size() && len > 0) {
                    parts.push_back(rest.substr(pos, len));
                    pos += len;
                    continue;
                }
            }
            break;
        }

        if (!parts.empty()) {
            std::string result;
            for (std::size_t i = 0; i < parts.size(); ++i) {
                if (i > 0) result += "::";
                result += parts[i];
            }
            return result;
        }
    }

    // Handle simple names like _Z3foov -> foo
    {
        std::size_t pos = 0;
        std::size_t start = pos;
        while (pos < rest.size() && rest[pos] >= '0' && rest[pos] <= '9') {
            ++pos;
        }

        if (pos > start) {
            std::size_t len = 0;
            for (std::size_t i = start; i < pos; ++i) {
                len = len * 10 + (rest[i] - '0');
            }
            if (pos + len <= rest.size() && len > 0) {
                return std::string(rest.substr(pos, len));
            }
        }
    }

    return std::string(mangled);
}

std::vector<const Symbol*> SymbolTable::find_nearby(uint64_t address, uint64_t range) const {
    std::vector<const Symbol*> result;
    uint64_t min_addr = (address >= range) ? (address - range) : 0;
    uint64_t max_addr = address + range;

    for (const auto& [addr, sym] : by_address_) {
        if (addr >= min_addr && addr <= max_addr) {
            result.push_back(&sym);
        }
    }

    return result;
}

SymbolTableStats SymbolTable::stats() const {
    SymbolTableStats s;
    for (const auto& [addr, sym] : by_address_) {
        switch (sym.sym_type) {
            case SymbolType::Func: s.functions++; break;
            case SymbolType::Object: s.objects++; break;
            default: s.other++; break;
        }
    }
    s.total_symbols = by_address_.size();
    return s;
}

// =====================================================================
// ElfLoader implementation
// =====================================================================

Result<ElfLoader> ElfLoader::load(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return EmulatorError::trace_parse("Failed to open ELF file: " + path);
    }

    std::vector<uint8_t> data(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>()
    );

    if (file.fail() && !file.eof()) {
        return EmulatorError::trace_parse("Failed to read ELF file: " + path);
    }

    return parse(data);
}

Result<ElfLoader> ElfLoader::parse(const std::vector<uint8_t>& data) {
    return parse(std::span<const uint8_t>(data));
}

Result<ElfLoader> ElfLoader::parse(std::span<const uint8_t> data) {
    // Verify ELF magic
    if (data.size() < 16) {
        return EmulatorError::trace_parse("File too small to be ELF");
    }

    constexpr std::array<uint8_t, 4> elf_magic = {0x7F, 'E', 'L', 'F'};
    if (!std::equal(elf_magic.begin(), elf_magic.end(), data.begin())) {
        return EmulatorError::trace_parse("Invalid ELF magic number");
    }

    // Check class (64-bit)
    if (data[4] != 2) {
        return EmulatorError::trace_parse("Only 64-bit ELF files are supported");
    }

    // Check endianness (little-endian)
    if (data[5] != 1) {
        return EmulatorError::trace_parse("Only little-endian ELF files are supported");
    }

    // Parse header
    auto header = TRY(parse_elf_header(data));

    // Verify ARM64 machine type (0xB7)
    if (header.machine != 0xB7) {
        return EmulatorError::trace_parse(
            "Expected ARM64 (0xB7), got machine type 0x" +
            [&]() { char buf[16]; std::snprintf(buf, sizeof(buf), "%X", header.machine); return std::string(buf); }());
    }

    // Parse program headers
    auto phdrs = TRY(parse_program_headers(data, header));

    // Parse section headers
    auto shdrs = TRY(parse_section_headers(data, header));

    // Load segments
    auto segments = TRY(load_segments(data, phdrs));

    // Parse symbols
    auto symtab = TRY(parse_symbols(data, shdrs));

    std::vector<uint8_t> data_copy(data.begin(), data.end());

    return ElfLoader(
        std::move(data_copy),
        std::move(header),
        std::move(phdrs),
        std::move(shdrs),
        std::move(segments),
        std::move(symtab)
    );
}

Result<ElfHeader> ElfLoader::parse_elf_header(std::span<const uint8_t> data) {
    if (data.size() < 64) {
        return EmulatorError::trace_parse("ELF header too small");
    }

    ElfHeader h;
    h.entry_point = read_le<uint64_t>(data, 24);
    h.machine    = read_le<uint16_t>(data, 18);
    h.phnum      = read_le<uint16_t>(data, 56);
    h.shnum      = read_le<uint16_t>(data, 60);
    h.phoff      = read_le<uint64_t>(data, 32);
    h.shoff      = read_le<uint64_t>(data, 40);
    h.shstrndx   = read_le<uint16_t>(data, 62);
    return h;
}

Result<std::vector<ProgramHeader>> ElfLoader::parse_program_headers(
    std::span<const uint8_t> data, const ElfHeader& header)
{
    std::vector<ProgramHeader> headers;

    for (std::size_t i = 0; i < header.phnum; ++i) {
        std::size_t offset = static_cast<std::size_t>(header.phoff) + i * 56;
        if (offset + 56 > data.size()) break;

        ProgramHeader ph;
        ph.p_type  = read_le<uint32_t>(data, offset);
        ph.p_flags = read_le<uint32_t>(data, offset + 4);
        ph.p_offset = read_le<uint64_t>(data, offset + 8);
        ph.p_vaddr  = read_le<uint64_t>(data, offset + 16);
        ph.p_filesz = read_le<uint64_t>(data, offset + 32);
        ph.p_memsz  = read_le<uint64_t>(data, offset + 40);
        ph.p_align  = read_le<uint64_t>(data, offset + 48);
        headers.push_back(ph);
    }

    return headers;
}

Result<std::vector<SectionHeader>> ElfLoader::parse_section_headers(
    std::span<const uint8_t> data, const ElfHeader& header)
{
    std::vector<SectionHeader> headers;

    for (std::size_t i = 0; i < header.shnum; ++i) {
        std::size_t offset = static_cast<std::size_t>(header.shoff) + i * 64;
        if (offset + 64 > data.size()) break;

        SectionHeader sh;
        sh.sh_name  = read_le<uint32_t>(data, offset);
        sh.sh_type  = read_le<uint32_t>(data, offset + 4);
        sh.sh_flags = read_le<uint64_t>(data, offset + 8);
        sh.sh_addr  = read_le<uint64_t>(data, offset + 16);
        sh.sh_offset = read_le<uint64_t>(data, offset + 24);
        sh.sh_size  = read_le<uint64_t>(data, offset + 32);
        headers.push_back(sh);
    }

    // Resolve section names from shstrtab
    if (header.shstrndx > 0 && static_cast<std::size_t>(header.shstrndx) < headers.size()) {
        const auto& strtab = headers[header.shstrndx];
        std::size_t strtab_offset = static_cast<std::size_t>(strtab.sh_offset);
        std::size_t strtab_size = static_cast<std::size_t>(strtab.sh_size);

        for (auto& sh : headers) {
            if (sh.sh_name > 0) {
                std::size_t name_start = strtab_offset + static_cast<std::size_t>(sh.sh_name);
                if (name_start < data.size()) {
                    std::size_t remaining = strtab_offset + strtab_size;
                    if (name_start >= remaining) continue;

                    auto sub = data.subspan(name_start, remaining - name_start);
                    auto null_pos = static_cast<std::size_t>(
                        std::distance(sub.begin(), std::find(sub.begin(), sub.end(), 0)));
                    if (null_pos == 0) null_pos = remaining - name_start;

                    sh.name = std::string(
                        reinterpret_cast<const char*>(sub.data()), null_pos);
                }
            }
        }
    }

    return headers;
}

Result<std::vector<MemorySegment>> ElfLoader::load_segments(
    std::span<const uint8_t> data, const std::vector<ProgramHeader>& phdrs)
{
    std::vector<MemorySegment> segments;

    for (const auto& ph : phdrs) {
        if (ph.p_type != 1) continue; // PT_LOAD = 1

        std::size_t start = static_cast<std::size_t>(ph.p_offset);
        std::size_t end = static_cast<std::size_t>(ph.p_offset + ph.p_filesz);
        if (end > data.size()) continue;

        std::vector<uint8_t> segment_data;
        if (ph.p_filesz > 0) {
            segment_data.assign(data.begin() + static_cast<std::ptrdiff_t>(start),
                                data.begin() + static_cast<std::ptrdiff_t>(end));
        }

        // Pad to p_memsz if needed (BSS)
        if (segment_data.size() < static_cast<std::size_t>(ph.p_memsz)) {
            segment_data.resize(static_cast<std::size_t>(ph.p_memsz), 0);
        }

        segments.push_back(MemorySegment{
            .vaddr = ph.p_vaddr,
            .size = segment_data.size(),
            .data = std::move(segment_data),
            .executable = (ph.p_flags & 0x1) != 0,  // PF_X
            .writable   = (ph.p_flags & 0x2) != 0,  // PF_W
            .readable   = (ph.p_flags & 0x4) != 0,  // PF_R
        });
    }

    return segments;
}

Result<SymbolTable> ElfLoader::parse_symbols(
    std::span<const uint8_t> data, const std::vector<SectionHeader>& shdrs)
{
    SymbolTable table;

    // Find .symtab section
    const SectionHeader* symtab_sec = nullptr;
    const SectionHeader* strtab_sec = nullptr;

    for (const auto& sh : shdrs) {
        if (sh.name == ".symtab" || sh.sh_type == 2) { // SHT_SYMTAB
            symtab_sec = &sh;
        }
        if (sh.name == ".strtab") {
            strtab_sec = &sh;
        }
    }

    if (!symtab_sec || !strtab_sec) {
        return table;
    }

    std::size_t symtab_offset = static_cast<std::size_t>(symtab_sec->sh_offset);
    std::size_t symtab_size = static_cast<std::size_t>(symtab_sec->sh_size);
    std::size_t strtab_offset = static_cast<std::size_t>(strtab_sec->sh_offset);
    std::size_t strtab_size = static_cast<std::size_t>(strtab_sec->sh_size);

    // ELF64 symbol entry is 24 bytes
    std::size_t num_symbols = symtab_size / 24;

    for (std::size_t i = 0; i < num_symbols; ++i) {
        std::size_t offset = symtab_offset + i * 24;
        if (offset + 24 > data.size()) break;

        uint32_t st_name = read_le<uint32_t>(data, offset);
        uint8_t  st_info = data[offset + 4];
        uint64_t st_value = read_le<uint64_t>(data, offset + 8);
        uint64_t st_size = read_le<uint64_t>(data, offset + 16);

        // Get symbol name
        std::size_t name_start = strtab_offset + static_cast<std::size_t>(st_name);
        if (name_start >= data.size()) continue;

        auto remaining = data.subspan(name_start, strtab_offset + strtab_size - name_start);
        auto null_pos = static_cast<std::size_t>(
            std::distance(remaining.begin(), std::find(remaining.begin(), remaining.end(), 0)));
        if (null_pos == 0) null_pos = remaining.size();

        std::string name(reinterpret_cast<const char*>(remaining.data()), null_pos);
        if (name.empty()) continue;

        // Symbol type: lower 4 bits of st_info
        uint8_t sym_type_val = st_info & 0xF;
        if (sym_type_val == 2 && st_value != 0) { // STT_FUNC = 2
            Symbol sym;
            sym.name = name;
            sym.address = st_value;
            sym.size = st_size;
            sym.sym_type = SymbolType::Func;
            sym.binding = symbol_binding_from_info(st_info);
            sym.section_index = read_le<uint16_t>(data, offset + 6);
            table.add(sym);
        }
    }

    return table;
}

ElfLoader::ElfLoader(std::vector<uint8_t> data, ElfHeader header,
                     std::vector<ProgramHeader> phdrs,
                     std::vector<SectionHeader> shdrs,
                     std::vector<MemorySegment> segments,
                     SymbolTable symbols)
    : data_(std::move(data))
    , header_(std::move(header))
    , program_headers_(std::move(phdrs))
    , section_headers_(std::move(shdrs))
    , segments_(std::move(segments))
    , symbols_(std::move(symbols))
{}

uint64_t ElfLoader::entry_point() const {
    return header_.entry_point;
}

std::optional<std::string_view> ElfLoader::get_symbol(uint64_t addr) const {
    const Symbol* sym = symbols_.lookup(addr);
    if (sym) return sym->name;
    return {};
}

std::optional<std::string_view> ElfLoader::get_function_at(uint64_t addr) const {
    auto name = symbols_.find_function(addr);
    if (!name.empty()) return name;
    return {};
}

std::optional<std::vector<uint8_t>> ElfLoader::read_memory(uint64_t addr, std::size_t size) const {
    for (const auto& seg : segments_) {
        if (addr >= seg.vaddr && addr + size <= seg.vaddr + seg.size) {
            std::size_t offset = static_cast<std::size_t>(addr - seg.vaddr);
            return std::vector<uint8_t>(
                seg.data.begin() + static_cast<std::ptrdiff_t>(offset),
                seg.data.begin() + static_cast<std::ptrdiff_t>(offset + size));
        }
    }
    return {};
}

std::optional<uint32_t> ElfLoader::read_instruction(uint64_t addr) const {
    auto bytes = read_memory(addr, 4);
    if (!bytes) return {};
    return static_cast<uint32_t>(
        static_cast<uint32_t>((*bytes)[0]) |
        (static_cast<uint32_t>((*bytes)[1]) << 8) |
        (static_cast<uint32_t>((*bytes)[2]) << 16) |
        (static_cast<uint32_t>((*bytes)[3]) << 24));
}

const std::vector<MemorySegment>& ElfLoader::segments() const {
    return segments_;
}

const std::vector<std::tuple<uint64_t, uint64_t, std::string>>& ElfLoader::functions() const {
    return symbols_.functions();
}

std::vector<const MemorySegment*> ElfLoader::executable_segments() const {
    std::vector<const MemorySegment*> result;
    for (const auto& seg : segments_) {
        if (seg.executable) {
            result.push_back(&seg);
        }
    }
    return result;
}

const ElfHeader& ElfLoader::header() const {
    return header_;
}

const std::vector<ProgramHeader>& ElfLoader::program_headers() const {
    return program_headers_;
}

const std::vector<SectionHeader>& ElfLoader::section_headers() const {
    return section_headers_;
}

const SymbolTable& ElfLoader::symbol_table() const {
    return symbols_;
}

std::optional<DecodedInstruction> ElfLoader::decode_at(uint64_t addr) const {
    auto raw = read_instruction(addr);
    if (!raw) return {};
    return Arm64Decoder::decode(addr, *raw);
}

} // namespace arm_cpu
