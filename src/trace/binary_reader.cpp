/// @file binary_reader.cpp
/// @brief Binary trace reader implementation.

#include "arm_cpu/trace/binary_reader.hpp"

#include <algorithm>
#include <cstring>

namespace arm_cpu {

Result<BinaryTraceReader> BinaryTraceReader::open(const std::string& path) {
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) {
        return EmulatorError::internal("Cannot open file: " + path);
    }

    // Read header
    std::array<uint8_t, FileHeader::SIZE> header_bytes;
    if (std::fread(header_bytes.data(), 1, FileHeader::SIZE, file) != FileHeader::SIZE) {
        fclose(file);
        return EmulatorError::internal("Failed to read trace file header");
    }

    FileHeader header;
    std::memcpy(&header, header_bytes.data(), FileHeader::SIZE);

    if (!header.validate()) {
        fclose(file);
        return EmulatorError::internal("Invalid trace file header");
    }

    // Read string table
    std::vector<std::string> strings;
    if (header.string_table_size > 0) {
        if (fseek(file, static_cast<long>(header.string_table_offset), SEEK_SET) != 0) {
            fclose(file);
            return EmulatorError::internal("Failed to seek to string table");
        }

        uint32_t remaining = header.string_table_size;
        while (remaining >= 2) {
            std::array<uint8_t, 2> len_bytes;
            if (std::fread(len_bytes.data(), 1, 2, file) != 2) break;
            uint16_t len = static_cast<uint16_t>(len_bytes[0]) | (static_cast<uint16_t>(len_bytes[1]) << 8);
            remaining -= 2;

            std::string s(len, '\0');
            if (len > 0 && std::fread(s.data(), 1, len, file) != len) break;
            remaining -= len;
            strings.push_back(std::move(s));
        }
    }

    // Read index if present
    std::vector<IndexEntry> index;
    if (header.has_index() && header.index_table_size > 0) {
        if (fseek(file, static_cast<long>(header.index_table_offset), SEEK_SET) != 0) {
            fclose(file);
            return EmulatorError::internal("Failed to seek to index table");
        }

        std::size_t count = header.index_table_size / IndexEntry::SIZE;
        index.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            std::array<uint8_t, IndexEntry::SIZE> entry_bytes;
            if (std::fread(entry_bytes.data(), 1, IndexEntry::SIZE, file) != IndexEntry::SIZE) break;
            IndexEntry entry;
            std::memcpy(&entry, entry_bytes.data(), IndexEntry::SIZE);
            index.push_back(entry);
        }
    }

    // Seek to instruction stream start
    fseek(file, static_cast<long>(FileHeader::SIZE), SEEK_SET);

    return BinaryTraceReader(file, header, std::move(strings), std::move(index));
}

BinaryTraceReader::BinaryTraceReader(FILE* file, FileHeader header,
                                     std::vector<std::string> strings,
                                     std::vector<IndexEntry> index)
    : file_(file)
    , header_(std::move(header))
    , string_table_(std::move(strings))
    , index_(std::move(index))
    , current_position_(FileHeader::SIZE)
{
}

Result<uint8_t> BinaryTraceReader::read_byte() {
    uint8_t b;
    if (std::fread(&b, 1, 1, file_) != 1) {
        return EmulatorError::internal("Unexpected end of file");
    }
    ++current_position_;
    return b;
}

Result<uint16_t> BinaryTraceReader::read_u16_le() {
    std::array<uint8_t, 2> bytes;
    if (std::fread(bytes.data(), 1, 2, file_) != 2) {
        return EmulatorError::internal("Unexpected end of file");
    }
    current_position_ += 2;
    return static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8);
}

Result<uint32_t> BinaryTraceReader::read_u32_le() {
    std::array<uint8_t, 4> bytes;
    if (std::fread(bytes.data(), 1, 4, file_) != 4) {
        return EmulatorError::internal("Unexpected end of file");
    }
    current_position_ += 4;
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        v |= static_cast<uint32_t>(bytes[i]) << (i * 8);
    }
    return v;
}

Result<uint64_t> BinaryTraceReader::read_u64_le() {
    std::array<uint8_t, 8> bytes;
    if (std::fread(bytes.data(), 1, 8, file_) != 8) {
        return EmulatorError::internal("Unexpected end of file");
    }
    current_position_ += 8;
    uint64_t val = 0;
    for (int i = 0; i < 8; ++i) {
        val |= static_cast<uint64_t>(bytes[i]) << (i * 8);
    }
    return val;
}

Result<uint64_t> BinaryTraceReader::read_varint() {
    uint64_t result = 0;
    int shift = 0;

    for (;;) {
        auto b = TRY(read_byte());
        result |= (static_cast<uint64_t>(b & 0x7F)) << shift;
        shift += 7;
        if ((b & 0x80) == 0) break;
        if (shift >= 64) {
            return EmulatorError::internal("Varint too large");
        }
    }
    return result;
}

Result<int64_t> BinaryTraceReader::read_signed_varint() {
    auto zigzag = TRY(read_varint());
    return (static_cast<int64_t>(zigzag >> 1))
         ^ (-(static_cast<int64_t>(zigzag & 1)));
}

bool BinaryTraceReader::seek_to_instruction(uint64_t instr_id) {
    if (!has_index()) return false;

    auto it = std::lower_bound(index_.begin(), index_.end(), instr_id,
        [](const IndexEntry& e, uint64_t id) { return e.instr_id < id; });

    if (it != index_.end() && it->instr_id == instr_id) {
        fseek(file_, static_cast<long>(it->offset), SEEK_SET);
        current_position_ = it->offset;
        instructions_read_ = instr_id;
        return true;
    }
    return false;
}

Result<std::optional<Instruction>> BinaryTraceReader::read_next() {
    if (instructions_read_ >= header_.instr_count) {
        return Ok(std::optional<Instruction>{});
    }

    if (current_position_ >= header_.string_table_offset && header_.string_table_offset > 0) {
        return Ok(std::optional<Instruction>{});
    }

    // Read instruction header
    std::array<uint8_t, InstrHeader::SIZE> header_bytes;
    if (std::fread(header_bytes.data(), 1, InstrHeader::SIZE, file_) != InstrHeader::SIZE) {
        return Ok(std::optional<Instruction>{}); // EOF
    }
    current_position_ += InstrHeader::SIZE;

    InstrHeader hdr;
    std::memcpy(&hdr, header_bytes.data(), InstrHeader::SIZE);

    // Read raw opcode
    auto raw_opcode = TRY(read_u32_le());

    // Determine PC
    uint64_t pc;
    if (hdr.flags & InstrFlags::EXTENDED_PC) {
        auto pc_val = TRY(read_u64_le());
        pc = pc_val;
    } else {
        pc = last_pc_ + hdr.pc_delta;
    }

    Instruction instr(InstructionId{static_cast<uint64_t>(hdr.id)}, pc, raw_opcode, decode_opcode(hdr.opcode));

    // Read operands (simplified)
    for (uint8_t i = 0; i < hdr.operand_count; ++i) {
        auto type_byte = TRY(read_byte());
        TRY(read_byte()); // value byte
        current_position_ += 2;

        bool is_dst = (type_byte & 0x80) != 0;
        uint8_t reg_num = 0;

        // Re-read value
        fseek(file_, static_cast<long>(current_position_ - 1), SEEK_SET);
        reg_num = TRY(read_byte());

        if (is_dst) {
            instr.with_dst_reg(Reg{reg_num});
        } else {
            instr.with_src_reg(Reg{reg_num});
        }
    }

    // Read memory access
    if (hdr.flags & InstrFlags::HAS_MEM) {
        auto flags_byte = TRY(read_byte());
        bool is_load = (flags_byte & 1) != 0;
        auto size_byte = TRY(read_byte());
        auto addr = TRY(read_varint());
        current_position_ += 2;

        instr.with_mem_access(addr, size_byte, is_load);
    }

    // Read branch info
    if (hdr.flags & InstrFlags::HAS_BRANCH) {
        auto flags_byte = TRY(read_byte());
        bool is_conditional = (flags_byte & 1) != 0;
        bool is_taken = (flags_byte & 2) != 0;
        auto target_delta = TRY(read_signed_varint());
        current_position_ += 1;

        uint64_t target = (static_cast<int64_t>(last_pc_) + target_delta);
        instr.with_branch(target, is_conditional, is_taken);
    }

    // Read disassembly
    if (hdr.flags & InstrFlags::HAS_DISASM) {
        auto idx = TRY(read_u32_le());
        if (idx < string_table_.size()) {
            instr.with_disasm(string_table_[idx]);
        }
    }

    last_pc_ = pc;
    ++instructions_read_;

    return Ok(std::optional<Instruction>{std::move(instr)});
}

Result<std::vector<Instruction>> BinaryTraceReader::read_range(uint64_t start, uint64_t end) {
    if (start >= end) {
        return Ok(std::vector<Instruction>{});
    }

    if (!seek_to_instruction(start)) {
        return EmulatorError::internal("Range reading requires index table");
    }

    std::vector<Instruction> instructions;
    uint64_t count = end - start;
    instructions.reserve(std::min(count, uint64_t(10000)));

    while (instructions.size() < count) {
        auto r = read_next();
        if (!r.ok()) break;
        auto& opt = r.value();
        if (!opt.has_value()) break;
        if (opt->id.value >= end) break;
        instructions.push_back(std::move(*opt));
    }

    return Ok(std::move(instructions));
}

std::vector<Instruction> BinaryTraceReader::stream_all() {
    std::vector<Instruction> instructions;
    while (true) {
        auto r = read_next();
        if (!r.ok()) break;
        if (!r.value().has_value()) break;
        instructions.push_back(std::move(*r.value()));
    }
    return instructions;
}

} // namespace arm_cpu
