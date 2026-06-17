#include "reactor/channel.h"
#include "reactor/event_loop.h"
#include "util/logger.h"

namespace hound {

Channel::Channel(EventLoop* loop, int fd)
    : loop_(loop), fd_(fd)
{
}

Channel::~Channel() {
    // Must not be in the middle of handle_event when destroyed
    if (loop_->is_in_loop_thread() || !event_handling_) {
        remove();
    }
}

void Channel::update() {
    added_to_loop_ = true;
    loop_->update_channel(this);
}

void Channel::remove() {
    if (!added_to_loop_) return;
    added_to_loop_ = false;
    loop_->remove_channel(this);
}

void Channel::enable_reading() {
    events_ |= kReadEvent;
    update();
}

void Channel::enable_writing() {
    events_ |= kWriteEvent;
    update();
}

void Channel::disable_reading() {
    events_ &= ~kReadEvent;
    update();
}

void Channel::disable_writing() {
    events_ &= ~kWriteEvent;
    update();
}

void Channel::disable_all() {
    events_ = kNoneEvent;
    update();
}

void Channel::tie(const std::shared_ptr<void>& obj) {
    tie_ = obj;
    tied_ = true;
}

void Channel::handle_event(Timestamp receive_time) {
    // If tied to an owner and owner is gone, skip
    std::shared_ptr<void> guard;
    if (tied_) {
        guard = tie_.lock();
        if (!guard) {
            // Owner destroyed — silently drop the event
            return;
        }
    }

    event_handling_ = true;

    // Handle close/error first (they take priority)
    if ((revents_ & kErrorEvent) && !(revents_ & kReadEvent)) {
        if (error_callback_) error_callback_();
    }

    // POLLHUP / connection closed by peer
    if (revents_ & kErrorEvent) {
        if (close_callback_) close_callback_();
    }

    // Readable: new data or new connection
    if (revents_ & kReadEvent) {
        if (read_callback_) read_callback_(receive_time);
    }

    // Writable: send buffer has space
    if (revents_ & kWriteEvent) {
        if (write_callback_) write_callback_();
    }

    event_handling_ = false;
}

} // namespace hound
