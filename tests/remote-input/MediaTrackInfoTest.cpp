#include "MediaTrackInfo.h"

#include <string>
#include <iostream>

int main() {
    std::cout << "Media Track Info Test\n\n";
    std::string input;

    while (true) {
        std::cout << "[ENTER] Get info | [p] Set position | [q] Quit: ";

        if (!std::getline(std::cin, input) || input == "q")
            break;

        if (input == "p") {
            std::cout << "Enter new position in seconds: ";
            std::string secInput;

            if (std::getline(std::cin, secInput)) {
                try {
                    MediaTrackInfo::SetPosition(std::stod(secInput));
                    std::cout << "Position updated.\n\n";
                } catch (...) {
                    std::cout << "Invalid input.\n\n";
                }
            }

            continue;
        }

        if (auto track = MediaTrackInfo::GetCurrentTrack()) {
            std::cout << "Title:   " << track->title << '\n'
                      << "Artist:  " << track->artist << '\n'
                      << "Album:   " << track->album << '\n'
                      << "Time:    " << track->position << " / " << track->duration << "s\n"
                      << "State:   " << (track->playing ? "Playing" : "Paused") << '\n';

            if (MediaTrackInfo::SaveCoverToFile(*track, "cover.jpg"))
                std::cout << "[Cover saved to cover.jpg: " << track->cover.size() << " bytes]\n\n";
            else
                std::cout << "[No cover available]\n\n";
        } else {
            std::cout << "No active media detected.\n\n";
        }
    }

    return 0;
}
