# 🐶 Hound — High-Performance HTTP/1.1 Server in C++17

A production-quality HTTP server written **from scratch** — no external dependencies
beyond the C++ standard library and OS sockets. Features a hand-rolled Reactor,
coroutine framework, and HTTP/1.1 protocol parser.

**Status: All 4 core modules complete ✅**

## Architecture

```
┌──────────────────────────────────────────┐
│            HTTP Protocol Layer           │  ← Module 4 ✅
│   State Machine Parser · Trie Router     │
│   HttpRequest · HttpResponse · Server    │
├──────────────────────────────────────────┤
│          Coroutine Framework             │  ← Module 3 ✅
│   Context (Fibers/ucontext) · Coroutine  │
│   Scheduler · co_read/write/accept/sleep │
├──────────────────────────────────────────┤
│        Reactor / Event Loop              │  ← Module 2 ✅
│   Channel · Poller (select/epoll)        │
│   Timer Wheel · Thread Pool              │
├──────────────────────────────────────────┤
│         I/O Abstraction Layer            │  ← Module 1 ✅
│   Socket (RAII) · Address · Chain Buffer │
│   Result<T> · Logger · NonCopyable       │
└──────────────────────────────────────────┘
```

## Quick Start

```bash
# Build (requires cmake and a C++17 compiler)
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Run the HTTP server
./build/examples/http_server

# Test it
curl http://localhost:8080/
curl http://localhost:8080/api/status
curl http://localhost:8080/api/users
curl -X POST -d "name=Test" http://localhost:8080/api/users
curl http://localhost:8080/hello/World
```

## Modules

### Module 1: I/O Abstraction Layer (`src/io/`)

| Component | Description |
|-----------|-------------|
| **Socket** | RAII TCP/UDP socket, move-only, cross-platform (Linux/macOS/Windows) |
| **Address** | IPv4/IPv6 address with DNS resolution |
| **Buffer** | Chain buffer (4KB blocks), O(1) append/retrieve, zero-copy string_view |
| **Result\<T\>** | Type-safe error handling without exceptions |
| **Logger** | Thread-safe logging with timestamps and source locations |

### Module 2: Reactor / Event Loop (`src/reactor/`)

| Component | Description |
|-----------|-------------|
| **Channel** | fd + event mask + callbacks binding |
| **Poller (SelectPoller)** | I/O multiplexing via `select()` (epoll/kqueue-ready) |
| **EventLoop** | Reactor core — poll → dispatch → timers → pending tasks |
| **TimerWheel** | O(1) hierarchical time wheel (6000 slots × 10ms) |
| **ThreadPool** | One loop per thread with round-robin distribution |

### Module 3: Coroutine Framework (`src/coro/`)

| Component | Description |
|-----------|-------------|
| **Context** | Platform context switching (Windows Fibers / POSIX ucontext) |
| **Coroutine** | Stackful coroutines (128KB stack), yield/resume |
| **Scheduler** | FIFO queue + I/O wait maps, integrated with EventLoop |
| **Hook (co_read/write/accept/connect/sleep)** | Coroutine-aware I/O — blocking syntax, async execution |

### Module 4: HTTP Protocol (`src/http/`)

| Component | Description |
|-----------|-------------|
| **HttpParser** | RFC 7230 state machine, Content-Length + Chunked encoding |
| **HttpRequest** | Parsed request (method, path, headers, body, cookies, query params) |
| **HttpResponse** | Response builder + serializer (status, headers, cookies, body) |
| **Router** | Trie-based URL routing with `:param` and `*wildcard` support |
| **HttpServer** | Full assembly: accept → parse → route → handle → respond |

## API Endpoints (demo)

```
GET  /               → HTML welcome page
GET  /api/status     → {"server":"Hound","version":"0.2.0"}
GET  /api/users      → [{"id":1,"name":"Alice"}, ...]
GET  /api/users/:id  → User by ID (path parameter)
POST /api/users      → Create user (form data → JSON response)
GET  /hello/:name    → "Hello, {name}!"
```

## Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `HOUND_BUILD_TESTS` | ON | Build unit tests (Google Test) |
| `HOUND_BUILD_EXAMPLES` | ON | Build example programs |
| `HOUND_ENABLE_ASAN` | OFF | Enable AddressSanitizer |
| `HOUND_ENABLE_TSAN` | OFF | Enable ThreadSanitizer |
| `HOUND_ENABLE_UBSAN` | OFF | Enable UndefinedBehaviorSanitizer |

## Platform Support

| Platform | Compiler | Backend |
|----------|----------|---------|
| Linux (x86-64) | GCC 8+, Clang 14+ | select / epoll (planned) |
| macOS (ARM64/x86-64) | Clang 14+ | select / kqueue (planned) |
| Windows (x86-64) | MSVC 2022+, MinGW 8+ | select |

## Design Decisions

| Decision | Why |
|----------|-----|
| Move-only sockets | Prevents double-close bugs at compile time |
| Chain buffer over ring buffer | HTTP messages have unpredictable sizes |
| 4 KB block size | Matches memory page size |
| `Result<T>` over exceptions | Connection failures are expected, not exceptional |
| C++17 over C++20 | Broader compiler support |
| Hand-written HTTP parser | Zero-copy scanning, no backtracking, incrementally parsable |
| Trie router over map | Path segments are naturally prefix-structured |

## References

- [RFC 7230 — HTTP/1.1 Message Syntax and Routing](https://datatracker.ietf.org/doc/html/rfc7230)
- [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/)
- [muduo — C++ Reactor network library](https://github.com/chenshuo/muduo)
- [libco — Tencent coroutine library](https://github.com/Tencent/libco)
