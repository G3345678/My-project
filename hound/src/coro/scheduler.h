#pragma once

#include "coro/coroutine.h"

#include <queue>
#include <unordered_map>
#include <memory>

namespace hound {

class EventLoop;

namespace coro {

/// Coroutine scheduler integrated with EventLoop.
///
/// Manages a pool of coroutines within a single EventLoop thread.
/// When a coroutine yields (e.g., waiting for I/O), the scheduler
/// runs the next ready coroutine or returns control to the EventLoop.
///
/// # Scheduling model
///
/// Simple FIFO queue for ready coroutines:
///
///   [co1] → [co2] → [co3] → ...
///    ↑                    ↑
///   head                tail
///
/// When a coroutine yields, it goes to the back of the queue.
/// When a coroutine is resumed (I/O ready), it goes to the back too.
///
/// # I/O integration
///
/// The Scheduler hooks I/O operations. When a coroutine calls co_read()
/// and gets EAGAIN:
///   1. The fd is registered with EventLoop
///   2. The coroutine yields (goes to SUSPENDED state)
///   3. When EventLoop detects the fd is readable, the coroutine
///      is re-queued
///   4. The coroutine retries the read when it runs again
class Scheduler : public std::enable_shared_from_this<Scheduler> {
public:
    using Ptr = std::shared_ptr<Scheduler>;

    /// Create a scheduler attached to an EventLoop.
    explicit Scheduler(EventLoop* loop);
    ~Scheduler();

    // ── Coroutine Management ─────────────────────────────────
    /// Create and schedule a new coroutine.
    /// The coroutine runs immediately if no other coroutine is running.
    void create_coroutine(Coroutine::Func func);

    /// Add an existing coroutine to the ready queue.
    void schedule(Coroutine::Ptr co);

    /// Called by the hook layer when a coroutine is waiting for I/O.
    /// The coroutine is suspended until the fd becomes ready.
    void wait_for_read(int fd);
    void wait_for_write(int fd);

    /// Resume a coroutine that was waiting for I/O on this fd.
    void on_fd_ready(int fd);

    /// Run one scheduling cycle. Returns true if there's more work.
    bool tick();

    // ── State ────────────────────────────────────────────────
    EventLoop* event_loop() const { return loop_; }
    size_t ready_count() const { return ready_queue_.size(); }
    size_t waiting_count() const {
        return waiting_for_read_.size() + waiting_for_write_.size();
    }

    /// Get the scheduler for the current thread.
    static Scheduler* current();
    static void set_current(Scheduler* sched);

private:
    EventLoop* loop_;

    // FIFO queue of coroutines ready to run
    std::queue<Coroutine::Ptr> ready_queue_;

    // Coroutines waiting for I/O: fd → coroutine
    std::unordered_map<int, Coroutine::Ptr> waiting_for_read_;
    std::unordered_map<int, Coroutine::Ptr> waiting_for_write_;

    static thread_local Scheduler* t_current_;
};

} // namespace coro
} // namespace hound
