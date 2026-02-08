#pragma once
#include <iostream>
#include <fstream>
#include <string>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <ctime>

// --- STEP 1: DEFINE LOG CHANNELS ---
// Using powers of 2 for bitmasking allows combining multiple channels if needed.
enum LogChannel {
    LOG_NONE = 0,
    LOG_ENGINE = 1 << 0, // 1
    LOG_SPLIT = 1 << 1, // 2
    LOG_BUFFER = 1 << 2, // 4
    LOG_QUERY = 1 << 3, // 8
    LOG_ALL = 0xFFFF  // Enable everything
};

/**
 * Logger: Thread-safe Singleton with Channel-based filtering.
 */
class Logger {
public:
    static Logger& GetInstance() {
        static Logger instance;
        return instance;
    }

    /**
     * SetEnabledChannels
     * Sets the active logging filter. Only logs matching this mask will be written.
     * @param mask Bitmask of active channels (e.g., LOG_ENGINE | LOG_SPLIT).
     */
    void SetEnabledChannels(int mask) {
        std::lock_guard<std::mutex> lock(mutex_);
        active_channels_ = mask;

        if (active_channels_ != LOG_NONE && !log_file_.is_open()) {
            log_file_.open("debug.log", std::ios::out | std::ios::trunc);
        }
    }

    /**
     * IsChannelEnabled
     * Fast check to see if a specific channel is active before building strings.
     */
    bool IsChannelEnabled(LogChannel channel) const {
        return (active_channels_ & channel) != 0;
    }

    /**
     * Log
     * Writes message only if the channel is enabled.
     */
    void Log(const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (log_file_.is_open()) {
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

    /**
     * Helper to print the menu options to CLI.
     */
    static void PrintDebugMenu() {
        std::cout << "\n[DEBUG MODE DETECTED] Select Log Channel:\n";
        std::cout << "1 - Engine Core (Txn, Startup)\n";
        std::cout << "2 - B-Tree Splits (Structure Changes)\n";
        std::cout << "3 - Buffer Pool (LRU, Pins)\n";
        std::cout << "4 - Query Execution (Cursor, Search)\n";
        std::cout << "9 - ALL LOGS\n";
        std::cout << "0 - Disable Logging\n";
        std::cout << "Choice > ";
    }

private:
    Logger() : active_channels_(LOG_NONE) {}

    ~Logger() {
        if (log_file_.is_open()) log_file_.close();
    }

    std::ofstream log_file_;
    std::mutex mutex_;
    int active_channels_; // Stores the bitmask of enabled logs
};

// --- STEP 2: DEFINE TYPE-SPECIFIC MACROS ---

// General Debug (Legacy support, maps to ALL)
#define LOG_DEBUG(msg) { \
    if (Logger::GetInstance().IsChannelEnabled(LOG_ALL)) { \
        std::stringstream ss; ss << "[General] " << msg; \
        Logger::GetInstance().Log(ss.str()); \
    } \
}

// Channel: ENGINE
#define LOG_DEBUG_ENGINE(msg) { \
    if (Logger::GetInstance().IsChannelEnabled(LOG_ENGINE)) { \
        std::stringstream ss; ss << "[Engine] " << msg; \
        Logger::GetInstance().Log(ss.str()); \
    } \
}

// Channel: SPLIT
#define LOG_DEBUG_SPLIT(msg) { \
    if (Logger::GetInstance().IsChannelEnabled(LOG_SPLIT)) { \
        std::stringstream ss; ss << "[Split ] " << msg; \
        Logger::GetInstance().Log(ss.str()); \
    } \
}

// Channel: BUFFER
#define LOG_DEBUG_BUFFER(msg) { \
    if (Logger::GetInstance().IsChannelEnabled(LOG_BUFFER)) { \
        std::stringstream ss; ss << "[Buffer] " << msg; \
        Logger::GetInstance().Log(ss.str()); \
    } \
}

// Channel: QUERY
#define LOG_DEBUG_QUERY(msg) { \
    if (Logger::GetInstance().IsChannelEnabled(LOG_QUERY)) { \
        std::stringstream ss; ss << "[Query ] " << msg; \
        Logger::GetInstance().Log(ss.str()); \
    } \
}