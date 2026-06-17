#pragma once

#include "util/noncopyable.h"
#include "reactor/timestamp.h"
#include "reactor/callbacks.h"

#include <memory>

namespace hound {

class EventLoop;

/// Channel: binds a file descriptor to a set of watched events and callbacks.
///
/// ## What problem does this solve?
///
/// Without Channel, every fd would need its own if-else branch in the event loop:
///
///   if (listen_fd is readable) accept();
///   if (conn1_fd is readable) read(conn1);
///   if (conn2_fd is readable) read(conn2);
///   ...
///
/// Channel abstracts this: "when fd N becomes readable, call this function."
/// The event loop doesn't know what each fd does — it just dispatches events
/// to Channels.
///
/// ## Lifecycle
/// - Created: Channel(loop, fd)
/// - Activated: ch.enable_reading() → registers with Poller via EventLoop
/// - Event fires: Poller → EventLoop → Channel::handle_event()
/// - Destroyed: Channel destructor → disable_all() → removed from Poller
///
/// ## Thread safety
/// Channels are NOT thread-safe. Each Channel belongs to a specific EventLoop
/// thread and must only be accessed from that thread.
class Channel : public NonCopyable {
public:
    Channel(EventLoop* loop, int fd);
    ~Channel();

    // ── Event Management ─────────────────────────────────────
    /// Start watching for readable events (data available, new connection).
    void enable_reading();

    /// Start watching for writable events (send buffer has space).
    void enable_writing();

    /// Stop watching for readable events.
    void disable_reading();

    /// Stop watching for writable events.
    void disable_writing();

    /// Stop watching all events.
    void disable_all();

    // ── State Queries ────────────────────────────────────────
    int fd() const { return fd_; }

    /// Events the user is interested in (readable, writable).
    /// This is the set of events we WANT to watch.
    int events() const { return events_; }

    /// Events that actually occurred (set by Poller).
    /// This is the set of events that HAPPENED.
    int revents() const { return revents_; }
    void set_revents(int revt) { revents_ = revt; }

    bool is_reading() const { return events_ & kReadEvent; }
    bool is_writing() const { return events_ & kWriteEvent; }
    bool is_none_event() const { return events_ == kNoneEvent; }

    /// Index used by Poller implementations to track this Channel
    /// in their internal data structures (e.g., fd_set or epoll).
    int index() const { return index_; }
    void set_index(int idx) { index_ = idx; }

    EventLoop* owner_loop() const { return loop_; }

    // ── Callbacks ────────────────────────────────────────────
    void set_read_callback(ReadCallback cb)  { read_callback_ = std::move(cb); }
    void set_write_callback(WriteCallback cb) { write_callback_ = std::move(cb); }
    void set_close_callback(CloseCallback cb) { close_callback_ = std::move(cb); }
    void set_error_callback(ErrorCallback cb) { error_callback_ = std::move(cb); }

    // ── Tie ──────────────────────────────────────────────────
    /// Tie this Channel to an owner object via weak_ptr.
    /// If the owner is destroyed, handle_event() becomes a no-op,
    /// preventing use-after-free.
    void tie(const std::shared_ptr<void>& obj);

    /// Called by EventLoop when this Channel has events.
    void handle_event(Timestamp receive_time);

    /// Remove this Channel from its EventLoop's Poller.
    void remove();

    // ── Event Constants ──────────────────────────────────────
    static const int kNoneEvent  = 0;
    static const int kReadEvent  = 1;   // Data available to read
    static const int kWriteEvent = 2;   // Buffer space available to write
    static const int kErrorEvent = 4;   // Error condition on fd

private:
    void update();  // Propagate event changes to Poller via EventLoop

    EventLoop* loop_;
    int fd_;
    int events_ = kNoneEvent;   // Events we're watching for
    int revents_ = kNoneEvent;  // Events that occurred (set by Poller)
    int index_ = -1;            // Poller-specific tracking index

    ReadCallback   read_callback_;
    WriteCallback  write_callback_;
    CloseCallback  close_callback_;
    ErrorCallback  error_callback_;

    std::weak_ptr<void> tie_;   // Prevents use-after-free
    bool tied_ = false;
    bool event_handling_ = false;
    bool added_to_loop_ = false;
};

} // namespace hound
