#include "Keyboard.h"

#include <iostream>
#include <thread>
#include <chrono>

#ifdef __linux__
    #include <linux/input-event-codes.h>
#endif

#ifdef _WIN32
    #include <Windows.h>
#endif

#ifdef __APPLE__
    #include <Carbon/Carbon.h>
#endif

int main() {
    try {
        Keyboard kb;

        std::cout << "You have 5 seconds to focus a text input (e.g. Notepad)...\n";
        std::this_thread::sleep_for(std::chrono::seconds(5));

        #ifdef __linux__

            kb.PressKey(KEY_LEFTSHIFT); kb.PressAndReleaseKey(KEY_H); kb.ReleaseKey(KEY_LEFTSHIFT);
            kb.PressAndReleaseKey(KEY_E);
            kb.PressAndReleaseKey(KEY_L);
            kb.PressAndReleaseKey(KEY_L);
            kb.PressAndReleaseKey(KEY_O);
            kb.PressAndReleaseKey(KEY_SPACE);
            kb.PressKey(KEY_LEFTSHIFT); kb.PressAndReleaseKey(KEY_W); kb.ReleaseKey(KEY_LEFTSHIFT);
            kb.PressAndReleaseKey(KEY_O);
            kb.PressAndReleaseKey(KEY_R);
            kb.PressAndReleaseKey(KEY_L);
            kb.PressAndReleaseKey(KEY_D);
            kb.PressKey(KEY_LEFTSHIFT); kb.PressAndReleaseKey(KEY_1); kb.ReleaseKey(KEY_LEFTSHIFT);

        #elif __APPLE__

            kb.PressKey(kVK_Shift); kb.PressAndReleaseKey(kVK_ANSI_H); kb.ReleaseKey(kVK_Shift);
            kb.PressAndReleaseKey(kVK_ANSI_E);
            kb.PressAndReleaseKey(kVK_ANSI_L);
            kb.PressAndReleaseKey(kVK_ANSI_L);
            kb.PressAndReleaseKey(kVK_ANSI_O);
            kb.PressAndReleaseKey(kVK_Space);
            kb.PressKey(kVK_Shift); kb.PressAndReleaseKey(kVK_ANSI_W); kb.ReleaseKey(kVK_Shift);
            kb.PressAndReleaseKey(kVK_ANSI_O);
            kb.PressAndReleaseKey(kVK_ANSI_R);
            kb.PressAndReleaseKey(kVK_ANSI_L);
            kb.PressAndReleaseKey(kVK_ANSI_D);
            kb.PressKey(kVK_Shift); kb.PressAndReleaseKey(kVK_ANSI_1); kb.ReleaseKey(kVK_Shift);

        #elif _WIN32

            kb.PressKey(VK_SHIFT); kb.PressAndReleaseKey('H'); kb.ReleaseKey(VK_SHIFT);
            kb.PressAndReleaseKey('E');
            kb.PressAndReleaseKey('L');
            kb.PressAndReleaseKey('L');
            kb.PressAndReleaseKey('O');
            kb.PressAndReleaseKey(VK_SPACE);
            kb.PressKey(VK_SHIFT); kb.PressAndReleaseKey('W'); kb.ReleaseKey(VK_SHIFT);
            kb.PressAndReleaseKey('O');
            kb.PressAndReleaseKey('R');
            kb.PressAndReleaseKey('L');
            kb.PressAndReleaseKey('D');
            kb.PressKey(VK_SHIFT); kb.PressAndReleaseKey('1'); kb.ReleaseKey(VK_SHIFT);

        #else

            std::cerr << "Not supported platform!\n";
            return 1;

        #endif

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize virtual keyboard: " << e.what() << "\n";
        return 1;
    }
}
