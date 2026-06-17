#include "http/http_request.h"

#include <cstring>
#include <algorithm>
#include <sstream>

namespace hound {
namespace http {

void HttpRequest::finalize() {
    // Extract query string from path: /path?key=value → path=/path, query=key=value
    auto pos = path_.find('?');
    if (pos != std::string::npos) {
        query_ = path_.substr(pos + 1);
        path_  = path_.substr(0, pos);
    }

    // Determine keep-alive based on HTTP version and Connection header
    auto conn = header("Connection");
    if (version_ == "HTTP/1.0") {
        keep_alive_ = (conn == "keep-alive" || conn == "Keep-Alive");
    } else {
        // HTTP/1.1: default is keep-alive, unless explicitly "close"
        keep_alive_ = (conn != "close" && conn != "Close");
    }
}

std::map<std::string, std::string> HttpRequest::query_params() const {
    std::map<std::string, std::string> params;
    if (query_.empty()) return params;

    std::istringstream iss(query_);
    std::string pair;
    while (std::getline(iss, pair, '&')) {
        auto pos = pair.find('=');
        if (pos != std::string::npos) {
            params[pair.substr(0, pos)] = pair.substr(pos + 1);
        } else {
            params[pair] = "";
        }
    }
    return params;
}

std::map<std::string, std::string> HttpRequest::form_data() const {
    std::map<std::string, std::string> data;
    // Check content type
    auto ct = header("Content-Type");
    if (ct.find("application/x-www-form-urlencoded") == std::string::npos) {
        return data;
    }

    std::istringstream iss(body_);
    std::string pair;
    while (std::getline(iss, pair, '&')) {
        auto pos = pair.find('=');
        if (pos != std::string::npos) {
            data[pair.substr(0, pos)] = pair.substr(pos + 1);
        } else {
            data[pair] = "";
        }
    }
    return data;
}

std::string HttpRequest::cookie(const std::string& name) const {
    auto cookie_header = header("Cookie");
    if (cookie_header.empty()) return {};

    std::istringstream iss(cookie_header);
    std::string item;
    while (std::getline(iss, item, ';')) {
        // Trim leading space
        size_t start = 0;
        while (start < item.size() && item[start] == ' ') ++start;
        item = item.substr(start);

        auto pos = item.find('=');
        if (pos != std::string::npos && item.substr(0, pos) == name) {
            return item.substr(pos + 1);
        }
    }
    return {};
}

const char* HttpRequest::method_str(Method m) {
    switch (m) {
        case GET:     return "GET";
        case POST:    return "POST";
        case PUT:     return "PUT";
        case HTTP_DELETE:  return "DELETE";
        case HEAD:    return "HEAD";
        case OPTIONS: return "OPTIONS";
        case PATCH:   return "PATCH";
        default:      return "UNKNOWN";
    }
}

HttpRequest::Method HttpRequest::method_from_str(const std::string& s) {
    if (s == "GET")     return GET;
    if (s == "POST")    return POST;
    if (s == "PUT")     return PUT;
    if (s == "DELETE")  return HTTP_DELETE;
    if (s == "HEAD")    return HEAD;
    if (s == "OPTIONS") return OPTIONS;
    if (s == "PATCH")   return PATCH;
    return UNKNOWN;
}

} // namespace http
} // namespace hound
