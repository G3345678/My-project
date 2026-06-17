#pragma once

#include <string>
#include <variant>
#include <system_error>
#include <optional>

namespace hound {

/// A simple Result<T> type for operations that can fail.
///
/// This is a minimal reimplementation of std::expected (C++23) / tl::expected.
/// We avoid exceptions for expected error paths (e.g., connection refused,
/// broken pipe) while still getting type-safe error propagation.
///
/// Usage:
///   auto sock = Socket::create_tcp();
///   if (!sock) {
///       LOG_ERROR("Failed to create socket: {}", sock.error().message());
///       return;
///   }
///   sock->bind(addr);
template <typename T>
class Result {
public:
    // ── Constructors ─────────────────────────────────────────
    Result(T value) noexcept
        : storage_(std::in_place_index<0>, std::move(value)) {}

    Result(std::error_code ec, std::string msg = {}) noexcept
        : storage_(std::in_place_index<1>,
                   Error{ec, std::move(msg)}) {}

    // ── Observers ────────────────────────────────────────────
    explicit operator bool() const noexcept { return has_value(); }

    bool has_value() const noexcept {
        return storage_.index() == 0;
    }

    T& value() { return std::get<0>(storage_); }
    const T& value() const { return std::get<0>(storage_); }

    T& operator*() { return value(); }
    const T& operator*() const { return value(); }

    T* operator->() { return &value(); }
    const T* operator->() const { return &value(); }

    const std::error_code& error() const {
        return std::get<1>(storage_).ec;
    }

    const std::string& error_msg() const {
        return std::get<1>(storage_).message;
    }

private:
    struct Error {
        std::error_code ec;
        std::string message;
    };

    std::variant<T, Error> storage_;
};

/// Specialization for void — no value, just success/failure.
template <>
class Result<void> {
public:
    Result() = default;  // Success

    Result(std::error_code ec, std::string msg = {}) noexcept
        : error_(Error{ec, std::move(msg)}) {}

    explicit operator bool() const noexcept { return !error_.has_value(); }
    bool has_value() const noexcept { return !error_.has_value(); }

    const std::error_code& error() const { return error_->ec; }
    const std::string& error_msg() const { return error_->message; }

private:
    struct Error {
        std::error_code ec;
        std::string message;
    };
    std::optional<Error> error_;
};

/// Helper for creating error Results with errno.
template <typename T = void>
Result<T> make_errno_result(std::string msg = {}) {
    return Result<T>(
        std::error_code(errno, std::generic_category()),
        std::move(msg));
}

} // namespace hound
