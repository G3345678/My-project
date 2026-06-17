#pragma once

#include "util/noncopyable.h"
#include "reactor/timestamp.h"

#include <vector>
#include <unordered_map>

// fd_set and select() need platform-specific headers
#ifdef _WIN32
    #include <winsock2.h>
#else
    #include <sys/select.h>
#endif

// Windows headers define macros that conflict with our names
#ifdef ERROR
    #undef ERROR
#endif
#ifdef DEBUG
    #undef DEBUG
#endif

namespace hound {

class Channel;
class EventLoop;

/// Abstract interface for I/O multiplexing backends.
///
/// # What is I/O multiplexing?
///
/// The kernel tells us which fds are ready for I/O, so one thread can
/// monitor thousands of connections simultaneously. Instead of:
///
///   for each connection: try read() → if EAGAIN, skip → waste CPU
///
/// We do:
///
///   poller->poll(timeout) → returns list of ready fds → handle each
///
/// # Backend implementations
///
/// | Backend       | Platform     | Mechanism                                  |
/// |---------------|-------------|---------------------------------------------|
/// | SelectPoller  | All          | select() — max 1024 fds, O(n) scan        |
/// | EpollPoller   | Linux 2.6+   | epoll — O(1) for any fd count             |
/// | KqueuePoller  | macOS/BSD    | kqueue — O(1) like epoll                   |
///
/// Our SelectPoller works everywhere and is the default on Windows.
/// On Linux, EpollPoller replaces it for production-level performance.
class Poller : public NonCopyable {
public:
    explicit Poller(EventLoop* loop) : owner_loop_(loop) {}
    virtual ~Poller() = default;

    /// Add or update a Channel's event registration.
    /// Called by EventLoop when a Channel changes its event mask.
    virtual void update_channel(Channel* channel) = 0;

    /// Remove a Channel from the poll set.
    /// Called when a Channel is destroyed or removed from its EventLoop.
    virtual void remove_channel(Channel* channel) = 0;

    /// Block until events arrive or timeout expires.
    /// @param timeout_ms  Max wait time in milliseconds (-1 = forever)
    /// @param active_channels  Output: channels with pending events
    /// @return Timestamp when poll() returned (for event timing)
    virtual Timestamp poll(int timeout_ms,
                          std::vector<Channel*>* active_channels) = 0;

    /// Check if this Poller has a specific Channel registered.
    virtual bool has_channel(Channel* channel) const = 0;

protected:
    EventLoop* owner_loop_;
};

/// Select-based Poller — works on ALL platforms (Windows/Linux/macOS).
///
/// Uses the POSIX select() system call. Limitations:
/// - Max fd: 1024 (FD_SETSIZE) on most platforms
/// - O(n) scan: select() must scan all fds to find active ones
/// - Modifies fd_set in-place: must rebuild on every call
///
/// These limitations are acceptable for development and moderate loads.
/// For production on Linux, swap to EpollPoller.
class SelectPoller : public Poller {
public:
    explicit SelectPoller(EventLoop* loop);
    ~SelectPoller() override = default;

    void update_channel(Channel* channel) override;
    void remove_channel(Channel* channel) override;
    Timestamp poll(int timeout_ms,
                   std::vector<Channel*>* active_channels) override;
    bool has_channel(Channel* channel) const override;

private:
    /// Rebuild the fd_set from our channel map.
    /// select() modifies fd_set in-place, so we reconstruct it
    /// before each call.
    void rebuild_fds(int* max_fd);

    // Map fd → Channel for O(1) lookup when select() returns
    std::unordered_map<int, Channel*> channels_;
    std::vector<Channel*> channel_list_;  // For iteration

    fd_set read_fds_;
    fd_set write_fds_;
    fd_set except_fds_;
};

} // namespace hound
