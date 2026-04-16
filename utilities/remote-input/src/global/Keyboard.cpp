#include "Keyboard.h"

Keyboard::Keyboard() : VirtualInputDevice("libreconnect-keyboard") {}

Keyboard::~Keyboard() = default;

void Keyboard::PressAndReleaseKey(Key key) {
    PressKey(key);
    ReleaseKey(key);
}
