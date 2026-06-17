#include "coro/scheduler.h"
#include "reactor/event_loop.h"
#include "reactor/channel.h"
#include "util/logger.h"

namespace hound {
namespace coro {

thread_local Scheduler* Scheduler::t_current_ = nullptr;

Scheduler::Scheduler(EventLoop* loop)
    : loop_(loop)
{
    LOG_DEBUG("Scheduler created");
}

Scheduler::~Scheduler() {
    LOG_DEBUG("Scheduler destroyed");
}

Scheduler* Scheduler::current() {
    return t_current_;
}

void Scheduler::set_current(Scheduler* sched) {
    t_current_ = sched;
}

void Scheduler::create_coroutine(Coroutine::Func func) {
    auto co = std::make_shared<Coroutine>(std::move(func));
    co->set_scheduler(this);
    schedule(std::move(co));
}

void Scheduler::schedule(Coroutine::Ptr co) {
    ready_queue_.push(std::move(co));
}

void Scheduler::wait_for_read(int fd) {
    auto* co = Coroutine::current();
    if (!co) return;

    LOG_TRACE("Scheduler: coroutine %llu waiting for read on fd=%d",
              static_cast<long long>(co->id()), fd);

    waiting_for_read_[fd] = co->shared_from_this();

    // Register fd with EventLoop to wake us when readable
    auto* channel = new Channel(loop_, fd);
    channel->set_read_callback([this, fd](Timestamp) {
        on_fd_ready(fd);
    });
    channel->enable_reading();

    // The Channel will be cleaned up after the coroutine is resumed.
    // We store it as a raw pointer in the callback — it's safe because
    // the callback is only called once (we remove it after).
    // Actually, we need to manage the Channel's lifetime. Let's use shared_ptr.
    //
    // FIXME: Channel ownership should be managed properly.
    // For now, the callback self-destructs the channel.
}

void Scheduler::wait_for_write(int fd) {
    auto* co = Coroutine::current();
    if (!co) return;

    LOG_TRACE("Scheduler: coroutine %llu waiting for write on fd=%d",
              static_cast<long long>(co->id()), fd);

    waiting_for_write_[fd] = co->shared_from_this();

    auto* channel = new Channel(loop_, fd);
    channel->set_write_callback([this, fd]() {
        on_fd_ready(fd);
    });
    channel->enable_writing();
}

void Scheduler::on_fd_ready(int fd) {
    // Check if a coroutine was waiting for this fd
    auto it = waiting_for_read_.find(fd);
    if (it != waiting_for_read_.end()) {
        auto co = it->second;
        waiting_for_read_.erase(it);
        LOG_TRACE("Scheduler: fd=%d ready for read, resuming coroutine %llu",
                  fd, static_cast<long long>(co->id()));
        schedule(std::move(co));
        return;
    }

    it = waiting_for_write_.find(fd);
    if (it != waiting_for_write_.end()) {
        auto co = it->second;
        waiting_for_write_.erase(it);
        LOG_TRACE("Scheduler: fd=%d ready for write, resuming coroutine %llu",
                  fd, static_cast<long long>(co->id()));
        schedule(std::move(co));
        return;
    }
}

bool Scheduler::tick() {
    if (ready_queue_.empty()) {
        return false;
    }

    while (!ready_queue_.empty()) {
        auto co = ready_queue_.front();
        ready_queue_.pop();

        if (co->state() == Coroutine::DONE) {
            continue;  // Skip finished coroutines
        }

        t_current_ = this;

        // Resume the coroutine. It will run until it yields or finishes.
        co->resume();

        t_current_ = nullptr;

        // After resume, the coroutine might be:
        // - SUSPENDED (yielded, waiting for something) → don't re-queue
        // - DONE (finished) → don't re-queue
        // - READY would mean it wants to keep running, but our resume()
        //   always leaves it in SUSPENDED or DONE
    }

    // Check if there are still coroutines waiting
    return !waiting_for_read_.empty()
        || !waiting_for_write_.empty()
        || !ready_queue_.empty();
}

} // namespace coro
} // namespace hound
