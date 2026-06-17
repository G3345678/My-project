#include "reactor/timer_wheel.h"
#include "util/logger.h"

#include <algorithm>

namespace hound {

TimerWheel::TimerWheel(int tick_ms, int slot_count)
    : tick_ms_(tick_ms)
    , slot_count_(slot_count)
    , buckets_(static_cast<size_t>(slot_count))
    , last_tick_(Timestamp::now())
{
    LOG_DEBUG("TimerWheel: created with %d slots × %dms", slot_count, tick_ms);
}

TimerWheel::~TimerWheel() {
    // Free all remaining timers
    for (auto& bucket : buckets_) {
        for (Timer* timer : bucket) {
            delete timer;
        }
    }
}

int64_t TimerWheel::add_timer(Timestamp when, TimerCallback callback) {
    auto* timer = new Timer{
        next_timer_id_.fetch_add(1),
        when,
        std::move(callback),
        0.0,     // one-shot
        0,       // rounds
        {}       // position (set by insert_timer)
    };
    insert_timer(timer);
    return timer->id;
}

int64_t TimerWheel::add_repeating(Timestamp when, double interval,
                                   TimerCallback callback) {
    auto* timer = new Timer{
        next_timer_id_.fetch_add(1),
        when,
        std::move(callback),
        interval,
        0,
        {}
    };
    insert_timer(timer);
    return timer->id;
}

void TimerWheel::insert_timer(Timer* timer) {
    ++timer_count_;

    // Calculate which slot and how many rounds
    int64_t delta_ms = timer->expiration.diff_ms(last_tick_);
    if (delta_ms < 0) {
        delta_ms = 0;  // Already due — fire immediately
    }

    timer->rounds = static_cast<int>(delta_ms / (tick_ms_ * slot_count_));
    int slot = static_cast<int>(
        (current_slot_ + (delta_ms / tick_ms_)) % slot_count_);

    auto& bucket = buckets_[static_cast<size_t>(slot)];
    bucket.push_back(timer);
    auto it = bucket.end();
    --it;
    timer->position = it;

    LOG_TRACE("TimerWheel: added timer %lld at slot %d, rounds=%d",
              static_cast<long long>(timer->id), slot, timer->rounds);
}

void TimerWheel::cancel(int64_t timer_id) {
    // We need to find the timer to access its position.
    // Since we store iterators, we can do this in O(1) if we have
    // access to the timer pointer. But we only have the ID.
    //
    // For now, we scan. A production implementation would use
    // a hash map (id → Timer*). The O(n) scan is acceptable because
    // cancel() is rare (connection close, explicit timeout cancel).
    for (auto& bucket : buckets_) {
        for (auto it = bucket.begin(); it != bucket.end(); ++it) {
            if ((*it)->id == timer_id) {
                delete *it;
                bucket.erase(it);
                --timer_count_;
                LOG_TRACE("TimerWheel: cancelled timer %lld",
                          static_cast<long long>(timer_id));
                return;
            }
        }
    }
    LOG_DEBUG("TimerWheel: cancel failed — timer %lld not found",
              static_cast<long long>(timer_id));
}

void TimerWheel::tick(const Timestamp& now) {
    last_tick_ = now;

    // How many slots to advance?
    int64_t elapsed_ms = now.diff_ms(last_tick_);
    if (elapsed_ms <= 0) return;

    int slots_to_advance = static_cast<int>(elapsed_ms / tick_ms_);
    if (slots_to_advance > slot_count_) {
        // More than a full cycle has passed — cap to one cycle
        // to avoid processing stale timers out of order
        slots_to_advance = slot_count_;
    }

    for (int i = 0; i < slots_to_advance; ++i) {
        current_slot_ = (current_slot_ + 1) % slot_count_;
        auto& bucket = buckets_[static_cast<size_t>(current_slot_)];

        auto it = bucket.begin();
        while (it != bucket.end()) {
            Timer* timer = *it;

            if (timer->rounds > 0) {
                // Not yet — decrement rounds and keep waiting
                --timer->rounds;
                ++it;
            } else {
                // Timer fires!
                // Remove from bucket BEFORE calling callback
                // (callback might add new timers or cancel this one)
                it = bucket.erase(it);
                --timer_count_;

                TimerCallback cb = timer->callback;
                double interval = timer->interval;
                delete timer;

                // Fire the callback
                cb();

                // If repeating, re-add
                if (interval > 0.0) {
                    Timestamp next = now.add_seconds(interval);
                    // Capture the return value but we don't need it here
                    // since the callback can't cancel a timer that's already
                    // been freed and re-added
                    add_repeating(next, interval, cb);
                }
            }
        }
    }
}

int64_t TimerWheel::next_timeout_ms() const {
    if (timer_count_ == 0) {
        return -1;  // No timers — poll indefinitely (or until woken)
    }

    // Walk forward from current slot to find the nearest timer
    for (int offset = 0; offset < slot_count_; ++offset) {
        int slot = (current_slot_ + offset) % slot_count_;
        const auto& bucket = buckets_[static_cast<size_t>(slot)];
        if (!bucket.empty()) {
            // Found a slot with timers — approximate timeout
            return offset * tick_ms_;
        }
    }

    // All timers have rounds > 0 — they're at least one full cycle away
    return slot_count_ * tick_ms_;
}

} // namespace hound
