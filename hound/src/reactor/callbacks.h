#pragma once

#include <functional>
#include <memory>

#include "reactor/timestamp.h"

namespace hound {

// Forward declarations for callback signatures
class Buffer;
class TcpConnection;

/// Callback types used throughout the server.
///
/// These are centralized here to avoid circular includes —
/// higher-level modules (HTTP, application) depend on these typedefs
/// without pulling in the full class definitions.

// ── I/O callbacks ────────────────────────────────────────────
using ReadCallback   = std::function<void(Timestamp)>;
using WriteCallback  = std::function<void()>;
using CloseCallback  = std::function<void()>;
using ErrorCallback  = std::function<void()>;

// ── Timer callbacks ──────────────────────────────────────────
using TimerCallback  = std::function<void()>;

// ── Generic task ─────────────────────────────────────────────
using Task           = std::function<void()>;

// ── Connection callbacks (for later modules) ─────────────────
using ConnectionCallback = std::function<void(int fd, const class Address&)>;
using MessageCallback    = std::function<void(int fd, Buffer*, Timestamp)>;

} // namespace hound
