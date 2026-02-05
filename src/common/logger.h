																#pragma once
#include <iostream>
#include <fstream>
#include <string>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <ctime>

// Singleton Logger Class
class Logger {
public:
    static Logger& GetInstance() {
        static Logger instance;
        return instance;
    }

    void Log(const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (log_file_.is_open()) {
            // Get current time
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
    Logger() {
        // Appends to debug.log in the execution directory
        log_file_.open("debug.log", std::ios::out | std::ios::app);
    }
    
    ~Logger() {
        if (log_file_.is_open()) log_file_.close();
    }

    std::ofstream log_file_;
    std::mutex mutex_;
};

// Helper Macro for easy logging
#define LOG_DEBUG(msg) { \
    std::stringstream ss; \
    ss << msg; \
    Logger::GetInstance().Log(ss.str()); \
}