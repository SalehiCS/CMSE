#include "time_utils.h"
#include <iostream>
#include <vector>

namespace cmse {
    namespace utils {

        int64_t TimeUtils::StringToTimestamp(const std::string& datetime_str) {
            std::tm tm = {};
            std::istringstream ss(datetime_str);

            // 1. Parse the standard seconds part: "2025-12-25 23:02:27"
            ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");

            if (ss.fail()) {
                return -1;
            }

            tm.tm_isdst = -1;
            std::time_t seconds = std::mktime(&tm);

            if (seconds == -1) {
                return -1;
            }

            // 2. Check for Microseconds (look for the '.')
            int64_t microseconds = 0;
            if (ss.peek() == '.') {
                char dot;
                ss >> dot; // Consume '.'

                std::string frac_str;
                ss >> frac_str; // Read the remaining numbers

                // Pad or truncate to ensure it represents microseconds (6 digits)
                // .5      -> 500000
                // .575408 -> 575408
                if (frac_str.length() > 6) {
                    frac_str = frac_str.substr(0, 6);
                }
                else {
                    while (frac_str.length() < 6) {
                        frac_str += '0';
                    }
                }

                // Safety: ensure purely numeric
                try {
                    microseconds = std::stoll(frac_str);
                }
                catch (...) {
                    microseconds = 0;
                }
            }

            // 3. Combine: (Seconds * 1,000,000) + Microseconds
            return (static_cast<int64_t>(seconds) * 1000000LL) + microseconds;
        }

        std::string TimeUtils::TimestampToString(int64_t timestamp_micros) {
            // Split back into Seconds and Microseconds
            std::time_t seconds = static_cast<std::time_t>(timestamp_micros / 1000000LL);
            int64_t micros = timestamp_micros % 1000000LL;

            std::tm tm_result;
#if defined(_WIN32) || defined(_WIN64)
            localtime_s(&tm_result, &seconds);
#else
            localtime_r(&seconds, &tm_result);
#endif

            std::ostringstream ss;
            // Format seconds part
            ss << std::put_time(&tm_result, "%Y-%m-%d %H:%M:%S");

            // Append Microseconds part (padded to 6 digits)
            // e.g., ".000500"
            ss << "." << std::setfill('0') << std::setw(6) << micros;

            return ss.str();
        }

    } // namespace utils
} // namespace cmse