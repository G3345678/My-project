#pragma once

#include <cstdint>
#include <chrono>
#include <string>

namespace hound {

/// High-resolution timestamp using steady_clock.
///
/// Why steady_clock instead of system_clock?
///   system_clock can jump backwards (NTP adjustment, daylight savings).
///   steady_clock is monotonic — it never goes backwards, which is
///   essential for timer comparisons and timeout calculations.
class Timestamp {
public:
    using Clock = std::chrono::steady_clock;
    using Microseconds = std::chrono::microseconds;

    Timestamp() : us_(0) {}

    static Timestamp now() {
        auto dur = Clock::now().time_since_epoch();
        return Timestamp(
            std::chrono::duration_cast<Microseconds>(dur).count());
    }

    static Timestamp invalid() { return Timestamp(0); }

    // ── Arithmetic ────────────────────────────────────────────
    bool valid() const { return us_ > 0; }

    int64_t microseconds() const { return us_; }

    Timestamp add_seconds(double s) const {
        return Timestamp(us_ + static_cast<int64_t>(s * 1'000'000.0));
    }

    Timestamp add_ms(int64_t ms) const {
        return Timestamp(us_ + ms * 1000);
    }

    double diff_seconds(const Timestamp& other) const {
        return static_cast<double>(us_ - other.us_) / 1'000'000.0;
    }

    int64_t diff_ms(const Timestamp& other) const {
        return (us_ - other.us_) / 1000;
    }

    // ── Comparison ────────────────────────────────────────────
    bool operator<(const Timestamp& rhs) const { return us_ < rhs.us_; }
    bool operator<=(const Timestamp& rhs) const { return us_ <= rhs.us_; }
    bool operator>(const Timestamp& rhs) const { return us_ > rhs.us_; }
    bool operator>=(const Timestamp& rhs) const { return us_ >= rhs.us_; }
    bool operator==(const Timestamp& rhs) const { return us_ == rhs.us_; }

    std::string to_string() const {
        return std::to_string(us_ / 1'000'000) + "." +
               std::to_string(us_ % 1'000'000 / 1'000) + "s";
    }

private:
    explicit Timestamp(int64_t us) : us_(us) {}
    int64_t us_;  // microseconds since steady_clock epoch
};

} // namespace hound
