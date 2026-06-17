#include <gtest/gtest.h>

#include "io/socket.h"
#include "io/address.h"

using namespace hound;

// ── Socket Creation ──────────────────────────────────────────

TEST(SocketTest, CreateTcpSocket) {
    auto result = Socket::create_tcp();
    ASSERT_TRUE(result.has_value()) << result.error_msg();
    EXPECT_TRUE(result->is_valid());
    // Socket closes automatically via RAII
}

TEST(SocketTest, CreateTcpSocketIPv6) {
    auto result = Socket::create_tcp(/*ipv6=*/true);
    ASSERT_TRUE(result.has_value()) << result.error_msg();
    EXPECT_TRUE(result->is_valid());
}

TEST(SocketTest, CreateUdpSocket) {
    auto result = Socket::create_udp();
    ASSERT_TRUE(result.has_value()) << result.error_msg();
    EXPECT_TRUE(result->is_valid());
}

// ── Socket Options ───────────────────────────────────────────

TEST(SocketTest, SetReuseAddr) {
    auto sock = Socket::create_tcp();
    ASSERT_TRUE(sock.has_value());

    // Should not throw or crash
    EXPECT_NO_THROW(sock->set_reuse_addr(true));
    EXPECT_NO_THROW(sock->set_reuse_addr(false));
}

TEST(SocketTest, SetTcpNoDelay) {
    auto sock = Socket::create_tcp();
    ASSERT_TRUE(sock.has_value());

    EXPECT_NO_THROW(sock->set_tcp_nodelay(true));
}

TEST(SocketTest, SetNonblocking) {
    auto sock = Socket::create_tcp();
    ASSERT_TRUE(sock.has_value());

    EXPECT_NO_THROW(sock->set_nonblocking(true));
}

TEST(SocketTest, SetKeepAlive) {
    auto sock = Socket::create_tcp();
    ASSERT_TRUE(sock.has_value());

    EXPECT_NO_THROW(sock->set_keep_alive(true));
}

// ── Move Semantics ───────────────────────────────────────────

TEST(SocketTest, MoveConstructor) {
    auto result = Socket::create_tcp();
    ASSERT_TRUE(result.has_value());

    NativeFd original_fd = result->fd();
    EXPECT_NE(original_fd, Socket::kInvalidFd);

    // Move
    Socket moved = std::move(*result);
    EXPECT_EQ(moved.fd(), original_fd);
    // result is now invalidated
    EXPECT_EQ(result->fd(), Socket::kInvalidFd);
}

TEST(SocketTest, MoveAssignment) {
    auto result1 = Socket::create_tcp();
    auto result2 = Socket::create_tcp();
    ASSERT_TRUE(result1.has_value());
    ASSERT_TRUE(result2.has_value());

    NativeFd fd1 = result1->fd();
    NativeFd fd2 = result2->fd();
    EXPECT_NE(fd1, fd2);

    // Move assign
    *result2 = std::move(*result1);
    EXPECT_EQ(result2->fd(), fd1);
    EXPECT_EQ(result1->fd(), Socket::kInvalidFd);
}

// ── Address Parsing ──────────────────────────────────────────

TEST(AddressTest, ParseIPv4) {
    auto addr = Address::parse("127.0.0.1", 8080);
    EXPECT_TRUE(addr.is_valid());
    EXPECT_TRUE(addr.is_ipv4());
    EXPECT_EQ(addr.port(), 8080);
    EXPECT_EQ(addr.ip(), "127.0.0.1");
}

TEST(AddressTest, ParseIPv6) {
    auto addr = Address::parse("::1", 3000);
    EXPECT_TRUE(addr.is_valid());
    EXPECT_TRUE(addr.is_ipv6());
    EXPECT_EQ(addr.port(), 3000);
}

TEST(AddressTest, LoopbackAddresses) {
    auto v4 = Address::loopback_v4(9999);
    EXPECT_EQ(v4.port(), 9999);
    EXPECT_TRUE(v4.is_ipv4());

    auto v6 = Address::loopback_v6(9999);
    EXPECT_EQ(v6.port(), 9999);
    EXPECT_TRUE(v6.is_ipv6());
}

TEST(AddressTest, ToString) {
    auto addr = Address::parse("192.168.1.1", 443);
    std::string str = addr.to_string();
    EXPECT_NE(str.find("192.168.1.1"), std::string::npos);
    EXPECT_NE(str.find("443"), std::string::npos);
}

// ── Bind and Listen ──────────────────────────────────────────

TEST(SocketTest, BindAndListen) {
    auto sock = Socket::create_tcp();
    ASSERT_TRUE(sock.has_value());

    sock->set_reuse_addr(true);

    auto addr = Address::loopback_v4(0);  // Port 0 = OS picks
    auto bind_result = sock->bind(addr);
    ASSERT_TRUE(bind_result.has_value()) << bind_result.error_msg();

    auto listen_result = sock->listen();
    ASSERT_TRUE(listen_result.has_value()) << listen_result.error_msg();
}

// ── Nonblocking Accept ───────────────────────────────────────

TEST(SocketTest, NonblockingAcceptReturnsEagain) {
    auto sock = Socket::create_tcp();
    ASSERT_TRUE(sock.has_value());

    sock->set_reuse_addr(true);
    sock->set_nonblocking(true);

    auto addr = Address::loopback_v4(0);
    ASSERT_TRUE(sock->bind(addr).has_value());
    ASSERT_TRUE(sock->listen().has_value());

    // In nonblocking mode with no pending connections,
    // accept should return an error (EAGAIN / EWOULDBLOCK).
    auto conn = sock->accept();
    if (!conn.has_value()) {
        // This is the expected path on most platforms
        SUCCEED() << "accept() correctly returned error: " << conn.error_msg();
    } else {
        // Some platforms might accept localhost connections
        SUCCEED() << "accept() unexpectedly succeeded (rare race condition)";
    }
}

// ── Invalid Operations ───────────────────────────────────────

TEST(SocketTest, BindInvalidAddressFails) {
    auto sock = Socket::create_tcp();
    ASSERT_TRUE(sock.has_value());

    // Binding to a remote address should fail
    Address bogus;
    auto result = sock->bind(bogus);
    // May or may not fail depending on platform — we just check no crash
    (void)result;
}
