#pragma once

#include "util/noncopyable.h"
#include "util/result.h"
#include "io/address.h"

#include <memory>
#include <vector>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    // Windows doesn't have struct iovec; define it for our interface
    struct iovec {
        void*  iov_base;
        size_t iov_len;
    };
#else
    #include <unistd.h>
    #include <sys/uio.h>  // readv/writev + struct iovec
#endif

namespace hound {

/// RAII wrapper around a TCP/UDP socket file descriptor.
///
/// ## Platform notes
/// On POSIX, fd is `int`; on Windows, fd is `SOCKET` (which is `UINT_PTR`).
/// The conversion is handled transparently — external code uses `fd()`.
///
/// ## Ownership
/// Socket is move-only. Copying a socket is a logic error (double-close).
/// Inherits NonCopyable for compile-time enforcement.
///
/// ## Error handling
/// Operations that commonly fail (bind, accept) return Result<T>.
/// Operations that fail only in unrecoverable situations (close on already-closed
/// fd) log and continue.
class Socket : public NonCopyable {
public:
#ifdef _WIN32
    using NativeFd = SOCKET;
    static constexpr NativeFd kInvalidFd = INVALID_SOCKET;
#else
    using NativeFd = int;
    static constexpr NativeFd kInvalidFd = -1;
#endif

    // ── Lifecycle ─────────────────────────────────────────────
    Socket() : fd_(kInvalidFd) {}
    explicit Socket(NativeFd fd) : fd_(fd) {
        hold_wsa_ref();
    }

    Socket(Socket&& other) noexcept : fd_(other.fd_) {
        other.fd_ = kInvalidFd;
        // wsa_ref_ is copied via shared_ptr, then other's is reset below
        wsa_ref_.swap(other.wsa_ref_);
    }

    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            other.fd_ = kInvalidFd;
            wsa_ref_ = std::move(other.wsa_ref_);
        }
        return *this;
    }

    ~Socket() { close(); }

    // ── Factory Methods ───────────────────────────────────────
    /// Create a TCP socket. Returns error if fd allocation fails.
    static Result<Socket> create_tcp(bool ipv6 = false);

    /// Create a UDP socket. Returns error if fd allocation fails.
    static Result<Socket> create_udp(bool ipv6 = false);

    // ── Socket Options ────────────────────────────────────────
    /// SO_REUSEADDR — allow rebinding to TIME_WAIT ports immediately.
    /// Critical for fast server restart during development.
    void set_reuse_addr(bool on = true);

    /// SO_REUSEPORT — allow multiple sockets to bind the same port.
    /// Enables kernel-level load balancing across threads/processes.
    /// NOTE: Not available on Windows. On Linux, requires kernel >= 3.9.
    void set_reuse_port(bool on = true);

    /// SO_KEEPALIVE — periodically send keep-alive probes.
    /// Detects dead connections even when idle.
    void set_keep_alive(bool on = true);

    /// TCP_NODELAY — disable Nagle's algorithm.
    /// Nagle batches small writes at the cost of latency.
    /// For HTTP servers, we want this OFF (nodelay = true) to minimize latency.
    void set_tcp_nodelay(bool on = true);

    /// Set O_NONBLOCK (POSIX) or FIONBIO (Windows).
    /// All I/O in this project is non-blocking — the Reactor drives
    /// readiness notifications via epoll/kqueue.
    void set_nonblocking(bool on = true);

    // ── Server Operations ─────────────────────────────────────
    /// Bind to a local address. Must be called before listen().
    Result<void> bind(const Address& addr);

    /// Mark socket as passive and set accept queue depth.
    /// SOMAXCONN is the system default maximum backlog.
    Result<void> listen(int backlog = SOMAXCONN);

    /// Accept an incoming connection.
    /// Returns a new Socket for the connection, or error (e.g., EAGAIN
    /// in nonblocking mode when no connections are pending).
    Result<Socket> accept(Address* peer_addr = nullptr);

    // ── Client Operations ─────────────────────────────────────
    /// Connect to a remote address.
    Result<void> connect(const Address& addr);

    // ── I/O Operations ────────────────────────────────────────
    /// Read up to `count` bytes into `buf`.
    /// Returns bytes read, 0 on EOF, or error.
    /// In nonblocking mode, EAGAIN means "no data yet, try again later".
    Result<ssize_t> read(void* buf, size_t count);

    /// Write up to `count` bytes from `buf`.
    /// Returns bytes written or error.
    Result<ssize_t> write(const void* buf, size_t count);

    /// Scatter read (readv). Reads into multiple buffers in one syscall.
    /// Useful for reading header + body into separate buffers without copying.
    Result<ssize_t> readv(const struct iovec* iov, int iovcnt);

    /// Gather write (writev). Writes from multiple buffers in one syscall.
    /// Useful for HTTP response: status line + headers + body without concatenation.
    Result<ssize_t> writev(const struct iovec* iov, int iovcnt);

    // ── State ─────────────────────────────────────────────────
    void close();

    NativeFd fd() const { return fd_; }
    bool is_valid() const { return fd_ != kInvalidFd; }

    /// Get the socket error status (SO_ERROR).
    /// Returns 0 if no error, otherwise the pending error code.
    int get_error() const;

private:
    /// Set a socket option. Templated to handle different option types.
    template <typename T>
    void set_option(int level, int optname, const T& value);

    /// Acquire a reference to the Winsock context (Windows only).
    void hold_wsa_ref();

    NativeFd fd_ = kInvalidFd;

#ifdef _WIN32
    // Each Socket holds a shared_ptr reference to the singleton Winsock
    // context, ensuring WSA isn't cleaned up while any socket is alive.
    std::shared_ptr<void> wsa_ref_;
#endif
};

/// Ensure the platform's networking subsystem is initialized.
/// On Windows, this calls WSAStartup. On POSIX, this is a no-op.
/// Safe to call multiple times (reference counted internally).
void ensure_networking_initialized();

} // namespace hound
