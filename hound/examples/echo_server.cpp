/// echo_server.cpp
///
/// A minimal TCP echo server demonstrating the I/O layer (Module 1).
///
/// Build:
///   g++ -std=c++17 -Isrc src/io/*.cpp examples/echo_server.cpp -o echo_server -lws2_32
///
/// Test:
///   echo "hello" | nc localhost 9877

#include "io/socket.h"
#include "io/address.h"
#include "io/buffer.h"
#include "util/logger.h"

#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace hound;

void handle_client(Socket client_sock, const Address& peer_addr) {
    LOG_INFO("Client connected: %s", peer_addr.to_string().c_str());

    char buf[4096];

    for (;;) {
        auto result = client_sock.read(buf, sizeof(buf) - 1);

        if (!result.has_value()) {
            LOG_WARN("Read error: %s", result.error_msg().c_str());
            break;
        }

        ssize_t n = *result;
        if (n == 0) {
            LOG_INFO("Client disconnected: %s", peer_addr.to_string().c_str());
            break;
        }

        buf[n] = '\0';
        LOG_DEBUG("Received %lld bytes: %s",
                  static_cast<long long>(n), buf);

        auto write_result = client_sock.write(buf, static_cast<size_t>(n));
        if (!write_result.has_value()) {
            LOG_WARN("Write error: %s", write_result.error_msg().c_str());
            break;
        }
    }
}

int main() {
    LOG_INFO("Echo Server starting...");

    auto sock_result = Socket::create_tcp();
    if (!sock_result) {
        LOG_FATAL("Failed to create socket: %s", sock_result.error_msg().c_str());
    }
    auto& listen_sock = *sock_result;

    listen_sock.set_reuse_addr(true);
    listen_sock.set_tcp_nodelay(true);

    auto addr = Address::parse("0.0.0.0", 9877);
    auto bind_result = listen_sock.bind(addr);
    if (!bind_result) {
        LOG_FATAL("Bind failed: %s", bind_result.error_msg().c_str());
    }

    auto listen_result = listen_sock.listen();
    if (!listen_result) {
        LOG_FATAL("Listen failed: %s", listen_result.error_msg().c_str());
    }

    LOG_INFO("Echo server listening on %s", addr.to_string().c_str());
    std::printf(">>> Echo server running on %s <<<\n", addr.to_string().c_str());
    std::printf("    Test with: echo \"hello\" | nc localhost 9877\n");
    std::printf("    Press Ctrl+C to stop\n\n");

    std::vector<std::thread> threads;

    for (;;) {
        Address peer_addr;
        auto client = listen_sock.accept(&peer_addr);

        if (!client.has_value()) {
            LOG_WARN("Accept error: %s", client.error_msg().c_str());
            continue;
        }

        threads.emplace_back(handle_client, std::move(*client), peer_addr);
        threads.back().detach();

        LOG_DEBUG("Active connections: ~%zu", threads.size());
    }

    return 0;
}
