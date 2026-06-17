#include "io/socket.h"
#include "util/logger.h"

#include <cstring>
#include <stdexcept>

#ifdef _WIN32
    // Winsock2 already included via socket.h
    #include <ws2tcpip.h>
#else
    #include <fcntl.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <unistd.h>
#endif

namespace hound {

// ─────────────────────────────────────────────────────────────
//  Platform: Error code extraction
// ─────────────────────────────────────────────────────────────

#ifdef _WIN32
    namespace { int sys_error() { return WSAGetLastError(); } }
    #define HOUND_EAGAIN WSAEWOULDBLOCK
    #define HOUND_EINTR  WSAEINTR
#else
    namespace { int sys_error() { return errno; } }
    #define HOUND_EAGAIN EAGAIN
    #define HOUND_EINTR  EINTR
#endif

// ─────────────────────────────────────────────────────────────
//  Windows: Winsock initialization (reference-counted singleton)
// ─────────────────────────────────────────────────────────────
//
// Windows requires WSAStartup() before any socket operation.
// We use a shared_ptr-based lazy-init: the first Socket creation
// initializes Winsock; the last Socket destruction cleans it up.
// This is thread-safe at program start/end boundaries.

#ifdef _WIN32
namespace {
    struct WsaContext {
        WsaContext() {
            WSADATA data;
            int ret = WSAStartup(MAKEWORD(2, 2), &data);
            if (ret != 0) {
                LOG_FATAL("WSAStartup failed: %d", ret);
            }
            LOG_DEBUG("Winsock initialized (v%d.%d)",
                      LOBYTE(data.wVersion), HIBYTE(data.wVersion));
        }
        ~WsaContext() {
            WSACleanup();
            LOG_DEBUG("Winsock cleaned up");
        }
    };

    // Returns a reference to a permanent WSA context.
    // The context is never destroyed (program-lifetime singleton),
    // so Winsock stays initialized until process exit.
    // This avoids the timing issue where WSA cleanup happens
    // before all sockets are destroyed.
    std::shared_ptr<WsaContext> ensure_wsa() {
        static auto ctx = std::make_shared<WsaContext>();
        return ctx;
    }
} // anonymous namespace

// hold_wsa_ref() is called in the Socket constructor to maintain a
// reference to the Winsock context for the lifetime of each socket.
void Socket::hold_wsa_ref() {
    wsa_ref_ = ensure_wsa();
}
#else
void Socket::hold_wsa_ref() {}  // no-op on POSIX
#endif

// ─────────────────────────────────────────────────────────────
//  Factory Methods
// ─────────────────────────────────────────────────────────────

Result<Socket> Socket::create_tcp(bool ipv6) {
    (void)ensure_wsa();  // Ensure Winsock is initialized before ::socket()

    int domain = ipv6 ? AF_INET6 : AF_INET;
    NativeFd fd = ::socket(domain, SOCK_STREAM, IPPROTO_TCP);

    if (fd == kInvalidFd) {
        return make_errno_result<Socket>(
            std::string("socket(AF_INET") + (ipv6 ? "6" : "") + ", SOCK_STREAM) failed");
    }

    LOG_TRACE("Socket::create_tcp: fd=%lld ipv6=%d", (long long)fd, ipv6);
    return Socket(fd);
}

Result<Socket> Socket::create_udp(bool ipv6) {
    (void)ensure_wsa();  // Ensure Winsock is initialized before ::socket()

    int domain = ipv6 ? AF_INET6 : AF_INET;
    NativeFd fd = ::socket(domain, SOCK_DGRAM, IPPROTO_UDP);

    if (fd == kInvalidFd) {
        return make_errno_result<Socket>("socket() for UDP failed");
    }

    LOG_TRACE("Socket::create_udp: fd=%lld", (long long)fd);
    return Socket(fd);
}

// ─────────────────────────────────────────────────────────────
//  Socket Options
// ─────────────────────────────────────────────────────────────

template <typename T>
void Socket::set_option(int level, int optname, const T& value) {
    int ret = ::setsockopt(fd_, level, optname,
                           reinterpret_cast<const char*>(&value),
                           static_cast<socklen_t>(sizeof(T)));
    if (ret < 0) {
        LOG_WARN("setsockopt(level=%d, opt=%d) failed: errno=%d",
                 level, optname, sys_error());
    }
}

void Socket::set_reuse_addr(bool on) {
    int opt = on ? 1 : 0;
    set_option(SOL_SOCKET, SO_REUSEADDR, opt);
}

void Socket::set_reuse_port(bool on) {
#ifdef SO_REUSEPORT
    int opt = on ? 1 : 0;
    set_option(SOL_SOCKET, SO_REUSEPORT, opt);
#else
    (void)on;
    LOG_DEBUG("SO_REUSEPORT not available on this platform");
#endif
}

void Socket::set_keep_alive(bool on) {
    int opt = on ? 1 : 0;
    set_option(SOL_SOCKET, SO_KEEPALIVE, opt);
}

void Socket::set_tcp_nodelay(bool on) {
    int opt = on ? 1 : 0;
    set_option(IPPROTO_TCP, TCP_NODELAY, opt);
}

void Socket::set_nonblocking(bool on) {
#ifdef _WIN32
    u_long mode = on ? 1 : 0;
    if (ioctlsocket(fd_, FIONBIO, &mode) != 0) {
        LOG_ERROR("ioctlsocket(FIONBIO) failed: %d", WSAGetLastError());
    }
#else
    int flags = ::fcntl(fd_, F_GETFL, 0);
    if (flags < 0) {
        LOG_ERROR("fcntl(F_GETFL) failed: %d", errno);
        return;
    }
    if (on) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    if (::fcntl(fd_, F_SETFL, flags) < 0) {
        LOG_ERROR("fcntl(F_SETFL, O_NONBLOCK) failed: %d", errno);
    }
#endif
}

// ─────────────────────────────────────────────────────────────
//  Server Operations
// ─────────────────────────────────────────────────────────────

Result<void> Socket::bind(const Address& addr) {
    if (::bind(fd_, addr.sockaddr(), addr.socklen()) < 0) {
        return make_errno_result(
            "bind(" + addr.to_string() + ") failed");
    }
    LOG_DEBUG("Socket::bind: bound to %s", addr.to_string().c_str());
    return {};
}

Result<void> Socket::listen(int backlog) {
    if (::listen(fd_, backlog) < 0) {
        return make_errno_result("listen() failed");
    }
    LOG_DEBUG("Socket::listen: listening (backlog=%d)", backlog);
    return {};
}

Result<Socket> Socket::accept(Address* peer_addr) {
    struct sockaddr_storage ss{};
    socklen_t len = sizeof(ss);

    NativeFd conn_fd = ::accept(
        fd_,
        reinterpret_cast<struct sockaddr*>(&ss),
        &len);

    if (conn_fd == kInvalidFd) {
        int err = sys_error();
        if (err == HOUND_EAGAIN || err == HOUND_EINTR) {
            // EAGAIN in nonblocking mode: no pending connections.
            // This is NOT an error — caller should retry when epoll
            // signals readability again.
            return Result<Socket>(
                std::error_code(err, std::generic_category()),
                "accept: no pending connections (EAGAIN)");
        }
        return make_errno_result<Socket>("accept() failed");
    }

    if (peer_addr) {
        *peer_addr = Address::from_sockaddr(
            reinterpret_cast<struct sockaddr*>(&ss), len);
    }

    LOG_TRACE("Socket::accept: new connection fd=%lld", (long long)conn_fd);
    return Socket(conn_fd);
}

// ─────────────────────────────────────────────────────────────
//  Client Operations
// ─────────────────────────────────────────────────────────────

Result<void> Socket::connect(const Address& addr) {
    if (::connect(fd_, addr.sockaddr(), addr.socklen()) < 0) {
        int err = sys_error();
#ifdef _WIN32
        if (err == WSAEWOULDBLOCK) {
#else
        if (err == EINPROGRESS) {
#endif
            // Nonblocking connect in progress — the fd will become writable
            // when the connection is established.
            return {};
        }
        return make_errno_result("connect(" + addr.to_string() + ") failed");
    }
    return {};
}

// ─────────────────────────────────────────────────────────────
//  I/O Operations
// ─────────────────────────────────────────────────────────────

Result<ssize_t> Socket::read(void* buf, size_t count) {
    auto n = ::recv(fd_, static_cast<char*>(buf),
                    static_cast<int>(count), 0);

    if (n > 0) {
        return static_cast<ssize_t>(n);
    }
    if (n == 0) {
        // Peer closed connection (FIN received)
        return static_cast<ssize_t>(0);
    }

    int err = sys_error();
    if (err == HOUND_EAGAIN || err == HOUND_EINTR) {
        return Result<ssize_t>(
            std::error_code(err, std::generic_category()),
            "read: would block");
    }

    return make_errno_result<ssize_t>("read() failed");
}

Result<ssize_t> Socket::write(const void* buf, size_t count) {
    auto n = ::send(fd_, static_cast<const char*>(buf),
                    static_cast<int>(count), 0);

    if (n > 0) {
        return static_cast<ssize_t>(n);
    }

    int err = sys_error();
    if (err == HOUND_EAGAIN || err == HOUND_EINTR) {
        return Result<ssize_t>(
            std::error_code(err, std::generic_category()),
            "write: would block");
    }

    return make_errno_result<ssize_t>("write() failed");
}

Result<ssize_t> Socket::readv(const struct iovec* iov, int iovcnt) {
#ifdef _WIN32
    // Windows does not have readv. Fall back to a single recv.
    // In production, you'd use WSARecv with WSABUF arrays instead.
    if (iovcnt < 1) return static_cast<ssize_t>(0);
    return read(iov[0].iov_base, iov[0].iov_len);
#else
    auto n = ::readv(fd_, iov, iovcnt);
    if (n > 0) return static_cast<ssize_t>(n);

    if (n == 0) return static_cast<ssize_t>(0);  // EOF

    int err = errno;
    if (err == HOUND_EAGAIN || err == HOUND_EINTR) {
        return Result<ssize_t>(
            std::error_code(err, std::generic_category()),
            "readv: would block");
    }
    return make_errno_result<ssize_t>("readv() failed");
#endif
}

Result<ssize_t> Socket::writev(const struct iovec* iov, int iovcnt) {
#ifdef _WIN32
    if (iovcnt < 1) return static_cast<ssize_t>(0);
    return write(iov[0].iov_base, iov[0].iov_len);
#else
    auto n = ::writev(fd_, iov, iovcnt);
    if (n > 0) return static_cast<ssize_t>(n);

    int err = errno;
    if (err == HOUND_EAGAIN || err == HOUND_EINTR) {
        return Result<ssize_t>(
            std::error_code(err, std::generic_category()),
            "writev: would block");
    }
    return make_errno_result<ssize_t>("writev() failed");
#endif
}

// ─────────────────────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────────────────────

void Socket::close() {
    if (fd_ == kInvalidFd) return;

    LOG_TRACE("Socket::close: closing fd=%lld", (long long)fd_);

#ifdef _WIN32
    ::closesocket(fd_);
#else
    ::close(fd_);
#endif

    fd_ = kInvalidFd;
}

int Socket::get_error() const {
    int optval = 0;
    socklen_t optlen = sizeof(optval);
    if (::getsockopt(fd_, SOL_SOCKET, SO_ERROR,
                     reinterpret_cast<char*>(&optval), &optlen) < 0) {
        return sys_error();
    }
    return optval;
}

void ensure_networking_initialized() {
#ifdef _WIN32
    (void)ensure_wsa();
#endif
    // POSIX: no initialization needed
}

} // namespace hound
