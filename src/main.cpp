#include <iostream>
#include <string>
#include "cli/cms_engine.h"

void PrintHelp() {
    std::cout << "\nCommands:" << std::endl;
    std::cout << "  load <path>   : Import a log file (Resumes if previously loaded)" << std::endl;
    std::cout << "  query         : Enter query mode" << std::endl;
    std::cout << "  exit          : Quit" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "   CMSE: Centralized Monitoring System  " << std::endl;
    std::cout << "   (C++ Custom Storage Engine v1.0)     " << std::endl;
    std::cout << "========================================" << std::endl;

    std::string db_filename = "main_storage.db";
    cmse::CMSEngine engine(db_filename);

    std::string cmd;
    while (true) {
        std::cout << "\nCMSE> ";
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
            // Clear input buffer first
            std::string dummy;
            std::getline(std::cin, dummy);

            engine.RunQueryInteractive();
        }
        else {
            PrintHelp();
        }
    }

    return 0;
}