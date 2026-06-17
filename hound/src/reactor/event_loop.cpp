#include "reactor/event_loop.h"
#include "reactor/poller.h"
#include "reactor/channel.h"
#include "reactor/timer_wheel.h"
#include "io/socket.h"  // ensure_networking_initialized()
#include "util/logger.h"

#include <cassert>
#include <algorithm>

#ifdef _WIN32
    // Windows select() only works with sockets. We create a self-connected
    // TCP socket pair for wakeup: one end for the Channel to watch, the
    // other for wakeup() to write to.
    // winsock2.h is already included via poller.h
#else
    #include <unistd.h>
    #include <sys/eventfd.h>
    #include <fcntl.h>
#endif

namespace hound {

thread_local EventLoop* EventLoop::t_loop_in_this_thread_ = nullptr;

// ─────────────────────────────────────────────────────────────
//  Helper: Create a wakeup fd pair
// ─────────────────────────────────────────────────────────────
// On Linux: eventfd — a lightweight single-fd counter
// On Windows: _pipe — a pair of fds, one for read one for write
//
// In both cases, we write a byte to the "write end" to make the
// "read end" readable, which wakes up the Poller.

#ifdef _WIN32
namespace {
    struct WakeupPair {
        int read_fd;   // SOCKET monitored by Channel
        int write_fd;  // SOCKET written to by wakeup()
    };

    WakeupPair create_wakeup_fds() {
        // Create a TCP socket pair for wakeup.
        // Windows select() only works with sockets, not pipe fds.
        SOCKET listener = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listener == INVALID_SOCKET) {
            LOG_FATAL("wakeup socket() failed: %d", WSAGetLastError());
        }

        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr("127.0.0.1");
        addr.sin_port = 0;  // OS picks a free port

        if (::bind(listener, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
            LOG_FATAL("wakeup bind() failed: %d", WSAGetLastError());
        }
        if (::listen(listener, 1) != 0) {
            LOG_FATAL("wakeup listen() failed: %d", WSAGetLastError());
        }

        // Get the assigned port
        int len = sizeof(addr);
        ::getsockname(listener, (struct sockaddr*)&addr, &len);

        // Create the write end and connect to the listener
        SOCKET writer = ::socket(AF_INET, SOCK_STREAM, 0);
        if (writer == INVALID_SOCKET) {
            LOG_FATAL("wakeup writer socket() failed: %d", WSAGetLastError());
        }
        if (::connect(writer, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
            LOG_FATAL("wakeup connect() failed: %d", WSAGetLastError());
        }

        // Accept the connection
        SOCKET reader = ::accept(listener, nullptr, nullptr);
        if (reader == INVALID_SOCKET) {
            LOG_FATAL("wakeup accept() failed: %d", WSAGetLastError());
        }

        ::closesocket(listener);

        // Set both to non-blocking
        u_long mode = 1;
        ::ioctlsocket(reader, FIONBIO, &mode);
        ::ioctlsocket(writer, FIONBIO, &mode);

        return { static_cast<int>(reader), static_cast<int>(writer) };
    }

    void close_wakeup_fds(int read_fd, int write_fd) {
        ::closesocket(read_fd);
        ::closesocket(write_fd);
    }

    void write_wakeup(int fd) {
        uint64_t one = 1;
        ::send(fd, (const char*)&one, sizeof(one), 0);
    }

    void read_wakeup(int fd) {
        uint64_t val;
        ::recv(fd, (char*)&val, sizeof(val), 0);
    }
}
#else
namespace {
    struct WakeupPair {
        int read_fd;
        int write_fd;
    };

    WakeupPair create_wakeup_fds() {
        int fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (fd < 0) {
            LOG_FATAL("eventfd() failed: %d", errno);
        }
        return { fd, fd };  // eventfd uses same fd for read and write
    }

    void close_wakeup_fds(int read_fd, int /*write_fd*/) {
        ::close(read_fd);  // eventfd: only one fd to close
    }

    void write_wakeup(int fd) {
        uint64_t one = 1;
        ::write(fd, &one, sizeof(one));
    }

    void read_wakeup(int fd) {
        uint64_t val;
        ::read(fd, &val, sizeof(val));
    }
}
#endif

// ─────────────────────────────────────────────────────────────
//  Construction / Destruction
// ─────────────────────────────────────────────────────────────

EventLoop::EventLoop()
    : poller_(new SelectPoller(this))
    , timer_wheel_(new TimerWheel())
    , wakeup_fd_(-1)
    , thread_id_(std::this_thread::get_id())
{
    // Ensure networking is initialized before creating sockets
    ensure_networking_initialized();

    // Create wakeup mechanism
    auto wfds = create_wakeup_fds();
    wakeup_fd_ = wfds.read_fd;
    wakeup_write_fd_ = wfds.write_fd;

    LOG_DEBUG("EventLoop created, wakeup_fd=%d", wakeup_fd_);

    // Create Channel for wakeup fd
    wakeup_channel_ = std::make_unique<Channel>(this, wakeup_fd_);
    wakeup_channel_->set_read_callback(
        [this](Timestamp) { handle_wakeup_read(Timestamp::now()); });
    wakeup_channel_->enable_reading();

    t_loop_in_this_thread_ = this;
}

EventLoop::~EventLoop() {
    LOG_DEBUG("EventLoop destroyed");

    t_loop_in_this_thread_ = nullptr;

    wakeup_channel_->disable_all();
    wakeup_channel_.reset();

    close_wakeup_fds(wakeup_fd_, wakeup_write_fd_);
}

EventLoop* EventLoop::current() {
    return t_loop_in_this_thread_;
}

void EventLoop::assert_in_loop_thread() const {
    if (!is_in_loop_thread()) {
        LOG_FATAL("EventLoop: cross-thread access! Expected thread of loop, "
                  "called from another thread");
    }
}

// ─────────────────────────────────────────────────────────────
//  Main Loop
// ─────────────────────────────────────────────────────────────

void EventLoop::loop() {
    assert_in_loop_thread();
    assert(!looping_);

    looping_ = true;
    quit_ = false;

    LOG_INFO("EventLoop: entering main loop");

    while (!quit_) {
        std::vector<Channel*> active_channels;

        // 1. Poll for I/O events with timeout from nearest timer
        int timeout_ms = static_cast<int>(timer_wheel_->next_timeout_ms());
        Timestamp poll_time = poller_->poll(timeout_ms, &active_channels);

        // 2. Dispatch I/O events to channels
        event_handling_ = true;
        for (Channel* ch : active_channels) {
            ch->handle_event(poll_time);
        }
        event_handling_ = false;

        // 3. Fire all expired timers
        timer_wheel_->tick(poll_time);

        // 4. Execute cross-thread tasks
        do_pending_tasks();
    }

    looping_ = false;
    LOG_INFO("EventLoop: main loop exited");
}

void EventLoop::quit() {
    quit_ = true;
    if (!is_in_loop_thread()) {
        wakeup();
    }
}

// ─────────────────────────────────────────────────────────────
//  Cross-Thread Task Queue
// ─────────────────────────────────────────────────────────────

void EventLoop::run_in_loop(Task task) {
    if (is_in_loop_thread()) {
        task();
    } else {
        queue_in_loop(std::move(task));
    }
}

void EventLoop::queue_in_loop(Task task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_tasks_.push_back(std::move(task));
    }
    if (!is_in_loop_thread() || calling_pending_tasks_) {
        wakeup();
    }
}

void EventLoop::do_pending_tasks() {
    std::vector<Task> tasks;
    calling_pending_tasks_ = true;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks.swap(pending_tasks_);
    }

    for (Task& task : tasks) {
        task();
    }

    calling_pending_tasks_ = false;
}

// ─────────────────────────────────────────────────────────────
//  Wakeup
// ─────────────────────────────────────────────────────────────

void EventLoop::wakeup() {
    write_wakeup(wakeup_write_fd_);
}

void EventLoop::handle_wakeup_read(Timestamp) {
    read_wakeup(wakeup_fd_);
}

// ─────────────────────────────────────────────────────────────
//  Poller / Channel Delegates
// ─────────────────────────────────────────────────────────────

void EventLoop::update_channel(Channel* channel) {
    assert_in_loop_thread();
    poller_->update_channel(channel);
}

void EventLoop::remove_channel(Channel* channel) {
    assert_in_loop_thread();
    poller_->remove_channel(channel);
}

bool EventLoop::has_channel(Channel* channel) const {
    assert_in_loop_thread();
    return poller_->has_channel(channel);
}

// ─────────────────────────────────────────────────────────────
//  Timer Delegates
// ─────────────────────────────────────────────────────────────

int64_t EventLoop::run_after(double delay, TimerCallback cb) {
    Timestamp when = Timestamp::now().add_seconds(delay);
    return timer_wheel_->add_timer(when, std::move(cb));
}

int64_t EventLoop::run_every(double interval, TimerCallback cb) {
    Timestamp when = Timestamp::now().add_seconds(interval);
    return timer_wheel_->add_repeating(when, interval, std::move(cb));
}

int64_t EventLoop::run_at(Timestamp time, TimerCallback cb) {
    return timer_wheel_->add_timer(time, std::move(cb));
}

void EventLoop::cancel_timer(int64_t timer_id) {
    timer_wheel_->cancel(timer_id);
}

} // namespace hound
