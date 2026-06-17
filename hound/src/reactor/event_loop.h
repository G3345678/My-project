#pragma once

#include "util/noncopyable.h"
#include "reactor/timestamp.h"
#include "reactor/callbacks.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>
#include <thread>

namespace hound {

class Channel;
class Poller;
class TimerWheel;

/// EventLoop — the heart of the Reactor pattern.
///
/// # What is a Reactor?
///
/// A Reactor (or "event loop") is a design pattern where a single thread
/// continuously waits for I/O events on many file descriptors, then
/// dispatches each event to its registered handler. It's the foundation
/// of Node.js, Nginx, Redis, and our HTTP server.
///
/// # One loop per thread
///
/// Each EventLoop belongs to exactly one thread. The thread calls `loop()`,
/// which blocks on the Poller until events arrive, then dispatches them.
/// Other threads can submit tasks via `run_in_loop()` / `queue_in_loop()`,
/// which are executed safely in the loop thread.
///
/// # Main loop pseudocode
///
///   while (!quit) {
///       poll(timeout)           // Wait for I/O events
///       for each active channel:
///           channel->handle_event()
///       expire_timers()        // Fire due timers
///       run_pending_tasks()    // Execute queued functions
///   }
///
/// # Thread safety
///
/// All I/O and Channel operations MUST happen in the loop thread.
/// `queue_in_loop()` is the safe way for other threads to interact.
class EventLoop : public NonCopyable {
public:
    EventLoop();
    ~EventLoop();

    // ── Main Loop ────────────────────────────────────────────
    /// Enter the event loop. Blocks until quit() is called.
    /// Must be called from the thread that created this EventLoop.
    void loop();

    /// Exit the event loop. Can be called from any thread.
    /// The loop will finish its current iteration before returning.
    void quit();

    /// Is this EventLoop currently running?
    bool is_looping() const { return looping_; }

    // ── Thread Identity ──────────────────────────────────────
    /// Assert that the caller is running in this EventLoop's thread.
    /// Used throughout the codebase to catch thread-safety bugs.
    void assert_in_loop_thread() const;

    /// Check if the caller is in this loop's thread.
    bool is_in_loop_thread() const {
        return thread_id_ == std::this_thread::get_id();
    }

    /// Get the EventLoop for the current thread (thread_local).
    static EventLoop* current();

    // ── Cross-Thread Task Submission ─────────────────────────
    /// Run a task in this loop's thread.
    /// If called from the loop thread: executes immediately.
    /// If called from another thread: queues the task and wakes up the loop.
    void run_in_loop(Task task);

    /// Queue a task. Always defers execution to the next loop iteration,
    /// even if called from the loop thread.
    void queue_in_loop(Task task);

    // ── Poller Interface ─────────────────────────────────────
    /// Called by Channel::update() — propagates to Poller.
    void update_channel(Channel* channel);

    /// Called by Channel::remove() — propagates to Poller.
    void remove_channel(Channel* channel);

    bool has_channel(Channel* channel) const;

    // ── Timer Interface ──────────────────────────────────────
    /// Run a callback after `delay` seconds.
    /// Returns a timer ID that can be used to cancel.
    int64_t run_after(double delay, TimerCallback cb);

    /// Run a callback every `interval` seconds.
    int64_t run_every(double interval, TimerCallback cb);

    /// Run a callback at a specific time.
    int64_t run_at(Timestamp time, TimerCallback cb);

    /// Cancel a timer by its ID.
    void cancel_timer(int64_t timer_id);

private:
    // ── Internal Wakers ──────────────────────────────────────
    /// Read handler for the wakeup fd. Called when another thread
    /// writes to wake up the loop.
    void handle_wakeup_read(Timestamp);

    /// Wake up the loop from another thread.
    void wakeup();

    /// Execute all pending tasks (called once per loop iteration).
    void do_pending_tasks();

    // ── Members ──────────────────────────────────────────────
    std::unique_ptr<Poller> poller_;
    std::unique_ptr<TimerWheel> timer_wheel_;

    // Wakeup mechanism: another thread writes to wakeup_fd_, the
    // Poller detects readability, handle_wakeup_read() reads the data.
    int wakeup_fd_;                              // Read end (eventfd or pipe[0])
    int wakeup_write_fd_;                        // Write end (same as read for eventfd)
    std::unique_ptr<Channel> wakeup_channel_;

    bool looping_ = false;
    std::atomic<bool> quit_{false};
    bool event_handling_ = false;
    bool calling_pending_tasks_ = false;

    const std::thread::id thread_id_;

    // Pending tasks queue — accessed from multiple threads
    std::mutex mutex_;
    std::vector<Task> pending_tasks_;

    // Per-loop pointer for EventLoop::current()
    static thread_local EventLoop* t_loop_in_this_thread_;
};

} // namespace hound
