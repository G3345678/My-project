#include "coro/coroutine.h"
#include "coro/scheduler.h"
#include "util/logger.h"

namespace hound {
namespace coro {

thread_local Coroutine* Coroutine::t_current_ = nullptr;
std::atomic<uint64_t> Coroutine::s_next_id_{1};

Coroutine::Coroutine(Func func, size_t /*stack_size*/)
    : id_(s_next_id_.fetch_add(1))
    , func_(std::move(func))
{
    LOG_TRACE("Coroutine %llu created", static_cast<long long>(id_));
}

Coroutine::~Coroutine() {
    LOG_TRACE("Coroutine %llu destroyed (state=%d)",
              static_cast<long long>(id_), static_cast<int>(state_));
}

void Coroutine::entry_point() {
    // This function runs inside the coroutine's context.
    // t_current_ was set by resume() before the context switch.
    Coroutine* self = t_current_;
    if (!self) {
        LOG_FATAL("Coroutine::entry_point: t_current_ is null");
    }

    // Execute the user's function
    self->func_();

    // Function returned — coroutine is done
    self->state_ = DONE;
    self->func_ = nullptr;  // Release any captured resources

    // Switch back to the scheduler
    // The scheduler will see state_ == DONE and clean up
    Context::swap(&self->ctx_, &self->scheduler_ctx_);

    // We should never reach here — the swap above never returns
    LOG_FATAL("Coroutine::entry_point: returned from final swap");
}

void Coroutine::resume() {
    // Must be in READY or SUSPENDED state
    state_ = RUNNING;
    t_current_ = this;

    LOG_TRACE("Coroutine %llu: resuming", static_cast<long long>(id_));

    if (!ctx_.valid()) {
        // First time: create the context and switch to it
        ctx_.make(&Coroutine::entry_point);
    }

    // Switch TO this coroutine. When it yields or finishes,
    // we'll return here.
    Context::swap(&scheduler_ctx_, &ctx_);

    // We're back! Either the coroutine yielded, or it's done.
    t_current_ = nullptr;

    LOG_TRACE("Coroutine %llu: returned from context (state=%d)",
              static_cast<long long>(id_), static_cast<int>(state_));
}

void Coroutine::yield() {
    Coroutine* self = t_current_;
    if (!self) {
        LOG_FATAL("Coroutine::yield: called outside of a coroutine");
    }

    self->state_ = SUSPENDED;

    LOG_TRACE("Coroutine %llu: yielding", static_cast<long long>(self->id_));

    // Switch back to the scheduler
    Context::swap(&self->ctx_, &self->scheduler_ctx_);

    // When we return here, the scheduler has resumed us
    LOG_TRACE("Coroutine %llu: resumed after yield", static_cast<long long>(self->id_));
}

Coroutine* Coroutine::current() {
    return t_current_;
}

} // namespace coro
} // namespace hound
