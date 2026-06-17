#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <sstream>

namespace hound {
namespace http {

/// HTTP response builder.
///
/// Usage:
///   HttpResponse resp;
///   resp.set_status(200, "OK");
///   resp.set_header("Content-Type", "text/html");
///   resp.set_body("<h1>Hello</h1>");
///   std::string raw = resp.serialize();  // Ready to send
///
/// The serialize() output is a complete HTTP/1.1 response including
/// status line, headers, and body.
class HttpResponse {
public:
    HttpResponse() = default;

    // ── Status ────────────────────────────────────────────────
    void set_status(int code, const std::string& message) {
        status_code_ = code;
        status_message_ = message;
    }

    int status_code() const { return status_code_; }
    const std::string& status_message() const { return status_message_; }

    // ── Headers ───────────────────────────────────────────────
    void set_header(const std::string& key, const std::string& value) {
        headers_[key] = value;
    }

    void set_content_type(const std::string& ct) {
        set_header("Content-Type", ct);
    }

    // ── Body ──────────────────────────────────────────────────
    void set_body(const std::string& body) {
        body_ = body;
        set_header("Content-Length", std::to_string(body_.size()));
    }

    void append_body(const std::string& chunk) {
        body_ += chunk;
        set_header("Content-Length", std::to_string(body_.size()));
    }

    // ── Cookies ───────────────────────────────────────────────
    void set_cookie(const std::string& name, const std::string& value,
                    const std::string& path = "/",
                    int max_age = 0,
                    bool http_only = true) {
        std::string cookie = name + "=" + value + "; Path=" + path;
        if (max_age > 0) {
            cookie += "; Max-Age=" + std::to_string(max_age);
        }
        if (http_only) {
            cookie += "; HttpOnly";
        }
        cookies_.push_back(cookie);
    }

    // ── Keep-Alive ────────────────────────────────────────────
    void set_keep_alive(bool on) {
        if (on) {
            set_header("Connection", "keep-alive");
        } else {
            set_header("Connection", "close");
        }
    }

    // ── Serialization ─────────────────────────────────────────
    /// Serialize the entire response to a string ready for send().
    std::string serialize() const;

    // ── Convenience Factories ─────────────────────────────────
    static HttpResponse ok(const std::string& body = "",
                           const std::string& content_type = "text/plain");

    static HttpResponse not_found(const std::string& body = "Not Found");

    static HttpResponse internal_error(const std::string& body = "Internal Server Error");

    static HttpResponse redirect(const std::string& location, int code = 302);

    static HttpResponse json(const std::string& json_body);

private:
    int status_code_ = 200;
    std::string status_message_ = "OK";
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
    std::vector<std::string> cookies_;
};

} // namespace http
} // namespace hound
