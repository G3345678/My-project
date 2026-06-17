#include "reactor/thread_pool.h"
#include "reactor/event_loop.h"
#include "util/logger.h"

#include <cassert>

namespace hound {

EventLoopThreadPool::EventLoopThreadPool(EventLoop* base_loop)
    : base_loop_(base_loop)
{
    // Default: use all available hardware threads
    thread_count_ = static_cast<int>(std::thread::hardware_concurrency());
    if (thread_count_ < 1) thread_count_ = 1;

    LOG_DEBUG("ThreadPool: created with %d threads", thread_count_);
}

EventLoopThreadPool::~EventLoopThreadPool() {
    // Quit all worker loops
    for (auto& loop : worker_loops_) {
        if (loop) {
            loop->quit();
        }
    }

    // Wait for all worker threads to finish
    for (auto& t : threads_) {
        if (t.joinable()) {
            t.join();
        }
    }

    LOG_DEBUG("ThreadPool: all threads stopped");
}

void EventLoopThreadPool::start(std::function<void(EventLoop*)> thread_init) {
    assert(!started_);

    // Allocate worker loops
    for (int i = 0; i < thread_count_; ++i) {
        auto loop = std::make_unique<EventLoop>();
        worker_loops_.push_back(std::move(loop));
    }

    // Build the full loop list: base loop + workers
    loops_.clear();
    loops_.push_back(base_loop_);
    for (auto& loop : worker_loops_) {
        loops_.push_back(loop.get());
    }

    // Start worker threads
    for (auto& loop : worker_loops_) {
        threads_.emplace_back([this, &loop, thread_init]() {
            worker_thread_func(loop.get());

            // Run optional initialization
            if (thread_init) {
                thread_init(loop.get());
            }

            // Enter the event loop (blocks until quit())
            loop->loop();
        });
    }

    started_ = true;
    LOG_INFO("ThreadPool: started %d worker threads", thread_count_);
}

EventLoop* EventLoopThreadPool::get_next_loop() {
    // Round-robin distribution
    size_t idx = next_index_.fetch_add(1, std::memory_order_relaxed);
    return loops_[idx % loops_.size()];
}

void EventLoopThreadPool::worker_thread_func(EventLoop* /*loop*/) {
    LOG_DEBUG("Worker thread started");
}

} // namespace hound
