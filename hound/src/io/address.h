#pragma once

// On Windows, inet_pton/inet_ntop require Vista+ (_WIN32_WINNT >= 0x0600).
// This MUST be defined before ANY header includes (including <string>) because
// the C++ standard library's locale/io headers may transitively include
// windows.h, which would then lack inet_pton declarations.
#ifdef _WIN32
    #ifndef _WIN32_WINNT
        #define _WIN32_WINNT 0x0600
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
#endif

#include <iosfwd>
#include <string>
#include <cstdint>

namespace hound {

/// Cross-platform IP address (IPv4 or IPv6) with port.
///
/// Wraps sockaddr_storage — the only allocation-safe way to store
/// either sockaddr_in or sockaddr_in6 in a single type.
class Address {
public:
    Address() = default;

    /// Parse a human-readable IP + port pair.
    /// Supports: "127.0.0.1", "::1", "0.0.0.0".
    static Address parse(const char* ip, uint16_t port);

    /// Create from an existing sockaddr (e.g., from accept()).
    static Address from_sockaddr(const struct sockaddr* addr, socklen_t len);

    /// Resolve a hostname:port string like "localhost:8080" or "0.0.0.0:3000".
    /// Returns an empty Address on failure.
    static Address resolve(const char* host_port);

    // ── Accessors ────────────────────────────────────────────
    const struct sockaddr* sockaddr() const {
        return reinterpret_cast<const struct sockaddr*>(&storage_);
    }

    struct sockaddr* sockaddr() {
        return reinterpret_cast<struct sockaddr*>(&storage_);
    }

    socklen_t socklen() const { return socklen_; }

    uint16_t port() const;
    std::string ip() const;
    std::string to_string() const;  // "127.0.0.1:8080" or "[::1]:8080"

    bool is_ipv4() const {
        return storage_.ss_family == AF_INET;
    }

    bool is_ipv6() const {
        return storage_.ss_family == AF_INET6;
    }

    bool is_valid() const {
        return storage_.ss_family == AF_INET ||
               storage_.ss_family == AF_INET6;
    }

    // ── Well-known addresses ─────────────────────────────────
    static Address any_ipv4(uint16_t port)    { return parse("0.0.0.0", port); }
    static Address loopback_v4(uint16_t port) { return parse("127.0.0.1", port); }
    static Address any_ipv6(uint16_t port)    { return parse("::", port); }
    static Address loopback_v6(uint16_t port) { return parse("::1", port); }

private:
    struct sockaddr_storage storage_{};
    socklen_t socklen_ = 0;
};

} // namespace hound
