#pragma once

// Windows macros that conflict with our enum values
#ifdef ERROR
    #undef ERROR
#endif

#include "io/buffer.h"
#include "http/http_request.h"

#include <string_view>
#include <functional>

namespace hound {
namespace http {

/// HTTP/1.1 request parser — state machine implementation.
///
/// # Why a state machine?
///
/// HTTP parsing sits on the hot path. Every byte of every request
/// goes through the parser. A hand-written state machine:
///   - Makes zero allocations during scanning (uses string_view into Buffer)
///   - Processes one byte at a time with no backtracking
///   - Is trivially bufferable (resume from any state when more data arrives)
///
/// # RFC 7230 compliance
///
/// This parser handles the mandatory subset of HTTP/1.1:
///   - Request line parsing (method SP URI SP version CRLF)
///   - Header parsing (name: value CRLF)
///   - Content-Length body (fixed-size)
///   - Chunked transfer encoding
///   - Connection: keep-alive / close
///
/// # State Machine Diagram
///
///   START → REQUEST_LINE → HEADERS → BODY → DONE
///              ↓              ↓        ↓
///            ERROR          ERROR    ERROR
///
/// Each state consumes bytes until it has enough data to transition
/// to the next state. If the buffer runs out mid-state, the parser
/// returns and resumes when more data arrives.
class HttpParser {
public:
    enum State {
        START,
        REQUEST_LINE,
        HEADERS,
        BODY,
        CHUNKED_BODY,
        DONE,
        ERROR
    };

    HttpParser();

    /// Feed data to the parser. Returns the number of bytes consumed.
    /// When the parser returns DONE (or ERROR), the request is ready.
    /// Remaining bytes in the buffer belong to the next request (pipelining).
    size_t parse(Buffer* buf);

    // ── State ────────────────────────────────────────────────
    State state() const { return state_; }
    bool is_done() const { return state_ == DONE; }
    bool has_error() const { return state_ == ERROR; }

    /// Take ownership of the parsed request and reset parser for next use.
    HttpRequest take_request();

    /// Reset parser for a new request.
    void reset();

    /// Current parsed request (read-only, being built).
    const HttpRequest& request() const { return request_; }

private:
    // ── Per-state parsers ────────────────────────────────────
    size_t parse_request_line(const char* data, size_t len);
    size_t parse_headers(const char* data, size_t len);
    size_t parse_body(const char* data, size_t len);
    size_t parse_chunked_body(const char* data, size_t len);

    // ── Helpers ──────────────────────────────────────────────
    /// Find a character in [data, data+len). Returns pointer or nullptr.
    static const char* find_char(const char* data, size_t len, char c);

    /// Find "\r\n" in [data, data+len). Returns pointer to '\r' or nullptr.
    static const char* find_crlf(const char* data, size_t len);

    State state_ = START;
    HttpRequest request_;

    // Parsing state
    std::string current_header_key_;
    size_t content_length_ = 0;
    size_t body_read_ = 0;
    std::string temp_body_;

    // Chunked state
    size_t chunk_size_ = 0;
    bool reading_chunk_size_ = true;
    bool chunked_ = false;
};

} // namespace http
} // namespace hound
