#pragma once
#include <iostream>
#include <fstream>
#include <string>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <ctime>

/**
 * Logger: A thread-safe Singleton utility for system-wide diagnostic logging.
 * Implements a "Lazy Singleton" pattern (Meyers Singleton) to manage a centralized debug file.
 */
class Logger {
public:
    /**
     * Accesses the global Logger instance.
     * Guaranteed to be thread-safe in C++11 and later for static initialization.
     * @return Reference to the persistent Logger instance.
     */
    static Logger& GetInstance() {
        static Logger instance; // Initialized on first use
        return instance;
    }

    /**
     * Toggles the logging state and initializes the output stream.
     * @param enable If true, opens/truncates "debug.log" for writing.
     */
    void SetEnabled(bool enable) {
        std::lock_guard<std::mutex> lock(mutex_); // Ensure exclusive access during state change
        enabled_ = enable;                        // Update control flag
        if (enabled_ && !log_file_.is_open()) {
            // Open file in truncate mode to prevent massive log growth across sessions
            log_file_.open("debug.log", std::ios::out | std::ios::trunc);
        }
    }

    /**
     * Checks if the logger is currently active.
     * Designed for high-frequency checks within macros to avoid unnecessary overhead.
     * @return The current state of the enabled_ flag.
     */
    bool IsEnabled() {
        // Read lock is omitted for performance; assumes atomic read for the boolean flag
        return enabled_;
    }

    /**
     * Writes a timestamped message to the log file.
     * Thread-safety is guaranteed via internal mutex locking.
     * @param message The pre-formatted string to write.
     */
    void Log(const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_); // Prevent interleaved writes from multiple threads
        if (enabled_ && log_file_.is_open()) {
            std::time_t t = std::time(nullptr);   // Fetch current system time
            std::tm tm_buf;                       // Buffer to hold broken-down time

            // Platform-specific thread-safe time conversion
#ifdef _WIN32
            localtime_s(&tm_buf, &t);             // Windows-safe variant
#else
            localtime_r(&t, &tm_buf);             // POSIX-safe variant
#endif
            // Stream output with timestamping: [HH:MM:SS] Message
            log_file_ << "[" << std::put_time(&tm_buf, "%H:%M:%S") << "] " << message << std::endl;
        }
    }

private:
    /** Private constructor to enforce Singleton pattern; defaults to disabled state. */
    Logger() : enabled_(false) {}

    /** Ensures the file handle is released properly upon system shutdown. */
    ~Logger() {
        if (log_file_.is_open()) log_file_.close();
    }

    std::ofstream log_file_; // The output stream to "debug.log"
    std::mutex mutex_;       // Synchronizes access to log_file_ across threads
    bool enabled_;           // Master toggle for logging activity
};

/**
 * LOG_DEBUG Macro
 * Performance Optimization: Checks IsEnabled() BEFORE performing expensive
 * stringstream formatting and memory allocations.
 * * Usage: LOG_DEBUG("Variable x is: " << x);
 */
#define LOG_DEBUG(msg) { \
    if (Logger::GetInstance().IsEnabled()) { \
        std::stringstream ss; \
        ss << msg; \
        Logger::GetInstance().Log(ss.str()); \
    } \
}