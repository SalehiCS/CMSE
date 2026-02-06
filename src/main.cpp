#include <iostream>
#include <string>
#include <limits> // Required for std::numeric_limits to handle stream flushing
#include "cli/cms_engine.h"
#include "common/logger.h"

/**
 * PrintHelp
 * Displays the primary navigation menu for the storage engine's CLI.
 */
void PrintHelp() {
    std::cout << "\nCommands:" << std::endl;
    std::cout << "  load <path>   : Import a raw log file into the indexed binary storage" << std::endl;
    std::cout << "  query         : Enter the interactive query mode (SQL-like filters)" << std::endl;
    std::cout << "  help          : Show this help menu" << std::endl;
    std::cout << "  exit          : Gracefully shut down the engine and close files" << std::endl;
}

/**
 * main
 * Application entry point. Handles boot-time arguments and the primary REPL loop.
 */
int main(int argc, char* argv[]) {
    // 1. ARGUMENT PARSING: Check for global flags like -g (debug mode)
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-g") {
            // Enable the internal tracing logger; output is directed to debug.log
            Logger::GetInstance().SetEnabled(true);
            std::cout << "[DEBUG] Logging Enabled (debug.log)" << std::endl;
        }
    }

    // --- VISUAL HEADER ---
    std::cout << "========================================" << std::endl;
    std::cout << "   CMSE: Centralized Monitoring System  " << std::endl;
    std::cout << "   (C++ Custom Storage Engine v1.0)     " << std::endl;
    std::cout << "========================================" << std::endl;

    // 2. BOOTSTRAP: Immediate user guidance on startup
    PrintHelp();

    // 3. ENGINE INITIALIZATION: Creates/Opens 'main_storage.db' and '.meta' files
    cmse::CMSEngine engine("main_storage.db");

    std::string cmd;
    // --- PRIMARY INTERACTION LOOP (REPL) ---
    while (true) {
        std::cout << "\nCMSE> ";

        // Extract the first word of the user input (the command verb)
        // Note: This leaves the trailing newline character (\n) in the input buffer
        std::cin >> cmd;

        // EXIT COMMAND: Break the loop to trigger engine destructors
        if (cmd == "exit") {
            break;
        }
        // LOAD COMMAND: Initiate file ingestion
        else if (cmd == "load") {
            std::string path;
            std::cin >> path; // Extract the file path argument
            engine.LoadLogFile(path);
        }
        // QUERY COMMAND: Switch to the sub-REPL for data retrieval
        else if (cmd == "query") {
            // 4. BUFFER SANITIZATION:
            // Because 'std::cin >> cmd' left the '\n' in the buffer, 
            // the query engine's 'getline' would see an empty line immediately.
            // This ignores all remaining characters until the next newline.
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

            engine.RunQueryInteractive();
        }
        // HELP COMMAND: Redisplay the command menu
        else if (cmd == "help") {
            PrintHelp();
        }
        // ERROR HANDLING: Handle invalid or nonsensical user input
        else {
            std::cout << "Unknown command. ";
            PrintHelp();
            // Clear buffer to prevent "cascading errors" if user typed multiple words
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        }
    }

    // Graceful exit: CMSEngine destructor will flush buffer pools to disk here
    return 0;
}