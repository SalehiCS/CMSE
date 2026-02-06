#include "time_utils.h"
#include <iostream>
#include <vector>

namespace cmse {
    namespace utils {

        /**
         * Converts a datetime string into a high-precision 64-bit microsecond timestamp.
         * Logic: Merges standard Unix time (seconds) with a manually parsed fractional component.
         */
        int64_t TimeUtils::StringToTimestamp(const std::string& datetime_str) {
            std::tm tm = {};                           // Zero-initialize time structure
            std::istringstream ss(datetime_str);       // Wrap input string for stream processing

            // 1. Parse the standard seconds part: "2025-12-25 23:02:27"
            // get_time handles the YMD HMS components but ignores trailing fractions.
            ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");

            if (ss.fail()) {
                return -1; // Indicate parsing failure if the primary format is not met
            }

            tm.tm_isdst = -1;                          // Attempt to auto-detect Daylight Savings Time
            std::time_t seconds = std::mktime(&tm);    // Convert broken-down time to seconds since epoch

            if (seconds == -1) {
                return -1; // mktime error sentinel
            }

            // 2. Check for Microseconds (look for the decimal point '.')
            int64_t microseconds = 0;                  // Initialize sub-second storage
            if (ss.peek() == '.') {                    // Inspect next character without extracting
                char dot;
                ss >> dot;                             // Extract and consume the '.' separator

                std::string frac_str;
                ss >> frac_str;                        // Extract the remaining numeric digits

                // Standardize fractional string to exactly 6 digits (microseconds)
                if (frac_str.length() > 6) {
                    // Truncate if precision exceeds microseconds (e.g., nanoseconds provided)
                    frac_str = frac_str.substr(0, 6);
                }
                else {
                    // Pad with trailing zeros if precision is lower (e.g., .5 -> .500000)
                    while (frac_str.length() < 6) {
                        frac_str += '0';
                    }
                }

                // Convert normalized fractional string to integer
                try {
                    microseconds = std::stoll(frac_str);
                }
                catch (...) {
                    microseconds = 0;                  // Fallback to zero on parsing error
                }
            }

            // 3. Combine: (Seconds scaled to millionth) + Microseconds
            // Final result is a monotonic-style 64-bit integer.
            return (static_cast<int64_t>(seconds) * 1000000LL) + microseconds;
        }

        /**
         * Converts a 64-bit microsecond timestamp back into a formatted datetime string.
         * Output Format: "YYYY-MM-DD HH:MM:SS.uuuuuu"
         */
        std::string TimeUtils::TimestampToString(int64_t timestamp_micros) {
            // Decompose the timestamp back into its two constituent parts
            std::time_t seconds = static_cast<std::time_t>(timestamp_micros / 1000000LL);
            int64_t micros = timestamp_micros % 1000000LL;

            std::tm tm_result;                         // Structure to hold local time breakdown
            // Cross-platform thread-safe localtime conversion
#if defined(_WIN32) || defined(_WIN64)
            localtime_s(&tm_result, &seconds);         // Windows implementation
#else
            localtime_r(&seconds, &tm_result);         // POSIX/Linux implementation
#endif

            std::ostringstream ss;                     // Builder for the final string

            // Format the base seconds part according to database standards
            ss << std::put_time(&tm_result, "%Y-%m-%d %H:%M:%S");

            // Append the microseconds component
            // Padded with '0' to ensure exactly 6 digits (e.g., 500 -> ".000500")
            ss << "." << std::setfill('0') << std::setw(6) << micros;

            return ss.str();                           // Return formatted string
        }

    } // namespace utils
} // namespace cmse