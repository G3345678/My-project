#pragma once

#include <string>
#include <unordered_map>
#include <map>
#include <sstream>

// Windows headers define DELETE as a macro (0x00010000L)
#ifdef DELETE
    #undef DELETE
#endif

namespace hound {
namespace http {

/// Parsed HTTP request.
///
/// All string members are copies of data from the I/O buffer.
/// This is deliberate — the HTTP parser zero-copies during scanning
/// (using string_view into the buffer), but the final parsed values
/// are copied so the buffer can be freed immediately after parsing.
class HttpRequest {
public:
    enum Method {
        GET, POST, PUT, HTTP_DELETE, HEAD, OPTIONS, PATCH, UNKNOWN
    };

    // Alias: use Request::DELETE in code (maps to HTTP_DELETE internally)
    static constexpr Method DELETE = HTTP_DELETE;

    // ── Setters (called by the parser) ───────────────────────
    void set_method(Method m) { method_ = m; }
    void set_path(const std::string& p) { path_ = p; }
    void set_version(const std::string& v) { version_ = v; }
    void set_body(const std::string& b) { body_ = b; }

    void add_header(const std::string& key, const std::string& value) {
        headers_[key] = value;
    }

    /// Called when parsing is complete to extract query string from path.
    void finalize();

    // ── Accessors ────────────────────────────────────────────
    Method method() const { return method_; }
    const std::string& path() const { return path_; }
    const std::string& query_string() const { return query_; }
    const std::string& version() const { return version_; }
    const std::string& body() const { return body_; }

    /// Get a header value. Returns empty string if not present.
    std::string header(const std::string& key) const {
        auto it = headers_.find(key);
        return it != headers_.end() ? it->second : std::string{};
    }

    /// Get all headers.
    const std::unordered_map<std::string, std::string>& headers() const {
        return headers_;
    }

    /// Parse query string into key-value pairs.
    std::map<std::string, std::string> query_params() const;

    /// Parse URL-encoded form body.
    std::map<std::string, std::string> form_data() const;

    /// Get a cookie by name.
    std::string cookie(const std::string& name) const;

    /// Connection should be kept alive after this request?
    bool keep_alive() const { return keep_alive_; }

    // ── Utility ──────────────────────────────────────────────
    static const char* method_str(Method m);
    static Method method_from_str(const std::string& s);

private:
    Method method_ = UNKNOWN;
    std::string path_;
    std::string query_;
    std::string version_ = "HTTP/1.1";
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
    bool keep_alive_ = true;
};

} // namespace http
} // namespace hound
