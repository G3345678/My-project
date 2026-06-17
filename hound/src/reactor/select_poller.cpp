#include "reactor/poller.h"
#include "reactor/channel.h"
#include "util/logger.h"

#include <cstring>
#include <algorithm>

namespace hound {

SelectPoller::SelectPoller(EventLoop* loop)
    : Poller(loop)
{
}

void SelectPoller::update_channel(Channel* channel) {
    int fd = channel->fd();
    if (channels_.find(fd) == channels_.end()) {
        channels_[fd] = channel;
        channel_list_.push_back(channel);
        LOG_TRACE("SelectPoller: added channel fd=%d", fd);
    }
    channel->set_index(0);  // Mark as registered (index unused for select)
}

void SelectPoller::remove_channel(Channel* channel) {
    int fd = channel->fd();
    auto it = channels_.find(fd);
    if (it != channels_.end()) {
        channels_.erase(it);
        auto& list = channel_list_;
        list.erase(std::remove(list.begin(), list.end(), channel), list.end());
        LOG_TRACE("SelectPoller: removed channel fd=%d", fd);
    }
    channel->set_index(-1);  // Mark as unregistered
}

bool SelectPoller::has_channel(Channel* channel) const {
    return channels_.find(channel->fd()) != channels_.end();
}

void SelectPoller::rebuild_fds(int* max_fd) {
    FD_ZERO(&read_fds_);
    FD_ZERO(&write_fds_);
    FD_ZERO(&except_fds_);

    *max_fd = 0;

    for (const auto& pair : channels_) {
        Channel* ch = pair.second;
        int fd = ch->fd();

        if (ch->is_reading()) {
            FD_SET(fd, &read_fds_);
        }
        if (ch->is_writing()) {
            FD_SET(fd, &write_fds_);
        }
        // Always monitor for exceptions (OOB data, errors)
        FD_SET(fd, &except_fds_);

        if (fd > *max_fd) {
            *max_fd = fd;
        }
    }
    // select() requires max_fd + 1
    *max_fd = *max_fd + 1;
}

Timestamp SelectPoller::poll(int timeout_ms,
                             std::vector<Channel*>* active_channels) {
    int max_fd = 0;
    rebuild_fds(&max_fd);

    struct timeval tv;
    struct timeval* ptv = nullptr;

    if (timeout_ms >= 0) {
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        ptv = &tv;
    }

    int n = select(max_fd, &read_fds_, &write_fds_, &except_fds_, ptv);
    Timestamp now = Timestamp::now();

    if (n < 0) {
        LOG_ERROR("SelectPoller::poll: select() error (errno=%d)", errno);
        return now;
    }

    if (n == 0) {
        // Timeout — no events
        return now;
    }

    // Scan all channels to find which ones are active
    // This is O(n) — the main limitation of select()
    active_channels->clear();
    active_channels->reserve(static_cast<size_t>(n));

    for (Channel* ch : channel_list_) {
        int fd = ch->fd();
        int revents = Channel::kNoneEvent;

        if (FD_ISSET(fd, &read_fds_)) {
            revents |= Channel::kReadEvent;
        }
        if (FD_ISSET(fd, &write_fds_)) {
            revents |= Channel::kWriteEvent;
        }
        if (FD_ISSET(fd, &except_fds_)) {
            revents |= Channel::kErrorEvent;
        }

        if (revents != Channel::kNoneEvent) {
            ch->set_revents(revents);
            active_channels->push_back(ch);

            if (--n <= 0) break;  // Found all active channels
        }
    }

    return now;
}

} // namespace hound
