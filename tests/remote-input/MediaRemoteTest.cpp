#include "MediaRemote.h"

#include <iostream>

int main() {
    try {
        MediaRemote remote;
        unsigned int choice;

        constexpr const char* menuPrompt = "\n[0] Play  [1] Next  [2] Prev  [3] Vol+  [4] Vol-  [5] Mute  [6] Exit\n> ";

        std::cout << "--- Media Remote Test ---\n";

        while (std::cout << menuPrompt && std::cin >> choice && choice != 6) {
            if (choice <= 5) {
                remote.ExecuteSignal(static_cast<MediaSignal>(choice));
                std::cout << "Signal sent.\n";
            } else {
                std::cout << "Out of range (0-5).\n";
            }
        }

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << '\n';
        return 1;
    }
}
