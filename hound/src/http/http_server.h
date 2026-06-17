#pragma once

#include "io/socket.h"
#include "io/address.h"
#include "io/buffer.h"
#include "reactor/event_loop.h"
#include "reactor/channel.h"
#include "http/http_parser.h"
#include "http/http_request.h"
#include "http/http_response.h"
#include "http/router.h"

#include <unordered_map>
#include <memory>
#include <string>

namespace hound {
namespace http {

/// HTTP/1.1 server built on the Reactor framework.
///
/// # Architecture
///
///   HttpServer
///   ├── Listening Socket (accepts connections)
///   ├── Router (Trie-based URL dispatch)
///   ├── Per-connection state (parser + buffer + fd)
///   └── EventLoop integration
///
/// # Usage
///
///   EventLoop loop;
///   HttpServer server(&loop, Address::parse("0.0.0.0", 8080));
///
///   server.get("/", [](auto& req, auto* resp) {
///       resp->set_body("Hello World!");
///   });
///
///   server.post("/api/data", [](auto& req, auto* resp) {
///       auto data = req.body();
///       resp->set_content_type("application/json");
///       resp->set_body("{\"status\": \"ok\"}");
///   });
///
///   server.set_static_dir("./www");  // Serve static files
///   server.start();
///   loop.loop();
class HttpServer {
public:
    /// Create an HTTP server.
    /// @param loop         The EventLoop to run on
    /// @param listen_addr  Address to bind to
    HttpServer(EventLoop* loop, const Address& listen_addr);

    ~HttpServer();

    // ── Route Registration ───────────────────────────────────
    void get(const std::string& pattern, Handler handler) {
        router_.add("GET", pattern, std::move(handler));
    }

    void post(const std::string& pattern, Handler handler) {
        router_.add("POST", pattern, std::move(handler));
    }

    void put(const std::string& pattern, Handler handler) {
        router_.add("PUT", pattern, std::move(handler));
    }

    void del(const std::string& pattern, Handler handler) {
        router_.add("DELETE", pattern, std::move(handler));
    }

    // ── Static Files ─────────────────────────────────────────
    /// Set the directory to serve static files from.
    /// Files are served under /<path> relative to this directory.
    void set_static_dir(const std::string& dir) {
        static_dir_ = dir;
    }

    // ── Lifecycle ────────────────────────────────────────────
    /// Start accepting connections.
    void start();

    /// Stop the server and close all connections.
    void stop();

    const Address& listen_addr() const { return listen_addr_; }

private:
    // ── Connection State ─────────────────────────────────────
    struct Connection {
        Socket sock;
        Channel channel;
        Buffer read_buf;
        HttpParser parser;
        int64_t timeout_timer = 0;
        bool closed = false;

        Connection(Socket s, EventLoop* loop)
            : sock(std::move(s)), channel(loop, sock.fd()) {}

        void refresh_timeout(HttpServer* server);
    };

    // ── Event Handlers ───────────────────────────────────────
    void on_accept();
    void on_message(int fd);
    void on_close(int fd);
    void handle_request(Connection* conn);
    void send_response(Connection* conn, const HttpResponse& resp);

    EventLoop* loop_;
    Address listen_addr_;
    Socket listen_sock_;
    std::unique_ptr<Channel> listen_channel_;
    Router router_;
    std::string static_dir_;

    std::unordered_map<int, std::unique_ptr<Connection>> connections_;
    bool running_ = false;
};

} // namespace http
} // namespace hound
