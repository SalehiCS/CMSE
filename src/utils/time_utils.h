#pragma once

#include <string>
#include <cstdint>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace cmse {
    namespace utils {

        class TimeUtils {
        public:
            /**
             * Converts Date String -> Unix Timestamp in MICROSECONDS.
             * Supported Formats:
             * 1. "YYYY-MM-DD HH:MM:SS"
             * 2. "YYYY-MM-DD HH:MM:SS.uuuuuu" (Microseconds)
             * * Example: "2025-12-25 23:02:27.575408" -> 1766703747575408
             */
            static int64_t StringToTimestamp(const std::string& datetime_str);

            /**
             * Converts Timestamp (Microseconds) -> "YYYY-MM-DD HH:MM:SS.uuuuuu"
             */
            static std::string TimestampToString(int64_t timestamp_micros);
        };

    } // namespace utils
} // namespace cmse