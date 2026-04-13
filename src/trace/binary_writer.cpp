/// @file binary_writer.cpp
/// @brief Binary trace writer implementation.

#include "arm_cpu/trace/binary_writer.hpp"

#include <algorithm>
#include <cstring>

namespace arm_cpu {

namespace {

struct StringTable {
    std::unordered_map<std::string, uint32_t> index;
    std::vector<std::string> strings;

    uint32_t intern(const std::string& s) {
        auto it = index.find(s);
        if (it != index.end()) return it->second;
        auto idx = static_cast<uint32_t>(strings.size());
        index[s] = idx;
        strings.push_back(s);
        return idx;
    }

    uint32_t serialized_size() const {
        uint32_t size = 0;
        for (const auto& s : strings) {
            size += 2 + static_cast<uint32_t>(s.size());
        }
        return size;
    }

    Result<void> serialize(FILE* file) {
        for (const auto& s : strings) {
            uint16_t len = static_cast<uint16_t>(s.size());
            if (std::fwrite(&len, 2, 1, file) != 1)
                return EmulatorError::internal("Failed to write string table");
            if (!s.empty() && std::fwrite(s.data(), 1, s.size(), file) != s.size())
                return EmulatorError::internal("Failed to write string table");
        }
        return {};
    }
};

} // anonymous namespace

BinaryTraceWriter::BinaryTraceWriter(FILE* file, bool build_index)
    : file_(file)
    , build_index_(build_index)
    , current_offset_(FileHeader::SIZE)
{}

BinaryTraceWriter::~BinaryTraceWriter() {
    close();
}

BinaryTraceWriter::BinaryTraceWriter(BinaryTraceWriter&& other) noexcept
    : file_(other.file_)
    , string_table_index_(std::move(other.string_table_index_))
    , string_table_strings_(std::move(other.string_table_strings_))
    , last_pc_(other.last_pc_)
    , instr_count_(other.instr_count_)
    , current_offset_(other.current_offset_)
    , build_index_(other.build_index_)
    , index_entries_(std::move(other.index_entries_))
    , finished_(other.finished_)
{
    other.file_ = nullptr;
    other.finished_ = true;
}

BinaryTraceWriter& BinaryTraceWriter::operator=(BinaryTraceWriter&& other) noexcept {
    if (this != &other) {
        close();
        file_ = other.file_;
        string_table_index_ = std::move(other.string_table_index_);
        string_table_strings_ = std::move(other.string_table_strings_);
        last_pc_ = other.last_pc_;
        instr_count_ = other.instr_count_;
        current_offset_ = other.current_offset_;
        build_index_ = other.build_index_;
        index_entries_ = std::move(other.index_entries_);
        finished_ = other.finished_;
        other.file_ = nullptr;
        other.finished_ = true;
    }
    return *this;
}

Result<BinaryTraceWriter> BinaryTraceWriter::create(const std::string& path) {
    FILE* file = fopen(path.c_str(), "wb");
    if (!file) {
        return EmulatorError::internal("Cannot create file: " + path);
    }

    // Write placeholder header
    FileHeader header = FileHeader::default_header();
    std::array<uint8_t, FileHeader::SIZE> header_bytes;
    std::memcpy(header_bytes.data(), &header, FileHeader::SIZE);
    if (std::fwrite(header_bytes.data(), 1, FileHeader::SIZE, file) != FileHeader::SIZE) {
        fclose(file);
        return EmulatorError::internal("Failed to write header");
    }

    return BinaryTraceWriter(file, false);
}

Result<BinaryTraceWriter> BinaryTraceWriter::create_with_index(const std::string& path) {
    FILE* file = fopen(path.c_str(), "wb");
    if (!file) {
        return EmulatorError::internal("Cannot create file: " + path);
    }

    FileHeader header = FileHeader::default_header();
    std::array<uint8_t, FileHeader::SIZE> header_bytes;
    std::memcpy(header_bytes.data(), &header, FileHeader::SIZE);
    if (std::fwrite(header_bytes.data(), 1, FileHeader::SIZE, file) != FileHeader::SIZE) {
        fclose(file);
        return EmulatorError::internal("Failed to write header");
    }

    return BinaryTraceWriter(file, true);
}

Result<void> BinaryTraceWriter::write_instruction(const Instruction& instr) {
    if (build_index_) {
        index_entries_.push_back(IndexEntry{instr.id.value, current_offset_});
    }

    uint16_t pc_delta = 0;
    if (instr.pc >= last_pc_) {
        pc_delta = static_cast<uint16_t>(instr.pc - last_pc_);
    }

    uint8_t flags = 0;
    if (instr.mem_access.has_value()) flags |= InstrFlags::HAS_MEM;
    if (instr.branch_info.has_value()) flags |= InstrFlags::HAS_BRANCH;
    if (instr.disasm.has_value()) flags |= InstrFlags::HAS_DISASM;
    if (pc_delta == 0 && instr.pc != last_pc_) flags |= InstrFlags::EXTENDED_PC;

    uint8_t operand_count = static_cast<uint8_t>(
        instr.src_regs.size() + instr.dst_regs.size()
        + instr.src_vregs.size() + instr.dst_vregs.size());

    InstrHeader hdr;
    hdr.id = static_cast<uint32_t>(instr.id.value);
    hdr.opcode = encode_opcode(instr.opcode_type);
    hdr.flags = flags;
    hdr.pc_delta = pc_delta;
    hdr.operand_count = operand_count;

    // Write header
    std::array<uint8_t, InstrHeader::SIZE> header_bytes;
    std::memcpy(header_bytes.data(), &hdr, InstrHeader::SIZE);
    if (std::fwrite(header_bytes.data(), 1, InstrHeader::SIZE, file_) != InstrHeader::SIZE)
        return EmulatorError::internal("Failed to write instruction header");
    current_offset_ += InstrHeader::SIZE;

    // Write raw opcode
    std::array<uint8_t, 4> opcode_bytes;
    opcode_bytes[0] = static_cast<uint8_t>(instr.raw_opcode & 0xFF);
    opcode_bytes[1] = static_cast<uint8_t>((instr.raw_opcode >> 8) & 0xFF);
    opcode_bytes[2] = static_cast<uint8_t>((instr.raw_opcode >> 16) & 0xFF);
    opcode_bytes[3] = static_cast<uint8_t>((instr.raw_opcode >> 24) & 0xFF);
    if (std::fwrite(opcode_bytes.data(), 1, 4, file_) != 4)
        return EmulatorError::internal("Failed to write opcode");
    current_offset_ += 4;

    // Extended PC
    if (flags & InstrFlags::EXTENDED_PC) {
        std::array<uint8_t, 8> pc_bytes;
        for (int i = 0; i < 8; ++i) pc_bytes[i] = static_cast<uint8_t>((instr.pc >> (i * 8)) & 0xFF);
        if (std::fwrite(pc_bytes.data(), 1, 8, file_) != 8)
            return EmulatorError::internal("Failed to write PC");
        current_offset_ += 8;
    }

    // Operands
    TRY(write_operands(instr));

    // Memory access
    if (instr.mem_access.has_value()) {
        TRY(write_mem_access(*instr.mem_access));
    }

    // Branch info
    if (instr.branch_info.has_value()) {
        TRY(write_branch_info(*instr.branch_info, last_pc_));
    }

    // Disassembly string index
    if (instr.disasm.has_value()) {
        uint32_t idx = 0;
        {
            // Use local string table for simplicity
            auto it = string_table_index_.find(*instr.disasm);
            if (it != string_table_index_.end()) {
                idx = it->second;
            } else {
                idx = static_cast<uint32_t>(string_table_strings_.size());
                string_table_index_[*instr.disasm] = idx;
                string_table_strings_.push_back(*instr.disasm);
            }
        }
        std::array<uint8_t, 4> idx_bytes;
        idx_bytes[0] = static_cast<uint8_t>(idx & 0xFF);
        idx_bytes[1] = static_cast<uint8_t>((idx >> 8) & 0xFF);
        idx_bytes[2] = static_cast<uint8_t>((idx >> 16) & 0xFF);
        idx_bytes[3] = static_cast<uint8_t>((idx >> 24) & 0xFF);
        if (std::fwrite(idx_bytes.data(), 1, 4, file_) != 4)
            return EmulatorError::internal("Failed to write disasm index");
        current_offset_ += 4;
    }

    last_pc_ = instr.pc;
    ++instr_count_;

    return {};
}

Result<void> BinaryTraceWriter::write_operands(const Instruction& instr) {
    // Source registers
    for (const auto& reg : instr.src_regs) {
        uint8_t type_byte = static_cast<uint8_t>(OperandType::Reg);
        uint8_t val = reg.value;
        if (std::fwrite(&type_byte, 1, 1, file_) != 1)
            return EmulatorError::internal("Failed to write operand");
        if (std::fwrite(&val, 1, 1, file_) != 1)
            return EmulatorError::internal("Failed to write operand");
        current_offset_ += 2;
    }

    // Destination registers
    for (const auto& reg : instr.dst_regs) {
        uint8_t type_byte = static_cast<uint8_t>(OperandType::Reg) | 0x80;
        uint8_t val = reg.value;
        if (std::fwrite(&type_byte, 1, 1, file_) != 1)
            return EmulatorError::internal("Failed to write operand");
        if (std::fwrite(&val, 1, 1, file_) != 1)
            return EmulatorError::internal("Failed to write operand");
        current_offset_ += 2;
    }

    // Vector registers (simplified)
    for (const auto& vreg : instr.src_vregs) {
        uint8_t type_byte = static_cast<uint8_t>(OperandType::VReg);
        uint8_t val = vreg.value;
        if (std::fwrite(&type_byte, 1, 1, file_) != 1)
            return EmulatorError::internal("Failed to write operand");
        if (std::fwrite(&val, 1, 1, file_) != 1)
            return EmulatorError::internal("Failed to write operand");
        current_offset_ += 2;
    }

    for (const auto& vreg : instr.dst_vregs) {
        uint8_t type_byte = static_cast<uint8_t>(OperandType::VReg) | 0x80;
        uint8_t val = vreg.value;
        if (std::fwrite(&type_byte, 1, 1, file_) != 1)
            return EmulatorError::internal("Failed to write operand");
        if (std::fwrite(&val, 1, 1, file_) != 1)
            return EmulatorError::internal("Failed to write operand");
        current_offset_ += 2;
    }

    return {};
}

Result<void> BinaryTraceWriter::write_mem_access(const MemAccess& mem) {
    uint8_t flags_byte = mem.is_load ? 1 : 0;
    if (std::fwrite(&flags_byte, 1, 1, file_) != 1)
        return EmulatorError::internal("Failed to write mem flags");
    if (std::fwrite(&mem.size, 1, 1, file_) != 1)
        return EmulatorError::internal("Failed to write mem size");

    TRY(write_varint(mem.addr));
    current_offset_ += 2;
    return {};
}

Result<void> BinaryTraceWriter::write_branch_info(const BranchInfo& branch, uint64_t last_pc) {
    uint8_t flags_byte = 0;
    if (branch.is_conditional) flags_byte |= 1;
    if (branch.is_taken) flags_byte |= 2;
    if (std::fwrite(&flags_byte, 1, 1, file_) != 1)
        return EmulatorError::internal("Failed to write branch flags");

    int64_t target_delta = static_cast<int64_t>(branch.target) - static_cast<int64_t>(last_pc);
    TRY(write_signed_varint(target_delta));
    current_offset_ += 1;
    return {};
}

Result<void> BinaryTraceWriter::write_varint(uint64_t value) {
    do {
        uint8_t byte = static_cast<uint8_t>(value & 0x7F);
        value >>= 7;
        if (value != 0) byte |= 0x80;
        if (std::fwrite(&byte, 1, 1, file_) != 1)
            return EmulatorError::internal("Failed to write varint");
        current_offset_ += 1;
    } while (value != 0);
    return {};
}

Result<void> BinaryTraceWriter::write_signed_varint(int64_t value) {
    // Zigzag encoding
    uint64_t zigzag = (static_cast<uint64_t>(value) << 1) ^ static_cast<uint64_t>(value >> 63);
    return write_varint(zigzag);
}

Result<void> BinaryTraceWriter::finish() {
    if (finished_ || !file_) return {};

    // Flush
    fflush(file_);

    // Calculate positions
    uint64_t string_table_offset = current_offset_;
    StringTable st;
    st.index = std::move(string_table_index_);
    st.strings = std::move(string_table_strings_);
    uint32_t string_table_size = st.serialized_size();

    // Write string table
    TRY(st.serialize(file_));

    // Write index table if enabled
    uint64_t index_table_offset = 0;
    uint32_t index_table_size = 0;
    if (build_index_) {
        index_table_offset = current_offset_ + string_table_size;
        index_table_size = static_cast<uint32_t>(index_entries_.size() * IndexEntry::SIZE);

        for (const auto& entry : index_entries_) {
            std::array<uint8_t, 8> id_bytes;
            std::array<uint8_t, 8> off_bytes;
            for (int i = 0; i < 8; ++i) {
                id_bytes[i] = static_cast<uint8_t>((entry.instr_id >> (i * 8)) & 0xFF);
                off_bytes[i] = static_cast<uint8_t>((entry.offset >> (i * 8)) & 0xFF);
            }
            if (std::fwrite(id_bytes.data(), 1, 8, file_) != 8)
                return EmulatorError::internal("Failed to write index");
            if (std::fwrite(off_bytes.data(), 1, 8, file_) != 8)
                return EmulatorError::internal("Failed to write index");
        }
    }

    // Seek back and write final header
    FileHeader header;
    header.magic = MAGIC;
    header.version = VERSION;
    header.flags = build_index_ ? FileFlags::HAS_INDEX : FileFlags::NONE;
    header.instr_count = instr_count_;
    header.string_table_offset = string_table_offset;
    header.string_table_size = string_table_size;
    header.index_table_offset = index_table_offset;
    header.index_table_size = index_table_size;

    fseek(file_, 0, SEEK_SET);
    std::array<uint8_t, FileHeader::SIZE> header_bytes;
    std::memcpy(header_bytes.data(), &header, FileHeader::SIZE);
    if (std::fwrite(header_bytes.data(), 1, FileHeader::SIZE, file_) != FileHeader::SIZE)
        return EmulatorError::internal("Failed to write final header");

    fflush(file_);
    finished_ = true;
    return {};
}

void BinaryTraceWriter::close() {
    if (file_ && !finished_) {
        finish();
    }
    if (file_) {
        fclose(file_);
        file_ = nullptr;
    }
}

} // namespace arm_cpu
