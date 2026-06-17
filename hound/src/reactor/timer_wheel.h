#pragma once

#include "util/noncopyable.h"
#include "reactor/timestamp.h"
#include "reactor/callbacks.h"

#include <cstdint>
#include <list>
#include <vector>
#include <atomic>

namespace hound {

/// Hierarchical Time Wheel — O(1) add, O(1) cancel, O(1) per-tick.
///
/// # Why a Time Wheel instead of a min-heap or red-black tree?
///
/// | Data Structure | Add       | Cancel   | Per-Tick |
/// |---------------|-----------|----------|----------|
/// | Min-Heap      | O(log n)  | O(log n) | O(1)     |
/// | Red-Black Tree| O(log n)  | O(log n) | O(1)     |
/// | Time Wheel    | O(1)      | O(1)     | O(1)     |
///
/// HTTP servers have LOTS of timers — every connection has a timeout
/// (typically 5-30 seconds). With 10,000 concurrent connections, that's
/// 10,000 timers. O(log n) per add/cancel becomes noticeable.
/// Time wheels give us O(1) for ALL operations.
///
/// # How it works
///
/// Imagine a circular array of 1000 slots, each representing 1ms:
///
///   Slot 0 (0ms)        → [timer1, timer2]
///   Slot 1 (1ms)        → []
///   ...
///   Slot 999 (999ms)    → [timer3]
///
/// Every millisecond, we advance the "hand" by one slot and fire all
/// timers in that slot. To add a timer 1234ms from now:
///   1. slot = (current_slot + 1234) % 1000
///   2. rounds = 1234 / 1000 = 1 (expires next cycle)
///   3. Insert into slot, with rounds=1
///
/// When the hand reaches a slot, timers with rounds==0 fire.
/// Timers with rounds>0 get rounds-- and stay.
///
/// # Parameters
/// - 6000 slots × 10ms per slot = 60-second coverage
/// - Timers beyond 60s are stored with extra rounds
class TimerWheel : public NonCopyable {
public:
    explicit TimerWheel(int tick_ms = 10, int slot_count = 6000);
    ~TimerWheel();

    /// Add a one-shot timer. Returns a unique timer ID.
    /// @param when      Absolute time to fire
    /// @param callback  Called when timer fires
    int64_t add_timer(Timestamp when, TimerCallback callback);

    /// Add a repeating timer. Returns a unique timer ID.
    /// @param when      Absolute time of first fire
    /// @param interval  Seconds between fires (> 0)
    /// @param callback  Called each time the timer fires
    int64_t add_repeating(Timestamp when, double interval, TimerCallback callback);

    /// Cancel a timer by ID. O(1) — the timer removes itself
    /// from its bucket via a stored iterator.
    void cancel(int64_t timer_id);

    /// Advance the wheel to the current time and fire all due timers.
    /// Called once per EventLoop iteration.
    /// @param now  Current timestamp
    void tick(const Timestamp& now);

    /// Get the time until the next timer fires.
    /// Used by EventLoop to calculate poll() timeout.
    /// Returns negative value if no timers are scheduled.
    int64_t next_timeout_ms() const;

    /// Total number of active timers.
    size_t active_count() const { return timer_count_; }

private:
    struct Timer {
        int64_t id;
        Timestamp expiration;
        TimerCallback callback;
        double interval = 0.0;  // 0 = one-shot, >0 = repeating
        int rounds = 0;          // How many more cycles before firing

        // Iterator pointing to this timer's position in the bucket list.
        // Enables O(1) cancellation.
        std::list<Timer*>::iterator position;
    };

    void insert_timer(Timer* timer);

    int tick_ms_;
    int slot_count_;
    std::vector<std::list<Timer*>> buckets_;
    int current_slot_ = 0;
    Timestamp last_tick_;

    std::atomic<int64_t> next_timer_id_{1};
    size_t timer_count_ = 0;
};

} // namespace hound
