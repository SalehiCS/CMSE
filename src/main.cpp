#include <iostream>
#include <string>
#include <limits> // Required for std::numeric_limits
#include "cli/cms_engine.h"
#include "common/logger.h"

void PrintHelp() {
    std::cout << "\nCommands:" << std::endl;
    std::cout << "  load <path>   : Import a log file" << std::endl;
    std::cout << "  query         : Enter query mode" << std::endl;
    std::cout << "  help          : Show this help menu" << std::endl;
    std::cout << "  exit          : Quit" << std::endl;
}

int main() {
    // 1. Check for -g argument
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-g") {
            Logger::GetInstance().SetEnabled(true);
            std::cout << "[DEBUG] Logging Enabled (debug.log)" << std::endl;
        }
    }

    std::cout << "========================================" << std::endl;
    std::cout << "   CMSE: Centralized Monitoring System  " << std::endl;
    std::cout << "   (C++ Custom Storage Engine v1.0)     " << std::endl;
    std::cout << "========================================" << std::endl;

    // 1. Print Help immediately on startup
    PrintHelp();

    cmse::CMSEngine engine("main_storage.db");

    std::string cmd;
    while (true) {
        std::cout << "\nCMSE> ";
        // Reads the command word (e.g. "query"), leaving the \n in the buffer
        std::cin >> cmd;

        if (cmd == "exit") {
            break;
        }
        else if (cmd == "load") {
            std::string path;
            std::cin >> path;
            engine.LoadLogFile(path);
        }
        else if (cmd == "query") {
            // 2. Clear input buffer cleanly
            // This ignores everything up to the next newline.
            // It fixes the issue where the engine would read an empty line 
            // or force you to press Enter twice.
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

            engine.RunQueryInteractive();
        }
        else if (cmd == "help") {
            PrintHelp();
        }
        else {
            std::cout << "Unknown command. ";
            PrintHelp();
            // Clear buffer in case they typed "garbage with spaces"
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        }
    }

    return 0;
}