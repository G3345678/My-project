#include "http/router.h"
#include "http/http_request.h"
#include "http/http_response.h"
#include "util/logger.h"

#include <sstream>

namespace hound {
namespace http {

Router::Node* Router::Node::find_child(const std::string& seg) const {
    for (const auto& child : children) {
        if (child->segment == seg || child->is_param || child->is_wildcard) {
            return child.get();
        }
    }
    return nullptr;
}

void Router::add(const std::string& method, const std::string& pattern,
                 Handler handler) {
    if (!root_) {
        root_ = std::make_unique<Node>();
    }

    // Split pattern by '/'
    std::vector<std::string> segments;
    std::istringstream iss(pattern);
    std::string seg;
    while (std::getline(iss, seg, '/')) {
        if (!seg.empty()) {
            segments.push_back(seg);
        }
    }

    // Walk/build the trie
    Node* node = root_.get();
    for (const auto& seg : segments) {
        // Find existing child or create new one
        Node* child = nullptr;
        for (auto& c : node->children) {
            if (c->segment == seg) {
                child = c.get();
                break;
            }
        }

        if (!child) {
            auto new_child = std::make_unique<Node>();
            new_child->segment = seg;
            if (seg[0] == ':') {
                new_child->is_param = true;
            } else if (seg[0] == '*') {
                new_child->is_wildcard = true;
            }
            child = new_child.get();
            node->children.push_back(std::move(new_child));
        }

        node = child;
    }

    // Register handler at the leaf node
    node->handlers[method] = std::move(handler);

    LOG_DEBUG("Router: added %s %s", method.c_str(), pattern.c_str());
}

Handler Router::route(const HttpRequest& req, HttpResponse* resp) const {
    if (!root_) return nullptr;

    std::string method = HttpRequest::method_str(req.method());
    const std::string& path = req.path();

    // Split path by '/'
    std::vector<std::string> segments;
    std::istringstream iss(path);
    std::string seg;
    while (std::getline(iss, seg, '/')) {
        if (!seg.empty()) {
            segments.push_back(seg);
        }
    }

    // Walk the trie, tracking parameter captures
    Node* node = root_.get();
    std::map<std::string, std::string> params;

    for (size_t i = 0; i < segments.size(); ++i) {
        const auto& seg = segments[i];

        // Try exact match first
        Node* match = nullptr;
        Node* param_match = nullptr;
        Node* wildcard_match = nullptr;

        for (const auto& child : node->children) {
            if (child->segment == seg) {
                match = child.get();
                break;
            }
            if (child->is_param && !param_match) {
                param_match = child.get();
            }
            if (child->is_wildcard && !wildcard_match) {
                wildcard_match = child.get();
            }
        }

        if (match) {
            node = match;
        } else if (param_match) {
            node = param_match;
            // Capture parameter (strip the ':')
            params[param_match->segment.substr(1)] = seg;
        } else if (wildcard_match) {
            node = wildcard_match;
            // Capture remaining path as wildcard value (strip the '*')
            std::string wildcard_value;
            for (size_t j = i; j < segments.size(); ++j) {
                if (j > i) wildcard_value += "/";
                wildcard_value += segments[j];
            }
            params[wildcard_match->segment.substr(1)] = wildcard_value;
            break;  // Wildcard consumes all remaining segments
        } else {
            // No match
            if (resp) {
                *resp = HttpResponse::not_found(
                    "404 Not Found: " + path);
            }
            return nullptr;
        }
    }

    // Find handler for the method
    auto it = node->handlers.find(method);
    if (it == node->handlers.end()) {
        // Method not allowed — check if other methods exist
        if (!node->handlers.empty() && resp) {
            *resp = HttpResponse::not_found("405 Method Not Allowed");
        } else if (resp) {
            *resp = HttpResponse::not_found("404 Not Found: " + path);
        }
        return nullptr;
    }

    return it->second;
}

} // namespace http
} // namespace hound
