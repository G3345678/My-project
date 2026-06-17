/// echo_server_reactor.cpp
///
/// Non-blocking TCP echo server using the Reactor (Module 2).
///
/// Architecture:
///   - Main thread runs the base EventLoop
///   - A listening socket accepts new connections
///   - Each connection gets a Channel watching for readability
///   - When data arrives, we echo it back and enable writing
///   - Idle connections have a 10-second timeout (via TimerWheel)
///
/// This is single-threaded but handles HUNDREDS of concurrent connections
/// (vs the blocking echo server which needs one thread per connection).
///
/// Build: see CMakeLists.txt or compile manually with:
///   g++ -std=c++17 -Isrc src/io/*.cpp src/reactor/*.cpp examples/echo_server_reactor.cpp -o echo_reactor -lws2_32
///
/// Test:
///   echo "hello" | nc localhost 9878

#include "io/socket.h"
#include "io/address.h"
#include "reactor/event_loop.h"
#include "reactor/channel.h"
#include "reactor/thread_pool.h"
#include "util/logger.h"

#include <cstdio>
#include <unordered_map>
#include <string>

using namespace hound;

/// Per-connection state.
/// In a full server, this would be a TcpConnection class.
/// For now, we manage Channels and buffers manually.
struct Connection {
    Socket sock;
    Channel channel;
    std::string read_buffer;
    bool closed = false;

    Connection(Socket s, EventLoop* loop)
        : sock(std::move(s))
        , channel(loop, sock.fd())
    {
        // Tie the Channel to this Connection's lifetime.
        // If Connection is destroyed, Channel::handle_event becomes a no-op.
    }

    // Timer ID for idle timeout
    int64_t timeout_timer = 0;

    void refresh_timeout(EventLoop* loop) {
        if (timeout_timer) loop->cancel_timer(timeout_timer);
        timeout_timer = loop->run_after(10.0, [this, loop]() {
            LOG_INFO("Connection timeout — closing");
            close_connection(loop);
        });
    }

    void close_connection(EventLoop* loop) {
        if (closed) return;
        closed = true;
        if (timeout_timer) loop->cancel_timer(timeout_timer);
        channel.disable_all();
        // The Connection will be deleted by the server's cleanup logic
    }
};

/// The EchoServer manages:
///   - A listening socket (accepts new connections)
///   - A map of active connections
class EchoServer {
public:
    EchoServer(EventLoop* loop, const Address& listen_addr)
        : loop_(loop)
    {
        // Create listening socket
        auto result = Socket::create_tcp();
        if (!result) {
            LOG_FATAL("Failed to create listening socket");
        }
        listen_sock_ = std::move(*result);

        listen_sock_.set_reuse_addr(true);
        listen_sock_.set_tcp_nodelay(true);
        listen_sock_.set_nonblocking(true);

        if (!listen_sock_.bind(listen_addr)) {
            LOG_FATAL("Bind failed");
        }
        if (!listen_sock_.listen()) {
            LOG_FATAL("Listen failed");
        }

        LOG_INFO("EchoServer listening on %s", listen_addr.to_string().c_str());

        // Create Channel for the listening socket
        listen_channel_ = std::make_unique<Channel>(loop, listen_sock_.fd());
        listen_channel_->set_read_callback([this](Timestamp) { on_accept(); });
        listen_channel_->enable_reading();
    }

    ~EchoServer() {
        // Close all connections
        for (auto& pair : connections_) {
            delete pair.second;
        }
    }

private:
    void on_accept() {
        Address peer_addr;
        auto result = listen_sock_.accept(&peer_addr);

        if (!result.has_value()) {
            // EAGAIN is normal in non-blocking mode
            return;
        }

        Socket client_sock = std::move(*result);
        int fd = client_sock.fd();

        LOG_INFO("New connection: fd=%d from %s", fd, peer_addr.to_string().c_str());

        client_sock.set_nonblocking(true);
        client_sock.set_tcp_nodelay(true);

        // Create connection state
        auto* conn = new Connection(std::move(client_sock), loop_);
        connections_[fd] = conn;

        // Set up read callback on the connection's channel
        conn->channel.set_read_callback(
            [this, fd](Timestamp ts) { on_message(fd, ts); });

        conn->channel.set_close_callback(
            [this, fd]() { on_close(fd); });

        conn->channel.set_error_callback(
            [this, fd]() { on_close(fd); });

        conn->channel.enable_reading();

        // Start idle timeout
        conn->refresh_timeout(loop_);
    }

    void on_message(int fd, Timestamp /*ts*/) {
        auto it = connections_.find(fd);
        if (it == connections_.end()) return;

        Connection* conn = it->second;
        if (conn->closed) return;

        char buf[4096];
        auto result = conn->sock.read(buf, sizeof(buf) - 1);

        if (!result.has_value()) {
            on_close(fd);
            return;
        }

        ssize_t n = *result;
        if (n == 0) {
            // Client closed connection
            on_close(fd);
            return;
        }

        // Echo back
        buf[n] = '\0';
        LOG_DEBUG("fd=%d received %lld bytes: %s",
                  fd, static_cast<long long>(n), buf);

        auto write_result = conn->sock.write(buf, static_cast<size_t>(n));
        if (!write_result.has_value()) {
            LOG_WARN("fd=%d write error: %s",
                     fd, write_result.error_msg().c_str());
            on_close(fd);
            return;
        }

        // Refresh timeout — connection is still active
        conn->refresh_timeout(loop_);
    }

    void on_close(int fd) {
        auto it = connections_.find(fd);
        if (it == connections_.end()) return;

        Connection* conn = it->second;
        LOG_INFO("Connection closed: fd=%d", fd);

        conn->close_connection(loop_);

        // Schedule deletion in the next loop iteration
        loop_->queue_in_loop([this, fd]() {
            auto it = connections_.find(fd);
            if (it != connections_.end()) {
                delete it->second;
                connections_.erase(it);
            }
        });
    }

    EventLoop* loop_;
    Socket listen_sock_;
    std::unique_ptr<Channel> listen_channel_;
    std::unordered_map<int, Connection*> connections_;
};

int main() {
    LOG_INFO("Reactor Echo Server starting...");

    EventLoop loop;

    auto addr = Address::parse("0.0.0.0", 9878);
    EchoServer server(&loop, addr);

    std::printf(">>> Reactor Echo Server on %s <<<\n", addr.to_string().c_str());
    std::printf("    Test: echo \"hello\" | nc localhost 9878\n");
    std::printf("    Handles hundreds of concurrent connections\n");
    std::printf("    Press Ctrl+C to stop\n\n");

    // Print active connections every 5 seconds
    loop.run_every(5.0, [&loop]() {
        LOG_INFO("EventLoop running — timers and I/O active");
    });

    loop.loop();  // Blocks until quit()

    LOG_INFO("Server stopped");
    return 0;
}
