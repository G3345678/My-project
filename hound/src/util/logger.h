#pragma once

#include <cstdio>
#include <cstring>
#include <chrono>
#include <ctime>
#include <thread>
#include <mutex>

// Windows.h (pulled in via winsock2.h) defines macros that conflict
// with our names. Undefine them here.
#ifdef ERROR
    #undef ERROR
#endif
#ifdef DEBUG
    #undef DEBUG
#endif

namespace hound {

enum class LogLevel {
    TRACE = 0,
    DEBUG = 1,
    INFO  = 2,
    WARN  = 3,
    ERROR = 4,
    FATAL = 5,
};

namespace detail {
    inline const char* level_str(LogLevel level) {
        switch (level) {
            case LogLevel::TRACE: return "TRACE";
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO:  return "INFO ";
            case LogLevel::WARN:  return "WARN ";
            case LogLevel::ERROR: return "ERROR";
            case LogLevel::FATAL: return "FATAL";
        }
        return "?????";
    }

    inline const char* timestamp() {
        // thread_local buffer — one per thread, no allocation
        thread_local char buf[32];
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S",
                      std::localtime(&time));
        // Overwrite the null terminator with ".SSS\0"
        auto len = std::strlen(buf);
        std::snprintf(buf + len, sizeof(buf) - len, ".%03lld",
                      static_cast<long long>(ms.count()));
        return buf;
    }
} // namespace detail

/// Thread-safe log function. All formatting is done via the macros,
/// so this takes a plain C string — no std::string dependency needed
/// in the header, avoiding MinGW g++ 8.1 include-order bugs.
inline void log(LogLevel level, const char* file, int line,
                const char* func, const char* msg) {
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);

    std::fprintf(stderr, "[%s] [%s] [%s:%d %s] %s\n",
                 detail::timestamp(),
                 detail::level_str(level),
                 file, line, func,
                 msg);
    std::fflush(stderr);
}

} // namespace hound

// ── Convenience Macros ───────────────────────────────────────
#define HOUND_LOG(level, fmt, ...) \
    do { \
        char _buf[1024]; \
        std::snprintf(_buf, sizeof(_buf), fmt, ##__VA_ARGS__); \
        ::hound::log(level, __FILE__, __LINE__, __func__, _buf); \
    } while (0)

#define LOG_TRACE(fmt, ...) HOUND_LOG(::hound::LogLevel::TRACE, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) HOUND_LOG(::hound::LogLevel::DEBUG, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...)  HOUND_LOG(::hound::LogLevel::INFO,  fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  HOUND_LOG(::hound::LogLevel::WARN,  fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) HOUND_LOG(::hound::LogLevel::ERROR, fmt, ##__VA_ARGS__)
#define LOG_FATAL(fmt, ...) \
    do { \
        HOUND_LOG(::hound::LogLevel::FATAL, fmt, ##__VA_ARGS__); \
        std::abort(); \
    } while (0)
