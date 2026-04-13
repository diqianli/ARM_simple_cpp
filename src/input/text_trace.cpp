/// @file text_trace.cpp
/// @brief Implementation of the text trace parser.

#include "arm_cpu/input/text_trace.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>
#include <sstream>

namespace arm_cpu {

// =====================================================================
// TextTraceParser::from_file
// =====================================================================

Result<TextTraceParser> TextTraceParser::from_file(const std::string& path) {
    std::ifstream stream(path);
    if (!stream.is_open()) {
        return Err(EmulatorError::trace_parse(
            "Failed to open file: " + path));
    }
    return Ok(TextTraceParser(std::move(stream), path));
}

// =====================================================================
// TextTraceParser::reset
// =====================================================================

Result<void> TextTraceParser::reset() {
    reader_.clear();
    reader_.seekg(0, std::ios::beg);
    if (!reader_.good()) {
        return Err(EmulatorError::trace_parse(
            "Failed to reset file: " + file_path_));
    }
    current_id_ = 0;
    line_buffer_.clear();
    return Ok();
}

// =====================================================================
// TextTraceParser::next_impl
// =====================================================================

Result<std::optional<Instruction>> TextTraceParser::next_impl() {
    while (true) {
        line_buffer_.clear();
        if (!std::getline(reader_, line_buffer_)) {
            // EOF
            if (reader_.eof()) {
                return Ok(std::optional<Instruction>{std::nullopt});
            }
            return Err(EmulatorError::trace_parse(
                "Error reading file: " + file_path_));
        }

        auto result = parse_line(line_buffer_);
        if (result.has_error()) {
            return Err(std::move(result.error()));
        }
        auto& opt = result.value();
        if (opt.has_value()) {
            return Ok(std::move(opt));
        }
        // Skip empty/comment lines
    }
}

// =====================================================================
// TextTraceParser::parse_line
// =====================================================================

Result<std::optional<Instruction>> TextTraceParser::parse_line(std::string_view line) {
    // Trim leading/trailing whitespace
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.front()))) {
        line.remove_prefix(1);
    }
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) {
        line.remove_suffix(1);
    }

    // Skip empty lines and comments
    if (line.empty() || line.front() == '#') {
        return Ok(std::optional<Instruction>{std::nullopt});
    }

    // Try format 2 (with colon and disassembly): "PC: DISASSEMBLY"
    auto colon_pos = line.find(':');
    if (colon_pos != std::string_view::npos) {
        auto pc_str = line.substr(0, colon_pos);
        auto disasm = line.substr(colon_pos + 1);

        // Trim
        while (!pc_str.empty() && std::isspace(static_cast<unsigned char>(pc_str.back()))) {
            pc_str.remove_suffix(1);
        }
        while (!disasm.empty() && std::isspace(static_cast<unsigned char>(disasm.front()))) {
            disasm.remove_prefix(1);
        }

        auto pc = parse_hex(pc_str);
        if (pc.has_error()) return Err(std::move(pc.error()));

        auto instr = parse_disassembly(pc.value(), disasm);
        if (instr.has_error()) return Err(std::move(instr.error()));
        return Ok(std::optional<Instruction>{std::move(instr.value())});
    }

    // Format 1 (structured format)
    // Tokenize by whitespace
    std::vector<std::string_view> parts;
    {
        std::string_view remaining = line;
        while (!remaining.empty()) {
            while (!remaining.empty() && std::isspace(static_cast<unsigned char>(remaining.front()))) {
                remaining.remove_prefix(1);
            }
            if (remaining.empty()) break;
            auto end = remaining.find_first_of(" \t\r\n");
            if (end == std::string_view::npos) end = remaining.size();
            parts.push_back(remaining.substr(0, end));
            remaining.remove_prefix(end);
        }
    }

    if (parts.empty()) {
        return Ok(std::optional<Instruction>{std::nullopt});
    }

    auto pc = parse_hex(parts[0]);
    if (pc.has_error()) return Err(std::move(pc.error()));

    auto opcode_type = OpcodeType::Other;
    if (parts.size() > 1) {
        auto op_result = parse_opcode_type(parts[1]);
        if (op_result.has_error()) return Err(std::move(op_result.error()));
        opcode_type = op_result.value();
    }

    Instruction instr(InstructionId(current_id_), pc.value(), 0, opcode_type);
    ++current_id_;

    // Parse remaining fields
    std::size_t idx = 2;
    while (idx < parts.size()) {
        auto part = parts[idx];

        // Source registers (format: R:0,1,2 or X:0,1,2 or src=0,1,2)
        if (part.starts_with("R:") || part.starts_with("X:") || part.starts_with("src=")) {
            auto regs_str = part.substr(part.find(':') != std::string_view::npos ? part.find(':') + 1 : 4);
            // Split by comma
            std::string_view r = regs_str;
            while (!r.empty()) {
                auto comma = r.find(',');
                auto token = (comma != std::string_view::npos) ? r.substr(0, comma) : r;
                if (auto reg = parse_reg(token)) {
                    instr.src_regs.push_back(*reg);
                }
                if (comma != std::string_view::npos) {
                    r.remove_prefix(comma + 1);
                } else {
                    break;
                }
            }
        }
        // Destination registers
        else if (part.starts_with("W:") || part.starts_with("dst=")) {
            auto regs_str = part.substr(part.find(':') != std::string_view::npos ? part.find(':') + 1 : 4);
            std::string_view r = regs_str;
            while (!r.empty()) {
                auto comma = r.find(',');
                auto token = (comma != std::string_view::npos) ? r.substr(0, comma) : r;
                if (auto reg = parse_reg(token)) {
                    instr.dst_regs.push_back(*reg);
                }
                if (comma != std::string_view::npos) {
                    r.remove_prefix(comma + 1);
                } else {
                    break;
                }
            }
        }
        // Memory address
        else if (part.starts_with("mem=") || part.starts_with("addr=")) {
            auto eq = part.find('=');
            auto addr_str = (eq != std::string_view::npos) ? part.substr(eq + 1) : std::string_view("0");
            auto addr = parse_hex(addr_str);
            if (addr.has_error()) return Err(std::move(addr.error()));

            uint8_t size = 8;
            if (idx + 1 < parts.size() && parts[idx + 1].starts_with("size=")) {
                ++idx;
                auto size_eq = parts[idx].find('=');
                auto size_str = (size_eq != std::string_view::npos) ? parts[idx].substr(size_eq + 1) : std::string_view("8");
                uint8_t parsed = 8;
                std::from_chars(size_str.data(), size_str.data() + size_str.size(), parsed);
                size = parsed;
            }

            bool is_load = (opcode_type == OpcodeType::Load || opcode_type == OpcodeType::LoadPair);
            instr.mem_access = MemAccess{addr.value(), size, is_load};
        }
        // Branch target
        else if (part.starts_with("target=") || part.starts_with("br=")) {
            auto eq = part.find('=');
            auto target_str = (eq != std::string_view::npos) ? part.substr(eq + 1) : std::string_view("0");
            auto target = parse_hex(target_str);
            if (target.has_error()) return Err(std::move(target.error()));

            bool is_taken = true;
            if (idx + 1 < parts.size()) {
                ++idx;
                auto taken_str = parts[idx];
                // Convert to lowercase
                std::string lower(taken_str);
                std::transform(lower.begin(), lower.end(), lower.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                is_taken = (lower == "taken");
            }

            instr.branch_info = BranchInfo{
                opcode_type == OpcodeType::BranchCond,
                target.value(),
                is_taken
            };
        }

        ++idx;
    }

    return Ok(std::optional<Instruction>{std::move(instr)});
}

// =====================================================================
// TextTraceParser::parse_disassembly
// =====================================================================

Result<Instruction> TextTraceParser::parse_disassembly(uint64_t pc, std::string_view disasm) {
    // Tokenize
    std::vector<std::string_view> parts;
    {
        std::string_view remaining = disasm;
        while (!remaining.empty()) {
            while (!remaining.empty() && std::isspace(static_cast<unsigned char>(remaining.front()))) {
                remaining.remove_prefix(1);
            }
            if (remaining.empty()) break;
            auto end = remaining.find_first_of(" \t\r\n");
            if (end == std::string_view::npos) end = remaining.size();
            parts.push_back(remaining.substr(0, end));
            remaining.remove_prefix(end);
        }
    }

    if (parts.empty()) {
        return Err(EmulatorError::trace_parse(
            "Empty disassembly at PC 0x" +
            ([](uint64_t v) -> std::string {
                char buf[20];
                std::snprintf(buf, sizeof(buf), "%llx", static_cast<unsigned long long>(v));
                return buf;
            })(pc)));
    }

    // Convert mnemonic to uppercase
    std::string mnemonic(parts[0]);
    std::transform(mnemonic.begin(), mnemonic.end(), mnemonic.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    auto opcode_type = mnemonic_to_opcode(mnemonic);

    Instruction instr(InstructionId(current_id_), pc, 0, opcode_type);
    instr.disasm = std::string(disasm);
    ++current_id_;

    // Parse operands
    if (parts.size() > 1) {
        // Join remaining parts and split by comma
        std::string operands_str;
        for (std::size_t i = 1; i < parts.size(); ++i) {
            if (i > 1) operands_str += ' ';
            operands_str += std::string(parts[i]);
        }

        // Split by comma
        std::vector<std::string_view> operands;
        std::string_view sv = operands_str;
        while (!sv.empty()) {
            auto comma = sv.find(',');
            auto token = (comma != std::string_view::npos) ? sv.substr(0, comma) : sv;
            // Trim
            while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front()))) {
                token.remove_prefix(1);
            }
            while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back()))) {
                token.remove_suffix(1);
            }
            if (!token.empty()) operands.push_back(token);
            if (comma != std::string_view::npos) {
                sv.remove_prefix(comma + 1);
            } else {
                break;
            }
        }

        for (std::size_t i = 0; i < operands.size(); ++i) {
            if (auto reg = parse_reg(operands[i])) {
                if (i == 0 && !is_memory_op(opcode_type)) {
                    instr.dst_regs.push_back(*reg);
                } else {
                    if (!instr.src_regs.contains(*reg)) {
                        instr.src_regs.push_back(*reg);
                    }
                }
            }
        }
    }

    // For load/store, check for memory operand pattern
    if (is_memory_op(opcode_type) && parts.size() > 2) {
        std::string rest;
        for (std::size_t i = 1; i < parts.size(); ++i) {
            if (i > 1) rest += ' ';
            rest += std::string(parts[i]);
        }
        if (extract_memory_operand(rest).has_value()) {
            instr.mem_access = MemAccess{
                0, // Will be filled by trace
                8,
                opcode_type == OpcodeType::Load || opcode_type == OpcodeType::LoadPair
            };
        }
    }

    return Ok(std::move(instr));
}

// =====================================================================
// TextTraceParser::extract_memory_operand
// =====================================================================

std::optional<std::string> TextTraceParser::extract_memory_operand(std::string_view s) {
    auto start = s.find('[');
    auto end = s.find(']');
    if (start != std::string_view::npos && end != std::string_view::npos && end > start) {
        return std::string(s.substr(start, end - start + 1));
    }
    return std::nullopt;
}

// =====================================================================
// TextTraceParser::parse_hex
// =====================================================================

Result<uint64_t> TextTraceParser::parse_hex(std::string_view s) {
    // Trim
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);

    // Strip 0x / 0X prefix
    if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s.remove_prefix(2);
    }

    uint64_t value = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value, 16);
    if (ec != std::errc{} || ptr != s.data() + s.size()) {
        return Err(EmulatorError::trace_parse(
            "Invalid hex value: " + std::string(s)));
    }
    return Ok(value);
}

// =====================================================================
// TextTraceParser::parse_opcode_type
// =====================================================================

Result<OpcodeType> TextTraceParser::parse_opcode_type(std::string_view s) {
    // Convert to uppercase
    std::string upper(s);
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    if (upper == "ADD" || upper == "ADDS" || upper == "ADC") return Ok(OpcodeType::Add);
    if (upper == "SUB" || upper == "SUBS" || upper == "SBC") return Ok(OpcodeType::Sub);
    if (upper == "MUL" || upper == "SMULL" || upper == "UMULL") return Ok(OpcodeType::Mul);
    if (upper == "DIV" || upper == "SDIV" || upper == "UDIV") return Ok(OpcodeType::Div);
    if (upper == "AND" || upper == "ANDS") return Ok(OpcodeType::And);
    if (upper == "ORR" || upper == "OR") return Ok(OpcodeType::Orr);
    if (upper == "EOR" || upper == "XOR") return Ok(OpcodeType::Eor);
    if (upper == "LSL") return Ok(OpcodeType::Lsl);
    if (upper == "LSR") return Ok(OpcodeType::Lsr);
    if (upper == "ASR") return Ok(OpcodeType::Asr);
    if (upper == "LDR" || upper == "LDUR" || upper == "LDP" || upper == "LDXR") return Ok(OpcodeType::Load);
    if (upper == "STR" || upper == "STUR" || upper == "STP" || upper == "STXR") return Ok(OpcodeType::Store);
    if (upper == "LDPSW" || upper == "LDRSW") return Ok(OpcodeType::Load);
    if (upper == "B") return Ok(OpcodeType::Branch);
    if (upper == "BL" || upper == "BR" || upper == "BLR" || upper == "RET") return Ok(OpcodeType::Branch);
    // Conditional branches
    if (upper == "B.EQ" || upper == "B.NE" || upper == "B.LT" || upper == "B.GT" ||
        upper == "B.LE" || upper == "B.GE" || upper == "B.HI" || upper == "B.LS" ||
        upper == "B.CC" || upper == "B.CS" || upper == "B.PL" || upper == "B.MI") {
        return Ok(OpcodeType::BranchCond);
    }
    if (upper == "CBZ" || upper == "CBNZ" || upper == "TBZ" || upper == "TBNZ") {
        return Ok(OpcodeType::BranchCond);
    }
    if (upper == "MSR") return Ok(OpcodeType::Msr);
    if (upper == "MRS") return Ok(OpcodeType::Mrs);
    if (upper == "SYS" || upper == "SYSL") return Ok(OpcodeType::Sys);
    if (upper == "NOP" || upper == "YIELD" || upper == "WFE" || upper == "WFI" || upper == "SEV") {
        return Ok(OpcodeType::Nop);
    }
    if (upper == "FADD") return Ok(OpcodeType::Fadd);
    if (upper == "FSUB") return Ok(OpcodeType::Fsub);
    if (upper == "FMUL") return Ok(OpcodeType::Fmul);
    if (upper == "FDIV") return Ok(OpcodeType::Fdiv);

    return Ok(OpcodeType::Other);
}

// =====================================================================
// TextTraceParser::mnemonic_to_opcode
// =====================================================================

OpcodeType TextTraceParser::mnemonic_to_opcode(std::string_view mnemonic) {
    auto result = parse_opcode_type(mnemonic);
    if (result.ok()) return result.value();
    return OpcodeType::Other;
}

// =====================================================================
// TextTraceParser::parse_reg
// =====================================================================

std::optional<Reg> TextTraceParser::parse_reg(std::string_view s) {
    // Trim
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);

    if (s.empty()) return std::nullopt;

    // Convert to uppercase
    std::string upper(s);
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    // Handle Xn, Wn, Rn format
    if (upper.size() < 2) return std::nullopt;
    char prefix = upper[0];
    if (prefix != 'X' && prefix != 'W' && prefix != 'R') return std::nullopt;

    auto num_str = upper.substr(1);
    uint8_t num = 0;
    auto [ptr, ec] = std::from_chars(num_str.data(), num_str.data() + num_str.size(), num);
    if (ec != std::errc{} || ptr != num_str.data() + num_str.size()) return std::nullopt;

    if (num <= 31) return Reg(num);
    return std::nullopt;
}

} // namespace arm_cpu
