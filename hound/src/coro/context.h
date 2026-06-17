#pragma once

/// Coroutine context switching abstraction.
///
/// # Platform backends
///
/// | Platform | Backend   | Mechanism                                    |
/// |----------|-----------|----------------------------------------------|
/// | Windows  | Fibers    | ConvertThreadToFiber / CreateFiber / SwitchToFiber |
/// | POSIX    | ucontext  | getcontext / makecontext / swapcontext        |
///
/// # What is a context?
///
/// A context is a snapshot of CPU state — all registers, stack pointer,
/// and instruction pointer. Switching contexts means:
///   1. Save current CPU state to current context
///   2. Load target context's CPU state
///   3. Jump to target's instruction pointer
///
/// This is the fundamental primitive behind coroutines, fibers, and
/// user-space threads. It's what makes `co_yield()` and `co_resume()`
/// possible without kernel involvement.
///
/// # Stackful vs stackless
///
/// These are STACKFUL coroutines — each coroutine has its own stack
/// (128KB by default). This means you can yield from nested function
/// calls, unlike stackless coroutines (C++20 co_await) which can only
/// suspend at the top level.

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #include <ucontext.h>
    #include <cstdlib>
    #include <cstring>
#endif

namespace hound {
namespace coro {

/// Platform-specific context data.
class Context {
public:
    Context() = default;
    ~Context();

    // Non-copyable, non-movable (raw pointers involved)
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    /// Initialize this context with a function and stack.
    /// @param func   Function to run when this context is first entered
    /// @param stack_size  Stack size in bytes (default 128KB)
    void make(void (*func)(), size_t stack_size = 128 * 1024);

    /// Save current execution state into `from` and restore `to`.
    /// After this call, execution continues in `to`'s context.
    /// The next time someone switches TO `from`, execution resumes
    /// right after this call.
    static void swap(Context* from, Context* to);

    /// Check if this context has been initialized.
    bool valid() const { return valid_; }

private:
#ifdef _WIN32
    LPVOID fiber_ = nullptr;   // Windows fiber handle
#else
    ucontext_t uctx_{};
    char* stack_ = nullptr;
    size_t stack_size_ = 0;
#endif
    bool valid_ = false;
};

} // namespace coro
} // namespace hound
