#include "Keyboard.h"

int GetNativeKey(Key key);

Keyboard::Keyboard() : VirtualInputDevice("libreconnect-keyboard") {}

Keyboard::~Keyboard() = default;

void Keyboard::PressKey(Key key) {
    int nativeKeyCode = GetNativeKey(key);
    if (nativeKeyCode == -1) return;
    EmitNativeKeyPress(nativeKeyCode);
}

void Keyboard::ReleaseKey(Key key) {
    int nativeKeyCode = GetNativeKey(key);
    if (nativeKeyCode == -1) return;
    EmitNativeKeyRelease(nativeKeyCode);
}

void Keyboard::PressAndReleaseKey(Key key) {
    PressKey(key);
    ReleaseKey(key);
}
