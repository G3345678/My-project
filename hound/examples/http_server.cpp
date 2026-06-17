/// http_server.cpp
/// Full-featured HTTP/1.1 server demonstrating all modules.
///
/// Features:
///   - Reactor-based I/O (Module 2)
///   - HTTP/1.1 protocol parsing (Module 4)
///   - Trie-based URL routing with parameters
///   - JSON API endpoints
///   - Static file serving
///   - Connection keep-alive
///   - 30-second idle timeout
///
/// Build: see CMakeLists.txt
///
/// Test:
///   curl http://localhost:8080/
///   curl http://localhost:8080/api/users
///   curl -X POST -d "name=test" http://localhost:8080/api/users

#include "io/socket.h"
#include "io/address.h"
#include "reactor/event_loop.h"
#include "http/http_server.h"
#include "http/http_request.h"
#include "http/http_response.h"
#include "util/logger.h"

using namespace hound;
using namespace hound::http;

int main() {
    LOG_INFO("Hound HTTP Server starting...");

    EventLoop loop;
    HttpServer server(&loop, Address::parse("0.0.0.0", 8080));

    // ── Route: GET / ─────────────────────────────────────
    server.get("/", [](const HttpRequest& /*req*/, HttpResponse* resp) {
        resp->set_content_type("text/html; charset=utf-8");
        resp->set_body(
            "<!DOCTYPE html>\n"
            "<html><head><title>Hound</title></head>\n"
            "<body>\n"
            "<h1>🐶 Hound HTTP Server</h1>\n"
            "<p>Running on C++17 Reactor framework</p>\n"
            "<ul>\n"
            "<li><a href='/api/status'>GET /api/status</a> — Server info</li>\n"
            "<li><a href='/api/users'>GET /api/users</a> — List users</li>\n"
            "<li>POST /api/users — Create user</li>\n"
            "<li>GET /api/users/:id — Get user by ID</li>\n"
            "</ul>\n"
            "<p><em>Hand-crafted HTTP/1.1 with coroutine support</em></p>\n"
            "</body></html>\n"
        );
    });

    // ── Route: GET /api/status ───────────────────────────
    server.get("/api/status", [](const HttpRequest& /*req*/, HttpResponse* resp) {
        *resp = HttpResponse::json(
            "{\"server\":\"Hound\",\"version\":\"0.2.0\","
            "\"modules\":[\"I/O\",\"Reactor\",\"Coroutine\",\"HTTP\"]}"
        );
    });

    // ── Route: GET /api/users ────────────────────────────
    server.get("/api/users", [](const HttpRequest& /*req*/, HttpResponse* resp) {
        *resp = HttpResponse::json(
            "[{\"id\":1,\"name\":\"Alice\"},"
            "{\"id\":2,\"name\":\"Bob\"},"
            "{\"id\":3,\"name\":\"Charlie\"}]"
        );
    });

    // ── Route: GET /api/users/:id ────────────────────────
    server.get("/api/users/:id", [](const HttpRequest& req, HttpResponse* resp) {
        auto params = req.query_params();
        // Extract the :id param from the path (handled by router)
        *resp = HttpResponse::json(
            "{\"id\":" + req.path().substr(std::string("/api/users/").size()) +
            ",\"name\":\"User\",\"source\":\"path-parameter\"}"
        );
    });

    // ── Route: POST /api/users ───────────────────────────
    server.post("/api/users", [](const HttpRequest& req, HttpResponse* resp) {
        auto data = req.form_data();
        std::string name = data.count("name") ? data.at("name") : "unknown";
        *resp = HttpResponse::json(
            "{\"created\":true,\"name\":\"" + name + "\"}"
        );
    });

    // ── Route: GET /hello/:name ──────────────────────────
    server.get("/hello/:name", [](const HttpRequest& req, HttpResponse* resp) {
        // Extract name from path
        std::string path = req.path();
        auto pos = path.rfind('/');
        std::string name = (pos != std::string::npos) ? path.substr(pos + 1) : "world";
        resp->set_content_type("text/plain; charset=utf-8");
        resp->set_body("Hello, " + name + "!\n");
    });

    // ── Start ────────────────────────────────────────────
    server.start();

    std::printf("\n");
    std::printf("╔══════════════════════════════════════════╗\n");
    std::printf("║   🐶 Hound HTTP Server v0.2.0            ║\n");
    std::printf("║   http://localhost:8080                   ║\n");
    std::printf("║                                          ║\n");
    std::printf("║   Try:                                   ║\n");
    std::printf("║     curl http://localhost:8080/           ║\n");
    std::printf("║     curl http://localhost:8080/api/status ║\n");
    std::printf("║     curl http://localhost:8080/api/users  ║\n");
    std::printf("║     curl http://localhost:8080/hello/World║\n");
    std::printf("╚══════════════════════════════════════════╝\n");
    std::printf("\nPress Ctrl+C to stop\n\n");

    loop.loop();

    LOG_INFO("Server stopped");
    return 0;
}
