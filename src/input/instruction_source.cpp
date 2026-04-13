/// @file instruction_source.cpp
/// @brief Factory function for creating instruction sources.

#include "arm_cpu/input/instruction_source.hpp"
#include "arm_cpu/input/text_trace.hpp"
#include "arm_cpu/input/binary_trace.hpp"
#include "arm_cpu/input/champsim_trace.hpp"

namespace arm_cpu {

std::unique_ptr<InstructionSource> create_source(const TraceInputConfig& config) {
    switch (config.format) {
        case TraceFormat::Text:
            return std::make_unique<TextTraceParser>(TextTraceParser::from_file(config.file_path).value());
        case TraceFormat::Binary:
            return std::make_unique<BinaryTraceParser>(BinaryTraceParser::from_file(config.file_path).value());
        case TraceFormat::ChampSim:
            return std::make_unique<ChampSimTraceParser>(ChampSimTraceParser::from_file(config.file_path).value());
        case TraceFormat::Json:
        case TraceFormat::ChampSimXz:
            // Not yet implemented; fall through to text as default
            return std::make_unique<TextTraceParser>(TextTraceParser::from_file(config.file_path).value());
    }
    return std::make_unique<TextTraceParser>(TextTraceParser::from_file(config.file_path).value());
}

} // namespace arm_cpu
