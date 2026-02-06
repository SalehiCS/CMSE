#include <iostream>
#include <string>
#include "../src/utils/time_utils.h"

int main() {
    std::string input = "2026-01-25 09:31:28.062077";
    int64_t ts = cmse::utils::TimeUtils::StringToTimestamp(input);
    std::cout << "Microseconds: " << ts << std::endl;
    

    std::string recovered = cmse::utils::TimeUtils::TimestampToString(ts);
    std::cout << "Recovered: " << recovered << std::endl;
    

    return 0;
}