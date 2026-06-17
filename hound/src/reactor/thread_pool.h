#pragma once

#include "util/noncopyable.h"

#include <functional>
#include <memory>
#include <vector>
#include <thread>
#include <atomic>

namespace hound {

class EventLoop;

/// EventLoopThreadPool — "one loop per thread" architecture.
///
/// # Why one loop per thread?
///
/// A single-threaded Reactor can handle ~10k connections on modern hardware.
/// But CPU-bound work (TLS handshake, compression, complex routing) needs
/// more cores. The standard pattern is:
///
///   1. Main thread: accepts new connections
///   2. Distributes connections round-robin to worker threads
///   3. Each worker thread has its own EventLoop (independent poller)
///
/// This eliminates lock contention: each EventLoop owns its fds,
/// no two threads touch the same Channel simultaneously.
///
/// # Thread model
///
///   Main Thread (EventLoop #0)
///     ├── Accepts connections
///     ├── Round-robin dispatch to workers
///     └── May also process connections
///
///   Worker Thread 1 (EventLoop #1)
///     ├── Owns a subset of connections
///     └── Independent epoll/select set
///
///   Worker Thread 2 (EventLoop #2)
///     ├── Owns a subset of connections
///     └── Independent epoll/select set
class EventLoopThreadPool : public NonCopyable {
public:
    explicit EventLoopThreadPool(EventLoop* base_loop);
    ~EventLoopThreadPool();

    /// Set the number of worker threads. Default: hardware concurrency.
    void set_thread_count(int n) { thread_count_ = n; }

    /// Start all worker threads.
    /// @param thread_init  Optional callback run in each worker before loop()
    void start(std::function<void(EventLoop*)> thread_init = nullptr);

    /// Get the next EventLoop in round-robin order.
    /// Used by the acceptor to distribute new connections.
    EventLoop* get_next_loop();

    /// Get all EventLoops (including the base loop at index 0).
    const std::vector<EventLoop*>& all_loops() const { return loops_; }

    /// Total number of loops (base loop + workers).
    size_t loop_count() const { return loops_.size(); }

    /// Is the pool running?
    bool started() const { return started_; }

private:
    void worker_thread_func(EventLoop* loop);

    EventLoop* base_loop_;
    int thread_count_ = 0;
    std::atomic<size_t> next_index_{0};

    std::vector<std::unique_ptr<EventLoop>> worker_loops_;
    std::vector<EventLoop*> loops_;       // [base_loop_, ...workers]
    std::vector<std::thread> threads_;
    bool started_ = false;
};

} // namespace hound
