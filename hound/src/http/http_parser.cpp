#include "http/http_parser.h"
#include "util/logger.h"

#include <cstring>
#include <cstdlib>
#include <algorithm>

namespace hound {
namespace http {

HttpParser::HttpParser() {
    reset();
}

void HttpParser::reset() {
    state_ = START;
    request_ = HttpRequest{};
    current_header_key_.clear();
    content_length_ = 0;
    body_read_ = 0;
    temp_body_.clear();
    chunk_size_ = 0;
    reading_chunk_size_ = true;
    chunked_ = false;
}

HttpRequest HttpParser::take_request() {
    request_.finalize();
    return std::move(request_);
}

const char* HttpParser::find_char(const char* data, size_t len, char c) {
    return static_cast<const char*>(std::memchr(data, c, len));
}

const char* HttpParser::find_crlf(const char* data, size_t len) {
    for (size_t i = 0; i + 1 < len; ++i) {
        if (data[i] == '\r' && data[i + 1] == '\n') {
            return data + i;
        }
    }
    return nullptr;
}

// ─────────────────────────────────────────────────────────────
//  Main parse loop
// ─────────────────────────────────────────────────────────────

size_t HttpParser::parse(Buffer* buf) {
    size_t total_consumed = 0;

    while (buf->size() > 0) {
        const char* data = buf->peek_data();
        size_t available = buf->peek_size();
        size_t consumed = 0;

        switch (state_) {
            case START:
            case REQUEST_LINE:
                consumed = parse_request_line(data, available);
                break;
            case HEADERS:
                consumed = parse_headers(data, available);
                break;
            case BODY:
                consumed = parse_body(data, available);
                break;
            case CHUNKED_BODY:
                consumed = parse_chunked_body(data, available);
                break;
            case DONE:
            case ERROR:
                return total_consumed;
        }

        if (consumed == 0) {
            // Not enough data in current block — need more
            break;
        }

        buf->retrieve(consumed);
        total_consumed += consumed;
    }

    return total_consumed;
}

// ─────────────────────────────────────────────────────────────
//  Request Line: METHOD SP PATH SP VERSION CRLF
// ─────────────────────────────────────────────────────────────

size_t HttpParser::parse_request_line(const char* data, size_t len) {
    // Find the CRLF that terminates the request line
    const char* crlf = find_crlf(data, len);
    if (!crlf) {
        return 0;  // Need more data
    }

    size_t line_len = static_cast<size_t>(crlf - data);
    std::string_view line(data, line_len);

    // Parse: METHOD SP URI SP VERSION
    auto sp1 = line.find(' ');
    if (sp1 == std::string_view::npos) {
        LOG_WARN("HTTP parser: no space after method");
        state_ = ERROR;
        return 0;
    }

    auto sp2 = line.find(' ', sp1 + 1);
    if (sp2 == std::string_view::npos) {
        LOG_WARN("HTTP parser: no space after URI");
        state_ = ERROR;
        return 0;
    }

    std::string method(line.substr(0, sp1));
    std::string path(line.substr(sp1 + 1, sp2 - sp1 - 1));
    std::string version(line.substr(sp2 + 1));

    request_.set_method(HttpRequest::method_from_str(method));
    request_.set_path(path);
    request_.set_version(version);

    LOG_DEBUG("HTTP parser: %s %s %s", method.c_str(), path.c_str(), version.c_str());

    state_ = HEADERS;
    return line_len + 2;  // +2 for CRLF
}

// ─────────────────────────────────────────────────────────────
//  Headers: Name: Value CRLF ... CRLF (empty line = end)
// ─────────────────────────────────────────────────────────────

size_t HttpParser::parse_headers(const char* data, size_t len) {
    size_t consumed_total = 0;

    while (consumed_total < len) {
        const char* current = data + consumed_total;
        size_t remaining = len - consumed_total;

        // Check for empty line (end of headers)
        if (remaining >= 2 && current[0] == '\r' && current[1] == '\n') {
            consumed_total += 2;

            // Headers done — determine body handling
            auto transfer_encoding = request_.header("Transfer-Encoding");
            if (transfer_encoding.find("chunked") != std::string::npos) {
                chunked_ = true;
                state_ = CHUNKED_BODY;
            } else {
                auto cl = request_.header("Content-Length");
                if (!cl.empty()) {
                    content_length_ = static_cast<size_t>(std::strtoul(cl.c_str(), nullptr, 10));
                    state_ = BODY;
                } else {
                    state_ = DONE;  // No body
                }
            }

            LOG_DEBUG("HTTP parser: headers done, content-length=%zu, chunked=%d",
                      content_length_, chunked_);
            return consumed_total;
        }

        // Find CRLF for this header line
        const char* crlf = find_crlf(current, remaining);
        if (!crlf) {
            return consumed_total;  // Need more data
        }

        size_t line_len = static_cast<size_t>(crlf - current);
        std::string_view line(current, line_len);

        // Parse: Name: Value
        auto colon = line.find(':');
        if (colon != std::string_view::npos) {
            // Trim leading space after colon
            size_t value_start = colon + 1;
            while (value_start < line.size() && line[value_start] == ' ') {
                ++value_start;
            }

            std::string key(line.substr(0, colon));
            std::string value(line.substr(value_start));

            request_.add_header(key, value);
        }

        consumed_total += line_len + 2;  // +2 for CRLF
    }

    return consumed_total;
}

// ─────────────────────────────────────────────────────────────
//  Fixed-length body
// ─────────────────────────────────────────────────────────────

size_t HttpParser::parse_body(const char* data, size_t len) {
    size_t needed = content_length_ - body_read_;
    size_t to_consume = std::min(needed, len);

    temp_body_.append(data, to_consume);
    body_read_ += to_consume;

    if (body_read_ >= content_length_) {
        request_.set_body(temp_body_);
        state_ = DONE;
        LOG_DEBUG("HTTP parser: body complete (%zu bytes)", body_read_);
    }

    return to_consume;
}

// ─────────────────────────────────────────────────────────────
//  Chunked transfer encoding
//  Format: <hex_size>\r\n<data>\r\n ... 0\r\n\r\n
// ─────────────────────────────────────────────────────────────

size_t HttpParser::parse_chunked_body(const char* data, size_t len) {
    size_t consumed_total = 0;

    while (consumed_total < len) {
        const char* current = data + consumed_total;
        size_t remaining = len - consumed_total;

        if (reading_chunk_size_) {
            // Read chunk size line
            const char* crlf = find_crlf(current, remaining);
            if (!crlf) {
                return consumed_total;  // Need more data
            }

            std::string_view size_line(current, static_cast<size_t>(crlf - current));
            chunk_size_ = static_cast<size_t>(
                std::strtoul(size_line.data(), nullptr, 16));
            consumed_total += static_cast<size_t>(crlf - current) + 2;

            if (chunk_size_ == 0) {
                // Final chunk — read trailing CRLF and finish
                state_ = DONE;
                request_.set_body(temp_body_);
                LOG_DEBUG("HTTP parser: chunked body complete (%zu bytes total)",
                          temp_body_.size());
                return consumed_total;
            }

            reading_chunk_size_ = false;
        } else {
            // Read chunk data
            size_t data_needed = chunk_size_ + 2;  // data + trailing CRLF
            size_t available = remaining;

            if (available < data_needed) {
                // Not enough data for the full chunk + CRLF
                // Read what we can
                size_t take = std::min(available, chunk_size_);
                temp_body_.append(current, take);
                chunk_size_ -= take;
                consumed_total += take;
                return consumed_total;
            }

            // Full chunk + CRLF available
            temp_body_.append(current, chunk_size_);
            consumed_total += chunk_size_ + 2;  // data + CRLF
            reading_chunk_size_ = true;
        }
    }

    return consumed_total;
}

} // namespace http
} // namespace hound
