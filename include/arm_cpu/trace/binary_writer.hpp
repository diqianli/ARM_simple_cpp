/// @file binary_writer.hpp
/// @brief Binary trace writer for efficient trace output.

#include "arm_cpu/trace/binary_format.hpp"
#include "arm_cpu/types.hpp"
#include "arm_cpu/error.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace arm_cpu {

// =====================================================================
// BinaryTraceWriter
// =====================================================================
class BinaryTraceWriter {
public:
    /// Create a new writer from a file path.
    static Result<BinaryTraceWriter> create(const std::string& path);

    /// Create a new writer with index building.
    static Result<BinaryTraceWriter> create_with_index(const std::string& path);

    /// Write a single instruction.
    Result<void> write_instruction(const Instruction& instr);

    /// Flush and finalize the trace file.
    Result<void> finish();

    /// Get current instruction count.
    uint64_t instr_count() const { return instr_count_; }

    /// Close the writer (calls finish if not already done).
    void close();

    ~BinaryTraceWriter();

    BinaryTraceWriter(BinaryTraceWriter&&) noexcept;
    BinaryTraceWriter& operator=(BinaryTraceWriter&&) noexcept;

    BinaryTraceWriter(const BinaryTraceWriter&) = delete;
    BinaryTraceWriter& operator=(const BinaryTraceWriter&) = delete;

private:
    BinaryTraceWriter(FILE* file, bool build_index);

    Result<void> write_operands(const Instruction& instr);
    Result<void> write_mem_access(const MemAccess& mem);
    Result<void> write_branch_info(const BranchInfo& branch, uint64_t last_pc);
    Result<void> write_varint(uint64_t value);
    Result<void> write_signed_varint(int64_t value);

    FILE* file_ = nullptr;
    std::unordered_map<std::string, uint32_t> string_table_index_;
    std::vector<std::string> string_table_strings_;
    uint64_t last_pc_ = 0;
    uint64_t instr_count_ = 0;
    uint64_t current_offset_ = 0;
    bool build_index_ = false;
    std::vector<IndexEntry> index_entries_;
    bool finished_ = false;
};

} // namespace arm_cpu
