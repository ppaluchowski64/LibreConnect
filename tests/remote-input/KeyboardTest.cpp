#include "Keyboard.h"

#include <iostream>
#include <thread>
#include <chrono>

int main() {
    try {
        Keyboard kb;

        std::cout << "You have 5 seconds to focus a text input (e.g. Notepad)...\n";
        std::this_thread::sleep_for(std::chrono::seconds(5));

        kb.PressKey(Key::LeftShift); kb.PressAndReleaseKey(Key::H); kb.ReleaseKey(Key::LeftShift);
        kb.PressAndReleaseKey(Key::E);
        kb.PressAndReleaseKey(Key::L);
        kb.PressAndReleaseKey(Key::L);
        kb.PressAndReleaseKey(Key::O);

        kb.PressAndReleaseKey(Key::Space);

        kb.PressKey(Key::LeftShift); kb.PressAndReleaseKey(Key::W); kb.ReleaseKey(Key::LeftShift);
        kb.PressAndReleaseKey(Key::O);
        kb.PressAndReleaseKey(Key::R);
        kb.PressAndReleaseKey(Key::L);
        kb.PressAndReleaseKey(Key::D);

        kb.PressKey(Key::LeftShift); kb.PressAndReleaseKey(Key::Num1); kb.ReleaseKey(Key::LeftShift);

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize virtual keyboard: " << e.what() << "\n";
        return 1;
    }
}
