#include "Keyboard.h"

#include <QGuiApplication>
#include <QMetaObject>

#include <iostream>
#include <thread>
#include <chrono>

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);

    std::thread testThread([]() {
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

            kb.PressAndReleaseKey(Key::Space); kb.TypeCharacter(0x1f642);

            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
            QMetaObject::invokeMethod(QGuiApplication::instance(), &QGuiApplication::quit);
        } catch (const std::exception& e) {
            std::cerr << "Failed to initialize virtual keyboard: " << e.what() << "\n";
            QMetaObject::invokeMethod(QGuiApplication::instance(), &QGuiApplication::quit);
        }
    });

    int result = app.exec();
    testThread.join();

    return result;
}
