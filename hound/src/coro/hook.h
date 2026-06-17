#pragma once

/// Coroutine-aware I/O wrappers.
///
/// These functions replace the standard blocking I/O calls with
/// coroutine-friendly versions. When an operation would block
/// (EAGAIN), the coroutine yields instead of blocking the thread.
///
/// # Usage
///
/// Inside a coroutine, use these instead of raw read/write/accept:
///
///   auto n = coro::co_read(fd, buf, sizeof(buf));
///   auto n = coro::co_write(fd, buf, len);
///   auto client = coro::co_accept(listen_fd, &peer_addr);
///   coro::co_sleep(1.5);  // Sleep for 1.5 seconds (coroutine yields)
///
/// These mimic blocking I/O from the coroutine's perspective, but
/// actually yield the coroutine and let other coroutines run.
///
/// # POSIX LD_PRELOAD hooks
///
/// On Linux, we also support hooking the POSIX read/write/accept/connect
/// functions via dlsym(RTLD_NEXT). This means EXISTING code that uses
/// read()/write() will automatically become coroutine-aware without
/// modification. Enable with -DHOUND_ENABLE_HOOK.

#include <cstddef>
#include <cstdint>
#include <sys/types.h>

#ifdef _WIN32
    #include <winsock2.h>
#else
    #include <sys/socket.h>
    #include <sys/uio.h>
#endif

namespace hound {

class Address;

namespace coro {

// ── Coroutine-aware I/O ─────────────────────────────────────

/// Read from fd. Yields coroutine if data not available.
ssize_t co_read(int fd, void* buf, size_t count);

/// Write to fd. Yields coroutine if buffer is full.
ssize_t co_write(int fd, const void* buf, size_t count);

/// Accept a connection. Yields coroutine if no connections pending.
int co_accept(int listen_fd, Address* peer_addr = nullptr);

/// Connect to remote address. Yields coroutine during handshake.
int co_connect(int fd, const Address& addr);

/// Sleep for `seconds` seconds. Yields coroutine instead of blocking thread.
void co_sleep(double seconds);

} // namespace coro
} // namespace hound
