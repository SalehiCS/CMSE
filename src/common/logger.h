#pragma once
#include <iostream>
#include <fstream>
#include <string>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <ctime>

class Logger {
public:
    static Logger& GetInstance() {
        static Logger instance;
        return instance;
    }

    // New: Control Flag
    void SetEnabled(bool enable) {
        std::lock_guard<std::mutex> lock(mutex_);
        enabled_ = enable;
        if (enabled_ && !log_file_.is_open()) {
            log_file_.open("debug.log", std::ios::out | std::ios::trunc); // Truncate to start fresh
        }
    }

    bool IsEnabled() {
        // No lock here for performance (atomic read usually fine, or accept loose consistency)
        return enabled_;
    }

    void Log(const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (enabled_ && log_file_.is_open()) {
            std::time_t t = std::time(nullptr);
            std::tm tm_buf;
#ifdef _WIN32
            localtime_s(&tm_buf, &t);
#else
            localtime_r(&t, &tm_buf);
#endif
            log_file_ << "[" << std::put_time(&tm_buf, "%H:%M:%S") << "] " << message << std::endl;
        }
    }

private:
    Logger() : enabled_(false) {} // Disabled by default

    ~Logger() {
        if (log_file_.is_open()) log_file_.close();
    }

    std::ofstream log_file_;
    std::mutex mutex_;
    bool enabled_;
};

// Macro checks enabled status BEFORE formatting string (saves CPU)
#define LOG_DEBUG(msg) { \
    if (Logger::GetInstance().IsEnabled()) { \
        std::stringstream ss; \
        ss << msg; \
        Logger::GetInstance().Log(ss.str()); \
    } \
}