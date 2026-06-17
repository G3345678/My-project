#include "io/address.h"
#include "util/logger.h"

#include <string>
#include <cstring>
#include <sstream>

#ifdef _WIN32
    // Winsock2 already included via address.h
#else
    #include <netdb.h>
    #include <arpa/inet.h>
#endif

namespace hound {

Address Address::parse(const char* ip, uint16_t port) {
    Address addr;

    // Try IPv4 first
    struct sockaddr_in* addr4 = reinterpret_cast<struct sockaddr_in*>(&addr.storage_);
    std::memset(addr4, 0, sizeof(*addr4));

    if (inet_pton(AF_INET, ip, &addr4->sin_addr) == 1) {
        addr4->sin_family = AF_INET;
        addr4->sin_port = htons(port);
        addr.socklen_ = sizeof(struct sockaddr_in);
        return addr;
    }

    // Try IPv6
    struct sockaddr_in6* addr6 = reinterpret_cast<struct sockaddr_in6*>(&addr.storage_);
    std::memset(addr6, 0, sizeof(*addr6));

    if (inet_pton(AF_INET6, ip, &addr6->sin6_addr) == 1) {
        addr6->sin6_family = AF_INET6;
        addr6->sin6_port = htons(port);
        addr.socklen_ = sizeof(struct sockaddr_in6);
        return addr;
    }

    // Neither — could be a hostname. Use getaddrinfo.
    // This is done in resolve(), not here, to keep parse() simple.
    LOG_WARN("Address::parse: invalid IP string '%s', trying as hostname", ip);
    return resolve((std::string(ip) + ":" + std::to_string(port)).c_str());
}

Address Address::from_sockaddr(const struct sockaddr* sa, socklen_t len) {
    Address addr;
    std::memcpy(&addr.storage_, sa, len);
    addr.socklen_ = len;
    return addr;
}

Address Address::resolve(const char* host_port) {
    std::string hp(host_port);

    // Split "host:port" into components
    auto pos = hp.rfind(':');
    if (pos == std::string::npos) {
        LOG_ERROR("Address::resolve: missing port in '%s'", hp.c_str());
        return Address{};
    }

    std::string host = hp.substr(0, pos);
    std::string port_str = hp.substr(pos + 1);

    // Remove brackets from IPv6 literal like [::1]:8080
    if (host.size() > 2 && host.front() == '[' && host.back() == ']') {
        host = host.substr(1, host.size() - 2);
    }

    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;      // IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;  // TCP
    hints.ai_flags = AI_PASSIVE;      // Suitable for bind()

    struct addrinfo* result = nullptr;
    int ret = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result);
    if (ret != 0 || !result) {
        LOG_ERROR("Address::resolve: getaddrinfo failed for '%s': %s",
                  hp.c_str(), gai_strerror(ret));
        return Address{};
    }

    Address addr;
    std::memcpy(&addr.storage_, result->ai_addr, result->ai_addrlen);
    addr.socklen_ = static_cast<socklen_t>(result->ai_addrlen);
    freeaddrinfo(result);

    LOG_DEBUG("Address::resolve: %s -> %s", hp.c_str(), addr.to_string().c_str());
    return addr;
}

uint16_t Address::port() const {
    if (storage_.ss_family == AF_INET) {
        auto* addr4 = reinterpret_cast<const struct sockaddr_in*>(&storage_);
        return ntohs(addr4->sin_port);
    }
    if (storage_.ss_family == AF_INET6) {
        auto* addr6 = reinterpret_cast<const struct sockaddr_in6*>(&storage_);
        return ntohs(addr6->sin6_port);
    }
    return 0;
}

std::string Address::ip() const {
    char buf[INET6_ADDRSTRLEN] = {};

    if (storage_.ss_family == AF_INET) {
        auto* addr4 = reinterpret_cast<const struct sockaddr_in*>(&storage_);
        // Windows inet_ntop takes PVOID (non-const); POSIX takes const void*.
        inet_ntop(AF_INET, const_cast<struct in_addr*>(&addr4->sin_addr), buf, sizeof(buf));
    } else if (storage_.ss_family == AF_INET6) {
        auto* addr6 = reinterpret_cast<const struct sockaddr_in6*>(&storage_);
        inet_ntop(AF_INET6, const_cast<struct in6_addr*>(&addr6->sin6_addr), buf, sizeof(buf));
    }

    return std::string(buf);
}

std::string Address::to_string() const {
    if (!is_valid()) return "(invalid)";

    std::ostringstream oss;
    if (is_ipv6()) {
        oss << "[" << ip() << "]";
    } else {
        oss << ip();
    }
    oss << ":" << port();
    return oss.str();
}

} // namespace hound
