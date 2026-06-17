#pragma once

namespace hound {

/// Base class that disables copy but allows move.
///
/// In a network server, file descriptors, buffers, and event handles
/// are unique resources — copying them is almost always a bug.
/// Inheriting from this class makes the intent explicit and catches
/// accidental copies at compile time.
class NonCopyable {
public:
    NonCopyable() = default;
    ~NonCopyable() = default;

    NonCopyable(const NonCopyable&) = delete;
    NonCopyable& operator=(const NonCopyable&) = delete;

    NonCopyable(NonCopyable&&) noexcept = default;
    NonCopyable& operator=(NonCopyable&&) noexcept = default;
};

} // namespace hound
