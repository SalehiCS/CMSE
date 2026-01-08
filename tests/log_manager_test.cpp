#include "../src/utils/log_manager.h"
#include "../src/common/types.h"
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <random>
#include <chrono>
#include <cstring>
#include <cstdio>  // For std::remove
#include <cstdlib> // For exit()

// Use namespaces
using namespace cmse;
using namespace cmse::utils;

// --- Helper Functions for Test Assertions ---
void assert_eq(long long actual, long long expected, const std::string& message) {
    if (actual != expected) {
        std::cerr << "[FAIL] " << message
            << " | Expected: " << expected
            << ", Actual: " << actual << std::endl;
        exit(1);
    }
}

void assert_true(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << std::endl;
        exit(1);
    }
}

// --- Data Pools ---
// 50 real-world resource names
const std::vector<std::string> RESOURCE_NAMES_POOL = {
    "Server-Alpha", "Server-Beta", "Gateway-Main", "Gateway-Backup", "Printer-HR",
    "Printer-Lobby", "Laptop-CEO", "Laptop-Dev1", "Laptop-Dev2", "Switch-Core",
    "Switch-Edge", "Router-Wan", "Firewall-Ext", "LoadBalancer-1", "Database-Pri",
    "Database-Sec", "Cache-Redis", "Queue-Kafka", "Desktop-Admin", "Desktop-Recp",
    "Camera-Front", "Camera-Back", "Sensor-Temp1", "Sensor-Temp2", "AccessPoint-1",
    "AccessPoint-2", "Storage-SAN", "Storage-NAS", "Backup-Tape", "Cloud-Connector",
    "Kiosk-Main", "Tablet-Sales", "Phone-VoIP1", "Phone-VoIP2", "Monitor-Sec",
    "Projector-Conf", "Scanner-Doc", "Ups-Main", "Generator-Diesel", "Cooling-Unit1",
    "Cooling-Unit2", "Badge-Reader", "Alarm-Panel", "Door-Lock", "Light-Controller",
    "Thermostat-Main", "Speaker-System", "Mic-Conf", "Whiteboard-Smart", "Workstation-AI"
};

// 50 real-world event types
const std::vector<std::string> EVENT_TYPES_POOL = {
    "LOGIN_SUCCESS", "LOGIN_FAILED", "LOGOUT", "DISK_FULL", "CPU_HIGH",
    "MEM_LOW", "NET_TIMEOUT", "NET_CONNECTED", "NET_DISCONN", "FILE_READ",
    "FILE_WRITE", "FILE_DELETE", "PERM_DENIED", "PROCESS_START", "PROCESS_KILL",
    "SERVICE_UP", "SERVICE_DOWN", "UPDATE_CHECK", "UPDATE_DONE", "UPDATE_FAIL",
    "BACKUP_START", "BACKUP_DONE", "RESTORE_START", "RESTORE_DONE", "CONFIG_CHANGE",
    "USER_ADDED", "USER_REMOVED", "PASS_RESET", "AUTH_TOKEN", "API_REQUEST",
    "API_ERROR", "DB_QUERY", "DB_COMMIT", "DB_ROLLBACK", "DB_CONNECT",
    "CACHE_HIT", "CACHE_MISS", "QUEUE_PUSH", "QUEUE_POP", "JOB_SCHEDULED",
    "JOB_COMPLETED", "JOB_FAILED", "MAIL_SENT", "MAIL_BOUNCED", "SMS_SENT",
    "ALERT_TRIGGER", "ALERT_RESOLVED", "SYSTEM_BOOT", "SYSTEM_HALT", "HEARTBEAT"
};

int main() {
    std::cout << "Running LogManager Tester (Custom Generation)..." << std::endl;

    // --- Configuration ---
    const int TEST_COUNT = 10000;
    const int64_t START_ID = 1000;
    const int ID_RANGE = 50; // IDs will be [1000, 1049]
    const int TIME_WINDOW_MS = 24 * 60 * 60 * 1000; // 24 Hours
    const std::string TEST_FILENAME = "test_custom_logs.csv";

    // --- Random Setup ---
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> pool_idx_dist(0, 49); // 0 to 49
    std::uniform_int_distribution<long long> time_dist(0, TIME_WINDOW_MS);

    // --- Step 1: Establish Correspondence (ID <-> Name) ---
    // We pre-assign each of the 50 IDs to one of the 50 Names to ensure consistency.
    std::map<int64_t, std::string> id_to_name_map;

    // Shuffle the names to make assignment random but fixed for this run
    std::vector<std::string> shuffled_names = RESOURCE_NAMES_POOL;
    // (Optional: std::shuffle(shuffled_names.begin(), shuffled_names.end(), gen); )

    for (int i = 0; i < ID_RANGE; ++i) {
        id_to_name_map[START_ID + i] = shuffled_names[i];
    }

    // --- Step 2: Generate 10,000 Random Records ---
    std::vector<LogRecord> generated_logs;
    generated_logs.reserve(TEST_COUNT);
    auto base_time = std::chrono::system_clock::now();

    for (int i = 0; i < TEST_COUNT; ++i) {
        LogRecord record;

        // A. Random Time
        record.timestamp = base_time + std::chrono::milliseconds(time_dist(gen));

        // B. Random Resource (Pick one of the 50 valid ID-Name pairs)
        int offset = pool_idx_dist(gen); // 0-49
        int64_t chosen_id = START_ID + offset;
        std::string chosen_name = id_to_name_map[chosen_id];

        record.resource_id = chosen_id;
        // Secure copy
        strncpy_s(record.resource_name, sizeof(record.resource_name), chosen_name.c_str(), _TRUNCATE);

        // C. Random Event
        std::string event = EVENT_TYPES_POOL[pool_idx_dist(gen)];
        strncpy_s(record.event_type, sizeof(record.event_type), event.c_str(), _TRUNCATE);

        generated_logs.push_back(record);
    }

    std::cout << "[INFO] Generated " << generated_logs.size() << " records." << std::endl;

    // --- Step 3: Write to Disk (Using LogManager) ---
    LogManager::writeLogsToFile(generated_logs, TEST_FILENAME);
    std::cout << "[INFO] Written to " << TEST_FILENAME << std::endl;

    // --- Step 4: Read and Verify (Using LogManager) ---
    std::vector<LogRecord> read_logs = LogManager::readLogsFromFile(TEST_FILENAME);

    // Assertion 1: Count
    assert_eq(read_logs.size(), TEST_COUNT, "Read count mismatch");

    // Assertion 2: Consistency Check on Read Data
    // We iterate through the file we just read to ensure the ID<->Name mapping is preserved.
    for (const auto& log : read_logs) {
        // Check if ID is in valid range
        assert_true(log.resource_id >= START_ID && log.resource_id < START_ID + ID_RANGE,
            "Read Log ID out of range");

        // Check if Name matches the expected name for this ID
        std::string expected_name = id_to_name_map[log.resource_id];
        std::string actual_name = log.resource_name;

        if (expected_name != actual_name) {
            std::cerr << "[FAIL] Consistency Error for ID " << log.resource_id
                << ". Expected: " << expected_name
                << ", Got: " << actual_name << std::endl;
            exit(1);
        }
    }

    std::cout << "[OK] Data Integrity & Consistency Verified." << std::endl;

    // --- Preview ---
    std::cout << "\n--- PREVIEW: First 10 Custom Logs ---" << std::endl;
    for (int i = 0; i < 10; ++i) {
        std::cout << "[" << i << "] " << read_logs[i].toString() << std::endl;
    }

    // Do not remove file, as requested
    return 0;
}