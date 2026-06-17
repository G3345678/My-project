#include "http/http_server.h"
#include "util/logger.h"

#include <fstream>
#include <sstream>
#include <cstdio>

#ifdef _WIN32
    #include <winsock2.h>
#else
    #include <sys/sendfile.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
#endif

namespace hound {
namespace http {

// ─────────────────────────────────────────────────────────────
//  Construction
// ─────────────────────────────────────────────────────────────

HttpServer::HttpServer(EventLoop* loop, const Address& listen_addr)
    : loop_(loop)
    , listen_addr_(listen_addr)
{
    LOG_INFO("HttpServer created");
}

HttpServer::~HttpServer() {
    stop();
}

// ─────────────────────────────────────────────────────────────
//  Start / Stop
// ─────────────────────────────────────────────────────────────

void HttpServer::start() {
    if (running_) return;

    // Create listening socket
    auto result = Socket::create_tcp();
    if (!result) {
        LOG_FATAL("HttpServer: failed to create socket");
    }
    listen_sock_ = std::move(*result);

    listen_sock_.set_reuse_addr(true);
    listen_sock_.set_tcp_nodelay(true);
    listen_sock_.set_nonblocking(true);

    if (!listen_sock_.bind(listen_addr_)) {
        LOG_FATAL("HttpServer: bind failed");
    }
    if (!listen_sock_.listen()) {
        LOG_FATAL("HttpServer: listen failed");
    }

    LOG_INFO("HttpServer listening on %s", listen_addr_.to_string().c_str());

    // Set up accept Channel
    listen_channel_ = std::make_unique<Channel>(loop_, listen_sock_.fd());
    listen_channel_->set_read_callback([this](Timestamp) { on_accept(); });
    listen_channel_->enable_reading();

    running_ = true;
}

void HttpServer::stop() {
    if (!running_) return;

    running_ = false;

    // Close all connections
    for (auto& pair : connections_) {
        auto& conn = pair.second;
        if (conn->timeout_timer) {
            loop_->cancel_timer(conn->timeout_timer);
        }
        conn->channel.disable_all();
        conn->sock.close();
    }
    connections_.clear();

    listen_channel_->disable_all();
    listen_channel_.reset();
    listen_sock_.close();

    LOG_INFO("HttpServer stopped");
}

// ─────────────────────────────────────────────────────────────
//  Connection Management
// ─────────────────────────────────────────────────────────────

void HttpServer::Connection::refresh_timeout(HttpServer* server) {
    if (timeout_timer) server->loop_->cancel_timer(timeout_timer);
    timeout_timer = server->loop_->run_after(30.0, [this, server]() {
        LOG_INFO("HTTP connection %d timed out", channel.fd());
        server->on_close(channel.fd());
    });
}

void HttpServer::on_accept() {
    Address peer_addr;
    auto result = listen_sock_.accept(&peer_addr);

    if (!result.has_value()) {
        // EAGAIN — no connections pending
        return;
    }

    Socket client_sock = std::move(*result);
    int fd = client_sock.fd();

    LOG_INFO("HTTP connection from %s (fd=%d)", peer_addr.to_string().c_str(), fd);

    client_sock.set_nonblocking(true);
    client_sock.set_tcp_nodelay(true);

    auto conn = std::make_unique<Connection>(std::move(client_sock), loop_);

    // Set up read callback
    conn->channel.set_read_callback([this, fd](Timestamp) { on_message(fd); });
    conn->channel.set_close_callback([this, fd]() { on_close(fd); });
    conn->channel.set_error_callback([this, fd]() { on_close(fd); });
    conn->channel.enable_reading();

    conn->refresh_timeout(this);

    connections_[fd] = std::move(conn);
}

void HttpServer::on_message(int fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;

    auto& conn = it->second;
    if (conn->closed) return;

    // Read data into buffer
    size_t writable = 0;
    char* ptr = conn->read_buf.begin_write(&writable);
    if (!ptr || writable == 0) {
        LOG_WARN("HTTP: buffer full for fd=%d", fd);
        on_close(fd);
        return;
    }

    auto read_result = conn->sock.read(ptr, writable);
    if (!read_result.has_value()) {
        on_close(fd);
        return;
    }

    ssize_t n = *read_result;
    if (n == 0) {
        on_close(fd);
        return;
    }

    conn->read_buf.has_written(static_cast<size_t>(n));
    conn->refresh_timeout(this);

    // Parse as many requests as we can from the buffer
    while (conn->read_buf.size() > 0) {
        size_t consumed = conn->parser.parse(&conn->read_buf);

        if (conn->parser.has_error()) {
            LOG_WARN("HTTP parse error on fd=%d", fd);
            send_response(conn.get(),
                HttpResponse::not_found("400 Bad Request"));
            on_close(fd);
            return;
        }

        if (conn->parser.is_done()) {
            handle_request(conn.get());
            conn->parser.reset();

            // HTTP pipelining: continue parsing next request
            if (!conn->parser.is_done()) {
                continue;
            }
        }

        if (consumed == 0) {
            break;  // Need more data
        }
    }
}

void HttpServer::on_close(int fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;

    auto& conn = it->second;
    if (conn->closed) return;

    conn->closed = true;

    LOG_DEBUG("HTTP connection closed: fd=%d", fd);

    if (conn->timeout_timer) {
        loop_->cancel_timer(conn->timeout_timer);
    }

    conn->channel.disable_all();

    // Schedule deletion
    loop_->queue_in_loop([this, fd]() {
        connections_.erase(fd);
    });
}

// ─────────────────────────────────────────────────────────────
//  Request Handling
// ─────────────────────────────────────────────────────────────

void HttpServer::handle_request(Connection* conn) {
    auto& req = conn->parser.request();
    HttpResponse resp;

    // Try router first
    auto handler = router_.route(req, &resp);
    if (handler) {
        handler(req, &resp);
    }
    // If no handler and resp is still default (404 was set by router),
    // check static files
    else if (!static_dir_.empty() && req.method() == HttpRequest::GET) {
        // Try serving static file
        std::string filepath = static_dir_ + req.path();
        std::ifstream file(filepath, std::ios::binary);
        if (file.is_open()) {
            std::ostringstream ss;
            ss << file.rdbuf();
            resp = HttpResponse::ok(ss.str(), "text/html");
        }
        // If file not found, resp already has 404 from router
    }

    // Set keep-alive based on request
    resp.set_keep_alive(req.keep_alive());

    send_response(conn, resp);

    // If client wants close, do it
    if (!req.keep_alive()) {
        on_close(conn->channel.fd());
    }
}

void HttpServer::send_response(Connection* conn, const HttpResponse& resp) {
    std::string raw = resp.serialize();
    auto result = conn->sock.write(raw.data(), raw.size());

    if (!result.has_value()) {
        LOG_WARN("HTTP: write error on fd=%d", conn->channel.fd());
        on_close(conn->channel.fd());
        return;
    }

    LOG_DEBUG("HTTP: sent %d response (%zu bytes)",
              resp.status_code(), raw.size());
}

} // namespace http
} // namespace hound
