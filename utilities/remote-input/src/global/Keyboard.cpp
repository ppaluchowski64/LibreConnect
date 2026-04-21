#include "Keyboard.h"

#include "NativeKeyboardMap.h"

Keyboard::Keyboard() : VirtualInputDevice("libreconnect-keyboard") {}
Keyboard::~Keyboard() = default;

void Keyboard::PressKey(Key key) {
    int nativeCode = GetNativeKeyCode(key);
    if (nativeCode == -1) return;
    EmitNativeKeyPress(nativeCode);
}

void Keyboard::ReleaseKey(Key key) {
    int nativeCode = GetNativeKeyCode(key);
    if (nativeCode == -1) return;
    EmitNativeKeyRelease(nativeCode);
}

void Keyboard::PressAndReleaseKey(Key key) {
    PressKey(key);
    ReleaseKey(key);
}
