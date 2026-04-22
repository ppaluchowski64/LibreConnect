#include "SystemVolumeController.h"

#include <string>
#include <iostream>

int main() {
    std::cout << "Volume Controller Test\n";
    std::cout << "Current volume: " << SystemVolumeController::GetVolume() << "%\n\n";

    std::string input;
    while (true) {
        std::cout << "Set volume (0-100, q to quit): ";

        if (!std::getline(std::cin, input) || input == "q")
            break;

        try {
            SystemVolumeController::SetVolume(std::stoi(input));
            std::cout << "System reports: " << SystemVolumeController::GetVolume() << "%\n\n";
        } catch (...) {
            std::cout << "Invalid input.\n\n";
        }
    }

    return 0;
}
