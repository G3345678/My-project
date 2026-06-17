#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>

namespace hound {
namespace http {

class HttpRequest;
class HttpResponse;

/// Handler function for HTTP routes.
/// Takes a request and a response (to be filled in).
using Handler = std::function<void(const HttpRequest&, HttpResponse*)>;

/// Trie-based URL router with path parameter support.
///
/// # Features
/// - Exact match: GET /api/users → handler
/// - Path parameters: GET /api/users/:id → handler (captures `id`)
/// - Wildcard: GET /static/*filepath → handler (captures `filepath`)
/// - Method-specific routing: same path, different method → different handler
///
/// # Trie Structure
///
///   root_ → "api" → "users" → ":id" (leaf, GET handler)
///         → "static" → "*filepath" (leaf, GET handler)
///         → "" (leaf, GET handler for "/")
///
/// Path matching walks the trie. Parameters are extracted during traversal.
class Router {
public:
    Router() = default;

    /// Register a handler for a method + path pattern.
    /// @param method   "GET", "POST", etc.
    /// @param pattern  "/api/users/:id" or "/static/*path"
    /// @param handler  Called when a request matches
    void add(const std::string& method, const std::string& pattern,
             Handler handler);

    /// Route a request to its handler.
    /// @param req   The incoming request
    /// @param resp  Output: set to 404 if no handler found
    /// @return The handler, or nullptr if not found
    Handler route(const HttpRequest& req, HttpResponse* resp) const;

    /// Check if any routes are registered.
    bool empty() const { return root_ == nullptr; }

private:
    struct Node {
        std::string segment;          // "users", ":id", "*path", ""
        bool is_param = false;        // Starts with ':'
        bool is_wildcard = false;     // Starts with '*'
        std::map<std::string, Handler> handlers;  // method → handler
        std::vector<std::unique_ptr<Node>> children;

        Node* find_child(const std::string& seg) const;
    };

    std::unique_ptr<Node> root_;
};

} // namespace http
} // namespace hound
