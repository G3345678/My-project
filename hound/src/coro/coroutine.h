#pragma once

#include "coro/context.h"

#include <functional>
#include <memory>
#include <atomic>

namespace hound {

class EventLoop;

namespace coro {

class Scheduler;

/// A stackful coroutine.
///
/// Each Coroutine has:
///   - Its own stack (128KB default)
///   - A function to execute
///   - A state (ready, running, suspended, done)
///   - Integration with a Scheduler
///
/// Usage:
///   auto co = std::make_shared<Coroutine>([]() {
///       LOG_INFO("Inside coroutine!");
///       Coroutine::yield();  // Suspend
///       LOG_INFO("Resumed!");
///   });
///   scheduler->schedule(co);
///   // Coroutine runs until yield(), then returns to scheduler
class Coroutine : public std::enable_shared_from_this<Coroutine> {
public:
    using Ptr = std::shared_ptr<Coroutine>;
    using Func = std::function<void()>;

    enum State {
        READY,      // Created, never run
        RUNNING,    // Currently executing
        SUSPENDED,  // Yielded, waiting to be resumed
        DONE        // Function returned
    };

    /// Create a coroutine with a function to execute.
    explicit Coroutine(Func func, size_t stack_size = 128 * 1024);

    ~Coroutine();

    // ── State ────────────────────────────────────────────────
    State state() const { return state_; }
    void set_state(State s) { state_ = s; }
    uint64_t id() const { return id_; }

    /// The Scheduler that owns this coroutine.
    Scheduler* scheduler() const { return scheduler_; }
    void set_scheduler(Scheduler* s) { scheduler_ = s; }

    // ── Context Switching ────────────────────────────────────
    /// Resume this coroutine. Called by the Scheduler.
    /// Switches FROM the caller's context TO this coroutine.
    void resume();

    /// Yield execution back to the Scheduler.
    /// Switches FROM this coroutine TO the scheduler's context.
    /// Must only be called from WITHIN the coroutine.
    static void yield();

    /// Get the currently running coroutine on this thread.
    static Coroutine* current();

private:
    /// The actual entry point. Calls func_, then switches back.
    static void entry_point();

    uint64_t id_;
    State state_ = READY;
    Func func_;
    Context ctx_;
    Context scheduler_ctx_;  // Where to return on yield/complete
    Scheduler* scheduler_ = nullptr;

    // Per-thread pointer to currently running coroutine
    static thread_local Coroutine* t_current_;

    static std::atomic<uint64_t> s_next_id_;
};

} // namespace coro
} // namespace hound
