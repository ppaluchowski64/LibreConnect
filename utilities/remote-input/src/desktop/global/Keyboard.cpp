#include "Keyboard.h"

#include "NativeKeyboardMap.h"
#include "TextClipboard.h"

#include <string>
#include <thread>
#include <chrono>
#include <mutex>

Keyboard::Keyboard() : VirtualInputDevice("libreconnect-keyboard") {}
Keyboard::~Keyboard() = default;

void Keyboard::PressKey(Key key) {
    int nativeCode = GetNativeKeyCode(key);

    if (nativeCode == -1)
        return;

    EmitNativeKeyPress(nativeCode);
}

void Keyboard::ReleaseKey(Key key) {
    int nativeCode = GetNativeKeyCode(key);

    if (nativeCode == -1)
        return;

    EmitNativeKeyRelease(nativeCode);
}

void Keyboard::PressAndReleaseKey(Key key) {
    PressKey(key);
    ReleaseKey(key);
}

void Keyboard::TypeCharacter(uint32_t unicodePoint) {
    std::string utf8_char;

    if (unicodePoint <= 0x7F) {
        utf8_char += static_cast<char>(unicodePoint);
    } else if (unicodePoint <= 0x7FF) {
        utf8_char += static_cast<char>(0xC0 | ((unicodePoint >> 6) & 0x1F));
        utf8_char += static_cast<char>(0x80 | (unicodePoint & 0x3F));
    } else if (unicodePoint <= 0xFFFF) {
        utf8_char += static_cast<char>(0xE0 | ((unicodePoint >> 12) & 0x0F));
        utf8_char += static_cast<char>(0x80 | ((unicodePoint >> 6) & 0x3F));
        utf8_char += static_cast<char>(0x80 | (unicodePoint & 0x3F));
    } else if (unicodePoint <= 0x10FFFF) {
        utf8_char += static_cast<char>(0xF0 | ((unicodePoint >> 18) & 0x07));
        utf8_char += static_cast<char>(0x80 | ((unicodePoint >> 12) & 0x3F));
        utf8_char += static_cast<char>(0x80 | ((unicodePoint >> 6) & 0x3F));
        utf8_char += static_cast<char>(0x80 | (unicodePoint & 0x3F));
    } else {
        return;
    }

    static std::string originalClipboard;
    static bool isTyping = false;
    static int typeSequence = 0;
    static std::mutex typingMutex;

    std::string currentClipboard = TextClipboard::Get();

    const int capturedSequence = [&]() {
        std::lock_guard<std::mutex> lock(typingMutex);
        typeSequence++;

        if (!isTyping) {
            originalClipboard = currentClipboard;
            isTyping = true;
        }

        return typeSequence;
    }();

    std::thread([this, utf8_char, capturedSequence]() {
        TextClipboard::Set(utf8_char);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        #ifdef __APPLE__
                PressKey(Key::LeftSuper);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                PressKey(Key::V);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                ReleaseKey(Key::V);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                ReleaseKey(Key::LeftSuper);
        #else
                PressKey(Key::LeftControl);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                PressKey(Key::V);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                ReleaseKey(Key::V);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                ReleaseKey(Key::LeftControl);
        #endif

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        {
            std::lock_guard<std::mutex> lock(typingMutex);
            if (typeSequence == capturedSequence) {
                TextClipboard::Set(originalClipboard);
                isTyping = false;
            }
        }
    }).detach();
}
