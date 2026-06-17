# Hound Roadmap

## Completed ✅

- [x] **Module 1 — I/O Abstraction Layer**
  - Socket RAII wrapper (TCP/UDP, non-blocking, cross-platform)
  - Address (IPv4/IPv6, DNS resolution)
  - Chain Buffer (4KB blocks, zero-copy string_view)
  - Result\<T\> (type-safe error handling)
  - Logger (thread-safe, timestamped)

- [x] **Module 2 — Reactor / Event Loop**
  - Channel (fd + events + callbacks)
  - Poller abstraction + SelectPoller (select() backend)
  - EventLoop (poll → dispatch → timers → pending tasks)
  - TimerWheel (O(1) hierarchical time wheel)
  - EventLoopThreadPool (one loop per thread)

- [x] **Module 3 — Coroutine Framework**
  - Context switching (Windows Fibers / POSIX ucontext)
  - Stackful Coroutine (yield/resume, 128KB stack)
  - Scheduler (FIFO queue + I/O wait maps)
  - Coroutine-aware I/O (co_read/write/accept/connect/sleep)

- [x] **Module 4 — HTTP Protocol**
  - HTTP/1.1 state machine parser (RFC 7230, Content-Length + Chunked)
  - HttpRequest / HttpResponse data structures
  - Trie-based URL router (:param and *wildcard)
  - HttpServer assembly
  - Static file serving (basic)

- [x] **Examples**
  - Blocking TCP echo server
  - Non-blocking Reactor echo server
  - Full HTTP/1.1 server with REST API

- [x] **Infrastructure**
  - CMake build system (C++17, sanitizers)
  - Cross-platform CI (Linux GCC/Clang, macOS, Windows MSVC)
  - Google Test integration

## In Progress 🚧

- [ ] **Linux EpollPoller** — Replace select() with epoll for 10x+ fd capacity
- [ ] **macOS KqueuePoller** — Native kqueue backend
- [ ] **Module 2 tests** — Channel, EventLoop, TimerWheel unit tests
- [ ] **Module 4 tests** — HTTP parser, Router unit tests

## Planned 📋

### Near-term

- [ ] **HTTP/1.1 response writev** — Use scatter/gather I/O for headers + body
- [ ] **Middleware chain** — Auth, CORS, rate limiting, request logging
- [ ] **JSON body parser** — Parse `application/json` request bodies
- [ ] **Multipart form data** — File upload support
- [ ] **HTTP compression** — gzip Content-Encoding
- [ ] **Connection pool** — Keep-Alive connection reuse for backend requests
- [ ] **Docker support** — Multi-stage build, <10MB image

### Long-term

- [ ] **TLS/HTTPS** — OpenSSL or BoringSSL integration
- [ ] **HTTP/2** — Binary framing, multiplexed streams, HPACK header compression
- [ ] **WebSocket** — RFC 6455 upgrade + frame protocol
- [ ] **io_uring backend** — Linux 5.1+ async I/O (30% faster than epoll)
- [ ] **LD_PRELOAD hook** — Transparent coroutine hooking on Linux (dlsym RTLD_NEXT)
- [ ] **Prometheus metrics** — Request count, latency histograms, connection gauge
- [ ] **Hot reload** — Graceful config reload without dropping connections
- [ ] **Clustering** — Multi-process with SO_REUSEPORT

## Known Limitations

| Limitation | Impact | Fix |
|-----------|--------|-----|
| select() max 1024 fds | ~1000 concurrent connections | EpollPoller |
| Non-blocking send() doesn't handle partial writes | Large responses may truncate | Output buffer + write event handling |
| Timer cancellation is O(n) | Slow with 100K+ timers | id→Timer* hash map |
| No TLS | HTTP only | OpenSSL integration |
| Windows coroutine hooks not transparent | Must use co_read() explicitly | Detours/IAT hook on Windows |

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 0.1.0 | 2026-06 | Modules 1-2: I/O layer + Reactor |
| 0.2.0 | 2026-06 | Modules 3-4: Coroutines + HTTP |
