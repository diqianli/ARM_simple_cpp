#pragma once

/// @file kanata_log_exporter.hpp
/// @brief Export pipeline visualization data in Kanata log format (.knata).
///
/// The Kanata format is a tab-separated text protocol compatible with the
/// upstream Konata visualization tool. Commands include:
///   Kanata\t0004          — format header
///   C=\t<cycle>           — set initial cycle
///   C\t<delta>            — advance cycle by delta
///   I\t<id>\t<gid>\t<tid> — introduce instruction
///   L\t<id>\t<type>\t<text> — label (disassembly)
///   S\t<id>\t<lane>\t<stage> — stage start
///   E\t<id>\t<lane>\t<stage> — stage end
///   R\t<id>\t<rid>\t<type>   — retire
///   W\t<consumer>\t<producer>\t<type> — wake-up dependency

#include "arm_cpu/visualization/konata_format.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace arm_cpu {

class KanataLogExporter {
public:
    /// Convert KonataOp list to Kanata log format string.
    static std::string export_to_string(
        const std::vector<KonataOp>& ops,
        uint64_t total_cycles,
        uint64_t total_instructions);

    /// Write Kanata log format to file.
    static bool export_to_file(
        const std::string& path,
        const std::vector<KonataOp>& ops,
        uint64_t total_cycles,
        uint64_t total_instructions);
};

} // namespace arm_cpu
