#include "http/http_response.h"

namespace hound {
namespace http {

std::string HttpResponse::serialize() const {
    std::ostringstream oss;

    // Status line: "HTTP/1.1 200 OK\r\n"
    oss << "HTTP/1.1 " << status_code_ << " " << status_message_ << "\r\n";

    // Headers
    for (const auto& h : headers_) {
        oss << h.first << ": " << h.second << "\r\n";
    }

    // Cookies (Set-Cookie headers)
    for (const auto& cookie : cookies_) {
        oss << "Set-Cookie: " << cookie << "\r\n";
    }

    // Empty line separates headers from body
    oss << "\r\n";

    // Body
    oss << body_;

    return oss.str();
}

// ── Convenience Factories ─────────────────────────────────────

HttpResponse HttpResponse::ok(const std::string& body,
                               const std::string& content_type) {
    HttpResponse resp;
    resp.set_status(200, "OK");
    resp.set_content_type(content_type);
    if (!body.empty()) resp.set_body(body);
    return resp;
}

HttpResponse HttpResponse::not_found(const std::string& body) {
    HttpResponse resp;
    resp.set_status(404, "Not Found");
    resp.set_content_type("text/plain");
    resp.set_body(body);
    return resp;
}

HttpResponse HttpResponse::internal_error(const std::string& body) {
    HttpResponse resp;
    resp.set_status(500, "Internal Server Error");
    resp.set_content_type("text/plain");
    resp.set_body(body);
    return resp;
}

HttpResponse HttpResponse::redirect(const std::string& location, int code) {
    HttpResponse resp;
    resp.set_status(code, code == 301 ? "Moved Permanently" : "Found");
    resp.set_header("Location", location);
    return resp;
}

HttpResponse HttpResponse::json(const std::string& json_body) {
    return ok(json_body, "application/json; charset=utf-8");
}

} // namespace http
} // namespace hound
