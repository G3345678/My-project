#include "coro/context.h"
#include "util/logger.h"

namespace hound {
namespace coro {

Context::~Context() {
#ifdef _WIN32
    if (fiber_) {
        ::DeleteFiber(fiber_);
        fiber_ = nullptr;
    }
#else
    if (stack_) {
        std::free(stack_);
        stack_ = nullptr;
    }
#endif
    valid_ = false;
}

void Context::make(void (*func)(), size_t stack_size) {
#ifdef _WIN32
    // Windows Fibers: ConvertThreadToFiber is idempotent —
    // if the thread is already a fiber, it returns the current fiber.
    ::ConvertThreadToFiber(nullptr);

    // CreateFiber expects VOID CALLBACK(LPVOID), our func is void(*)(void)
    // On x86-64 the calling conventions are compatible
    fiber_ = ::CreateFiber(stack_size,
                           reinterpret_cast<LPFIBER_START_ROUTINE>(
                               reinterpret_cast<void(*)()>(func)),
                           nullptr);
    if (!fiber_) {
        LOG_FATAL("CreateFiber failed: %lu", ::GetLastError());
    }
#else
    // POSIX: ucontext — the traditional Unix coroutine API
    stack_ = static_cast<char*>(std::malloc(stack_size));
    if (!stack_) {
        LOG_FATAL("Coroutine stack allocation failed");
    }
    stack_size_ = stack_size;

    if (::getcontext(&uctx_) != 0) {
        LOG_FATAL("getcontext failed: %d", errno);
    }

    uctx_.uc_stack.ss_sp    = stack_;
    uctx_.uc_stack.ss_size  = stack_size_;
    uctx_.uc_link           = nullptr;  // No fallback context

    ::makecontext(&uctx_, func, 0);
#endif
    valid_ = true;
    LOG_TRACE("Context::make: created context");
}

void Context::swap(Context* from, Context* to) {
#ifdef _WIN32
    (void)from;  // Windows: SwitchToFiber saves state automatically
    ::SwitchToFiber(to->fiber_);
#else
    // POSIX: swapcontext saves current state to `from`, restores `to`
    ::swapcontext(&from->uctx_, &to->uctx_);
#endif
}

} // namespace coro
} // namespace hound
