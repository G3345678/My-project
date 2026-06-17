#include "coro/hook.h"
#include "coro/coroutine.h"
#include "coro/scheduler.h"
#include "reactor/event_loop.h"
#include "io/address.h"
#include "util/logger.h"

#include <cerrno>
#include <chrono>
#include <thread>

#ifdef _WIN32
    #include <winsock2.h>
#else
    #include <unistd.h>
    #include <fcntl.h>
#endif

namespace hound {
namespace coro {

// ─────────────────────────────────────────────────────────────
//  Helper: set fd to non-blocking
// ─────────────────────────────────────────────────────────────

namespace {
    void ensure_nonblocking(int fd) {
#ifdef _WIN32
        u_long mode = 1;
        ::ioctlsocket(fd, FIONBIO, &mode);
#else
        int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags >= 0) {
            ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }
#endif
    }

    bool would_block() {
#ifdef _WIN32
        int err = WSAGetLastError();
        return (err == WSAEWOULDBLOCK);
#else
        return (errno == EAGAIN || errno == EWOULDBLOCK);
#endif
    }
}

// ─────────────────────────────────────────────────────────────
//  co_read
// ─────────────────────────────────────────────────────────────

ssize_t co_read(int fd, void* buf, size_t count) {
    auto* sched = Scheduler::current();
    if (!sched) {
        // Not in a coroutine — fall back to blocking read
#ifdef _WIN32
        return ::recv(fd, static_cast<char*>(buf), static_cast<int>(count), 0);
#else
        return ::read(fd, buf, count);
#endif
    }

    ensure_nonblocking(fd);

    for (;;) {
#ifdef _WIN32
        auto n = ::recv(fd, static_cast<char*>(buf), static_cast<int>(count), 0);
#else
        auto n = ::read(fd, buf, count);
#endif
        if (n > 0) return n;
        if (n == 0) return 0;  // EOF

        if (would_block()) {
            // No data yet — yield and wait for EventLoop to wake us
            LOG_TRACE("co_read(fd=%d): would block, yielding", fd);
            sched->wait_for_read(fd);
            Coroutine::yield();
            // When we return, the fd should be readable — retry
            continue;
        }

        return -1;  // Real error
    }
}

// ─────────────────────────────────────────────────────────────
//  co_write
// ─────────────────────────────────────────────────────────────

ssize_t co_write(int fd, const void* buf, size_t count) {
    auto* sched = Scheduler::current();
    if (!sched) {
#ifdef _WIN32
        return ::send(fd, static_cast<const char*>(buf), static_cast<int>(count), 0);
#else
        return ::write(fd, buf, count);
#endif
    }

    ensure_nonblocking(fd);

    for (;;) {
#ifdef _WIN32
        auto n = ::send(fd, static_cast<const char*>(buf), static_cast<int>(count), 0);
#else
        auto n = ::write(fd, buf, count);
#endif
        if (n > 0) return n;

        if (would_block()) {
            LOG_TRACE("co_write(fd=%d): would block, yielding", fd);
            sched->wait_for_write(fd);
            Coroutine::yield();
            continue;
        }

        return -1;
    }
}

// ─────────────────────────────────────────────────────────────
//  co_accept
// ─────────────────────────────────────────────────────────────

int co_accept(int listen_fd, Address* peer_addr) {
    auto* sched = Scheduler::current();
    if (!sched) {
        struct sockaddr_storage ss{};
        socklen_t len = sizeof(ss);
#ifdef _WIN32
        auto fd = ::accept(listen_fd, reinterpret_cast<struct sockaddr*>(&ss), &len);
#else
        auto fd = ::accept(listen_fd, reinterpret_cast<struct sockaddr*>(&ss), &len);
#endif
        if (peer_addr && static_cast<int>(fd) >= 0) {
            *peer_addr = Address::from_sockaddr(
                reinterpret_cast<struct sockaddr*>(&ss), len);
        }
        return static_cast<int>(fd);
    }

    ensure_nonblocking(listen_fd);

    for (;;) {
        struct sockaddr_storage ss{};
        socklen_t len = sizeof(ss);
#ifdef _WIN32
        auto fd = ::accept(listen_fd, reinterpret_cast<struct sockaddr*>(&ss), &len);
#else
        auto fd = ::accept(listen_fd, reinterpret_cast<struct sockaddr*>(&ss), &len);
#endif
        if (static_cast<int>(fd) >= 0) {
            if (peer_addr) {
                *peer_addr = Address::from_sockaddr(
                    reinterpret_cast<struct sockaddr*>(&ss), len);
            }
            return static_cast<int>(fd);
        }

        if (would_block()) {
            LOG_TRACE("co_accept: would block, yielding");
            sched->wait_for_read(listen_fd);
            Coroutine::yield();
            continue;
        }

        return -1;
    }
}

// ─────────────────────────────────────────────────────────────
//  co_connect
// ─────────────────────────────────────────────────────────────

int co_connect(int fd, const Address& addr) {
    auto* sched = Scheduler::current();
    if (!sched) {
        return ::connect(fd, addr.sockaddr(), addr.socklen());
    }

    ensure_nonblocking(fd);

    int ret = ::connect(fd, addr.sockaddr(), addr.socklen());
    if (ret == 0) return 0;  // Connected immediately

    if (would_block()
#ifdef _WIN32
        || WSAGetLastError() == WSAEWOULDBLOCK
#else
        || errno == EINPROGRESS
#endif
    ) {
        // Connection in progress — yield and wait
        LOG_TRACE("co_connect: in progress, yielding");
        sched->wait_for_write(fd);
        Coroutine::yield();

        // Check if connection succeeded
        int error = 0;
        socklen_t len = sizeof(error);
#ifdef _WIN32
        ::getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &len);
#else
        ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len);
#endif
        if (error != 0) {
            errno = error;
            return -1;
        }
        return 0;
    }

    return -1;
}

// ─────────────────────────────────────────────────────────────
//  co_sleep
// ─────────────────────────────────────────────────────────────

void co_sleep(double seconds) {
    auto* sched = Scheduler::current();
    if (!sched) {
        // Not in a coroutine — actually sleep
        std::this_thread::sleep_for(
            std::chrono::microseconds(static_cast<int64_t>(seconds * 1'000'000)));
        return;
    }

    // In a coroutine: set a timer on the EventLoop, then yield.
    // When the timer fires, we'll be resumed.
    auto* loop = sched->event_loop();
    auto* co = Coroutine::current();

    // Use a shared flag that the timer callback sets
    auto done = std::make_shared<bool>(false);

    loop->run_after(seconds, [done, co_ptr = co->shared_from_this(), sched]() {
        *done = true;
        sched->schedule(co_ptr);
    });

    // Yield until the timer fires
    while (!*done) {
        Coroutine::yield();
    }

    LOG_TRACE("co_sleep(%.3f): woke up", seconds);
}

} // namespace coro
} // namespace hound
