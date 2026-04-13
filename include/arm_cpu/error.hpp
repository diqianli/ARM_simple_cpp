#pragma once

/// @file error.hpp
/// @brief Error types and Result wrapper for the ARM CPU emulator.

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace arm_cpu {

/// Error codes for emulator operations.
enum class EmulatorErrorCode : uint8_t {
    InvalidRegister,
    InvalidInstruction,
    TraceParseError,
    ConfigError,
    MemoryError,
    ChiError,
    InternalError,
};

/// Emulator error type, analogous to Rust's thiserror-based EmulatorError.
class EmulatorError : public std::runtime_error {
public:
    EmulatorError(EmulatorErrorCode code, std::string message)
        : std::runtime_error(format_message(code, message))
        , code_(code)
        , message_(std::move(message)) {}

    EmulatorErrorCode code() const noexcept { return code_; }
    const std::string& message() const noexcept { return message_; }

    // Factory methods matching Rust enum variants
    static EmulatorError invalid_register(uint8_t reg) {
        return {EmulatorErrorCode::InvalidRegister,
                "Invalid register: " + std::to_string(reg)};
    }

    static EmulatorError invalid_instruction(uint64_t pc, std::string reason) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "Invalid instruction at PC 0x%llx: ", static_cast<unsigned long long>(pc));
        return {EmulatorErrorCode::InvalidInstruction, std::string(buf) + reason};
    }

    static EmulatorError trace_parse(std::string reason) {
        return {EmulatorErrorCode::TraceParseError, std::move(reason)};
    }

    static EmulatorError config(std::string reason) {
        return {EmulatorErrorCode::ConfigError, std::move(reason)};
    }

    static EmulatorError memory(uint64_t addr, std::string reason) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "Memory access error at address 0x%llx: ", static_cast<unsigned long long>(addr));
        return {EmulatorErrorCode::MemoryError, std::string(buf) + reason};
    }

    static EmulatorError chi(std::string reason) {
        return {EmulatorErrorCode::ChiError, std::move(reason)};
    }

    static EmulatorError internal(std::string reason) {
        return {EmulatorErrorCode::InternalError, std::move(reason)};
    }

private:
    static std::string format_message(EmulatorErrorCode code, const std::string& msg) {
        const char* prefix = "Unknown";
        switch (code) {
            case EmulatorErrorCode::InvalidRegister:  prefix = "InvalidRegister"; break;
            case EmulatorErrorCode::InvalidInstruction: prefix = "InvalidInstruction"; break;
            case EmulatorErrorCode::TraceParseError:  prefix = "TraceParseError"; break;
            case EmulatorErrorCode::ConfigError:      prefix = "ConfigError"; break;
            case EmulatorErrorCode::MemoryError:      prefix = "MemoryError"; break;
            case EmulatorErrorCode::ChiError:         prefix = "ChiError"; break;
            case EmulatorErrorCode::InternalError:    prefix = "InternalError"; break;
        }
        return std::string(prefix) + ": " + msg;
    }

    EmulatorErrorCode code_;
    std::string message_;
};

/// Result<T> = std::variant<T, EmulatorError>, with helper methods.
/// C++20 does not have std::expected, so we use std::variant.
template<typename T>
class Result {
public:
    // Construct success
    Result(T value) : var_(std::in_place_index<0>, std::move(value)) {}
    Result(std::in_place_t, auto&&... args) : var_(std::in_place_index<0>, std::forward<decltype(args)>(args)...) {}

    // Construct error
    Result(EmulatorError err) : var_(std::in_place_index<1>, std::move(err)) {}

    bool ok() const noexcept { return var_.index() == 0; }
    bool has_error() const noexcept { return var_.index() == 1; }
    explicit operator bool() const noexcept { return ok(); }

    T& value() & { return std::get<0>(var_); }
    const T& value() const& { return std::get<0>(var_); }
    T&& value() && { return std::get<0>(std::move(var_)); }

    T& operator*() & { return value(); }
    const T& operator*() const& { return value(); }
    T&& operator*() && { return std::move(value()); }

    T* operator->() { return &value(); }
    const T* operator->() const { return &value(); }

    EmulatorError& error() & { return std::get<1>(var_); }
    const EmulatorError& error() const& { return std::get<1>(var_); }

    // Monadic operations
    template<typename F>
    auto map(F&& f) -> Result<decltype(f(std::declval<T&>()))> {
        using U = decltype(f(std::declval<T&>()));
        if (ok()) return Result<U>(f(value()));
        return Result<U>(error());
    }

    template<typename F>
    auto and_then(F&& f) -> decltype(f(std::declval<T&>())) {
        if (ok()) return f(value());
        return decltype(f(std::declval<T&>()))(error());
    }

private:
    std::variant<T, EmulatorError> var_;
};

/// Specialization for void
template<>
class Result<void> {
public:
    Result() : var_(std::monostate{}) {}
    Result(EmulatorError err) : var_(std::move(err)) {}

    bool ok() const noexcept { return std::holds_alternative<std::monostate>(var_); }
    bool has_error() const noexcept { return std::holds_alternative<EmulatorError>(var_); }
    explicit operator bool() const noexcept { return ok(); }

    void value() const {
        if (has_error()) [[unlikely]]
            throw error();
    }

    EmulatorError& error() & { return std::get<EmulatorError>(var_); }
    const EmulatorError& error() const& { return std::get<EmulatorError>(var_); }

private:
    std::variant<std::monostate, EmulatorError> var_;
};

/// Helper to construct a successful Result.
template<typename T>
inline Result<T> Ok(T value) {
    return Result<T>(std::move(value));
}

/// Helper to construct a successful void Result.
inline Result<void> Ok() {
    return Result<void>();
}

/// Wrapper that implicitly converts to any Result<T>.
/// Usage: return Err(EmulatorError::trace_parse("msg"));
/// The compiler will convert via Result<T>(EmulatorError) constructor.
struct ErrWrapper {
    EmulatorError err;
    template<typename T>
    operator Result<T>() && {
        return Result<T>(std::move(err));
    }
};

inline ErrWrapper Err(EmulatorError err) {
    return ErrWrapper{std::move(err)};
}

/// TRY macro: unwrap a Result<T>, returning early on error.
/// Usage: auto value = TRY(expr);
/// On success, binds the T value. On error, returns the error from the enclosing function.
/// Uses statement expressions (GCC/Clang extension).
#define TRY(expr)                                                       \
    ({                                                                  \
        auto _try_result = (expr);                                      \
        if (_try_result.has_error()) [[unlikely]]                       \
            return _try_result.error();                                 \
        std::move(_try_result).value();                                  \
    })

/// TRY_VOID macro: for Result<void>, returns early on error.
/// Usage: TRY_VOID(expr);
#define TRY_VOID(expr)                                                  \
    do {                                                                \
        auto _try_result = (expr);                                      \
        if (_try_result.has_error()) [[unlikely]]                       \
            return Err(std::move(_try_result.error()));                  \
    } while (0)

} // namespace arm_cpu
