#pragma once

/// @file output_sink.hpp
/// @brief Abstract output sink trait and concrete implementations.
///
/// Analogous to Rust's `output::sink::{OutputSink, FileSink, MemorySink,
/// StdoutSink, NullSink}`.

#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace arm_cpu {

/// Abstract output sink that consumes lines of simulation output.
class OutputSink {
public:
    virtual ~OutputSink() = default;

    /// Write a line of output.
    virtual bool write_line(const std::string& line) = 0;

    /// Flush any buffered output.
    virtual bool flush() = 0;

    /// Get the number of lines written.
    virtual uint64_t lines_written() const noexcept = 0;

    /// Check if the sink is still valid for writing.
    virtual bool is_valid() const = 0;
};

/// Output sink that writes to a file.
class FileOutputSink final : public OutputSink {
public:
    /// Create a new file sink (truncates existing file).
    static std::unique_ptr<FileOutputSink> create(const std::string& path) {
        auto stream = std::make_unique<std::ofstream>(path);
        if (!stream->is_open()) return nullptr;
        return std::unique_ptr<FileOutputSink>(new FileOutputSink(std::move(stream)));
    }

    /// Create with options (append mode).
    static std::unique_ptr<FileOutputSink> create(const std::string& path, bool append) {
        auto stream = std::make_unique<std::ofstream>();
        if (append) {
            stream->open(path, std::ios::app);
        } else {
            stream->open(path, std::ios::out | std::ios::trunc);
        }
        if (!stream->is_open()) return nullptr;
        return std::unique_ptr<FileOutputSink>(new FileOutputSink(std::move(stream)));
    }

    bool write_line(const std::string& line) override {
        *stream_ << line << '\n';
        ++lines_;
        return static_cast<bool>(*stream_);
    }

    bool flush() override {
        stream_->flush();
        return static_cast<bool>(*stream_);
    }

    uint64_t lines_written() const noexcept override { return lines_; }

    bool is_valid() const override { return static_cast<bool>(*stream_); }

private:
    explicit FileOutputSink(std::unique_ptr<std::ofstream> stream)
        : stream_(std::move(stream)), lines_(0) {}

    std::unique_ptr<std::ofstream> stream_;
    uint64_t lines_;
};

/// Output sink that buffers to memory.
class MemoryOutputSink final : public OutputSink {
public:
    MemoryOutputSink() = default;

    /// Create with a maximum capacity (0 = unlimited).
    explicit MemoryOutputSink(std::size_t max_capacity)
        : max_capacity_(max_capacity) {}

    bool write_line(const std::string& line) override {
        if (max_capacity_ == 0 || buffer_.size() < max_capacity_) {
            buffer_.push_back(line);
            return true;
        }
        return false;
    }

    bool flush() override { return true; }

    uint64_t lines_written() const noexcept override {
        return static_cast<uint64_t>(buffer_.size());
    }

    bool is_valid() const override {
        return max_capacity_ == 0 || buffer_.size() < max_capacity_;
    }

    /// Get all buffered content joined by newlines.
    std::string to_string() const {
        std::ostringstream oss;
        for (std::size_t i = 0; i < buffer_.size(); ++i) {
            if (i > 0) oss << '\n';
            oss << buffer_[i];
        }
        return oss.str();
    }

    /// Get the buffered lines.
    const std::vector<std::string>& lines() const noexcept { return buffer_; }

    /// Clear the buffer.
    void clear() { buffer_.clear(); }

    /// Write the buffer to a file.
    bool write_to_file(const std::string& path) const {
        std::ofstream file(path);
        if (!file.is_open()) return false;
        for (const auto& line : buffer_) {
            file << line << '\n';
        }
        return static_cast<bool>(file);
    }

private:
    std::vector<std::string> buffer_;
    std::size_t max_capacity_ = 0;
};

/// Output sink that writes to stdout.
class StdoutOutputSink final : public OutputSink {
public:
    StdoutOutputSink() = default;

    bool write_line(const std::string& line) override {
        std::printf("%s\n", line.c_str());
        ++lines_;
        return true;
    }

    bool flush() override {
        std::fflush(stdout);
        return true;
    }

    uint64_t lines_written() const noexcept override { return lines_; }

    bool is_valid() const override { return true; }

private:
    uint64_t lines_ = 0;
};

/// Null sink that discards all output.
class NullOutputSink final : public OutputSink {
public:
    NullOutputSink() = default;

    bool write_line(const std::string& /*line*/) override {
        ++lines_;
        return true;
    }

    bool flush() override { return true; }

    uint64_t lines_written() const noexcept override { return lines_; }

    bool is_valid() const override { return true; }

private:
    uint64_t lines_ = 0;
};

} // namespace arm_cpu
